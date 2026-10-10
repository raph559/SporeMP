#include "native_actors.h"
#include "../network/steering.h"
#include "actor_commands.h"
#include "native_actor_abi.h"
#include "native_pool_abi.h"
#include "native_player_context.h"
#include "native_award_context.h"
#include "native_relationship_context.h"
#include "native_history_context.h"
#include "native_pickup.h"
#include "native_social.h"
#include "native_death.h"
#include "native_hatch.h"
#include "native_replica.h"
#include "native_scene.h"
#include "native_scene_ownership.h"
#include "native_network.h"
#include "native_replica_abi.h"
#include "native_persistence_abi.h"
#include "native_checkpoint.h"
#include "native_quiescence_probe.h"
#include "native_player_transfer_probe.h"
#include "detour_transaction.h"
#include "diagnostics.h"
#include "build_identity.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureAbility.h>
#include <Spore/Simulator/cLocomotiveObject.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cCollectableItems.h>
#include <Spore/Simulator/cNest.h>
#include <Spore/Simulator/SubSystem/GamePersistenceManager.h>
#include <Spore/App/IGameModeManager.h>
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/App/IMessageManager.h>
#include <Spore/App/ICheatManager.h>
#include <Spore/ArgScript/ICommand.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <intrin.h>
#include <set>
#include <vector>
#include <algorithm>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Creature = Simulator::cCreatureBase;
using Combatant = Simulator::cCombatant;
using Noun = Simulator::cGameData;
using Manager = Simulator::cGameNounManager;
using Vec = Math::Vector3;
using ActorAbi = NativeActorAbi<Creature, Animal, Combatant, Vec>;
// The noun ID of a player in the installed executable, independently qualified for M03. The SDK's cPlayer TYPE
// differs from this executable and is deliberately unused.
constexpr uint32_t native_player_noun_id = 0x02c21781;
ActorCommands commands;
std::atomic<bool> enabled{false};
DWORD engine_thread = 0;
HANDLE output = INVALID_HANDLE_VALUE;
LARGE_INTEGER started{}, frequency{};
std::array<char, 32768> buffer{};
size_t used = 0;
uint64_t sequence = 0, bytes_written = 0, native_call = 0, executing = 0;
// Three-process M07 death/reconnect evidence exceeded the earlier 32 MiB
// budget before the bounded 600-second run ended. Keep every existing sample
// and a finite file limit, with the same explicit failure on exhaustion.
constexpr uint64_t trace_limit_bytes = 64ull * 1024ull * 1024ull;
uint64_t target_callbacks = 0, target_sampled_out = 0;
uint64_t worker_ai_entries = 0;
uint64_t ability_scope = 0, strike_scope = 0, animal_damage_scope = 0;
uint64_t pickup_scope = 0;
bool pickup_award_observed = false;

struct PickupGrant {
    uint64_t epoch = 0;
    uintptr_t corpse = 0;
    uint32_t native_id = UINT32_MAX, owner = 0;
};

std::array<PickupGrant, network::max_entities> pickup_grants{};

struct PickupContext {
    Animal* actor = nullptr;
    uint64_t id = 0;
    uint32_t native_id = UINT32_MAX, corpse_native_id = UINT32_MAX;
    uintptr_t corpse = 0;
    const void* state = nullptr;
};

PickupContext pickup_context{};
ULONGLONG next_unbound_target_sample = 0;
std::atomic<uint64_t> foreign{0};
bool failed = false, attached = false, setup_pending = false, opponents_pending = false;
bool setup_foreign_pending = false;
uint64_t foreign_setup_actor = 0, foreign_setup_epoch = 0;
uint64_t npc_source = 0, npc_target = 0;
uint32_t duel_owner = 0;
uint64_t nest_probe_epoch = 0;
int players_pending = 0;
bool rewards_pending = false;

struct FossilApproachProbe {
    uint64_t epoch = 0, actor = 0;
    uint32_t owner = 0, native_id = UINT32_MAX, target = UINT32_MAX;
};

FossilApproachProbe fossil_probe{};
unsigned fossil_probe_count = 0;
ULONGLONG last_sample = 0;
uintptr_t executable_base = 0, executable_end = 0;
App::IMessageManager* messages = nullptr;
App::ICheatManager* cheats = nullptr;

struct Tracked {
    uint64_t id = 0, jump_command = 0, npc_ticks = 0, avatar_ticks = 0;
    uint64_t last_ability_call = 0;
    uint32_t last_ability_caller = 0;
    uint32_t native_id = UINT32_MAX;
    uint32_t decision_samples = 0;
    ActorCommand intention{};
    KeyboardSteeringState steering{};
    ULONGLONG expires = 0, next_step = 0, next_target_sample = 0;
    // Scene in which this actor last began a command, and the unowned-movement stops made in a scene.
    uint64_t commanded_epoch = 0, unowned_epoch = 0;
    uint32_t unowned_stops = 0;
    // C02 plant food: the fruit this creature was last ordered to eat, the last one that then left the world,
    // and how many have.
    uint32_t forage_fruit = 0, fruit_eaten = 0, fruits_eaten = 0;
    float forage_hunger = 0;
    uint64_t forage_place = 0;
};

std::array<Tracked, ActorCommands::capacity> tracked{};

bool on_thread() noexcept {
    if (GetCurrentThreadId() != engine_thread) {
        ++foreign;
        return false;
    }
    return enabled && !failed;
}

void flush() noexcept {
    if (!used || output == INVALID_HANDLE_VALUE || failed) {
        return;
    }
    DWORD written = 0;
    if (!WriteFile(output, buffer.data(), static_cast<DWORD>(used), &written, nullptr) || written != used) {
        failed = true;
    }
    bytes_written += written;
    used = 0;
}

// Constant format strings, bounded buffer/file; raw native addresses are never logged.
void record(const char* event, const char* format = "", ...) noexcept {
    if (output == INVALID_HANDLE_VALUE || GetCurrentThreadId() != engine_thread || failed) {
        return;
    }
    // Reserve one bounded terminal record so diagnostic exhaustion cannot look
    // like an unexplained native-progress regression or a healthy trace close.
    if (bytes_written + used >= trace_limit_bytes - 4096 && strcmp(event, "trace_limit") != 0) {
        record("trace_limit", ",\"limit_bytes\":%llu,\"reason\":\"diagnostic_budget_exhausted\"", trace_limit_bytes);
        flush();
        failed = true;
        return;
    }
    char extra[2048]{};
    va_list args;
    va_start(args, format);
    int count = vsprintf_s(extra, format, args);
    va_end(args);
    if (count < 0) {
        failed = true;
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    char line[3072]{};
    int length = sprintf_s(
        line,
        "{\"schema_version\":1,\"evidence_class\":\"NATIVE_PROBE\",\"harness\":\"M03\",\"event\":\"%s\","
        "\"sequence\":%llu,\"pid\":%lu,\"thread_id\":%lu,\"qpc\":%lld,\"qpc_frequency\":%lld,"
        "\"epoch\":%llu,\"executing_command\":%llu,\"foreign_callbacks\":%llu,"
        "\"ability_scope\":%llu,\"strike_scope\":%llu,\"animal_damage_scope\":%llu,\"pickup_scope\":%llu%s}\n",
        event, ++sequence, GetCurrentProcessId(), engine_thread, now.QuadPart - started.QuadPart, frequency.QuadPart,
        commands.epoch(), executing, foreign.load(), ability_scope, strike_scope, animal_damage_scope, pickup_scope,
        extra);
    if (length < 0) {
        failed = true;
        return;
    }
    if (used + length > buffer.size()) {
        flush();
    }
    if (!failed) {
        memcpy(buffer.data() + used, line, length);
        used += length;
    }
}

void player_event(const char* event, const char* fields) {
    record(event, "%s", fields);
}

uintptr_t key(Noun* noun) {
    return reinterpret_cast<uintptr_t>(noun);
}

ActorBinding identity(Noun* noun) {
    return commands.find_key(key(noun));
}

Tracked* track(uint64_t id) {
    for (auto& state : tracked) {
        if (id && state.id == id) {
            return &state;
        }
    }
    return nullptr;
}

// Resolve through a fresh native noun enumeration. A stored address alone never permits a read/call.
Animal* resolve(uint64_t id) {
    auto binding = commands.find(id);
    if (!binding.live || binding.epoch != commands.epoch() || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    auto manager = Manager::Get();
    if (!manager) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (key(&noun) == binding.local_key) {
            if (noun.mbIsDestroyed || noun.GetNounID() != Animal::NOUN_ID) {
                break;
            }
            auto animal = object_cast<Animal>(&noun);
            auto detail = track(id);
            if (animal && detail && animal->mID == detail->native_id && !animal->mbMarkedForDeletion) {
                return animal;
            }
            break;
        }
    }
    record("binding_missing", ",\"actor\":%llu", id);
    commands.invalidate(binding.local_key);
    return nullptr;
}

// The number of every noun, into `ids`. Stops and fails at the first number that two nouns share.
bool unique_noun_ids(Manager* manager, std::set<uint32_t>& ids) {
    for (auto& noun : manager->mNouns) {
        if (!ids.insert(noun.mID).second) {
            return false;
        }
    }
    return true;
}

uint64_t combat_id(Combatant* combatant) {
    return combatant ? identity(combatant->ToGameData()).id : 0;
}

uint32_t caller_rva(void* address) {
    auto value = reinterpret_cast<uintptr_t>(address);
    return value >= executable_base && value < executable_end ? static_cast<uint32_t>(value - executable_base) : 0;
}

// Read-only, engine-thread snapshots of the pinned tree layout. Failed reads
// remain absent evidence, never permission to call or mutate native objects.
template <class T>
bool native_read(uintptr_t address, size_t offset, T& value) {
    if (!address || offset > UINTPTR_MAX - address || sizeof(T) > UINTPTR_MAX - address - offset) {
        return false;
    }
    SIZE_T copied = 0;
    const DWORD saved = GetLastError();
    const bool result = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address + offset), &value,
                                          sizeof(T), &copied) &&
                        copied == sizeof(T);
    SetLastError(saved);
    return result;
}

struct BehaviorSnapshot {
    uint32_t decider_id = 0, behavior_id = 0, depth = 0, root_id = 0;
    uint32_t decider_rva = 0, root_rva = 0, decision_rva = 0, tick_rva = 0;
    bool valid = false;
};

BehaviorSnapshot behavior_snapshot(Creature* animal) {
    BehaviorSnapshot result{};
    if (!animal) {
        return result;
    }
    const auto tree = static_cast<uintptr_t>(animal->field_B4C);
    // C09B06 supplies tree+1D0 to the native executor. Header and decider
    // fields agree with pinned SDK AI/cBehaviorTreeData.h, not inferred types.
    uint32_t header[4]{}, root = 0, decision = 0, behavior = 0, tick = 0;
    if (!native_read(tree, 0x1d0, header) || !native_read(tree, 0, root)) {
        return result;
    }
    result.decider_id = header[0];
    result.behavior_id = header[2];
    result.depth = header[3];
    result.decider_rva = caller_rva(reinterpret_cast<void*>(header[1]));
    result.root_rva = caller_rva(reinterpret_cast<void*>(root));
    native_read(root, 0, result.root_id);
    native_read(header[1], 8, decision);
    if (native_read(header[1], 0x70, behavior)) {
        native_read(behavior, 8, tick);
    }
    result.decision_rva = caller_rva(reinterpret_cast<void*>(decision));
    result.tick_rva = caller_rva(reinterpret_cast<void*>(tick));
    result.valid = true;
    return result;
}

void record_behavior(uint64_t actor, const char* event, const BehaviorSnapshot& value) {
    record(event,
           ",\"actor\":%llu,\"valid\":%s,\"decider_id\":%u,\"behavior_id\":%u,\"depth\":%u,"
           "\"root_id\":%u,\"decider_rva\":%u,\"root_rva\":%u,\"decision_rva\":%u,\"tick_rva\":%u",
           actor, value.valid ? "true" : "false", value.decider_id, value.behavior_id, value.depth, value.root_id,
           value.decider_rva, value.root_rva, value.decision_rva, value.tick_rva);
}

// Engine-thread-only nesting. An asynchronous strike gets its own call identity;
// it is never attributed to whichever console command happened to run last.
struct CallScope {
    uint64_t& slot;
    uint64_t previous;

    CallScope(uint64_t& value, uint64_t current) : slot(value), previous(value) {
        slot = current;
    }

    ~CallScope() {
        slot = previous;
    }
};

void combat_context(Animal* animal, const char* event) {
    if (!animal) {
        return;
    }
    const auto binding = identity(animal);
    if (!binding.live) {
        return;
    }
    const auto spatial = static_cast<Simulator::cSpatialObject*>(animal);
    const auto vtable = *reinterpret_cast<uintptr_t**>(spatial);
    // Pinned Animal spatial vtable +0x58 is the pure IsPlayerOwned predicate.
    // Installed implementation reads general flags & 0x300, not the owner table.
    const bool predicate_valid = vtable[0x58 / 4] - executable_base == 0x802ff0;
    const auto herd = animal->mHerd.get();
    const auto index = animal->mCurrentAttackIdx;
    const auto marker = animal->mpAnimatedCreature ? animal->mpAnimatedCreature->field_170[0] : 0;
    const int effect_gate = index < animal->mAbilityStates.size() ? int(animal->mAbilityStates[index].field_01) : -1;
    record(event,
           ",\"actor\":%llu,\"owner\":%u,\"flags\":%d,\"player_owned_binding_valid\":%s,"
           "\"native_player_owned\":%s,\"current_attack_index\":%u,\"current_attack_animation\":%u,"
           "\"herd_native_id\":%u,\"herd_owned_by_avatar\":%s,\"herd_enabled\":%s,"
           "\"herd_damage_multiplier\":%.9g,\"herd_personality\":%d,\"animation_marker\":%d,\"effect_gate\":%d,"
           "\"herd_members\":%u,\"herd_nest_present\":%s,\"herd_nest_native_id\":%u",
           binding.id, binding.owner, animal->mGeneralFlags, predicate_valid ? "true" : "false",
           predicate_valid ? (spatial->IsPlayerOwned() ? "true" : "false") : "null", animal->mCurrentAttackIdx,
           animal->mCurrentAttackAnimId, herd ? herd->mID : UINT32_MAX, herd && herd->mOwnedByAvatar ? "true" : "false",
           herd && herd->mbEnabled ? "true" : "false", herd ? double(herd->mDamageMultiplier) : 0.0,
           herd ? static_cast<int>(herd->mCreaturePersonality) : -1, marker, effect_gate,
           herd ? static_cast<unsigned>(herd->mHerd.size()) : 0, herd && herd->mpNest ? "true" : "false",
           herd && herd->mpNest ? herd->mpNest->mID : UINT32_MAX);
}

void state(Animal* animal, const char* event) {
    if (!animal) {
        return;
    }
    auto binding = identity(animal);
    if (!binding.live) {
        return;
    }
    auto position = animal->GetPosition();
    uint32_t active[3]{}, recharge[3]{};
    for (size_t i = 0; i < 88; ++i) {
        if (animal->mInUseAbilityBits.test(i)) {
            active[i / 32] |= 1u << (i % 32);
        }
        if (animal->mRechargingAbilityBits.test(i)) {
            recharge[i / 32] |= 1u << (i % 32);
        }
    }
    const auto detail = track(binding.id);
    // The species' mouths, as the pinned SDK's cSpeciesProfile names them (+608h, +60Ch): what it can eat (diagnostic).
    const auto profile = animal->mpSpeciesProfile;
    const bool mouths =
        profile &&
        reinterpret_cast<uintptr_t>(&profile->mMouthHerbivorous) - reinterpret_cast<uintptr_t>(profile) == 0x60c;
    // Sampled native values. Campaign mEnergy semantics are not assumed.
    record(event,
           ",\"actor\":%llu,\"owner\":%u,\"native_id\":%u,\"political_id\":%u,"
           "\"avatar\":%s,\"health\":%.9g,\"raw_max_health_field\":%.9g,\"hunger\":%.9g,\"energy\":%.9g,"
           "\"dead\":%s,\"has_been_eaten\":%s,\"food_value\":%.9g,\"position\":[%.9g,%.9g,%.9g],\"target\":%llu,"
           "\"ability_target\":%llu,"
           "\"last_attacker\":%llu,\"last_attacker_political\":%u,\"default_attack\":%d,"
           "\"active_bits\":[%u,%u,%u],\"recharge_bits\":[%u,%u,%u],\"inventory_count\":%u,"
           "\"species\":[%u,%u,%u],\"flags\":%d,\"intention\":%d,\"npc_ticks\":%llu,\"avatar_ticks\":%llu,\"mouths\":[%"
           "d,%d]",
           binding.id, binding.owner, animal->mID, animal->mPoliticalID,
           Manager::Get()->GetAvatar() == animal ? "true" : "false", double(animal->mHealthPoints),
           double(animal->mMaxHealthPoints), double(animal->mHunger), double(animal->mEnergy),
           animal->mbDead ? "true" : "false", animal->mbHasBeenEaten ? "true" : "false", double(animal->mFoodValue),
           double(position.x), double(position.y), double(position.z), combat_id(animal->GetTarget()),
           combat_id(animal->mpCombatantTarget), combat_id(animal->mpLastAttacker.get()), animal->mLastAttacker,
           animal->mDefaultAttackAbilityIndex, active[0], active[1], active[2], recharge[0], recharge[1], recharge[2],
           static_cast<unsigned>(animal->mItemInventory.size()), animal->mSpeciesKey.instanceID,
           animal->mSpeciesKey.typeID, animal->mSpeciesKey.groupID, animal->mGeneralFlags,
           animal->mIntentionTowardsTarget, detail ? detail->npc_ticks : 0, detail ? detail->avatar_ticks : 0,
           mouths ? profile->mMouthCarnivorous : -1, mouths ? profile->mMouthHerbivorous : -1);
}

// Virtual dispatch slots are recorded as executable-relative RVAs, never pointers.
void record_binding_abi(const ActorBinding& binding, Animal* animal) {
    auto combat = static_cast<Combatant*>(animal);
    auto vtable = *reinterpret_cast<uintptr_t**>(combat);
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto creature_vtable = *reinterpret_cast<uintptr_t**>(animal);
    record("creature_target_abi", ",\"actor\":%llu,\"set_creature_target_rva\":%u", binding.id,
           static_cast<unsigned>(creature_vtable[0x84 / 4] - base));
    record("binding_abi",
           ",\"actor\":%llu,\"combatant_offset\":%u,\"damage_rva\":%u,\"set_target_rva\":%u,\"get_target_rva\":%u",
           binding.id, static_cast<unsigned>(reinterpret_cast<uintptr_t>(combat) - key(animal)),
           static_cast<unsigned>(vtable[0x18 / 4] - base), static_cast<unsigned>(vtable[0x50 / 4] - base),
           static_cast<unsigned>(vtable[0x54 / 4] - base));
}

void record_abilities(const ActorBinding& binding, Animal* animal) {
    int ability_count = animal->GetAbilitiesCount();
    if (ability_count < 0 || ability_count > 88) {
        return;
    }
    for (int i = 0; i < ability_count; ++i) {
        auto ability = animal->GetAbility(i);
        if (!ability) {
            continue;
        }
        record("ability",
               ",\"actor\":%llu,\"index\":%d,\"type\":%d,\"category\":%d,\"damage\":%.9g,\"range\":%.9g,\"energy_"
               "cost\":%.9g,\"adventure_energy_cost\":%.9g,\"recharge\":%.9g",
               binding.id, i, int(ability->mType), ability->mCategory, double(ability->mDamage),
               double(ability->mRange), double(ability->mEnergyCost), double(ability->mAdventurerEnergyCost),
               double(ability->mRecharge));
    }
}

ActorBinding bind(Animal* animal, uint32_t owner, bool corpse_target = false) {
    // A freshly selected corpse may never have been a bridge combat target.
    // Only a validated ownerless pickup target can acquire a dead binding;
    // controlled actors still require the normal living admission.
    if (!animal || (animal->mbDead && (!corpse_target || owner)) || animal->mbIsDestroyed) {
        return {};
    }
    auto binding = commands.bind(key(animal), owner);
    if (!binding.live) {
        return {};
    }
    if (!track(binding.id)) {
        for (auto& slot : tracked) {
            if (!commands.find(slot.id).live) {
                slot = {binding.id};
                break;
            }
        }
    }
    if (auto detail = track(binding.id)) {
        detail->native_id = animal->mID;
    }
    record("bound", ",\"actor\":%llu,\"owner\":%u,\"native_id\":%u,\"political_id\":%u", binding.id, owner, animal->mID,
           animal->mPoliticalID);
    record_binding_abi(binding, animal);
    state(animal, "bound_state");
    combat_context(animal, "bound_combat_context");
    record("campaign_context", ",\"actor\":%llu,\"archetype\":%d,\"native_archetype_data\":%s,\"herd_native_id\":%u",
           binding.id, animal->mArchetype, animal->field_E84 ? "true" : "false",
           animal->mHerd ? animal->mHerd->mID : UINT32_MAX);
    record_abilities(binding, animal);
    App::ConsolePrintF("M03 actor %llu, owner %u, native %u, health %.1f", binding.id, owner, animal->mID,
                       animal->mHealthPoints);
    flush();
    return binding;
}

void sample() {
    if (Simulator::GetGameModeID() != kGameCreature || !Manager::Get()) {
        return;
    }
    for (const auto& slot : tracked) {
        if (auto animal = resolve(slot.id)) {
            state(animal, "actor_state");
            sample_native_social_actor(animal, slot.id);
            if (animal->mCurrentAttackIdx != UINT32_MAX) {
                combat_context(animal, "sampled_attack_context");
            }
        }
    }
    auto avatar = Manager::Get()->GetAvatar();
    record("player_context", ",\"avatar\":%llu,\"avatar_native_id\":%u,\"global_dna\":%.9g",
           avatar ? identity(avatar).id : 0, avatar ? avatar->mID : UINT32_MAX,
           double(Simulator::cCreatureGameData::GetEvolutionPoints()));
    sample_native_player_context(commands.epoch());
    sample_native_awards();
    sample_native_ornaments();
    sample_native_social_ui(commands.epoch());
    sample_native_replica();
}

Vec offset(Animal* animal, int direction, float distance) {
    auto position = animal->GetPosition();
    auto forward = animal->GetDirection().Normalized();
    auto right = forward.Cross(position.Normalized()).Normalized();
    auto delta = direction == 0 ? forward : direction == 1 ? right : direction == 2 ? forward * -1.0f : right * -1.0f;
    return position + delta * distance;
}

uint64_t owned(uint32_t owner) {
    for (const auto& slot : tracked) {
        auto binding = commands.find(slot.id);
        if (binding.live && binding.owner == owner) {
            return binding.id;
        }
    }
    return 0;
}

Animal* create_in_herd(const Vec& position, Animal* source) {
    if (!source || !source->mpSpeciesProfile || !source->mHerd || !source->field_E84) {
        return nullptr;
    }
    auto species = source->mpSpeciesProfile;
    auto herd = Manager::Get()->CreateHerd(position, species, 1, false,
                                           static_cast<int>(Simulator::CreaturePersonality::Guard), false);
    if (!herd) {
        return nullptr;
    }
    // Creature factory resolves native campaign archetype data using the herd's
    // archetype and generation (0xC09C26..0xC09C43). Preserve a live template's
    // identifiers so target UI and native reward rules receive valid data.
    herd->mArchetype = source->mHerd->mArchetype;
    herd->mArchetypeGroup = source->mHerd->mArchetypeGroup;
    herd->mGeneration = source->mHerd->mGeneration;
    herd->mOwnerSpeciesKey = source->mHerd->mOwnerSpeciesKey;
    record("native_herd_created", ",\"native_id\":%u,\"members\":%u,\"enabled\":%s", herd->mID,
           static_cast<unsigned>(herd->mHerd.size()), herd->mbEnabled ? "true" : "false");
    // CreateHerd initializes the herd; it does not populate it synchronously in
    // this campaign. Animal::Create retains the herd and inserts the member
    // through its native factory (installed 0xC09BD4..0xC09D76).
    auto animal =
        herd->mHerd.empty() ? Animal::Create(position, species, 1, herd, false, false) : herd->mHerd.front().get();
    if (animal && !animal->field_E84) {
        record("archetype_initialization_failed",
               ",\"native_id\":%u,\"template\":%u,\"herd_archetype\":%u,\"generation\":%d", animal->mID, source->mID,
               herd->mArchetype, herd->mGeneration);
        Manager::Get()->DestroyInstance(animal);
        return nullptr;
    }
    return animal;
}

bool set_creature_target(Animal* animal, Combatant* target) {
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto vtable = *reinterpret_cast<uintptr_t**>(animal);
    // Pinned SDK virtual slot, independently checked against the installed
    // animal vtable. Native implementation retains/releases mpCombatantTarget.
    // Native campaign callers use (target,false,0), e.g. 0xD3599B..0xD359B0.
    if (vtable[0x84 / 4] - base != 0x803DF0) {
        record("creature_target_binding_rejected");
        return false;
    }
    animal->SetCreatureTarget(target, false, 0);
    return true;
}

bool stop_native_movement(Animal* animal) {
    // Pinned SDK cLocomotiveObject virtual ECh, independently inspected in
    // GOG 3.1.0.29. ECX is the +C0 locomotive subobject, not the animal base.
    auto locomotive = static_cast<Simulator::cLocomotiveObject*>(animal);
    const auto vtable = *reinterpret_cast<const uintptr_t* const*>(locomotive);
    constexpr uintptr_t function_rva = 0x843800, vtable_rva = 0x1069ec0;
    constexpr unsigned char prefix[] = {0x83, 0xec, 0x74, 0xf3, 0x0f, 0x10, 0x05};
    if (reinterpret_cast<uintptr_t>(locomotive) - key(animal) != 0xc0 ||
        reinterpret_cast<uintptr_t>(vtable) != executable_base + vtable_rva ||
        function_rva + 11 > executable_end - executable_base || vtable[0xec / 4] != executable_base + function_rva ||
        memcmp(reinterpret_cast<const void*>(executable_base + function_rva), prefix, sizeof(prefix))) {
        record("native_movement_stop_binding_rejected");
        return false;
    }
    uintptr_t operand = 0;
    memcpy(&operand, reinterpret_cast<const void*>(executable_base + function_rva + 7), sizeof(operand));
    if (operand != executable_base + 0x128e7e4) {
        record("native_movement_stop_binding_rejected");
        return false;
    }
    const auto id = identity(animal).id;
    record("native_movement_stop_enter",
           ",\"actor\":%llu,\"request_words\":%zu,\"desired_speed\":%.9g,\"in_transit\":%.9g,\"velocity\":[%.9g,%.9g,%."
           "9g]",
           id, static_cast<size_t>(locomotive->field_1F4.size()), double(locomotive->mDesiredSpeed),
           double(locomotive->mInTransitTime), double(locomotive->mVelocity.x), double(locomotive->mVelocity.y),
           double(locomotive->mVelocity.z));
    locomotive->StopMovement();
    animal = resolve(id);
    if (!animal) {
        return false;
    }
    locomotive = static_cast<Simulator::cLocomotiveObject*>(animal);
    record("native_movement_stop_return",
           ",\"actor\":%llu,\"request_words\":%zu,\"desired_speed\":%.9g,\"in_transit\":%.9g,\"velocity\":[%.9g,%.9g,%."
           "9g],\"native_world_idle_qualified\":false",
           id, static_cast<size_t>(locomotive->field_1F4.size()), double(locomotive->mDesiredSpeed),
           double(locomotive->mInTransitTime), double(locomotive->mVelocity.x), double(locomotive->mVelocity.y),
           double(locomotive->mVelocity.z));
    return true;
}

