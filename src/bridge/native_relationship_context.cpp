#include "native_relationship_context.h"
#include "native_relationship_abi.h"
#include "native_award_context.h"
#include "native_history_context.h"
#include "native_replica.h"
#include "native_scene.h"
#include <Spore/IO.h>
#include <Spore/Simulator/Serialization.h>
#include <Spore/Editors/SpeciesManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cHerd.h>
#include <intrin.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace sporemp {
struct NativeRelationshipScopeAccess {
    static bool secondary(const NativeRelationshipDamageScope* scope) {
        return scope && scope->secondary_;
    }

    static Simulator::cCreatureAnimal* target(const NativeRelationshipDamageScope* scope) {
        return scope ? scope->target_ : nullptr;
    }

    static uint64_t serial(const NativeRelationshipDamageScope* scope) {
        return scope ? scope->serial_ : 0;
    }

    static uint32_t target_native_id(const NativeRelationshipDamageScope* scope) {
        return scope ? scope->target_native_id_ : UINT32_MAX;
    }
};

namespace {
using Animal = Simulator::cCreatureAnimal;
using Manager = Simulator::cGameNounManager;
using Herd = Simulator::cHerd;
using Stage = Simulator::cCreatureGameData;
using Abi = NativeRelationshipAbi<Animal, Manager>;
Abi::Get get_original = nullptr;
Abi::Action action_original = nullptr;
Abi::Allocate allocate_original = nullptr;
Abi::Construct construct_original = nullptr;
Abi::Delete delete_original = nullptr;
Abi::Find find_original = nullptr;
Abi::Effective effective_original = nullptr;
Abi::Collection collection_original = nullptr;
Abi::Serialize<Simulator::ISerializerWriteStream> write_original = nullptr;
Abi::Serialize<Simulator::ISerializerReadStream> read_original = nullptr;
Abi::Insert insert_original = nullptr;
Abi::RefreshGoal refresh_goal_original = nullptr;
Abi::CardManager card_manager_original = nullptr;
Abi::PopulateCard populate_card_original = nullptr;
void* checkpoint_root = nullptr;
uintptr_t image_base = 0;
DWORD engine_thread = 0;
PlayerContextEvent sink = nullptr;
bool (*thread_check)() = nullptr;
void* secondary_manager = nullptr;
void* primary_manager = nullptr;
Animal* b_actor = nullptr;
Animal* a_actor = nullptr;
Simulator::cPlayer* a_player = nullptr;
Simulator::cPlayer* b_player = nullptr;
uint32_t b_native_id = UINT32_MAX, a_native_id = UINT32_MAX, a_player_id = UINT32_MAX;
uint32_t b_player_id = UINT32_MAX;
uint64_t b_id = 0, b_epoch = 0, next_serial = 0;
uint64_t failure_epoch = 0;
unsigned damage_depth = 0, action_depth = 0;
bool retiring = false, identity_retired = false;
const char* failure = nullptr;
NativeRelationshipDamageScope* current_scope = nullptr;

bool on_thread() {
    return GetCurrentThreadId() == engine_thread;
}

void event(const char* name, const char* format = "", ...) {
    if (!sink || !on_thread()) {
        return;
    }
    char fields[2304]{};
    va_list args;
    va_start(args, format);
    const int result = vsprintf_s(fields, format, args);
    va_end(args);
    if (result >= 0) {
        sink(name, fields);
    }
}

void fail(const char* reason) {
    if (failure) {
        return;
    }
    failure = reason;
    failure_epoch = b_epoch;
    event("native_relationship_acceptance_failed",
          ",\"actor\":%llu,\"epoch\":%llu,\"reason\":\"%s\",\"original_action_preserved\":true", b_id, b_epoch, reason);
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

template <class T>
T word(const void* base, size_t offset) {
    T result{};
    memcpy(&result, static_cast<const unsigned char*>(base) + offset, sizeof(result));
    return result;
}

bool manager_layout(void* manager) {
    return readable(manager, 0x60) && word<uintptr_t>(manager, 0) == image_base + 0x1065d58 &&
           word<uintptr_t>(manager, 4) == image_base + 0x1065d08 &&
           word<uintptr_t>(manager, 8) == image_base + 0x1065d00;
}

// A fresh original relationship manager of the pinned 0x60-byte layout, allocated under `allocation_name` and built
// by the original constructor. Null when the allocation failed or the constructor did not leave that layout; the
// allocation is then released.
void* construct_relationship_manager(const char* allocation_name) {
    auto allocation = allocate_original(0x60, allocation_name, 0, 0, nullptr, 0);
    if (!allocation) {
        return nullptr;
    }
    auto constructed = construct_original(allocation);
    if (constructed != allocation || !manager_layout(constructed)) {
        delete_original(allocation, 1);
        return nullptr;
    }
    return constructed;
}

// The bounds of a native vector of 32-bit pointers: in order, a whole number of entries, at most `limit` of them,
// and readable when not empty.
bool pointer_range(uintptr_t begin, uintptr_t end, size_t limit) {
    return end >= begin && !((end - begin) % 4) && (end - begin) / 4 <= limit &&
           (end <= begin || readable(reinterpret_cast<void*>(begin), end - begin));
}

bool current_noun(const Simulator::cGameData* candidate, uint32_t native_id) {
    auto manager = Manager::Get();
    if (!candidate || !manager) {
        return false;
    }
    for (auto& noun : manager->mNouns) {
        if (&noun == candidate) {
            return noun.mID == native_id && !noun.mbIsDestroyed && !noun.field_20;
        }
    }
    return false;
}

bool current_animal(Animal* animal, uint32_t native_id) {
    return current_noun(animal, native_id) && readable(animal, sizeof(Animal)) &&
           word<uintptr_t>(animal, 0) == image_base + 0x106a080 && !animal->mbMarkedForDeletion;
}

bool owner_current() {
    auto manager = Manager::Get();
    return secondary_manager && !retiring && !identity_retired && manager && thread_check && thread_check() &&
           Simulator::GetGameModeID() == kGameCreature && !Simulator::IsLoadingGameMode() &&
           current_animal(b_actor, b_native_id) && !b_actor->mbDead && current_animal(a_actor, a_native_id) &&
           current_noun(a_player, a_player_id) && current_noun(b_player, b_player_id) &&
           native_second_player(b_epoch) == b_player && manager->mpAvatar.get() == a_actor &&
           manager->mpPlayer.get() == a_player && get_original() == primary_manager &&
           manager_layout(primary_manager) && manager_layout(secondary_manager);
}

bool read_state(void* manager, const ResourceKey& key, NativeRelationshipState& result) {
    result = {};
    if (!manager_layout(manager) || !find_original) {
        return false;
    }
    auto map = static_cast<unsigned char*>(manager) + 0x20;
    const auto buckets = word<uintptr_t>(map, 4);
    const auto count = word<uint32_t>(map, 8);
    if (!count || count > 1048576 || !readable(reinterpret_cast<void*>(buckets), (size_t(count) + 1) * 4)) {
        return false;
    }
    uint32_t iterator[2]{};
    if (find_original(map, iterator, reinterpret_cast<const uint32_t*>(&key)) != iterator) {
        return false;
    }
    if (iterator[0] == reinterpret_cast<const uint32_t*>(buckets)[count]) {
        return true;
    }
    const auto node = reinterpret_cast<const unsigned char*>(iterator[0]);
    if (!readable(node, 0x24) || memcmp(node, &key, sizeof(key))) {
        return false;
    }
    result.present = true;
    memcpy(result.words.data(), node + 0xc, 20);
    return true;
}

uint64_t hash_bytes(uint64_t hash, const void* data, size_t length) {
    const auto bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool map_digest(void* manager, uint64_t& hash, uint32_t& entries) {
    hash = 0;
    entries = 0;
    if (!manager_layout(manager)) {
        return false;
    }
    auto map = static_cast<const unsigned char*>(manager) + 0x20;
    const auto buckets = word<uintptr_t>(map, 4);
    const auto count = word<uint32_t>(map, 8), expected = word<uint32_t>(map, 0xc);
    if (!count || count > 65536 || expected > 65536 ||
        !readable(reinterpret_cast<void*>(buckets), (size_t(count) + 1) * 4)) {
        return false;
    }
    const auto table = reinterpret_cast<const uint32_t*>(buckets);
    for (uint32_t i = 0; i < count; ++i) {
        for (auto address = table[i]; address;) {
            if (entries >= expected || !readable(reinterpret_cast<void*>(address), 0x24)) {
                return false;
            }
            const auto node = reinterpret_cast<const unsigned char*>(address);
            hash += hash_bytes(1469598103934665603ull, node, 0x20);
            ++entries;
            address = word<uint32_t>(node, 0x20);
        }
    }
    return entries == expected;
}

struct PrimaryState {
    bool valid = false;
    std::array<float, 5> progress{};
    uint64_t relations = 0, profile = 0, herd_members = 0;
    uint32_t relation_entries = 0, herd_id = UINT32_MAX;
    int brain = 0;
    float normalizing_scale = 0;
    ResourceKey default_key{}, species{};
    uintptr_t profile_address = 0, herd_address = 0;
    uint32_t herd_flags = 0;
    NativeHistoryDigest history{};
};

PrimaryState primary_state() {
    PrimaryState state;
    auto manager = Manager::Get();
    auto species = Editors::cSpeciesManager::Get();
    auto stage = Stage::Get();
    if (!owner_current() || !manager || !species || !stage || !a_actor->mpSpeciesProfile || !a_actor->mHerd ||
        !readable(a_actor->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile))) {
        return state;
    }
    const auto herd = a_actor->mHerd.get();
    if (!current_noun(herd, herd->mID) || !readable(herd, sizeof(Herd)) || herd->mHerd.size() > 1024) {
        return state;
    }
    state.progress = {stage->mEvolutionPoints, a_player->mCurrentGoalProgress, a_player->mGoalProgressTotal,
                      a_player->mCombatTraitProgress, a_player->mSocialTraitProgress};
    for (float v : state.progress) {
        if (!std::isfinite(v)) {
            return state;
        }
    }
    state.brain = stage->mCurrentBrainLevel;
    state.normalizing_scale = stage->mAvatarNormalizingScale;
    state.species = a_actor->mSpeciesKey;
    state.default_key = species->mDefaultKey;
    state.profile_address = reinterpret_cast<uintptr_t>(a_actor->mpSpeciesProfile);
    state.profile = hash_bytes(1469598103934665603ull, a_actor->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile));
    state.herd_address = reinterpret_cast<uintptr_t>(herd);
    state.herd_id = herd->mID;
    state.herd_flags = uint32_t(herd->mOwnedByAvatar) | (uint32_t(herd->mbShouldEvolve) << 1) |
                       (uint32_t(herd->mbExtinction) << 2) | (uint32_t(herd->mbBestFriends) << 3);
    state.herd_members = 1469598103934665603ull;
    for (auto& member : herd->mHerd) {
        if (!member || !current_animal(member.get(), member->mID)) {
            return state;
        }
        state.herd_members = hash_bytes(state.herd_members, &member->mID, 4);
    }
    state.valid =
        map_digest(primary_manager, state.relations, state.relation_entries) && native_history_read(1, state.history);
    return state;
}

bool same_primary(const PrimaryState& a, const PrimaryState& b) {
    return a.valid && b.valid && a.progress == b.progress && a.relations == b.relations &&
           a.relation_entries == b.relation_entries && a.profile == b.profile &&
           a.profile_address == b.profile_address && a.species == b.species && a.default_key == b.default_key &&
           a.brain == b.brain && a.normalizing_scale == b.normalizing_scale && a.herd_address == b.herd_address &&
           a.herd_id == b.herd_id && a.herd_flags == b.herd_flags && a.herd_members == b.herd_members &&
           a.history.valid && b.history.valid && a.history.categories == b.history.categories &&
           a.history.events == b.history.events && a.history.hash == b.history.hash;
}

struct Observation {
    bool valid = false, branch = false, later = false, default_species = false;
    uint32_t age = 0, flags = 0, special = 0, fear = 0, extinction = 0, species_members = 0, species_flagged = 0,
             herd_id = UINT32_MAX, herd_flags = 0;
    int effective = -1, primary_effective = -1;
    uint32_t related_herds = 0, allied_herds = 0, owned_herds = 0;
    uint64_t herd_digest = 0;
    NativeRelationshipState primary{}, secondary{};
    ResourceKey key{};
};

// The original collection of the nouns of one kind through B212D0 (Abi::Collection), as the bounds of its vector of
// pointers. `kind_callback_rva` is the one callback that differs between the animal and the herd collection.
bool collect_nouns(Manager* manager, uint32_t kind_callback_rva, uint32_t noun_id, uintptr_t& begin, uintptr_t& end) {
    auto collection = collection_original(manager, reinterpret_cast<void*>(image_base + 0x8d7e90),
                                          reinterpret_cast<void*>(image_base + 0x93d3d0),
                                          reinterpret_cast<void*>(image_base + kind_callback_rva),
                                          reinterpret_cast<void*>(image_base + 0x71e440), noun_id);
    if (!readable(collection, 12)) {
        return false;
    }
    begin = word<uintptr_t>(collection, 4);
    end = word<uintptr_t>(collection, 8);
    return pointer_range(begin, end, 65536);
}

bool species_flags(const ResourceKey& key, uint32_t& count, uint32_t& flagged) {
    count = flagged = 0;
    auto manager = Manager::Get();
    if (!manager) {
        return false;
    }
    uintptr_t begin = 0, end = 0;
    if (!collect_nouns(manager, 0x6ce030, Animal::NOUN_ID, begin, end)) {
        return false;
    }
    // Exact original collection, including dead/corpse animals. No live filter.
    for (auto at = begin; at < end; at += 4) {
        auto animal = *reinterpret_cast<Animal**>(at);
        if (!readable(animal, sizeof(Animal)) || word<uintptr_t>(animal, 0) != image_base + 0x106a080) {
            return false;
        }
        if (animal->mSpeciesKey == key) {
            ++count;
            if (animal->mGeneralFlags & 0x80) {
                ++flagged;
            }
        }
    }
    return count != 0;
}

bool related_herds(Herd* target, Observation& observation) {
    auto manager = Manager::Get();
    if (!manager || !target || !target->mpSpeciesProfile) {
        return false;
    }
    uintptr_t begin = 0, end = 0;
    if (!collect_nouns(manager, 0x6ccb70, Herd::NOUN_ID, begin, end)) {
        return false;
    }
    bool target_found = false;
    // BA4840 matches every original collected herd by profile pointer, not
    // species key or enabled/living membership. Its shared NPC state remains
    // original; only target ShouldEvolve may clear during the first kill.
    for (auto at = begin; at < end; at += 4) {
        auto herd = *reinterpret_cast<Herd**>(at);
        if (!readable(herd, sizeof(Herd)) || word<uintptr_t>(herd, 0) != image_base + 0x1071b90) {
            return false;
        }
        if (herd->mpSpeciesProfile != target->mpSpeciesProfile) {
            continue;
        }
        ++observation.related_herds;
        if (herd == target) {
            target_found = true;
        }
        if (herd->mbBestFriends) {
            ++observation.allied_herds;
        }
        if (herd->mOwnedByAvatar || herd == a_actor->mHerd.get() || herd == b_actor->mHerd.get()) {
            ++observation.owned_herds;
        }
        const uint32_t values[]{herd->mID,
                                reinterpret_cast<uintptr_t>(herd),
                                reinterpret_cast<uintptr_t>(herd->mpSpeciesProfile),
                                herd->mOwnerSpeciesKey.instanceID,
                                uint32_t(herd->mOwnerSpeciesKey.typeID),
                                herd->mOwnerSpeciesKey.groupID,
                                uint32_t(herd->mOwnedByAvatar),
                                uint32_t(herd->mbExtinction),
                                uint32_t(herd->mbBestFriends),
                                herd == target ? 0 : uint32_t(herd->mbShouldEvolve)};
        observation.herd_digest += hash_bytes(1469598103934665603ull, values, sizeof(values));
    }
    return target_found;
}

Observation observe(Animal* target, uint32_t native_id) {
    Observation observation;
    // A just-killed target is expected at action0: it must remain the current
    // original noun, but mbDead is deliberately not rejected here.
    if (!owner_current() || !current_animal(target, native_id) || target == a_actor || target == b_actor ||
        target->mSpeciesKey == a_actor->mSpeciesKey || target->mSpeciesKey == b_actor->mSpeciesKey || !target->mHerd) {
        return observation;
    }
    const auto herd = target->mHerd.get();
    if (herd == a_actor->mHerd.get() || herd == b_actor->mHerd.get() || !current_noun(herd, herd->mID) ||
        !readable(herd, sizeof(Herd)) || herd->mOwnedByAvatar || !readable(target->field_E84, 0x404)) {
        return observation;
    }
    observation.key = target->mSpeciesKey;
    observation.age = static_cast<uint32_t>(target->mAge);
    observation.flags = static_cast<uint32_t>(target->mGeneralFlags);
    observation.default_species = target->IsDefaultSpecies();
    observation.special = word<uint8_t>(target->field_E84, 0x388);
    observation.fear = word<uint32_t>(target->field_E84, 0x3fc);
    observation.extinction = word<uint32_t>(target->field_E84, 0x400);
    observation.herd_id = herd->mID;
    observation.herd_flags = uint32_t(herd->mOwnedByAvatar) | (uint32_t(herd->mbShouldEvolve) << 1) |
                             (uint32_t(herd->mbExtinction) << 2) | (uint32_t(herd->mbBestFriends) << 3);
    if (!read_state(primary_manager, observation.key, observation.primary) ||
        !read_state(secondary_manager, observation.key, observation.secondary) ||
        !species_flags(observation.key, observation.species_members, observation.species_flagged) ||
        !related_herds(herd, observation)) {
        return observation;
    }
    observation.effective =
        effective_original(secondary_manager, reinterpret_cast<const uint32_t*>(&observation.key), target, nullptr);
    observation.primary_effective =
        effective_original(primary_manager, reinterpret_cast<const uint32_t*>(&observation.key), target, nullptr);
    // A pack's alpha (flag 1) is admitted: the original's results for one were as for any member (trial339). What
    // player 1 holds on the species does not matter either; that it stays unchanged is checked at each step. Up to
    // 0.0.233 a first kill needed an ordinary member of a species player 1 had never touched.
    const bool ordinary = observation.age == 1 && !(observation.flags & 0x200) && !observation.default_species &&
                          !observation.special && !observation.species_flagged && !observation.allied_herds &&
                          !observation.owned_herds && !(observation.herd_flags & 12) &&
                          !observation.secondary.words[1] && !observation.secondary.words[2];
    observation.later =
        ordinary && observation.effective == 1 && observation.secondary.present &&
        observation.secondary.words[0] == 1 &&
        native_relationship_later_kill(observation.secondary.words[3], observation.fear, observation.extinction);
    observation.valid = true;
    observation.branch =
        observation.later || (ordinary && observation.effective == 3 &&
                              native_relationship_first_feared_kill(observation.secondary.words[3], observation.fear,
                                                                    observation.extinction));
    return observation;
}

void observation_event(const char* phase, uint64_t serial, uint32_t action, uint32_t target_native_id,
                       const Observation& observation) {
    event("native_relationship_state",
          ",\"phase\":\"%s\",\"serial\":%llu,\"actor\":%llu,\"target_native_id\":%u,\"action\":%u,"
          "\"valid\":%s,\"bounded_branch\":%s,\"later_kill\":%s,\"effective\":%d,\"species\":[%u,%u,%u],\"age\":%u,"
          "\"flags\":%u,\"special_388\":%u,\"default_species\":%s,"
          "\"fear_threshold\":%u,\"extinction_threshold\":%u,\"species_members\":%u,\"species_flagged\":%u,\"herd_"
          "native_id\":%u,\"herd_flags\":%u,"
          "\"a_present\":%s,\"a_words\":[%u,%u,%u,%u,%u],\"b_present\":%s,\"b_words\":[%u,%u,%u,%u,%u]",
          phase, serial, b_id, target_native_id, action, observation.valid ? "true" : "false",
          observation.branch ? "true" : "false", observation.later ? "true" : "false", observation.effective,
          observation.key.instanceID, observation.key.typeID, observation.key.groupID, observation.age,
          observation.flags, observation.special, observation.default_species ? "true" : "false", observation.fear,
          observation.extinction, observation.species_members, observation.species_flagged, observation.herd_id,
          observation.herd_flags, observation.primary.present ? "true" : "false", observation.primary.words[0],
          observation.primary.words[1], observation.primary.words[2], observation.primary.words[3],
          observation.primary.words[4], observation.secondary.present ? "true" : "false",
          observation.secondary.words[0], observation.secondary.words[1], observation.secondary.words[2],
          observation.secondary.words[3], observation.secondary.words[4]);
    event("native_relationship_shared_herds",
          ",\"phase\":\"%s\",\"serial\":%llu,\"actor\":%llu,\"target_native_id\":%u,"
          "\"primary_effective\":%d,\"related_herds\":%u,\"allied_herds\":%u,\"owned_herds\":%u,\"herd_digest\":%llu",
          phase, serial, b_id, target_native_id, observation.primary_effective, observation.related_herds,
          observation.allied_herds, observation.owned_herds, observation.herd_digest);
}

// Owner-2 client display only. The original Creature goal cards cache their
// counts through C2EE80. This private copy of B's adopted relationship values
// is the receiver for that one getter call; BA44C0/BA44E0 may insert absent
// species here, never in the adopted checkpoint root or A's manager.
struct GoalPresentation {
    void* root = nullptr;
    uint64_t epoch = 0, source_hash = 0, presented = 0;
    uint32_t source_entries = 0;
    ULONGLONG next = 0;
};

GoalPresentation goal_presentation;

struct GoalCall {
    void* goal = nullptr;
    ResourceKey key{};
    NativeRelationshipGoalValues before{}, expected{};
    uint32_t award = 0;
    bool present = false;
};

GoalCall* goal_call = nullptr;
bool goal_card = false;
unsigned goal_cards = 0;

bool goal_layout(const void* goal) {
    return readable(goal, 0xc8) && word<uintptr_t>(goal, 0) == image_base + 0x106b3c0;
}

NativeRelationshipGoalValues goal_cached(const void* goal) {
    return {word<uint32_t>(goal, 0x8c), word<uint32_t>(goal, 0x90), word<uint32_t>(goal, 0xa0)};
}

bool same_goal(const NativeRelationshipGoalValues& a, const NativeRelationshipGoalValues& b) {
    return a.befriended == b.befriended && a.killed == b.killed && a.relation == b.relation;
}

bool goal_owner(uint64_t epoch) {
    if (!on_thread() || !native_replica_is_client() || !secondary_manager || retiring || failure || !epoch ||
        epoch != b_epoch || Simulator::GetGameModeID() != kGameCreature || Simulator::IsLoadingGameMode()) {
        return false;
    }
    auto manager = Manager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    NativeSceneLocalIdentity identity;
    return avatar &&
           native_scene_local_identity(epoch, avatar->mID, reinterpret_cast<uintptr_t>(avatar), identity, true) &&
           identity.owner == 2;
}

void discard_goal_presentation() {
    if (goal_call) {
        return;
    }
    if (goal_presentation.root) {
        delete_original(goal_presentation.root, 1);
    }
    goal_presentation = {};
}

bool copy_goal_source() {
    uint64_t hash = 0, copied = 0;
    uint32_t entries = 0, copied_entries = 0;
    if (!map_digest(secondary_manager, hash, entries) || word<uint32_t>(secondary_manager, 0x4c)) {
        return false;
    }
    if (goal_presentation.root && goal_presentation.epoch == b_epoch && goal_presentation.source_hash == hash &&
        goal_presentation.source_entries == entries) {
        return true;
    }
    discard_goal_presentation();
    auto root = construct_relationship_manager("SporeMP relationship display");
    if (!root) {
        return false;
    }
    const auto map = static_cast<const unsigned char*>(secondary_manager) + 0x20;
    const auto table = reinterpret_cast<const uint32_t*>(word<uintptr_t>(map, 4));
    const auto count = word<uint32_t>(map, 8);
    bool valid = true;
    for (uint32_t i = 0; valid && i < count; ++i) {
        for (auto address = table[i]; valid && address;) {
            const auto node = reinterpret_cast<const unsigned char*>(address);
            uint32_t key[3]{};
            memcpy(key, node, sizeof(key));
            auto value = insert_original(static_cast<unsigned char*>(root) + 0x20, key);
            valid = readable(value, 20);
            if (valid) {
                memcpy(value, node + 0xc, 20);
            }
            address = word<uint32_t>(node, 0x20);
        }
    }
    valid = valid && map_digest(root, copied, copied_entries) && copied == hash && copied_entries == entries &&
            !word<uint32_t>(root, 0x4c);
    if (!valid) {
        delete_original(root, 1);
        return false;
    }
    const auto next = goal_presentation.next;
    goal_presentation = {};
    goal_presentation.next = next;
    goal_presentation.root = root;
    goal_presentation.epoch = b_epoch;
    goal_presentation.source_hash = hash;
    goal_presentation.source_entries = entries;
    event("native_relationship_goal_display_copied",
          ",\"owner\":2,\"actor\":%llu,\"epoch\":%llu,\"entries\":%u,\"hash\":%llu,\"exact\":true,\"source_unchanged\":"
          "true",
          b_id, b_epoch, entries, hash);
    return true;
}

// The original card items cache their text. E18220 keeps them in the UI
// manager's +28/+2C vector; each item holds its goal at +9C (E17F20).
unsigned populate_goal_cards(void* goal) {
    auto ui = card_manager_original();
    if (!readable(ui, 0x30)) {
        return 0;
    }
    const auto begin = word<uintptr_t>(ui, 0x28), end = word<uintptr_t>(ui, 0x2c);
    if (!pointer_range(begin, end, 4096)) {
        return 0;
    }
    unsigned populated = 0;
    for (auto at = begin; at < end; at += 4) {
        auto item = reinterpret_cast<void*>(word<uintptr_t>(reinterpret_cast<void*>(at), 0));
        if (!readable(item, 0xa0) || word<uintptr_t>(item, 0) != image_base + 0x107fd64 ||
            word<uintptr_t>(item, 0x9c) != reinterpret_cast<uintptr_t>(goal)) {
            continue;
        }
        goal_card = true;
        populate_card_original(item, 1);
        goal_card = false;
        ++populated;
    }
    return populated;
}

bool refresh_goal(void* goal) {
    if (!native_replica_allows(replica::Mutation::apply_state)) {
        return false;
    }
    refresh_goal_original(goal);
    goal_cards += populate_goal_cards(goal);
    return true;
}

// One manager's first map as a canonical snapshot. Refuses a populated second map,
// which the bounded foreign-owner path has never produced or qualified.
bool collect_relationships(void* manager, network::RelationshipFrame& frame) {
    frame.count = 0;
    frame.entries = {};
    if (!manager_layout(manager) || word<uint32_t>(manager, 0x4c)) {
        return false;
    }
    const auto map = static_cast<const unsigned char*>(manager) + 0x20;
    const auto buckets = word<uintptr_t>(map, 4);
    const auto count = word<uint32_t>(map, 8), expected = word<uint32_t>(map, 0xc);
    if (!count || count > 65536 || expected > network::relationship_capacity ||
        !readable(reinterpret_cast<void*>(buckets), (size_t(count) + 1) * 4)) {
        return false;
    }
    const auto table = reinterpret_cast<const uint32_t*>(buckets);
    for (uint32_t i = 0; i < count; ++i) {
        for (auto address = table[i]; address;) {
            if (frame.count >= expected || !readable(reinterpret_cast<void*>(address), 0x24)) {
                return false;
            }
            const auto node = reinterpret_cast<const unsigned char*>(address);
            auto& entry = frame.entries[frame.count++];
            memcpy(entry.key.data(), node, 12);
            memcpy(entry.words.data(), node + 0xc, 20);
            address = word<uint32_t>(node, 0x20);
        }
    }
    if (frame.count != expected) {
        return false;
    }
    std::sort(frame.entries.begin(), frame.entries.begin() + frame.count,
              [](const network::RelationshipEntry& a, const network::RelationshipEntry& b) { return a.key < b.key; });
    return network::valid_relationships(frame.count, frame.entries);
}

bool write_relationships(void* context) {
    if (!native_replica_allows(replica::Mutation::apply_state)) {
        return false;
    }
    const auto& frame = *static_cast<const network::RelationshipFrame*>(context);
    for (uint32_t i = 0; i < frame.count; ++i) {
        // BA3B40 returns the existing five-word value or inserts an absent one.
        auto value =
            insert_original(static_cast<unsigned char*>(secondary_manager) + 0x20, frame.entries[i].key.data());
        if (!readable(value, 20)) {
            return false;
        }
        memcpy(value, frame.entries[i].words.data(), 20);
    }
    return true;
}

PrimaryState experiment_primary;
Animal* experiment_target = nullptr;
uint32_t experiment_target_native_id = UINT32_MAX;
uint64_t experiment_target_id = 0, experiment_serial = 0, next_sample = 0;
ResourceKey experiment_key{};
Observation experiment_after{};
NativeHistoryDigest experiment_history_after{};
bool experiment_kill_observed = false;
unsigned later_samples = 0;
// Player 1 has acted or been struck while this fight of the second player is followed.
bool primary_active = false;
std::array<uint32_t, 8> rejected_targets{};
size_t rejected_target_count = 0;

void progress_event(const char* phase, uint64_t serial, const PrimaryState& now, bool unchanged) {
    float dna = 0;
    const auto player = native_second_player(b_epoch);
    const bool b_valid = player && native_awards_read_dna(b_id, b_epoch, dna);
    event("native_relationship_primary_invariants",
          ",\"phase\":\"%s\",\"serial\":%llu,\"actor\":%llu,\"primary_valid\":%s,\"primary_unchanged\":%s,"
          "\"a_progress\":[%.9g,%.9g,%.9g,%.9g,%.9g],\"a_relation_entries\":%u,\"a_relation_digest\":%llu,\"a_profile_"
          "digest\":%llu,"
          "\"a_herd_native_id\":%u,\"a_herd_flags\":%u,\"a_herd_members_digest\":%llu,\"a_brain\":%d,\"a_scale\":%.9g,"
          "\"b_valid\":%s,\"b_progress\":[%.9g,%.9g,%.9g,%.9g,%.9g]",
          phase, serial, b_id, now.valid ? "true" : "false", unchanged ? "true" : "false", double(now.progress[0]),
          double(now.progress[1]), double(now.progress[2]), double(now.progress[3]), double(now.progress[4]),
          now.relation_entries, now.relations, now.profile, now.herd_id, now.herd_flags, now.herd_members, now.brain,
          double(now.normalizing_scale), b_valid ? "true" : "false", double(dna),
          double(player ? player->mCurrentGoalProgress : 0), double(player ? player->mGoalProgressTotal : 0),
          double(player ? player->mCombatTraitProgress : 0), double(player ? player->mSocialTraitProgress : 0));
    NativeHistoryDigest b_history{};
    native_history_read(2, b_history);
    event("native_relationship_history_invariants",
          ",\"phase\":\"%s\",\"serial\":%llu,\"actor\":%llu,"
          "\"a_valid\":%s,\"a_categories\":%u,\"a_events\":%u,\"a_hash\":%llu,\"b_valid\":%s,\"b_categories\":%u,\"b_"
          "events\":%u,\"b_hash\":%llu",
          phase, serial, b_id, now.history.valid ? "true" : "false", now.history.categories, now.history.events,
          now.history.hash, b_history.valid ? "true" : "false", b_history.categories, b_history.events, b_history.hash);
}

// Stops following the current fight: its target, the state its kill left and what player 1 did during it.
void forget_followed_fight() {
    experiment_target = nullptr;
    experiment_target_id = 0;
    experiment_target_native_id = UINT32_MAX;
    experiment_after = {};
    experiment_history_after = {};
    experiment_kill_observed = false;
    primary_active = false;
}

void dispose_if_idle() {
    if (!retiring || damage_depth || action_depth || !secondary_manager) {
        return;
    }
    auto disposing = secondary_manager;
    secondary_manager = nullptr;
    discard_goal_presentation();
    retire_native_history();
    delete_original(disposing, 1);
    event("native_relationship_retired", ",\"actor\":%llu,\"epoch\":%llu,\"deleting_destructor\":true", b_id, b_epoch);
    primary_manager = nullptr;
    b_actor = a_actor = nullptr;
    a_player = b_player = nullptr;
    b_id = b_epoch = 0;
    retiring = identity_retired = false;
    experiment_target = nullptr;
    experiment_target_id = 0;
    experiment_primary = {};
    later_samples = 0;
    primary_active = false;
}

void* __cdecl manager_hook() {
    const auto where = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - image_base);
    auto primary = get_original();
    if (on_thread() && goal_call && goal_presentation.root && native_relationship_goal_reader(where)) {
        return goal_presentation.root;
    }
    if (on_thread() && goal_card && goal_presentation.root && native_relationship_card_reader(where)) {
        return goal_presentation.root;
    }
    if (on_thread() && secondary_manager &&
        ((NativeRelationshipScopeAccess::secondary(current_scope) && native_relationship_damage_caller(where)) ||
         (where == 0x7a58b3 && native_history_relationship_query_active()))) {
        if (primary != primary_manager) {
            fail("primary_manager_changed_during_callback");
        }
        event("native_relationship_receiver_selected",
              ",\"serial\":%llu,\"actor\":%llu,\"caller_rva\":%u,\"owner\":2,\"failure_latched\":%s",
              NativeRelationshipScopeAccess::serial(current_scope), b_id, where, failure ? "true" : "false");
        return secondary_manager;
    }
    return primary;
}

