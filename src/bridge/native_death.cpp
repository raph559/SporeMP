#include "native_death.h"
#include "native_death_abi.h"
#include "native_respawn_herd.h"
#include "native_award_abi.h"
#include "native_award_context.h"
#include "native_actors.h"
#include "native_network.h"
#include "native_replica.h"
#include "native_hatch.h"
#include "../network/death_wire.h"
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/Simulator/SubSystem/CinematicManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameTimeManager.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/Simulator/cNest.h>
#include <Spore/Swarm/IEffectsManager.h>
#include <Spore/Hash.h>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <array>

namespace sporemp {
namespace {
using Strategy = App::cCreatureModeStrategy;
using Animal = Simulator::cCreatureAnimal;
using Manager = Simulator::cGameNounManager;
using Play = NativeAwardAbi<Animal, void, void, void, void>::CinematicPlay;
NativeDeathAbi::Begin begin_original = nullptr;
NativeDeathAbi::Message message_original = nullptr;
Play play_original = nullptr;
uintptr_t base = 0, end = 0;
DWORD thread = 0;
bool (*thread_check)() = nullptr;
PlayerContextEvent sink = nullptr;
std::array<NativeDeathRequest, 3> pending{};
std::array<void*, 3> pending_handler{};
NativeDeathRequest presenting{}, secondary_observed{};
// D47B50..D47B67 is the complete inlined original constructor: vtable,
// refcount/state/timer/effect/reason. No native container or heap ownership.
alignas(4) std::array<uint32_t, 6> secondary_handler{};
bool secondary_tick = false, secondary_restart = false, secondary_blocked = false;
uint32_t restart_routes = 0;
NativeDeathAbi::Action handler_action = nullptr;
NativeDeathAbi::Tick handler_tick = nullptr;
NativeDeathAbi::Reset handler_reset = nullptr;
NativeDeathAbi::Destroy handler_destroy = nullptr;
NativeDeathAbi::Restart restart_original = nullptr;
NativeDeathAbi::ClearInteraction interaction_original = nullptr;
NativeDeathAbi::UiDeactivate ui_deactivate_original = nullptr;
NativeDeathAbi::UiActivate ui_activate_original = nullptr;
NativeDeathAbi::CinematicParameter parameter_original = nullptr;
uint64_t occurrence_counter = 0, client_occurrence = 0;
uint32_t client_cinematic = 0, client_reason = 0;
bool deferring = false, deferred = false, client_restart = false;
bool client_paused = false, client_resumed = false, client_cycle_failed = false;
IVisualEffectPtr client_fade;
constexpr uint32_t restart_message = 0x04f60b92;
// The vtable of the original death handler; handler_code_pinned checks its eight methods.
constexpr uint32_t handler_vtable_rva = 0x107aa90;
// Where the original keeps the names of its cinematics, each compared before use: "CRG_AvatarDied",
// "CRG_AvatarDied_Starvation" and, for the restart, "CRG_Fade_From_Black_Egg".
constexpr uint32_t combat_cinematic_rva = 0x107aab0;
constexpr uint32_t starvation_cinematic_rva = 0x107aadc;
constexpr uint32_t restart_cinematic_rva = 0x107accc;
// Three words of the original that the second player's restart saves before and puts back after, as the first
// player's restart position.
constexpr uint32_t restart_position_rva = 0x129e3b4;

bool on_thread() {
    return thread && GetCurrentThreadId() == thread && thread_check && thread_check();
}

bool readable(const void* p, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!p || !VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto address = reinterpret_cast<uintptr_t>(p), start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return address >= start && size <= info.RegionSize && address - start <= info.RegionSize - size;
}

template <size_t N>
bool prefix(uint32_t rva, const unsigned char (&value)[N]) {
    return rva < end - base && N <= end - base - rva && readable(reinterpret_cast<void*>(base + rva), N) &&
           !std::memcmp(reinterpret_cast<void*>(base + rva), value, N);
}

uint32_t word(const void* p, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, static_cast<const unsigned char*>(p) + offset, 4);
    return value;
}

void record(const char* event, const NativeDeathRequest& request, bool applied) {
    if (!sink || !on_thread()) {
        return;
    }
    char fields[300]{};
    sprintf_s(
        fields,
        ",\"occurrence\":%llu,\"death_epoch\":%llu,\"native_id\":%u,\"reason\":%u,\"native_slot\":%u,\"applied\":%s",
        request.occurrence, request.epoch, request.native_id, request.reason, request.native_slot,
        applied ? "true" : "false");
    sink(event, fields);
}

Animal* avatar() {
    auto manager = Manager::Get();
    if (!on_thread() || !manager || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    auto value = manager->GetAvatar();
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(value)) {
            if (!readable(value, sizeof(*value)) || noun.mbIsDestroyed || noun.field_20 || value->mbMarkedForDeletion ||
                *reinterpret_cast<const uintptr_t*>(value) != base + 0x106a080) {
                return nullptr;
            }
            return value;
        }
    }
    return nullptr;
}

