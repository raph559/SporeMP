#include "native_ui.h"
#include "native_worker.h"
#include "native_actors.h"
#include "native_persistence.h"
#include "native_player_transfer_probe.h"
#include "native_quiescence_probe.h"
#include "native_global_observer.h"
#include "native_award_context.h"
#include "native_replica.h"
#include "native_network.h"
#include "native_scene.h"
#include "native_content.h"
#include "native_editor_observer.h"
#include "../worker/pipe.h"
#include <Spore/App/IMessageManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <memory>
#include <cstdlib>
#include <string>
#include <intrin.h>

namespace sporemp {
namespace {
std::unique_ptr<worker::Pipe> channel;
worker::Generation generation{};
DWORD engine_thread = 0;
uint64_t sent = 0, received = 0, app_ticks = 0, last_status = 0;
bool listening = false, quitting = false;
App::IMessageManager* messages = nullptr;

using worker::Op;
using worker::Result;

// Whether every scalar of a request from `first` on is zero, as an operation that takes none (or fewer) requires.
bool values_zero_from(const worker::Message& request, size_t first) {
    for (size_t i = first; i < request.values.size(); ++i) {
        if (request.values[i]) {
            return false;
        }
    }
    return true;
}

// A developer probe that reads or pauses the world is refused in a game that runs a network session.
bool network_session_configured() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", value, _countof(value)) != 0;
}

bool in_phase(const worker::Message& status, worker::Phase phase) {
    return status.values[0] == static_cast<uint64_t>(phase);
}

Result quiescence_command(const worker::Message& request) {
    if (request.op == Op::resume_world) {
        if (!request.values[0] || !values_zero_from(request, 1)) {
            return Result::invalid;
        }
        if (network_session_configured()) {
            return Result::unavailable;
        }
        return request_native_quiescence_resume(request.values[0], request.epoch);
    }
    if (!values_zero_from(request, 0)) {
        return Result::invalid;
    }
    if (request.op == Op::quiesce) {
        return request_native_quiescence_pause(request.sequence, request.epoch, app_ticks);
    }
    return native_quiescence_probe_ready(request.epoch) ? Result::accepted : Result::unavailable;
}

// A player import that fails after the original objects exist cannot be undone: the process ends with a code that
// names the step.
Result import_player(const worker::Message& request) {
    if (!worker::valid_player_import(request)) {
        return Result::invalid;
    }
    if (!native_actor_import_probe_ready()) {
        return Result::unavailable;
    }
    NativePlayerTransferFixture approved;
    if (!load_native_player_transfer_fixture(request.values, approved)) {
        return Result::invalid;
    }
    const auto imported = import_native_player_transfer_probe(request.sequence, request.epoch, approved);
    if (!imported.imported) {
        return Result::failed;
    }
    if (!native_actor_adopt_import_probe(request.epoch, request.sequence, imported.player, imported.avatar,
                                         imported.herd)) {
        native_actor_worker_event("native_player_import_partial_failure",
                                  ",\"reason\":\"diagnostic_actor_adoption\",\"transfer_qualified\":false");
        TerminateProcess(GetCurrentProcess(), 0x4d11);
        return Result::failed;
    }
    if (imported.has_stage) {
        NativeAwardCheckpoint stage;
        stage.words = imported.stage_words;
        if (!restore_native_actor_awards(stage, request.epoch)) {
            native_actor_worker_event("native_player_import_partial_failure",
                                      ",\"reason\":\"source_stage_adoption\",\"transfer_qualified\":false");
            TerminateProcess(GetCurrentProcess(), 0x4d12);
            return Result::failed;
        }
        native_actor_worker_event(
            "native_player_import_stage_adoption",
            ",\"native_reader_defaults\":true,\"award_context_ready\":true,\"transfer_qualified\":false");
    }
    return Result::accepted;
}

Result export_player(const worker::Message& request, const worker::Message& status) {
    if (!values_zero_from(request, 0)) {
        return Result::invalid;
    }
    if (native_replica_is_client() || network_session_configured() || !in_phase(status, worker::Phase::scene) ||
        Simulator::IsLoadingGameMode()) {
        return Result::unavailable;
    }
    return export_native_player_graph(request.sequence, request.op != Op::export_player,
                                      request.op == Op::export_player_state)
               ? Result::accepted
               : Result::failed;
}

// Explicit private developer probe; no normal-account or network entry. This original loader may populate caches.
// It never runs in a replica.
Result creation_command(const worker::Message& request, const worker::Message& status) {
    if (native_replica_is_client()) {
        return Result::unavailable;
    }
    if (request.op == Op::inspect_creation ? !worker::valid_creation_inspection(request)
                                           : !worker::valid_creation_import(request)) {
        return Result::invalid;
    }
    if ((!in_phase(status, worker::Phase::menu) && !in_phase(status, worker::Phase::scene)) ||
        Simulator::IsLoadingGameMode()) {
        return Result::busy;
    }
    if (request.op == Op::import_creation) {
        return import_native_content(request.values, request.sequence) ? Result::accepted : Result::failed;
    }
    return inspect_native_content(static_cast<uint32_t>(request.values[0]), static_cast<uint32_t>(request.values[1]),
                                  static_cast<uint32_t>(request.values[2]), request.sequence)
               ? Result::accepted
               : Result::failed;
}

// Captures the scene as the game that plays one owner would publish it, records the counts, and releases the scene
// adapter again.
Result scene_probe(const worker::Message& request, const worker::Message& status) {
    const uint64_t owner = request.values[0];
    if (native_replica_is_client() || !in_phase(status, worker::Phase::scene) || (owner != 1 && owner != 2)) {
        return Result::unavailable;
    }
    if (!values_zero_from(request, 1)) {
        return Result::invalid;
    }
    if (!initialize_native_scene()) {
        return Result::unavailable;
    }
    NativeSceneFrame frame;
    const auto result = capture_native_scene(frame, static_cast<uint32_t>(owner));
    size_t owner_count = 0, foreign_count = 0;
    for (size_t i = 0; i < frame.count; ++i) {
        owner_count += frame.entities[i].owner == owner;
        foreign_count += frame.entities[i].owner != 0 && frame.entities[i].owner != owner;
    }
    native_actor_worker_event(
        "native_primary_scene_probe",
        (",\"owner\":" + std::to_string(owner) + ",\"result\":\"" + native_scene_result_name(result) + "\"" +
         ",\"entities\":" + std::to_string(frame.count) + ",\"owned\":" + std::to_string(owner_count) +
         ",\"foreign_owned\":" + std::to_string(foreign_count))
            .c_str());
    dispose_native_scene();
    return result == NativeSceneResult::accepted && owner_count == 1 && !foreign_count ? Result::accepted
                                                                                       : Result::unavailable;
}

Result checkpoint_command(const worker::Message& request) {
    if (!values_zero_from(request, 0)) {
        return Result::invalid;
    }
    NativeActorCheckpoint checkpoint;
    return snapshot_native_actor_checkpoint(checkpoint, request.sequence) ? Result::accepted : Result::unavailable;
}

// Two actors, three packed scalars each: every scalar carries two 32-bit words.
Result restore_command(const worker::Message& request) {
    if (!values_zero_from(request, 6)) {
        return Result::invalid;
    }
    if (native_persistence_snapshot().state != NativePersistenceState::loaded) {
        return Result::unavailable;
    }
    NativeActorCheckpoint checkpoint;
    checkpoint.epoch = request.epoch;
    for (size_t i = 0; i < checkpoint.actors.size(); ++i) {
        const auto at = i * 3;
        checkpoint.actors[i] = {
            static_cast<uint32_t>(request.values[at]),     static_cast<uint32_t>(request.values[at] >> 32),
            static_cast<uint32_t>(request.values[at + 1]), static_cast<uint32_t>(request.values[at + 1] >> 32),
            static_cast<uint32_t>(request.values[at + 2]), static_cast<uint32_t>(request.values[at + 2] >> 32)};
    }
    return restore_native_actor_checkpoint(checkpoint, request.sequence) ? Result::accepted : Result::invalid;
}

// One request from the supervisor. The order of the gates is the contract: status and shutdown always; then the
// scene epoch, a save in progress, a measured pause; then the operations a client may take; then the rest.
Result dispatch(const worker::Message& request) {
    const auto status = native_actor_worker_status();
    if (request.op == Op::status) {
        return Result::accepted;
    }
    if (request.op == Op::shutdown) {
        quitting = true;
        return Result::accepted;
    }
    if (request.epoch != status.epoch) {
        return Result::stale;
    }
    if (native_persistence_busy()) {
        return Result::busy;
    }
    if (request.op == Op::quiesce || request.op == Op::quiescence_status || request.op == Op::resume_world) {
        return quiescence_command(request);
    }
    // A measured pause admits only its read-only state export and explicit release. Do not let an unrelated
    // developer command mutate the snapshot.
    if (native_quiescence_probe_status(status.epoch).owns_pause &&
        (request.op != Op::export_player_state || !native_quiescence_probe_ready(status.epoch))) {
        return Result::busy;
    }
    switch (request.op) {
    case Op::import_player:
        return import_player(request);
    case Op::fence_inputs:
        return values_zero_from(request, 0) ? native_network_fence_authority_inputs(request.epoch, request.sequence)
                                            : Result::invalid;
    case Op::export_player:
    case Op::export_player_slice:
    case Op::export_player_state:
        return export_player(request, status);
    case Op::inspect_creation:
    case Op::import_creation:
        return creation_command(request, status);
    case Op::replica:
        return native_replica_command(request);
    case Op::network_action:
        return native_network_command(request);
    case Op::input:
        return native_network_input(request);
    case Op::click:
        return native_network_click(request);
    case Op::ui:
        return native_ui_read(request);
    case Op::point:
        return native_network_point(request);
    case Op::scene_probe:
        return scene_probe(request, status);
    default:
        break;
    }
    if (native_replica_is_client() && (request.op == Op::move || request.op == Op::jump || request.op == Op::stop)) {
        const auto network_result = native_network_command(request);
        if (network_result != Result::unavailable) {
            return network_result;
        }
    }
    // The isolated developer bootstrap may prepare one original M03 encounter before its first baseline. This is
    // never exportable/savable, and the role gate closes permanently on arming. No gameplay command is admitted
    // later.
    if (native_replica_is_client() &&
        (!native_replica_bootstrap_open() ||
         (request.op != Op::load && request.op != Op::restore && request.op != Op::setup && request.op != Op::duel))) {
        return Result::unavailable;
    }
    switch (request.op) {
    case Op::load:
        return status.values[3] || status.values[4] ? Result::unavailable : native_persistence_command(request, status);
    case Op::save:
    case Op::save_probe:
    case Op::save_fault:
        return native_persistence_command(request, status);
    case Op::checkpoint:
        return checkpoint_command(request);
    case Op::restore:
        return restore_command(request);
    default:
        return native_actor_worker_command(request);
    }
}

class Listener final : public App::IUnmanagedMessageListener {
public:
    bool HandleMessage(uint32_t id, void*) override {
        if (id != App::kMsgAppUpdate || GetCurrentThreadId() != engine_thread || !channel) {
            return false;
        }
        ++app_ticks;
        if (channel->failed()) {
            PostQuitMessage(34);
            return false;
        }
        if (quitting && channel->writable()) {
            PostQuitMessage(0);
            return false;
        }
        if (!quitting) {
            update_native_global_observer(app_ticks, native_actor_worker_status().epoch);
        }
        // Advance a previously acknowledged persistence request only after its
        // bounded pipe write has completed. No caller treats that ack as saved.
        if (!quitting && channel->writable()) {
            update_native_persistence(native_actor_worker_status());
        }
        if (!quitting) {
            update_native_quiescence_probe(app_ticks);
        }
        // The peer cannot make a game frame wait: one request per update, and
        // reserve the single bounded output slot before consuming that request.
        worker::Message request;
        if (channel->writable() && channel->receive(request)) {
            if (request.kind != worker::Kind::command || !worker::accept_sequence(request, generation, received)) {
                channel->close();
                PostQuitMessage(34);
                return false;
            }
            const auto result = dispatch(request);
            auto reply = native_actor_worker_status();
            annotate_native_persistence(reply);
            reply.kind = worker::Kind::reply;
            reply.op = request.op;
            reply.generation = generation;
            reply.sequence = ++sent;
            reply.values[1] = app_ticks;
            reply.values[6] = request.sequence;
            if (quitting) {
                reply.values[0] = static_cast<uint64_t>(worker::Phase::stopping);
            }
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
            status.generation = generation;
            status.sequence = ++sent;
            status.values[1] = app_ticks;
            channel->send(status);
            last_status = now;
        }
        return false;
    }
} listener;
} // namespace