// After a blow or a relationship action of the second player, sample_native_relationships looks at both players'
// states twelve more times, at least 250 ms apart.
void watch_later_frames() {
    later_samples = 12;
    next_sample = GetTickCount64() + 250;
}

// Whether the original action left anything but the admitted result: after a kill (action 0) relation 1, one more
// kill and the target herd's ShouldEvolve clear; after action 2 the same relation and kill count. Nothing else of
// the second player's entry, of the first player's entry or of the related herds may differ.
bool unexpected_action_result(uint32_t action, const Observation& before, const Observation& after) {
    const bool killed = action == 0;
    return !before.valid || !after.valid || !after.secondary.present ||
           after.effective != (killed ? 1 : before.effective) || after.species_flagged ||
           before.herd_id != after.herd_id ||
           after.herd_flags != (killed ? (before.herd_flags & ~2u) : before.herd_flags) ||
           before.related_herds != after.related_herds || before.herd_digest != after.herd_digest ||
           after.allied_herds || after.owned_herds || before.primary_effective != after.primary_effective ||
           before.primary.present != after.primary.present || before.primary.words != after.primary.words ||
           (action == 0 && after.secondary.words[3] != before.secondary.words[3] + 1) ||
           (action == 2 && after.secondary.words[3] != before.secondary.words[3]) || after.secondary.words[2] != 0 ||
           after.secondary.words[0] != (killed ? 1 : before.secondary.words[0]) ||
           before.secondary.words[1] != after.secondary.words[1] ||
           native_relationship_initialized_last_word(before.secondary.present, before.secondary.words[4]) !=
               after.secondary.words[4];
}