Animal* foreign_template(Animal* actor, bool initial_brain_only = false);

bool foreign_setup_fresh(Animal* avatar) {
    auto manager = Manager::Get();
    auto player = manager ? manager->GetPlayer() : nullptr;
    if (!manager || !player || !avatar || native_replica_is_client() || Simulator::IsLoadingGameMode() ||
        native_second_player(commands.epoch()) || players_pending || rewards_pending || commands.pending_count() ||
        foreign_setup_epoch == commands.epoch() || owned(2) || (owned(1) && resolve(owned(1)) != avatar) ||
        commands.live_count() != (owned(1) ? 1u : 0u)) {
        return false;
    }
    unsigned players = 0, avatars = 0;
    bool primary_present = false, avatar_present = false;
    // Same pinned noun/flag census as the one-primary binding probe. A loaded
    // two-player checkpoint is rejected before any factory or binding change.
    for (auto& noun : manager->mNouns) {
        if (noun.GetNounID() == native_player_noun_id) {
            ++players;
            if (&noun == static_cast<Noun*>(player) && !noun.mbIsDestroyed && !noun.field_20) {
                primary_present = true;
            }
        }
        if (noun.GetNounID() == Animal::NOUN_ID) {
            auto animal = object_cast<Animal>(&noun);
            if (animal && (animal->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar)) {
                ++avatars;
            }
            if (animal == avatar && !noun.mbIsDestroyed && !noun.field_20 && !animal->mbMarkedForDeletion) {
                avatar_present = true;
            }
        }
    }
    record("foreign_setup_census",
           ",\"native_players\":%u,\"avatar_flags\":%u,\"primary_present\":%s,\"avatar_present\":%s", players, avatars,
           primary_present ? "true" : "false", avatar_present ? "true" : "false");
    return players == 1 && avatars == 1 && primary_present && avatar_present;
}

void setup(bool experimental_foreign = false) {
    auto manager = Manager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    if (!avatar || avatar->mbDead || !avatar->mpSpeciesProfile || owned(2) || foreign_setup_epoch == commands.epoch()) {
        record("setup_rejected");
        App::ConsolePrintF("M03 setup requires a live Creature avatar and no existing actor pair.");
        return;
    }
    auto source = avatar;
    if (experimental_foreign) {
        if (!foreign_setup_fresh(avatar) || !(source = foreign_template(avatar, true))) {
            record("foreign_setup_rejected");
            return;
        }
        // The original factory can create a herd even if animal creation
        // later fails. This entire experimental scene cannot be checkpointed.
        foreign_setup_epoch = commands.epoch();
        foreign_setup_actor = 0;
    }
    if (!owned(1)) {
        bind(avatar, 1);
    }
    if (experimental_foreign && resolve(owned(1)) != avatar) {
        return;
    }
    // Native factories retain the live template profile and initialize the
    // model, abilities and campaign archetype. No existing B is replaced.
    auto second = create_in_herd(offset(avatar, 1, 5.0f), source);
    if (experimental_foreign && (!second || second == source || second == avatar || second->mbDead ||
                                 second->mpSpeciesProfile != source->mpSpeciesProfile ||
                                 second->mSpeciesKey != source->mSpeciesKey || second->GetCurrentBrainLevel() != 0 ||
                                 !second->mHerd || second->mHerd == source->mHerd || second->mHerd == avatar->mHerd ||
                                 second->mHerd->mHerd.size() != 1 || second->mHerd->mHerd.front().get() != second)) {
        record("foreign_setup_factory_rejected");
        return;
    }
    const auto binding = bind(second, 2);
    if (!binding.live) {
        record("create_failed");
    } else if (experimental_foreign) {
        foreign_setup_actor = binding.id;
        record("foreign_setup_created",
               ",\"actor\":%llu,\"native_id\":%u,\"template_native_id\":%u,\"species\":[%u,%u,%u],\"checkpoint_"
               "schema\":4,\"checkpoint_qualified\":false,\"experimental\":true",
               binding.id, second->mID, source->mID, second->mSpeciesKey.instanceID, second->mSpeciesKey.typeID,
               second->mSpeciesKey.groupID);
    }
    sample();
    flush();
}

bool experimental_foreign_actor(uint64_t id) {
    return id && id == foreign_setup_actor && foreign_setup_epoch == commands.epoch();
}

Animal* foreign_template(Animal* actor, bool initial_brain_only) {
    Animal* source = nullptr;
    float best = 1e20f;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.GetNounID() == Animal::NOUN_ID && !noun.mbIsDestroyed) {
            auto candidate = object_cast<Animal>(&noun);
            if (candidate && !candidate->mbDead && candidate->field_E84 && candidate->mHerd &&
                candidate->mpSpeciesProfile && candidate->mpSpeciesProfile != actor->mpSpeciesProfile &&
                !identity(candidate).live) {
                if (initial_brain_only &&
                    (candidate->mbMarkedForDeletion || candidate->field_20 ||
                     (candidate->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar) ||
                     candidate->GetCurrentBrainLevel() != 0 || candidate->mHerd->mbIsDestroyed ||
                     candidate->mHerd->field_20 || candidate->mHerd->mpSpeciesProfile != candidate->mpSpeciesProfile ||
                     candidate->mSpeciesKey == actor->mSpeciesKey ||
                     candidate->mpSpeciesProfile->mCreatureKey != candidate->mSpeciesKey)) {
                    continue;
                }
                float squared_distance = (candidate->GetPosition() - actor->GetPosition()).SquaredLength();
                if (squared_distance < best) {
                    source = candidate;
                    best = squared_distance;
                }
            }
        }
    }
    if (!source) {
        record("no_foreign_species");
        App::ConsolePrintF("M03: no live foreign species template in the scene.");
        return nullptr;
    }
    // Both template and native factory arguments are live in this single engine callback.
    record("opponent_template", ",\"native_id\":%u,\"political_id\":%u,\"distance\":%.9g", source->mID,
           source->mPoliticalID, double(std::sqrt(best)));
    return source;
}

void duel(uint32_t owner) {
    auto actor = resolve(owned(owner));
    if (!actor || actor->mbDead) {
        record("duel_rejected");
        return;
    }
    auto source = foreign_template(actor);
    if (!source) {
        return;
    }
    auto opponent = bind(create_in_herd(offset(actor, 0, 1.5f), source), 0);
    if (!opponent.live) {
        return;
    }
    ActorCommand command{};
    command.actor = identity(actor).id;
    command.owner = owner;
    command.verb = ActorVerb::attack;
    command.target = opponent.id;
    auto decision = commands.enqueue(command);
    record("command_queued",
           ",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"attack\",\"target\":%llu,\"decision\":\"%s\","
           "\"fixture\":\"duel\"",
           command.sequence, command.actor, owner, command.target, actor_decision(decision));
    App::ConsolePrintF("M03 duel: actor %llu, native NPC %llu, %s", command.actor, command.target,
                       actor_decision(decision));
}

void opponents() {
    auto first = resolve(owned(1));
    auto second = resolve(owned(2));
    if (!first || !second) {
        record("opponents_rejected");
        return;
    }
    for (const auto& slot : tracked) {
        auto binding = commands.find(slot.id);
        if (binding.live && !binding.owner) {
            record("opponents_already_present");
            return;
        }
    }
    auto source = foreign_template(first);
    if (!source) {
        return;
    }
    bind(create_in_herd(offset(first, 0, 1.5f), source), 0);
    second = resolve(owned(2));
    if (second) {
        bind(create_in_herd(offset(second, 0, 1.5f), source), 0);
    }
}

using DestroyFn = void(__thiscall*)(Manager*, Noun*);
using PoolReturnFn = NativePoolAbi<void, Noun>::Return;
using LandFn = void(__thiscall*)(Creature*);
using AiFn = void(__thiscall*)(Animal*, float);
using DamageFn = ActorAbi::Damage;
using AbilityFn = void(__thiscall*)(Creature*, int, Anim::AnimIndex*);
using EnergyFn = void(__thiscall*)(Creature*, float);
using DnaFn = void(__cdecl*)(float);
using TargetFn = void(__thiscall*)(Animal*, Combatant*, bool, int);
// Signature of the internal selector is established by PlayAbility's three
// pushes at C1E5CE..C1E5D5, its stack reads and ret 0xC at C19800..C198F9.
// Observer only: no standalone call is made to this internal function.
using SelectAbilityFn = ActorAbi::SelectAbility;
// SDK cCreatureBase.h virtual slot B8; installed Animal table 146A080+B8.
// This function APPLIES native effects. It is not a read-only eligibility query.
using StrikeFn = ActorAbi::Strike;
DestroyFn destroy_original = nullptr;
PoolReturnFn pool_return_original = nullptr;
LandFn land_original = nullptr;
AiFn npc_original = nullptr, avatar_original = nullptr;
DamageFn damage_original = nullptr;
AbilityFn ability_original = nullptr;
EnergyFn energy_original = nullptr;
DnaFn dna_original = nullptr;
TargetFn target_original = nullptr;
SelectAbilityFn select_ability_original = nullptr;
StrikeFn strike_original = nullptr;
DamageFn animal_damage_original = nullptr;
ActorAbi::SetAttack set_attack_original = nullptr;
ActorAbi::AnimationDone animation_done_original = nullptr;
ActorAbi::AttackStop attack_stop_original = nullptr;
ActorAbi::Decide nest_decide_original = nullptr;
ActorAbi::MemoryCreature memory_creature_original = nullptr;

struct NestDecisionScope {
    NestDecisionScope* previous;
    uint64_t target = 0;
    uint32_t owner = 0;
    bool native_avatar = false;
    static NestDecisionScope* current;

    NestDecisionScope() : previous(current) {
        current = this;
    }

    ~NestDecisionScope() {
        current = previous;
    }
};

NestDecisionScope* NestDecisionScope::current = nullptr;

Creature* __cdecl memory_creature_hook(void* memory) {
    const auto caller = caller_rva(_ReturnAddress());
    auto result = memory_creature_original(memory);
    // Capture the object from the ORIGINAL lookup, without another lookup or
    // pointer substitution. Only D7D900's exact player-exemption branch qualifies.
    if (on_thread() && caller == 0x97d9aa && NestDecisionScope::current && result) {
        auto binding = identity(result);
        if (binding.live && binding.owner && !result->mbDead) {
            auto& scope = *NestDecisionScope::current;
            scope.target = binding.id;
            scope.owner = binding.owner;
            scope.native_avatar = (result->mGeneralFlags & 0x200) != 0;
        }
    }
    return result;
}

float __cdecl nest_decide_hook(Creature* actor, double time, uint32_t flags, void* scratch, bool* dirty, void* decider,
                               void* context) {
    if (!on_thread() || Simulator::GetGameModeID() != kGameCreature) {
        return nest_decide_original(actor, time, flags, scratch, dirty, decider, context);
    }
    NestDecisionScope scope;
    const float native_score = nest_decide_original(actor, time, flags, scratch, dirty, decider, context);
    // The native home-herd rule exempts a remembered target with avatar flag 200h.
    // Extend only that classification to a live, explicitly owned M03 actor.
    // Native decide/lookup still execute once; scratch ownership and all other
    // callbacks stay native. No avatar, flags, target, damage or reward is changed.
    if (native_score > 0.0f && scope.owner && !scope.native_avatar) {
        record("native_nest_player_exemption",
               ",\"actor\":%llu,\"target\":%llu,\"target_owner\":%u,"
               "\"native_score\":%.9g,\"adapted_score\":0,\"decision_flags\":%u",
               identity(actor).id, scope.target, scope.owner, double(native_score), flags);
        return 0.0f;
    }
    return native_score;
}

bool __cdecl attack_stop_hook(void* context, Creature* self, uint32_t flags, Combatant* target, Animal* target_animal) {
    const auto binding = on_thread() ? identity(self) : ActorBinding{};
    auto detail = binding.live ? track(binding.id) : nullptr;
    if (!detail || detail->decision_samples >= 90) {
        return attack_stop_original(context, self, flags, target, target_animal);
    }
    ++detail->decision_samples;
    const auto call = ++native_call;
    const auto target_id = combat_id(target);
    const auto spatial = target ? target->ToSpatialObject() : nullptr;
    const auto target_data = target ? target->ToGameData() : nullptr;
    uint32_t context_kind = 0, animation = 0, other_animation = 0, tree_context = 0, order_kind = 0;
    native_read(reinterpret_cast<uintptr_t>(context), 0x18, context_kind);
    native_read(reinterpret_cast<uintptr_t>(context), 0x30, animation);
    native_read(reinterpret_cast<uintptr_t>(context), 0x34, other_animation);
    if (native_read(static_cast<uintptr_t>(self->field_B4C), 0x600, tree_context)) {
        native_read(tree_context, 8, order_kind);
    }
    record("native_attack_stop_enter",
           ",\"call\":%llu,\"actor\":%llu,\"target\":%llu,\"caller_rva\":%u,"
           "\"decision_flags\":%u,\"actor_flags\":%d,\"actor_political\":%u,\"target_political\":%u,"
           "\"target_spatial_enabled\":%s,\"target_destroyed\":%s,\"target_combat_state\":%d,\"target_stealthed\":%s,"
           "\"context_kind\":%u,\"context_animation\":%u,\"context_other_animation\":%u,\"order_kind\":%u",
           call, binding.id, target_id, caller_rva(_ReturnAddress()), flags, self->mGeneralFlags, self->mPoliticalID,
           target_data ? target_data->mPoliticalID : UINT32_MAX,
           spatial ? (spatial->mbEnabled ? "true" : "false") : "null",
           target_data ? (target_data->mbIsDestroyed ? "true" : "false") : "null", target ? target->field_34 : -1,
           target_animal ? (target_animal->mbStealthed ? "true" : "false") : "null", context_kind, animation,
           other_animation, order_kind);
    const bool result = attack_stop_original(context, self, flags, target, target_animal);
    record("native_attack_stop_return", ",\"call\":%llu,\"actor\":%llu,\"target\":%llu,\"stop\":%s", call, binding.id,
           target_id, result ? "true" : "false");
    return result;
}

void __fastcall set_attack_hook(Creature* self, void*, int index) {
    const auto binding = on_thread() ? identity(self) : ActorBinding{};
    const auto caller = caller_rva(_ReturnAddress());
    if (binding.live && (self->mCurrentAttackIdx != UINT32_MAX || index != -1)) {
        record("native_attack_index_set",
               ",\"actor\":%llu,\"before_index\":%u,\"requested_index\":%d,\"caller_rva\":%u", binding.id,
               self->mCurrentAttackIdx, index, caller);
    }
    set_attack_original(self, index);
}

bool __fastcall animation_done_hook(Creature* self, void*, uint32_t animation) {
    const auto binding = on_thread() ? identity(self) : ActorBinding{};
    const bool relevant =
        binding.live && self->mCurrentAttackIdx != UINT32_MAX && animation == self->mCurrentAttackAnimId;
    const auto caller = caller_rva(_ReturnAddress());
    const auto index = relevant ? self->mCurrentAttackIdx : UINT32_MAX;
    const bool result = animation_done_original(self, animation);
    if (relevant && result) {
        record("native_attack_animation_done",
               ",\"actor\":%llu,\"index\":%u,\"animation\":%u,\"caller_rva\":%u,\"result\":true", binding.id, index,
               animation, caller);
        if (auto animal = resolve(binding.id)) {
            combat_context(animal, "animation_done_context");
        }
    }
    return result;
}

int __fastcall select_ability_hook(Creature* self, void*, int index, bool recharge, bool extra) {
    // Native PlayAbility handles the original selector's -1 result itself,
    // preserving its output/return convention and native no-action path.
    if (!native_replica_allows(replica::Mutation::ability)) {
        return -1;
    }
    if (!on_thread()) {
        return select_ability_original(self, index, recharge, extra);
    }
    const auto binding = identity(self);
    if (!binding.live) {
        return select_ability_original(self, index, recharge, extra);
    }
    const auto call = ++native_call;
    record("native_ability_select_enter",
           ",\"call\":%llu,\"actor\":%llu,\"owner\":%u,\"index\":%d,"
           "\"recharge_flag\":%s,\"extra_flag\":%s,\"current_attack_index\":%u",
           call, binding.id, binding.owner, index, recharge ? "true" : "false", extra ? "true" : "false",
           self->mCurrentAttackIdx);
    const int result = select_ability_original(self, index, recharge, extra);
    record("native_ability_select_return", ",\"call\":%llu,\"actor\":%llu,\"result_index\":%d", call, binding.id,
           result);
    return result;
}

bool __fastcall strike_hook(Animal* self, void*, Combatant* target, int index, Vec* position) {
    if (!native_replica_allows(replica::Mutation::damage)) {
        return false;
    }
    if (!on_thread()) {
        return strike_original(self, target, index, position);
    }
    const auto actor = identity(self);
    const auto target_id = combat_id(target);
    if (!actor.live && !target_id) {
        return strike_original(self, target, index, position);
    }
    const auto call = ++native_call;
    CallScope scope(strike_scope, call);
    record("native_strike_enter",
           ",\"call\":%llu,\"actor\":%llu,\"owner\":%u,\"target\":%llu,"
           "\"index\":%d,\"position_supplied\":%s,\"caller_rva\":%u",
           call, actor.id, actor.owner, target_id, index, position ? "true" : "false", caller_rva(_ReturnAddress()));
    if (auto animal = resolve(actor.id)) {
        combat_context(animal, "strike_actor_context");
    }
    if (auto animal = resolve(target_id)) {
        combat_context(animal, "strike_target_context");
    }
    const bool result = strike_original(self, target, index, position);
    // Raw native result; a true/false result alone is not hit acceptance.
    record("native_strike_return", ",\"call\":%llu,\"actor\":%llu,\"target\":%llu,\"result\":%s", call, actor.id,
           target_id, result ? "true" : "false");
    return result;
}

int __fastcall animal_damage_hook(Combatant* self, void*, float amount, uint32_t political, int type,
                                  const Vec& direction, Combatant* attacker) {
    if (!native_replica_allows(replica::Mutation::damage)) {
        return 0;
    }
    if (!on_thread()) {
        return animal_damage_original(self, amount, political, type, direction, attacker);
    }
    // This extra layer records actor-to-actor combat. The existing base hook
    // retains environmental/starvation evidence without tripling every tick.
    const auto receiver = identity(self->ToGameData());
    const auto source = attacker ? identity(attacker->ToGameData()) : ActorBinding{};
    const bool observe = attacker && (receiver.live || source.live);
    const auto call = observe ? ++native_call : 0;
    int result = 0;
    {
        // Every engine-thread invocation masks an outer owner, including
        // environmental and untracked nested damage. Original execution is
        // never skipped because the bounded foreign predicates changed.
        CallScope scope(animal_damage_scope, call);
        auto actor = source.live ? resolve(source.id) : nullptr;
        auto target = receiver.live ? resolve(receiver.id) : nullptr;
        NativeDamageOwnerScope award_scope(actor, source.owner, source.id);
        NativeRelationshipDamageScope relationship_scope(actor, source.owner, source.id, target, receiver.id,
                                                         commands.epoch());
        if (observe) {
            record("native_animal_damage_enter",
                   ",\"call\":%llu,\"receiver\":%llu,\"receiver_owner\":%u,"
                   "\"attacker\":%llu,\"attacker_owner\":%u,\"damage\":%.9g,\"type\":%d,\"caller_rva\":%u",
                   call, receiver.id, receiver.owner, source.id, source.owner, double(amount), type,
                   caller_rva(_ReturnAddress()));
        }
        result = animal_damage_original(self, amount, political, type, direction, attacker);
        if (observe) {
            if (auto animal = resolve(receiver.id)) {
                state(animal, "animal_damage_state");
            }
            record("native_animal_damage_return", ",\"call\":%llu,\"receiver\":%llu,\"attacker\":%llu,\"result\":%d",
                   call, receiver.id, source.id, result);
        }
    }
    const char* failure = nullptr;
    if (native_relationship_failure_ready(commands.epoch(), failure)) {
        native_actor_fence_network_inputs(failure);
    }
    return result;
}

bool checkpoint_idle(bool diagnostic_second_player = false, bool restoring_second_player = false,
                     bool allow_foreign = false);

// A console or worker request waiting for the next update, or an observed native call still on the stack.
bool setup_or_scope_busy() {
    return setup_pending || opponents_pending || players_pending || rewards_pending || duel_owner || npc_source ||
           npc_target || ability_scope || strike_scope || animal_damage_scope || pickup_scope;
}

// The tracked-actor part of checkpoint_idle: a live actor with a retained intention, a jump command, or keyboard
// steering active or waiting for its release.
bool selection_tracked_busy() {
    for (const auto& detail : tracked) {
        if (commands.find(detail.id).live && (detail.intention.sequence || detail.jump_command ||
                                              detail.steering.active || detail.steering.release.sequence)) {
            return true;
        }
    }
    return false;
}

bool foreign_selection_idle(Animal* actor, Combatant* target) {
    if (commands.pending_count() || executing || setup_foreign_pending || setup_or_scope_busy() ||
        !native_death_checkpoint_idle()) {
        return false;
    }
    if (selection_tracked_busy()) {
        return false;
    }
    for (size_t i = 0; i < 88; ++i) {
        if (actor->mInUseAbilityBits.test(i)) {
            return false;
        }
    }
    if (!target) {
        return native_network_foreign_selection(actor->mID, key(actor), UINT32_MAX, 0);
    }
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) {
            continue;
        }
        auto candidate = object_cast<Animal>(&noun);
        if (candidate && static_cast<Combatant*>(candidate) == target) {
            return native_network_foreign_selection(actor->mID, key(actor), candidate->mID, key(candidate));
        }
    }
    return false;
}

// How far a target change got through the test for a selection view on a client. The same test as before, kept in
// parts so that a denial can say which part refused.
struct SelectionGate {
    bool client_avatar = false, attack_idle = false, foreign_idle = false;
    bool primary = false, second_player = false, primary_idle = false;
};

SelectionGate selection_gate(Animal* self, Combatant* target, uint32_t caller) {
    SelectionGate gate;
    gate.client_avatar = on_thread() && caller && !executing && native_replica_is_client() &&
                         !native_replica_bootstrap_open() && self == Manager::Get()->GetAvatar() && !self->mbDead &&
                         !self->mbMarkedForDeletion;
    gate.attack_idle = gate.client_avatar && self->mCurrentAttackIdx == UINT32_MAX;
    gate.foreign_idle = gate.attack_idle && foreign_selection_idle(self, target);
    gate.primary = gate.attack_idle && !gate.foreign_idle && self == resolve(owned(1));
    gate.second_player = gate.primary && native_second_player(commands.epoch()) != nullptr;
    // Beside a foreign-species second player the checkpoint guard always refuses; the selection view asks
    // only for what it needs (native_selection_allowed_by_awards).
    gate.primary_idle =
        gate.primary && (checkpoint_idle(gate.second_player) ||
                         (gate.second_player && foreign_setup_epoch == commands.epoch() &&
                          nest_probe_epoch != commands.epoch() && native_death_checkpoint_idle() &&
                          !commands.pending_count() && !setup_or_scope_busy() && !selection_tracked_busy() &&
                          native_selection_allowed_by_awards(native_awards_checkpoint_busy(commands.epoch()))));
    return gate;
}

// Whether `target` is null or a creature found in the current native census.
bool selection_target_listed(Combatant* target) {
    auto manager = Manager::Get();
    bool live_target = target == nullptr;
    for (auto& noun : manager->mNouns) {
        if (live_target) {
            break;
        }
        if (noun.mbIsDestroyed || noun.GetNounID() != Animal::NOUN_ID) {
            continue;
        }
        auto animal = object_cast<Animal>(&noun);
        live_target = animal && !animal->mbMarkedForDeletion && static_cast<Combatant*>(animal) == target;
    }
    return live_target;
}

// Wild creatures' own target changes are denied here all the time; the player's avatar is counted apart
// so that its few denials are not crowded out (trial206).
void record_target_denied(Animal* self, Combatant* target, uint32_t caller, int intention, const SelectionGate& gate) {
    static unsigned denied_samples = 0, avatar_denied_samples = 0;
    const bool avatar = on_thread() && self == Manager::Get()->GetAvatar();
    if (on_thread() && (avatar ? avatar_denied_samples++ < 32 : denied_samples++ < 16)) {
        record("replica_target_denied",
               ",\"actor\":%llu,\"caller_rva\":%u,\"intention\":%d,\"target_present\":%s,"
               "\"client_avatar\":%s,\"attack_idle\":%s,\"foreign_idle\":%s,\"primary\":%s,\"second_player\":%s,"
               "\"primary_idle\":%s,\"pending_commands\":%zu,\"executing\":%llu,\"epoch\":%llu,"
               "\"nest_probe\":%s,\"foreign_setup\":%s,\"death_idle\":%s,\"setup_or_scope\":%s,\"tracked_busy\":%s,"
               "\"awards_idle\":%s,\"awards_idle_foreign\":%s,\"awards_busy\":%u",
               identity(self).id, caller, intention, target ? "true" : "false", gate.client_avatar ? "true" : "false",
               gate.attack_idle ? "true" : "false", gate.foreign_idle ? "true" : "false",
               gate.primary ? "true" : "false", gate.second_player ? "true" : "false",
               gate.primary_idle ? "true" : "false", static_cast<size_t>(commands.pending_count()),
               static_cast<unsigned long long>(executing), static_cast<unsigned long long>(commands.epoch()),
               nest_probe_epoch == commands.epoch() ? "true" : "false",
               foreign_setup_epoch == commands.epoch() ? "true" : "false",
               native_death_checkpoint_idle() ? "true" : "false", setup_or_scope_busy() ? "true" : "false",
               selection_tracked_busy() ? "true" : "false",
               native_awards_checkpoint_idle(commands.epoch(), false) ? "true" : "false",
               native_awards_checkpoint_idle(commands.epoch(), true) ? "true" : "false",
               native_awards_checkpoint_busy(commands.epoch()));
    }
}

