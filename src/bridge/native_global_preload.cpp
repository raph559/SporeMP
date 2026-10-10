#include "native_global_preload.h"
#include "native_global_observer.h"
#include <Windows.h>
#include <memory>

namespace sporemp {
namespace {
std::unique_ptr<network::Peer> transport;
network::PeerConfig selected;
uint64_t nonce = 0, sent = 0, next_renew = 0, created = 0;
DWORD pid = 0, thread = 0;
bool enabled = false, installed = false, pending = false, fenced = false;

bool live() {
    return transport && !fenced && transport->transport_active();
}

bool fail(std::string& error, const char* reason) {
    error = reason;
    fence_native_global_preload();
    return false;
}
} // namespace

bool initialize_native_global_preload(const network::PeerConfig& config, std::string& error) {
    if (transport) {
        return false;
    }
    if (!config.location.valid()) {
        return true; // Earlier single-location diagnostics.
    }
    selected = config;
    enabled = true;
    thread = GetCurrentThreadId();
    pid = GetCurrentProcessId();
    FILETIME start{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &start, &exit, &kernel, &user)) {
        return fail(error, "global_preload_process_identity");
    }
    created = (uint64_t(start.dwHighDateTime) << 32) | start.dwLowDateTime;
    NativeGlobalAdmissionContext pending_context;
    pending_context.role = static_cast<NativeGlobalAdmissionRole>(config.role);
    pending_context.process_id = pid;
    pending_context.process_created = created;
    if (!prepare_native_global_admission(pending_context)) {
        return fail(error, "global_preload_supervision_unverified");
    }
    transport = std::make_unique<network::Peer>();
    nonce = 1;
    sent = GetTickCount64();
    pending = true;
    if (!transport->start_preload(config, nonce, pid, created, error)) {
        fence_native_global_preload();
        return false;
    }
    // SDK post-initialization callback, never DllMain. Original global record
    // initialization starts before the first AppUpdate, so admission must finish
    // before returning to that startup path. TLS I/O stays on Peer's own thread.
    const auto deadline = sent + 10000;
    while (!installed && GetTickCount64() < deadline) {
        if (!update_native_global_preload(error)) {
            return false;
        }
        if (!installed) {
            Sleep(1);
        }
    }
    return installed ? true : fail(error, "global_preload_startup_timeout");
}

namespace {
// A grant must answer the pending request of this very process, for the selected location, slot and transfer.
bool grant_out_of_scope(const network::Packet& packet) {
    const auto& grant = packet.preload;
    return packet.role != selected.role || grant.nonce != nonce || !(grant.location == selected.location) ||
           grant.process_id != pid || grant.process_created != created ||
           grant.native_load_slot != selected.native_load_slot ||
           grant.maintenance_transfer != selected.maintenance_transfer ||
           (selected.role == network::Role::authority && grant.worker_generation != selected.worker_generation);
}

// The grant as the native guard takes it. Its lifetime counts from the moment the request was sent.
NativeGlobalAdmissionGrant native_grant(const network::PreloadFrame& grant) {
    NativeGlobalAdmissionGrant native;
    auto& context = native.context;
    context.role = static_cast<NativeGlobalAdmissionRole>(selected.role);
    context.universe = grant.universe;
    context.checkpoint = grant.checkpoint;
    context.checkpoint_revision = grant.checkpoint_revision;
    context.location = {grant.location.galaxy, grant.location.star, grant.location.planet, grant.location.area};
    context.worker = grant.worker;
    context.generation = grant.worker_generation;
    context.process_id = pid;
    context.process_created = created;
    native.nonce = nonce;
    native.sent_at_ms = sent;
    native.ttl_ms = grant.ttl_ms;
    native.producer_mask = grant.producer_mask;
    for (size_t i = 0; i < native.owners.size(); ++i) {
        native.owners[i] = {grant.owners[i].worker, grant.owners[i].generation};
    }
    return native;
}

// One grant packet: the first arms the native guard, every one installs its grant and schedules the renewal.
// False after fencing.
bool accept_grant(const network::Packet& packet, std::string& error) {
    if (grant_out_of_scope(packet)) {
        return fail(error, "global_preload_scope_mismatch");
    }
    const auto& grant = packet.preload;
    const NativeGlobalAdmissionGrant native = native_grant(grant);
    if (!installed) {
        const bool armed = selected.role == network::Role::authority ? arm_native_global_authority(native.context, live)
                                                                     : arm_native_global_reader(native.context, live);
        if (!armed) {
            return fail(error, "global_preload_native_guard_unavailable");
        }
    }
    if (!install_native_global_grant(native)) {
        return fail(error, "global_preload_grant_invalid");
    }
    installed = true;
    pending = false;
    next_renew = sent + grant.ttl_ms / 6;
    return true;
}

// After the packets of this update: the installed grant must still be live, a pending request must not be
// overdue, and a grant due for renewal is asked for again under the next nonce. False after fencing.
bool keep_grant_current(std::string& error) {
    const auto now = GetTickCount64();
    if (installed && !native_global_grant_ready()) {
        return fail(error, "global_preload_grant_expired");
    }
    if (pending && now - sent >= network::preload_ttl_ms) {
        return fail(error, "global_preload_timeout");
    }
    if (installed && !pending && now >= next_renew) {
        if (nonce == UINT64_MAX) {
            return fail(error, "global_preload_nonce_exhausted");
        }
        ++nonce;
        sent = now;
        pending = true;
        if (!transport->renew_preload(nonce)) {
            return fail(error, "global_preload_renew_failed");
        }
    }
    return true;
}
} // namespace

bool update_native_global_preload(std::string& error) {
    if (!enabled) {
        return true;
    }
    if (fenced || !transport || GetCurrentThreadId() != thread) {
        return fail(error, "global_preload_unavailable");
    }
    network::Event event;
    size_t count = 0;
    while (count++ < 16 && transport->poll(event)) {
        if (event.kind == network::EventKind::connected) {
            continue;
        }
        if (event.kind != network::EventKind::packet || event.packet.kind != network::Kind::preload_grant || !pending) {
            return fail(error, "global_preload_refused_or_disconnected");
        }
        if (!accept_grant(event.packet, error)) {
            return false;
        }
    }
    return keep_grant_current(error);
}

bool native_global_preload_ready() {
    return !enabled || (installed && !fenced && native_global_grant_ready());
}

void fence_native_global_preload() {
    fenced = true;
    if (installed) {
        revoke_native_global_grant("network_fence");
    }
    if (transport) {
        transport->request_stop();
    }
}

void dispose_native_global_preload() {
    fence_native_global_preload();
    if (transport) {
        transport->stop();
        transport.reset();
    }
}
} // namespace sporemp