// The state a kill left is what the later samples are compared with.
void keep_kill_result(const Observation& after, const char* unreadable_history) {
    experiment_after = after;
    experiment_kill_observed = true;
    if (!native_history_read(2, experiment_history_after)) {
        fail(unreadable_history);
    }
}

// The original writes one first-kill event to the second player's history for a first kill, and none for a later
// kill or for an action that did not kill.
void check_action_history(bool killed, const Observation& before, const Observation& after,
                          const NativeHistoryReceipt& history_before, const NativeHistoryReceipt& history_after) {
    if (killed && before.later) {
        // A later kill of a hunted species: the count rose (checked above) and the original writes no first-kill event.
        if (history_after.serial != history_before.serial) {
            fail("unexpected_later_kill_history_event");
        }
        keep_kill_result(after, "unreadable_later_kill_history");
    } else if (killed) {
        if (!history_after.valid || history_after.serial <= history_before.serial || history_after.key != before.key ||
            history_after.event_type != 5 || history_after.relation != 1) {
            fail("missing_original_first_kill_history");
        }
        keep_kill_result(after, "unreadable_first_kill_history");
    } else if (history_after.serial != history_before.serial) {
        fail("unexpected_nonlethal_history_event");
    }
}

// The original relationship action on the second player's own manager, observed before and after. Whatever is
// found, the original runs exactly once with its arguments unchanged; a finding only latches the failure.
void followed_action(void* self, uint32_t action, const uint32_t* key, Animal* target) {
    ++action_depth;
    const auto serial = NativeRelationshipScopeAccess::serial(current_scope);
    const bool authenticated = NativeRelationshipScopeAccess::secondary(current_scope) &&
                               target == NativeRelationshipScopeAccess::target(current_scope);
    const auto native_id = NativeRelationshipScopeAccess::target_native_id(current_scope);
    const auto before = observe(target, native_id);
    const auto primary_before = primary_state();
    observation_event("action_enter", serial, action, native_id, before);
    if (!authenticated || !before.valid || !before.branch || (action != 0 && action != 2) ||
        !readable(key, sizeof(ResourceKey)) || memcmp(key, &before.key, sizeof(ResourceKey))) {
        fail("unexpected_original_relationship_branch");
    }
    NativeHistoryReceipt history_before{};
    native_history_last_receipt(history_before);
    // The original receives every argument unchanged, even after failure.
    action_original(self, action, key, target);
    const auto after = observe(target, native_id);
    const auto primary_after = primary_state();
    observation_event("action_return", serial, action, native_id, after);
    const bool unchanged = same_primary(primary_before, primary_after);
    progress_event("action_return", serial, primary_after, unchanged);
    if (!unchanged) {
        fail("primary_changed_during_relationship_action");
    }
    if (unexpected_action_result(action, before, after)) {
        fail("unexpected_original_relationship_result");
    }
    NativeHistoryReceipt history_after{};
    native_history_last_receipt(history_after);
    check_action_history(action == 0, before, after, history_before, history_after);
    watch_later_frames();
    --action_depth;
    dispose_if_idle();
}