bool handler_live(void* handler) {
    if (handler == secondary_handler.data()) {
        return secondary_handler[0] == base + handler_vtable_rva;
    }
    auto strategy = Strategy::Get();
    if (!strategy || !readable(handler, 0x18) || word(handler, 0) != base + handler_vtable_rva) {
        return false;
    }
    for (const auto& item : strategy->mActionHandlers) {
        if (item.get() == handler) {
            return true;
        }
    }
    return false;
}

uint32_t __fastcall cinematic_hook(void* receiver, void*, const char* name, uint32_t a, uint32_t b,
                                   const uint32_t* parameters, uint32_t c, uint32_t d) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (on_thread() && secondary_restart && caller == base + 0x941330 &&
        receiver == Simulator::cCinematicManager::Get() &&
        name == reinterpret_cast<const char*>(base + restart_cinematic_rva) && a == 1 && b == 1 && !parameters && !c &&
        !d) {
        restart_routes |= 8;
        return 0;
    }
    const auto expected_caller = presenting.reason == 5 ? 0x938e8b : 0x938eb8;
    const auto expected_name = presenting.reason == 5 ? starvation_cinematic_rva : combat_cinematic_rva;
    if (on_thread() && deferring && network::valid_death_reason(presenting.reason) &&
        caller == base + expected_caller && receiver == Simulator::cCinematicManager::Get() &&
        name == reinterpret_cast<const char*>(base + expected_name) && a == 1 && b == 1 && !parameters && !c && !d) {
        deferred = true;
        record("native_death_presentation_deferred", presenting, true);
        // D38E60 does not use the cinematic's request ID. Zero states that no
        // local cinematic was queued; the original handler/penalty still run.
        return 0;
    }
    return play_original(receiver, name, a, b, parameters, c, d);
}

void __fastcall begin_hook(void* receiver, void*, uint32_t reason) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint32_t slot = secondary_tick && receiver == secondary_handler.data() ? 2 : 1;
    // SDK GetAvatar reads mpAvatar directly and bypasses the native getter
    // detour. D401E0 uses B1FD00, so validate the same scoped native result.
    auto actor =
        on_thread()
            ? (slot == 2 ? reinterpret_cast<Animal*(__thiscall*)(Manager*)>(base + 0x71fd00)(Manager::Get()) : avatar())
            : nullptr;
    uint32_t owned = UINT32_MAX;
    if (!on_thread() || !native_network_death_authority() || caller != base + 0x94028f || !actor || !actor->mbDead ||
        actor->mHealthPoints != 0 || !handler_live(receiver) || word(receiver, 8) != 1 || pending[slot].occurrence ||
        !native_actor_owner_native_id(slot, owned) || owned != actor->mID) {
        if (slot == 2) {
            secondary_blocked = true;
            record("native_death_secondary_begin_rejected", secondary_observed, false);
            return;
        }
        begin_original(receiver, reason);
        return;
    }
    record("native_death_reason_observed", {native_actor_worker_status().epoch, 0, actor->mID, reason, slot}, false);
    if (!network::valid_death_reason(reason)) {
        begin_original(receiver, reason);
        return;
    }
    auto& request = pending[slot];
    request = {native_actor_worker_status().epoch, ++occurrence_counter, actor->mID, reason, slot};
    pending_handler[slot] = receiver;
    presenting = request;
    deferring = true;
    deferred = false;
    begin_original(receiver, reason);
    deferring = false;
    const bool good = deferred && handler_live(receiver) && word(receiver, 8) == 2;
    record("native_death_handler_return", request, good);
    presenting = {};
    if (!good) {
        request = {};
        pending_handler[slot] = nullptr;
        if (slot == 2) {
            secondary_blocked = true;
        }
    }
}

void __cdecl interaction_hook() {
    if (on_thread() && secondary_restart && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x9411b4) {
        restart_routes |= 1;
        return;
    }
    interaction_original();
}

void __fastcall ui_deactivate_hook(void* self, void*) {
    if (on_thread() && secondary_restart && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x9412e5) {
        if (native_secondary_hatch_selected(self)) {
            ui_deactivate_original(self);
            restart_routes |= 2;
        }
        return;
    }
    ui_deactivate_original(self);
}

void __fastcall ui_activate_hook(void* self, void*, uint32_t mode, uint32_t flags) {
    if (on_thread() && secondary_restart && mode == 4 && !flags &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x9412f5) {
        if (native_secondary_hatch_selected(self)) {
            ui_activate_original(self, mode, flags);
            restart_routes |= 4;
        }
        return;
    }
    ui_activate_original(self, mode, flags);
}

void __fastcall parameter_hook(void* self, void*, uint32_t key, const void* value, uint32_t request) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (on_thread() && secondary_restart && !request && (restart_routes & 8) &&
        self == Simulator::cCinematicManager::Get()) {
        if (caller == base + 0x941353 && key == 0x28930a2c) {
            restart_routes |= 16;
            return;
        }
        if (caller == base + 0x941463 && key == 0x927b68d9) {
            restart_routes |= 32;
            return;
        }
    }
    parameter_original(self, key, value, request);
}

