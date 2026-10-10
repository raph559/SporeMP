#include "native_award_context.h"
#include "native_actors.h"
#include "native_scene.h"
#include "native_replica.h"
#include "native_award_abi.h"
#include "native_pickup.h"
#include "native_social_observation.h"
#include "native_tutorial_pending.h"
#include "native_social_objective.h"
#include "native_relationship_context.h"
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/Editors/SpeciesManager.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/CinematicManager.h>
#include <Spore/Simulator/SubSystem/GameTimeManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cInteractiveOrnament.h>
#include <Spore/Simulator/Serialization.h>
#include <intrin.h>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Creature = Simulator::cCreatureBase;
using Player = Simulator::cPlayer;
using Manager = Simulator::cGameNounManager;
using Spatial = Simulator::cSpatialObject;
using Stage = Simulator::cCreatureGameData;
using Strategy = App::cCreatureModeStrategy;
// These signatures are pinned to the inspected x86 instructions, not inferred C.
using Abi = NativeAwardAbi<Animal, Spatial, Player, Manager, Strategy>;
using Owned = Abi::Owned;
using Amount = Abi::Amount;
using PlayerGet = Abi::PlayerGet;
using AvatarGet = Abi::AvatarGet;
using DisplayAdd = Abi::DisplayAdd;
using Action = Abi::Action;
using Update = Abi::Update;
using Goal = Abi::Goal;
using Tutorial = Abi::Tutorial;
using InitDeque = void(__thiscall*)(void*, uint32_t);
using ObjectCall = void(__thiscall*)(void*);
using CounterTick = void(__thiscall*)(void*, uint32_t);
using StageReset = void(__cdecl*)();
Owned owned_original = nullptr;
Amount amount_original = nullptr;
PlayerGet player_original = nullptr;
AvatarGet avatar_original = nullptr;
Abi::SpeciesGet species_original = nullptr;
DisplayAdd display_original = nullptr;
Action action_original = nullptr;
Abi::PropertyAction property_original = nullptr;
Abi::PropertyLookup property_lookup = nullptr;
Abi::PropertyApply property_apply = nullptr;
Update update_original = nullptr;
Goal goal_original = nullptr;
Tutorial tutorial_original = nullptr;
Abi::TutorialHas tutorial_has = nullptr;
Abi::TutorialRecord tutorial_record = nullptr;
Abi::CinematicPlay cinematic_play = nullptr;
Abi::CinematicCaption cinematic_caption = nullptr;
Abi::RelationshipManager relationship_manager = nullptr;
Abi::RelationshipFind relationship_find = nullptr;
InitDeque deque_init = nullptr;
ObjectCall deque_destroy = nullptr, timer_init = nullptr, timer_destroy = nullptr;
CounterTick counter_tick = nullptr;
StageReset stage_reset = nullptr;
uintptr_t image_base = 0;
DWORD engine_thread = 0;
PlayerContextEvent sink = nullptr;
bool (*thread_check)() = nullptr;
Animal* b_actor = nullptr;
uint32_t b_native_id = UINT32_MAX;
uint64_t b_id = 0, b_epoch = 0;
bool ready = false, context_active = false, world_selected = false, world_saved_a = false, b_world_pending = false;
bool alternate_world = false, boundary_reported = false, suspended_for_death = false;
bool foreign_initializing = false, foreign_context = false;
Editors::cSpeciesManager* initialization_manager = nullptr;
Simulator::cSpeciesProfile* initialization_primary = nullptr;
unsigned initialization_species_calls = 0;
uint64_t counter_updates = 0, world_updates = 0, awards = 0;
Stage b_stage{};
// Last observed value survives reward-context cleanup for a still-present
// corpse. This scalar is presentation evidence, never restored native state.
uint64_t retired_actor_id = 0, retired_epoch = 0;
float retired_dna = 0;
bool retired_dna_valid = false;
// Native display counter: state +00, native deque +04 (40 bytes), timer +30.
// Constructors/destructors and queue processing are original game functions.
// There is no SDK/container destructor at DLL unload and no native pointer leaves the bridge.
alignas(8) std::array<unsigned char, 0x50> b_counter{};
constexpr size_t counter_deque_offset = 4, counter_timer_offset = 0x30;
// Where the original display strategy keeps the first player's counter; its add routine applies this offset to its
// receiver itself.
constexpr size_t display_counter_offset = 0x128;
Animal* damage_actor = nullptr;
uint32_t damage_owner = 0;
uint64_t damage_id = 0;
bool damage_foreign_latched = false;
uint64_t damage_foreign_epoch = 0;
Animal* pickup_actor = nullptr;
Animal* pickup_corpse = nullptr;
uint32_t pickup_owner = 0, pickup_corpse_native_id = UINT32_MAX;
uint64_t pickup_id = 0;
bool pickup_granted = false;
uint64_t social_observation = 0;
uint64_t objective_observation = 0, objective_result = 0, objective_epoch = 0;
ResourceKey objective_species{};
unsigned objective_samples_left = 0;
NativeSocialObjectiveReceipt objective_receipt;
bool objective_before_valid = false, objective_active = false, objective_awarded = false;
uint64_t objective_active_call = 0;
uint32_t objective_before_count = 0, objective_before_relation = 0;
uint64_t tutorial_observation = 0;
bool defer_secondary_tutorial = false;
NativeTutorialPending pending_tutorial, primary_tutorial;
NativeTutorialPresentation tutorial_presentation = NativeTutorialPresentation::idle;
uint32_t presentation_id = 0;
bool presentation_paused = false;
Animal* social_owner_actor = nullptr;
Animal* social_owner_target = nullptr;
uint64_t social_owner_id = 0;
uint32_t social_owner_target_id = UINT32_MAX;
Simulator::cInteractiveOrnament* fossil_target = nullptr;
uint32_t fossil_target_id = UINT32_MAX;
bool fossil_property_applied = false;

void event(const char* name, const char* format = "", ...) {
    if (!sink || GetCurrentThreadId() != engine_thread) {
        return;
    }
    char fields[1536]{};
    va_list args;
    va_start(args, format);
    const int result = vsprintf_s(fields, format, args);
    va_end(args);
    if (result >= 0) {
        sink(name, fields);
    }
}

uint32_t caller(void* p) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - image_base);
}

bool owner_present() {
    if (!ready || GetCurrentThreadId() != engine_thread || !native_second_player(b_epoch) || !Manager::Get()) {
        return false;
    }
    for (auto& noun : Manager::Get()->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(b_actor)) {
            return !noun.mbIsDestroyed && !noun.field_20 && noun.mID == b_native_id && !b_actor->mbMarkedForDeletion;
        }
    }
    return false;
}

bool live() {
    return owner_present() && !b_actor->mbDead;
}

bool owned_damage() {
    if (damage_owner != 2 || damage_actor != b_actor || damage_id != b_id) {
        return false;
    }
    // The original in-flight foreign hit retains its beneficiary if B dies
    // reentrantly. A retired player/actor/epoch is never adopted by this latch.
    return foreign_context ? damage_foreign_latched && damage_foreign_epoch == b_epoch && owner_present() : live();
}

bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto begin = reinterpret_cast<uintptr_t>(address), region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin >= region && size <= info.RegionSize && begin - region <= info.RegionSize - size;
}

// The first player's counter inside the original display strategy.
const unsigned char* primary_counter(Strategy* strategy) {
    return reinterpret_cast<const unsigned char*>(strategy->mpDisplayStrategy.get()) + display_counter_offset;
}

// A display counter's queue holds nothing when the two words of its native deque at +0C and +1C are equal; the
// state samples report this as queue_empty.
bool counter_queue_empty(const unsigned char* counter) {
    return *reinterpret_cast<const uintptr_t*>(counter + 0xc) == *reinterpret_cast<const uintptr_t*>(counter + 0x1c);
}

// A readable display counter at rest: its state word is zero and its queue is empty.
bool counter_idle(const unsigned char* counter) {
    return readable(counter, b_counter.size()) && *reinterpret_cast<const uint32_t*>(counter) == 0 &&
           counter_queue_empty(counter);
}

bool relationship_state(const ResourceKey& key, uint32_t& count, uint32_t& relation) {
    if (foreign_context) {
        NativeRelationshipState state;
        if (!native_relationship_read(2, key, state)) {
            return false;
        }
        // This is raw native entry state, not effective attitude. A missing
        // entry is valid empty; no query inserts a fabricated native record.
        count = state.words[2];
        relation = state.words[0];
        return true;
    }
    // BA44C0 would insert a default entry for an unknown species. Call only
    // the audited non-inserting BA3190 lookup and reject its end iterator.
    if (!relationship_manager || !relationship_find) {
        return false;
    }
    const auto manager = static_cast<unsigned char*>(relationship_manager());
    if (!readable(manager, 0x30)) {
        return false;
    }
    auto map = manager + 0x20;
    const auto buckets = *reinterpret_cast<const uint32_t*>(map + 4);
    const auto size = *reinterpret_cast<const uint32_t*>(map + 8);
    if (!size || size > 1048576 || !readable(reinterpret_cast<void*>(buckets), (size_t(size) + 1) * 4)) {
        return false;
    }
    uint32_t iterator[2]{};
    if (relationship_find(map, iterator, reinterpret_cast<const uint32_t*>(&key)) != iterator ||
        iterator[0] == reinterpret_cast<const uint32_t*>(buckets)[size] ||
        !readable(reinterpret_cast<void*>(iterator[0]), 0x24)) {
        return false;
    }
    const auto node = reinterpret_cast<const unsigned char*>(iterator[0]);
    if (memcmp(node, &key, sizeof(key))) {
        return false;
    }
    memcpy(&relation, node + 0xc, 4);
    memcpy(&count, node + 0x14, 4);
    return relation <= 7;
}

bool owned_objective() {
    return objective_active && objective_active_call && objective_epoch == b_epoch &&
           GetCurrentThreadId() == engine_thread && thread_check && thread_check() && live();
}

Animal* current_corpse(Animal* candidate, uint32_t expected = UINT32_MAX) {
    auto manager = Manager::Get();
    if (!candidate || !manager) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(candidate)) {
            if (!readable(candidate, sizeof(Animal)) || noun.mbIsDestroyed || noun.field_20 ||
                candidate->mbMarkedForDeletion || !candidate->mbEnabled || !candidate->mbDead ||
                *reinterpret_cast<const uintptr_t*>(candidate) != image_base + 0x106a080 ||
                (expected != UINT32_MAX && noun.mID != expected)) {
                return nullptr;
            }
            return candidate;
        }
    }
    return nullptr;
}

bool owned_pickup() {
    if (GetCurrentThreadId() != engine_thread || pickup_owner != 2 || pickup_actor != b_actor || pickup_id != b_id ||
        pickup_corpse_native_id == UINT32_MAX || !thread_check || !thread_check() || !live()) {
        return false;
    }
    auto corpse = current_corpse(pickup_corpse, pickup_corpse_native_id);
    return corpse && corpse->mpWhoIsInteractingWithMe.get() == pickup_actor;
}

struct Progress {
    float dna, goal, combat, social;
};

Progress progress(Player* player, float dna) {
    return {dna, player->mCurrentGoalProgress, player->mCombatTraitProgress, player->mSocialTraitProgress};
}

Player* first_player() {
    auto manager = Manager::Get();
    auto candidate = manager && player_original ? player_original(manager) : nullptr;
    if (!candidate) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(candidate)) {
            return !noun.mbIsDestroyed && !noun.field_20 && readable(candidate, sizeof(Player)) ? candidate : nullptr;
        }
    }
    return nullptr;
}

// The first player and the second, for a result the second player owns.
struct Players {
    Player* first = nullptr;
    Player* second = nullptr;
};

// Both players when the second owns the result in progress, and neither when it does not. The first player is
// looked up only once the second is known.
Players players_when(bool second_owns) {
    Players players;
    players.second = second_owns ? native_second_player(b_epoch) : nullptr;
    players.first = players.second ? first_player() : nullptr;
    return players;
}

// Progress of the first player (a, with the game's own Stage) and of the second (b, with the Stage kept here).
struct ProgressPair {
    Progress a, b;
};

ProgressPair progress_pair(const Players& players) {
    return {progress(players.first, Stage::Get()->mEvolutionPoints),
            progress(players.second, b_stage.mEvolutionPoints)};
}

Animal* social_animal(uint32_t address) {
    // Compare against the live noun roots before reading a borrowed payload
    // address. No virtual call is made through the untrusted address.
    auto manager = Manager::Get();
    if (!address || !manager) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (reinterpret_cast<uintptr_t>(&noun) == address) {
            if (noun.mbIsDestroyed || noun.field_20 || !readable(&noun, sizeof(Animal)) ||
                *reinterpret_cast<const uintptr_t*>(&noun) != image_base + 0x106a080) {
                return nullptr;
            }
            auto animal = static_cast<Animal*>(&noun);
            return animal->mbMarkedForDeletion || !animal->mbEnabled || animal->mbDead ? nullptr : animal;
        }
    }
    return nullptr;
}