void __fastcall action_hook(void* self, void*, uint32_t action, const uint32_t* key, Animal* target) {
    if (!on_thread()) {
        action_original(self, action, key, target);
        return;
    }
    // The receiver itself owns B's history even if authentication fails.
    // Keep this unexpected in-flight action out of A's storage, then fence.
    const bool owned = self == secondary_manager && secondary_manager;
    NativeHistoryOwnerScope history_scope(owned ? 2 : 0, b_id, b_epoch, action, target, key);
    if (self != secondary_manager || !secondary_manager) {
        action_original(self, action, key, target);
        return;
    }
    followed_action(self, action, key, target);
}
} // namespace

NativeHook native_relationship_manager_binding() {
    return {reinterpret_cast<void**>(&get_original), reinterpret_cast<void*>(manager_hook)};
}

NativeHook native_relationship_action_binding() {
    return {reinterpret_cast<void**>(&action_original), reinterpret_cast<void*>(action_hook)};
}

bool prepare_native_relationships(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event_sink,
                                  bool (*check)()) {
    image_base = base;
    engine_thread = thread;
    sink = event_sink;
    thread_check = check;

    struct Prefix {
        uint32_t rva;
        unsigned char bytes[20];
        uint32_t relocations;
        unsigned length;
    };

    static const Prefix prefixes[] = {
#include "native_relationship_prefixes.inc"
    };
    for (const auto& prefix : prefixes) {
        unsigned char bytes[20];
        memcpy(bytes, prefix.bytes, 20);
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
            event("relationship_binding_rejected", ",\"rva\":%u", prefix.rva);
            return false;
        }
    }
    get_original = reinterpret_cast<Abi::Get>(base + 0x73d5c0);
    action_original = reinterpret_cast<Abi::Action>(base + 0x7a4a00);
    allocate_original = reinterpret_cast<Abi::Allocate>(base + 0xb47240);
    construct_original = reinterpret_cast<Abi::Construct>(base + 0x7a4040);
    delete_original = reinterpret_cast<Abi::Delete>(base + 0x7a40c0);
    find_original = reinterpret_cast<Abi::Find>(base + 0x7a3190);
    effective_original = reinterpret_cast<Abi::Effective>(base + 0x7a40e0);
    collection_original = reinterpret_cast<Abi::Collection>(base + 0x7212d0);
    insert_original = reinterpret_cast<Abi::Insert>(base + 0x7a3b40);
    refresh_goal_original = reinterpret_cast<Abi::RefreshGoal>(base + 0x82fbb0);
    card_manager_original = reinterpret_cast<Abi::CardManager>(base + 0x73d500);
    populate_card_original = reinterpret_cast<Abi::PopulateCard>(base + 0xa17bf0);
    write_original = reinterpret_cast<Abi::Serialize<Simulator::ISerializerWriteStream>>(base + 0x7a27c0);
    read_original = reinterpret_cast<Abi::Serialize<Simulator::ISerializerReadStream>>(base + 0x7a2830);
    event("relationship_bindings_checked", ",\"code_prefixes\":%u", unsigned(std::size(prefixes)));
    return true;
}