Animal* owned_actor(uint32_t slot) {
    uint32_t id = UINT32_MAX;
    auto manager = Manager::Get();
    if (!on_thread() || !manager || !native_actor_owner_native_id(slot, id)) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (noun.mID == id && !noun.mbIsDestroyed && !noun.field_20 && readable(&noun, sizeof(Animal)) &&
            *reinterpret_cast<uintptr_t*>(&noun) == base + 0x106a080) {
            auto actor = static_cast<Animal*>(&noun);
            return actor->mbMarkedForDeletion ? nullptr : actor;
        }
    }
    return nullptr;
}

bool secondary_step(Animal* actor, void*) {
    if (secondary_blocked || !actor->mbDead || actor->mHealthPoints != 0 || secondary_handler[2] != 1) {
        return false;
    }
    const auto before = secondary_handler[2];
    secondary_tick = true;
    const bool result = handler_tick(secondary_handler.data(), 0);
    secondary_tick = false;
    if (before != secondary_handler[2]) {
        record("native_death_secondary_penalty_return", pending[2],
               result && secondary_handler[2] == 2 && pending[2].occurrence);
        sample_native_awards();
    }
    return result;
}

bool secondary_action(Animal* actor, void*) {
    if (!actor->mbDead || actor->mHealthPoints != 0 || secondary_handler[0]) {
        return false;
    }
    secondary_handler.fill(0);
    secondary_handler[0] = static_cast<uint32_t>(base + handler_vtable_rva);
    uintptr_t payload[] = {reinterpret_cast<uintptr_t>(actor), 0, secondary_observed.reason};
    return handler_action(secondary_handler.data(), 0xd3353639, payload) && secondary_handler[2] == 1;
}

bool secondary_herd_admitted(Animal* actor, Simulator::cHerd* herd) {
    auto manager = Manager::Get();
    uint32_t peer_id = UINT32_MAX;
    if (!manager || !actor || !herd || !native_actor_owner_native_id(1, peer_id)) {
        return false;
    }
    bool present = false;
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(herd)) {
            present = readable(herd, sizeof(*herd)) && !noun.mbIsDestroyed && !noun.field_20 &&
                      noun.GetNounID() == Simulator::cHerd::NOUN_ID && noun.mID != UINT32_MAX;
            break;
        }
    }
    if (!present) {
        return false;
    }
    unsigned owner_members = 0;
    std::vector<NativeRespawnMember> members;
    members.reserve(herd->mHerd.size());
    for (const auto& entry : herd->mHerd) {
        auto member = entry.get();
        NativeRespawnMember observed;
        if (member == actor) {
            ++owner_members;
        }
        for (auto& noun : manager->mNouns) {
            if (&noun == static_cast<Simulator::cGameData*>(member)) {
                if (readable(member, sizeof(*member)) && !noun.mbIsDestroyed && !noun.field_20 &&
                    *reinterpret_cast<const uintptr_t*>(member) == base + 0x106a080 && !member->mbMarkedForDeletion) {
                    observed = {member->mID, member->mHerd.get() == herd ? herd->mID : UINT32_MAX, true};
                }
                break;
            }
        }
        members.push_back(observed);
    }
    // D40FA0 calls C6B6E0, which fills only the native population deficit.
    // Preserve existing NPCs and resident corpses; require one current B and
    // no other authenticated player, retired member, wrong herd or duplicate.
    return owner_members == 1 && native_respawn_herd_admissible(actor->mID, peer_id, herd->mID, members);
}

// What a restart of the second player must leave as it was: the first player's avatar and herd, that avatar's
// state, the selected cinematic request and the restart position the original keeps for the first player.
struct PrimaryRestartState {
    Animal* avatar = nullptr;
    Simulator::cHerd* herd = nullptr;
    Math::Vector3 position;
    float health = 0, hunger = 0;
    int age = 0;
    bool dead = false;
    Simulator::cCinematicManager* cinematic = nullptr;
    Simulator::cGameTimeManager* time = nullptr;
    uint32_t request = 0;
    std::array<uint32_t, 3> restart_position{};
};

// False when there is no first avatar to compare with, or while a cinematic or a pause is active.
bool capture_primary_restart_state(Manager* manager, PrimaryRestartState& primary) {
    primary.avatar = manager->mpAvatar.get();
    primary.herd = manager->mpAvatarHerd.get();
    if (!primary.avatar) {
        return false;
    }
    primary.position = primary.avatar->GetPosition();
    primary.health = primary.avatar->mHealthPoints;
    primary.hunger = primary.avatar->mHunger;
    primary.age = primary.avatar->mAge;
    primary.dead = primary.avatar->mbDead;
    primary.cinematic = Simulator::cCinematicManager::Get();
    primary.time = Simulator::cGameTimeManager::Get();
    if (!primary.cinematic || !primary.time || primary.cinematic->field_2C || primary.time->mFlags) {
        return false;
    }
    primary.request = word(primary.cinematic, 0x148);
    std::memcpy(primary.restart_position.data(), reinterpret_cast<void*>(base + restart_position_rva), 12);
    return true;
}