bool owned_social() {
    if (!thread_check || !thread_check() || !live() || social_owner_actor != b_actor || social_owner_id != b_id) {
        return false;
    }
    auto target = social_animal(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(social_owner_target)));
    // The result scope pins the initiating pair once, before native relationship
    // changes. A legitimate native target clear inside that result must not
    // silently redirect its later reward to A.
    return target && target->mID == social_owner_target_id && target != b_actor &&
           target->mpSpeciesProfile != b_actor->mpSpeciesProfile;
}

void social_progress_event(const char* name, float amount, const Progress& a_before, const Progress& b_before) {
    auto first = first_player();
    auto second = native_second_player(b_epoch);
    if (!first || !second) {
        return;
    }
    const auto a_after = progress(first, Stage::Get()->mEvolutionPoints),
               b_after = progress(second, b_stage.mEvolutionPoints);
    event(name,
          ",\"owner\":2,\"actor\":%llu,\"actor_native_id\":%u,\"target_native_id\":%u,\"amount\":%.9g,"
          "\"a_dna_before\":%.9g,\"a_dna_after\":%.9g,\"b_dna_before\":%.9g,\"b_dna_after\":%.9g,"
          "\"a_goal_before\":%.9g,\"a_goal_after\":%.9g,\"b_goal_before\":%.9g,\"b_goal_after\":%.9g,"
          "\"a_combat_before\":%.9g,\"a_combat_after\":%.9g,\"b_combat_before\":%.9g,\"b_combat_after\":%.9g,"
          "\"a_social_before\":%.9g,\"a_social_after\":%.9g,\"b_social_before\":%.9g,\"b_social_after\":%.9g",
          b_id, b_native_id, social_owner_target_id, double(amount), double(a_before.dna), double(a_after.dna),
          double(b_before.dna), double(b_after.dna), double(a_before.goal), double(a_after.goal), double(b_before.goal),
          double(b_after.goal), double(a_before.combat), double(a_after.combat), double(b_before.combat),
          double(b_after.combat), double(a_before.social), double(a_after.social), double(b_before.social),
          double(b_after.social));
}

void social_snapshot(const char* phase, uint64_t observation, const NativeSocialPayload& payload, bool decoded) {
    const bool context_valid = decoded && !context_active && Simulator::GetGameModeID() == kGameCreature;
    const auto actor = context_valid ? social_animal(payload.actor) : nullptr;
    const auto target = context_valid ? social_animal(payload.target) : nullptr;
    const auto first = context_valid ? first_player() : nullptr;
    const auto second = context_valid && ready ? native_second_player(b_epoch) : nullptr;
    const auto primary = first ? progress(first, Stage::Get()->mEvolutionPoints) : Progress{};
    const auto secondary = second ? progress(second, b_stage.mEvolutionPoints) : Progress{};
    const auto finite = [](const Progress& p) {
        return std::isfinite(p.dna) && std::isfinite(p.goal) && std::isfinite(p.combat) && std::isfinite(p.social);
    };
    const bool valid = context_valid && actor && target && first && finite(primary) && (!second || finite(secondary));
    if (!valid) {
        event(phase, ",\"social_call\":%llu,\"action\":%u,\"caller_rva\":%u,\"resolved\":false", observation,
              native_social_action, native_social_action_caller_rva);
        return;
    }
    // These are payload/campaign observations, not an attribution of the
    // initiating human. The original social routine already changed the
    // relationship before dispatching this action. Deferred DNA may follow.
    event(phase,
          ",\"social_call\":%llu,\"action\":%u,\"caller_rva\":%u,\"resolved\":true,"
          "\"payload_actor_native_id\":%u,\"payload_target_native_id\":%u,"
          "\"primary_player_native_id\":%u,\"secondary_present\":%s,\"secondary_player_native_id\":%u,"
          "\"primary_dna\":%.9g,\"primary_goal\":%.9g,\"primary_combat\":%.9g,\"primary_social\":%.9g,"
          "\"secondary_dna\":%.9g,\"secondary_goal\":%.9g,\"secondary_combat\":%.9g,\"secondary_social\":%.9g",
          observation, native_social_action, native_social_action_caller_rva, actor->mID, target->mID, first->mID,
          second ? "true" : "false", second ? second->mID : UINT32_MAX, double(primary.dna), double(primary.goal),
          double(primary.combat), double(primary.social), double(secondary.dna), double(secondary.goal),
          double(secondary.combat), double(secondary.social));
}

void pickup_progress_event(const char* name, const char* kind, uint32_t caller_rva, float amount,
                           const Progress& a_before, const Progress& a_after, const Progress& b_before,
                           const Progress& b_after) {
    event(name,
          ",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"reward_kind\":\"%s\",\"caller_rva\":%u,"
          "\"amount\":%.9g,\"a_dna_before\":%.9g,\"a_dna_after\":%.9g,\"b_dna_before\":%.9g,\"b_dna_after\":%.9g,"
          "\"a_goal_before\":%.9g,\"a_goal_after\":%.9g,\"b_goal_before\":%.9g,\"b_goal_after\":%.9g,"
          "\"a_combat_before\":%.9g,\"a_combat_after\":%.9g,\"b_combat_before\":%.9g,\"b_combat_after\":%.9g,"
          "\"a_social_before\":%.9g,\"a_social_after\":%.9g,\"b_social_before\":%.9g,\"b_social_after\":%.9g,"
          "\"scope_first_feed_return_observed\":%s,\"native_dna_increased\":%s,\"awards\":%llu",
          b_id, pickup_corpse_native_id, kind, caller_rva, double(amount), double(a_before.dna), double(a_after.dna),
          double(b_before.dna), double(b_after.dna), double(a_before.goal), double(a_after.goal), double(b_before.goal),
          double(b_after.goal), double(a_before.combat), double(a_after.combat), double(b_before.combat),
          double(b_after.combat), double(a_before.social), double(a_after.social), double(b_before.social),
          double(b_after.social), pickup_granted ? "true" : "false",
          std::isfinite(b_before.dna) && std::isfinite(b_after.dna) && b_after.dna > b_before.dna ? "true" : "false",
          awards);
}

// Enter only around a known native reward calculation/application, B counter,
// or the separately guarded original death/restart/hatch leaves.
// Whole world AI/physics never run under this switch. Scoped getters route
// identities without replacing the noun manager's avatar/player pointers.
class StageScope {
    Stage saved{};
    bool outer;

public:
    StageScope() : outer(!context_active) {
        if (outer) {
            saved = *Stage::Get();
            *Stage::Get() = b_stage;
            context_active = true;
        }
    }

    ~StageScope() {
        if (outer) {
            b_stage = *Stage::Get();
            *Stage::Get() = saved;
            context_active = false;
        }
    }
};

void* __fastcall species_hook(void* self, void*) {
    // 4DF490 always executes once. Only D2E720's exact return site selects
    // the already resident B profile, during explicit initial normalization.
    // The species manager's default key and map are never written here.
    const auto where = caller(_ReturnAddress());
    auto original = species_original(self);
    if (GetCurrentThreadId() == engine_thread && where == 0x92e740) {
        if (auto selected = native_scene_replica_normalization(self, original)) {
            return selected;
        }
    }
    if (GetCurrentThreadId() == engine_thread && foreign_initializing && context_active && where == 0x92e740 &&
        thread_check && thread_check() && live() && self == initialization_manager &&
        original == initialization_primary && readable(b_actor->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile)) &&
        b_actor->mpSpeciesProfile != initialization_primary &&
        b_actor->mpSpeciesProfile->mCreatureKey == b_actor->mSpeciesKey) {
        ++initialization_species_calls;
        return b_actor->mpSpeciesProfile;
    }
    return original;
}

// The two one-time Creature tutorials followed here through the original scheduler D389D0, by the name and the
// token the original passes for each.
constexpr char first_dna_tutorial_name[] = "CRG_FirstDNA";
constexpr char first_part_tutorial_name[] = "CRG_FirstPartUnlock";
constexpr uint32_t first_dna_tutorial_token = 0xea7666af;
constexpr uint32_t first_part_tutorial_token = 0xb721f66f;

template <size_t Size>
bool tutorial_named(const char* name, const char (&expected)[Size]) {
    return readable(name, Size) && !std::memcmp(name, expected, Size);
}

// One call of the original tutorial scheduler.
struct TutorialCall {
    uint32_t token = 0;
    uint32_t caller_rva = 0;
    // The first-part tutorial; when the call is followed and this is false, the first-DNA one.
    bool first_part = false;
    // One of the two tutorials, on the engine thread in Creature mode.
    bool followed = false;
    // Raised inside the second player's award scope, so for that player.
    bool secondary = false;
    Player* player = nullptr;
    uint32_t player_native_id = UINT32_MAX;
    // The number of this followed call; 0 when it is not followed.
    uint64_t serial = 0;
};

TutorialCall resolve_tutorial_call(const char* name, uint32_t token, uint32_t caller_rva) {
    TutorialCall call;
    call.token = token;
    call.caller_rva = caller_rva;
    call.first_part = token == first_part_tutorial_token && tutorial_named(name, first_part_tutorial_name);
    call.followed =
        GetCurrentThreadId() == engine_thread && thread_check && thread_check() &&
        Simulator::GetGameModeID() == kGameCreature &&
        (call.first_part || (token == first_dna_tutorial_token && tutorial_named(name, first_dna_tutorial_name)));
    call.secondary = call.followed && context_active && live();
    call.player = call.followed ? (call.secondary ? native_second_player(b_epoch) : first_player()) : nullptr;
    call.player_native_id = call.player ? call.player->mID : UINT32_MAX;
    call.serial = call.followed ? ++tutorial_observation : 0;
    return call;
}

// Whether a call is held back for its owner instead of being shown by the original now.
struct TutorialDeferral {
    bool wanted = false;
    uint32_t owner = 1;
    uint64_t actor_id = 0;
};

TutorialDeferral tutorial_deferral(const TutorialCall& call) {
    const auto worker_state = call.followed ? native_actor_worker_status() : worker::Message{};
    uint32_t primary_native_id = UINT32_MAX;
    const auto primary_avatar = call.followed && Manager::Get() ? Manager::Get()->GetAvatar() : nullptr;
    // The first player's own tutorial while the second player is of another species.
    const bool first_beside_foreign = call.followed && !call.secondary && foreign_context && !context_active &&
                                      live() && worker_state.epoch == b_epoch && worker_state.values[3] &&
                                      call.player && primary_avatar && !primary_avatar->mbDead &&
                                      native_actor_owner_native_id(1, primary_native_id) &&
                                      primary_avatar->mID == primary_native_id;
    TutorialDeferral deferral;
    deferral.owner = call.secondary ? 2u : 1u;
    deferral.actor_id = call.secondary ? b_id : worker_state.values[3];
    deferral.wanted = ((call.secondary &&
                        (defer_secondary_tutorial || (call.first_part && fossil_target && fossil_property_applied))) ||
                       first_beside_foreign) &&
                      call.caller_rva == 0x9399ff;
    return deferral;
}

// Queues a wanted tutorial for its owner. False when the queue refused it: the original then runs as usual.
bool defer_tutorial(const TutorialCall& call, const TutorialDeferral& deferral) {
    // D3CD70 uses C75820(false/true), which reads these exact pinned
    // combat/social fields. Preserve its native caption selection.
    const auto combat = call.player->mCombatTraitProgress, social = call.player->mSocialTraitProgress;
    const uint32_t caption = call.first_part      ? 0
                             : !combat && !social ? 0x04599f0e
                             : combat > social    ? 0x061d8186
                                                  : 0x061d8045;
    auto& pending = call.secondary ? pending_tutorial : primary_tutorial;
    const auto offered =
        std::isfinite(combat) && std::isfinite(social)
            ? pending.offer({b_epoch, deferral.actor_id, call.player_native_id, call.token, caption, deferral.owner})
            : TutorialOffer::invalid;
    if (offered == TutorialOffer::queued || offered == TutorialOffer::duplicate) {
        // D389D0 owns only the player's tutorial marker and the global
        // cinematic request. Defer before both, returning the original
        // scheduler's supported absent-request value. The caller skips
        // optional caption configuration and continues native progression.
        event(call.first_part ? "native_first_part_tutorial_deferred" : "native_first_dna_tutorial_deferred",
              ",\"call\":%llu,\"owner\":%u,\"actor\":%llu,\"player_native_id\":%u,\"token\":%u,\"duplicate\":%s,"
              "\"native_completion_written\":false,\"delivered\":false,\"checkpoint_blocked\":true",
              call.serial, deferral.owner, deferral.actor_id, call.player_native_id, call.token,
              offered == TutorialOffer::duplicate ? "true" : "false");
        return true;
    }
    // No loss on an unexpected queue conflict: preserve the original path
    // and its visible global limitation, and expose the failed deferral.
    event("native_first_dna_tutorial_deferral_rejected", ",\"call\":%llu,\"reason\":\"identity_conflict\"",
          call.serial);
    return false;
}