// D672A0 has four saved registers and D672F4 has three stack arguments.
// At this exact, guarded callsite the callback's return slot is +32
// from the target hook's incoming return slot. No general stack unwind is inferred.
void record_attack_deactivation(Animal* self, const ActorBinding& actor, uintptr_t return_slot) {
    uintptr_t callback_return = 0, cleanup_return = 0, decision_node = 0;
    uint32_t selected_index = UINT32_MAX, selected_id = 0, selected_rva = 0;
    const bool frame_read = native_read(return_slot, 32, callback_return);
    const auto callback_caller = frame_read ? caller_rva(reinterpret_cast<void*>(callback_return)) : 0;
    bool selection_read = false;
    // Qualified only for BC8720's leaf cleanup call at BC87CE. Its three
    // saved registers plus six outgoing stack words put its return at +72.
    if (callback_caller == 0x7c87d0 && native_read(return_slot, 72, cleanup_return) &&
        caller_rva(reinterpret_cast<void*>(cleanup_return)) == 0x7c957d) {
        // BC9578 runs when a different child won. The caller's ESI is saved
        // at +64; BC957D reloads the winning index from caller ESP+54h,
        // which is +160 from this incoming slot. Never infer a general unwind.
        uint32_t count = 0, children = 0;
        if (native_read(return_slot, 64, decision_node) && native_read(return_slot, 160, selected_index) &&
            native_read(decision_node, 0x7c, count) && count <= 256 && selected_index < count &&
            native_read(decision_node, 0x80, children)) {
            const auto selected = static_cast<uintptr_t>(children) + selected_index * 0x88;
            selected_rva = caller_rva(reinterpret_cast<void*>(selected));
            selection_read = selected_rva && native_read(selected, 0, selected_id);
        }
    }
    record("native_attack_deactivation",
           ",\"actor\":%llu,\"attack_index\":%u,\"frame_read\":%s,\"callback_caller_rva\":%u,"
           "\"cleanup_caller_rva\":%u,\"selection_read\":%s,\"selected_index\":%u,\"selected_decider_id\":%u,"
           "\"selected_decider_rva\":%u",
           actor.id, self->mCurrentAttackIdx, frame_read ? "true" : "false", callback_caller,
           caller_rva(reinterpret_cast<void*>(cleanup_return)), selection_read ? "true" : "false", selected_index,
           selected_id, selected_rva);
    record_behavior(actor.id, "native_deactivation_behavior", behavior_snapshot(self));
}

// How often an unowned creature's target changes are traced, as harness_ready reports (npc_target_sample_ms).
constexpr ULONGLONG npc_target_sample_ms = 1000;

// Whether this target change is traced: one that concerns no bound actor is not.
// NPC decision evaluation repeatedly assigns and clears temporary
// targets every frame. Sample those calls once per second per actor;
// keep owned-actor changes and explicit command dispatch unthrottled.
bool target_change_traced(Animal* self, const ActorBinding& actor, uint64_t next_target) {
    if (!actor.live && !next_target) {
        return false;
    }
    ++target_callbacks;
    if (!actor.owner && !executing && (!actor.live || self->mCurrentAttackIdx == UINT32_MAX)) {
        auto detail = track(actor.id);
        auto& next_sample = detail ? detail->next_target_sample : next_unbound_target_sample;
        auto now = GetTickCount64();
        if (now < next_sample) {
            ++target_sampled_out;
            return false;
        }
        next_sample = now + npc_target_sample_ms;
    }
    return true;
}

void __fastcall target_hook(Animal* self, void*, Combatant* target, bool flag, int intention) {
    auto caller = caller_rva(_ReturnAddress());
    bool selection_view = false;
    const SelectionGate gate = selection_gate(self, target, caller);
    if (gate.foreign_idle || gate.primary_idle) {
        // A restored two-player fixture retains B's player object even on a
        // replica. Use its existing reward-idle guard instead of requiring B
        // not to exist; the latter made every normal target selection fail.
        // C03DF0's inspected body retains/releases the selected combatant,
        // updates local intention/selection timeout and refreshes native UI.
        // It does not run AI, an ability, damage, inventory or a reward. These
        // remain independently denied, including callbacks from the UI refresh.
        // Limit this reversible view to the native main avatar and a target
        // found in the current native census. No pointer or target is published.
        selection_view = selection_target_listed(target) && native_replica_allows(replica::Mutation::presentation);
    }
    if (!selection_view && !native_replica_allows(replica::Mutation::targeting)) {
        record_target_denied(self, target, caller, intention, gate);
        return;
    }
    const bool record_it = on_thread();
    auto actor = record_it ? identity(self) : ActorBinding{};
    auto next_target = record_it ? combat_id(target) : 0;
    if (actor.live && caller == 0x9672f6 && self->mCurrentAttackIdx != UINT32_MAX) {
        record_attack_deactivation(self, actor, reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));
    }
    uint64_t call = 0;
    if (target_change_traced(self, actor, next_target)) {
        call = ++native_call;
        record("native_target_enter",
               ",\"call\":%llu,\"actor\":%llu,\"native_id\":%u,\"owner\":%u,\"before\":%llu,\"target\":%llu,\"target_"
               "native_id\":%u,\"flag\":%s,\"intention\":%d,\"previous_intention\":%d,\"caller_rva\":%u,\"attack_"
               "index\":%u",
               call, actor.id, self->mID, actor.owner, combat_id(self->mpCombatantTarget), next_target,
               target ? target->ToGameData()->mID : UINT32_MAX, flag ? "true" : "false", intention,
               self->mIntentionTowardsTarget, caller, self->mCurrentAttackIdx);
    }
    target_original(self, target, flag, intention);
    if (selection_view) {
        record("replica_selection_view",
               ",\"actor\":%llu,\"target\":%llu,\"target_native_id\":%u,"
               "\"caller_rva\":%u,\"intention\":%d,\"gameplay_authority\":false",
               actor.id, combat_id(target), target ? target->ToGameData()->mID : UINT32_MAX, caller, intention);
    }
    if (call) {
        record("native_target_return", ",\"call\":%llu,\"actor\":%llu,\"attack_index\":%u", call, actor.id,
               self->mCurrentAttackIdx);
    }
}

void retire_native_incarnation(Noun* noun, const char* reason) {
    // Native IDs and allocation addresses can both be reused. Retire the
    // observed beneficiary before destruction so a later corpse cannot
    // inherit an earlier incarnation's first-feed result.
    for (auto& grant : pickup_grants) {
        if (grant.epoch == commands.epoch() && grant.native_id == noun->mID) {
            record("native_pickup_grant_retired", ",\"corpse_native_id\":%u,\"owner\":%u,\"reason\":\"%s\"",
                   grant.native_id, grant.owner, reason);
            grant = {};
        }
    }
    native_scene_invalidated(noun->mID);
    invalidate_native_relationship_entity(noun->mID);
    auto binding = commands.invalidate(key(noun));
    native_replica_invalidated(binding.id);
    if (binding.live) {
        record("invalidated", ",\"actor\":%llu,\"native_id\":%u,\"reason\":\"%s\"", binding.id, noun->mID, reason);
    }
}

void __fastcall destroy_hook(Manager* self, void*, Noun* noun) {
    if (observe_native_checkpoint_destroy(noun, caller_rva(_ReturnAddress()))) {
        return;
    }
    if (on_thread() && preserve_native_scene_fossil(key(noun), "destroy", caller_rva(_ReturnAddress()))) {
        return;
    }
    if (on_thread() && noun) {
        retire_native_incarnation(noun, "destroy");
    }
    destroy_original(self, noun); // No dereference after the native destroy.
}

Animal* current_pool_animal(Noun* candidate, uint32_t expected = UINT32_MAX) {
    auto manager = Manager::Get();
    if (!candidate || !manager || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (&noun == candidate) {
            if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID ||
                (expected != UINT32_MAX && noun.mID != expected)) {
                return nullptr;
            }
            auto animal = object_cast<Animal>(&noun);
            if (!animal || *reinterpret_cast<const uintptr_t*>(animal) != executable_base + 0x106a080 ||
                reinterpret_cast<uintptr_t>(&animal->field_E54) - key(animal) != 0xe54) {
                return nullptr;
            }
            return animal;
        }
    }
    return nullptr;
}

bool __fastcall pool_return_hook(void* self, void*, Noun* noun) {
    if (on_thread() && preserve_native_scene_fossil(key(noun), "pool_return", caller_rva(_ReturnAddress()))) {
        return false;
    }
    // ACCEC0 retains its noun in the original pool, increments Creature+E54,
    // and clears the animal's herd. It does not call DestroyInstance. Resolve
    // again after return: no saved pointer alone permits a native dereference.
    auto before = on_thread() ? current_pool_animal(noun) : nullptr;
    const auto native_id = before ? before->mID : UINT32_MAX;
    const auto counter = before ? static_cast<uint32_t>(before->field_E54) : 0;
    uint32_t other_id = UINT32_MAX;
    if (!before && on_thread() && noun && Manager::Get()) {
        for (auto& candidate : Manager::Get()->mNouns) {
            if (&candidate == noun && !candidate.mbIsDestroyed && !candidate.field_20) {
                other_id = candidate.mID;
            }
        }
    }
    const bool result = pool_return_original(self, noun);
    // ACCEC0's true return also retires non-animal pool entries. Keep their
    // replicated incarnation fenced without reading an animal-only counter
    // or dereferencing the borrowed noun after the original callback.
    if (result && other_id != UINT32_MAX) {
        native_scene_invalidated(other_id);
        invalidate_native_relationship_entity(other_id);
    }
    if (result && before) {
        if (auto after = current_pool_animal(noun, native_id)) {
            const auto next = static_cast<uint32_t>(after->field_E54);
            if (next != counter) {
                record(
                    "native_pool_return_observed",
                    ",\"native_id\":%u,\"counter_before\":%u,\"counter_after\":%u,\"enabled\":%s,\"herd_present\":%s",
                    native_id, counter, next, after->mbEnabled ? "true" : "false", after->mHerd ? "true" : "false");
                retire_native_incarnation(after, "native_pool_return");
            }
        }
    }
    return result;
}

void __fastcall land_hook(Creature* self, void*) {
    native_replica_allows(replica::Mutation::presentation);
    const bool record_it = on_thread();
    const auto binding = record_it ? identity(self) : ActorBinding{};
    if (binding.live) {
        auto detail = track(binding.id);
        record("native_landing", ",\"actor\":%llu,\"owner\":%u,\"jump_command\":%llu", binding.id, binding.owner,
               detail ? detail->jump_command : 0);
        if (detail) {
            detail->jump_command = 0;
        }
    }
    land_original(self);
}

void __fastcall npc_hook(Animal* self, void*, float delta) {
    if (!native_replica_allows(replica::Mutation::ai)) {
        return;
    }
    if (GetCurrentThreadId() == engine_thread) {
        ++worker_ai_entries;
    }
    const auto binding = on_thread() ? identity(self) : ActorBinding{};
    auto detail = binding.live ? track(binding.id) : nullptr;
    const auto before_index = detail ? self->mCurrentAttackIdx : UINT32_MAX;
    const auto before_call = detail ? detail->last_ability_call : 0;
    const auto before_behavior = detail ? behavior_snapshot(self) : BehaviorSnapshot{};
    if (detail) {
        ++detail->npc_ticks;
    }
    npc_original(self, delta);
    // Observe the original AI boundary, including an ability started and cleared
    // within the same tick. No attack state or native decision is changed.
    if (detail) {
        if (auto animal = resolve(binding.id)) {
            if (detail->last_ability_call != before_call || before_index != animal->mCurrentAttackIdx) {
                record("native_npc_attack_transition",
                       ",\"actor\":%llu,\"before_index\":%u,\"after_index\":%u,\"last_ability_call\":%llu,\"last_"
                       "ability_caller_rva\":%u,\"target\":%llu",
                       binding.id, before_index, animal->mCurrentAttackIdx, detail->last_ability_call,
                       detail->last_ability_caller, combat_id(animal->mpCombatantTarget));
                record_behavior(binding.id, "native_ai_behavior_before", before_behavior);
                record_behavior(binding.id, "native_ai_behavior_after", behavior_snapshot(animal));
            }
        }
    }
}

void __fastcall avatar_hook(Animal* self, void*, float delta) {
    if (!native_replica_allows(replica::Mutation::ai)) {
        return;
    }
    if (GetCurrentThreadId() == engine_thread) {
        ++worker_ai_entries;
    }
    if (on_thread()) {
        if (auto detail = track(identity(self).id)) {
            ++detail->avatar_ticks;
        }
    }
    avatar_original(self, delta);
}

int __fastcall damage_hook(Combatant* self, void*, float damage, uint32_t attacker_id, int type, const Vec& direction,
                           Combatant* attacker) {
    if (!native_replica_allows(replica::Mutation::damage)) {
        return 0;
    }
    const bool record_it = on_thread();
    const auto receiver = record_it ? identity(self->ToGameData()) : ActorBinding{};
    const auto source = record_it && attacker ? identity(attacker->ToGameData()) : ActorBinding{};
    const bool observe_second = record_it && receiver.live && receiver.owner == 2 && self->mHealthPoints > 0;
    uint64_t call = 0;
    if (receiver.live || source.live) {
        call = ++native_call;
        record("native_damage_enter",
               ",\"call\":%llu,\"receiver\":%llu,\"attacker\":%llu,\"attacker_owner\":%u,\"damage\":%.9g,\"attacker_"
               "political\":%u,\"type\":%d,\"receiver_health\":%.9g",
               call, receiver.id, source.id, source.owner, double(damage), attacker_id, type,
               double(self->mHealthPoints));
    }
    const int result = damage_original(self, damage, attacker_id, type, direction, attacker);
    if (observe_second && self->mHealthPoints == 0) {
        if (auto animal = resolve(receiver.id)) {
            observe_native_secondary_death(animal->mID, static_cast<uint32_t>(type));
        }
    }
    if (call) {
        record("native_damage_return", ",\"call\":%llu,\"receiver\":%llu,\"attacker\":%llu,\"result\":%d", call,
               receiver.id, source.id, result);
        if (auto animal = resolve(receiver.id)) {
            state(animal, "damage_state");
        }
    }
    return result;
}

void __fastcall ability_hook(Creature* self, void*, int index, Anim::AnimIndex* destination) {
    const auto caller = caller_rva(_ReturnAddress());
    const bool record_it = on_thread();
    const auto binding = record_it ? identity(self) : ActorBinding{};
    uint64_t call = 0;
    if (binding.live) {
        call = ++native_call;
    }
    // Foreign callbacks never touch engine-thread scope state.
    if (!record_it) {
        ability_original(self, index, destination);
        return;
    }
    CallScope scope(ability_scope, call);
    if (call) {
        if (auto detail = track(binding.id)) {
            detail->last_ability_call = call;
            detail->last_ability_caller = caller;
        }
        record("native_ability_enter",
               ",\"call\":%llu,\"actor\":%llu,\"owner\":%u,\"index\":%d,\"target\":%llu,\"caller_rva\":%u", call,
               binding.id, binding.owner, index, combat_id(self->mpCombatantTarget), caller);
    }
    ability_original(self, index, destination);
    if (call) {
        if (auto animal = resolve(binding.id)) {
            combat_context(animal, "ability_return_context");
        }
        record("native_ability_return", ",\"call\":%llu,\"actor\":%llu", call, binding.id);
    }
}

void __fastcall energy_hook(Creature* self, void*, float amount) {
    if (!native_replica_allows(replica::Mutation::energy)) {
        return;
    }
    const bool record_it = on_thread();
    const auto binding = record_it ? identity(self) : ActorBinding{};
    if (binding.live) {
        record("native_energy", ",\"actor\":%llu,\"owner\":%u,\"amount\":%.9g,\"before\":%.9g", binding.id,
               binding.owner, double(amount), double(self->mEnergy));
    }
    energy_original(self, amount);
}

void __cdecl dna_hook(float amount) {
    if (!native_replica_allows(replica::Mutation::reward)) {
        return;
    }
    auto caller = caller_rva(_ReturnAddress());
    if (dispatch_native_owned_award(amount, dna_original, caller)) {
        if (pickup_scope && native_pickup_award_scope_granted()) {
            pickup_award_observed = true;
        }
        return;
    }
    const bool record_it = on_thread() && Simulator::GetGameModeID() == kGameCreature;
    auto avatar = record_it && Manager::Get() ? Manager::Get()->GetAvatar() : nullptr;
    const auto avatar_id = avatar ? identity(avatar).id : 0;
    const auto call = record_it ? ++native_call : 0;
    if (record_it) {
        record("native_global_dna_enter",
               ",\"call\":%llu,\"amount\":%.9g,\"avatar\":%llu,\"before\":%.9g,\"caller_rva\":%u", call, double(amount),
               avatar_id, double(Simulator::cCreatureGameData::GetEvolutionPoints()), caller);
    }
    const auto before = record_it ? Simulator::cCreatureGameData::GetEvolutionPoints() : 0;
    dna_original(amount);
    const auto after = record_it ? Simulator::cCreatureGameData::GetEvolutionPoints() : 0;
    // Original first feeding legitimately awards zero DNA in this fixture.
    // Observe its returned call without inventing a positive bonus; actual
    // resource benefit additionally requires original food/nutrition evidence.
    if (pickup_scope && caller == native_pickup_reward_caller_rva && record_it && std::isfinite(amount) &&
        amount >= 0 && std::isfinite(before) && std::isfinite(after) &&
        (amount == 0 ? after == before : after >= before && after <= before + amount)) {
        pickup_award_observed = true;
    }
    if (record_it) {
        record("native_global_dna_return", ",\"call\":%llu,\"avatar\":%llu,\"after\":%.9g", call, avatar_id,
               double(after));
    }
}

Animal* current_pickup_animal(uint32_t native_id, uintptr_t expected = 0) {
    if (native_id == UINT32_MAX || !Manager::Get() || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    Animal* result = nullptr;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mID == native_id) {
            if (result || noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) {
                return nullptr;
            }
            result = object_cast<Animal>(&noun);
            if (!result || result->mbMarkedForDeletion || (expected && key(result) != expected)) {
                return nullptr;
            }
        }
    }
    return result;
}

bool pickup_scope_snapshot(NativePickupSnapshot& state, ActorBinding& binding) {
    if (!pickup_scope || !on_thread() || native_replica_is_client() || !pickup_context.id) {
        return false;
    }
    binding = commands.find(pickup_context.id);
    if (!binding.live || binding.epoch != commands.epoch() || (binding.owner != 1 && binding.owner != 2) ||
        resolve(binding.id) != pickup_context.actor) {
        return false;
    }
    return native_pickup_snapshot(pickup_context.actor, pickup_context.native_id, pickup_context.state, state) &&
           state.corpse_native_id == pickup_context.corpse_native_id &&
           current_pickup_animal(state.corpse_native_id, pickup_context.corpse);
}

bool pickup_first_feed_owner() {
    NativePickupSnapshot state;
    ActorBinding binding;
    const bool qualified = pickup_scope_snapshot(state, binding) && binding.owner == 2 && !state.eaten &&
                           state.claimed_by_actor && native_awards_ready(binding.id, binding.epoch) &&
                           native_pickup_award_scope_ready();
    if (qualified) {
        record("native_pickup_owner_classified", ",\"actor\":%llu,\"owner\":2,\"corpse_native_id\":%u", binding.id,
               state.corpse_native_id);
    }
    return qualified;
}

bool pickup_honor_owned_claim() {
    NativePickupSnapshot state;
    ActorBinding binding;
    if (!pickup_scope_snapshot(state, binding) || !state.claimant_present || state.claimed_by_actor) {
        return false;
    }
    auto claimant = current_pickup_animal(state.claimant_native_id);
    if (!claimant || claimant->mbDead || !claimant->mbEnabled || !std::isfinite(claimant->mHealthPoints) ||
        claimant->mHealthPoints <= 0) {
        return false;
    }
    const auto other = claimant ? identity(claimant) : ActorBinding{};
    if (!other.live || other.epoch != commands.epoch() || (other.owner != 1 && other.owner != 2) ||
        other.id == binding.id || resolve(other.id) != claimant) {
        return false;
    }
    record("native_pickup_existing_claim_honored",
           ",\"actor\":%llu,\"owner\":%u,\"claimant_actor\":%llu,\"claimant_owner\":%u,\"corpse_native_id\":%u",
           binding.id, binding.owner, other.id, other.owner, state.corpse_native_id);
    return true;
}

void pickup_sample(const char* name, const ActorBinding& binding, const NativePickupSnapshot& state) {
    record(name,
           ",\"actor\":%llu,\"owner\":%u,\"actor_native_id\":%u,\"corpse_native_id\":%u,\"phase\":%u,"
           "\"claimant_native_id\":%u,\"claimant_present\":%s,\"claimed_by_actor\":%s,\"fed_on\":%s,"
           "\"food\":%.9g,\"hunger\":%.9g,\"health\":%.9g,\"avatar_bit\":%s,\"first_feed_state\":%s",
           binding.id, binding.owner, state.actor_native_id, state.corpse_native_id, state.phase,
           state.claimant_native_id, state.claimant_present ? "true" : "false",
           state.claimed_by_actor ? "true" : "false", state.eaten ? "true" : "false", double(state.food),
           double(state.hunger), double(state.health), state.avatar_classified ? "true" : "false",
           state.first_feed ? "true" : "false");
}

class PickupCallbackScope {
    PickupContext previous_context;
    uint64_t previous_scope;
    bool previous_award;
    NativePickupOwnerScope unowned;

public:
    PickupCallbackScope()
        : previous_context(pickup_context),
          previous_scope(pickup_scope),
          previous_award(pickup_award_observed),
          unowned(nullptr, 0, 0, nullptr) {
        pickup_context = {};
        pickup_scope = 0;
        pickup_award_observed = false;
    }

    ~PickupCallbackScope() {
        pickup_context = previous_context;
        pickup_scope = previous_scope;
        pickup_award_observed = previous_award;
    }

    PickupCallbackScope(const PickupCallbackScope&) = delete;
    PickupCallbackScope& operator=(const PickupCallbackScope&) = delete;
};

// The grant already held for this corpse in this scene, else a free one, else null.
PickupGrant* pickup_grant_slot(uint32_t corpse_native_id, uintptr_t corpse) {
    for (auto& entry : pickup_grants) {
        if (entry.epoch == commands.epoch() && entry.native_id == corpse_native_id && entry.corpse == corpse) {
            return &entry;
        }
    }
    for (auto& entry : pickup_grants) {
        if (!entry.epoch) {
            return &entry;
        }
    }
    return nullptr;
}

// When the original tick fed on the corpse for the first time and its reward call was observed, remembers whose
// creature fed. `corpse` is the corpse's address as a number: it is compared, never dereferenced.
void observe_first_feed(const ActorBinding& binding, uintptr_t corpse, const NativePickupSnapshot& before,
                        const NativePickupSnapshot& after) {
    if (before.eaten || !after.eaten || before.corpse_native_id != after.corpse_native_id || !pickup_award_observed) {
        return;
    }
    auto current = current_pickup_animal(after.corpse_native_id, corpse);
    PickupGrant* grant = pickup_grant_slot(after.corpse_native_id, corpse);
    if (current && grant) {
        *grant = {commands.epoch(), corpse, after.corpse_native_id, binding.owner};
        record("native_pickup_first_feed_observed",
               ",\"actor\":%llu,\"owner\":%u,\"corpse_native_id\":%u,\"food_before\":%.9g,\"food_after\":%.9g,"
               "\"remaining_food_is_separate\":true",
               binding.id, binding.owner, after.corpse_native_id, double(before.food), double(after.food));
    } else {
        record("native_pickup_observation_rejected", ",\"reason\":\"corpse_lifetime_or_capacity\"");
    }
}

bool pickup_tick_dispatch(NativePickupTick original, Animal* actor, double clock, uint32_t flags, uintptr_t parameter,
                          void* state, void* activation, float delta) {
    if (!native_replica_allows(replica::Mutation::pickup)) {
        return false;
    }
    if (!on_thread()) {
        return original(actor, clock, flags, parameter, state, activation, delta);
    }
    // Even an untracked or unqualified nested original tick must not inherit
    // the outer actor's interior-branch predicates or reward/grant observation.
    // Every engine-thread return path restores both contexts through RAII.
    PickupCallbackScope callback_scope;
    const auto binding = identity(actor);
    if (!binding.live || (binding.owner != 1 && binding.owner != 2) || resolve(binding.id) != actor) {
        return original(actor, clock, flags, parameter, state, activation, delta);
    }
    NativePickupSnapshot before;
    if (!native_pickup_snapshot(actor, actor->mID, state, before)) {
        return original(actor, clock, flags, parameter, state, activation, delta);
    }
    auto corpse = current_pickup_animal(before.corpse_native_id);
    if (!corpse) {
        return original(actor, clock, flags, parameter, state, activation, delta);
    }
    // Only this original callback gets an owner identity. Individual audited
    // reward/action calls enter their own short StageScope; AI never does.
    pickup_context = {actor, binding.id, actor->mID, before.corpse_native_id, key(corpse), state};
    pickup_scope = ++native_call;
    pickup_award_observed = false;
    pickup_sample("native_pickup_tick_enter", binding, before);
    bool result = false;
    {
        NativePickupOwnerScope owner(actor, binding.owner, binding.id, corpse);
        result = original(actor, clock, flags, parameter, state, activation, delta);
    }
    NativePickupSnapshot after;
    const bool observed =
        resolve(binding.id) == actor && native_pickup_snapshot(actor, before.actor_native_id, state, after);
    if (observed) {
        pickup_sample("native_pickup_tick_return_state", binding, after);
        observe_first_feed(binding, key(corpse), before, after);
    }
    record("native_pickup_tick_return",
           ",\"actor\":%llu,\"owner\":%u,\"result\":%s,\"state_observed\":%s,\"original_reward_return_observed\":%s",
           binding.id, binding.owner, result ? "true" : "false", observed ? "true" : "false",
           pickup_award_observed ? "true" : "false");
    return result;
}