bool primary_identity_kept(Manager* manager, const PrimaryRestartState& primary) {
    return manager->mpAvatar.get() == primary.avatar && manager->mpAvatarHerd.get() == primary.herd &&
           manager->mPosseMembers.empty();
}

// The avatar's position is read in every case, as before; its other state only once its identity is confirmed.
bool primary_state_kept(const PrimaryRestartState& primary, bool identity) {
    const auto position_after = primary.avatar->GetPosition();
    return identity && !std::memcmp(&primary.position, &position_after, sizeof(primary.position)) &&
           primary.avatar->mHealthPoints == primary.health && primary.avatar->mHunger == primary.hunger &&
           primary.avatar->mAge == primary.age && primary.avatar->mbDead == primary.dead &&
           !primary.cinematic->field_2C && !primary.time->mFlags && word(primary.cinematic, 0x148) == primary.request;
}

void record_restart_routes(Animal* actor, Simulator::cHerd* herd, bool identity, bool primary_unchanged,
                           bool herd_preserved, size_t herd_members_before) {
    if (!sink) {
        return;
    }
    char fields[450]{};
    sprintf_s(fields,
              ",\"native_slot\":2,\"routes\":%u,\"primary_identity_unchanged\":%s,\"primary_unchanged\":%s,\"herd_"
              "native_id\":%u,\"herd_preserved\":%s,\"herd_members_before\":%u,\"herd_members_after\":%u,"
              "\"health\":%.9g,\"hunger\":%.9g,\"age\":%d",
              restart_routes, identity ? "true" : "false", primary_unchanged ? "true" : "false", herd->mID,
              herd_preserved ? "true" : "false", static_cast<unsigned>(herd_members_before),
              static_cast<unsigned>(herd->mHerd.size()), double(actor->mHealthPoints), double(actor->mHunger),
              static_cast<int>(actor->mAge));
    sink("native_death_secondary_restart_routes", fields);
}

bool secondary_revive(Animal* actor, void*) {
    auto manager = Manager::Get();
    auto herd = actor->mHerd.get();
    auto player = native_second_player(secondary_observed.epoch);
    uint32_t migration = 0;
    const auto has_property = reinterpret_cast<bool(__thiscall*)(void*, uint32_t, uint32_t*)>(base + 0x877520);
    if (!actor->mbDead || actor->mHealthPoints != 0 || !manager || !player || !manager->mPosseMembers.empty() ||
        !herd || herd == manager->mpAvatarHerd.get() || !secondary_herd_admitted(actor, herd) || !herd->mpNest ||
        !native_secondary_hatch_ready(actor, secondary_observed.epoch) ||
        has_property(player, 0x0514a219, &migration) || secondary_handler[2] != 2 ||
        word(reinterpret_cast<void*>(word(actor, 0)), 0x90) != base + 0x802d00) {
        return false;
    }
    // Same original revival and RestartAtNest as D42E50. The shared mode reset
    // is replaced only by the owned handler reset/target clear; it must not
    // deactivate A's action handlers. Native health/age/placement remain intact.
    const auto herd_members_before = herd->mHerd.size();
    PrimaryRestartState primary;
    if (!capture_primary_restart_state(manager, primary)) {
        return false;
    }
    if (!handler_reset(secondary_handler.data()) || secondary_handler[2]) {
        return false;
    }
    actor->SetCreatureTarget(nullptr, false, 0);
    if (!begin_native_secondary_hatch(actor, secondary_observed.epoch)) {
        return false;
    }
    secondary_restart = true;
    restart_routes = 0;
    actor->func90h();
    restart_original();
    secondary_restart = false;
    const bool hatch_ready = end_native_secondary_hatch();
    const bool herd_preserved = actor->mHerd.get() == herd;
    std::memcpy(reinterpret_cast<void*>(base + restart_position_rva), primary.restart_position.data(), 12);
    const bool identity = primary_identity_kept(manager, primary);
    const bool primary_unchanged = primary_state_kept(primary, identity);
    record_restart_routes(actor, herd, identity, primary_unchanged, herd_preserved, herd_members_before);
    return hatch_ready && herd_preserved && primary_unchanged && restart_routes == 63 && !actor->mbDead &&
           actor->mHealthPoints > 0;
}