uint32_t __stdcall tutorial_hook(const char* name, uint32_t token) {
    const TutorialCall call = resolve_tutorial_call(name, token, caller(_ReturnAddress()));
    if (call.followed) {
        event(call.first_part ? "native_first_part_tutorial_enter" : "native_first_dna_tutorial_enter",
              ",\"call\":%llu,\"token\":%u,\"caller_rva\":%u,\"owner\":%u,\"actor\":%llu,\"player_native_id\":%u,"
              "\"secondary_award_scope\":%s",
              call.serial, token, call.caller_rva, call.secondary ? 2u : 1u, call.secondary ? b_id : 0,
              call.player_native_id, call.secondary ? "true" : "false");
    }
    const TutorialDeferral deferral = tutorial_deferral(call);
    if (deferral.wanted && defer_tutorial(call, deferral)) {
        return 0;
    }
    // Original player completion record and scheduler execute exactly once.
    // Observation does not suppress, reroute or acknowledge presentation.
    const auto result = tutorial_original(name, token);
    if (call.followed) {
        event(call.first_part ? "native_first_part_tutorial_return" : "native_first_dna_tutorial_return",
              ",\"call\":%llu,\"token\":%u,\"owner\":%u,\"player_native_id\":%u,\"native_request_present\":%s",
              call.serial, token, call.secondary ? 2u : 1u, call.player_native_id, result ? "true" : "false");
    }
    return result;
}

bool __fastcall owned_hook(Spatial* self, void*) {
    const auto where = caller(_ReturnAddress());
    const bool result = owned_original(self);
    if (!result && native_replica_hunger_owned(self, where)) {
        return true;
    }
    if (!result && GetCurrentThreadId() == engine_thread && (where == 0x807ac9 || where == 0x807b87) &&
        owned_damage() && self == static_cast<Spatial*>(b_actor)) {
        event("native_reward_owner_classified", ",\"owner\":2,\"actor\":%llu,\"caller_rva\":%u", b_id, where);
        return true;
    }
    return result;
}

float __fastcall amount_hook(Animal* self, void*, bool social) {
    if (GetCurrentThreadId() == engine_thread && caller(_ReturnAddress()) == 0x99bd0a && social &&
        self == social_owner_target && owned_social()) {
        StageScope scope;
        const float amount = amount_original(self, social);
        event("native_owned_social_amount", ",\"owner\":2,\"actor\":%llu,\"target_native_id\":%u,\"amount\":%.9g", b_id,
              self->mID, double(amount));
        return amount;
    }
    if (GetCurrentThreadId() != engine_thread || caller(_ReturnAddress()) != 0x807be4 || !owned_damage()) {
        return amount_original(self, social);
    }
    StageScope scope;
    const float amount = amount_original(self, social);
    event("native_owned_reward_amount", ",\"owner\":2,\"actor\":%llu,\"victim_native_id\":%u,\"amount\":%.9g", b_id,
          self->mID, double(amount));
    return amount;
}

Player* __fastcall player_hook(Manager* self, void*) {
    auto result = player_original(self);
    if (GetCurrentThreadId() == engine_thread &&
        (context_active || (caller(_ReturnAddress()) == 0x807c0a && owned_damage()) ||
         (caller(_ReturnAddress()) == 0x99bd34 && owned_social()) ||
         (caller(_ReturnAddress()) == 0x82efe7 && owned_objective() && objective_awarded))) {
        if (auto player = native_second_player(b_epoch)) {
            return player;
        }
    }
    return result;
}

Animal* __fastcall avatar_hook(Manager* self, void*) {
    auto result = avatar_original(self);
    if (GetCurrentThreadId() == engine_thread && caller(_ReturnAddress()) == 0x99bc07 && owned_social()) {
        return b_actor;
    }
    if (GetCurrentThreadId() == engine_thread && caller(_ReturnAddress()) == 0x82ef76 && owned_objective()) {
        return b_actor;
    }
    return GetCurrentThreadId() == engine_thread && context_active && ready ? b_actor : result;
}

void __fastcall display_hook(void* self, void*, float amount) {
    if (GetCurrentThreadId() == engine_thread && context_active && ready) {
        // Preserve the native add routine by adjusting its receiver so its own
        // +128 selects B's constructed counter. That routine touches no other field.
        display_original(
            reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(b_counter.data()) - display_counter_offset), amount);
        event("native_owned_reward_enqueued", ",\"owner\":2,\"actor\":%llu,\"amount\":%.9g", b_id, double(amount));
    } else {
        display_original(self, amount);
    }
}

// The id of the original fossil reward, as its producer at D86B91..D86BAE passes it to the action dispatch and as
// the property routine D47860 receives it.
constexpr uint32_t fossil_reward_action = 0xd3353638;

bool owned_fossil(void* payload) {
    if (GetCurrentThreadId() != engine_thread || !context_active || !fossil_target || !live() ||
        native_replica_is_client() || !thread_check || !thread_check() || !readable(payload, 3 * sizeof(uintptr_t))) {
        return false;
    }
    const auto words = static_cast<const uintptr_t*>(payload);
    if (words[0] != reinterpret_cast<uintptr_t>(b_actor) || words[1] != reinterpret_cast<uintptr_t>(fossil_target) ||
        words[2]) {
        return false;
    }
    for (auto& noun : Manager::Get()->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(fossil_target)) {
            return !noun.mbIsDestroyed && !noun.field_20 && noun.mID == fossil_target_id &&
                   noun.GetNounID() == Simulator::cInteractiveOrnament::NOUN_ID &&
                   readable(fossil_target, sizeof(*fossil_target)) &&
                   fossil_target->mType == Simulator::kOrnamentBone &&
                   fossil_target->mpWhoIsInteractingWithMe.get() == b_actor;
        }
    }
    return false;
}

uint32_t __fastcall property_hook(void* self, void*, uint32_t id, void* payload) {
    if (id != fossil_reward_action || !owned_fossil(payload)) {
        return property_original(self, id, payload);
    }
    // D47860 normally admits avatar-only properties through animal flag200.
    // Extend only this synchronous, owned fossil result. Never change the
    // animal flags or reward property, and preserve native lookup/application.
    auto property = property_lookup(self, id, fossil_target);
    uint32_t kind = UINT32_MAX;
    if (readable(property, 0x2c)) {
        std::memcpy(&kind, static_cast<unsigned char*>(property) + 4, 4);
    }
    if (kind != 6 || fossil_property_applied) {
        event("native_owned_fossil_property_rejected",
              ",\"owner\":2,\"actor\":%llu,\"target_native_id\":%u,\"property_kind\":%u,\"duplicate\":%s", b_id,
              fossil_target_id, kind, fossil_property_applied ? "true" : "false");
        return 0;
    }
    fossil_property_applied = true;
    const auto result = property_apply(property, fossil_target, b_actor);
    event("native_owned_fossil_property_return",
          ",\"owner\":2,\"actor\":%llu,\"target_native_id\":%u,\"property_kind\":%u,\"native_result\":%u,\"original_"
          "application_count\":1",
          b_id, fossil_target_id, kind, result);
    return result;
}

void clear_fossil_target() {
    fossil_target = nullptr;
    fossil_target_id = UINT32_MAX;
    fossil_property_applied = false;
}

// The payload of one fossil action: the acting creature, the fossil and a zero word.
struct FossilPayload {
    std::array<uintptr_t, 3> words{};
    bool decoded = false;
    // Native ids of the creature and of the fossil, each known only when its word is a current noun of that kind.
    uint32_t actor_native_id = UINT32_MAX, target_native_id = UINT32_MAX;
};

FossilPayload read_fossil_payload(void* payload) {
    FossilPayload fossil;
    fossil.decoded = readable(payload, sizeof(fossil.words));
    if (fossil.decoded) {
        std::memcpy(fossil.words.data(), payload, sizeof(fossil.words));
    }
    auto manager = Manager::Get();
    if (fossil.decoded && fossil.words[2] == 0 && manager) {
        for (auto& noun : manager->mNouns) {
            if (noun.mbIsDestroyed || noun.field_20) {
                continue;
            }
            if (reinterpret_cast<uintptr_t>(&noun) == fossil.words[0] && noun.GetNounID() == Animal::NOUN_ID) {
                fossil.actor_native_id = noun.mID;
            }
            if (reinterpret_cast<uintptr_t>(&noun) == fossil.words[1] && noun.GetNounID() == 0x03a2511e) {
                fossil.target_native_id = noun.mID;
            }
        }
    }
    return fossil;
}

// The second player's fossil: the original action runs once under that player's Stage, with the fossil pinned for
// property_hook. False when the claim was refused before anything ran; the original is then not called at all.
bool run_owned_fossil_action(Strategy* self, uint32_t id, void* payload, const FossilPayload& fossil) {
    if (context_active || fossil_target || !native_second_player(b_epoch) || !first_player()) {
        event("native_owned_fossil_action_rejected", ",\"owner\":2,\"actor\":%llu", b_id);
        return false;
    }
    fossil_target = reinterpret_cast<Simulator::cInteractiveOrnament*>(fossil.words[1]);
    fossil_target_id = fossil.target_native_id;
    fossil_property_applied = false;
    {
        StageScope scope;
        if (owned_fossil(payload)) {
            action_original(self, id, payload);
        } else {
            event("native_owned_fossil_action_rejected", ",\"owner\":2,\"actor\":%llu,\"reason\":\"claim_or_identity\"",
                  b_id);
        }
    }
    event("native_owned_fossil_action_return",
          ",\"owner\":2,\"actor\":%llu,\"target_native_id\":%u,\"original_property_applied\":%s", b_id,
          fossil_target_id, fossil_property_applied ? "true" : "false");
    clear_fossil_target();
    return true;
}

void fossil_action(Strategy* self, uint32_t id, void* payload, uint32_t caller_rva) {
    // D86B91..D86BAE passes exactly three initialized words to the already
    // qualified action thiscall. Observe the original producer, not an
    // invented UnlockPart call: native fossil rewards use the row path.
    static uint64_t observation = 0;
    const FossilPayload fossil = read_fossil_payload(payload);
    const auto serial = observation < 16 ? ++observation : 0;
    const bool observed = serial && fossil.actor_native_id != UINT32_MAX && fossil.target_native_id != UINT32_MAX;
    const bool secondary =
        fossil.decoded && fossil.target_native_id != UINT32_MAX && fossil.actor_native_id == b_native_id &&
        fossil.words[0] == reinterpret_cast<uintptr_t>(b_actor) && live() && !native_replica_is_client();
    if (observed) {
        event("native_fossil_action_enter",
              ",\"observation\":%llu,\"action\":%u,\"caller_rva\":%u,\"actor_native_id\":%u,\"target_native_id\":%"
              "u,\"observation_only\":%s",
              serial, id, caller_rva, fossil.actor_native_id, fossil.target_native_id, secondary ? "false" : "true");
        observe_native_player_checkpoint(serial, "fossil_before");
    }
    if (secondary) {
        if (!run_owned_fossil_action(self, id, payload, fossil)) {
            return;
        }
    } else {
        action_original(self, id, payload);
    }
    if (observed) {
        observe_native_player_checkpoint(serial, "fossil_after");
        event("native_fossil_action_return",
              ",\"observation\":%llu,\"payload_unchanged\":true,\"secondary_owner_scope\":%s", serial,
              secondary ? "true" : "false");
    }
}

void social_action(Strategy* self, uint32_t id, void* payload, uint32_t caller_rva) {
    NativeSocialPayload words;
    const bool decoded = decode_native_social_payload(id, caller_rva, payload,
                                                      readable(payload, sizeof(words)) ? sizeof(words) : 0, words);
    const auto observation = ++social_observation;
    social_snapshot("native_social_action_enter", observation, words, decoded);
    if (social_owner_actor) {
        const Players players = players_when(owned_social());
        if (!players.first || context_active || !decoded || words.actor != reinterpret_cast<uintptr_t>(b_actor) ||
            words.target != reinterpret_cast<uintptr_t>(social_owner_target)) {
            event("native_owned_social_action_rejected", ",\"owner\":2,\"actor\":%llu", b_id);
            return;
        }
        const ProgressPair before = progress_pair(players);
        // Only the original synchronous progression action selects B's
        // existing Stage/player. The complete social AI tick does not.
        {
            StageScope scope;
            action_original(self, id, payload);
        }
        social_progress_event("native_owned_social_action_applied", 0, before.a, before.b);
    } else {
        action_original(self, id, payload);
    }
    social_snapshot("native_social_action_return", observation, words, decoded);
}