const NativeHook hooks[] = {
    {reinterpret_cast<void**>(&destroy_original), reinterpret_cast<void*>(destroy_hook)},
    {reinterpret_cast<void**>(&pool_return_original), reinterpret_cast<void*>(pool_return_hook)},
    {reinterpret_cast<void**>(&land_original), reinterpret_cast<void*>(land_hook)},
    {reinterpret_cast<void**>(&npc_original), reinterpret_cast<void*>(npc_hook)},
    {reinterpret_cast<void**>(&avatar_original), reinterpret_cast<void*>(avatar_hook)},
    {reinterpret_cast<void**>(&damage_original), reinterpret_cast<void*>(damage_hook)},
    {reinterpret_cast<void**>(&ability_original), reinterpret_cast<void*>(ability_hook)},
    {reinterpret_cast<void**>(&energy_original), reinterpret_cast<void*>(energy_hook)},
    {reinterpret_cast<void**>(&dna_original), reinterpret_cast<void*>(dna_hook)},
    {reinterpret_cast<void**>(&target_original), reinterpret_cast<void*>(target_hook)},
    {reinterpret_cast<void**>(&select_ability_original), reinterpret_cast<void*>(select_ability_hook)},
    {reinterpret_cast<void**>(&strike_original), reinterpret_cast<void*>(strike_hook)},
    {reinterpret_cast<void**>(&animal_damage_original), reinterpret_cast<void*>(animal_damage_hook)},
    {reinterpret_cast<void**>(&set_attack_original), reinterpret_cast<void*>(set_attack_hook)},
    {reinterpret_cast<void**>(&animation_done_original), reinterpret_cast<void*>(animation_done_hook)},
    {reinterpret_cast<void**>(&attack_stop_original), reinterpret_cast<void*>(attack_stop_hook)},
    {reinterpret_cast<void**>(&nest_decide_original), reinterpret_cast<void*>(nest_decide_hook)},
    {reinterpret_cast<void**>(&memory_creature_original), reinterpret_cast<void*>(memory_creature_hook)},
    native_player_message_binding(),
    native_player_remove_binding(),
    native_award_owned_binding(),
    native_award_amount_binding(),
    native_award_player_binding(),
    native_award_avatar_binding(),
    native_award_display_binding(),
    native_award_action_binding(),
    native_award_property_binding(),
    native_award_update_binding(),
    native_award_goal_binding(),
    native_award_tutorial_binding(),
    native_award_species_binding(),
    native_relationship_manager_binding(),
    native_relationship_action_binding(),
    native_history_manager_binding(),
    native_history_event_binding(),
    native_history_ui_binding(),
    native_pickup_tick_binding(),
    native_pickup_first_feed_binding(),
    native_pickup_claim_binding(),
    native_social_order_observer_binding(),
    native_social_input_binding(),
    native_pickup_input_observer_binding(),
    native_pickup_classify_observer_binding(),
    native_plant_food_stock_observer_binding(),
    native_social_tick_binding(),
    native_social_result_binding(),
    native_social_pause_binding(),
    native_social_resume_binding(),
    native_social_meter_relation_binding(),
    native_death_begin_binding(),
    native_death_cinematic_binding(),
    native_death_message_binding(),
    native_death_interaction_binding(),
    native_death_ui_deactivate_binding(),
    native_death_ui_activate_binding(),
    native_death_parameter_binding(),
    native_hatch_binding(0),
    native_hatch_binding(1),
    native_hatch_binding(2),
    native_hatch_binding(3),
    native_hatch_binding(4),
    native_hatch_binding(5),
    native_hatch_binding(6),
    native_hatch_binding(7),
    native_hatch_binding(8),
    native_hatch_binding(9),
    native_hatch_binding(10),
    native_hatch_binding(11),
    native_hatch_binding(12),
    native_hatch_binding(13),
    native_hatch_binding(14),
    native_hatch_binding(15),
    native_hatch_binding(16)};

// The verbs whose target is another creature.
bool targets_creature(ActorVerb verb) {
    return verb == ActorVerb::attack || verb == ActorVerb::approach || verb == ActorVerb::engage ||
           verb == ActorVerb::pickup || verb == ActorVerb::social || verb == ActorVerb::combat;
}

// What a caller of the worker or network boundary is told about a command it asked to queue.
worker::Result queue_result(ActorDecision decision) {
    return decision == ActorDecision::accepted ? worker::Result::accepted
           : decision == ActorDecision::full   ? worker::Result::busy
                                               : worker::Result::invalid;
}

void cancel_intention(Tracked& detail, const char* reason) {
    if (!detail.intention.sequence) {
        return;
    }
    record("intention_cancelled", ",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"reason\":\"%s\"",
           detail.intention.sequence, detail.id, detail.intention.owner, detail.intention.target, reason);
    detail.intention = {};
    detail.expires = detail.next_step = 0;
}

// Why the last foreign-species command was not admitted, for the trace (trial331: player 1's attack on a creature
// of the species player 2 had just hunted was refused without a word).
const char* foreign_refusal = "";

bool refuse_foreign(const char* reason) {
    foreign_refusal = reason;
    return false;
}

// Whether the owner giving a command on a third-species target is the player whose creature `actor` is, with that
// player's relationships ready for it. `first` and `second` are the two players' creatures.
bool foreign_owner_admitted(const ActorCommand& command, Animal* actor, Animal* target, Animal* first, Animal* second) {
    if (command.owner == 1) {
        if (command.actor != owned(1) || actor != first || actor != Manager::Get()->GetAvatar()) {
            return refuse_foreign("not_the_first_player_avatar");
        }
        return native_relationship_primary_input_ready(commands.epoch()) ||
               refuse_foreign("first_player_relationships_not_ready");
    }
    if (command.owner != 2 || command.actor != owned(2) || actor != second ||
        !experimental_foreign_actor(command.actor)) {
        return refuse_foreign("not_the_second_player_creature");
    }
    if (command.verb == ActorVerb::pickup) {
        return native_relationship_secondary_input_ready(commands.epoch()) ||
               refuse_foreign("second_player_relationships_not_ready");
    }
    return native_relationship_preflight(actor, command.actor, target, command.target, commands.epoch()) ||
           refuse_foreign("second_player_target_not_admitted");
}

bool foreign_action_admitted(const ActorCommand& command, Animal* actor, Animal* target) {
    foreign_refusal = "";
    if (!foreign_setup_actor || foreign_setup_epoch != commands.epoch()) {
        return true;
    }
    const char* failure = nullptr;
    if (native_relationship_failure_ready(commands.epoch(), failure)) {
        return refuse_foreign("relationship_failure_latched");
    }
    if (!foreign_creature_input(command.owner, command.verb, command.direction)) {
        return refuse_foreign("verb_not_admitted");
    }
    if (command.verb == ActorVerb::move || command.verb == ActorVerb::steer || command.verb == ActorVerb::steer_stop ||
        command.verb == ActorVerb::jump || command.verb == ActorVerb::stop) {
        return true;
    }
    // Plant food touches neither player's relationships; the fruit and the eater are checked where the order is given.
    if (command.verb == ActorVerb::forage) {
        return actor != nullptr;
    }
    if (!actor || !target) {
        return refuse_foreign("actor_or_target_missing");
    }
    const auto binding = identity(target);
    if (!binding.live || binding.owner || binding.id != command.target || resolve(binding.id) != target) {
        return refuse_foreign("target_binding");
    }
    auto first = resolve(owned(1)), second = resolve(owned(2));
    if (!first || !second) {
        return refuse_foreign("owner_missing");
    }
    if (target->mpSpeciesProfile == first->mpSpeciesProfile || target->mpSpeciesProfile == second->mpSpeciesProfile) {
        return refuse_foreign("target_of_an_owner_species");
    }
    if (command.verb == ActorVerb::pickup) {
        // A corpse of a third species with food left, for either owner. The original decides whether that
        // creature can eat it; before 0.0.229 only the first player could ask (trial320).
        if (!target->mbDead || !std::isfinite(target->mFoodValue) || target->mFoodValue <= 0) {
            return refuse_foreign("not_a_corpse_with_food");
        }
    } else if (command.direction < 0 || !native_combat_ability(actor, static_cast<uint32_t>(command.direction))) {
        // Each of the creature's own attacks, under the same target and relationship conditions the first
        // qualified one (Bite, slot 5) has; before 0.0.221 only that slot was admitted.
        return refuse_foreign("not_an_attack_of_this_creature");
    }
    return foreign_owner_admitted(command, actor, target, first, second);
}

// A fruit a player's creature was ordered to eat and that has left its place is gone: the original removes its noun
// or re-places it elsewhere.
void observe_plant_food() {
    for (auto& detail : tracked) {
        if (!detail.forage_fruit) {
            continue;
        }
        const bool present = native_plant_food_present(detail.forage_fruit);
        const auto eater = resolve(detail.id);
        const bool fed = eater && eater->mHunger > detail.forage_hunger + 1;
        if (!native_plant_food_left(present, present && native_plant_food_at(detail.forage_fruit, detail.forage_place),
                                    fed)) {
            continue;
        }
        detail.fruit_eaten = detail.forage_fruit;
        detail.forage_fruit = 0;
        ++detail.fruits_eaten;
        // Two players may have ordered one fruit (trial378): it leaves the world once, and feeds the one whose
        // hunger rose.
        record("native_plant_food_eaten",
               ",\"actor\":%llu,\"fruit_native_id\":%u,\"count\":%u,\"fed\":%s,\"noun_removed\":%s", detail.id,
               detail.fruit_eaten, detail.fruits_eaten, fed ? "true" : "false", present ? "false" : "true");
    }
}

// The target is still out of the ability's reach: walk towards it.
void walk_to_intention_target(const ActorCommand& command, Animal* actor, Animal* target, const Vec& delta,
                              float distance, float approach_range) {
    auto destination = target->GetPosition();
    record("intention_walk",
           ",\"actor\":%llu,\"target\":%llu,\"distance\":%.9g,\"approach_range\":%.9g,\"destination\":[%.9g,%."
           "9g,%.9g]",
           command.actor, command.target, double(distance), double(approach_range), double(destination.x),
           double(destination.y), double(destination.z));
    actor->WalkTo(Creature::kStandardSpeed, destination, delta.Normalized(), 0.3f, 0.5f);
}

// The target is within reach and the ability is free: select the target, then play the ability if both creatures
// still resolve and the command is still admitted.
void submit_intention_attack(Tracked& detail, const ActorCommand& command, Animal* actor, Animal* target, int index,
                             bool selected) {
    sample();
    if (set_creature_target(actor, static_cast<Combatant*>(target))) {
        actor = resolve(command.actor);
        target = resolve(command.target);
        if (actor && target && !actor->mbDead && !target->mbDead && foreign_action_admitted(command, actor, target)) {
            record("native_combat_input",
                   ",\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"ability_index\":%d,\"selected_by_ui\":%s",
                   command.actor, command.owner, command.target, index, selected ? "true" : "false");
            actor->PlayAbility(index);
        } else {
            cancel_intention(detail, "post_selection_identity_or_foreign_preflight");
            if (actor) {
                set_creature_target(actor, nullptr);
                stop_native_movement(actor);
            }
        }
    } else {
        cancel_intention(detail, "binding_rejected");
    }
    sample();
}

// One step of a retained engage or combat intention: cancel it, walk closer, or attack.
// Bounded owner input adapter, not a combat simulator. Original AI, WalkTo,
// PlayAbility, range checks, cooldowns, damage and rewards continue unchanged.
// Native pointers are resolved afresh each dispatch and never retained here.
void step_intention(Tracked& detail, ULONGLONG now) {
    const auto command = detail.intention;
    if (!command.sequence || now < detail.next_step) {
        return;
    }
    detail.next_step = now + 250;
    if (now >= detail.expires) {
        cancel_intention(detail, "expired");
        return;
    }
    if (commands.authorize(command) != ActorDecision::accepted) {
        cancel_intention(detail, "authority_or_target_invalid");
        return;
    }
    auto actor = resolve(command.actor);
    auto target = resolve(command.target);
    if (!actor || !target || actor->mbDead || actor->mHealthPoints <= 0 || target->mbDead ||
        target->mHealthPoints <= 0 || !target->field_E84) {
        cancel_intention(detail, "actor_or_target_unavailable");
        return;
    }
    if (!foreign_action_admitted(command, actor, target)) {
        cancel_intention(detail, "foreign_combat_preflight");
        return;
    }
    const bool selected = command.verb == ActorVerb::combat;
    int index = selected ? command.direction : actor->mDefaultAttackAbilityIndex;
    if (index < 0 || index >= 88 || index >= actor->GetAbilitiesCount() ||
        (selected && !native_combat_ability(actor, static_cast<uint32_t>(index)))) {
        cancel_intention(detail, "unsupported_ability");
        return;
    }
    auto ability = actor->GetAbility(index);
    // Use the pinned native ability and footprint values to choose when
    // to submit input. This is not a replacement for native hit checks.
    // A centre-distance constant can demand overlapping creature bodies.
    if (!ability || !std::isfinite(ability->mRange) || ability->mRange <= 0 ||
        !std::isfinite(actor->mFootprintRadius) || actor->mFootprintRadius < 0 ||
        !std::isfinite(target->mFootprintRadius) || target->mFootprintRadius < 0) {
        cancel_intention(detail, "unsupported_native_range");
        return;
    }
    float approach_range = ability->mRange + actor->mFootprintRadius + target->mFootprintRadius;
    auto delta = target->GetPosition() - actor->GetPosition();
    auto distance = std::sqrt(delta.SquaredLength());
    executing = command.sequence;
    if (distance > approach_range) {
        walk_to_intention_target(command, actor, target, delta, distance, approach_range);
    } else if (!actor->mInUseAbilityBits.test(index) && !actor->mRechargingAbilityBits.test(index)) {
        record("intention_attack",
               ",\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"distance\":%.9g,\"approach_range\":%.9g,\"ability_"
               "range\":%.9g,\"actor_footprint\":%.9g,\"target_footprint\":%.9g",
               command.actor, command.owner, command.target, double(distance), double(approach_range),
               double(ability->mRange), double(actor->mFootprintRadius), double(target->mFootprintRadius));
        submit_intention_attack(detail, command, actor, target, index, selected);
    }
    executing = 0;
}

void dispatch_intentions() {
    if (!native_network_authority_inputs_ready()) {
        native_actor_fence_network_inputs("authority_inputs_unavailable");
        return;
    }
    auto now = GetTickCount64();
    for (auto& detail : tracked) {
        step_intention(detail, now);
    }
}

// The verbs that end a social order the second player's creature holds.
bool replaces_social(ActorVerb verb) {
    return verb == ActorVerb::move || verb == ActorVerb::steer || verb == ActorVerb::approach ||
           verb == ActorVerb::engage || verb == ActorVerb::attack || verb == ActorVerb::pickup ||
           verb == ActorVerb::collect || verb == ActorVerb::stop || verb == ActorVerb::combat ||
           verb == ActorVerb::forage;
}

// steer_stop only records the release here: the stop itself runs on a later update (dispatch_keyboard_releases).
void record_keyboard_release(const ActorCommand& command) {
    const auto detail = track(command.actor);
    record("native_keyboard_release", ",\"actor\":%llu,\"pending\":%s,\"jump_command\":%llu", command.actor,
           detail && detail->steering.release.sequence ? "true" : "false", detail ? detail->jump_command : 0);
}

void perform_collect(const ActorCommand& command, Animal* animal) {
    const bool queued = native_scene_collect_fossil(commands.epoch(), command.target, command.target_generation,
                                                    command.target_native_id, animal, animal->mID);
    record("network_fossil_native_input",
           ",\"command\":%llu,\"owner\":%u,\"actor\":%llu,\"target\":%llu,\"target_generation\":%llu,\"queued\":%s",
           command.sequence, command.owner, command.actor, command.target, command.target_generation,
           queued ? "true" : "false");
}

void perform_social(const ActorCommand& command, Animal* animal, Animal* target) {
    const bool admitted = command.direction >= -1 && command.direction < 88 &&
                          (command.owner == 1 ||
                           (command.owner == 2 &&
                            ensure_native_network_social_owner(animal, animal->mID, command.actor, commands.epoch())));
    const bool queued = admitted && native_social_control(
                                        animal, animal->mID, target, target->mID,
                                        command.direction < 0 ? UINT32_MAX : static_cast<uint32_t>(command.direction));
    record("network_social_native_input",
           ",\"command\":%llu,\"owner\":%u,\"actor\":%llu,\"target\":%llu,\"ability\":%d,\"queued\":%s",
           command.sequence, command.owner, command.actor, command.target, command.direction,
           queued ? "true" : "false");
}

// How long an engage or combat intention is retained, as its record says.
constexpr ULONGLONG intention_maximum_ms = 30000;

// engage and combat: the command is retained and carried out step by step (dispatch_intentions).
void begin_intention(const ActorCommand& command) {
    if (auto detail = track(command.actor)) {
        detail->intention = command;
        detail->expires = GetTickCount64() + intention_maximum_ms;
        detail->next_step = 0;
        record("intention_started", ",\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"maximum_ms\":30000", command.actor,
               command.owner, command.target);
    }
}

// move, steer and approach. False when a steer command's camera direction is not valid: the command is abandoned.
bool perform_walk(const ActorCommand& command, Animal* animal, Animal* target) {
    auto destination = target ? target->GetPosition() : offset(animal, command.direction, 6.0f);
    if (command.verb == ActorVerb::steer) {
        network::SteeringVector requested, tangent;
        const auto position = animal->GetPosition();
        if (!network::unpack_steering(command.direction, requested) ||
            !network::steering_tangent(requested, {position.x, position.y, position.z}, tangent)) {
            record("command_rejected", ",\"reason\":\"invalid_camera_direction\"");
            return false;
        }
        destination = position + Vec(tangent.x, tangent.y, tangent.z) * 6.0f;
    }
    auto direction = (destination - animal->GetPosition()).Normalized();
    record("native_walk_request", ",\"actor\":%llu,\"destination\":[%.9g,%.9g,%.9g]", command.actor,
           double(destination.x), double(destination.y), double(destination.z));
    animal->WalkTo(Creature::kStandardSpeed, destination, direction, 0.5f, 1.0f);
    return true;
}

// Observation only: original C18CA0's three early refusal gates. The
// original call that follows remains authoritative and executes exactly once.
void record_jump_preconditions(const ActorCommand& command, Animal* animal) {
    auto profile = animal->mpSpeciesProfile;
    auto spatial = static_cast<Simulator::cSpatialObject*>(animal);
    const bool layout =
        profile &&
        reinterpret_cast<uintptr_t>(&animal->mpSpeciesProfile) - reinterpret_cast<uintptr_t>(animal) == 0xb20 &&
        reinterpret_cast<uintptr_t>(spatial) - reinterpret_cast<uintptr_t>(animal) == 0xc0 &&
        reinterpret_cast<uintptr_t>(&spatial->mbSupported) - reinterpret_cast<uintptr_t>(spatial) == 0x77 &&
        reinterpret_cast<uintptr_t>(&spatial->mFlags) - reinterpret_cast<uintptr_t>(spatial) == 0x50 &&
        reinterpret_cast<uintptr_t>(&profile->mJumpLevel) - reinterpret_cast<uintptr_t>(profile) == 0x610 &&
        reinterpret_cast<uintptr_t>(&profile->mCreatureAbilityJumping) - reinterpret_cast<uintptr_t>(profile) ==
            0x548 &&
        reinterpret_cast<uintptr_t>(&profile->mCreatureAbilityFlapping) - reinterpret_cast<uintptr_t>(profile) == 0x550;
    if (!layout) {
        record("native_jump_preconditions_unavailable", ",\"actor\":%llu,\"reason\":\"profile_layout\"", command.actor);
        return;
    }
    const float jumping = profile->mCreatureAbilityJumping, flapping = profile->mCreatureAbilityFlapping;
    const float selected = spatial->mbSupported ? jumping : flapping;
    const auto bits = [](float value) {
        uint32_t word = 0;
        std::memcpy(&word, &value, sizeof(word));
        return word;
    };
    char jump_value[48] = "null", flap_value[48] = "null", selected_value[48] = "null";
    if (std::isfinite(jumping)) {
        sprintf_s(jump_value, "%.9g", double(jumping));
    }
    if (std::isfinite(flapping)) {
        sprintf_s(flap_value, "%.9g", double(flapping));
    }
    if (std::isfinite(selected)) {
        sprintf_s(selected_value, "%.9g", double(selected));
    }
    record("native_jump_preconditions",
           ",\"actor\":%llu,\"native_id\":%u,\"owner\":%u,\"jump_level\":%d,"
           "\"supported\":%s,\"spatial_flags\":%u,\"jumping\":%s,\"flapping\":%s,\"selected_power\":%s,"
           "\"power_bits\":[%u,%u,%u],\"level_zero\":%s,\"air_flag_refusal\":%s,\"selected_not_positive\":%s,"
           "\"energy_argument\":0",
           command.actor, animal->mID, command.owner, profile->mJumpLevel, spatial->mbSupported ? "true" : "false",
           spatial->mFlags, jump_value, flap_value, selected_value, bits(jumping), bits(flapping), bits(selected),
           profile->mJumpLevel == 0 ? "true" : "false",
           !spatial->mbSupported && (spatial->mFlags & 0x1000) ? "true" : "false", !(selected > 0) ? "true" : "false");
}

void perform_jump(const ActorCommand& command, Animal* animal) {
    auto detail = track(command.actor);
    if (detail) {
        detail->jump_command = command.sequence;
    }
    record_jump_preconditions(command, animal);
    bool accepted = animal->DoJump(0);
    record("native_jump_return", ",\"actor\":%llu,\"accepted\":%s", command.actor, accepted ? "true" : "false");
    if (detail && !accepted) {
        detail->jump_command = 0;
    }
}

// The creature's default attack on its target, once. False when the target binding was rejected: the command is
// abandoned.
bool perform_attack(const ActorCommand& command, Animal* animal, Animal* target) {
    int index = animal->mDefaultAttackAbilityIndex;
    if (index < 0 || index >= animal->GetAbilitiesCount()) {
        record("unsupported_ability", ",\"actor\":%llu", command.actor);
        return true;
    }
    if (!set_creature_target(animal, static_cast<Combatant*>(target))) {
        return false;
    }
    state(animal, "target_assigned");
    animal = resolve(command.actor);
    target = resolve(command.target);
    if (animal && target && !animal->mbDead && !target->mbDead) {
        animal->PlayAbility(index);
    }
    return true;
}

void perform_forage(const ActorCommand& command, Animal* animal) {
    const bool ordered = native_plant_food_order(animal, animal->mID, static_cast<uint32_t>(command.direction),
                                                 command.target_generation);
    if (auto detail = track(command.actor); detail && ordered) {
        detail->forage_fruit = static_cast<uint32_t>(command.direction);
        detail->native_id = animal->mID;
        detail->forage_hunger = animal->mHunger;
        detail->forage_place = command.target_generation;
    }
    record("native_plant_food_order_return",
           ",\"actor\":%llu,\"owner\":%u,\"fruit_native_id\":%d,\"ordered\":%s,\"accepted_is_queued\":true",
           command.actor, command.owner, command.direction, ordered ? "true" : "false");
}

void perform_pickup(const ActorCommand& command, Animal* animal, Animal* target) {
    const bool ordered = native_pickup_order(animal, animal->mID, target, target->mID);
    record("native_pickup_order_return",
           ",\"actor\":%llu,\"owner\":%u,\"target\":%llu,\"ordered\":%s,\"accepted_is_queued\":true", command.actor,
           command.owner, command.target, ordered ? "true" : "false");
}

void perform_retire(Animal* animal) {
    if (animal != Manager::Get()->GetAvatar()) {
        Manager::Get()->DestroyInstance(animal);
    } else {
        record("retire_rejected_avatar");
    }
}

// Carries out an admitted command. False when it was abandoned part-way: such a command reports no return.
bool perform(const ActorCommand& command, Animal* animal, Animal* target) {
    switch (command.verb) {
    case ActorVerb::steer_stop:
        record_keyboard_release(command);
        return true;
    case ActorVerb::collect:
        perform_collect(command, animal);
        return true;
    case ActorVerb::social:
        perform_social(command, animal, target);
        return true;
    case ActorVerb::engage:
    case ActorVerb::combat:
        begin_intention(command);
        return true;
    case ActorVerb::move:
    case ActorVerb::steer:
    case ActorVerb::approach:
        return perform_walk(command, animal, target);
    case ActorVerb::jump:
        perform_jump(command, animal);
        return true;
    case ActorVerb::attack:
        return perform_attack(command, animal, target);
    case ActorVerb::forage:
        perform_forage(command, animal);
        return true;
    case ActorVerb::pickup:
        perform_pickup(command, animal, target);
        return true;
    case ActorVerb::stop:
        set_creature_target(animal, nullptr);
        return true;
    case ActorVerb::retire:
        perform_retire(animal);
        return true;
    }
    return true;
}

// Resolves the creature a queued command is for and, for a targeted verb, its target. False, with the reason
// recorded, when the command cannot run now.
bool admit_command(const ActorCommand& command, Animal*& animal, Animal*& target) {
    if (!native_network_authority_inputs_ready()) {
        native_actor_fence_network_inputs("authority_inputs_unavailable");
        record("command_rejected", ",\"command\":%llu,\"actor\":%llu,\"reason\":\"network_inputs_fenced\"",
               command.sequence, command.actor);
        return false;
    }
    auto decision = commands.authorize(command);
    animal = decision == ActorDecision::accepted ? resolve(command.actor) : nullptr;
    if (!animal || animal->mbDead || animal->mHealthPoints <= 0) {
        record("command_rejected", ",\"command\":%llu,\"actor\":%llu,\"reason\":\"%s\"", command.sequence,
               command.actor, animal ? "dead_or_zero_health" : actor_decision(decision));
        return false;
    }
    const bool targeted = targets_creature(command.verb);
    target = targeted ? resolve(command.target) : nullptr;
    if (targeted && (!target || !target->field_E84 ||
                     (command.verb == ActorVerb::pickup ? !target->mbDead || target->mFoodValue <= 0
                                                        : target->mbDead || target->mHealthPoints <= 0))) {
        record("command_rejected", ",\"command\":%llu,\"reason\":\"target_unavailable\"", command.sequence);
        return false;
    }
    if (!foreign_action_admitted(command, animal, target)) {
        record("command_rejected", ",\"command\":%llu,\"reason\":\"foreign_combat_preflight\"", command.sequence);
        return false;
    }
    return true;
}

void execute(ActorCommand command) {
    Animal* animal = nullptr;
    Animal* target = nullptr;
    if (!admit_command(command, animal, target)) {
        return;
    }
    executing = command.sequence;
    if (auto detail = track(command.actor)) {
        detail->commanded_epoch = commands.epoch();
    }
    if (auto detail = track(command.actor); detail && command.verb != ActorVerb::steer_stop) {
        cancel_intention(*detail, "superseded_by_command");
    }
    sample();
    record("command_begin",
           ",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"%s\",\"target\":%llu,\"direction\":%d",
           command.sequence, command.actor, command.owner, actor_verb(command.verb), command.target, command.direction);
    if (command.owner == 2 && replaces_social(command.verb) &&
        !cancel_native_network_social_owner(command.actor, commands.epoch())) {
        record("command_rejected",
               ",\"command\":%llu,\"actor\":%llu,\"reason\":\"native_social_cancellation_unavailable\"",
               command.sequence, command.actor);
        sample();
        executing = 0;
        flush();
        return;
    }
    if (auto detail = track(command.actor)) {
        detail->steering.observe(command);
    }
    if (!perform(command, animal, target)) {
        executing = 0;
        flush();
        return;
    }
    record("command_return", ",\"command\":%llu,\"actor\":%llu", command.sequence, command.actor);
    sample();
    executing = 0;
    flush();
}

void dispatch_keyboard_releases() {
    for (auto& detail : tracked) {
        const auto release = detail.steering.release;
        if (!release.sequence) {
            continue;
        }
        auto animal = commands.authorize(release) == ActorDecision::accepted ? resolve(release.actor) : nullptr;
        if (!animal || animal->mbDead || animal->mHealthPoints <= 0) {
            detail.steering.clear();
            continue;
        }
        // OnJumpLand clears the token, then returns through the original game.
        // StopMovement runs only on a later AppUpdate, outside that callback.
        if (detail.jump_command) {
            continue;
        }
        detail.steering.clear();
        executing = release.sequence;
        const bool stopped = stop_native_movement(animal);
        record("native_keyboard_stop", ",\"actor\":%llu,\"command\":%llu,\"stopped\":%s", release.actor,
               release.sequence, stopped ? "true" : "false");
        sample();
        executing = 0;
        flush();
    }
}