bool __fastcall message_hook(void* receiver, void*, uint32_t message, void* payload) {
    if (on_thread() && native_replica_is_client() && message == restart_message) {
        auto strategy = Strategy::Get();
        auto actor = avatar();
        auto cinematic = Simulator::cCinematicManager::Get();
        const auto selected = cinematic && readable(cinematic, sizeof(*cinematic)) ? word(cinematic, 0x148) : 0;
        const bool receiver_matches = strategy && receiver == static_cast<App::IMessageListener*>(strategy);
        // D42E50's restart branch ignores payload. Delivery may occur after
        // the selected request has been retired, so retain the observed native
        // pause/resume identity instead of requiring an active queue entry.
        const bool matches = client_occurrence && client_cinematic && strategy && receiver_matches && actor &&
                             actor->mbDead && client_paused && !client_cycle_failed &&
                             (selected == client_cinematic || client_resumed);
        if (sink) {
            char fields[440]{};
            sprintf_s(fields,
                      ",\"occurrence\":%llu,\"native_request\":%u,\"selected_request\":%u,\"receiver_matches\":%s,"
                      "\"payload_present\":%s,\"paused\":%s,\"resumed\":%s,\"cycle_failed\":%s,\"cinematic_state\":%d",
                      client_occurrence, client_cinematic, selected, receiver_matches ? "true" : "false",
                      payload ? "true" : "false", client_paused ? "true" : "false", client_resumed ? "true" : "false",
                      client_cycle_failed ? "true" : "false", cinematic ? cinematic->field_2C : -1);
            sink("native_death_client_message_identity", fields);
        }
        if (matches) {
            client_restart = true;
        }
        NativeDeathRequest request{native_actor_worker_status().epoch, client_occurrence,
                                   actor ? actor->mID : UINT32_MAX, client_reason};
        record("native_death_client_restart", request, matches);
        // Restart changes native gameplay/model state and is authority-only,
        // including unsolicited local messages outside a presentation.
        return false;
    }
    return message_original(receiver, message, payload);
}

// The original death presentation bound by prepare_native_death: D38E60 (begin), AE08B0 (cinematic play) and
// D42E50 (message), with the names of the two death cinematics.
bool presentation_code_pinned() {
    const unsigned char begin[] = {0x8b, 0x44, 0x24, 0x04, 0x6a, 0, 0x6a, 0,    0x6a, 0,    0x6a, 1,
                                   0xc7, 0x41, 8,    2,    0,    0, 0,    0x6a, 1,    0x83, 0xf8, 5};
    const unsigned char play[] = {0x56, 0x8b, 0xf1, 0x80, 0xbe, 0x5f, 1,    0,    0,
                                  0,    0x75, 6,    0x33, 0xc0, 0x5e, 0xc2, 0x18, 0};
    const unsigned char message[] = {0x8b, 0x44, 0x24, 4,    0x83, 0xec, 0x20, 0x53, 0x55,
                                     0x56, 0x57, 0x8b, 0xd9, 0x3d, 0xcd, 0x93, 0x1b, 5};
    const unsigned char begin_tail[] = {0xc2, 4, 0};
    unsigned char dispatch[] = {0x75, 0x14, 0x68, 0xdc, 0xaa, 0x47, 1,    0xe8, 0x4c, 0x47, 0xe0, 0xff, 0x8b, 0xc8,
                                0xe8, 0x25, 0x7a, 0xda, 0xff, 0xc2, 4,    0,    0x83, 0xf8, 9,    0x75, 0x14, 0x68,
                                0xc0, 0xaa, 0x47, 1,    0xe8, 0x33, 0x47, 0xe0, 0xff, 0x8b, 0xc8, 0xe8, 0x0c, 0x7a,
                                0xda, 0xff, 0xc2, 4,    0,    0x68, 0xb0, 0xaa, 0x47, 1,    0xe8, 0x1f, 0x47, 0xe0,
                                0xff, 0x8b, 0xc8, 0xe8, 0xf8, 0x79, 0xda, 0xff, 0xc2, 4,    0};
    // The three PUSH string operands are PE HIGHLOW relocations. Match their
    // loaded addresses while retaining every opcode and relative call byte.
    for (const auto offset : {3u, 28u, 48u}) {
        uint32_t address = 0;
        std::memcpy(&address, dispatch + offset, 4);
        address += static_cast<uint32_t>(base) - 0x400000;
        std::memcpy(dispatch + offset, &address, 4);
    }
    const unsigned char caller[] = {0x8b, 0x4b, 0x14, 0x51, 0x8b, 0xcb, 0xe8, 0xd1, 0x8b, 0xff, 0xff};
    const char combat_name[] = "CRG_AvatarDied", starvation_name[] = "CRG_AvatarDied_Starvation";
    const unsigned char message_tail[] = {0x32, 0xc0, 0x5b, 0x83, 0xc4, 0x20, 0xc2, 8, 0};
    if (!prefix(0x938e60, begin) || !prefix(0x6e08b0, play) || !prefix(0x942e50, message) ||
        !prefix(0x938e8b, begin_tail) || !prefix(0x938e78, dispatch) || !prefix(0x940284, caller) ||
        !prefix(0x9439a1, message_tail) ||
        !readable(reinterpret_cast<void*>(base + combat_cinematic_rva), sizeof(combat_name)) ||
        std::memcmp(reinterpret_cast<void*>(base + combat_cinematic_rva), combat_name, sizeof(combat_name)) ||
        !readable(reinterpret_cast<void*>(base + starvation_cinematic_rva), sizeof(starvation_name)) ||
        std::memcmp(reinterpret_cast<void*>(base + starvation_cinematic_rva), starvation_name,
                    sizeof(starvation_name))) {
        return false;
    }
    return true;
}