namespace {
// The identities every later check compares against: both relationship managers, both creatures and both players.
void bind_owner(void* root, void* primary, Animal* actor, uint64_t actor_id, uint64_t epoch, Manager* manager,
                Simulator::cPlayer* second) {
    secondary_manager = root;
    primary_manager = primary;
    b_actor = actor;
    b_id = actor_id;
    b_epoch = epoch;
    b_native_id = actor->mID;
    a_actor = manager->mpAvatar.get();
    a_native_id = a_actor->mID;
    a_player = manager->mpPlayer.get();
    a_player_id = a_player->mID;
    b_player = second;
    b_player_id = second->mID;
}

// A newly bound owner has no failure, no followed fight and no refused target.
void reset_fight_state() {
    retiring = identity_retired = false;
    failure = nullptr;
    failure_epoch = 0;
    experiment_primary = {};
    experiment_target = nullptr;
    experiment_target_id = 0;
    later_samples = 0;
    primary_active = false;
    experiment_after = {};
    experiment_history_after = {};
    experiment_kill_observed = false;
    rejected_targets = {};
    rejected_target_count = 0;
}

// Gives the second player a relationship manager and a history of its own. A restored owner adopts the root read
// from its checkpoint; a new one gets a manager built by the original constructor.
bool initialize_relationship_owner(Animal* actor, uint64_t actor_id, uint64_t epoch, bool restoring) {
    if (!on_thread() || !get_original || secondary_manager || !actor || !actor_id || !epoch || epoch == failure_epoch ||
        damage_depth || action_depth) {
        return false;
    }
    auto manager = Manager::Get();
    auto primary = get_original();
    auto second = native_second_player(epoch);
    if (!manager || !manager_layout(primary) || !current_animal(actor, actor->mID) || actor->mbDead ||
        !manager->mpAvatar || !manager->mpPlayer || !second || second == manager->mpPlayer.get() ||
        actor->mSpeciesKey == manager->mpAvatar->mSpeciesKey) {
        return false;
    }
    auto root = restoring ? (checkpoint_root && manager_layout(checkpoint_root) ? checkpoint_root : nullptr)
                          : construct_relationship_manager("SporeMP relationship context");
    if (!root) {
        return false;
    }
    if (restoring ? !adopt_native_history_checkpoint(actor_id, epoch) : !enable_native_history(actor_id, epoch)) {
        if (!restoring) {
            delete_original(root, 1);
        }
        return false;
    }
    if (restoring) {
        checkpoint_root = nullptr;
    }
    bind_owner(root, primary, actor, actor_id, epoch, manager, second);
    reset_fight_state();
    event(restoring ? "native_relationship_checkpoint_adopted" : "native_relationship_enabled",
          ",\"owner\":2,\"actor\":%llu,\"epoch\":%llu,\"allocation_bytes\":96,\"constructor_only\":%s,\"listener_"
          "registered\":false,\"replayed_actions\":0",
          b_id, b_epoch, restoring ? "false" : "true");
    return true;
}
} // namespace