// See native_unowned_movement. Runs on AppUpdate, the context the keyboard stop above already uses.
// Client only, each frame: this game's own avatar keeps no native locomotion request of its own.
void clear_client_avatar_request() {
    if (!native_replica_is_client()) {
        return;
    }
    auto manager = Manager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    if (!avatar || avatar->mbDead || avatar->mHealthPoints <= 0) {
        return;
    }
    static uint64_t counted_epoch = 0;
    static uint32_t stops = 0;
    const auto epoch = commands.epoch();
    if (counted_epoch != epoch) {
        counted_epoch = epoch;
        stops = 0;
    }
    const auto words = static_cast<size_t>(static_cast<Simulator::cLocomotiveObject*>(avatar)->field_1F4.size());
    const auto action = native_client_avatar_request(words, stops);
    if (action == ClientRequest::none) {
        return;
    }
    const auto native_id = avatar->mID;
    if (action == ClientRequest::exhausted) {
        if (stops == native_client_request_stop_limit) {
            ++stops;
            record("native_client_avatar_request_unstoppable", ",\"native_id\":%u,\"stops\":%u", native_id,
                   native_client_request_stop_limit);
            flush();
        }
        return;
    }
    const auto stop = ++stops;
    stop_native_movement(avatar);
    avatar = manager->GetAvatar();
    const auto left = avatar && avatar->mID == native_id
                          ? static_cast<size_t>(static_cast<Simulator::cLocomotiveObject*>(avatar)->field_1F4.size())
                          : SIZE_MAX;
    record("native_client_avatar_request_cleared",
           ",\"native_id\":%u,\"stop\":%u,\"request_words\":%zu,\"request_words_after\":%zu", native_id, stop, words,
           left);
    flush();
}

void fence_unowned_movement() {
    // Authority only: on a client every creature moves by replication and no command ever begins there.
    if (native_replica_is_client()) {
        return;
    }
    const auto epoch = commands.epoch();
    for (auto& detail : tracked) {
        const auto owner = detail.id ? commands.find(detail.id).owner : 0;
        if (!owner) {
            continue;
        }
        auto animal = resolve(detail.id);
        if (!animal || animal->mbDead || animal->mHealthPoints <= 0) {
            continue;
        }
        if (detail.unowned_epoch != epoch) {
            detail.unowned_epoch = epoch;
            detail.unowned_stops = 0;
        }
        const auto& velocity = static_cast<Simulator::cLocomotiveObject*>(animal)->mVelocity;
        const auto action =
            native_unowned_movement(detail.commanded_epoch == epoch,
                                    velocity.x != 0 || velocity.y != 0 || velocity.z != 0, detail.unowned_stops);
        if (action == UnownedMovement::none) {
            continue;
        }
        if (action == UnownedMovement::exhausted) {
            if (detail.unowned_stops == native_unowned_stop_limit) {
                ++detail.unowned_stops;
                record("native_unowned_movement_unstoppable", ",\"actor\":%llu,\"owner\":%u,\"stops\":%u", detail.id,
                       owner, native_unowned_stop_limit);
                flush();
            }
            continue;
        }
        const auto stop = ++detail.unowned_stops;
        const auto id = detail.id;
        const bool stopped = stop_native_movement(animal);
        record("native_unowned_movement_stopped", ",\"actor\":%llu,\"owner\":%u,\"stop\":%u,\"stopped\":%s", id, owner,
               stop, stopped ? "true" : "false");
        flush();
    }
}

uint64_t integer(const char* value) {
    if (!value || !*value || strlen(value) > 18) {
        return 0;
    }
    for (auto c = value; *c; ++c) {
        if (*c < '0' || *c > '9') {
            return 0;
        }
    }
    return _strtoui64(value, nullptr, 10);
}

// The console's developer walk towards a fossil: OWNER FOSSIL. False when it is rejected.
bool queue_console_fossil_approach(const char* owner_word, const char* target_word) {
    const auto owner = integer(owner_word), target = integer(target_word);
    const auto actor_id = owner == 1 || owner == 2 ? owned(static_cast<uint32_t>(owner)) : 0;
    auto actor = resolve(actor_id);
    float position[3]{};
    if (fossil_probe.actor || fossil_probe_count >= 12 || !commands.inputs_ready() ||
        !native_network_authority_inputs_ready() || !actor || actor->mbDead || target >= UINT32_MAX ||
        !native_fossil_position(static_cast<uint32_t>(target), position)) {
        App::ConsolePrintF("M11 developer fossil approach rejected.");
        return false;
    }
    fossil_probe = {commands.epoch(), actor_id, static_cast<uint32_t>(owner), actor->mID,
                    static_cast<uint32_t>(target)};
    ++fossil_probe_count;
    record("native_fossil_approach_probe_queued",
           ",\"actor\":%llu,\"owner\":%u,\"target_native_id\":%u,\"diagnostic_only\":true", actor_id,
           static_cast<uint32_t>(owner), static_cast<uint32_t>(target));
    App::ConsolePrintF("M11 developer approach queued; collection is not requested.");
    return true;
}

// The console's four words for a direction, numbered as ActorCommand::direction and `offset` count them.
bool parse_move_direction(const char* word, int& direction) {
    if (!strcmp(word, "forward")) {
        direction = 0;
    } else if (!strcmp(word, "right")) {
        direction = 1;
    } else if (!strcmp(word, "back")) {
        direction = 2;
    } else if (!strcmp(word, "left")) {
        direction = 3;
    } else {
        return false;
    }
    return true;
}

// A console line that is one owner's command to its actor: VERB OWNER ACTOR and, for some verbs, a target or a
// direction. False when the line is not such a command; nothing is queued then.
bool queue_console_command(const char* const* args, size_t count) {
    ActorCommand command{};
    uint64_t owner = integer(args[1]);
    if (owner > UINT32_MAX) {
        owner = 0;
    }
    command.owner = static_cast<uint32_t>(owner);
    command.actor = integer(args[2]);
    if (!strcmp(args[0], "move") && count == 4) {
        command.verb = ActorVerb::move;
        if (!parse_move_direction(args[3], command.direction)) {
            App::ConsolePrintF("M03 move direction: forward/right/back/left");
            return false;
        }
    } else if (!strcmp(args[0], "attack") && count == 4) {
        command.verb = ActorVerb::attack;
        command.target = integer(args[3]);
    } else if (!strcmp(args[0], "approach") && count == 4) {
        command.verb = ActorVerb::approach;
        command.target = integer(args[3]);
    } else if (!strcmp(args[0], "engage") && count == 4) {
        command.verb = ActorVerb::engage;
        command.target = integer(args[3]);
    } else if (!strcmp(args[0], "jump") && count == 3) {
        command.verb = ActorVerb::jump;
    } else if (!strcmp(args[0], "stop") && count == 3) {
        command.verb = ActorVerb::stop;
    } else if (!strcmp(args[0], "retire") && count == 3) {
        command.verb = ActorVerb::retire;
    } else {
        App::ConsolePrintF("M03 unknown command.");
        return false;
    }
    auto decision = commands.enqueue(command);
    record("command_queued",
           ",\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"%s\",\"target\":%llu,\"decision\":\"%s\"",
           command.sequence, command.actor, command.owner, actor_verb(command.verb), command.target,
           actor_decision(decision));
    App::ConsolePrintF("M03 %s actor %llu: %s, command %llu", actor_verb(command.verb), command.actor,
                       actor_decision(decision), command.sequence);
    return true;
}

class Command final : public ArgScript::ICommand {
public:
    void ParseLine(const ArgScript::Line& line) override {
        if (!on_thread()) {
            return;
        }
        if (native_replica_is_client()) {
            App::ConsolePrintF("M05 replica commands require the bounded worker control channel.");
            return;
        }
        size_t count = 0;
        auto args = line.GetArgumentsRange(&count, 1, 4);
        if (Simulator::GetGameModeID() != kGameCreature) {
            App::ConsolePrintF("M03 requires the original Creature campaign.");
            return;
        }
        if (count == 1 && !strcmp(args[0], "setup")) {
            setup_foreign_pending = false;
            setup_pending = true;
        } else if (count == 1 && !strcmp(args[0], "players")) {
            players_pending = 1;
        } else if (count == 2 && !strcmp(args[0], "players") && !strcmp(args[1], "off")) {
            players_pending = -1;
        } else if (count == 1 && !strcmp(args[0], "rewards")) {
            rewards_pending = true;
        } else if (count == 1 && !strcmp(args[0], "opponents")) {
            opponents_pending = true;
        } else if (count == 2 && !strcmp(args[0], "duel") && (integer(args[1]) == 1 || integer(args[1]) == 2)) {
            duel_owner = static_cast<uint32_t>(integer(args[1]));
        } else if (count == 1 && !strcmp(args[0], "status")) {
            sample();
            App::ConsolePrintF("M03: A=%llu (owner 1), B=%llu (owner 2), epoch %llu, trace %s", owned(1), owned(2),
                               commands.epoch(), failed ? "incomplete" : "healthy");
        } else if (count == 3 && !strcmp(args[0], "fossil-approach")) {
            if (!queue_console_fossil_approach(args[1], args[2])) {
                return;
            }
        } else if (count == 3 && !strcmp(args[0], "npc")) {
            npc_source = integer(args[1]);
            npc_target = integer(args[2]);
        } else if (count >= 3) {
            if (!queue_console_command(args, count)) {
                return;
            }
        } else {
            App::ConsolePrintF("M03: setup | players [off] | opponents | duel OWNER | status | move OWNER ACTOR "
                               "DIRECTION | jump OWNER ACTOR | attack/approach/engage OWNER ACTOR TARGET | npc NPC "
                               "TARGET | stop OWNER ACTOR | retire OWNER ACTOR");
        }
        flush();
    }
};

// The trace gets a sample of every tracked actor four times a second at most.
void sample_when_due() {
    if (GetTickCount64() - last_sample >= 250) {
        last_sample = GetTickCount64();
        sample();
        flush();
    }
}

// The scene is being left: cancels and clears what was queued, retained or pending in it.
void leave_scene() {
    native_network_scene_exit();
    reset_native_network_player_controller();
    native_replica_scene_exit();
    for (auto& detail : tracked) {
        cancel_intention(detail, "scene_exit");
        detail.steering.clear();
    }
    retire_native_player_context("scene_exit");
    players_pending = 0;
    rewards_pending = false;
    fossil_probe = {};
    record("scene_exit");
    commands.scene_exit();
    pickup_grants = {};
    foreign_setup_actor = foreign_setup_epoch = 0;
    setup_foreign_pending = false;
    setup_pending = opponents_pending = false;
    npc_source = npc_target = 0;
    duel_owner = 0;
    flush();
}

void apply_pending_setup() {
    if (!setup_pending) {
        return;
    }
    const bool mode = setup_foreign_pending;
    setup_pending = setup_foreign_pending = false;
    setup(mode);
}

void apply_pending_players() {
    if (!players_pending) {
        return;
    }
    const int action = players_pending;
    players_pending = 0;
    if (action < 0) {
        retire_native_player_context("explicit_stop");
    } else {
        auto second = resolve(owned(2));
        const bool created = second && !second->mbDead && create_native_player_context(commands.epoch());
        record("player_context_request", ",\"created\":%s", created ? "true" : "false");
        App::ConsolePrintF(created ? "M03 native player B initialized; reward routing remains disabled."
                                   : "M03 native player creation rejected.");
    }
    sample();
    flush();
}

void apply_pending_rewards() {
    if (!rewards_pending) {
        return;
    }
    rewards_pending = false;
    const auto actor_id = owned(2);
    const bool reward_ready =
        enable_native_awards(resolve(actor_id), actor_id, commands.epoch(), experimental_foreign_actor(actor_id));
    record("reward_context_request", ",\"enabled\":%s", reward_ready ? "true" : "false");
    App::ConsolePrintF(reward_ready ? "M03 native B reward context enabled."
                                    : "M03 reward context rejected; setup and players required.");
    flush();
}

double vector_length(const Vec& delta) {
    return std::sqrt(double(delta.x) * delta.x + double(delta.y) * delta.y + double(delta.z) * delta.z);
}

// The distances from a fossil at which the developer approach orders a walk.
bool fossil_approach_distance(double distance) {
    return std::isfinite(distance) && distance > 7 && distance <= 200;
}

// How far from the fossil the developer approach stops, as its record says.
constexpr float fossil_standoff_distance = 6.0f;

// Walks the probe's actor to a standoff from its fossil. False when no walk was ordered.
bool walk_to_fossil(const FossilApproachProbe& request) {
    auto actor = resolve(request.actor);
    const auto binding = commands.find(request.actor);
    float position[3]{};
    if (native_replica_is_client() || request.epoch != commands.epoch() || !binding.live ||
        binding.owner != request.owner || !actor || actor->mID != request.native_id || actor->mbDead ||
        !std::isfinite(actor->mHealthPoints) || !(actor->mHealthPoints > 0) ||
        !native_replica_allows(replica::Mutation::targeting) || !native_fossil_position(request.target, position)) {
        return false;
    }
    const auto delta = Vec(position[0], position[1], position[2]) - actor->GetPosition();
    if (!fossil_approach_distance(vector_length(delta)) ||
        (request.owner == 2 && !cancel_native_network_social_owner(request.actor, commands.epoch()))) {
        return false;
    }
    actor = resolve(request.actor);
    if (!actor || actor->mID != request.native_id || actor->mbDead || !(actor->mHealthPoints > 0) ||
        !native_fossil_position(request.target, position)) {
        return false;
    }
    const Vec fossil(position[0], position[1], position[2]);
    const auto fresh_delta = fossil - actor->GetPosition();
    if (!fossil_approach_distance(vector_length(fresh_delta))) {
        return false;
    }
    // Keep the fossil visible for the next original
    // click. This probe never requests collection.
    const auto direction = fresh_delta.Normalized();
    const Vec destination = fossil - direction * fossil_standoff_distance;
    if (auto detail = track(request.actor)) {
        cancel_intention(*detail, "developer_fossil_approach");
        detail->steering.clear();
    }
    actor->WalkTo(Creature::kStandardSpeed, destination, direction, 0.5f, 1.0f);
    return true;
}

void run_fossil_approach_probe() {
    if (!fossil_probe.actor) {
        return;
    }
    const auto request = fossil_probe;
    fossil_probe = {};
    const bool walked = walk_to_fossil(request);
    record("native_fossil_approach_probe_return",
           ",\"actor\":%llu,\"owner\":%u,\"target_native_id\":%u,\"walk_called\":%s,\"standoff_distance\":"
           "6,\"diagnostic_only\":true,\"collection_requested\":false",
           request.actor, request.owner, request.target, walked ? "true" : "false");
}

void apply_pending_opponents() {
    if (!opponents_pending) {
        return;
    }
    opponents_pending = false;
    opponents();
}

void apply_pending_duel() {
    if (!duel_owner) {
        return;
    }
    auto owner = duel_owner;
    duel_owner = 0;
    duel(owner);
}

// The console's `npc NPC TARGET`: gives one unowned creature another as its target.
void apply_pending_npc_target() {
    if (!npc_source) {
        return;
    }
    auto source = resolve(npc_source);
    auto target = resolve(npc_target);
    if (source && target && !commands.find(npc_source).owner && !source->mbDead && !target->mbDead) {
        record("npc_target_request", ",\"actor\":%llu,\"target\":%llu", npc_source, npc_target);
        set_creature_target(source, static_cast<Combatant*>(target));
        state(source, "npc_target_assigned");
    } else {
        record("npc_target_rejected");
    }
    npc_source = npc_target = 0;
}

// One update of the Creature scene: what the console and the worker left pending, then one queued command, then the
// work that continues across updates.
void update_scene() {
    if (!native_network_authority_inputs_ready()) {
        native_actor_fence_network_inputs("authority_inputs_unavailable");
        sample_when_due();
        return;
    }
    apply_pending_setup();
    apply_pending_players();
    apply_pending_rewards();
    update_native_awards(commands.epoch());
    const char* relationship_failure = nullptr;
    if (native_relationship_failure_ready(commands.epoch(), relationship_failure)) {
        native_actor_fence_network_inputs(relationship_failure);
        sample_when_due();
        return;
    }
    update_native_secondary_death();
    update_native_network_social_owner(commands.epoch());
    run_fossil_approach_probe();
    apply_pending_opponents();
    apply_pending_duel();
    apply_pending_npc_target();
    ActorCommand command;
    if (commands.pop(command)) {
        execute(command);
    }
    dispatch_keyboard_releases();
    dispatch_intentions();
    observe_plant_food();
    fence_unowned_movement();
    clear_client_avatar_request();
    sample_when_due();
}

class Listener final : public App::IUnmanagedMessageListener {
public:
    bool HandleMessage(uint32_t id, void*) override {
        if (!on_thread()) {
            return false;
        }
        if (id == App::kMsgOnModeExit) {
            leave_scene();
        } else if (id == App::kMsgAppUpdate && Simulator::GetGameModeID() == kGameCreature) {
            update_scene();
        }
        return false;
    }
} listener;

constexpr uint32_t message_ids[] = {App::kMsgAppUpdate, App::kMsgOnModeExit};

bool verify_context_bindings() {
    // Exact installed executable inspection, 2026-09-12. This supplements the
    // pre-injection file guard; it is not runtime gameplay qualification.
    struct Code {
        uint32_t rva;
        std::array<unsigned char, 16> bytes;
    };

    const Code code[] = {
        {0x6ccec0, {0x83, 0xec, 0x78, 0x53, 0x8b, 0x9c, 0x24, 0x80, 0x00, 0x00, 0x00, 0x56, 0x8b, 0xf1, 0x85, 0xdb}},
        {0x6cd0d5, {0xff, 0x86, 0x54, 0x0e, 0x00, 0x00, 0x8b, 0x55, 0x00, 0x8b, 0x42, 0x20, 0x8b, 0xcd, 0xff, 0xd0}},
        {0x6cd13e, {0x8b, 0xce, 0xff, 0xd2, 0x5f, 0x5d, 0x5e, 0xb0, 0x01, 0x5b, 0x83, 0xc4, 0x78, 0xc2, 0x04, 0x00}},
        {0x819800, {0x53, 0x55, 0x56, 0x57, 0x8b, 0xf9, 0x83, 0xcd, 0xff, 0x80, 0x7c, 0x24, 0x18, 0x00, 0x8d, 0x9f}},
        {0x802b40, {0x8b, 0x44, 0x24, 0x0c, 0x83, 0xec, 0x20, 0x53, 0x55, 0x57, 0x8b, 0x7c, 0x24, 0x30, 0x8b, 0xe9}},
        {0x8075e0, {0x83, 0xec, 0x6c, 0x53, 0x8b, 0xd9, 0xe8, 0x45, 0x0c, 0xce, 0xff, 0x83, 0xf8, 0x02, 0x0f, 0x84}},
        {0x802ff0, {0x8b, 0x81, 0x98, 0x0a, 0x00, 0x00, 0x25, 0x00, 0x03, 0x00, 0x00, 0xf7, 0xd8, 0x1b, 0xc0, 0xf7}},
        {0x807500, {0x8b, 0x44, 0x24, 0x04, 0x57, 0x8b, 0xf9, 0x89, 0x87, 0x8c, 0x0e, 0x00, 0x00, 0x83, 0xf8, 0xff}},
        {0x80e2d0, {0x56, 0x8b, 0xf1, 0x8b, 0x8e, 0x54, 0x0b, 0x00, 0x00, 0x85, 0xc9, 0x75, 0x06, 0xb0, 0x01, 0x5e}},
        {0x965980, {0x57, 0x8b, 0x7c, 0x24, 0x14, 0x85, 0xff, 0x0f, 0x84, 0x4a, 0x02, 0x00, 0x00, 0x8b, 0x07, 0x8b}},
        {0x9672a0, {0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x10, 0x80, 0xbe, 0x5e, 0x0b, 0x00, 0x00, 0x00, 0x57, 0x75}},
        {0x7c8720, {0x53, 0x56, 0x8b, 0x74, 0x24, 0x20, 0x57, 0x8b, 0x7c, 0x24, 0x20, 0x32, 0xdb, 0x8d, 0x49, 0x00}},
        {0x7c9578, {0xe8, 0xa3, 0xf1, 0xff, 0xff, 0x8b, 0x4c, 0x24, 0x54, 0x83, 0xc4, 0x18, 0x8b, 0x44, 0x24, 0x2c}},
        {0x97d900, {0xf7, 0x44, 0x24, 0x10, 0x00, 0x10, 0x00, 0x00, 0x56, 0x57, 0x0f, 0x85, 0xd3, 0x00, 0x00, 0x00}},
        {0x999930, {0x8b, 0x44, 0x24, 0x04, 0x56, 0x85, 0xc0, 0x74, 0x33, 0x8b, 0x48, 0x2c, 0x85, 0xc9, 0x74, 0x2c}}};
    const auto image_size = executable_end - executable_base;
    for (const auto& entry : code) {
        if (entry.rva > image_size || entry.bytes.size() > image_size - entry.rva ||
            memcmp(reinterpret_cast<const void*>(executable_base + entry.rva), entry.bytes.data(),
                   entry.bytes.size())) {
            record("context_binding_rejected", ",\"rva\":%u", entry.rva);
            return false;
        }
    }

    const struct {
        uint32_t slot, expected;
    } slots[] = {
        {0x106a080 + 0xb8, 0x802b40}, // Animal's primary vtable, SDK funcB8h
        {0x1069e30 + 0x18, 0x8075e0}, // Animal's Combatant virtual TakeDamage
        {0x1069ec0 + 0x58, 0x802ff0}  // Animal's Spatial IsPlayerOwned
    };

    for (const auto& entry : slots) {
        if (entry.slot > image_size || sizeof(uintptr_t) > image_size - entry.slot ||
            *reinterpret_cast<const uintptr_t*>(executable_base + entry.slot) != executable_base + entry.expected) {
            record("context_vtable_rejected", ",\"slot_rva\":%u", entry.slot);
            return false;
        }
    }
    record("context_bindings_checked", ",\"code_prefixes\":%u,\"virtual_slots\":3", unsigned(_countof(code)));
    return true;
}

bool checkpoint_idle(bool diagnostic_second_player, bool restoring_second_player, bool allow_foreign) {
    // The experimental nest graph has not qualified save/transfer recovery.
    if (nest_probe_epoch == commands.epoch() || (!allow_foreign && foreign_setup_epoch == commands.epoch())) {
        return false;
    }
    if (!native_death_checkpoint_idle()) {
        return false;
    }
    if (commands.pending_count() || executing || setup_or_scope_busy()) {
        return false;
    }
    if (selection_tracked_busy()) {
        return false;
    }
    // Reward context requires this separate player. Also count native players
    // below: a saved but not yet adopted second player is outside this slice.
    if (restoring_second_player) {
        return native_second_player(commands.epoch()) != nullptr;
    }
    return diagnostic_second_player ? native_awards_checkpoint_idle(commands.epoch(), allow_foreign)
                                    : !native_second_player(commands.epoch());
}

void checkpoint_actor_event(const NativeActorFingerprint& actor, uint32_t owner, uint64_t request) {
    record("checkpoint_actor",
           ",\"request\":%llu,\"owner\":%u,\"native_id\":%u,\"herd_native_id\":%u,"
           "\"species_instance\":%u,\"species_type\":%u,\"species_group\":%u,\"archetype\":%u",
           request, owner, actor.native_id, actor.herd_native_id, actor.species_instance, actor.species_type,
           actor.species_group, actor.archetype);
}

bool fingerprint_equal(const NativeActorFingerprint& a, const NativeActorFingerprint& b) {
    return a.native_id == b.native_id && a.herd_native_id == b.herd_native_id &&
           a.species_instance == b.species_instance && a.species_type == b.species_type &&
           a.species_group == b.species_group && a.archetype == b.archetype;
}

// `candidate` when it is in the noun list, present and of this type; otherwise null. The pointer is only compared
// until the enumeration has found it.
Noun* listed_noun(Manager* manager, Noun* candidate, uint32_t noun_type) {
    for (auto& noun : manager->mNouns) {
        if (&noun == candidate) {
            if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != noun_type) {
                return nullptr;
            }
            return &noun;
        }
    }
    return nullptr;
}

size_t count_nouns_numbered(Manager* manager, uint32_t native_id) {
    size_t count = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.mID == native_id) {
            ++count;
        }
    }
    return count;
}

// Every address is obtained from this enumeration or checked against it before
// dereferencing. Native IDs are checkpoint-local matching candidates only.
bool checkpoint_fingerprint(Animal* actor, NativeActorFingerprint& fingerprint) {
    auto manager = Manager::Get();
    if (!manager || !actor) {
        return false;
    }
    Noun* actor_noun = listed_noun(manager, static_cast<Noun*>(actor), Animal::NOUN_ID);
    if (!actor_noun || object_cast<Animal>(actor_noun) != actor || actor->mbDead || actor->mbMarkedForDeletion ||
        !actor->field_E84 || !actor->mpSpeciesProfile || !actor->mHerd || actor->mID == UINT32_MAX) {
        return false;
    }
    auto herd = actor->mHerd.get();
    const size_t actor_ids = count_nouns_numbered(manager, actor->mID);
    const Noun* herd_noun = listed_noun(manager, static_cast<Noun*>(herd), Simulator::cHerd::NOUN_ID);
    if (!herd_noun || herd_noun->mID == UINT32_MAX || actor_ids != 1) {
        return false;
    }
    if (count_nouns_numbered(manager, herd->mID) != 1) {
        return false;
    }
    size_t memberships = 0;
    for (const auto& member : herd->mHerd) {
        if (member.get() == actor) {
            ++memberships;
        }
    }
    if (memberships != 1) {
        return false;
    }
    fingerprint = {actor->mID,
                   herd->mID,
                   actor->mSpeciesKey.instanceID,
                   actor->mSpeciesKey.typeID,
                   actor->mSpeciesKey.groupID,
                   static_cast<uint32_t>(actor->mArchetype)};
    return true;
}

bool checkpoint_census(const NativeActorCheckpoint* expected, std::array<Animal*, 2>& candidates, uint64_t request,
                       bool diagnostic_second_player = false) {
    auto manager = Manager::Get();
    size_t animal_count = 0, herd_count = 0, player_count = 0;
    std::array<size_t, 2> id_matches{};
    bool first_player_present = false, second_player_present = false;
    auto first_player = manager->GetPlayer();
    for (auto& noun : manager->mNouns) {
        const auto type = noun.GetNounID();
        if (!noun.mbIsDestroyed && !noun.field_20) {
            if (type == Animal::NOUN_ID) {
                ++animal_count;
            }
            if (type == Simulator::cHerd::NOUN_ID) {
                ++herd_count;
            }
            // Installed noun ID, independently qualified for M03. SDK cPlayer
            // TYPE differs from this executable and is deliberately unused.
            if (type == native_player_noun_id) {
                ++player_count;
                if (&noun == static_cast<Noun*>(first_player)) {
                    first_player_present = true;
                }
                if (&noun == static_cast<Noun*>(native_second_player(commands.epoch()))) {
                    second_player_present = true;
                }
            }
        }
        if (expected) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (noun.mID == expected->actors[i].native_id) {
                    ++id_matches[i];
                    const bool available = !noun.mbIsDestroyed && !noun.field_20 && type == Animal::NOUN_ID;
                    if (available) {
                        candidates[i] = object_cast<Animal>(&noun);
                    }
                    record("checkpoint_candidate",
                           ",\"request\":%llu,\"owner\":%u,\"native_id\":%u,"
                           "\"noun_type\":%u,\"available\":%s",
                           request, static_cast<unsigned>(i + 1), noun.mID, type, available ? "true" : "false");
                }
            }
        }
    }
    record("checkpoint_census",
           ",\"request\":%llu,\"animals\":%u,\"herds\":%u,\"players\":%u,"
           "\"a_id_matches\":%u,\"b_id_matches\":%u,\"native_player_present\":%s",
           request, static_cast<unsigned>(animal_count), static_cast<unsigned>(herd_count),
           static_cast<unsigned>(player_count), static_cast<unsigned>(id_matches[0]),
           static_cast<unsigned>(id_matches[1]), first_player_present ? "true" : "false");
    return player_count == (diagnostic_second_player ? 2u : 1u) && first_player_present &&
           (!diagnostic_second_player || second_player_present) &&
           (!expected || (id_matches[0] == 1 && id_matches[1] == 1 && candidates[0] && candidates[1]));
}
} // namespace