// The original death handler, of which the second player gets its own instance (secondary_handler): its inlined
// constructor, its vtable and methods, and the restart code bound by prepare_native_death.
bool handler_code_pinned() {
    const unsigned char action[] = {0x8b, 0x44, 0x24, 4, 0x3d, 0x75, 0x7a, 0xca, 6};
    const unsigned char tick[] = {0x83, 0xec, 0x7c, 0x53, 0x55, 0x56, 0x8b, 0xd9, 0xe8, 0x13, 0xd2, 0xdf, 0xff};
    const unsigned char reset[] = {0x81, 0xec, 0xd8, 0, 0, 0, 0x57, 0x8b, 0xf9, 0x8b, 0x4f, 0x10};
    const unsigned char destroy_tail[] = {0x8b, 0xc6, 0x5e, 0xc2, 4, 0};
    const unsigned char restart[] = {0x83, 0xec, 0x68, 0x53, 0x55, 0x56, 0x57, 0xe8, 0x54, 0xc4, 0xdf, 0xff};
    const unsigned char interaction[] = {0x0f, 0x57, 0xc0, 0x56};
    const unsigned char deactivate[] = {0x51, 0x55, 0x8b, 0xe9, 0x8b, 0x4d, 0x58, 0x8d, 0x45, 0x54};
    const unsigned char activate[] = {0x83, 0xec, 8, 0x8b, 0x44, 0x24, 0x0c, 0x53, 0x56, 0x57, 0x8b, 0xf9};
    const unsigned char parameter[] = {0x53, 0x8b, 0x5c, 0x24, 0x10, 0x56, 0x57, 0x8b, 0xf9, 0x85, 0xdb, 0x75, 0x3a};
    const unsigned char parameter_tail[] = {0x5f, 0x5e, 0x5b, 0xc2, 0x0c, 0};
    const unsigned char revive[] = {0x56, 0x57, 0x8b, 0xf1, 0xe8, 0xf7, 0x8d, 0, 0};
    const unsigned char property[] = {0x83, 0xec, 8, 0x56, 0x8d, 0xb1, 0x98, 6, 0, 0};
    unsigned char constructor[] = {0x0f, 0x57, 0xc0, 0x89, 0x68, 4,    0xc7, 0,    0x90, 0xaa, 0x47, 1,    0x89,
                                   0x68, 8,    0xf3, 0x0f, 0x11, 0x40, 0x0c, 0x89, 0x68, 0x10, 0x89, 0x68, 0x14};
    const uint32_t vtable = static_cast<uint32_t>(base + handler_vtable_rva);
    std::memcpy(constructor + 8, &vtable, 4);
    const char restart_name[] = "CRG_Fade_From_Black_Egg";
    if (!prefix(0x938ec0, action) || !prefix(0x9401e0, tick) || !prefix(0x9405f0, reset) ||
        !prefix(0x938f2d, destroy_tail) || !prefix(0x940fa0, restart) || !prefix(0x93c360, interaction) ||
        !prefix(0x94d670, deactivate) || !prefix(0x94d740, activate) || !prefix(0x6e0930, parameter) ||
        !prefix(0x6e09eb, parameter_tail) || !prefix(0x802d00, revive) || !prefix(0x877520, property) ||
        !prefix(0x947b50, constructor) ||
        !readable(reinterpret_cast<void*>(base + restart_cinematic_rva), sizeof(restart_name)) ||
        std::memcmp(reinterpret_cast<void*>(base + restart_cinematic_rva), restart_name, sizeof(restart_name))) {
        return false;
    }
    const uint32_t handler_methods[] = {0x938f00, 0x1454f0, 0x053a10, 0x053a80, 0x9401e0, 0x9405f0, 0x938ec0, 0x9de930};
    if (!readable(reinterpret_cast<void*>(vtable), sizeof(handler_methods))) {
        return false;
    }
    for (size_t i = 0; i < std::size(handler_methods); ++i) {
        if (word(reinterpret_cast<void*>(vtable), i * 4) != base + handler_methods[i]) {
            return false;
        }
    }
    return true;
}
} // namespace

bool prepare_native_death(uintptr_t image, uintptr_t image_end, DWORD engine_thread, PlayerContextEvent events,
                          bool (*check)()) {
    base = image;
    end = image_end;
    thread = engine_thread;
    sink = events;
    thread_check = check;
    reset_native_death();
    if (!presentation_code_pinned()) {
        return false;
    }
    begin_original = reinterpret_cast<NativeDeathAbi::Begin>(base + 0x938e60);
    play_original = reinterpret_cast<Play>(base + 0x6e08b0);
    message_original = reinterpret_cast<NativeDeathAbi::Message>(base + 0x942e50);
    if (!handler_code_pinned()) {
        return false;
    }
    handler_action = reinterpret_cast<NativeDeathAbi::Action>(base + 0x938ec0);
    handler_tick = reinterpret_cast<NativeDeathAbi::Tick>(base + 0x9401e0);
    handler_reset = reinterpret_cast<NativeDeathAbi::Reset>(base + 0x9405f0);
    handler_destroy = reinterpret_cast<NativeDeathAbi::Destroy>(base + 0x938f00);
    restart_original = reinterpret_cast<NativeDeathAbi::Restart>(base + 0x940fa0);
    interaction_original = reinterpret_cast<NativeDeathAbi::ClearInteraction>(base + 0x93c360);
    ui_deactivate_original = reinterpret_cast<NativeDeathAbi::UiDeactivate>(base + 0x94d670);
    ui_activate_original = reinterpret_cast<NativeDeathAbi::UiActivate>(base + 0x94d740);
    parameter_original = reinterpret_cast<NativeDeathAbi::CinematicParameter>(base + 0x6e0930);
    return true;
}