void pickup_action(Strategy* self, uint32_t id, void* payload, uint32_t caller_rva) {
    // D713B7..D713D7 initializes exactly three words. A fourth SDK-inferred
    // payload word is not present at this callsite and must never be read.
    std::array<uintptr_t, 3> words{};
    const bool valid_payload = readable(payload, sizeof(words));
    if (valid_payload) {
        std::memcpy(words.data(), payload, sizeof(words));
    }
    const Players players = players_when(owned_pickup());
    if (!players.second || !players.first || context_active || !valid_payload ||
        words[0] != reinterpret_cast<uintptr_t>(pickup_actor) ||
        words[1] != reinterpret_cast<uintptr_t>(pickup_corpse) || words[2] != 0) {
        event("native_owned_pickup_action_rejected",
              ",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"action\":%u,\"caller_rva\":%u", pickup_id,
              pickup_corpse_native_id, id, caller_rva);
        return;
    }
    const ProgressPair before = progress_pair(players);
    // Only this original action (and its synchronous native handlers) uses
    // B's campaign/player getters. The whole eating tick stays unmodified.
    {
        StageScope scope;
        action_original(self, id, payload);
    }
    const ProgressPair after = progress_pair(players);
    pickup_progress_event("native_owned_pickup_action_applied", "eat_meat_action", caller_rva, 0, before.a, after.a,
                          before.b, after.b);
}

// The original action runs once and may set the strategy's field_48. Under the second player's Stage the value it
// leaves is that player's pending world input (b_world_pending); during an update selected for the second player it
// is kept as the first player's (world_saved_a). In both cases the strategy gets back the value it had before.
void world_pending_action(Strategy* self, uint32_t id, void* payload) {
    const bool selected_b = context_active;
    const bool previous = self->field_48;
    action_original(self, id, payload);
    if (selected_b) {
        b_world_pending = self->field_48;
        self->field_48 = previous;
        event("native_owned_reward_action", ",\"owner\":2,\"actor\":%llu,\"action\":%u,\"world_pending\":%s", b_id, id,
              b_world_pending ? "true" : "false");
    } else if (world_selected) {
        world_saved_a = self->field_48;
        self->field_48 = previous;
    }
}

// Routes one original action: the second player's fossil, social and corpse results run under that player's Stage,
// and everything else reaches the original unchanged.
void __fastcall action_hook(Strategy* self, void*, uint32_t id, void* payload) {
    if (!native_replica_allows_action(id)) {
        return;
    }
    const auto caller_rva = caller(_ReturnAddress());
    if (GetCurrentThreadId() == engine_thread && id == fossil_reward_action && caller_rva == 0x986bb3 && thread_check &&
        thread_check() && Simulator::GetGameModeID() == kGameCreature) {
        fossil_action(self, id, payload, caller_rva);
        return;
    }
    if (GetCurrentThreadId() == engine_thread && id == native_social_action &&
        caller_rva == native_social_action_caller_rva && thread_check && thread_check() &&
        Simulator::GetGameModeID() == kGameCreature) {
        social_action(self, id, payload, caller_rva);
        return;
    }
    if (GetCurrentThreadId() == engine_thread && id == native_pickup_action && caller_rva == 0x9713dc &&
        pickup_owner == 2) {
        pickup_action(self, id, payload, caller_rva);
        return;
    }
    if (GetCurrentThreadId() != engine_thread || id != 0x045ab96e || !ready) {
        action_original(self, id, payload);
        return;
    }
    world_pending_action(self, id, payload);
}

float __cdecl goal_hook() {
    if (GetCurrentThreadId() == engine_thread && caller(_ReturnAddress()) == 0x945aa9 && live() && b_world_pending) {
        auto strategy = Strategy::Get();
        auto a = avatar_original(Manager::Get());
        // Current M03 fixture uses the same native species profile. Divergent
        // species require a separate qualified world-selection binding.
        if (strategy && a && a->mpSpeciesProfile == b_actor->mpSpeciesProfile &&
            (!strategy->field_48 || alternate_world)) {
            world_saved_a = strategy->field_48;
            world_selected = true;
            strategy->field_48 = b_world_pending;
            StageScope scope;
            return goal_original();
        }
    }
    return goal_original();
}

void __fastcall update_hook(Strategy* self, void*, float dt, float elapsed) {
    if (GetCurrentThreadId() != engine_thread || !ready) {
        update_original(self, dt, elapsed);
        return;
    }
    world_selected = false;
    // The original complete mode update executes exactly once per engine call.
    // Only its existing herd-evolution input uses a pending B goal on selected ticks.
    update_original(self, dt, elapsed);
    if (world_selected) {
        b_world_pending = self->field_48;
        self->field_48 = world_saved_a;
        ++world_updates;
        if (world_updates <= 3 || !b_world_pending) {
            event("native_owned_world_update", ",\"owner\":2,\"actor\":%llu,\"updates\":%llu,\"pending\":%s", b_id,
                  world_updates, b_world_pending ? "true" : "false");
        }
    }
    world_selected = false;
    alternate_world = !alternate_world;
}
} // namespace

NativePickupOwnerScope::NativePickupOwnerScope(Animal* actor, uint32_t owner, uint64_t id, Animal* corpse)
    : previous_actor(pickup_actor),
      previous_corpse(pickup_corpse),
      previous_owner(pickup_owner),
      previous_corpse_native_id(pickup_corpse_native_id),
      previous_id(pickup_id),
      previous_granted(pickup_granted) {
    pickup_actor = actor;
    pickup_corpse = corpse;
    pickup_owner = owner;
    pickup_id = id;
    pickup_corpse_native_id = UINT32_MAX;
    pickup_granted = false;
    if (GetCurrentThreadId() == engine_thread && thread_check && thread_check()) {
        if (auto current = current_corpse(corpse)) {
            pickup_corpse_native_id = current->mID;
        }
    }
}

NativePickupOwnerScope::~NativePickupOwnerScope() {
    pickup_actor = previous_actor;
    pickup_corpse = previous_corpse;
    pickup_owner = previous_owner;
    pickup_corpse_native_id = previous_corpse_native_id;
    pickup_id = previous_id;
    pickup_granted = previous_granted;
}

bool native_pickup_award_scope_ready() {
    return owned_pickup();
}

NativeSocialOwnerScope::NativeSocialOwnerScope(Animal* actor, Animal* target, uint64_t actor_id)
    : previous_actor(social_owner_actor),
      previous_target(social_owner_target),
      previous_id(social_owner_id),
      previous_target_id(social_owner_target_id) {
    social_owner_actor = actor;
    social_owner_target = target;
    social_owner_id = actor_id;
    auto current = social_animal(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)));
    social_owner_target_id = current ? current->mID : UINT32_MAX;
}

NativeSocialOwnerScope::~NativeSocialOwnerScope() {
    social_owner_actor = previous_actor;
    social_owner_target = previous_target;
    social_owner_id = previous_id;
    social_owner_target_id = previous_target_id;
}

bool native_social_award_scope_ready() {
    return owned_social() && b_actor->mpCombatantTarget == static_cast<Simulator::cCombatant*>(social_owner_target) &&
           social_owner_target->mpCombatantTarget == static_cast<Simulator::cCombatant*>(b_actor);
}

void sample_native_social_owner_result(bool entering, uint64_t call, uint32_t animation) {
    const auto name = entering ? "native_social_owner_enter" : "native_social_owner_return";
    auto second = owned_social() && !context_active ? native_second_player(b_epoch) : nullptr;
    auto first = second ? first_player() : nullptr;
    if (!first) {
        event(name, ",\"result_call\":%llu,\"resolved\":false", call);
        return;
    }
    if (entering) {
        objective_receipt = {};
        objective_before_valid =
            relationship_state(social_owner_target->mSpeciesKey, objective_before_count, objective_before_relation);
    }
    const auto a = progress(first, Stage::Get()->mEvolutionPoints), b = progress(second, b_stage.mEvolutionPoints);
    event(name,
          ",\"result_call\":%llu,\"resolved\":true,\"owner\":2,\"actor\":%llu,"
          "\"actor_native_id\":%u,\"target_native_id\":%u,\"primary_player_native_id\":%u,\"secondary_player_native_"
          "id\":%u,"
          "\"animation\":%u,\"primary_dna\":%.9g,\"primary_goal\":%.9g,\"primary_combat\":%.9g,\"primary_social\":%.9g,"
          "\"secondary_dna\":%.9g,\"secondary_goal\":%.9g,\"secondary_combat\":%.9g,\"secondary_social\":%.9g",
          call, b_id, b_native_id, social_owner_target_id, first->mID, second->mID, animation, double(a.dna),
          double(a.goal), double(a.combat), double(a.social), double(b.dna), double(b.goal), double(b.combat),
          double(b.social));
    if (!entering) {
        objective_result = call;
        objective_epoch = b_epoch;
        objective_species = social_owner_target->mSpeciesKey;
        objective_samples_left = 16;
        uint32_t count = 0, relation = 0;
        const bool after_valid = relationship_state(objective_species, count, relation);
        const bool transition = objective_before_valid && after_valid && objective_before_count != UINT32_MAX &&
                                count == objective_before_count + 1 && objective_before_relation != 6 && relation == 6;
        if (transition) {
            objective_receipt = {
                b_epoch,     b_id,
                call,        GetTickCount64() + 1000,
                b_native_id, objective_before_count,
                count,       objective_before_relation,
                relation,    {objective_species.instanceID, objective_species.typeID, objective_species.groupID},
                true};
        }
        event("native_social_relationship_transition",
              ",\"result_call\":%llu,\"actor\":%llu,\"before_valid\":%s,\"after_valid\":%s,\"count_before\":%u,\"count_"
              "after\":%u,\"relation_before\":%u,\"relation_after\":%u,\"objective_receipt_pending\":%s",
              call, b_id, objective_before_valid ? "true" : "false", after_valid ? "true" : "false",
              objective_before_count, count, objective_before_relation, relation, transition ? "true" : "false");
    }
}

namespace {
bool social_objective_snapshot(void* receiver, uint64_t call, uint32_t caller_rva, bool entering) {
    if (GetCurrentThreadId() != engine_thread || !thread_check || !thread_check() || native_replica_is_client() ||
        context_active || !live() || objective_epoch != b_epoch || !readable(receiver, 0xc8)) {
        return false;
    }
    const auto bytes = static_cast<const unsigned char*>(receiver);
    ResourceKey species;
    memcpy(&species, bytes + 0x80, sizeof(species));
    if (species != objective_species) {
        return false;
    }
    auto first = first_player(), second = native_second_player(b_epoch);
    if (!first || !second) {
        return false;
    }
    uintptr_t vtable = 0;
    memcpy(&vtable, bytes, 4);
    uint32_t fields[12]{};
    constexpr size_t offsets[] = {0x7c, 0x8c, 0x90, 0x94, 0x98, 0x9c, 0xa0, 0xa4, 0xa8, 0xb0, 0xbc, 0xc0};
    for (size_t i = 0; i < std::size(offsets); ++i) {
        memcpy(&fields[i], bytes + offsets[i], 4);
    }
    const auto a = progress(first, Stage::Get()->mEvolutionPoints), b = progress(second, b_stage.mEvolutionPoints);
    event(entering ? "native_social_objective_enter" : "native_social_objective_return",
          ",\"call\":%llu,\"recent_result\":%llu,\"actor\":%llu,\"caller_rva\":%u,\"vtable_rva\":%u,\"species\":[%u,%u,"
          "%u],"
          "\"fields_7c_8c_90_94_98_9c_a0_a4_a8_b0_bc_c0\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],"
          "\"primary_dna\":%.9g,\"primary_goal\":%.9g,\"primary_combat\":%.9g,\"primary_social\":%.9g,"
          "\"secondary_dna\":%.9g,\"secondary_goal\":%.9g,\"secondary_combat\":%.9g,\"secondary_social\":%.9g,"
          "\"observation_only\":true",
          call, objective_result, b_id, caller_rva,
          vtable >= image_base ? static_cast<uint32_t>(vtable - image_base) : 0, species.instanceID, species.typeID,
          species.groupID, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7],
          fields[8], fields[9], fields[10], fields[11], double(a.dna), double(a.goal), double(a.combat),
          double(a.social), double(b.dna), double(b.goal), double(b.combat), double(b.social));
    return true;
}
} // namespace

uint64_t begin_native_social_objective_observation(void* receiver, uintptr_t caller_address) {
    // An event source does not establish ownership of this shared objective
    // receiver. Foreign relationship storage does not admit objective routing.
    if (GetCurrentThreadId() != engine_thread || foreign_context || !objective_samples_left || objective_active) {
        return 0;
    }
    const auto call = objective_observation + 1;
    if (!social_objective_snapshot(
            receiver, call, caller_address >= image_base ? static_cast<uint32_t>(caller_address - image_base) : 0,
            true)) {
        return 0;
    }
    objective_observation = call;
    --objective_samples_left;
    const auto words = static_cast<const uint32_t*>(receiver);
    uint32_t count = 0, relation = 0;
    if (caller_address == image_base + 0x82fbd4 && words[0] == image_base + 0x106b3c0 && words[0x7c / 4] != 5 &&
        words[0xa4 / 4] && relationship_state(objective_species, count, relation) &&
        objective_receipt.claim(b_epoch, b_id, b_native_id,
                                {objective_species.instanceID, objective_species.typeID, objective_species.groupID},
                                words[0x8c / 4], words[0xa0 / 4], count, relation, words[0xbc / 4], GetTickCount64())) {
        objective_active = true;
        objective_awarded = false;
        objective_active_call = call;
        event("native_social_objective_owner_claimed",
              ",\"call\":%llu,\"result_call\":%llu,\"actor\":%llu,\"count_before\":%u,\"count_after\":%u,\"one_shot\":"
              "true",
              call, objective_receipt.result, b_id, objective_receipt.before_count, count);
    }
    return call;
}