void native_actor_worker_event(const char* event, const char* json_fields) {
    if (!on_thread() || !event || !json_fields) {
        return;
    }
    record(event, "%s", json_fields);
    flush();
}

bool snapshot_native_actor_checkpoint(NativeActorCheckpoint& checkpoint, uint64_t request,
                                      bool diagnostic_second_player) {
    checkpoint = {};
    if (!on_thread()) {
        return false;
    }
    const char* reason = "scene_unavailable";
    NativeActorCheckpoint capture;
    bool valid = false;
    do {
        if (Simulator::GetGameModeID() != kGameCreature || !Manager::Get()) {
            break;
        }
        reason = "pending_actions_or_player_context";
        if (!checkpoint_idle(diagnostic_second_player, false, diagnostic_second_player)) {
            break;
        }
        reason = "actor_pair_unavailable";
        if (diagnostic_second_player ? commands.live_count() < 2 : commands.live_count() != 2) {
            break;
        }
        auto first = resolve(owned(1));
        auto second = resolve(owned(2));
        if (!first || !second || first == second || first != Manager::Get()->GetAvatar()) {
            break;
        }
        std::array<Animal*, 2> ignored{};
        reason = "additional_native_player";
        if (!checkpoint_census(nullptr, ignored, request, diagnostic_second_player)) {
            break;
        }
        reason = "native_fingerprint_unavailable";
        if (!checkpoint_fingerprint(first, capture.actors[0]) || !checkpoint_fingerprint(second, capture.actors[1])) {
            break;
        }
        capture.epoch = commands.epoch();
        checkpoint_actor_event(capture.actors[0], 1, request);
        checkpoint_actor_event(capture.actors[1], 2, request);
        valid = true;
        reason = "captured";
    } while (false);
    // A diagnostic second-player snapshot must never be accepted by the M04
    // sealing tool as a complete, restorable actor-only checkpoint.
    record(diagnostic_second_player ? "checkpoint_probe_snapshot" : "checkpoint_snapshot",
           ",\"request\":%llu,\"valid\":%s,\"reason\":\"%s\"", request, valid ? "true" : "false", reason);
    flush();
    if (failed) {
        return false;
    }
    if (valid) {
        checkpoint = capture;
    }
    return valid;
}

namespace {
// Why this scene cannot take a checkpoint's two actors, or null once `candidates` holds the nouns that match them.
const char* checkpoint_restore_refusal(const NativeActorCheckpoint& checkpoint, uint64_t request,
                                       bool restored_second_player, std::array<Animal*, 2>& candidates) {
    if (Simulator::GetGameModeID() != kGameCreature || !Manager::Get()) {
        return "scene_unavailable";
    }
    if (checkpoint.epoch != commands.epoch()) {
        return "stale_epoch";
    }
    if (commands.live_count() || !checkpoint_idle(false, restored_second_player)) {
        return "existing_bindings_or_pending_context";
    }
    if (checkpoint.actors[0].native_id == UINT32_MAX || checkpoint.actors[1].native_id == UINT32_MAX ||
        checkpoint.actors[0].native_id == checkpoint.actors[1].native_id) {
        return "invalid_native_ids";
    }
    if (!checkpoint_census(&checkpoint, candidates, request, restored_second_player)) {
        return "native_census_mismatch";
    }
    if (candidates[0] != Manager::Get()->GetAvatar() || candidates[0] == candidates[1]) {
        return "native_avatar_mismatch";
    }
    NativeActorFingerprint observed_first, observed_second;
    if (!checkpoint_fingerprint(candidates[0], observed_first) ||
        !checkpoint_fingerprint(candidates[1], observed_second) ||
        !fingerprint_equal(checkpoint.actors[0], observed_first) ||
        !fingerprint_equal(checkpoint.actors[1], observed_second)) {
        return "native_fingerprint_mismatch";
    }
    return nullptr;
}

void invalidate_bindings(const std::array<Animal*, 2>& candidates) {
    commands.invalidate(key(candidates[0]));
    commands.invalidate(key(candidates[1]));
}
} // namespace

bool restore_native_actor_checkpoint(const NativeActorCheckpoint& checkpoint, uint64_t request,
                                     bool restored_second_player) {
    if (!on_thread()) {
        return false;
    }
    std::array<Animal*, 2> candidates{};
    const char* reason = checkpoint_restore_refusal(checkpoint, request, restored_second_player, candidates);
    bool valid = false;
    if (!reason) {
        // Both slots are free after the no-live-bindings check. These two pure
        // table operations contain no native callback: publish both identities
        // before the existing bind helper emits any native diagnostics.
        auto first = commands.bind(key(candidates[0]), 1);
        auto second = commands.bind(key(candidates[1]), 2);
        if (!first.live || !second.live) {
            invalidate_bindings(candidates);
            reason = "binding_capacity";
        } else {
            bind(candidates[0], 1);
            bind(candidates[1], 2);
            if (failed) {
                invalidate_bindings(candidates);
                return false;
            }
            valid = true;
            reason = "adopted_existing_nouns";
        }
    }
    record("checkpoint_restore",
           ",\"request\":%llu,\"valid\":%s,\"reason\":\"%s\","
           "\"actor_a\":%llu,\"actor_b\":%llu,\"created_nouns\":0",
           request, valid ? "true" : "false", reason, owned(1), owned(2));
    flush();
    return valid && !failed;
}

bool restore_native_actor_awards(const NativeAwardCheckpoint& checkpoint, uint64_t epoch, bool foreign_checkpoint) {
    if (!on_thread() || epoch != commands.epoch() ||
        !restore_native_awards(resolve(owned(2)), owned(2), epoch, checkpoint, foreign_checkpoint)) {
        return false;
    }
    if (foreign_checkpoint) {
        foreign_setup_actor = owned(2);
        foreign_setup_epoch = epoch;
    }
    return true;
}

worker::Message native_actor_worker_status() {
    worker::Message value;
    // A trace error disables unsafe native observation, not the progress already
    // counted on this engine thread. Preserve it in the explicit failed status.
    if (GetCurrentThreadId() == engine_thread) {
        value.epoch = commands.epoch();
        value.values[2] = worker_ai_entries;
    }
    if (!on_thread()) {
        value.values[0] = static_cast<uint64_t>(worker::Phase::failed);
        return value;
    }
    const auto mode = Simulator::GetGameModeID();
    value.epoch = commands.epoch();
    value.values[0] = static_cast<uint64_t>(mode == kGameCreature && Manager::Get() && Manager::Get()->GetAvatar()
                                                ? worker::Phase::scene
                                            : Simulator::IsLoadingGameMode() ? worker::Phase::loading
                                                                             : worker::Phase::menu);
    value.values[2] = worker_ai_entries;
    value.values[3] = owned(1);
    value.values[4] = owned(2);
    value.values[5] = mode;
    return value;
}

bool native_actor_read_vitals(uint64_t local_id, replica::Vitals& value) {
    if (!on_thread()) {
        return false;
    }
    auto actor = resolve(local_id);
    if (!actor || actor != Manager::Get()->GetAvatar() || actor->mbDead || actor->mbMarkedForDeletion ||
        !checkpoint_idle()) {
        return false;
    }
    value = {actor->mHealthPoints, actor->mEnergy, actor->mHunger, Simulator::cCreatureGameData::GetEvolutionPoints()};
    return replica::valid(value);
}

bool native_actor_import_probe_ready() {
    wchar_t network[2]{};
    return on_thread() && Simulator::GetGameModeID() == kGameCreature && !Simulator::IsLoadingGameMode() &&
           !native_replica_is_client() && !GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", network, _countof(network)) &&
           !commands.live_count() && !setup_pending && !players_pending && !rewards_pending;
}

static bool transfer_primary_bindings_ready() {
    // Completed targeted intentions retain their original owner-0 NPC binding.
    // Count controlled slots separately, preserving those targets. The import
    // still needs an actual free bridge slot before any native object is read.
    size_t live = 0, primary = 0;
    for (const auto& slot : tracked) {
        const auto binding = commands.find(slot.id);
        if (!binding.live) {
            continue;
        }
        ++live;
        if (binding.owner == 1) {
            ++primary;
        } else if (binding.owner != 0) {
            return false;
        }
    }
    return primary == 1 && live == commands.live_count() && live < ActorCommands::capacity;
}

static bool adopt_imported_actor(uint64_t epoch, uint64_t request, uint32_t player_native_id, uint32_t avatar_native_id,
                                 uint32_t herd_native_id, bool preserve_primary) {
    if (epoch != commands.epoch() || !on_thread()) {
        return false;
    }
    if (!preserve_primary && !native_actor_import_probe_ready()) {
        return false;
    }
    if (preserve_primary && (commands.inputs_ready() || !transfer_primary_bindings_ready() || !owned(1) || owned(2) ||
                             !checkpoint_idle(false, true))) {
        return false;
    }
    auto manager = Manager::Get();
    auto second = native_second_player(epoch);
    auto primary = manager ? manager->GetAvatar() : nullptr;
    if (!manager || !second || !primary || !manager->GetPlayer() || second == manager->GetPlayer() ||
        second->mID != player_native_id || primary->mID == avatar_native_id) {
        return false;
    }
    Animal* imported = nullptr;
    unsigned matches = 0, players = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.GetNounID() == native_player_noun_id) {
            ++players;
        }
        if (noun.mID == avatar_native_id) {
            ++matches;
            imported = object_cast<Animal>(&noun);
        }
    }
    if (matches != 1 || players != 2 || !imported || imported->mbDead || imported->mbMarkedForDeletion ||
        imported->mbIsDestroyed || !imported->mHerd || imported->mHerd->mID != herd_native_id ||
        imported->mHerd == primary->mHerd || imported->mHerd->mHerd.size() != 1 ||
        imported->mHerd->mHerd.front().get() != imported || !imported->field_E84 ||
        (imported->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar)) {
        return false;
    }
    if (preserve_primary && resolve(owned(1)) != primary) {
        return false;
    }
    const auto primary_binding = preserve_primary ? commands.find(owned(1)) : bind(primary, 1),
               imported_binding = bind(imported, 2);
    if (!primary_binding.live || !imported_binding.live || primary_binding.id == imported_binding.id) {
        return false;
    }
    record("native_player_import_actor_adoption",
           ",\"request\":%llu,\"primary_actor\":%llu,\"imported_actor\":%llu,"
           "\"player_native_id\":%u,\"avatar_native_id\":%u,\"herd_native_id\":%u,"
           "\"movement_diagnostics_only\":true,\"award_context_ready\":false,\"transfer_qualified\":false",
           request, primary_binding.id, imported_binding.id, player_native_id, avatar_native_id, herd_native_id);
    observe_native_player_checkpoint(request, "import_after");
    flush();
    return true;
}

bool native_actor_adopt_import_probe(uint64_t epoch, uint64_t request, uint32_t player_native_id,
                                     uint32_t avatar_native_id, uint32_t herd_native_id) {
    return adopt_imported_actor(epoch, request, player_native_id, avatar_native_id, herd_native_id, false);
}

bool native_actor_transfer_import_ready(uint64_t epoch, uint32_t primary_player, uint32_t primary_avatar,
                                        uint32_t primary_unique_game) {
    if (!on_thread() || epoch != commands.epoch() || commands.inputs_ready() || !transfer_primary_bindings_ready() ||
        !owned(1) || owned(2) || !checkpoint_idle() || Simulator::GetGameModeID() != kGameCreature ||
        Simulator::IsLoadingGameMode() || native_replica_is_client()) {
        return false;
    }
    auto manager = Manager::Get();
    auto player = manager ? manager->GetPlayer() : nullptr;
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    unsigned players = 0;
    if (!manager) {
        return false;
    }
    for (auto& noun : manager->mNouns) {
        if (noun.GetNounID() == native_player_noun_id) {
            if (&noun != static_cast<Noun*>(player)) {
                return false;
            }
            ++players;
        }
    }
    return players == 1 && player && avatar && resolve(owned(1)) == avatar && player->mID == primary_player &&
           avatar->mID == primary_avatar && player->mUniqueGameID == primary_unique_game &&
           validate_native_player_checkpoint(player, primary_player) && !avatar->mbDead &&
           !avatar->mbMarkedForDeletion && !avatar->mbIsDestroyed;
}

bool native_actor_adopt_transfer_import(uint64_t epoch, uint64_t request, uint32_t primary_player,
                                        uint32_t primary_avatar, uint32_t primary_unique_game, uint32_t imported_player,
                                        uint32_t imported_avatar, uint32_t imported_herd) {
    if (!on_thread() || epoch != commands.epoch() || !Manager::Get()) {
        return false;
    }
    auto first = Manager::Get()->GetPlayer();
    auto avatar = Manager::Get()->GetAvatar();
    if (!first || !avatar || first->mID != primary_player || avatar->mID != primary_avatar ||
        first->mUniqueGameID != primary_unique_game) {
        return false;
    }
    return adopt_imported_actor(epoch, request, imported_player, imported_avatar, imported_herd, true);
}

bool native_actor_owner_native_id(uint32_t owner, uint32_t& native_id) {
    if (!on_thread() || (owner != 1 && owner != 2)) {
        return false;
    }
    auto actor = resolve(owned(owner));
    if (!actor) {
        return false;
    }
    native_id = actor->mID;
    return true;
}

bool native_actor_transfer_selection(uint64_t epoch, uint32_t native_slot, uint32_t expected_unique_game,
                                     uint32_t& player_native_id, uint32_t& avatar_native_id) {
    if (!on_thread() || epoch != commands.epoch() || (native_slot != 1 && native_slot != 2) || !expected_unique_game ||
        !Manager::Get() || Simulator::IsLoadingGameMode()) {
        return false;
    }
    auto manager = Manager::Get();
    auto secondary = native_second_player(epoch);
    if (!checkpoint_idle(secondary != nullptr)) {
        return false;
    }
    auto actor = resolve(owned(native_slot));
    auto player = native_slot == 1 ? manager->GetPlayer() : secondary;
    if (!actor || !player || (native_slot == 1) != (actor == manager->GetAvatar()) ||
        player->mUniqueGameID != expected_unique_game || !validate_native_player_checkpoint(player, player->mID) ||
        actor->mbDead || actor->mbMarkedForDeletion || actor->mbIsDestroyed || !actor->mHerd ||
        !actor->mpSpeciesProfile || !actor->field_E84) {
        return false;
    }
    unsigned player_matches = 0, actor_matches = 0, players = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.mID == player->mID) {
            if (&noun != static_cast<Noun*>(player)) {
                return false;
            }
            ++player_matches;
        }
        if (noun.mID == actor->mID) {
            if (&noun != static_cast<Noun*>(actor)) {
                return false;
            }
            ++actor_matches;
        }
        if (noun.GetNounID() == native_player_noun_id) {
            if (&noun != static_cast<Noun*>(manager->GetPlayer()) && &noun != static_cast<Noun*>(secondary)) {
                return false;
            }
            ++players;
        }
    }
    if (player_matches != 1 || actor_matches != 1 || players != (secondary ? 2u : 1u)) {
        return false;
    }
    player_native_id = player->mID;
    avatar_native_id = actor->mID;
    return true;
}

namespace {
// C083D0 (primary virtual RemoveOwner slot44) removes ESI from its herd's
// member vector at C08456..C08468 and releases/clears mHerd at C0846B..C08484.
bool remove_owner_binding_verified(Animal* animal) {
    const auto animal_table = *reinterpret_cast<const uintptr_t* const*>(animal);
    const unsigned char remove_prefix[] = {0x51, 0x56, 0x8b, 0xf1, 0x56, 0xe8, 0x26, 0x51, 0, 0};
    return reinterpret_cast<uintptr_t>(animal_table) == executable_base + 0x106a080 &&
           animal_table[0x44 / 4] == executable_base + 0x8083d0 &&
           !memcmp(reinterpret_cast<void*>(executable_base + 0x8083d0), remove_prefix, sizeof(remove_prefix));
}

std::vector<std::array<uint32_t, 3>> item_statuses(Simulator::cCollectableItems* items) {
    std::vector<std::array<uint32_t, 3>> values;
    for (const auto& item : items->mItemStatusInfos) {
        values.push_back({item.first.instanceID, item.first.groupID, item.second});
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<std::array<uint32_t, 2>> item_unlocks(Simulator::cCollectableItems* items) {
    std::vector<std::array<uint32_t, 2>> values;
    for (const auto& item : items->mUnlockedItems) {
        values.push_back({item.instanceID, item.groupID});
    }
    return values;
}

std::array<float, 4> goal_progress(Simulator::cPlayer* player) {
    return {player->mCurrentGoalProgress, player->mGoalProgressTotal, player->mSocialTraitProgress,
            player->mCombatTraitProgress};
}

using CreatureStageBytes = std::array<unsigned char, sizeof(Simulator::cCreatureGameData)>;

CreatureStageBytes creature_stage_bytes() {
    CreatureStageBytes bytes{};
    memcpy(bytes.data(), Simulator::cCreatureGameData::Get(), bytes.size());
    return bytes;
}

// The first player's stage data, nouns, binding and earnings as they are before the second player's nouns are
// removed. Removing them must change none of it.
struct PrimaryState {
    CreatureStageBytes stage;
    uint64_t binding;
    Simulator::cPlayer* player;
    Animal* avatar;
    uint32_t game;
    std::array<float, 4> progress;
    Simulator::cCollectableItems* items;
    int points;
    std::vector<std::array<uint32_t, 3>> statuses;
    std::vector<std::array<uint32_t, 2>> unlocks;

    PrimaryState(Simulator::cPlayer* primary, Animal* primary_avatar)
        : stage(creature_stage_bytes()),
          binding(owned(1)),
          player(primary),
          avatar(primary_avatar),
          game(primary->mUniqueGameID),
          progress(goal_progress(primary)),
          items(primary->mpCRGItems.get()),
          points(items->mUnlockPoints),
          statuses(item_statuses(items)),
          unlocks(item_unlocks(items)) {}

    bool changed(Manager* manager) const {
        return manager->GetPlayer() != player || manager->GetAvatar() != avatar || owned(1) != binding ||
               resolve(binding) != avatar || player->mUniqueGameID != game || player->mpCRGItems.get() != items ||
               items->mUnlockPoints != points || item_statuses(items) != statuses || item_unlocks(items) != unlocks ||
               progress != goal_progress(player) ||
               memcmp(stage.data(), Simulator::cCreatureGameData::Get(), stage.size());
    }
};

// Records why a retirement stopped after native nouns had been removed, and ends the process.
void fail_partial_retirement(uint64_t request, const char* reason) {
    record("native_transfer_retire_partial_failure", ",\"request\":%llu,\"reason\":\"%s\"", request, reason);
    flush();
    TerminateProcess(GetCurrentProcess(), 0x4d10);
    __fastfail(7);
}
} // namespace

bool native_actor_retire_transfer_secondary(uint64_t request, uint64_t epoch, uint64_t pause_request,
                                            uint32_t expected_unique_game, uint32_t player_native_id,
                                            uint32_t avatar_native_id, uint32_t herd_native_id) {
    const auto pause = native_quiescence_probe_status(epoch);
    uint32_t selected_player = 0, selected_avatar = 0;
    if (!on_thread() || !request || !pause_request || !pause.ready || !pause.owns_pause ||
        pause.request != pause_request || commands.inputs_ready() ||
        !native_actor_transfer_selection(epoch, 2, expected_unique_game, selected_player, selected_avatar) ||
        selected_player != player_native_id || selected_avatar != avatar_native_id) {
        return false;
    }
    auto manager = Manager::Get();
    auto primary = manager->GetPlayer();
    auto primary_avatar = manager->GetAvatar();
    auto secondary = native_second_player(epoch);
    cCreatureAnimalPtr animal = resolve(owned(2));
    cHerdPtr herd = animal ? animal->mHerd : nullptr;
    if (!secondary || !animal || !herd || herd->mID != herd_native_id || herd == primary_avatar->mHerd ||
        herd->mOwnedByAvatar || herd->mpNest || !herd->mEggs.empty() || herd->mpHerdMom || herd->mpEggLayer ||
        herd->mHerd.size() != 1 || herd->mHerd.front().get() != animal.get()) {
        return false;
    }
    if (!remove_owner_binding_verified(animal.get())) {
        return false;
    }
    std::set<uint32_t> expected;
    if (!unique_noun_ids(manager, expected) || expected.erase(player_native_id) != 1 ||
        expected.erase(avatar_native_id) != 1 || expected.erase(herd_native_id) != 1) {
        return false;
    }
    const PrimaryState before(primary, primary_avatar);
    if (!observe_native_player_checkpoint(request, "retire_before")) {
        return false;
    }
    retire_native_awards();
    manager->DestroyInstance(animal.get());
    if (!animal->mbIsDestroyed || animal->mHerd || !herd->mHerd.empty() || owned(2)) {
        fail_partial_retirement(request, "native_avatar_removal");
    }
    retire_native_player_context("committed_secondary_transfer");
    if (native_second_player(epoch)) {
        fail_partial_retirement(request, "native_player_removal");
    }
    manager->DestroyInstance(herd.get());
    std::set<uint32_t> after;
    if (!unique_noun_ids(manager, after)) {
        fail_partial_retirement(request, "duplicate_after_removal");
    }
    if (after != expected || !herd->mbIsDestroyed || before.changed(manager)) {
        fail_partial_retirement(request, "primary_or_census_changed");
    }
    if (!observe_native_player_checkpoint(request, "retire_after")) {
        fail_partial_retirement(request, "retire_final_census");
    }
    if (!reset_native_player_transfer_after_retirement(epoch, player_native_id, avatar_native_id, herd_native_id)) {
        fail_partial_retirement(request, "retire_import_use");
    }
    record("native_transfer_secondary_retired",
           ",\"request\":%llu,\"epoch\":%llu,\"player_native_id\":%u,"
           "\"avatar_native_id\":%u,\"herd_native_id\":%u,\"removed_nouns\":3,\"primary_binding_unchanged\":true,"
           "\"primary_stage_unchanged\":true,\"native_players\":1,\"experimental\":true,\"transfer_qualified\":false",
           request, epoch, player_native_id, avatar_native_id, herd_native_id);
    flush();
    return true;
}

bool native_actor_experimental_foreign_ready(uint64_t epoch) {
    if (!on_thread() || native_replica_is_client() || epoch != commands.epoch()) {
        return false;
    }
    const auto id = owned(2);
    auto first = resolve(owned(1)), second = resolve(id);
    return experimental_foreign_actor(id) && first && second && first == Manager::Get()->GetAvatar() &&
           !first->mbDead && !second->mbDead && first->mpSpeciesProfile != second->mpSpeciesProfile &&
           first->mSpeciesKey != second->mSpeciesKey;
}

bool native_actor_network_prepare_rewards() {
    if (!on_thread() || native_replica_is_client() || Simulator::GetGameModeID() != kGameCreature) {
        return false;
    }
    const auto id = owned(2);
    if (!id) {
        // In a one-primary owner-1 location the original SPORE player already
        // owns rewards; no second-player award context is needed.
        auto primary = resolve(owned(1));
        return primary && primary == Manager::Get()->GetAvatar() && !primary->mbDead;
    }
    auto actor = resolve(id);
    if (!actor || actor->mbDead) {
        return false;
    }
    // A one-avatar location may lease owner 2 to SPORE's original primary
    // player. Its rewards already use the original primary progression path.
    if (actor == Manager::Get()->GetAvatar()) {
        return true;
    }
    if (native_awards_ready(id, commands.epoch())) {
        return ensure_native_network_player_controller(actor, actor->mID, id, commands.epoch(),
                                                       experimental_foreign_actor(id));
    }
    if (!native_second_player(commands.epoch()) && !create_native_player_context(commands.epoch())) {
        return false;
    }
    actor = resolve(id);
    if (!actor || actor->mbDead) {
        return false;
    }
    const bool ready = enable_native_awards(actor, id, commands.epoch(), experimental_foreign_actor(id)) &&
                       ensure_native_network_player_controller(actor, actor->mID, id, commands.epoch(),
                                                               experimental_foreign_actor(id));
    record("network_reward_context", ",\"owner\":2,\"actor\":%llu,\"ready\":%s", id, ready ? "true" : "false");
    return ready;
}

bool native_actor_owner_dna(uint32_t owner, float& dna, bool* current_native) {
    if (current_native) {
        *current_native = false;
    }
    if (!on_thread() || Simulator::GetGameModeID() != kGameCreature || (owner != 1 && owner != 2)) {
        return false;
    }
    auto actor = resolve(owned(owner));
    if (!actor) {
        return false;
    }
    if (actor != Manager::Get()->GetAvatar()) {
        return owner == 2 && native_awards_read_dna(owned(2), commands.epoch(), dna, actor->mbDead, current_native);
    }
    const float value = Simulator::cCreatureGameData::GetEvolutionPoints();
    if (!std::isfinite(value) || value < 0) {
        return false;
    }
    dna = value;
    if (current_native) {
        *current_native = true;
    }
    return true;
}

namespace {
// Whether the words of a network command fit its verb. The actor and the target are not looked at here.
worker::Result check_network_command_words(uint32_t owner, ActorVerb verb, int direction, uint32_t target_native_id,
                                           uint64_t resource_id, uint64_t resource_generation) {
    using Result = worker::Result;
    const bool targeted = targets_creature(verb);
    const bool collect = verb == ActorVerb::collect;
    if (!targeted && !collect && verb != ActorVerb::move && verb != ActorVerb::steer && verb != ActorVerb::steer_stop &&
        verb != ActorVerb::jump && verb != ActorVerb::stop && verb != ActorVerb::forage) {
        return Result::invalid;
    }
    if (verb == ActorVerb::forage && direction <= 0) {
        return Result::invalid;
    }
    if (collect && owner == 2 && !native_awards_ready(owned(2), commands.epoch())) {
        return Result::unavailable;
    }
    // An intention to eat a fruit carries the place the player saw it at in the resource word, and no incarnation.
    if (verb == ActorVerb::forage ? (!resource_id || resource_generation)
        : collect                 ? (!resource_id || !resource_generation || direction)
                                  : (resource_id || resource_generation)) {
        return Result::invalid;
    }
    if (verb == ActorVerb::steer_stop && direction) {
        return Result::invalid;
    }
    if (verb == ActorVerb::move && (direction < 0 || direction > 3)) {
        return Result::invalid;
    }
    network::SteeringVector steering;
    if (verb == ActorVerb::steer && !network::unpack_steering(direction, steering)) {
        return Result::invalid;
    }
    // Primary tutorial/relationship ownership is not yet adapted. Do not
    // expose a new unqualified primary result through this measured B path.
    if (verb == ActorVerb::social && (direction < -1 || direction >= 88)) {
        return Result::invalid;
    }
    if (((targeted || collect) && target_native_id == UINT32_MAX) ||
        (!targeted && !collect && target_native_id != UINT32_MAX)) {
        return Result::invalid;
    }
    return Result::accepted;
}

// The creature noun with this number, or null when no noun has it. False when two nouns have it, or the noun that
// has it fails the presence test (destroyed, field_20) or is not a creature.
bool unique_animal_noun(uint32_t native_id, Animal*& animal) {
    animal = nullptr;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mID == native_id) {
            if (animal || noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) {
                return false;
            }
            animal = object_cast<Animal>(&noun);
        }
    }
    return true;
}

// The bridge actor of the creature a network command targets, into `target_id`.
worker::Result bind_network_target(ActorVerb verb, uint32_t target_native_id, Animal* actor, uint64_t& target_id) {
    using Result = worker::Result;
    Animal* target = nullptr;
    // A global/native mapping from a previous frame is insufficient. Resolve
    // one current noun and register its process-local lifetime afresh.
    if (!unique_animal_noun(target_native_id, target)) {
        return Result::invalid;
    }
    if (!target || target == actor ||
        (verb == ActorVerb::pickup ? !target->mbDead || target->mFoodValue <= 0
                                   : target->mbDead || target->mHealthPoints <= 0) ||
        target->mbMarkedForDeletion || !target->field_E84 || !target->mpSpeciesProfile || !target->mHerd) {
        return Result::invalid;
    }
    auto target_binding = identity(target);
    // M07's shared-NPC fixture does not enable PvP or player-owned targets.
    if (target_binding.live && target_binding.owner) {
        return Result::invalid;
    }
    if (!target_binding.live) {
        target_binding = bind(target, 0, verb == ActorVerb::pickup);
    }
    if (!target_binding.live) {
        return Result::busy;
    }
    target_id = target_binding.id;
    return Result::accepted;
}

// The actor command a network command stands for, before the creature it targets is bound.
ActorCommand network_actor_command(uint64_t actor_id, uint32_t owner, ActorVerb verb, int direction,
                                   uint32_t target_native_id, uint64_t resource_id, uint64_t resource_generation) {
    ActorCommand command;
    command.actor = actor_id;
    command.owner = owner;
    command.verb = verb;
    command.direction = direction;
    if (verb == ActorVerb::collect) {
        command.target = resource_id;
        command.target_generation = resource_generation;
        command.target_native_id = target_native_id;
    }
    // No entity is this command's target; its generation word holds the seen place until the order is given.
    if (verb == ActorVerb::forage) {
        command.target_generation = resource_id;
    }
    return command;
}
} // namespace