NativeHook native_death_begin_binding() {
    return {reinterpret_cast<void**>(&begin_original), reinterpret_cast<void*>(begin_hook)};
}

NativeHook native_death_cinematic_binding() {
    return {reinterpret_cast<void**>(&play_original), reinterpret_cast<void*>(cinematic_hook)};
}

NativeHook native_death_message_binding() {
    return {reinterpret_cast<void**>(&message_original), reinterpret_cast<void*>(message_hook)};
}

NativeHook native_death_interaction_binding() {
    return {reinterpret_cast<void**>(&interaction_original), reinterpret_cast<void*>(interaction_hook)};
}

NativeHook native_death_ui_deactivate_binding() {
    return {reinterpret_cast<void**>(&ui_deactivate_original), reinterpret_cast<void*>(ui_deactivate_hook)};
}

NativeHook native_death_ui_activate_binding() {
    return {reinterpret_cast<void**>(&ui_activate_original), reinterpret_cast<void*>(ui_activate_hook)};
}

NativeHook native_death_parameter_binding() {
    return {reinterpret_cast<void**>(&parameter_original), reinterpret_cast<void*>(parameter_hook)};
}

void observe_native_secondary_death(uint32_t id, uint32_t reason) {
    if (!on_thread() || !native_network_death_authority() || secondary_handler[0] ||
        secondary_observed.native_id != UINT32_MAX || !network::valid_death_reason(reason)) {
        return;
    }
    auto actor = owned_actor(2);
    if (!actor || actor->mID != id || actor->mHealthPoints != 0) {
        return;
    }
    secondary_observed = {native_actor_worker_status().epoch, 0, id, reason, 2};
    sample_native_awards();
    record("native_death_secondary_damage_return", secondary_observed, false);
}

void update_native_secondary_death() {
    if (!on_thread() || secondary_blocked || !native_network_death_authority() ||
        secondary_observed.native_id == UINT32_MAX || secondary_observed.epoch != native_actor_worker_status().epoch) {
        return;
    }
    if (!secondary_handler[0]) {
        auto actor = owned_actor(2);
        if (!actor || !actor->mbDead || actor->mHealthPoints != 0) {
            return;
        }
        const bool applied = call_native_secondary_death(secondary_observed.native_id, secondary_observed.epoch,
                                                         secondary_action, nullptr);
        record("native_death_secondary_action_return", secondary_observed, applied);
    }
    if (secondary_handler[2] != 1) {
        return;
    }
    call_native_secondary_death(secondary_observed.native_id, secondary_observed.epoch, secondary_step, nullptr);
}

bool pending_native_death(uint32_t slot, NativeDeathRequest& out) {
    out = {};
    if (slot != 1 && slot != 2) {
        return false;
    }
    auto actor = on_thread() ? owned_actor(slot) : nullptr;
    const auto& request = pending[slot];
    auto handler = pending_handler[slot];
    if (!request.occurrence || !actor || actor->mID != request.native_id || !actor->mbDead ||
        actor->mHealthPoints != 0 || request.epoch != native_actor_worker_status().epoch || !handler_live(handler) ||
        word(handler, 8) != 2) {
        return false;
    }
    out = request;
    return true;
}

bool complete_native_death(const NativeDeathRequest& request) {
    NativeDeathRequest current;
    if (!native_network_death_authority() || !pending_native_death(request.native_slot, current) ||
        request.occurrence != current.occurrence || request.epoch != current.epoch ||
        request.native_id != current.native_id || request.reason != current.reason) {
        return false;
    }
    auto strategy = Strategy::Get();
    if (!strategy) {
        return false;
    }
    // The original listener resets its handler, revives the same avatar, and
    // runs the original restart-at-nest path. No invented health/age/location.
    bool restarted = true;
    if (request.native_slot == 2) {
        sample_native_awards();
        restarted = call_native_secondary_death(request.native_id, request.epoch, secondary_revive, nullptr);
        sample_native_awards();
    } else {
        message_original(static_cast<App::IMessageListener*>(strategy), restart_message, nullptr);
    }
    auto actor = owned_actor(request.native_slot);
    const bool applied =
        restarted && actor && actor->mID == request.native_id && !actor->mbDead && actor->mHealthPoints > 0;
    record("native_death_authority_restart", request, applied);
    if (applied) {
        pending[request.native_slot] = {};
        pending_handler[request.native_slot] = nullptr;
        if (request.native_slot == 2) {
            handler_destroy(secondary_handler.data(), 0);
            secondary_handler.fill(0);
            secondary_observed = {};
        }
    }
    return applied;
}