void end_native_social_objective_observation(void* receiver, uint64_t call) {
    if (call) {
        social_objective_snapshot(receiver, call, 0, false);
    }
    if (call && call == objective_active_call) {
        event("native_social_objective_owner_released", ",\"call\":%llu,\"actor\":%llu,\"native_award_returned\":%s",
              call, b_id, objective_awarded ? "true" : "false");
        objective_active = false;
        objective_awarded = false;
        objective_active_call = 0;
    }
}

bool native_pickup_award_scope_granted() {
    return GetCurrentThreadId() == engine_thread && pickup_owner == 2 && pickup_granted;
}

NativeDamageOwnerScope::NativeDamageOwnerScope(Animal* actor, uint32_t owner, uint64_t id)
    : previous_actor(damage_actor),
      previous_owner(damage_owner),
      previous_id(damage_id),
      previous_foreign_latched(damage_foreign_latched),
      previous_foreign_epoch(damage_foreign_epoch) {
    damage_actor = actor;
    damage_owner = owner;
    damage_id = id;
    damage_foreign_latched = foreign_context && owner == 2 && actor == b_actor && id == b_id && live();
    damage_foreign_epoch = damage_foreign_latched ? b_epoch : 0;
}

NativeDamageOwnerScope::~NativeDamageOwnerScope() {
    damage_actor = previous_actor;
    damage_owner = previous_owner;
    damage_id = previous_id;
    damage_foreign_latched = previous_foreign_latched;
    damage_foreign_epoch = previous_foreign_epoch;
}

#define BINDING(name, original, hook)                                                                                  \
    NativeHook name() {                                                                                                \
        return {reinterpret_cast<void**>(&original), reinterpret_cast<void*>(hook)};                                   \
    }
BINDING(native_award_owned_binding, owned_original, owned_hook)
BINDING(native_award_amount_binding, amount_original, amount_hook)
BINDING(native_award_player_binding, player_original, player_hook)
BINDING(native_award_avatar_binding, avatar_original, avatar_hook)
BINDING(native_award_display_binding, display_original, display_hook)
BINDING(native_award_action_binding, action_original, action_hook)
BINDING(native_award_property_binding, property_original, property_hook)
BINDING(native_award_update_binding, update_original, update_hook)
BINDING(native_award_goal_binding, goal_original, goal_hook)
BINDING(native_award_tutorial_binding, tutorial_original, tutorial_hook)
BINDING(native_award_species_binding, species_original, species_hook)
#undef BINDING

namespace {
// The result scopes, the objective and tutorial observations and the pending tutorials belong to no owner yet.
void reset_result_scopes() {
    clear_fossil_target();
    social_owner_actor = social_owner_target = nullptr;
    social_owner_id = 0;
    social_owner_target_id = UINT32_MAX;
    objective_observation = objective_result = objective_epoch = 0;
    objective_samples_left = 0;
    objective_species = {};
    objective_receipt = {};
    objective_before_valid = objective_active = objective_awarded = false;
    objective_active_call = 0;
    tutorial_observation = 0;
    defer_secondary_tutorial = false;
    pending_tutorial.retire();
    primary_tutorial.retire();
    reset_native_tutorial_presentation();
}

// Compares every pinned code prefix with the loaded image and reports the first that differs. `count` receives the
// number of prefixes.
bool code_prefixes_match(uintptr_t base, uintptr_t end, unsigned& count) {
    // Generated from the already SHA-256-pinned PE. Relocation entries compare
    // their exact rebased address; they are never wildcarded.
    struct Prefix {
        uint32_t rva;
        unsigned char bytes[20];
        uint32_t relocations;
        unsigned length;
    };

    static const Prefix prefixes[] = {
#include "native_award_prefixes.inc"
    };
    count = unsigned(std::size(prefixes));
    for (const auto& prefix : prefixes) {
        unsigned char bytes[20];
        memcpy(bytes, prefix.bytes, sizeof(bytes));
        for (unsigned i = 0; i + 4 <= prefix.length; ++i) {
            if (prefix.relocations & (1u << i)) {
                uint32_t value;
                memcpy(&value, bytes + i, 4);
                value += static_cast<uint32_t>(base) - 0x400000;
                memcpy(bytes + i, &value, 4);
            }
        }
        if (prefix.rva > end - base || prefix.length > end - base - prefix.rva ||
            memcmp(reinterpret_cast<void*>(base + prefix.rva), bytes, prefix.length)) {
            event("award_binding_rejected", ",\"rva\":%u", prefix.rva);
            return false;
        }
    }
    return true;
}

// The original functions this file hooks or calls, at the RVAs whose prefixes were just compared.
void bind_originals(uintptr_t base) {
    owned_original = reinterpret_cast<Owned>(base + 0x802ff0);
    amount_original = reinterpret_cast<Amount>(base + 0x8042a0);
    player_original = reinterpret_cast<PlayerGet>(base + 0xb67d40);
    avatar_original = reinterpret_cast<AvatarGet>(base + 0x71fd00);
    species_original = reinterpret_cast<Abi::SpeciesGet>(base + 0xdf490);
    display_original = reinterpret_cast<DisplayAdd>(base + 0x92e2e0);
    action_original = reinterpret_cast<Action>(base + 0x939360);
    property_original = reinterpret_cast<Abi::PropertyAction>(base + 0x947860);
    property_lookup = reinterpret_cast<Abi::PropertyLookup>(base + 0x9475e0);
    property_apply = reinterpret_cast<Abi::PropertyApply>(base + 0x946be0);
    update_original = reinterpret_cast<Update>(base + 0x945740);
    goal_original = reinterpret_cast<Goal>(base + 0x92e360);
    tutorial_original = reinterpret_cast<Tutorial>(base + 0x9389d0);
    tutorial_has = reinterpret_cast<Abi::TutorialHas>(base + 0x877df0);
    tutorial_record = reinterpret_cast<Abi::TutorialRecord>(base + 0x8785c0);
    cinematic_play = reinterpret_cast<Abi::CinematicPlay>(base + 0x6e08b0);
    cinematic_caption = reinterpret_cast<Abi::CinematicCaption>(base + 0x6dde10);
    relationship_manager = reinterpret_cast<Abi::RelationshipManager>(base + 0x73d5c0);
    relationship_find = reinterpret_cast<Abi::RelationshipFind>(base + 0x7a3190);
    deque_init = reinterpret_cast<InitDeque>(base + 0x92c490);
    deque_destroy = reinterpret_cast<ObjectCall>(base + 0x92c940);
    timer_init = reinterpret_cast<ObjectCall>(base + 0x7639d0);
    timer_destroy = reinterpret_cast<ObjectCall>(base + 0x7639f0);
    counter_tick = reinterpret_cast<CounterTick>(base + 0x92d0b0);
    stage_reset = reinterpret_cast<StageReset>(base + 0x92e980);
}
} // namespace

bool prepare_native_awards(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event_sink,
                           bool (*check)()) {
    image_base = base;
    engine_thread = thread;
    sink = event_sink;
    thread_check = check;
    reset_result_scopes();
    unsigned prefix_count = 0;
    if (!code_prefixes_match(base, end, prefix_count)) {
        return false;
    }
    bind_originals(base);
    event("award_bindings_checked", ",\"code_prefixes\":%u", prefix_count);
    return true;
}

namespace {
// A second player of another species than the first is admitted only with the pinned species manager, both species
// profiles readable and matching their creatures, a herd of its own, and the first player's species as the manager's
// default.
bool foreign_species_admissible(Animal* actor, Animal* avatar, Editors::cSpeciesManager* species_manager) {
    static_assert(sizeof(Simulator::cSpeciesProfile) == 0xa18);
    if (!readable(species_manager, 0xe4) ||
        *reinterpret_cast<const uintptr_t*>(species_manager) != image_base + 0xff08c8 ||
        *reinterpret_cast<const uintptr_t*>(image_base + 0xff08c8) != image_base + 0xdb410 ||
        *reinterpret_cast<const uintptr_t*>(image_base + 0xff08cc) != image_base + 0xdcf40 ||
        !readable(actor->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile)) ||
        !readable(avatar->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile)) || !actor->mHerd ||
        actor->mHerd == avatar->mHerd || actor->mHerd->mpSpeciesProfile != actor->mpSpeciesProfile ||
        actor->mpSpeciesProfile->mCreatureKey != actor->mSpeciesKey || !actor->mpSpeciesProfile->creatureSeq ||
        !std::isfinite(actor->mpSpeciesProfile->mBoundingBox.lower.z) ||
        actor->mpSpeciesProfile->mBoundingBox.lower.z <= 0 ||
        avatar->mpSpeciesProfile->mCreatureKey != avatar->mSpeciesKey ||
        species_manager->mDefaultKey != avatar->mSpeciesKey || actor->mSpeciesKey == avatar->mSpeciesKey) {
        return false;
    }
    return true;
}

// Native stage configuration, same addition order as D43E76..D43EAE.
float native_goal_total() {
    float total = Stage::GetEvoPointsToNextBrainLevel(3);
    for (int level = 2; level >= 0; --level) {
        total += Stage::GetEvoPointsToNextBrainLevel(level);
    }
    return total;
}

// The identity the hooks compare against.
void bind_owner(Animal* actor, uint64_t actor_id, uint64_t epoch) {
    b_actor = actor;
    b_native_id = actor->mID;
    b_id = actor_id;
    b_epoch = epoch;
}

// The second player's display counter, built by the original constructors of its deque and its timer.
void construct_counter() {
    b_counter.fill(0);
    deque_init(b_counter.data() + counter_deque_offset, 0);
    timer_init(b_counter.data() + counter_timer_offset);
}

// A foreign second player's Stage initialisation: who takes part, and what the first player's side held before it.
struct ForeignInitialization {
    Manager* manager;
    Player* first;
    Animal* avatar;
    Animal* actor;
    Editors::cSpeciesManager* species_manager;
    Simulator::cSpeciesProfile* primary_profile;
    Simulator::cSpeciesProfile* secondary_profile;
    Progress primary_progress;
    float primary_total;
    ResourceKey default_key;
};

ForeignInitialization before_initialization(Manager* manager, Player* first, Animal* avatar, Animal* actor,
                                            Editors::cSpeciesManager* species_manager, const ResourceKey& default_key) {
    return {manager,
            first,
            avatar,
            actor,
            species_manager,
            avatar->mpSpeciesProfile,
            actor->mpSpeciesProfile,
            progress(first, Stage::Get()->mEvolutionPoints),
            first->mGoalProgressTotal,
            default_key};
}

// Builds the second player's Stage with the original reset and normalisation, all of it under that Stage. While a
// foreign normalisation runs, species_hook answers its one species query with the second player's profile.
void initialize_secondary_stage(Animal* actor, Editors::cSpeciesManager* species_manager,
                                Simulator::cSpeciesProfile* primary_profile, bool foreign) {
    StageScope scope;
    stage_reset();
    Stage::Get()->mCurrentBrainLevel = actor->GetCurrentBrainLevel();
    initialization_manager = species_manager;
    initialization_primary = primary_profile;
    foreign_initializing = foreign;
    Stage::CalculateAvatarNormalizingScale();
    foreign_initializing = false;
    initialization_manager = nullptr;
    initialization_primary = nullptr;
}

// Records a foreign initialisation, and tells whether it left the first player's side as it was and gave the second
// player a usable Stage through exactly one redirected species query.
bool foreign_initialization_valid(uint64_t actor_id, const ForeignInitialization& before, const Stage& primary_stage) {
    const auto after = progress(before.first, Stage::Get()->mEvolutionPoints);
    const bool unchanged =
        !memcmp(&primary_stage, Stage::Get(), sizeof(primary_stage)) && before.manager->GetAvatar() == before.avatar &&
        before.manager->GetPlayer() == before.first && before.avatar->mpSpeciesProfile == before.primary_profile &&
        before.actor->mpSpeciesProfile == before.secondary_profile &&
        before.species_manager->mDefaultKey == before.default_key &&
        before.first->mGoalProgressTotal == before.primary_total && before.primary_progress.dna == after.dna &&
        before.primary_progress.goal == after.goal && before.primary_progress.combat == after.combat &&
        before.primary_progress.social == after.social;
    const bool valid = unchanged && initialization_species_calls == 1 && b_stage.mCurrentBrainLevel == 0 &&
                       std::isfinite(b_stage.mAvatarNormalizingScale) && b_stage.mAvatarNormalizingScale > 0;
    const auto& primary_species = before.avatar->mSpeciesKey;
    const auto& secondary_species = before.actor->mSpeciesKey;
    event("native_foreign_stage_initialization",
          ",\"actor\":%llu,\"primary_species\":[%u,%u,%u],\"secondary_species\":[%u,%u,%u],"
          "\"normalization_species_calls\":%u,\"secondary_profile_dimension_z\":%.9g,\"primary_scale\":%.9g,"
          "\"secondary_scale\":%.9g,\"primary_unchanged\":%s,\"valid\":%s,"
          "\"checkpoint_schema\":4,\"checkpoint_qualified\":false,\"world_progress_qualified\":false,"
          "\"experimental\":true",
          actor_id, primary_species.instanceID, primary_species.typeID, primary_species.groupID,
          secondary_species.instanceID, secondary_species.typeID, secondary_species.groupID,
          initialization_species_calls, double(before.secondary_profile->mBoundingBox.lower.z),
          double(primary_stage.mAvatarNormalizingScale), double(b_stage.mAvatarNormalizingScale),
          unchanged ? "true" : "false", valid ? "true" : "false");
    return valid;
}
} // namespace