worker::Result native_actor_network_command(uint64_t epoch, uint32_t owner, uint32_t actor_native_id,
                                            uint32_t target_native_id, ActorVerb verb, int direction, uint64_t request,
                                            uint64_t resource_id, uint64_t resource_generation) {
    using Result = worker::Result;
    if (!on_thread() || native_replica_is_client() || Simulator::GetGameModeID() != kGameCreature) {
        return Result::unavailable;
    }
    if (!native_network_authority_inputs_ready() || !commands.inputs_ready()) {
        return Result::unavailable;
    }
    if (epoch != commands.epoch()) {
        return Result::stale;
    }
    if (!request || (owner != 1 && owner != 2) || actor_native_id == UINT32_MAX) {
        return Result::invalid;
    }
    const Result words =
        check_network_command_words(owner, verb, direction, target_native_id, resource_id, resource_generation);
    if (words != Result::accepted) {
        return words;
    }
    const auto actor_id = owned(owner);
    auto actor = resolve(actor_id);
    if (!actor || actor->mbDead || actor->mHealthPoints <= 0 || actor->mID != actor_native_id) {
        return Result::invalid;
    }
    if (verb == ActorVerb::combat &&
        (direction < 0 || direction >= 88 || !native_combat_ability(actor, static_cast<uint32_t>(direction)))) {
        return Result::invalid;
    }
    ActorCommand command =
        network_actor_command(actor_id, owner, verb, direction, target_native_id, resource_id, resource_generation);
    const bool targeted = targets_creature(verb);
    if (targeted) {
        const Result bound = bind_network_target(verb, target_native_id, actor, command.target);
        if (bound != Result::accepted) {
            return bound;
        }
    }
    if (!foreign_action_admitted(command, actor, targeted ? resolve(command.target) : nullptr)) {
        record("network_actor_command_refused",
               ",\"request\":%llu,\"owner\":%u,\"verb\":\"%s\",\"target_native_id\":%u,\"reason\":\"%s\"", request,
               owner, actor_verb(verb), target_native_id, foreign_refusal);
        return Result::unavailable;
    }
    const auto decision = commands.enqueue(command);
    record("network_actor_command_queued",
           ",\"request\":%llu,\"command\":%llu,\"actor\":%llu,\"actor_native_id\":%u,"
           "\"owner\":%u,\"verb\":\"%s\",\"target\":%llu,\"target_native_id\":%u,\"decision\":\"%s\"",
           request, command.sequence, command.actor, actor_native_id, owner, actor_verb(verb), command.target,
           target_native_id, actor_decision(decision));
    return queue_result(decision);
}

uint64_t native_actor_fence_network_inputs(const char* reason, bool stop_native) {
    if (!on_thread() || native_replica_is_client()) {
        return 0;
    }
    const bool first = commands.inputs_ready();
    const auto pending = commands.pending_count();
    const auto input_epoch = commands.fence_inputs();
    for (auto& detail : tracked) {
        cancel_intention(detail, reason ? reason : "network_inputs_fenced");
        detail.steering.clear();
    }
    setup_foreign_pending = false;
    setup_pending = opponents_pending = rewards_pending = false;
    players_pending = 0;
    duel_owner = 0;
    npc_source = npc_target = 0;
    fossil_probe = {};
    if (first) {
        bool targets_cleared = false, movement_stopped = false;
        uint32_t active_abilities = 0;
        if (stop_native && Simulator::GetGameModeID() == kGameCreature && !Simulator::IsLoadingGameMode()) {
            targets_cleared = movement_stopped = true;
            for (uint32_t owner = 1; owner <= 2; ++owner) {
                auto actor = resolve(owned(owner));
                if (!actor || actor->mbDead) {
                    continue;
                }
                const bool cleared = set_creature_target(actor, nullptr);
                targets_cleared = cleared && targets_cleared;
                actor = resolve(owned(owner));
                if (!actor) {
                    targets_cleared = false;
                    continue;
                }
                movement_stopped = stop_native_movement(actor) && movement_stopped;
                actor = resolve(owned(owner));
                if (!actor) {
                    targets_cleared = movement_stopped = false;
                    continue;
                }
                targets_cleared = !actor->GetTarget() && !actor->mpCombatantTarget && targets_cleared;
                for (size_t i = 0; i < 88; ++i) {
                    active_abilities += actor->mInUseAbilityBits.test(i) ? 1u : 0u;
                }
                state(actor, "network_input_fence_actor");
            }
        }
        record("native_actor_inputs_fenced",
               ",\"input_epoch\":%llu,\"cancelled_queued\":%zu,\"targets_cleared\":%s,\"movement_stop_calls_"
               "succeeded\":%s,\"active_ability_bits\":%u,\"native_world_idle_qualified\":false",
               input_epoch, pending, targets_cleared ? "true" : "false", movement_stopped ? "true" : "false",
               active_abilities);
        flush();
    }
    return input_epoch;
}

bool native_actor_resume_network_inputs(uint64_t input_epoch) {
    const char* failure = nullptr;
    if (native_relationship_failure_ready(commands.epoch(), failure)) {
        return false;
    }
    if (!on_thread() || native_replica_is_client() || !commands.resume_inputs(input_epoch)) {
        return false;
    }
    record("native_actor_inputs_resumed", ",\"input_epoch\":%llu", input_epoch);
    flush();
    return true;
}

uint32_t native_actor_pickup_owner(uint32_t corpse_native_id) {
    if (!on_thread() || native_replica_is_client()) {
        return 0;
    }
    for (const auto& grant : pickup_grants) {
        if (grant.epoch == commands.epoch() && grant.native_id == corpse_native_id) {
            auto corpse = current_pickup_animal(corpse_native_id, grant.corpse);
            if (corpse && corpse->mbHasBeenEaten) {
                return grant.owner;
            }
        }
    }
    return 0;
}

bool snapshot_native_pickup_checkpoint(NativePickupCheckpoint& checkpoint) {
    checkpoint = {};
    if (!on_thread() || pickup_scope) {
        return false;
    }
    size_t index = 0;
    for (const auto& grant : pickup_grants) {
        if (grant.epoch == commands.epoch()) {
            auto corpse = current_pickup_animal(grant.native_id, grant.corpse);
            if (!corpse || !corpse->mbHasBeenEaten || grant.owner < 1 || grant.owner > 2) {
                return false;
            }
            checkpoint.grants[index++] = {grant.native_id, grant.owner};
        }
    }
    return true;
}

bool restore_native_pickup_flags(const NativePickupCheckpoint& checkpoint) {
    // Original load retains the corpse and remaining food, but resets the
    // transient first-feed bit. Restore the observed saved bit before the
    // first gameplay tick, at the verified post-load player-cleanup callsite.
    if (!on_thread() || pickup_scope || !Manager::Get() ||
        native_persistence_snapshot().state != NativePersistenceState::loading) {
        return false;
    }
    std::array<Animal*, network::max_entities> candidates{};
    size_t count = 0;
    for (const auto& grant : checkpoint.grants) {
        if (!grant[0] && !grant[1]) {
            continue;
        }
        if (!grant[0] || grant[0] == UINT32_MAX || grant[1] < 1 || grant[1] > 2) {
            return false;
        }
        Animal* corpse = nullptr;
        if (!unique_animal_noun(grant[0], corpse)) {
            return false;
        }
        // Original feeding can consume more than the remaining food in its
        // final tick. A saved negative value is an exhausted corpse, not an
        // invalid reward grant. Keep the native value and restore only its
        // first-feed bit; new pickup orders still require positive food.
        if (!corpse || corpse->mbMarkedForDeletion || !corpse->mbDead || !std::isfinite(corpse->mFoodValue)) {
            return false;
        }
        for (size_t n = 0; n < count; ++n) {
            if (candidates[n] == corpse) {
                return false;
            }
        }
        candidates[count++] = corpse;
    }
    for (size_t n = 0; n < count; ++n) {
        auto corpse = candidates[n];
        const bool before = corpse->mbHasBeenEaten;
        corpse->mbHasBeenEaten = true;
        record("native_pickup_flag_restored",
               ",\"corpse_native_id\":%u,\"before\":%s,\"after\":true,\"food\":%.9g,\"replayed_rewards\":0",
               corpse->mID, before ? "true" : "false", double(corpse->mFoodValue));
    }
    return true;
}

bool restore_native_pickup_checkpoint(const NativePickupCheckpoint& checkpoint) {
    if (!on_thread() || pickup_scope) {
        return false;
    }
    auto candidate = pickup_grants;
    candidate = {};
    size_t index = 0;
    for (const auto& grant : checkpoint.grants) {
        if (!grant[0] && !grant[1]) {
            continue;
        }
        if (!grant[0] || grant[0] == UINT32_MAX || grant[1] < 1 || grant[1] > 2) {
            return false;
        }
        for (size_t n = 0; n < index; ++n) {
            if (candidate[n].native_id == grant[0]) {
                return false;
            }
        }
        auto corpse = current_pickup_animal(grant[0]);
        if (!corpse || !corpse->mbHasBeenEaten) {
            bool found = false;
            for (auto& noun : Manager::Get()->mNouns) {
                if (noun.mID == grant[0]) {
                    found = true;
                    auto animal = object_cast<Animal>(&noun);
                    record("native_pickup_checkpoint_rejected",
                           ",\"corpse_native_id\":%u,\"owner\":%u,\"present\":true,\"noun_type\":%u,\"destroyed\":%s,"
                           "\"pending_delete\":%s,\"animal\":%s,\"dead\":%s,\"fed_on\":%s,\"food\":%.9g",
                           grant[0], grant[1], noun.GetNounID(), noun.mbIsDestroyed ? "true" : "false",
                           noun.field_20 ? "true" : "false", animal ? "true" : "false",
                           animal && animal->mbDead ? "true" : "false",
                           animal && animal->mbHasBeenEaten ? "true" : "false",
                           double(animal ? animal->mFoodValue : 0));
                }
            }
            if (!found) {
                record("native_pickup_checkpoint_rejected", ",\"corpse_native_id\":%u,\"owner\":%u,\"present\":false",
                       grant[0], grant[1]);
            }
            return false;
        }
        candidate[index++] = {commands.epoch(), key(corpse), grant[0], grant[1]};
    }
    pickup_grants = candidate;
    record("native_pickup_grants_restored", ",\"count\":%u,\"replayed_rewards\":0", static_cast<unsigned>(index));
    return true;
}

bool native_actor_apply_vitals(uint64_t local_id, const replica::Vitals& value) {
    replica::Vitals before;
    if (!native_replica_is_client() || !native_replica_allows(replica::Mutation::apply_state) ||
        !replica::valid(value) || !native_actor_read_vitals(local_id, before)) {
        return false;
    }
    auto actor = resolve(local_id);
    if (!actor) {
        return false;
    }
    const auto combatant = static_cast<Combatant*>(actor);
    const auto vtable = *reinterpret_cast<uintptr_t**>(combatant);
    if (vtable[0x58 / 4] != executable_base + 0x805d50) {
        record("replica_vitals_rejected", ",\"reason\":\"effective_health_binding\"");
        return false;
    }
    // C05D50 reads base/species/stage modifiers. The raw Combatant maximum
    // is 1 in this real 10-HP fixture and is not the effective Creature limit.
    const float maximum = actor->GetMaxHitPoints();
    if (!std::isfinite(maximum) || maximum <= 0 || value.health > maximum) {
        record("replica_vitals_rejected", ",\"reason\":\"effective_health_limit\",\"maximum\":%.9g", double(maximum));
        return false;
    }
    record("replica_vitals_limits", ",\"effective_max_health\":%.9g,\"raw_max_health\":%.9g,\"raw_max_energy\":%.9g",
           double(maximum), double(actor->mMaxHealthPoints), double(actor->mMaxEnergy));
    // Absolute presentation fields only. Never call TakeDamage, AddEvolutionPoints,
    // native reward handlers, death, or an irreversible action to replay a result.
    actor->mHealthPoints = value.health;
    actor->mEnergy = value.energy;
    actor->mHunger = value.hunger;
    Simulator::cCreatureGameData::SetEvolutionPoints(value.dna);
    replica::Vitals after;
    const bool matched = native_actor_read_vitals(local_id, after) && after.health == value.health &&
                         after.energy == value.energy && after.hunger == value.hunger && after.dna == value.dna;
    record("replica_vitals_applied",
           ",\"actor\":%llu,\"matched\":%s,\"before_health\":%.9g,"
           "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"before_dna\":%.9g,\"dna\":%.9g",
           local_id, matched ? "true" : "false", double(before.health), double(value.health), double(value.energy),
           double(value.hunger), double(before.dna), double(value.dna));
    return matched;
}

namespace {
// Read IDs/flags in native containers in this callback only; never transmit
// addresses or the container representation. Commutative, so order-independent.
uint64_t inventory_digest(Simulator::cCollectableItems* items) {
    uint64_t digest = 0;
    for (const auto& item : items->mItemStatusInfos) {
        uint64_t id = 0;
        static_assert(sizeof(item.first) == 8);
        memcpy(&id, &item.first, 8);
        digest ^= (id ^ (uint64_t(item.second) << 56)) * 0x9e3779b97f4a7c15ULL;
    }
    return digest;
}

// What the replica challenge must leave as it was: the player's inventory, the non-player creature's state, the
// player creature's lifecycle and the scene.
struct ChallengeBaseline {
    uint64_t item_digest;
    size_t flags_count, unlocked_count;
    int points;
    float npc_health, npc_energy, npc_hunger;
    Combatant* npc_target;
    int age, brain;
    bool dead, removed;
    size_t noun_count;
    uint32_t mode;
    decltype(Creature::mInUseAbilityBits) active_bits;
    decltype(Creature::mRechargingAbilityBits) recharge_bits;
    bool world_pending;

    ChallengeBaseline(Animal* actor, Animal* npc, Manager* manager, Simulator::cCollectableItems* items,
                      App::cCreatureModeStrategy* strategy)
        : item_digest(inventory_digest(items)),
          flags_count(items->mItemStatusInfos.size()),
          unlocked_count(items->mUnlockedItems.size()),
          points(items->mUnlockPoints),
          npc_health(npc->mHealthPoints),
          npc_energy(npc->mEnergy),
          npc_hunger(npc->mHunger),
          npc_target(npc->mpCombatantTarget),
          age(actor->mAge),
          brain(actor->GetCurrentBrainLevel()),
          dead(actor->mbDead),
          removed(actor->mbMarkedForDeletion),
          noun_count(manager->mNouns.size()),
          mode(Simulator::GetGameModeID()),
          active_bits(npc->mInUseAbilityBits),
          recharge_bits(npc->mRechargingAbilityBits),
          world_pending(strategy->field_48) {}

    bool inventory_unchanged(Simulator::cCollectableItems* items) const {
        return points == items->mUnlockPoints && flags_count == items->mItemStatusInfos.size() &&
               unlocked_count == items->mUnlockedItems.size() && item_digest == inventory_digest(items);
    }

    bool npc_unchanged(Animal* npc, uint32_t attack_index) const {
        return npc == resolve(owned(2)) && attack_index == npc->mCurrentAttackIdx &&
               npc_target == npc->mpCombatantTarget && npc_health == npc->mHealthPoints && npc_energy == npc->mEnergy &&
               npc_hunger == npc->mHunger;
    }

    bool lifecycle_unchanged(Animal* actor, Manager* manager) const {
        return actor == resolve(owned(1)) && age == actor->mAge && brain == actor->GetCurrentBrainLevel() &&
               dead == actor->mbDead && removed == actor->mbMarkedForDeletion && noun_count == manager->mNouns.size();
    }

    bool abilities_unchanged(Animal* npc) const {
        return active_bits == npc->mInUseAbilityBits && recharge_bits == npc->mRechargingAbilityBits;
    }
};

// What the guarded native entry points returned when the challenge called them.
struct RefusedCalls {
    uint32_t attack_index = UINT32_MAX;
    int selected = 0, damage = 0;
    bool strike = false, unlocked = false, locked = false, spawned = false, saved = false;
    bool save_message_handled = false, load_mode_unchanged = false;
};

// These are actual guarded native entry points with live receivers, not calls
// to policy.allow(). No bypass/trampoline is used by the challenge.
RefusedCalls call_refused_mutations(Animal* actor, Animal* npc, Simulator::cCollectableItems* items,
                                    App::cCreatureModeStrategy* strategy, const uint32_t item_ids[2], float dna,
                                    const ChallengeBaseline& baseline) {
    using ReplicaAbi = NativeReplicaAbi<Animal>;
    RefusedCalls calls;
    npc->NPCTickAI(0.25f);
    actor->AvatarTickAI(0.25f);
    npc->SetCreatureTarget(actor, true, 2);
    calls.attack_index = npc->mCurrentAttackIdx;
    calls.selected = reinterpret_cast<ActorAbi::SelectAbility>(executable_base + 0x819800)(
        npc, npc->mDefaultAttackAbilityIndex, true, false);
    npc->PlayAbility(npc->mDefaultAttackAbilityIndex);
    reinterpret_cast<ReplicaAbi::AbilityUse>(executable_base + 0x819900)(npc, npc->mDefaultAttackAbilityIndex);
    calls.strike = npc->funcB8h(actor, npc->mDefaultAttackAbilityIndex, nullptr);
    calls.damage = actor->TakeDamage(1.0f, npc->mPoliticalID, 0, Vec(0, 0, 0), npc);
    actor->ConsumeEnergy(1.0f);
    actor->UpdateHunger(1.0f);
    reinterpret_cast<ReplicaAbi::Hunger>(executable_base + 0x815470)(actor, 1.0f);
    Simulator::cCreatureGameData::AddEvolutionPoints(17.0f);
    Simulator::cCreatureGameData::SetEvolutionPoints(dna + 29.0f);
    // The pinned Strategy dispatcher forwards these three native words to its
    // subhandlers. All objects remain local and live for this synchronous call.
    uintptr_t action_data[]{reinterpret_cast<uintptr_t>(actor), reinterpret_cast<uintptr_t>(npc), 0};
    static_assert(sizeof(action_data) == 12);
    strategy->ExecuteAction(0x045ab96e, action_data);
    for (const auto id : replica_part_actions) {
        strategy->ExecuteAction(id, action_data);
    }
    actor->func8Ch();
    actor->func90h();
    actor->GrowUp();
    actor->SetCurrentBrainLevel(baseline.brain + 1);
    actor->funcC8h(false);
    calls.unlocked =
        reinterpret_cast<ReplicaAbi::Unlock>(executable_base + 0x196da0)(items, item_ids[0], item_ids[1], 1);
    calls.locked = reinterpret_cast<ReplicaAbi::Lock>(executable_base + 0x196e10)(items, item_ids[0], item_ids[1]);
    calls.spawned =
        Animal::Create(actor->GetPosition(), actor->mpSpeciesProfile, 1, actor->mHerd.get(), false, false) != nullptr;
    reinterpret_cast<ReplicaAbi::HerdUpdate>(executable_base + 0x86d960)(actor->mHerd.get(), 1000);
    calls.saved = reinterpret_cast<NativePersistenceAbi<Simulator::cGamePersistenceManager>::Save>(
        executable_base + 0x729600)(Simulator::cGamePersistenceManager::Get(), L"SporeMP-M05-refused", false);
    for (auto message : replica_save_messages) {
        calls.save_message_handled |= reinterpret_cast<ReplicaAbi::PersistenceMessage>(executable_base + 0x729960)(
            Simulator::cGamePersistenceManager::Get(), message, nullptr);
    }
    reinterpret_cast<NativePersistenceAbi<Simulator::cGamePersistenceManager>::Load>(executable_base + 0x728020)(
        Simulator::cGamePersistenceManager::Get(), L"Satiria.spo");
    calls.load_mode_unchanged = baseline.mode == Simulator::GetGameModeID();
    return calls;
}
} // namespace

bool native_actor_probe_replica_denials() {
    if (!on_thread() || !native_replica_is_client() || native_replica_bootstrap_open()) {
        return false;
    }
    replica::Vitals before;
    auto actor = resolve(owned(1)), npc = resolve(owned(2));
    auto manager = Manager::Get();
    auto player = manager ? manager->GetPlayer() : nullptr;
    auto items = player ? player->mpCRGItems.get() : nullptr;
    auto strategy = App::cCreatureModeStrategy::Get();
    if (!actor || !npc || actor == npc || !native_actor_read_vitals(owned(1), before) || !items ||
        items->mUnlockableItems.empty() || !actor->mHerd || !actor->mpSpeciesProfile || !strategy) {
        return false;
    }
    const auto item_key = items->mUnlockableItems.begin()->first;
    const ChallengeBaseline baseline(actor, npc, manager, items, strategy);
    uint32_t item_ids[2]{};
    memcpy(item_ids, &item_key, sizeof(item_ids));
    record("replica_challenge_begin",
           ",\"actor\":%llu,\"npc\":%llu,\"dna\":%.9g,\"item_instance\":%u,\"item_group\":%u", owned(1), owned(2),
           double(before.dna), item_ids[0], item_ids[1]);
    const RefusedCalls calls = call_refused_mutations(actor, npc, items, strategy, item_ids, before.dna, baseline);
    replica::Vitals after;
    const bool vitals = native_actor_read_vitals(owned(1), after) && before.health == after.health &&
                        before.energy == after.energy && before.hunger == after.hunger && before.dna == after.dna;
    const bool inventory = baseline.inventory_unchanged(items);
    const bool npc_unchanged = baseline.npc_unchanged(npc, calls.attack_index);
    const bool lifecycle = baseline.lifecycle_unchanged(actor, manager);
    const bool ability_state = baseline.abilities_unchanged(npc);
    const bool progression = baseline.world_pending == strategy->field_48;
    const bool charm_queued = native_replica_queue_charm_probe(actor);
    const bool passed = vitals && inventory && npc_unchanged && lifecycle && ability_state && progression &&
                        calls.load_mode_unchanged && calls.damage == 0 && calls.selected == -1 && !calls.strike &&
                        !calls.unlocked && !calls.locked && !calls.spawned && !calls.saved &&
                        !calls.save_message_handled && charm_queued;
    record("replica_challenge_result",
           ",\"passed\":%s,\"vitals_unchanged\":%s,\"inventory_unchanged\":%s,"
           "\"npc_unchanged\":%s,\"lifecycle_unchanged\":%s,\"damage_result\":%d,\"unlock_result\":%s,"
           "\"lock_result\":%s,\"spawn_null\":%s,\"save_result\":%s,\"selected_ability\":%d,\"strike_result\":%s,"
           "\"ability_bits_unchanged\":%s,\"pending_progression_unchanged\":%s,\"save_message_handled\":%s,\"charm_"
           "probe_queued\":%s,\"load_mode_unchanged\":%s,\"dna\":%.9g",
           passed ? "true" : "false", vitals ? "true" : "false", inventory ? "true" : "false",
           npc_unchanged ? "true" : "false", lifecycle ? "true" : "false", calls.damage,
           calls.unlocked ? "true" : "false", calls.locked ? "true" : "false", calls.spawned ? "false" : "true",
           calls.saved ? "true" : "false", calls.selected, calls.strike ? "true" : "false",
           ability_state ? "true" : "false", progression ? "true" : "false",
           calls.save_message_handled ? "true" : "false", charm_queued ? "true" : "false",
           calls.load_mode_unchanged ? "true" : "false", double(after.dna));
    return passed;
}

