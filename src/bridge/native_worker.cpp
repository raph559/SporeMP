#include "native_worker.h"
#include "native_actors.h"
#include "native_persistence.h"
#include "native_replica.h"
#include "native_network.h"
#include "native_content.h"
#include "native_editor_observer.h"
#include "../worker/pipe.h"
#include <Spore/App/IMessageManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <memory>
#include <cstdlib>

namespace sporemp {
namespace {
std::unique_ptr<worker::Pipe> channel;
worker::Generation generation{};
DWORD engine_thread = 0;
uint64_t sent = 0, received = 0, app_ticks = 0, last_status = 0;
bool listening = false, quitting = false;
App::IMessageManager* messages = nullptr;
worker::Result dispatch(const worker::Message& request) {
    using worker::Op; using worker::Result;
    auto status = native_actor_worker_status();
    if (request.op == Op::status) return Result::accepted;
    if (request.op == Op::shutdown) { quitting = true; return Result::accepted; }
    if (request.epoch != status.epoch) return Result::stale;
    if (native_persistence_busy()) return Result::busy;
    if (request.op == Op::inspect_creation || request.op == Op::import_creation) {
        // Explicit private developer probe; no normal-account or network entry.
        // This original loader may populate caches. Never run it in a replica.
        if (native_replica_is_client()) return Result::unavailable;
        if (request.op == Op::inspect_creation ? !worker::valid_creation_inspection(request) : !worker::valid_creation_import(request)) return Result::invalid;
        if ((status.values[0] != static_cast<uint64_t>(worker::Phase::menu) &&
             status.values[0] != static_cast<uint64_t>(worker::Phase::scene)) ||
            Simulator::IsLoadingGameMode()) return Result::busy;
        if (request.op == Op::import_creation)
            return import_native_content(request.values, request.sequence) ? Result::accepted : Result::failed;
        return inspect_native_content(static_cast<uint32_t>(request.values[0]),
            static_cast<uint32_t>(request.values[1]), static_cast<uint32_t>(request.values[2]),
            request.sequence) ? Result::accepted : Result::failed;
    }
    if (request.op == Op::replica) return native_replica_command(request);
    if (request.op == Op::network_action) return native_network_command(request);
    if (native_replica_is_client() && (request.op == Op::move || request.op == Op::jump || request.op == Op::stop)) {
        const auto network_result = native_network_command(request);
        if (network_result != Result::unavailable) return network_result;
    }
    // The isolated developer bootstrap may prepare one original M03 encounter
    // before its first baseline. This is never exportable/savable, and the role
    // gate closes permanently on arming. No gameplay command is admitted later.
    if (native_replica_is_client() && (!native_replica_bootstrap_open() ||
        (request.op != Op::load && request.op != Op::restore && request.op != Op::setup &&
         request.op != Op::duel))) return Result::unavailable;
    if (request.op == Op::load) {
        if (status.values[3] || status.values[4]) return Result::unavailable;
        return native_persistence_command(request, status);
    }
    if (request.op == Op::save) return native_persistence_command(request, status);
    if (request.op == Op::checkpoint) {
        for (auto scalar : request.values) if (scalar) return Result::invalid;
        NativeActorCheckpoint checkpoint;
        if (!snapshot_native_actor_checkpoint(checkpoint, request.sequence)) return Result::unavailable;
        return Result::accepted;
    }
    if (request.op == Op::restore) {
        for (size_t i = 6; i < request.values.size(); ++i) if (request.values[i]) return Result::invalid;
        if (native_persistence_snapshot().state != NativePersistenceState::loaded) return Result::unavailable;
        NativeActorCheckpoint checkpoint; checkpoint.epoch = request.epoch;
        for (size_t i = 0; i < checkpoint.actors.size(); ++i) {
            const auto at = i * 3;
            checkpoint.actors[i] = {static_cast<uint32_t>(request.values[at]), static_cast<uint32_t>(request.values[at] >> 32),
                static_cast<uint32_t>(request.values[at+1]), static_cast<uint32_t>(request.values[at+1] >> 32),
                static_cast<uint32_t>(request.values[at+2]), static_cast<uint32_t>(request.values[at+2] >> 32)};
        }
        return restore_native_actor_checkpoint(checkpoint, request.sequence) ? Result::accepted : Result::invalid;
    }
    return native_actor_worker_command(request);
}
class Listener final : public App::IUnmanagedMessageListener {
public:
    bool HandleMessage(uint32_t id, void*) override {
        if (id != App::kMsgAppUpdate || GetCurrentThreadId() != engine_thread || !channel) return false;
        ++app_ticks;
        if (channel->failed()) { PostQuitMessage(34); return false; }
        if (quitting && channel->writable()) { PostQuitMessage(0); return false; }
        // Advance a previously acknowledged persistence request only after its
        // bounded pipe write has completed. No caller treats that ack as saved.
        if (!quitting && channel->writable()) update_native_persistence(native_actor_worker_status());
        // The peer cannot make a game frame wait: one request per update, and
        // reserve the single bounded output slot before consuming that request.
        worker::Message request;
        if (channel->writable() && channel->receive(request)) {
            if (request.kind != worker::Kind::command || !worker::accept_sequence(request, generation, received)) {
                channel->close(); PostQuitMessage(34); return false;
            }
            const auto result = dispatch(request);
            auto reply = native_actor_worker_status();
            annotate_native_persistence(reply);
            reply.kind = worker::Kind::reply; reply.op = request.op;
            reply.generation = generation; reply.sequence = ++sent;
            reply.values[1] = app_ticks; reply.values[6] = request.sequence;
            if (quitting) reply.values[0] = static_cast<uint64_t>(worker::Phase::stopping);
            reply.values[7] = static_cast<uint64_t>(result);
            channel->send(reply);
        }
        const auto now = GetTickCount64();
        if (!quitting && now - last_status >= 250 && channel->writable()) {
            auto status = native_actor_worker_status();
            annotate_native_persistence(status);
            const auto projection = native_network_projection();
            status.values[6] = static_cast<uint64_t>(projection.state);
            status.values[7] = projection.baseline;
            status.generation = generation; status.sequence = ++sent; status.values[1] = app_ticks;
            channel->send(status); last_status = now;
        }
        return false;
    }
} listener;
}
void initialize_native_worker() {
    wchar_t value[64]{}, parent[32]{};
    // A normal-account network client has a role generation but no supervisor.
    // Its network listener initializes persistence on the same SDK callback.
    wchar_t net[2]{};
    if (GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", net, _countof(net)) &&
        !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", parent, _countof(parent))) return;
    if (!GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", value, _countof(value))) return;
    engine_thread = GetCurrentThreadId();
    if (!worker::generation_from_hex(value, generation) ||
        !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", parent, _countof(parent))) { PostQuitMessage(34); return; }
    wchar_t* end = nullptr;
    const auto pid = wcstoul(parent, &end, 10);
    if (!pid || !end || *end) { PostQuitMessage(34); return; }
    channel = std::make_unique<worker::Pipe>();
    if (!channel->client(worker::pipe_name(generation, false), pid)) { PostQuitMessage(34); return; }
    messages = App::IMessageManager::Get();
    if (!messages) { channel.reset(); PostQuitMessage(34); return; }
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    initialize_native_persistence(base, base + pe->OptionalHeader.SizeOfImage, engine_thread, native_actor_worker_event);
    initialize_native_content(base, pe->OptionalHeader.SizeOfImage, engine_thread, native_actor_worker_event);
    initialize_native_editor_observer(base, pe->OptionalHeader.SizeOfImage, engine_thread, native_actor_worker_event);
    messages->AddUnmanagedListener(&listener, App::kMsgAppUpdate); listening = true;
}
void dispose_native_worker() {
    dispose_native_editor_observer();
    dispose_native_content();
    dispose_native_persistence();
    if (listening && GetCurrentThreadId() == engine_thread) {
        messages->RemoveListener(&listener, App::kMsgAppUpdate); listening = false;
    }
    channel.reset();
}
}