bool enable_native_awards(Animal* actor, uint64_t actor_id, uint64_t epoch, bool experimental_foreign) {
    if (GetCurrentThreadId() != engine_thread || ready || !actor || actor->mbDead || !thread_check || !thread_check()) {
        return false;
    }
    auto player = native_second_player(epoch);
    auto manager = Manager::Get();
    auto first = manager ? manager->GetPlayer() : nullptr;
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    auto strategy = Strategy::Get();
    if (!player || !first || !avatar || !strategy || !strategy->mpDisplayStrategy ||
        (!experimental_foreign && actor->mpSpeciesProfile != avatar->mpSpeciesProfile) ||
        actor->GetCurrentBrainLevel() != 0 || player->mCurrentGoalProgress != 0 || player->mGoalProgressTotal != 1) {
        return false;
    }
    const bool foreign = actor->mpSpeciesProfile != avatar->mpSpeciesProfile;
    auto species_manager = foreign ? Editors::cSpeciesManager::Get() : nullptr;
    ResourceKey default_key{};
    if (foreign) {
        if (!foreign_species_admissible(actor, avatar, species_manager)) {
            return false;
        }
        default_key = species_manager->mDefaultKey;
    }
    const float total = native_goal_total();
    if (!std::isfinite(total) || total <= 0 || total != first->mGoalProgressTotal) {
        return false;
    }
    bind_owner(actor, actor_id, epoch);
    b_stage = {};
    retired_actor_id = retired_epoch = 0;
    retired_dna_valid = false;
    construct_counter();
    ready = true;
    suspended_for_death = false;
    foreign_context = foreign;
    const auto primary_stage = *Stage::Get();
    const ForeignInitialization before =
        before_initialization(manager, first, avatar, actor, species_manager, default_key);
    initialization_species_calls = 0;
    initialize_secondary_stage(actor, species_manager, before.primary_profile, foreign);
    if (foreign && !foreign_initialization_valid(actor_id, before, primary_stage)) {
        retire_native_awards();
        return false;
    }
    player->mGoalProgressTotal = total;
    if (foreign &&
        (!enable_native_relationships(actor, actor_id, epoch) || !defer_native_secondary_tutorial(actor_id, epoch))) {
        retire_native_awards();
        return false;
    }
    b_world_pending = false;
    boundary_reported = false;
    awards = counter_updates = world_updates = 0;
    event("native_awards_enabled",
          ",\"owner\":2,\"actor\":%llu,\"player_native_id\":%u,\"goal_total\":%.9g,\"brain\":%d", b_id, player->mID,
          double(total), b_stage.mCurrentBrainLevel);
    sample_native_awards();
    return true;
}

bool native_awards_ready(uint64_t actor_id, uint64_t epoch) {
    return actor_id && epoch && actor_id == b_id && epoch == b_epoch && live();
}

bool defer_native_secondary_tutorial(uint64_t actor_id, uint64_t epoch) {
    if (!native_awards_ready(actor_id, epoch) || context_active || pending_tutorial.pending()) {
        return false;
    }
    defer_secondary_tutorial = true;
    event("native_secondary_tutorial_deferral_enabled", ",\"owner\":2,\"actor\":%llu,\"delivered\":false", actor_id);
    return true;
}

bool pending_native_tutorial(NativeTutorialRequest& request, uint32_t owner) {
    request = {};
    if ((owner != 1 && owner != 2) || GetCurrentThreadId() != engine_thread || context_active || !live()) {
        return false;
    }
    const auto& pending = owner == 1 ? primary_tutorial : pending_tutorial;
    if (!pending.pending()) {
        return false;
    }
    const auto& value = pending.request();
    if (value.epoch != b_epoch || value.owner != owner) {
        return false;
    }
    if (owner == 1) {
        const auto state = native_actor_worker_status();
        auto avatar = Manager::Get()->GetAvatar();
        if (!foreign_context || state.epoch != value.epoch || state.values[3] != value.actor || !avatar ||
            avatar->mbDead || !first_player() || first_player()->mID != value.player) {
            return false;
        }
    }
    request = value;
    return true;
}

bool complete_native_tutorial(const NativeTutorialRequest& request) {
    NativeTutorialRequest current;
    if (!pending_native_tutorial(current, request.owner) || request.epoch != current.epoch ||
        request.actor != current.actor || request.player != current.player || request.token != current.token ||
        request.caption != current.caption) {
        return false;
    }
    auto second = native_second_player(current.epoch), first = first_player();
    if (!second || !first || first == second) {
        return false;
    }
    const bool primary = current.owner == 1;
    auto recipient = primary ? first : second, peer = primary ? second : first;
    if (recipient->mID != current.player) {
        return false;
    }
    const uint32_t key[] = {current.token, 0, 0x68dd6929};
    const bool peer_before = tutorial_has(peer, key), owner_before = tutorial_has(recipient, key);
    const auto a = progress(first, Stage::Get()->mEvolutionPoints), b = progress(second, b_stage.mEvolutionPoints);
    if (!owner_before) {
        tutorial_record(recipient, key);
    }
    const auto a_after = progress(first, Stage::Get()->mEvolutionPoints),
               b_after = progress(second, b_stage.mEvolutionPoints);
    const bool recorded = tutorial_has(recipient, key), peer_unchanged = tutorial_has(peer, key) == peer_before;
    const bool progress_unchanged = a.dna == a_after.dna && a.goal == a_after.goal && a.combat == a_after.combat &&
                                    a.social == a_after.social && b.dna == b_after.dna && b.goal == b_after.goal &&
                                    b.combat == b_after.combat && b.social == b_after.social;
    event(current.token == first_part_tutorial_token ? "native_first_part_tutorial_completion"
                                                     : "native_first_dna_tutorial_completion",
          ",\"owner\":%u,\"actor\":%llu,\"player_native_id\":%u,\"token\":%u,\"already_recorded\":%s,\"recorded\":%s,"
          "\"peer_unchanged\":%s,\"primary_unchanged\":%s,\"progress_unchanged\":%s",
          current.owner, current.actor, current.player, current.token, owner_before ? "true" : "false",
          recorded ? "true" : "false", peer_unchanged ? "true" : "false",
          (!primary && peer_unchanged) ? "true" : "false", progress_unchanged ? "true" : "false");
    if (!recorded || !peer_unchanged || !progress_unchanged) {
        return false;
    }
    (primary ? primary_tutorial : pending_tutorial).retire();
    return true;
}

void reset_native_tutorial_presentation() {
    // This clears correlation only; it never forces a native cinematic exit.
    tutorial_presentation = NativeTutorialPresentation::idle;
    presentation_id = 0;
    presentation_paused = false;
}

bool present_native_tutorial(uint32_t token, uint32_t caption) {
    const bool part = token == first_part_tutorial_token && !caption;
    if (GetCurrentThreadId() != engine_thread || !thread_check || !thread_check() || !native_replica_is_client() ||
        Simulator::GetGameModeID() != kGameCreature || tutorial_presentation != NativeTutorialPresentation::idle ||
        !(part || (token == first_dna_tutorial_token &&
                   (caption == 0x04599f0e || caption == 0x061d8186 || caption == 0x061d8045)))) {
        return false;
    }
    auto manager = Simulator::cCinematicManager::Get();
    auto time = Simulator::cGameTimeManager::Get();
    if (!manager || !readable(manager, sizeof(*manager)) || manager->field_2C || !time || time->mFlags) {
        return false;
    }
    // Presentation only: bypass D389D0's player-completion write. The server
    // retains that native write until this exact client observes its cycle.
    presentation_id =
        cinematic_play(manager, part ? first_part_tutorial_name : first_dna_tutorial_name, 1, 1, nullptr, 0, 0);
    if (!presentation_id) {
        return false;
    }
    const uint32_t key[] = {caption, 0, 0x9569a644};
    if (!part) {
        cinematic_caption(manager, 0x7046e98b, key, presentation_id);
    }
    tutorial_presentation = NativeTutorialPresentation::active;
    presentation_paused = false;
    event("native_tutorial_client_queued", ",\"token\":%u,\"caption\":%u,\"native_request\":%u,\"completed\":false",
          token, caption, presentation_id);
    return true;
}

void observe_native_tutorial_pause(bool pausing, uint32_t type, uint32_t caller_rva, int counter, int flags) {
    if (GetCurrentThreadId() != engine_thread || tutorial_presentation != NativeTutorialPresentation::active ||
        type != 0x0600c4ae) {
        return;
    }
    auto manager = Simulator::cCinematicManager::Get();
    uint32_t selected = 0;
    if (manager && readable(manager, sizeof(*manager))) {
        std::memcpy(&selected, reinterpret_cast<const unsigned char*>(manager) + 0x148, 4);
    }
    if (selected != presentation_id) {
        tutorial_presentation = NativeTutorialPresentation::failed;
        event("native_tutorial_client_identity_changed",
              ",\"expected_request\":%u,\"selected_request\":%u,\"completed\":false", presentation_id, selected);
        return;
    }
    if (pausing && caller_rva == 0x6e1fd0 && counter == 1 && flags == 3) {
        presentation_paused = true;
    }
    if (!pausing && caller_rva == 0x6e1f46 && !counter && !flags && presentation_paused) {
        tutorial_presentation = NativeTutorialPresentation::completed;
    }
    event("native_tutorial_client_cycle",
          ",\"native_request\":%u,\"pausing\":%s,\"caller_rva\":%u,\"counter_after\":%d,\"flags_after\":%d,"
          "\"completed\":%s",
          presentation_id, pausing ? "true" : "false", caller_rva, counter, flags,
          tutorial_presentation == NativeTutorialPresentation::completed ? "true" : "false");
}

NativeTutorialPresentation native_tutorial_presentation() {
    return tutorial_presentation;
}

uint32_t native_awards_checkpoint_busy(uint64_t epoch) {
    uint32_t busy = 0;
    if (GetCurrentThreadId() != engine_thread) {
        return 1;
    }
    if (epoch != b_epoch) {
        busy |= 2;
    }
    if (!live()) {
        busy |= 4;
    }
    if (context_active) {
        busy |= 8;
    }
    if (foreign_context) {
        busy |= 0x10;
    }
    if (foreign_context && !native_relationship_checkpoint_idle(epoch)) {
        busy |= 0x20;
    }
    if (world_selected) {
        busy |= 0x40;
    }
    if (damage_actor || pickup_actor || social_owner_actor) {
        busy |= 0x80;
    }
    if (objective_active || (objective_receipt.pending && GetTickCount64() < objective_receipt.expires)) {
        busy |= 0x100;
    }
    if (pending_tutorial.pending() || primary_tutorial.pending()) {
        busy |= 0x200;
    }
    auto strategy = Strategy::Get();
    if (!strategy || !strategy->mpDisplayStrategy) {
        return busy | 0x400;
    }
    if (!counter_idle(primary_counter(strategy))) {
        busy |= 0x800;
    }
    if (!counter_idle(b_counter.data())) {
        busy |= 0x1000;
    }
    return busy;
}

bool native_awards_checkpoint_idle(uint64_t epoch, bool allow_foreign) {
    if (GetCurrentThreadId() != engine_thread || epoch != b_epoch || !live() || context_active ||
        (foreign_context && (!allow_foreign || !native_relationship_checkpoint_idle(epoch))) || world_selected ||
        damage_actor || pickup_actor || social_owner_actor || objective_active ||
        (objective_receipt.pending && GetTickCount64() < objective_receipt.expires) || pending_tutorial.pending() ||
        primary_tutorial.pending()) {
        return false;
    }
    auto strategy = Strategy::Get();
    if (!strategy || !strategy->mpDisplayStrategy) {
        return false;
    }
    // The original primary display and our original-constructed secondary
    // display use the same audited counter layout. Neither pending animation
    // queue is serialized by this extension: require both to have drained.
    return counter_idle(primary_counter(strategy)) && counter_idle(b_counter.data());
}