bool present_native_death(uint64_t occurrence, uint32_t reason) {
    if (!on_thread() || !native_replica_is_client() || !occurrence || !network::valid_death_reason(reason) ||
        client_occurrence) {
        return false;
    }
    auto actor = avatar();
    auto manager = Simulator::cCinematicManager::Get();
    auto time = Simulator::cGameTimeManager::Get();
    if (!actor || !actor->mbDead || actor->mHealthPoints != 0 || !manager || !readable(manager, sizeof(*manager)) ||
        manager->field_2C || !time || time->mFlags ||
        native_tutorial_presentation() == NativeTutorialPresentation::active) {
        return false;
    }
    client_cinematic = play_original(
        manager, reinterpret_cast<const char*>(base + (reason == 5 ? starvation_cinematic_rva : combat_cinematic_rva)),
        1, 1, nullptr, 0, 0);
    if (!client_cinematic) {
        return false;
    }
    client_occurrence = occurrence;
    client_reason = reason;
    client_restart = false;
    client_paused = client_resumed = client_cycle_failed = false;
    record("native_death_client_queued", {native_actor_worker_status().epoch, occurrence, actor->mID, reason}, true);
    return true;
}

bool native_death_confirmed(uint64_t occurrence) {
    if (!on_thread() || !client_restart || !client_resumed || client_cycle_failed || occurrence != client_occurrence) {
        return false;
    }
    auto manager = Simulator::cCinematicManager::Get();
    return manager && readable(manager, sizeof(*manager)) && !manager->field_2C;
}

bool restore_native_death_view(uint64_t occurrence) {
    if (!on_thread() || !native_replica_is_client() || occurrence != client_occurrence ||
        !native_death_confirmed(occurrence)) {
        return false;
    }
    auto actor = avatar();
    if (!actor || actor->mbDead || actor->mHealthPoints <= 0) {
        return false;
    }
    if (client_fade) {
        return client_fade->IsRunning() == 0;
    }
    auto effects = Swarm::IEffectsManager::Get();
    constexpr auto effect_id = id("fade_from_black_1");
    if (!effects || !effects->HasVisualEffect(effect_id, 0) ||
        !effects->CreateVisualEffect(effect_id, 0, client_fade) || !client_fade) {
        return false;
    }
    // The pinned original Creature cinematic resource starts this exact Swarm
    // effect after RestartAtNest to replace its persistent black_screen.
    // Only that visual effect is needed here: the authority already ran the
    // original revival, nest relocation, age and gameplay sequence.
    client_fade->Start(1);
    record("native_death_client_fade_started",
           {native_actor_worker_status().epoch, occurrence, actor->mID, client_reason}, true);
    return false;
}

void observe_native_death_pause(bool pausing, uint32_t type, uint32_t caller, int counter, int flags) {
    if (!on_thread() || !client_occurrence || type != 0x0600c4ae) {
        return;
    }
    auto manager = Simulator::cCinematicManager::Get();
    const auto selected = manager && readable(manager, sizeof(*manager)) ? word(manager, 0x148) : 0;
    if (selected != client_cinematic) {
        client_cycle_failed = true;
    }
    if (!client_cycle_failed && pausing && caller == 0x6e1fd0 && counter == 1 && flags == 3) {
        client_paused = true;
    }
    if (!client_cycle_failed && !pausing && caller == 0x6e1f46 && !counter && !flags && client_paused) {
        client_resumed = true;
    }
    if (sink) {
        char fields[440]{};
        sprintf_s(fields,
                  ",\"occurrence\":%llu,\"native_request\":%u,\"selected_request\":%u,\"pausing\":%s,\"caller_rva\":%u,"
                  "\"counter_after\":%d,\"flags_after\":%d,\"resumed\":%s,\"cycle_failed\":%s",
                  client_occurrence, client_cinematic, selected, pausing ? "true" : "false", caller, counter, flags,
                  client_resumed ? "true" : "false", client_cycle_failed ? "true" : "false");
        sink("native_death_client_cycle", fields);
    }
}

bool native_death_checkpoint_idle() {
    return !pending[1].occurrence && !pending[2].occurrence && secondary_observed.native_id == UINT32_MAX &&
           !secondary_restart && !client_occurrence && native_hatch_checkpoint_idle();
}

void reset_native_death() {
    reset_native_hatch();
    if (client_fade) {
        client_fade->Stop(1);
        client_fade = nullptr;
    }
    if (on_thread() && secondary_handler[0] && handler_reset && handler_destroy) {
        handler_reset(secondary_handler.data());
        handler_destroy(secondary_handler.data(), 0);
    }
    secondary_handler.fill(0);
    secondary_observed = {};
    secondary_tick = secondary_restart = secondary_blocked = false;
    restart_routes = 0;
    pending = {};
    pending_handler = {};
    presenting = {};
    deferring = deferred = false;
    client_occurrence = 0;
    client_cinematic = 0;
    client_reason = 0;
    client_restart = false;
    client_paused = client_resumed = client_cycle_failed = false;
}
} // namespace sporemp
