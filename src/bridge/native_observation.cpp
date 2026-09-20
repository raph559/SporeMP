#include "native_observation.h"
#include "observation.h"
#include "detour_transaction.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/App/IMessageManager.h>
#include <Spore/App/IGameModeManager.h>
#include <Spore/App/ICheatManager.h>
#include <Spore/ArgScript/ICommand.h>
#include <atomic>

namespace sporemp {
namespace {
ObservationTrace trace;
EntityTable entities;
std::atomic<bool> enabled{false};
bool attached = false, command_added = false;
uint64_t parent_action = 0, npc_ticks = 0, avatar_ticks = 0;
uint32_t previous_stage = UINT32_MAX;
ULONGLONG last_sample = 0;
App::IMessageManager* messages = nullptr;
App::ICheatManager* cheats = nullptr;

// Same x86 thiscall-to-fastcall adapter as the pinned SDK's CppRevEng.h.
// Every replacement calls its trampoline exactly once, without altering arguments/results.
using NounManager = Simulator::cGameNounManager;
using Noun = Simulator::cGameData;
using Creature = Simulator::cCreatureBase;
using Animal = Simulator::cCreatureAnimal;
using CreateFn = Noun* (__thiscall*)(NounManager*, uint32_t);
using DestroyFn = void (__thiscall*)(NounManager*, Noun*);
using AvatarFn = void (__thiscall*)(NounManager*, Animal*);
using JumpFn = bool (__thiscall*)(Creature*, int);
using LandFn = void (__thiscall*)(Creature*);
using AiFn = void (__thiscall*)(Animal*, float);
CreateFn create_original = nullptr;
DestroyFn destroy_original = nullptr;
AvatarFn avatar_original = nullptr;
JumpFn jump_original = nullptr;
LandFn land_original = nullptr;
AiFn npc_original = nullptr, avatar_ai_original = nullptr;

bool observing() noexcept { return enabled.load() && trace.on_engine_thread() && trace.healthy(); }

ObservedEntity identify(Noun* noun, ObservationKind kind = ObservationKind::entity_observed) noexcept {
    if (!noun) return {};
    auto old = entities.find(reinterpret_cast<uintptr_t>(noun));
    auto token = entities.observe(reinterpret_cast<uintptr_t>(noun));
    if (!token.id) { trace.emit({ObservationKind::overflow}); return {}; }
    if (!old.id || kind == ObservationKind::entity_created) {
        Observation event{kind}; event.entity = token.id;
        // Only live callback arguments/results are read, never a stored registry address.
        event.native_id = noun->mID; event.political_id = noun->mPoliticalID;
        trace.emit(event);
    }
    return token;
}
Observation creature_state(Creature* creature, ObservationKind kind) noexcept {
    Observation event{kind};
    if (!creature) return event;
    event.entity = identify(static_cast<Noun*>(creature)).id;
    event.mode_id = Simulator::GetGameModeID();
    event.native_id = creature->mID; event.political_id = creature->mPoliticalID;
    event.has_state = true; event.health = creature->mHealthPoints;
    event.energy = creature->mEnergy; event.hunger = creature->mHunger; event.dead = creature->mbDead;
    return event;
}
Noun* __fastcall create_hook(NounManager* self, void*, uint32_t type) {
    Noun* noun = create_original(self, type);
    if (observing() && noun) {
        auto token = identify(noun, ObservationKind::entity_created);
        // The type passed into CreateInstance is a noun type, not a guessed dynamic Cast ID.
        Observation event{ObservationKind::entity_observed}; event.entity = token.id; event.type_id = type;
        trace.emit(event);
    }
    return noun;
}
void __fastcall destroy_hook(NounManager* self, void*, Noun* noun) {
    uintptr_t key = reinterpret_cast<uintptr_t>(noun);
    const bool record = observing();
    if (record && noun) {
        identify(noun);
        auto token = entities.invalidate(key); // Fence before original destruction, including reentrant callbacks.
        Observation event{ObservationKind::entity_invalidated}; event.entity = token.id;
        trace.emit(event);
    }
    destroy_original(self, noun);
    if (record) entities.finish_destroy(key); // Key only. Never read noun after the native destroy call.
}
void __fastcall avatar_hook(NounManager* self, void*, Animal* animal) {
    // Capture the argument while valid; do not retain/dereference it after native SetAvatar.
    const bool record = observing();
    Observation event{ObservationKind::avatar_assigned};
    if (record) event.entity = identify(static_cast<Noun*>(animal)).id;
    avatar_original(self, animal);
    if (record) trace.emit(event); // Assignment invocation/return; sampled current avatar confirms actual state.
}
bool __fastcall jump_hook(Creature* self, void*, int energy_consumed) {
    if (!observing()) return jump_original(self, energy_consumed);
    Observation event = creature_state(self, ObservationKind::jump_enter);
    event.action = trace.next_action(); event.parent = parent_action; event.argument = energy_consumed;
    trace.emit(event);
    const uint64_t previous_parent = parent_action; parent_action = event.action;
    const bool result = jump_original(self, energy_consumed);
    parent_action = previous_parent;
    event.kind = ObservationKind::jump_return; event.has_state = false;
    event.result = result ? 1 : 0;
    trace.emit(event); // Native return value; no post-call dereference of possibly invalidated self.
    return result;
}
void __fastcall land_hook(Creature* self, void*) {
    const bool record = observing();
    Observation event{ObservationKind::jump_land};
    if (record) event = creature_state(self, ObservationKind::jump_land);
    land_original(self);
    if (record) trace.emit(event); // An observed landing, not an invented causal action ID.
}
void __fastcall npc_hook(Animal* self, void*, float delta) {
    if (observing()) ++npc_ticks;
    npc_original(self, delta);
}
void __fastcall avatar_ai_hook(Animal* self, void*, float delta) {
    if (observing()) ++avatar_ticks;
    avatar_ai_original(self, delta);
}
const NativeHook hooks[] = {
    {reinterpret_cast<void**>(&create_original), reinterpret_cast<void*>(create_hook)},
    {reinterpret_cast<void**>(&destroy_original), reinterpret_cast<void*>(destroy_hook)},
    {reinterpret_cast<void**>(&avatar_original), reinterpret_cast<void*>(avatar_hook)},
    {reinterpret_cast<void**>(&jump_original), reinterpret_cast<void*>(jump_hook)},
    {reinterpret_cast<void**>(&land_original), reinterpret_cast<void*>(land_hook)},
    {reinterpret_cast<void**>(&npc_original), reinterpret_cast<void*>(npc_hook)},
    {reinterpret_cast<void**>(&avatar_ai_original), reinterpret_cast<void*>(avatar_ai_hook)}
};

void sample() {
    uint32_t stage = Simulator::GetGameModeID();
    if (stage != previous_stage) {
        Observation event{ObservationKind::stage_observed}; event.mode_id = stage; trace.emit(event);
        previous_stage = stage;
    }
    // Cell has a separate pool/identity system; never reinterpret it as Creature nouns.
    if (stage == kGameCreature) {
        auto manager = NounManager::Get();
        if (manager && manager->GetAvatar()) {
            auto event = creature_state(manager->GetAvatar(), ObservationKind::avatar_state);
            event.mode_id = stage; trace.emit(event);
        }
    }
    Observation ai{ObservationKind::ai_summary}; ai.count = npc_ticks; ai.secondary_count = avatar_ticks;
    trace.emit(ai); npc_ticks = avatar_ticks = 0;
    trace.flush();
}
class Listener final : public App::IUnmanagedMessageListener {
public:
    bool HandleMessage(uint32_t id, void* data) override {
        if (!observing()) return false;
        if (id == App::kMsgAppUpdate) {
            if (GetTickCount64() - last_sample >= 1000) { last_sample = GetTickCount64(); sample(); }
        } else if (id == App::kMsgOnModeExit) {
            Observation event{ObservationKind::scene_exit};
            if (data) event.mode_id = static_cast<App::OnModeExitMessage*>(data)->GetPreviousModeID();
            trace.emit(event);
            entities.clear(); trace.advance_epoch(); // All old tokens become invalid; IDs remain monotonic.
        } else if (id == App::kMsgOnModeEnter) {
            Observation event{ObservationKind::scene_enter};
            if (data) event.mode_id = static_cast<App::OnModeEnterMessage*>(data)->GetModeID();
            trace.emit(event);
        }
        return false; // Observation must never consume a native message.
    }
} listener;
class SnapshotCommand final : public ArgScript::ICommand {
public:
    void ParseLine(const ArgScript::Line& line) override {
        line.GetArguments(0); // Read-only, fixed command; no arbitrary addresses or engine actions.
        if (!observing()) return;
        trace.emit({ObservationKind::diagnostic_snapshot}); sample();
        App::ConsolePrintF("SporeMP M02: observational probe, %llu records, scene epoch %llu, trace %s. Native qualification pending.",
            trace.sequence(), trace.epoch(), trace.healthy() ? "healthy" : "incomplete");
    }
};
constexpr uint32_t message_ids[] = {App::kMsgAppUpdate, App::kMsgOnModeExit, App::kMsgOnModeEnter};
}

void initialize_native_observation(const wchar_t* directory) {
    wchar_t option[32]{};
    DWORD length = GetEnvironmentVariableW(L"SPOREMP_M02_TRACE", option, _countof(option));
    if (length != 7 || wcscmp(option, L"observe") != 0) return;
    if (!trace.open(directory)) return;
    messages = App::IMessageManager::Get(); cheats = App::ICheatManager::Get();
    if (!messages || !cheats || cheats->GetCheat("sporemp_trace")) {
        Observation event{ObservationKind::hooks_failed}; event.result = ERROR_NOT_READY; trace.emit(event); trace.close(); return;
    }
    create_original = reinterpret_cast<CreateFn>(GetAddress(Simulator::cGameNounManager, CreateInstance));
    destroy_original = reinterpret_cast<DestroyFn>(GetAddress(Simulator::cGameNounManager, DestroyInstance));
    avatar_original = reinterpret_cast<AvatarFn>(GetAddress(Simulator::cGameNounManager, SetAvatar));
    jump_original = reinterpret_cast<JumpFn>(GetAddress(Simulator::cCreatureBase, DoJump));
    land_original = reinterpret_cast<LandFn>(GetAddress(Simulator::cCreatureAnimal, OnJumpLand));
    npc_original = reinterpret_cast<AiFn>(GetAddress(Simulator::cCreatureAnimal, NPCTickAI));
    avatar_ai_original = reinterpret_cast<AiFn>(GetAddress(Simulator::cCreatureAnimal, AvatarTickAI));
    LONG status = change_hooks(hooks, _countof(hooks), true);
    if (status != NO_ERROR) {
        Observation event{ObservationKind::hooks_failed}; event.result = status; trace.emit(event); trace.close(); return;
    }
    attached = true;
    for (uint32_t id : message_ids) messages->AddUnmanagedListener(&listener, id);
    cheats->AddCheat("sporemp_trace", new SnapshotCommand(), false);
    command_added = true;
    enabled = true;
    Observation event{ObservationKind::hooks_ready}; event.count = _countof(hooks); trace.emit(event);
    sample();
}
void dispose_native_observation() {
    if (!attached) return;
    if (!trace.on_engine_thread()) { enabled = false; return; } // Cannot safely call managers on an unknown thread.
    if (trace.healthy()) sample();
    enabled = false;
    for (uint32_t id : message_ids) {
        if (!messages->RemoveListener(&listener, id)) {
            Observation event{ObservationKind::hooks_failed}; event.result = ERROR_NOT_FOUND; trace.emit(event);
        }
    }
    if (command_added) cheats->RemoveCheat("sporemp_trace");
    LONG status = change_hooks(hooks, _countof(hooks), false);
    if (status != NO_ERROR) { Observation event{ObservationKind::hooks_failed}; event.result = status; trace.emit(event); }
    else attached = false;
    trace.close();
}
}