namespace {
uint32_t float_word(float value) {
    uint32_t result;
    memcpy(&result, &value, 4);
    return result;
}

float word_float(uint32_t value) {
    float result;
    memcpy(&result, &value, 4);
    return result;
}

Stage checkpoint_stage(const NativeAwardCheckpoint& checkpoint) {
    const auto& w = checkpoint.words;
    Stage value{};
    value.mCurrentBrainLevel = static_cast<int>(w[0]);
    value.mAvatarNormalizingScale = word_float(w[1]);
    value.mNumFlapsAllowed = static_cast<int>(w[2]);
    value.field_C = (w[3] & 1) != 0;
    value.field_D = (w[3] & 2) != 0;
    value.field_E = (w[3] & 4) != 0;
    value.field_F = (w[3] & 8) != 0;
    value.field_10 = (w[4] & 1) != 0;
    value.field_11 = (w[4] & 2) != 0;
    value.field_14 = static_cast<int>(w[5]);
    value.field_18 = {word_float(w[6]), word_float(w[7]), word_float(w[8])};
    value.mAbilityMode = static_cast<Stage::AbilityMode>(w[9]);
    value.mEvolutionPoints = word_float(w[10]);
    value.DEPRECATED_mLearnedAbilities = uint64_t(w[12]) | (uint64_t(w[13]) << 32);
    for (size_t i = 0; i < 3; ++i) {
        value.mLearnedAbilities[i] = static_cast<int>(w[14 + i]);
    }
    value.mGlideStartPosition = {word_float(w[17]), word_float(w[18]), word_float(w[19])};
    return value;
}
} // namespace

bool valid_native_award_checkpoint(const NativeAwardCheckpoint& checkpoint) {
    const auto& w = checkpoint.words;
    if (w[0] > 3 || w[2] > 1000 || w[3] > 15 || w[4] > 3 || w[9] > 1 || w[11] > 1) {
        return false;
    }
    for (auto i : {1, 6, 7, 8, 10, 17, 18, 19}) {
        if (!std::isfinite(word_float(w[i]))) {
            return false;
        }
    }
    return word_float(w[1]) > 0 && word_float(w[10]) >= 0;
}

bool snapshot_native_awards(NativeAwardCheckpoint& checkpoint, uint64_t epoch, bool allow_foreign) {
    checkpoint = {};
    if (!native_awards_checkpoint_idle(epoch, allow_foreign)) {
        return false;
    }
    auto& w = checkpoint.words;
    const auto& v = b_stage;
    w[0] = v.mCurrentBrainLevel;
    w[1] = float_word(v.mAvatarNormalizingScale);
    w[2] = v.mNumFlapsAllowed;
    w[3] = uint32_t(v.field_C) | (uint32_t(v.field_D) << 1) | (uint32_t(v.field_E) << 2) | (uint32_t(v.field_F) << 3);
    w[4] = uint32_t(v.field_10) | (uint32_t(v.field_11) << 1);
    w[5] = v.field_14;
    w[6] = float_word(v.field_18.x);
    w[7] = float_word(v.field_18.y);
    w[8] = float_word(v.field_18.z);
    w[9] = static_cast<uint32_t>(v.mAbilityMode);
    w[10] = float_word(v.mEvolutionPoints);
    // Native herd evolution may remain pending across an ordinary save. Keep
    // its owner-specific input instead of dropping it or replaying the award.
    w[11] = b_world_pending ? 1u : 0u;
    w[12] = static_cast<uint32_t>(v.DEPRECATED_mLearnedAbilities);
    w[13] = static_cast<uint32_t>(v.DEPRECATED_mLearnedAbilities >> 32);
    for (size_t i = 0; i < 3; ++i) {
        w[14 + i] = v.mLearnedAbilities[i];
    }
    w[17] = float_word(v.mGlideStartPosition.x);
    w[18] = float_word(v.mGlideStartPosition.y);
    w[19] = float_word(v.mGlideStartPosition.z);
    return valid_native_award_checkpoint(checkpoint);
}

bool write_native_secondary_award_stage(uint64_t epoch, uint32_t player_native_id, uint32_t avatar_native_id,
                                        Simulator::ISerializerWriteStream* stream, NativeAwardCheckpoint& observed) {
    if (!stream || !stream->IsGood() || !snapshot_native_awards(observed, epoch) || observed.words[11] || !b_actor ||
        b_actor->mID != avatar_native_id) {
        return false;
    }
    auto player = native_second_player(epoch);
    auto strategy = Strategy::Get();
    if (!player || player->mID != player_native_id || !strategy || strategy->field_48) {
        return false;
    }
    unsigned char expected[] = {0x81, 0xec, 0x14, 0x0a, 0, 0, 0x68, 0x26, 0x0d, 0xa8, 0x01, 0x68, 0, 0, 0, 0};
    const auto descriptor = static_cast<uint32_t>(image_base + 0x11829b8);
    memcpy(expected + 12, &descriptor, 4);
    if (memcmp(reinterpret_cast<const void*>(image_base + 0x92e300), expected, sizeof(expected))) {
        return false;
    }
    std::array<unsigned char, sizeof(Stage)> primary{}, secondary{};
    memcpy(primary.data(), Stage::Get(), primary.size());
    memcpy(secondary.data(), &b_stage, secondary.size());
    const bool written = reinterpret_cast<bool(__thiscall*)(Stage*, Simulator::ISerializerStream*)>(
        image_base + 0x92e300)(&b_stage, stream);
    const bool unchanged =
        !memcmp(primary.data(), Stage::Get(), primary.size()) && !memcmp(secondary.data(), &b_stage, secondary.size());
    if (!unchanged) {
        event("native_secondary_stage_write_changed_state");
        TerminateProcess(GetCurrentProcess(), 0x4d10);
        __fastfail(7);
    }
    NativeAwardCheckpoint after;
    return written && stream->IsGood() && snapshot_native_awards(after, epoch) && after.words == observed.words;
}

bool restore_native_awards(Animal* actor, uint64_t id, uint64_t epoch, const NativeAwardCheckpoint& checkpoint,
                           bool foreign_checkpoint) {
    if (GetCurrentThreadId() != engine_thread || ready || !actor || !id || actor->mbDead || !thread_check ||
        !thread_check() || !valid_native_award_checkpoint(checkpoint)) {
        return false;
    }
    auto player = native_second_player(epoch);
    auto manager = Manager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    auto strategy = Strategy::Get();
    if (!player || !avatar || !strategy || !strategy->mpDisplayStrategy ||
        (actor->mpSpeciesProfile != avatar->mpSpeciesProfile) != foreign_checkpoint ||
        actor->GetCurrentBrainLevel() != int(checkpoint.words[0]) || !std::isfinite(player->mCurrentGoalProgress) ||
        !std::isfinite(player->mGoalProgressTotal) || player->mCurrentGoalProgress < 0 ||
        player->mGoalProgressTotal <= 0) {
        return false;
    }
    if (foreign_checkpoint) {
        auto species = Editors::cSpeciesManager::Get();
        if (!readable(species, 0xe4) || !readable(actor->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile)) ||
            !readable(avatar->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile)) ||
            actor->mSpeciesKey == avatar->mSpeciesKey || actor->mpSpeciesProfile->mCreatureKey != actor->mSpeciesKey ||
            avatar->mpSpeciesProfile->mCreatureKey != avatar->mSpeciesKey ||
            species->mDefaultKey != avatar->mSpeciesKey || !actor->mHerd || actor->mHerd == avatar->mHerd ||
            actor->mHerd->mpSpeciesProfile != actor->mpSpeciesProfile || checkpoint.words[0]) {
            return false;
        }
    }
    bind_owner(actor, id, epoch);
    b_stage = checkpoint_stage(checkpoint);
    construct_counter();
    retired_actor_id = retired_epoch = 0;
    retired_dna_valid = false;
    b_world_pending = checkpoint.words[11] != 0;
    boundary_reported = false;
    awards = counter_updates = world_updates = 0;
    ready = true;
    suspended_for_death = false;
    foreign_context = foreign_checkpoint;
    if (foreign_checkpoint &&
        (!adopt_native_relationship_checkpoint(actor, id, epoch) || !defer_native_secondary_tutorial(id, epoch))) {
        retire_native_awards();
        return false;
    }
    event("native_awards_restored",
          ",\"owner\":2,\"actor\":%llu,\"player_native_id\":%u,\"foreign_species\":%s,\"replayed_rewards\":0", id,
          player->mID, foreign_checkpoint ? "true" : "false");
    sample_native_awards();
    return true;
}

bool native_awards_read_dna(uint64_t actor_id, uint64_t epoch, float& dna, bool allow_retired_corpse,
                            bool* current_native) {
    if (current_native) {
        *current_native = false;
    }
    if (GetCurrentThreadId() != engine_thread || !actor_id || !epoch) {
        return false;
    }
    if (actor_id == b_id && epoch == b_epoch && owner_present() && (allow_retired_corpse || live()) &&
        std::isfinite(b_stage.mEvolutionPoints) && b_stage.mEvolutionPoints >= 0) {
        dna = b_stage.mEvolutionPoints;
        if (current_native) {
            *current_native = true;
        }
        return true;
    }
    if (allow_retired_corpse && retired_dna_valid && actor_id == retired_actor_id && epoch == retired_epoch) {
        dna = retired_dna;
        return true;
    }
    return false;
}

namespace {
using AwardOriginal = void(__cdecl*)(float);

// The DNA of the social objective the second player claimed: applied once under that player's Stage.
void award_social_objective(float amount, AwardOriginal original) {
    const Players players = players_when(owned_objective());
    if (!players.first || !original || context_active || objective_awarded || !std::isfinite(amount) || amount < 0) {
        event("native_owned_social_objective_rejected", ",\"call\":%llu,\"actor\":%llu", objective_active_call, b_id);
        return;
    }
    const ProgressPair before = progress_pair(players);
    {
        StageScope scope;
        original(amount);
    }
    objective_awarded = true;
    ++awards;
    event("native_owned_social_objective_award",
          ",\"call\":%llu,\"actor\":%llu,\"amount\":%.9g,\"a_dna_before\":%.9g,\"a_dna_after\":%.9g,\"b_dna_"
          "before\":%.9g,\"b_dna_after\":%.9g,\"a_goal_before\":%.9g,\"a_goal_after\":%.9g,\"b_goal_before\":%.9g,"
          "\"b_goal_after\":%.9g",
          objective_active_call, b_id, double(amount), double(before.a.dna), double(Stage::Get()->mEvolutionPoints),
          double(before.b.dna), double(b_stage.mEvolutionPoints), double(before.a.goal),
          double(players.first->mCurrentGoalProgress), double(before.b.goal),
          double(players.second->mCurrentGoalProgress));
}

// The DNA of a social result the second player initiated.
void award_social_result(float amount, AwardOriginal original) {
    const Players players = players_when(owned_social());
    if (!players.first || !original || context_active) {
        event("native_owned_social_reward_rejected", ",\"owner\":2,\"actor\":%llu", b_id);
        return;
    }
    const ProgressPair before = progress_pair(players);
    {
        StageScope scope;
        original(amount);
    }
    ++awards;
    social_progress_event("native_owned_social_reward_applied", amount, before.a, before.b);
}

// The DNA of the first feed on a corpse the second player claimed.
void award_corpse_first_feed(float amount, AwardOriginal original, uint32_t caller_rva) {
    const Players players = players_when(owned_pickup());
    if (!players.second || !players.first || context_active || !original) {
        event("native_owned_pickup_reward_rejected",
              ",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"caller_rva\":%u", pickup_id,
              pickup_corpse_native_id, caller_rva);
        return;
    }
    const ProgressPair before = progress_pair(players);
    {
        StageScope scope;
        original(amount);
    }
    // D713A3 reads the configured original float, which can legitimately be
    // zero. D2E8A0 then returns without changing DNA. For positive amounts
    // its original cap may retain or reduce the expected increase. Observe
    // the returned call; never replace its amount or infer nutrition here.
    const float dna_after = b_stage.mEvolutionPoints;
    const float uncapped = before.b.dna + amount;
    const bool returned = std::isfinite(amount) && amount >= 0 && std::isfinite(before.b.dna) &&
                          std::isfinite(dna_after) && std::isfinite(uncapped) && dna_after >= before.b.dna &&
                          (amount == 0 ? dna_after == before.b.dna : dna_after <= uncapped);
    pickup_granted = pickup_granted || returned;
    ++awards;
    const ProgressPair after = progress_pair(players);
    pickup_progress_event("native_owned_pickup_reward_applied", "corpse_first_feed", caller_rva, amount, before.a,
                          after.a, before.b, after.b);
}

// D46BE0's optional property DNA effect is already inside EatMeat's
// StageScope. Preserve one original application; do not consult the
// unrelated damage scope or enter another owner context.
void award_corpse_property(float amount, AwardOriginal original, uint32_t caller_rva) {
    if (!owned_pickup() || !original) {
        event("native_owned_pickup_property_reward_rejected",
              ",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"caller_rva\":%u", pickup_id,
              pickup_corpse_native_id, caller_rva);
        return;
    }
    const float dna_before = Stage::Get()->mEvolutionPoints;
    original(amount);
    ++awards;
    event("native_owned_pickup_property_reward_applied",
          ",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,"
          "\"reward_kind\":\"corpse_property\",\"caller_rva\":%u,\"amount\":%.9g,\"dna_before\":%.9g,\"dna_after\":"
          "%.9g,\"awards\":%llu",
          b_id, pickup_corpse_native_id, caller_rva, double(amount), double(dna_before),
          double(Stage::Get()->mEvolutionPoints), awards);
}

// Each player's progress with its goal total, before and after a combat award to a second player of another species.
void foreign_combat_progress_event(float amount, uint32_t caller_rva, const Players& players,
                                   const ProgressPair& before, float a_total_before, float b_total_before) {
    const ProgressPair after = progress_pair(players);
    event("native_foreign_combat_reward_progress",
          ",\"actor\":%llu,\"caller_rva\":%u,\"amount\":%.9g,\"awards\":%llu,"
          "\"a_before\":[%.9g,%.9g,%.9g,%.9g,%.9g],\"a_after\":[%.9g,%.9g,%.9g,%.9g,%.9g],"
          "\"b_before\":[%.9g,%.9g,%.9g,%.9g,%.9g],\"b_after\":[%.9g,%.9g,%.9g,%.9g,%.9g]",
          b_id, caller_rva, double(amount), awards, double(before.a.dna), double(before.a.goal), double(a_total_before),
          double(before.a.combat), double(before.a.social), double(after.a.dna), double(after.a.goal),
          double(players.first->mGoalProgressTotal), double(after.a.combat), double(after.a.social),
          double(before.b.dna), double(before.b.goal), double(b_total_before), double(before.b.combat),
          double(before.b.social), double(after.b.dna), double(after.b.goal),
          double(players.second->mGoalProgressTotal), double(after.b.combat), double(after.b.social));
}

// The DNA of damage the second player dealt. The caller has established owned_damage().
void award_damage(float amount, AwardOriginal original, uint32_t caller_rva) {
    Players players;
    players.second = native_second_player(b_epoch);
    players.first = first_player();
    const ProgressPair before{players.first ? progress(players.first, Stage::Get()->mEvolutionPoints) : Progress{},
                              progress(players.second, b_stage.mEvolutionPoints)};
    const float a_total = players.first ? players.first->mGoalProgressTotal : 0,
                b_total = players.second->mGoalProgressTotal;
    const float dna_before = b_stage.mEvolutionPoints, goal_before = players.second->mCurrentGoalProgress;
    {
        StageScope scope;
        original(amount);
    }
    ++awards;
    event("native_owned_reward_applied",
          ",\"owner\":2,\"actor\":%llu,\"amount\":%.9g,\"dna_before\":%.9g,\"dna_after\":%.9g,"
          "\"goal_before\":%.9g,\"goal_after\":%.9g,\"awards\":%llu",
          b_id, double(amount), double(dna_before), double(b_stage.mEvolutionPoints), double(goal_before),
          double(players.second->mCurrentGoalProgress), awards);
    if (foreign_context && players.first) {
        foreign_combat_progress_event(amount, caller_rva, players, before, a_total, b_total);
    }
}
} // namespace