uint64_t native_worker_app_updates() {
    return engine_thread == GetCurrentThreadId() ? app_ticks : 0;
}

void initialize_native_worker() {
    wchar_t value[64]{}, parent[32]{};
    // A normal-account network client has a role generation but no supervisor.
    // Its network listener initializes persistence on the same SDK callback.
    wchar_t net[2]{};
    if (GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", net, _countof(net)) &&
        !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", parent, _countof(parent))) {
        return;
    }
    if (!GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", value, _countof(value))) {
        return;
    }
    engine_thread = GetCurrentThreadId();
    if (!worker::generation_from_hex(value, generation) ||
        !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", parent, _countof(parent))) {
        PostQuitMessage(34);
        return;
    }
    wchar_t* end = nullptr;
    const auto pid = wcstoul(parent, &end, 10);
    if (!pid || !end || *end) {
        PostQuitMessage(34);
        return;
    }
    channel = std::make_unique<worker::Pipe>();
    if (!channel->client(worker::pipe_name(generation, false), pid)) {
        PostQuitMessage(34);
        return;
    }
    messages = App::IMessageManager::Get();
    if (!messages) {
        channel.reset();
        PostQuitMessage(34);
        return;
    }
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    // Initialize only after the common post-init identity guard, on this
    // supervised worker's engine thread, before any controlled original load.
    if (!initialize_native_global_observer(base, base + pe->OptionalHeader.SizeOfImage, engine_thread,
                                           native_actor_worker_event)) {
        native_actor_worker_event("native_global_observer_rejected", ",\"before_native_load\":true");
        PostQuitMessage(34);
        return;
    }
    initialize_native_persistence(base, base + pe->OptionalHeader.SizeOfImage, engine_thread,
                                  native_actor_worker_event);
    initialize_native_player_transfer_probe(base, base + pe->OptionalHeader.SizeOfImage, engine_thread,
                                            native_actor_worker_event);
    initialize_native_quiescence_probe(base, base + pe->OptionalHeader.SizeOfImage, engine_thread,
                                       native_actor_worker_event);
    initialize_native_content(base, pe->OptionalHeader.SizeOfImage, engine_thread, native_actor_worker_event);
    initialize_native_editor_observer(base, pe->OptionalHeader.SizeOfImage, engine_thread, native_actor_worker_event);
    messages->AddUnmanagedListener(&listener, App::kMsgAppUpdate);
    listening = true;
}

void dispose_native_worker() {
    dispose_native_quiescence_probe();
    dispose_native_editor_observer();
    dispose_native_content();
    dispose_native_persistence();
    if (!dispose_native_global_observer()) {
        native_actor_worker_event("native_global_observer_dispose_failed", ",\"module_unload_refused\":true");
        // A live detour cannot outlive its DLL. This path only exists in the
        // explicit supervised worker; preserve the trace and stop that process.
        TerminateProcess(GetCurrentProcess(), 0x4d13);
        __fastfail(7);
    }
    if (listening && GetCurrentThreadId() == engine_thread) {
        messages->RemoveListener(&listener, App::kMsgAppUpdate);
        listening = false;
    }
    channel.reset();
}
} // namespace sporemp