bool enable_native_relationships(Animal* actor, uint64_t actor_id, uint64_t epoch) {
    return initialize_relationship_owner(actor, actor_id, epoch, false);
}

bool adopt_native_relationship_checkpoint(Animal* actor, uint64_t actor_id, uint64_t epoch) {
    return initialize_relationship_owner(actor, actor_id, epoch, true);
}

bool native_relationship_checkpoint_idle(uint64_t epoch) {
    network::HistoryState history;
    uint64_t hash = 0;
    uint32_t entries = 0;
    // The bounded foreign experiment uses the first native map only. Retain
    // the native writer for both maps, but refuse unqualified second-map data.
    return on_thread() && b_epoch == epoch && owner_current() && !failure && !damage_depth && !action_depth &&
           !checkpoint_root && !current_scope && !word<uint32_t>(secondary_manager, 0x4c) &&
           map_digest(secondary_manager, hash, entries) && capture_native_history(2, epoch, history);
}

bool write_native_relationship_checkpoint(uint64_t epoch, Simulator::ISerializerWriteStream* stream) {
    if (!stream || !stream->IsGood() || !native_relationship_checkpoint_idle(epoch)) {
        return false;
    }
    uint64_t hash = 0, after = 0, primary = 0, primary_after = 0;
    uint32_t entries = 0, after_entries = 0, p_entries = 0, p_after = 0;
    if (!map_digest(secondary_manager, hash, entries) || !map_digest(primary_manager, primary, p_entries)) {
        return false;
    }
    const uint32_t words[]{entries, static_cast<uint32_t>(hash), static_cast<uint32_t>(hash >> 32)};
    if (!IO::WriteUInt32(stream->GetRecord()->GetStream(), words, 3)) {
        return false;
    }
    write_original(static_cast<unsigned char*>(secondary_manager) + 4, stream);
    const bool unchanged = map_digest(secondary_manager, after, after_entries) && hash == after &&
                           entries == after_entries && map_digest(primary_manager, primary_after, p_after) &&
                           primary == primary_after && p_entries == p_after && !word<uint32_t>(secondary_manager, 0x4c);
    const bool valid = stream->IsGood() && unchanged;
    event("native_relationship_checkpoint_written",
          ",\"owner\":2,\"success\":%s,\"entries\":%u,\"hash\":%llu,\"primary_unchanged\":%s,\"replayed_actions\":0",
          valid ? "true" : "false", entries, hash, unchanged ? "true" : "false");
    return valid;
}

bool read_native_relationship_checkpoint(Simulator::ISerializerReadStream* stream) {
    if (!on_thread() || !stream || !stream->IsGood() || checkpoint_root || secondary_manager || damage_depth ||
        action_depth) {
        return false;
    }
    uint32_t words[3]{};
    if (!IO::ReadUInt32(stream->GetRecord()->GetStream(), words, 3) || words[0] > 65536) {
        return false;
    }
    auto root = allocate_original(0x60, "SporeMP relationship checkpoint", 0, 0, nullptr, 0);
    if (!root) {
        return false;
    }
    const bool constructed = construct_original(root) == root && manager_layout(root);
    if (constructed) {
        read_original(static_cast<unsigned char*>(root) + 4, stream);
    }
    uint64_t hash = 0;
    uint32_t entries = 0;
    const bool valid = constructed && stream->IsGood() && !word<uint32_t>(root, 0x4c) &&
                       map_digest(root, hash, entries) && words[0] == entries &&
                       hash == (uint64_t(words[1]) | (uint64_t(words[2]) << 32));
    if (valid) {
        checkpoint_root = root;
    } else {
        delete_original(root, 1);
    }
    event("native_relationship_checkpoint_read",
          ",\"owner\":2,\"success\":%s,\"entries\":%u,\"hash\":%llu,\"exact_state\":%s,\"replayed_actions\":0",
          valid ? "true" : "false", entries, hash, valid ? "true" : "false");
    return valid;
}

void discard_native_relationship_checkpoint() {
    if (on_thread() && checkpoint_root) {
        delete_original(checkpoint_root, 1);
        checkpoint_root = nullptr;
    }
    discard_native_history_checkpoint();
}

void retire_native_relationships() {
    if (on_thread() && secondary_manager) {
        retiring = true;
        dispose_if_idle();
    }
}

bool native_relationship_read(uint32_t owner, const ResourceKey& key, NativeRelationshipState& result) {
    result = {};
    if (!on_thread() || !get_original || (owner != 1 && owner != 2)) {
        return false;
    }
    return read_state(owner == 2 ? secondary_manager : get_original(), key, result);
}