namespace {
// Whether the request names the second player's live actor, in a scene that can take a nest probe now.
worker::Result nest_request_admission(const worker::Message& request, bool initial_home, ActorBinding& binding) {
    using worker::Result;
    if (!worker::valid_nest_probe(request)) {
        return Result::invalid;
    }
    if (!on_thread() || native_replica_is_client() || request.epoch != commands.epoch() ||
        (!initial_home && !native_network_authority_inputs_ready()) || commands.pending_count() || executing ||
        !native_death_checkpoint_idle() || nest_probe_epoch == commands.epoch()) {
        return Result::unavailable;
    }
    binding = commands.find(request.values[1]);
    if (!binding.live || binding.epoch != request.epoch || binding.owner != 2) {
        return Result::invalid;
    }
    return Result::accepted;
}

// The number of every noun before the nest is made, into `ids`. False when two nouns share a number, or when `herd`
// is not a present noun among them.
bool census_before_nest(Manager* manager, Simulator::cHerd* herd, std::set<uint32_t>& ids) {
    bool present = false;
    for (auto& noun : manager->mNouns) {
        if (!ids.insert(noun.mID).second) {
            return false;
        }
        if (&noun == static_cast<Noun*>(herd)) {
            present = !noun.mbIsDestroyed && !noun.field_20;
        }
    }
    return present;
}

// Whether both players' creatures are live and in different herds, the first being the original avatar in the
// avatar herd, and the second player's reward context is ready.
bool nest_scene_ready(Manager* manager, Animal* actor, Animal* primary, uint64_t actor_id) {
    return manager && actor && primary && primary == manager->GetAvatar() && actor != primary && !primary->mbDead &&
           !actor->mbDead && actor->mHerd && primary->mHerd && !(actor->mHerd == primary->mHerd) &&
           !(manager->mpAvatarHerd != primary->mHerd) && native_awards_ready(actor_id, commands.epoch());
}

// Whether `herd` is enabled, is not the avatar's, holds `actor` alone and has neither nest nor eggs.
bool lone_nestless_herd(Simulator::cHerd* herd, Animal* actor) {
    return !herd->mpNest && !herd->mOwnedByAvatar && herd->mbEnabled && herd->mHerd.size() == 1 &&
           herd->mHerd.front().get() == actor && herd->mEggs.empty();
}

// Whether the factory made the nest of `herd` and left the second player's creature in that herd, with the health
// and the place it had.
bool nest_made_for(Simulator::cNest* nest, Simulator::cHerd* herd, Animal* actor, float actor_health,
                   const Vec& actor_position) {
    return nest && !nest->mbIsDestroyed && !nest->field_20 && nest->GetNounID() == Simulator::cNest::NOUN_ID &&
           nest->mpHerd == herd && herd->mpNest.get() == nest && !herd->mOwnedByAvatar && actor->mHerd == herd &&
           actor->mHealthPoints == actor_health && actor->GetPosition() == actor_position;
}

// SDK cbf9206: CreateNest(const Vector3&, cHerd*) -> cNest*, __thiscall,
// installed B20DD0, RET 8. B23920's native caller then assigns mpNest.
// Unlike CreateHerd(..., true, ...), this does not replace mpAvatarHerd.
bool create_nest_binding_verified() {
    constexpr unsigned char prefix[] = {0x83, 0xec, 0x1c, 0x53, 0x56, 0x57, 0x68, 0x22,
                                        0x61, 0xaa, 0x52, 0xe8, 0x10, 0xfe, 0xff, 0xff};
    constexpr unsigned char tail[] = {0x8b, 0xc7, 0x5f, 0x5e, 0x5b, 0x83, 0xc4, 0x1c, 0xc2, 8, 0};
    return executable_end - executable_base >= 0x720e6e &&
           GetAddress(Simulator::cGameNounManager, CreateNest) == executable_base + 0x720dd0 &&
           !memcmp(reinterpret_cast<void*>(executable_base + 0x720dd0), prefix, sizeof(prefix)) &&
           !memcmp(reinterpret_cast<void*>(executable_base + 0x720e63), tail, sizeof(tail));
}

// Whether the nouns are now those numbered in `before` and the new nest, each number once. `after` receives the
// numbers found.
bool nest_is_only_new_noun(Manager* manager, const std::set<uint32_t>& before, Simulator::cNest* nest,
                           std::set<uint32_t>& after) {
    bool unique = true;
    for (auto& noun : manager->mNouns) {
        unique = after.insert(noun.mID).second && unique;
    }
    auto expected = before;
    if (nest) {
        expected.insert(nest->mID);
    }
    return unique && expected.size() == before.size() + 1 && expected == after;
}

// The first player's home and state as they are before a nest is made for the second player. Making that nest must
// change none of them.
struct PrimaryHome {
    cHerdPtr herd;
    cNestPtr nest;
    Simulator::cPlayer* player;
    eastl::vector<cCreatureAnimalPtr> posse;
    Simulator::cCreatureGameData stage;
    float health;
    Vec position;

    PrimaryHome(Manager* manager, Animal* primary)
        : herd(manager->mpAvatarHerd),
          nest(herd->mpNest),
          player(manager->GetPlayer()),
          posse(manager->mPosseMembers),
          stage(*Simulator::cCreatureGameData::Get()),
          health(primary->mHealthPoints),
          position(primary->GetPosition()) {}

    bool unchanged(Manager* manager, Animal* primary) const {
        return manager->GetAvatar() == primary && manager->GetPlayer() == player && manager->mpAvatarHerd == herd &&
               herd->mpNest == nest && manager->mPosseMembers == posse &&
               !memcmp(&stage, Simulator::cCreatureGameData::Get(), sizeof(stage)) &&
               primary->mHealthPoints == health && primary->GetPosition() == position;
    }
};
} // namespace

static worker::Result create_secondary_nest(const worker::Message& request, bool initial_home) {
    using worker::Result;
    ActorBinding binding;
    const Result admission = nest_request_admission(request, initial_home, binding);
    if (admission != Result::accepted) {
        return admission;
    }
    auto manager = Manager::Get();
    auto actor = resolve(binding.id);
    auto primary = resolve(owned(1));
    if (!nest_scene_ready(manager, actor, primary, binding.id)) {
        return Result::unavailable;
    }
    auto herd = actor->mHerd;
    std::set<uint32_t> before;
    if (!census_before_nest(manager, herd.get(), before) || !lone_nestless_herd(herd.get(), actor)) {
        return Result::unavailable;
    }
    const auto position = herd->mInitialPosition;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !create_nest_binding_verified()) {
        return Result::unavailable;
    }
    const PrimaryHome home(manager, primary);
    const auto actor_health = actor->mHealthPoints;
    const auto actor_position = actor->GetPosition();
    if (!observe_native_player_checkpoint(request.sequence, "nest_before")) {
        return Result::unavailable;
    }
    nest_probe_epoch = commands.epoch();
    auto nest = manager->CreateNest(position, herd.get());
    if (nest && !nest->mbIsDestroyed && nest->mpHerd == herd) {
        herd->mpNest = nest;
    }
    std::set<uint32_t> after;
    const bool only_nest_added = nest_is_only_new_noun(manager, before, nest, after);
    const bool primary_unchanged = home.unchanged(manager, primary);
    const bool applied =
        nest_made_for(nest, herd.get(), actor, actor_health, actor_position) && primary_unchanged && only_nest_added;
    record("native_secondary_nest_factory",
           ",\"request\":%llu,\"owner\":2,\"actor\":%llu,\"native_id\":%u,"
           "\"herd_native_id\":%u,\"nest_native_id\":%u,\"primary_herd_native_id\":%u,\"primary_nest_native_id\":%u,"
           "\"primary_unchanged\":%s,\"nouns_before\":%u,\"nouns_after\":%u,\"applied\":%s,"
           "\"native_position\":[%.9g,%.9g,%.9g],\"initial_home\":%s",
           request.sequence, binding.id, actor->mID, herd->mID, nest ? nest->mID : UINT32_MAX, home.herd->mID,
           home.nest ? home.nest->mID : UINT32_MAX, primary_unchanged ? "true" : "false", unsigned(before.size()),
           unsigned(after.size()), applied ? "true" : "false", nest ? double(nest->GetPosition().x) : 0.,
           nest ? double(nest->GetPosition().y) : 0., nest ? double(nest->GetPosition().z) : 0.,
           initial_home ? "true" : "false");
    combat_context(primary, "nest_primary_after");
    combat_context(actor, "nest_secondary_after");
    const bool census = observe_native_player_checkpoint(request.sequence, "nest_after");
    // Only the network home path publishes the nest and permits original Save.
    // A failed/uncertain native mutation retains the existing checkpoint fence.
    if (initial_home && applied && census) {
        nest_probe_epoch = 0;
    }
    flush();
    return applied && census ? Result::accepted : Result::failed;
}

worker::Result native_nest_factory_probe(const worker::Message& request) {
    return create_secondary_nest(request, false);
}

bool native_actor_network_prepare_home() {
    if (!on_thread() || native_replica_is_client()) {
        return false;
    }
    const auto id = owned(2);
    if (!id) {
        return true;
    }
    auto actor = resolve(id);
    auto manager = Manager::Get();
    if (!actor || !manager || !actor->mHerd || nest_probe_epoch == commands.epoch()) {
        return false;
    }
    auto herd = actor->mHerd;
    if (herd->mpNest) {
        auto nest = herd->mpNest.get();
        for (auto& noun : manager->mNouns) {
            if (&noun == static_cast<Noun*>(nest)) {
                return !noun.mbIsDestroyed && !noun.field_20 && noun.GetNounID() == Simulator::cNest::NOUN_ID &&
                       nest->mpHerd == herd;
            }
        }
        return false;
    }
    worker::Message request;
    request.kind = worker::Kind::command;
    request.op = worker::Op::nest_probe;
    request.epoch = commands.epoch();
    request.values[0] = 2;
    request.values[1] = id;
    return create_secondary_nest(request, true) == worker::Result::accepted;
}

namespace {
// The live actor a worker request names in its second word, when it belongs to the owner named in its first.
bool request_binding(const worker::Message& request, ActorBinding& binding) {
    binding = commands.find(request.values[1]);
    return binding.live && binding.epoch == request.epoch && binding.owner == request.values[0];
}

// Local developer investigation only. It cannot bypass an online session's command admission or a replica's
// immutable mutation fence.
bool local_developer_session() {
    wchar_t online[2]{};
    return !GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", online, _countof(online)) && !native_replica_is_client();
}

// Queues the developer walk towards a fossil for the next update. Collection is not requested.
worker::Result queue_fossil_approach(const worker::Message& request) {
    using worker::Result;
    if (!worker::valid_fossil_approach(request)) {
        return Result::invalid;
    }
    if (native_replica_is_client() || !commands.inputs_ready()) {
        return Result::unavailable;
    }
    if (fossil_probe.actor || fossil_probe_count >= 12) {
        return Result::busy;
    }
    ActorBinding binding;
    if (!request_binding(request, binding)) {
        return Result::invalid;
    }
    auto actor = resolve(binding.id);
    float position[3]{};
    if (!actor || actor->mbDead || !std::isfinite(actor->mHealthPoints) || actor->mHealthPoints <= 0 ||
        !native_fossil_position(static_cast<uint32_t>(request.values[2]), position)) {
        return Result::unavailable;
    }
    fossil_probe = {request.epoch, binding.id, static_cast<uint32_t>(request.values[0]), actor->mID,
                    static_cast<uint32_t>(request.values[2])};
    ++fossil_probe_count;
    record("native_fossil_approach_probe_queued",
           ",\"request\":%llu,\"actor\":%llu,\"owner\":%u,\"target_native_id\":%u,\"diagnostic_only\":true",
           request.sequence, binding.id, static_cast<uint32_t>(request.values[0]),
           static_cast<uint32_t>(request.values[2]));
    return Result::accepted;
}

worker::Result probe_social_tree(const worker::Message& request) {
    using worker::Result;
    if (!worker::valid_social_tree_probe(request)) {
        return Result::invalid;
    }
    if (!local_developer_session()) {
        return Result::unavailable;
    }
    ActorBinding binding;
    if (!request_binding(request, binding)) {
        return Result::invalid;
    }
    auto actor = resolve(binding.id);
    if (!actor) {
        return Result::unavailable;
    }
    const auto result = native_social_tree_probe(actor, actor->mID, binding.id, request.epoch,
                                                 static_cast<uint32_t>(request.values[2]));
    record("worker_social_tree_probe_result",
           ",\"request\":%llu,\"actor\":%llu,\"owner\":%llu,\"mode\":%llu,\"applied\":%s", request.sequence, binding.id,
           request.values[0], request.values[2], result ? "true" : "false");
    flush();
    return result ? Result::accepted : Result::unavailable;
}

worker::Result probe_social(const worker::Message& request) {
    using worker::Result;
    if (!worker::valid_social_probe(request)) {
        return Result::invalid;
    }
    if (!local_developer_session()) {
        return Result::unavailable;
    }
    ActorBinding binding;
    if (!request_binding(request, binding)) {
        return Result::invalid;
    }
    auto actor = resolve(binding.id);
    auto target = current_pickup_animal(static_cast<uint32_t>(request.values[2]));
    if (!actor || !target || identity(target).owner) {
        return Result::unavailable;
    }
    const auto result =
        native_social_probe(actor, actor->mID, target, target->mID, static_cast<uint32_t>(request.values[3]));
    record("worker_social_probe_result",
           ",\"request\":%llu,\"actor\":%llu,\"owner\":%llu,\"target_native_id\":%llu,\"ability_index\":%llu,"
           "\"queued\":%s",
           request.sequence, binding.id, request.values[0], request.values[2], request.values[3],
           result ? "true" : "false");
    flush();
    return result ? Result::accepted : Result::unavailable;
}

// What a first-owner binding starts from: the present native players and creatures flagged as the avatar.
struct PrimaryCensus {
    unsigned native_players = 0, avatar_flags = 0;
    bool primary_in_census = false, avatar_in_census = false;
};

PrimaryCensus census_primary(Manager* manager, Animal* avatar) {
    PrimaryCensus census;
    for (auto& noun : manager->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20) {
            continue;
        }
        if (noun.GetNounID() == native_player_noun_id) {
            ++census.native_players;
            if (&noun == static_cast<Noun*>(manager->GetPlayer())) {
                census.primary_in_census = true;
            }
        }
        if (noun.GetNounID() == Animal::NOUN_ID) {
            auto animal = object_cast<Animal>(&noun);
            if (animal && !animal->mbMarkedForDeletion) {
                if (animal->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar) {
                    ++census.avatar_flags;
                }
                if (animal == avatar) {
                    census.avatar_in_census = true;
                }
            }
        }
    }
    return census;
}

// Binds the original avatar of a one-player game to the owner the request names.
worker::Result bind_primary_owner(const worker::Message& request) {
    using worker::Result;
    for (size_t i = 1; i < request.values.size(); ++i) {
        if (request.values[i]) {
            return Result::invalid;
        }
    }
    const auto owner = request.values[0];
    if (owner != 1 && owner != 2) {
        return Result::invalid;
    }
    if ((native_replica_is_client() && !native_replica_bootstrap_open()) || Simulator::IsLoadingGameMode() ||
        commands.live_count() || setup_pending || players_pending || rewards_pending ||
        native_second_player(commands.epoch())) {
        return Result::unavailable;
    }
    auto manager = Manager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    if (!manager || !manager->GetPlayer() || !avatar || avatar->mbDead || avatar->mbMarkedForDeletion ||
        avatar->mbIsDestroyed || !avatar->mpSpeciesProfile) {
        return Result::unavailable;
    }
    // The pinned original player noun ID is separately checked by the
    // M03 player-context adapter. This probe must start from one original
    // primary player and one original avatar, not a hidden M09 pair.
    const PrimaryCensus census = census_primary(manager, avatar);
    record("native_primary_census",
           ",\"owner\":%llu,\"native_players\":%u,\"avatar_flags\":%u,\"primary_in_census\":%s,\"avatar_in_"
           "census\":%s,\"primary_unique_game_id\":%u",
           owner, census.native_players, census.avatar_flags, census.primary_in_census ? "true" : "false",
           census.avatar_in_census ? "true" : "false", manager->GetPlayer()->mUniqueGameID);
    if (census.native_players != 1 || census.avatar_flags != 1 || !census.primary_in_census ||
        !census.avatar_in_census || manager->GetPlayer()->GetNounID() != native_player_noun_id) {
        flush();
        return Result::unavailable;
    }
    const auto binding = bind(avatar, static_cast<uint32_t>(owner));
    if (!binding.live) {
        return Result::failed;
    }
    record("native_primary_owner_probe",
           ",\"owner\":%llu,\"actor\":%llu,\"native_id\":%u,\"primary_player\":true,\"second_player\":false", owner,
           binding.id, avatar->mID);
    flush();
    return Result::accepted;
}

// The actor pair is created on the next update. A first word of 1 asks for the experimental second actor of a
// foreign species.
worker::Result request_setup(const worker::Message& request) {
    using worker::Result;
    if (!worker::valid_setup(request)) {
        return Result::invalid;
    }
    if (request.values[0] == 1 && native_replica_is_client()) {
        return Result::unavailable;
    }
    if (setup_pending || owned(2)) {
        return Result::busy;
    }
    setup_foreign_pending = request.values[0] == 1;
    setup_pending = true;
    return Result::accepted;
}

worker::Result request_players() {
    using worker::Result;
    if (players_pending) {
        return Result::busy;
    }
    if (!owned(2)) {
        return Result::unavailable;
    }
    players_pending = 1;
    return Result::accepted;
}

worker::Result request_rewards() {
    using worker::Result;
    if (rewards_pending) {
        return Result::busy;
    }
    if (!owned(2)) {
        return Result::unavailable;
    }
    rewards_pending = true;
    return Result::accepted;
}

worker::Result request_duel(uint32_t owner) {
    if (duel_owner) {
        return worker::Result::busy;
    }
    duel_owner = owner;
    return worker::Result::accepted;
}

// The worker operations that are one owner's command to its actor: each is queued as that actor command.
worker::Result queue_worker_command(const worker::Message& request) {
    using worker::Op;
    using worker::Result;
    ActorCommand command;
    command.owner = static_cast<uint32_t>(request.values[0]);
    command.actor = request.values[1];
    switch (request.op) {
    case Op::engage:
    case Op::pickup:
        for (size_t i = 3; i < request.values.size(); ++i) {
            if (request.values[i]) {
                return Result::invalid;
            }
        }
        command.verb = request.op == Op::engage ? ActorVerb::engage : ActorVerb::pickup;
        command.target = request.values[2];
        break;
    case Op::jump:
        command.verb = ActorVerb::jump;
        break;
    case Op::move:
        if (request.values[2] > 3) {
            return Result::invalid;
        }
        command.verb = ActorVerb::move;
        command.direction = static_cast<int>(request.values[2]);
        break;
    case Op::stop:
        command.verb = ActorVerb::stop;
        break;
    case Op::retire:
        command.verb = ActorVerb::retire;
        break;
    default:
        return Result::unavailable;
    }
    const auto decision = commands.enqueue(command);
    record("worker_command_queued",
           ",\"request\":%llu,\"command\":%llu,\"actor\":%llu,\"owner\":%u,\"verb\":\"%s\",\"decision\":\"%s\"",
           request.sequence, command.sequence, command.actor, command.owner, actor_verb(command.verb),
           actor_decision(decision));
    return queue_result(decision);
}
} // namespace

worker::Result native_actor_worker_command(const worker::Message& request) {
    using worker::Op;
    using worker::Result;
    if (!on_thread() || Simulator::GetGameModeID() != kGameCreature) {
        return Result::unavailable;
    }
    if (request.epoch != commands.epoch()) {
        return Result::stale;
    }
    if (!native_network_authority_inputs_ready()) {
        return Result::unavailable;
    }
    switch (request.op) {
    case Op::fossil_approach:
        return queue_fossil_approach(request);
    case Op::nest_probe:
        return native_nest_factory_probe(request);
    case Op::social_tree_probe:
        return probe_social_tree(request);
    case Op::social_probe:
        return probe_social(request);
    case Op::bind_primary:
        return bind_primary_owner(request);
    case Op::setup:
        return request_setup(request);
    case Op::players:
        return request_players();
    case Op::rewards:
        return request_rewards();
    default:
        break;
    }
    // Every other operation belongs to one of the two owners.
    const auto owner = request.values[0];
    if (owner != 1 && owner != 2) {
        return Result::invalid;
    }
    if (request.op == Op::duel) {
        return request_duel(static_cast<uint32_t>(owner));
    }
    return queue_worker_command(request);
}

namespace {
// The harness is opt-in: SPOREMP_M03_ACTORS must be exactly `harness`.
bool harness_requested() {
    wchar_t option[32]{};
    return GetEnvironmentVariableW(L"SPOREMP_M03_ACTORS", option, _countof(option)) == 7 && !wcscmp(option, L"harness");
}

// Creates this process's trace file in `directory`. A file that already exists is never reopened.
bool open_trace(const wchar_t* directory) {
    if (!absolute_local_path(directory)) {
        return false;
    }
    wchar_t path[32768]{};
    if (swprintf_s(path, L"%ls\\actors-%lu.jsonl", directory, GetCurrentProcessId()) < 0) {
        return false;
    }
    output = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    return output != INVALID_HANDLE_VALUE;
}

// Writes what the trace holds and closes it: initialisation stops here and the harness stays off.
void abandon_trace() {
    flush();
    CloseHandle(output);
    output = INVALID_HANDLE_VALUE;
}

void measure_executable_image() {
    executable_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(executable_base);
    auto pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(executable_base + dos->e_lfanew);
    executable_end = executable_base + pe->OptionalHeader.SizeOfImage;
}

// This adapter's pinned bytes, then the preparation of each adapter it integrates, stopping at the first failure.
bool prepare_native_contexts() {
    return verify_context_bindings() &&
           prepare_native_player_context(executable_base, executable_end, engine_thread, player_event, on_thread) &&
           prepare_native_awards(executable_base, executable_end, engine_thread, player_event, on_thread) &&
           prepare_native_relationships(executable_base, executable_end, engine_thread, player_event, on_thread) &&
           prepare_native_history(executable_base, executable_end, engine_thread, player_event, on_thread) &&
           prepare_native_pickup(executable_base, executable_end, engine_thread, player_event, on_thread,
                                 pickup_tick_dispatch, pickup_first_feed_owner, pickup_honor_owned_claim) &&
           prepare_native_social(executable_base, executable_end, engine_thread, player_event, on_thread) &&
           prepare_native_death(executable_base, executable_end, engine_thread, player_event, on_thread) &&
           prepare_native_hatch(executable_base, executable_end, engine_thread, player_event, on_thread);
}

void locate_hook_originals() {
    destroy_original = reinterpret_cast<DestroyFn>(GetAddress(Simulator::cGameNounManager, DestroyInstance));
    pool_return_original = reinterpret_cast<PoolReturnFn>(executable_base + 0x6ccec0);
    land_original = reinterpret_cast<LandFn>(GetAddress(Simulator::cCreatureAnimal, OnJumpLand));
    npc_original = reinterpret_cast<AiFn>(GetAddress(Simulator::cCreatureAnimal, NPCTickAI));
    avatar_original = reinterpret_cast<AiFn>(GetAddress(Simulator::cCreatureAnimal, AvatarTickAI));
    damage_original = reinterpret_cast<DamageFn>(GetAddress(Simulator::cCombatant, TakeDamage));
    ability_original = reinterpret_cast<AbilityFn>(GetAddress(Simulator::cCreatureBase, PlayAbility));
    energy_original = reinterpret_cast<EnergyFn>(GetAddress(Simulator::cCreatureBase, ConsumeEnergy));
    dna_original = reinterpret_cast<DnaFn>(GetAddress(Simulator::cCreatureGameData, AddEvolutionPoints));
    // Pinned Animal virtual slot 0x84, independently inspected in the guarded
    // executable (target-binding-01.log). Observer only; arguments unchanged.
    target_original = reinterpret_cast<TargetFn>(executable_base + 0x803DF0);
    select_ability_original = reinterpret_cast<SelectAbilityFn>(executable_base + 0x819800);
    strike_original = reinterpret_cast<StrikeFn>(executable_base + 0x802b40);
    animal_damage_original = reinterpret_cast<DamageFn>(executable_base + 0x8075e0);
    set_attack_original = reinterpret_cast<ActorAbi::SetAttack>(executable_base + 0x807500);
    animation_done_original = reinterpret_cast<ActorAbi::AnimationDone>(executable_base + 0x80e2d0);
    attack_stop_original = reinterpret_cast<ActorAbi::AttackStop>(executable_base + 0x965980);
    nest_decide_original = reinterpret_cast<ActorAbi::Decide>(executable_base + 0x97d900);
    memory_creature_original = reinterpret_cast<ActorAbi::MemoryCreature>(executable_base + 0x999930);
}
} // namespace

void initialize_native_actors(const wchar_t* directory) {
    if (!harness_requested() || !open_trace(directory)) {
        return;
    }
    engine_thread = GetCurrentThreadId();
    QueryPerformanceCounter(&started);
    QueryPerformanceFrequency(&frequency);
    measure_executable_image();
    // Rows count time from this trace's start. qpc_origin is that start on the performance counter all
    // processes of this boot share, so the traces of one trial can be placed on one timeline afterwards.
    record("trace_start",
           ",\"qpc_origin\":%lld,\"executable_sha256\":\"%s\",\"sdk_commit\":\"%s\",\"bridge_version\":\"%s\","
           "\"developer_mutations_enabled\":true",
           started.QuadPart, candidate_exe_sha256, sdk_revision, bridge_version);
    if (!prepare_native_contexts()) {
        abandon_trace();
        return;
    }
    messages = App::IMessageManager::Get();
    cheats = App::ICheatManager::Get();
    if (!messages || !cheats || cheats->GetCheat("sporemp_actors")) {
        record("initialization_failed");
        abandon_trace();
        return;
    }
    locate_hook_originals();
    LONG status = change_hooks(hooks, _countof(hooks), true);
    if (status != NO_ERROR) {
        record("hooks_failed", ",\"status\":%ld", status);
        abandon_trace();
        return;
    }
    attached = true;
    enabled = true;
    for (auto id : message_ids) {
        messages->AddUnmanagedListener(&listener, id);
    }
    cheats->AddCheat("sporemp_actors", new Command(), false);
    record("harness_ready",
           ",\"hook_count\":%u,\"npc_target_sample_ms\":1000,\"combat_context_schema\":1,\"animation_context_schema\":"
           "2,\"decision_context_schema\":1,\"social_context_schema\":1,\"owned_social_result_schema\":1,\"decision_"
           "sample_limit_per_actor\":90",
           unsigned(_countof(hooks)));
    flush();
}

void dispose_native_actors() {
    if (!attached) {
        return;
    }
    if (GetCurrentThreadId() != engine_thread) {
        enabled = false;
        return;
    }
    if (!failed) {
        sample();
    }
    retire_native_player_context("bridge_dispose");
    enabled = false;
    for (auto id : message_ids) {
        if (!messages->RemoveListener(&listener, id)) {
            record("listener_remove_failed", ",\"message\":%u", id);
        }
    }
    cheats->RemoveCheat("sporemp_actors");
    LONG status = change_hooks(hooks, _countof(hooks), false);
    if (status == NO_ERROR) {
        attached = false;
    }
    record("trace_stop", ",\"detach_status\":%ld,\"healthy\":%s,\"target_callbacks\":%llu,\"target_sampled_out\":%llu",
           status, failed ? "false" : "true", target_callbacks, target_sampled_out);
    flush();
    if (output != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(output);
        CloseHandle(output);
        output = INVALID_HANDLE_VALUE;
    }
}
} // namespace sporemp