// Applies one original DNA award for the second player when the call site and the open result scope say it is that
// player's. True when the award was handled here, applied or refused; false leaves it to the caller.
bool dispatch_native_owned_award(float amount, void(__cdecl* original)(float), uint32_t caller_rva) {
    if (GetCurrentThreadId() == engine_thread && caller_rva == 0x82efce && objective_active) {
        award_social_objective(amount, original);
        return true;
    }
    if (GetCurrentThreadId() == engine_thread && caller_rva == 0x99bd1b && social_owner_actor) {
        award_social_result(amount, original);
        return true;
    }
    if (GetCurrentThreadId() == engine_thread && caller_rva == native_pickup_reward_caller_rva && pickup_owner == 2) {
        award_corpse_first_feed(amount, original, caller_rva);
        return true;
    }
    if (GetCurrentThreadId() == engine_thread && context_active && pickup_owner == 2 && caller_rva == 0x946cd3) {
        award_corpse_property(amount, original, caller_rva);
        return true;
    }
    if (GetCurrentThreadId() != engine_thread || caller_rva != 0x807bf1 || !owned_damage()) {
        return false;
    }
    award_damage(amount, original, caller_rva);
    return true;
}

bool call_native_secondary_death(uint32_t native_id, uint64_t epoch, bool (*call)(Animal*, void*), void* context) {
    if (!call || !thread_check || !thread_check() || native_replica_is_client() || context_active || world_selected ||
        foreign_context || !owner_present() || epoch != b_epoch || native_id != b_native_id || !Stage::Get() ||
        b_stage.mCurrentBrainLevel != 0) {
        return false;
    }
    const auto primary = *Stage::Get();
    bool result = false;
    {
        StageScope scope;
        result = call(b_actor, context);
    }
    const bool unchanged = !memcmp(&primary, Stage::Get(), sizeof(primary));
    if (!unchanged) {
        event("native_death_primary_stage_changed", ",\"owner\":2,\"native_id\":%u", native_id);
    }
    return result && unchanged;
}

void update_native_awards(uint64_t epoch) {
    if (GetCurrentThreadId() != engine_thread || !ready) {
        return;
    }
    sample_native_relationships(epoch);
    present_native_relationship_goals(epoch);
    if (epoch != b_epoch || !owner_present()) {
        retire_native_awards();
        return;
    }
    // Death does not remove the owned native noun or player. Keep its Stage,
    // original counter queue and pending tutorial for the same identity. No
    // reward/world update runs for a corpse; live-only checkpoint admission
    // also remains closed until a separately qualified native revival.
    if (b_actor->mbDead) {
        if (!suspended_for_death) {
            sample_native_awards();
            event("native_awards_suspended",
                  ",\"owner\":2,\"actor\":%llu,\"native_id\":%u,\"reason\":\"native_owner_dead\",\"queued_work_"
                  "retained\":true",
                  b_id, b_native_id);
        }
        suspended_for_death = true;
        return;
    }
    if (suspended_for_death) {
        suspended_for_death = false;
        event("native_awards_resumed", ",\"owner\":2,\"actor\":%llu,\"native_id\":%u", b_id, b_native_id);
    }
    auto player = native_second_player(epoch);
    // M03 qualifies the first native brain-level segment. Native brain upgrades
    // also touch shared species/cinematics, so retain queued work at that boundary
    // until the later campaign progression adapter is qualified. Never drop it.
    const float threshold = Stage::GetEvoPointsToNextBrainLevel(0);
    if (player->mCurrentGoalProgress >= threshold) {
        if (!boundary_reported) {
            event("native_reward_stage_boundary", ",\"owner\":2,\"actor\":%llu,\"queued_work_retained\":true", b_id);
        }
        boundary_reported = true;
        return;
    }
    const auto before = *reinterpret_cast<uintptr_t*>(b_counter.data() + 0xc);
    {
        StageScope scope;
        counter_tick(b_counter.data(), 0);
    }
    ++counter_updates;
    if (before != *reinterpret_cast<uintptr_t*>(b_counter.data() + 0xc)) {
        event("native_owned_reward_dequeued", ",\"owner\":2,\"actor\":%llu,\"counter_updates\":%llu", b_id,
              counter_updates);
    }
}

void sample_native_social_ui(uint64_t epoch) {
    if (GetCurrentThreadId() != engine_thread || !epoch || !thread_check || !thread_check() || context_active ||
        Simulator::GetGameModeID() != kGameCreature) {
        return;
    }
    auto strategy = Strategy::Get();
    auto display = readable(strategy, sizeof(Strategy)) ? strategy->mpDisplayStrategy.get() : nullptr;
    auto ui =
        readable(display, sizeof(Simulator::cCreatureDisplayStrategy)) ? display->mpUISocialRainbow.get() : nullptr;
    NativeSocialMeter meter;
    const bool resolved =
        readable(ui, native_social_ui_read_size) &&
        decode_native_social_meter(ui, native_social_ui_read_size,
                                   static_cast<uint32_t>(image_base + native_social_ui_vtable_rva), meter);
    static uint64_t last_epoch = 0;
    static bool last_resolved = false;
    static NativeSocialMeter last;
    if (epoch == last_epoch && resolved == last_resolved && (!resolved || meter == last)) {
        return;
    }
    last_epoch = epoch;
    last_resolved = resolved;
    last = meter;
    if (!resolved) {
        event("native_social_ui", ",\"schema\":1,\"resolved\":false");
        return;
    }
    event("native_social_ui",
          ",\"schema\":1,\"resolved\":true,\"field_24\":%u,\"field_25\":%u,\"field_26\":%u,"
          "\"field_44\":%u,\"field_3c\":%.9g,\"field_40\":%.9g,\"field_134\":%.9g,\"field_138\":%.9g,"
          "\"field_13c\":%.9g,\"field_1d8\":%.9g,\"field_1dc\":%.9g,\"field_1e0\":%.9g",
          unsigned(meter.flags[0]), unsigned(meter.flags[1]), unsigned(meter.flags[2]), meter.field_44,
          double(meter.values[0]), double(meter.values[1]), double(meter.values[2]), double(meter.values[3]),
          double(meter.values[4]), double(meter.values[5]), double(meter.values[6]), double(meter.values[7]));
}

void sample_native_awards() {
    if (GetCurrentThreadId() != engine_thread || !ready) {
        return;
    }
    auto player = native_second_player(b_epoch);
    if (!player) {
        return;
    }
    const bool empty = counter_queue_empty(b_counter.data());
    event("native_owned_reward_state",
          ",\"owner\":2,\"actor\":%llu,\"dna\":%.9g,\"goal\":%.9g,\"goal_total\":%.9g,"
          "\"combat_progress\":%.9g,\"brain\":%d,\"queue_empty\":%s,\"counter_state\":%u,\"counter_updates\":%llu,"
          "\"world_updates\":%llu,\"awards\":%llu",
          b_id, double(b_stage.mEvolutionPoints), double(player->mCurrentGoalProgress),
          double(player->mGoalProgressTotal), double(player->mCombatTraitProgress), b_stage.mCurrentBrainLevel,
          empty ? "true" : "false", *reinterpret_cast<uint32_t*>(b_counter.data()), counter_updates, world_updates,
          awards);
    // Read the same original per-player key lookup used by completion. A fresh
    // load must supply its own native evidence; the saved file/hash alone does
    // not establish that the original reader restored this one-time event.
    auto first = first_player();
    if (!context_active && tutorial_has && first && first != player) {
        const uint32_t key[] = {first_dna_tutorial_token, 0, 0x68dd6929};
        event("native_first_dna_tutorial_state",
              ",\"actor\":%llu,\"primary_player\":%u,\"secondary_player\":%u,\"token\":3933628079,\"primary_"
              "completed\":%s,\"secondary_completed\":%s,\"pending\":%s,\"primary_pending\":%s",
              b_id, first->mID, player->mID, tutorial_has(first, key) ? "true" : "false",
              tutorial_has(player, key) ? "true" : "false",
              (pending_tutorial.pending() || primary_tutorial.pending()) ? "true" : "false",
              primary_tutorial.pending() ? "true" : "false");
    }
}

void retire_native_awards() {
    if (GetCurrentThreadId() != engine_thread || !ready) {
        return;
    }
    retire_native_relationships();
    sample_native_awards();
    if (pending_tutorial.pending()) {
        event("native_first_dna_tutorial_cancelled",
              ",\"owner\":2,\"actor\":%llu,\"token\":%u,\"reason\":\"native_owner_retired\",\"delivered\":false,"
              "\"native_completion_written\":false",
              pending_tutorial.request().actor, pending_tutorial.request().token);
    }
    if (primary_tutorial.pending()) {
        event("native_first_dna_tutorial_cancelled",
              ",\"owner\":1,\"actor\":%llu,\"token\":%u,\"reason\":\"native_owner_retired\",\"delivered\":false,"
              "\"native_completion_written\":false",
              primary_tutorial.request().actor, primary_tutorial.request().token);
    }
    pending_tutorial.retire();
    primary_tutorial.retire();
    defer_secondary_tutorial = false;
    objective_samples_left = 0;
    objective_epoch = 0;
    objective_receipt = {};
    objective_before_valid = objective_active = objective_awarded = false;
    objective_active_call = 0;
    retired_actor_id = b_id;
    retired_epoch = b_epoch;
    retired_dna = b_stage.mEvolutionPoints;
    retired_dna_valid = std::isfinite(retired_dna) && retired_dna >= 0;
    deque_destroy(b_counter.data() + counter_deque_offset);
    timer_destroy(b_counter.data() + counter_timer_offset);
    ready = false;
    suspended_for_death = false;
    b_actor = nullptr;
    b_native_id = UINT32_MAX;
    foreign_initializing = foreign_context = false;
    initialization_manager = nullptr;
    initialization_primary = nullptr;
    event("native_awards_retired",
          ",\"owner\":2,\"actor\":%llu,\"awards\":%llu,\"counter_updates\":%llu,\"world_updates\":%llu", b_id, awards,
          counter_updates, world_updates);
    b_id = b_epoch = 0;
    b_world_pending = false;
}
} // namespace sporemp