bool native_relationship_preflight(Animal* source, uint64_t source_id, Animal* target, uint64_t target_id,
                                   uint64_t epoch) {
    const char* history_failure = nullptr;
    if (native_history_failure_ready(epoch, history_failure)) {
        fail(history_failure);
        return false;
    }
    if (!on_thread() || !owner_current() || failure || source != b_actor || source_id != b_id || epoch != b_epoch ||
        !target || !target_id || damage_depth || action_depth || target->mbDead ||
        (experiment_target_id && (experiment_target_id != target_id || experiment_target != target) &&
         !native_relationship_target_replaceable(damage_depth, action_depth, later_samples))) {
        return false;
    }
    if (experiment_target_id && (experiment_target_id != target_id || experiment_target != target)) {
        // The last fight is over or abandoned: follow this creature instead (up to 0.0.226 a session knew one).
        event("native_relationship_target_replaced",
              ",\"actor\":%llu,\"previous_native_id\":%u,\"previous_killed\":%s,\"target_native_id\":%u", b_id,
              experiment_target_native_id, experiment_kill_observed ? "true" : "false", target->mID);
        forget_followed_fight();
    }
    const auto sample = observe(target, target->mID);
    if (!sample.valid || !sample.branch) {
        bool seen = false;
        for (size_t i = 0; i < rejected_target_count; ++i) {
            seen = seen || rejected_targets[i] == target->mID;
        }
        if (!seen && rejected_target_count < rejected_targets.size()) {
            rejected_targets[rejected_target_count++] = target->mID;
            observation_event("preflight_rejected", ++next_serial, UINT32_MAX, target->mID, sample);
        }
        return false;
    }
    if (!experiment_target_id) {
        experiment_primary = primary_state();
        if (!experiment_primary.valid) {
            return false;
        }
        primary_active = false;
        experiment_target = target;
        experiment_target_id = target_id;
        experiment_target_native_id = target->mID;
        experiment_key = target->mSpeciesKey;
        experiment_serial = ++next_serial;
        observation_event("preflight", experiment_serial, UINT32_MAX, target->mID, sample);
        progress_event("preflight", experiment_serial, experiment_primary, true);
    }
    return true;
}

bool native_relationship_failure_ready(uint64_t epoch, const char*& reason) {
    const char* history_failure = nullptr;
    if (on_thread() && native_history_failure_ready(epoch, history_failure)) {
        fail(history_failure);
    }
    reason = nullptr;
    if (!on_thread() || epoch != failure_epoch || damage_depth || action_depth || !failure) {
        return false;
    }
    reason = failure;
    return true;
}

bool native_relationship_primary_input_ready(uint64_t epoch) {
    if (!on_thread() || !owner_current() || epoch != b_epoch || failure || damage_depth || action_depth) {
        return false;
    }
    // A keeps its original campaign state across successive native encounters.
    // Do not erase earned progress or force its existing history into B's
    // one-entry presentation snapshot. Later brain levels need qualification.
    const auto primary = primary_state();
    const bool ready = primary.valid && !primary.brain && primary.progress[2] > 0;
    // Player 1 may fight and eat while the second player's fight is followed (up to 0.0.234 it could not, and
    // trial342's player 1 stood under a pack's bites): what then changes for it between that player's blows is
    // its own.
    if (ready && experiment_target_id) {
        primary_active = true;
    }
    return ready;
}

bool native_relationship_secondary_input_ready(uint64_t epoch) {
    return on_thread() && owner_current() && epoch == b_epoch && !failure && !damage_depth && !action_depth;
}

namespace {
// Player 1's state against the followed fight's baseline, at a point outside the second player's own calls.
bool primary_steady(const PrimaryState& now, const char* phase) {
    if (!now.valid) {
        return false;
    }
    switch (native_relationship_primary_change(!same_primary(experiment_primary, now), false, primary_active)) {
    case PrimaryChange::none:
        return true;
    case PrimaryChange::own:
        event("native_relationship_primary_rebased",
              ",\"actor\":%llu,\"phase\":\"%s\",\"serial\":%llu,\"target_native_id\":%u", b_id, phase,
              experiment_serial, experiment_target_native_id);
        experiment_primary = now;
        return true;
    default:
        return false;
    }
}

void release_finished_fight() {
    const bool dead = experiment_target && experiment_target->mbDead;
    if (!experiment_target_id || failure ||
        !native_relationship_fight_finished(experiment_kill_observed, later_samples, dead)) {
        return;
    }
    event("native_relationship_fight_finished",
          ",\"actor\":%llu,\"target_native_id\":%u,\"serial\":%llu,\"killed_by_owner\":%s", b_id,
          experiment_target_native_id, experiment_serial, experiment_kill_observed ? "true" : "false");
    forget_followed_fight();
}
} // namespace

void invalidate_native_relationship_entity(uint32_t native_id) {
    if (!on_thread() || !secondary_manager) {
        return;
    }
    if (native_id == b_native_id || native_id == a_native_id || native_id == a_player_id || native_id == b_player_id) {
        identity_retired = true;
        fail("owned_identity_retired");
    }
    if (experiment_target && native_id == experiment_target_native_id) {
        // A completed native corpse may leave the world after the bounded
        // post-kill observations. Drop its borrowed identity; its retirement
        // cannot undo the owned relationship/history or block later movement.
        // The locked experiment target ID still prevents another combat here.
        experiment_target = nullptr;
        if (!experiment_kill_observed && !failure &&
            native_relationship_target_replaceable(damage_depth, action_depth, later_samples)) {
            // It left the world without the second player's kill: another killed it, or it went away.
            event("native_relationship_fight_finished",
                  ",\"actor\":%llu,\"target_native_id\":%u,\"serial\":%llu,\"killed_by_owner\":false", b_id, native_id,
                  experiment_serial);
            experiment_target_id = 0;
            experiment_target_native_id = UINT32_MAX;
            experiment_after = {};
            experiment_history_after = {};
            primary_active = false;
            return;
        }
        NativeRelationshipState state{};
        NativeHistoryDigest history{};
        const char* history_failure = nullptr;
        const auto primary = primary_state();
        const bool completed =
            experiment_kill_observed && !later_samples && !damage_depth && !action_depth && !failure &&
            !native_history_failure_ready(b_epoch, history_failure) && primary_steady(primary, "target_retired") &&
            native_relationship_read(2, experiment_key, state) && state.present == experiment_after.secondary.present &&
            state.words == experiment_after.secondary.words && native_history_read(2, history) &&
            history.categories == experiment_history_after.categories &&
            history.events == experiment_history_after.events && history.hash == experiment_history_after.hash;
        if (completed) {
            progress_event("completed_target_retired", experiment_serial, primary, true);
            event("native_relationship_completed_target_retired",
                  ",\"actor\":%llu,\"epoch\":%llu,\"target_native_id\":%u,"
                  "\"borrowed_target_released\":true,\"owned_results_preserved\":true",
                  b_id, b_epoch, native_id);
        } else {
            fail("observed_target_retired");
        }
    }
}

void sample_native_relationships(uint64_t epoch) {
    if (!on_thread() || epoch != b_epoch || damage_depth || action_depth || !secondary_manager) {
        return;
    }
    release_finished_fight();
    if (!later_samples || GetTickCount64() < next_sample) {
        return;
    }
    if (native_relationship_watch(later_samples, current_animal(b_actor, b_native_id) && b_actor->mbDead) ==
        RelationshipWatch::end) {
        event("native_relationship_watch_ended",
              ",\"actor\":%llu,\"reason\":\"owner_dead\",\"target_native_id\":%u,\"samples_left\":%u", b_id,
              experiment_target_native_id, later_samples);
        later_samples = 0;
        return;
    }
    --later_samples;
    next_sample = GetTickCount64() + 250;
    const auto now = primary_state();
    const bool unchanged = primary_steady(now, "later_frame");
    progress_event("later_frame", experiment_serial, now, unchanged);
    if (!unchanged) {
        fail("primary_changed_after_combat");
    }
    if (experiment_target) {
        auto sample = observe(experiment_target, experiment_target_native_id);
        observation_event("later_frame", experiment_serial, UINT32_MAX, experiment_target_native_id, sample);
        if (experiment_kill_observed) {
            NativeHistoryDigest history{};
            if (!sample.valid || sample.effective != experiment_after.effective ||
                sample.primary_effective != experiment_after.primary_effective ||
                sample.primary.present != experiment_after.primary.present ||
                sample.primary.words != experiment_after.primary.words ||
                sample.secondary.present != experiment_after.secondary.present ||
                sample.secondary.words != experiment_after.secondary.words || sample.species_flagged ||
                sample.herd_id != experiment_after.herd_id || sample.herd_flags != experiment_after.herd_flags ||
                sample.related_herds != experiment_after.related_herds ||
                sample.herd_digest != experiment_after.herd_digest || !native_history_read(2, history) ||
                history.categories != experiment_history_after.categories ||
                history.events != experiment_history_after.events || history.hash != experiment_history_after.hash) {
                fail("first_kill_state_changed_after_callback");
            }
        }
    }
    release_finished_fight();
}

bool native_relationship_replication_ready(uint64_t epoch) {
    return on_thread() && !native_replica_is_client() && secondary_manager && b_epoch == epoch && !retiring;
}

bool capture_native_relationships(uint64_t epoch, network::RelationshipFrame& frame) {
    frame.count = 0;
    frame.entries = {};
    return native_relationship_replication_ready(epoch) && !failure && !damage_depth && !action_depth &&
           Simulator::GetGameModeID() == kGameCreature && collect_relationships(secondary_manager, frame);
}

bool apply_native_relationships(uint64_t epoch, const network::RelationshipFrame& frame, bool& changed) {
    changed = false;
    if (!insert_original || !goal_owner(epoch) || damage_depth || action_depth || goal_call || goal_card ||
        !network::valid_relationships(frame.count, frame.entries)) {
        return false;
    }
    network::RelationshipFrame local;
    if (!collect_relationships(secondary_manager, local) || !network::relationships_cover(frame, local)) {
        return false;
    }
    uint64_t hash = 0;
    uint32_t entries = 0;
    if (!network::same_relationships(frame, local)) {
        auto wanted = frame;
        if (!native_replica_project(write_relationships, &wanted, false) ||
            !collect_relationships(secondary_manager, local) || !network::same_relationships(frame, local)) {
            return false;
        }
        changed = true;
        // The display copy is rebuilt from the new values on the next presentation.
        goal_presentation.next = 0;
    }
    if (!map_digest(secondary_manager, hash, entries)) {
        return false;
    }
    event("native_relationship_snapshot_applied",
          ",\"owner\":2,\"actor\":%llu,\"epoch\":%llu,\"entries\":%u,\"hash\":%llu,"
          "\"changed\":%s,\"exact\":true,\"replayed_actions\":0",
          b_id, b_epoch, entries, hash, changed ? "true" : "false");
    return true;
}

void present_native_relationship_goals(uint64_t epoch) {
    if (!on_thread() || !insert_original || !refresh_goal_original || !card_manager_original ||
        !populate_card_original || goal_call || goal_card) {
        return;
    }
    if (goal_presentation.root && (goal_presentation.epoch != epoch || !secondary_manager)) {
        discard_goal_presentation();
    }
    const auto now = GetTickCount64();
    if (now < goal_presentation.next) {
        return;
    }
    goal_presentation.next = now + 1000;
    if (!goal_owner(epoch) || damage_depth || action_depth || !copy_goal_source()) {
        return;
    }
    auto manager = Manager::Get();
    if (!manager) {
        return;
    }
    unsigned goals = 0, refreshed = 0;
    goal_cards = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || !goal_layout(&noun)) {
            continue;
        }
        ++goals;
        ResourceKey key;
        memcpy(&key, reinterpret_cast<const unsigned char*>(&noun) + 0x80, sizeof(key));
        NativeRelationshipState state;
        if (!read_state(goal_presentation.root, key, state)) {
            continue;
        }
        // Evaluate only a goal whose cached original values differ. The
        // original refresh then reads B's values and reloads its card props.
        if (same_goal(goal_cached(&noun), native_relationship_goal_values(state.present, state.words))) {
            continue;
        }
        if (native_replica_project(refresh_goal, &noun, false)) {
            ++refreshed;
        }
    }
    if (refreshed) {
        event("native_relationship_goal_display_refreshed",
              ",\"owner\":2,\"actor\":%llu,\"epoch\":%llu,\"goals\":%u,\"refreshed\":%u,\"cards_populated\":%u,"
              "\"presented_total\":%llu",
              b_id, epoch, goals, refreshed, goal_cards, goal_presentation.presented);
    }
}

NativeRelationshipGoalScope::NativeRelationshipGoalScope(void* goal) {
    if (!on_thread() || goal_call || !goal_presentation.root || !goal_owner(goal_presentation.epoch) ||
        !goal_layout(goal)) {
        return;
    }
    auto call = new GoalCall;
    call->goal = goal;
    memcpy(&call->key, static_cast<const unsigned char*>(goal) + 0x80, sizeof(call->key));
    NativeRelationshipState state;
    if (!read_state(goal_presentation.root, call->key, state)) {
        delete call;
        return;
    }
    call->present = state.present;
    call->before = goal_cached(goal);
    call->expected = native_relationship_goal_values(state.present, state.words);
    call->award = word<uint32_t>(goal, 0x9c);
    // C2EE80 rewards a changed count that reaches its threshold. A replica
    // never earns: seed the cached values the original is about to read, so
    // the display evaluation observes no transition.
    const auto bytes = static_cast<unsigned char*>(goal);
    memcpy(bytes + 0x8c, &call->expected.befriended, 4);
    memcpy(bytes + 0x90, &call->expected.killed, 4);
    memcpy(bytes + 0xa0, &call->expected.relation, 4);
    goal_call = call;
    entered_ = true;
}

void NativeRelationshipGoalScope::finish(uint32_t first_text_id, uint32_t second_text_id) {
    if (!entered_ || !goal_call) {
        return;
    }
    auto call = goal_call;
    goal_call = nullptr;
    entered_ = false;
    const auto after = goal_cached(call->goal);
    const bool exact = same_goal(after, call->expected) && word<uint32_t>(call->goal, 0x9c) == call->award;
    const bool changed = !same_goal(call->before, call->expected);
    if (changed || !exact || goal_presentation.presented < 16) {
        event("native_relationship_goal_presented",
              ",\"owner\":2,\"actor\":%llu,\"epoch\":%llu,\"species\":[%u,%u,%u],\"entry_present\":%s,"
              "\"before\":[%u,%u,%u],\"after\":[%u,%u,%u],\"extinction_threshold\":%u,\"befriend_threshold\":%u,"
              "\"first_text_id\":%u,\"second_text_id\":%u,\"exact\":%s,\"reward_executed\":false",
              b_id, goal_presentation.epoch, call->key.instanceID, uint32_t(call->key.typeID), call->key.groupID,
              call->present ? "true" : "false", call->before.befriended, call->before.killed, call->before.relation,
              after.befriended, after.killed, after.relation, word<uint32_t>(call->goal, 0xb0),
              word<uint32_t>(call->goal, 0xbc), first_text_id, second_text_id, exact ? "true" : "false");
    }
    ++goal_presentation.presented;
    if (!exact) {
        fail("goal_display_changed_original_result");
    }
    delete call;
}

NativeRelationshipGoalScope::~NativeRelationshipGoalScope() {
    finish(0, 0);
}

NativeRelationshipDamageScope::NativeRelationshipDamageScope(Animal* source, uint32_t owner, uint64_t source_id,
                                                             Animal* target, uint64_t target_id, uint64_t epoch) {
    if (!on_thread()) {
        return;
    }
    entered_ = true;
    previous_ = current_scope;
    current_scope = this;
    ++damage_depth;
    source_ = source;
    target_ = target;
    source_id_ = source_id;
    target_id_ = target_id;
    secondary_ = secondary_manager && source == b_actor && owner == 2 && source_id == b_id && epoch == b_epoch &&
                 owner_current();
    if (!secondary_) {
        return;
    }
    serial_ = ++next_serial;
    target_native_id_ = target ? target->mID : UINT32_MAX;
    const auto sample = observe(target_, target_native_id_);
    observation_event("damage_enter", serial_, UINT32_MAX, target_native_id_, sample);
    if (!experiment_target_id || target_id != experiment_target_id || target != experiment_target || !sample.valid ||
        !sample.branch) {
        fail("unexpected_native_damage_target_or_branch");
    }
    // What player 1 did or suffered since the last comparison is settled here, so that the comparison at the end
    // of this blow is of the blow alone.
    const auto primary = primary_state();
    progress_event("damage_enter", serial_, primary, primary_steady(primary, "damage_enter"));
}

NativeRelationshipDamageScope::~NativeRelationshipDamageScope() {
    if (!entered_) {
        return;
    }
    if (secondary_) {
        const auto sample = observe(target_, target_native_id_);
        observation_event("damage_return", serial_, UINT32_MAX, target_native_id_, sample);
        const auto primary = primary_state();
        const bool unchanged = same_primary(experiment_primary, primary);
        progress_event("damage_return", serial_, primary, unchanged);
        if (!unchanged) {
            fail("primary_changed_during_damage");
        }
        watch_later_frames();
    } else if (experiment_target_id && a_actor && (source_ == a_actor || target_ == a_actor)) {
        primary_active = true;
    }
    current_scope = previous_;
    --damage_depth;
    dispose_if_idle();
}
} // namespace sporemp
