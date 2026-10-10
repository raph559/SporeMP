#include "native_quiescence_probe.h"
#include "native_actors.h"
#include "native_player_context.h"
#include "native_persistence.h"
#include "native_replica.h"
#include "native_award_context.h"
#include "native_network.h"
#include "detour_transaction.h"
#include <Spore/Simulator/SubSystem/GameTimeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Spore/Simulator/cCollectableItems.h>
#include <Spore/Simulator/cHerd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <intrin.h>
#include <vector>

namespace sporemp {
namespace {
using Time = Simulator::cGameTimeManager;
using Pause = Simulator::TimeManagerPause;
using Nouns = Simulator::cGameNounManager;
using Animal = Simulator::cCreatureAnimal;
using Player = Simulator::cPlayer;
using Stage = Simulator::cCreatureGameData;
using Items = Simulator::cCollectableItems;
using TimeCall = int(__thiscall*)(Time*, Pause);
using DeltaCall = int(__thiscall*)(Time*, int);
constexpr auto gameplay = Pause::Gameplay;
static_assert(static_cast<uint32_t>(gameplay) == 0x04bf38a7 && sizeof(Time) == 0x80 && sizeof(Stage) == 0x50,
              "Pinned original pause and stage layouts");
// SDK GameNounIDs::kPlayer.
constexpr uint32_t player_noun = 0x02c21781;
// The creature's unnamed general-flag bit that the original camera fade D2D7D0 sets and clears; see
// capture_native_alpha.
constexpr uint32_t camera_fade_flag = 0x800u;
// Milliseconds since the pause was acquired, after which settling is given up: no zero delta from the original
// update, and a camera transparency that has not settled.
constexpr uint64_t zero_delta_timeout_ms = 5000, transparency_timeout_ms = 10000;
// The continuous time the native state must stay unchanged to be reported as observed.
constexpr uint64_t observation_window_ms = 3000;
uintptr_t base = 0, image_end = 0, owned_manager = 0;
DWORD engine_thread = 0;
NativeQuiescenceEvent sink = nullptr;
TimeCall pause_original = nullptr, resume_original = nullptr, count_original = nullptr;
DeltaCall delta_original = nullptr;
bool initialized = false, hooks_attached = false, busy = false, ownership_ambiguous = false;
bool authenticated_hold = false;
bool presentation_wait_reported = false;
NativeQuiescenceStatus status;
worker::Generation generation{};
uint64_t acquired_app = 0, begin_ms = 0, acquired_ms = 0, zero_at_begin = 0, nonzero_at_begin = 0;
uint64_t zero_deltas = 0, nonzero_deltas = 0, foreign_pause_calls = 0;
int last_native_delta = 0;

struct Snapshot {
    std::vector<std::array<uint32_t, 2>> nouns;
    std::vector<std::array<uint32_t, 3>> item_statuses;
    std::vector<std::array<uint32_t, 2>> unlocked;
    std::vector<uint32_t> actor;
    std::array<uint32_t, 8> player{};
    std::array<unsigned char, sizeof(Stage)> stage{};
    std::vector<uint32_t> secondary;
    std::vector<std::array<uint32_t, 3>> secondary_statuses;
    std::vector<std::array<uint32_t, 2>> secondary_unlocked;
    std::array<uint32_t, 20> secondary_stage{};
    uintptr_t secondary_target = 0, secondary_combat_target = 0;
    uintptr_t target = 0, combat_target = 0;
    std::array<uint32_t, 2> alpha_bits{};
    std::array<bool, 2> alpha_readable{};
    bool presentation_settled = false;
    uint64_t frame_clock = 0; // Observation only: not assumed to be simulation time.
};

Snapshot begin;
std::vector<std::array<int32_t, 2>> begin_pauses;

bool on_thread() {
    return initialized && GetCurrentThreadId() == engine_thread;
}

uint32_t bits(float value) {
    uint32_t word;
    memcpy(&word, &value, 4);
    return word;
}

float floating(uint32_t value) {
    float result;
    memcpy(&result, &value, 4);
    return result;
}

const char* phase_name(NativeQuiescencePhase phase) {
    switch (phase) {
    case NativeQuiescencePhase::idle:
        return "idle";
    case NativeQuiescencePhase::settling:
        return "settling";
    case NativeQuiescencePhase::observing:
        return "observing";
    case NativeQuiescencePhase::measured:
        return "measured";
    case NativeQuiescencePhase::rejected:
        return "rejected";
    case NativeQuiescencePhase::released:
        return "released";
    }
    return "unknown";
}

void event(const char* name, const char* reason = "") {
    if (!sink) {
        return;
    }
    char fields[1600]{};
    sprintf_s(
        fields,
        ",\"request\":%llu,\"pause_epoch\":%llu,\"input_epoch\":%llu,\"phase\":\"%s\",\"reason\":\"%s\","
        "\"owns_pause\":%s,\"elapsed_ms\":%llu,\"app_begin\":%llu,\"app_end\":%llu,\"ai_begin\":%llu,\"ai_end\":%llu,"
        "\"zero_native_deltas\":%llu,\"nonzero_native_deltas\":%llu,\"last_native_delta\":%d,\"foreign_pause_calls\":%"
        "llu,"
        "\"player_native_id\":%u,\"avatar_native_id\":%u,\"nouns\":%u,\"gameplay_count\":%d,\"pause_flags\":%d,"
        "\"experimental\":true,\"native_world_idle_qualified\":false,\"transfer_qualified\":false",
        status.request, status.epoch, status.input_epoch, phase_name(status.phase), reason,
        status.owns_pause ? "true" : "false", status.elapsed_ms, status.app_begin, status.app_end, status.ai_begin,
        status.ai_end, status.zero_native_deltas, status.nonzero_native_deltas, last_native_delta, foreign_pause_calls,
        status.player_native_id, status.avatar_native_id, status.nouns, status.gameplay_count, status.pause_flags);
    sink(name, fields);
}

void reject(const char* reason) {
    const bool first = status.phase != NativeQuiescencePhase::rejected;
    status.phase = NativeQuiescencePhase::rejected;
    status.ready = false;
    if (first) {
        event("native_quiescence_rejected", reason);
    }
}

[[noreturn]] void fatal(const char* reason) {
    reject(reason);
    TerminateProcess(GetCurrentProcess(), 0x4d11);
    __fastfail(7);
}

bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto at = reinterpret_cast<uintptr_t>(address), start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return at >= start && size <= info.RegionSize && at - start <= info.RegionSize - size;
}

bool capture_native_alpha(Animal* animal, uint32_t& alpha) {
    // Pinned SDK cbf9206: AnimatedCreature.mColor +74, ColorRGBA.a +0c.
    // Original camera fade D2D7D0 writes this +80 value while setting/clearing
    // the creature's unnamed general-flag bit 800. Read only; no fade override.
    const auto animated = animal ? animal->mpAnimatedCreature.get() : nullptr;
    if (!readable(animated, 0x84) ||
        reinterpret_cast<uintptr_t>(&animated->mColor.a) - reinterpret_cast<uintptr_t>(animated) != 0x80) {
        return false;
    }
    const auto value = animated->mColor.a;
    if (!std::isfinite(value) || value < 0 || value > 1) {
        return false;
    }
    alpha = bits(value);
    return true;
}

bool supervised_context() {
    wchar_t value[64]{}, parent[64]{};
    worker::Generation current{};
    return !native_replica_is_client() &&
           GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", parent, _countof(parent)) > 0 &&
           GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", value, _countof(value)) > 0 &&
           worker::generation_from_hex(value, current) && current == generation;
}

bool developer_context() {
    wchar_t network[2]{};
    return supervised_context() && !GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", network, _countof(network));
}

bool held_context() {
    return authenticated_hold ? supervised_context() && native_network_transfer_context(status.request, status.epoch)
                              : developer_context();
}

Time* time_manager() {
    auto manager = Time::Get();
    if (!readable(manager, sizeof(Time)) || *reinterpret_cast<uintptr_t*>(manager) != base + 0x1060310 ||
        manager->mPauseTypes.empty() || manager->mPauseTypes.size() > 32) {
        return nullptr;
    }
    const auto found = manager->mPauseMaps.find(gameplay);
    if (found == manager->mPauseMaps.end() || found->second < 0 ||
        static_cast<size_t>(found->second) >= manager->mPauseTypes.size() ||
        manager->mPauseTypes[found->second].flags != 3) {
        return nullptr;
    }
    return manager;
}

std::vector<std::array<int32_t, 2>> pause_values(Time* manager) {
    std::vector<std::array<int32_t, 2>> result;
    for (const auto& value : manager->mPauseTypes) {
        result.push_back({value.counter, value.flags});
    }
    return result;
}

// A snapshot is taken only in the loaded Creature scene of the held epoch. A developer hold admits one player.
bool scene_refuses_snapshot(const worker::Message& current) {
    return current.epoch != status.epoch || current.values[0] != static_cast<uint64_t>(worker::Phase::scene) ||
           !current.values[3] || (!authenticated_hold && (current.values[4] || native_second_player(status.epoch))) ||
           native_persistence_snapshot().state != NativePersistenceState::loaded || Simulator::IsLoadingGameMode() ||
           Simulator::GetGameModeID() != kGameCreature;
}

// The primary player and its avatar must be the bound, live ones, with the pinned stage data behind them.
bool primary_refuses_snapshot(Nouns* manager, Time* time, Player* player, Animal* actor) {
    uint32_t bound = 0;
    return !manager || !time || !player || !actor || !native_actor_owner_native_id(1, bound) || bound != actor->mID ||
           !validate_native_player_checkpoint(player, player->mID) || actor->mbDead || actor->mbMarkedForDeletion ||
           !actor->mHerd || !actor->field_E84 || !actor->mpSpeciesProfile ||
           reinterpret_cast<uintptr_t>(Stage::Get()) != base + 0x129e370;
}

// The identifier and kind of every noun, sorted. The only player nouns may be the primary and the secondary, the
// primary avatar must be there once, and no identifier may repeat. players receives the number of player nouns.
bool census_nouns(Nouns* manager, Player* player, Player* secondary, Animal* actor, Snapshot& out, unsigned& players) {
    unsigned actors = 0;
    for (auto& noun : manager->mNouns) {
        if (out.nouns.size() >= 100000) {
            return false;
        }
        out.nouns.push_back({noun.mID, noun.GetNounID()});
        if (noun.GetNounID() == player_noun) {
            if (&noun != static_cast<Simulator::cGameData*>(player) &&
                &noun != static_cast<Simulator::cGameData*>(secondary)) {
                return false;
            }
            ++players;
        }
        if (&noun == static_cast<Simulator::cGameData*>(actor)) {
            ++actors;
        }
    }
    std::sort(out.nouns.begin(), out.nouns.end());
    if (players != (secondary ? 2u : 1u) || actors != 1 || out.nouns.size() > 100000) {
        return false;
    }
    for (size_t i = 1; i < out.nouns.size(); ++i) {
        if (out.nouns[i - 1][0] == out.nouns[i][0]) {
            return false;
        }
    }
    return true;
}

bool all_finite(std::initializer_list<float> values) {
    for (float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

// The 26 words of one creature that a held world must keep: identity, herd, species, pose, vitals, motion,
// general flags, current attack and the size of field_1F4. Snapshot::actor is these words and three words of
// ability bits. Snapshot::secondary is the player's two identifiers, these words, four words of progress, three
// words of ability bits and the unlock points.
void append_actor_words(std::vector<uint32_t>& words, Animal* actor, const Math::Vector3& position,
                        const Math::Quaternion& orientation) {
    words.insert(words.end(), {actor->mID,
                               actor->mHerd->mID,
                               actor->mSpeciesKey.instanceID,
                               actor->mSpeciesKey.typeID,
                               actor->mSpeciesKey.groupID,
                               bits(position.x),
                               bits(position.y),
                               bits(position.z),
                               bits(orientation.x),
                               bits(orientation.y),
                               bits(orientation.z),
                               bits(orientation.w),
                               bits(actor->mHealthPoints),
                               bits(actor->mEnergy),
                               bits(actor->mHunger),
                               bits(actor->mVelocity.x),
                               bits(actor->mVelocity.y),
                               bits(actor->mVelocity.z),
                               bits(actor->mAngularVelocity.x),
                               bits(actor->mAngularVelocity.y),
                               bits(actor->mAngularVelocity.z),
                               bits(actor->mDesiredSpeed),
                               bits(actor->mInTransitTime),
                               static_cast<uint32_t>(actor->mGeneralFlags),
                               static_cast<uint32_t>(actor->mCurrentAttackIdx),
                               static_cast<uint32_t>(actor->field_1F4.size())});
}

// The in-use ability bits of a creature, packed into the three words that start at first_word.
void pack_ability_bits(Animal* actor, std::vector<uint32_t>& words, size_t first_word) {
    for (size_t i = 0; i < 88; ++i) {
        if (actor->mInUseAbilityBits.test(i)) {
            words[first_word + (i >> 5)] |= 1u << (i & 31);
        }
    }
}

bool items_refused(Items* items) {
    return !items || items->mItemStatusInfos.size() > 1024 || items->mUnlockedItems.size() > 1024;
}

// The status of every collectable item, sorted, and the unlocked items in their original order.
void capture_items(Items* items, std::vector<std::array<uint32_t, 3>>& statuses,
                   std::vector<std::array<uint32_t, 2>>& unlocked) {
    for (const auto& value : items->mItemStatusInfos) {
        statuses.push_back({value.first.instanceID, value.first.groupID, value.second});
    }
    std::sort(statuses.begin(), statuses.end());
    for (const auto& value : items->mUnlockedItems) {
        unlocked.push_back({value.instanceID, value.groupID});
    }
}

bool capture_primary_actor(Animal* actor, Snapshot& out) {
    const auto position = actor->GetPosition();
    const auto orientation = actor->GetOrientation();
    if (!all_finite({position.x, position.y, position.z, orientation.x, orientation.y, orientation.z, orientation.w,
                     actor->mHealthPoints, actor->mEnergy, actor->mHunger, actor->mDesiredSpeed,
                     actor->mInTransitTime})) {
        return false;
    }
    append_actor_words(out.actor, actor, position, orientation);
    out.actor.insert(out.actor.end(), {0, 0, 0});
    pack_ability_bits(actor, out.actor, 26);
    out.target = reinterpret_cast<uintptr_t>(actor->GetTarget());
    out.combat_target = reinterpret_cast<uintptr_t>(actor->mpCombatantTarget);
    out.alpha_readable[0] = capture_native_alpha(actor, out.alpha_bits[0]);
    return true;
}

// The primary player's collection, progress and stage data. players is the census count kept with them.
bool capture_primary_player(Player* player, unsigned players, Snapshot& out) {
    auto items = player->mpCRGItems.get();
    if (items_refused(items)) {
        return false;
    }
    capture_items(items, out.item_statuses, out.unlocked);
    out.player = {player->mID,
                  player->mUniqueGameID,
                  bits(player->mCurrentGoalProgress),
                  bits(player->mGoalProgressTotal),
                  bits(player->mSocialTraitProgress),
                  bits(player->mCombatTraitProgress),
                  static_cast<uint32_t>(items->mUnlockPoints),
                  players};
    memcpy(out.stage.data(), Stage::Get(), out.stage.size());
    return true;
}

// The one noun with a native identifier, as a creature. Null when it is absent, repeated or not a creature.
Animal* unique_animal(Nouns* manager, uint32_t native_id) {
    Animal* found = nullptr;
    for (auto& noun : manager->mNouns) {
        if (noun.mID == native_id) {
            if (found || noun.GetNounID() != Animal::NOUN_ID) {
                return nullptr;
            }
            found = object_cast<Animal>(&noun);
        }
    }
    return found;
}

// The second player of an authenticated hold: its avatar, its progress, its collection and its awards.
bool capture_secondary(Nouns* manager, Player* secondary, Animal* actor, Snapshot& out) {
    uint32_t secondary_id = 0;
    if (!native_actor_owner_native_id(2, secondary_id) ||
        !validate_native_player_checkpoint(secondary, secondary->mID)) {
        return false;
    }
    Animal* second_actor = unique_animal(manager, secondary_id);
    if (!second_actor || second_actor == actor || second_actor->mbDead || second_actor->mbMarkedForDeletion ||
        !second_actor->mHerd || !second_actor->field_E84 || !second_actor->mpSpeciesProfile) {
        return false;
    }
    const auto position = second_actor->GetPosition();
    const auto orientation = second_actor->GetOrientation();
    if (!all_finite({position.x, position.y, position.z, orientation.x, orientation.y, orientation.z, orientation.w,
                     second_actor->mHealthPoints, second_actor->mEnergy, second_actor->mHunger})) {
        return false;
    }
    out.secondary = {secondary->mID, secondary->mUniqueGameID};
    append_actor_words(out.secondary, second_actor, position, orientation);
    out.secondary.insert(out.secondary.end(),
                         {bits(secondary->mCurrentGoalProgress), bits(secondary->mGoalProgressTotal),
                          bits(secondary->mSocialTraitProgress), bits(secondary->mCombatTraitProgress), 0, 0, 0});
    pack_ability_bits(second_actor, out.secondary, 32);
    out.secondary_target = reinterpret_cast<uintptr_t>(second_actor->GetTarget());
    out.secondary_combat_target = reinterpret_cast<uintptr_t>(second_actor->mpCombatantTarget);
    out.alpha_readable[1] = capture_native_alpha(second_actor, out.alpha_bits[1]);
    auto second_items = secondary->mpCRGItems.get();
    if (items_refused(second_items)) {
        return false;
    }
    out.secondary.push_back(static_cast<uint32_t>(second_items->mUnlockPoints));
    capture_items(second_items, out.secondary_statuses, out.secondary_unlocked);
    NativeAwardCheckpoint stage;
    if (!snapshot_native_awards(stage, status.epoch)) {
        return false;
    }
    out.secondary_stage = stage.words;
    return true;
}

// The native state that must stay unchanged while this probe holds the gameplay pause. False when the scene or a
// player is not in a state that can be compared.
bool snapshot(Snapshot& out) {
    out = {};
    const auto current = native_actor_worker_status();
    if (scene_refuses_snapshot(current)) {
        return false;
    }
    auto manager = Nouns::Get();
    auto time = time_manager();
    auto player = manager ? manager->GetPlayer() : nullptr;
    auto actor = manager ? manager->GetAvatar() : nullptr;
    auto secondary = authenticated_hold ? native_second_player(status.epoch) : nullptr;
    if (bool(current.values[4]) != bool(secondary)) {
        return false;
    }
    if (primary_refuses_snapshot(manager, time, player, actor)) {
        return false;
    }
    unsigned players = 0;
    if (!census_nouns(manager, player, secondary, actor, out, players) || !capture_primary_actor(actor, out) ||
        !capture_primary_player(player, players, out)) {
        return false;
    }
    if (secondary && !capture_secondary(manager, secondary, actor, out)) {
        return false;
    }
    // This is a stricter prerequisite for beginning the full unchanged window,
    // not an exemption from comparing general flags. A camera fade that never
    // becomes opaque must time out rather than being modified or ignored.
    out.presentation_settled =
        out.alpha_readable[0] && out.alpha_bits[0] == bits(1.0f) && !(out.actor[23] & camera_fade_flag) &&
        (out.secondary.empty() ||
         (out.alpha_readable[1] && out.alpha_bits[1] == bits(1.0f) && !(out.secondary[25] & camera_fade_flag)));
    out.frame_clock = static_cast<uint64_t>(time->mTimeAtStartOfFrame.QuadPart);
    return true;
}

const char* difference(const Snapshot& a, const Snapshot& b) {
    if (a.nouns != b.nouns) {
        return "native_noun_census_changed";
    }
    if (a.actor != b.actor || a.target != b.target || a.combat_target != b.combat_target) {
        return "native_primary_actor_changed";
    }
    if (a.player != b.player || a.item_statuses != b.item_statuses || a.unlocked != b.unlocked) {
        return "native_primary_player_changed";
    }
    if (a.stage != b.stage) {
        return "native_primary_stage_changed";
    }
    if (a.secondary != b.secondary || a.secondary_target != b.secondary_target ||
        a.secondary_combat_target != b.secondary_combat_target) {
        return "native_secondary_actor_or_player_changed";
    }
    if (a.secondary_statuses != b.secondary_statuses || a.secondary_unlocked != b.secondary_unlocked ||
        a.secondary_stage != b.secondary_stage) {
        return "native_secondary_collection_or_stage_changed";
    }
    return nullptr;
}

void snapshot_event(const char* name, const Snapshot& value) {
    if (!sink || value.actor.size() != 29) {
        return;
    }
    char fields[1400]{};
    sprintf_s(
        fields,
        ",\"request\":%llu,\"pause_epoch\":%llu,\"player_native_id\":%u,\"avatar_native_id\":%u,"
        "\"position\":[%.9g,%.9g,%.9g],\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,"
        "\"desired_speed_bits\":%u,\"transit_bits\":%u,\"ability_words\":[%u,%u,%u],\"target_present\":%s,"
        "\"combat_target_present\":%s,\"noun_count\":%zu,\"players\":%u,\"item_statuses\":%zu,\"unlocked_items\":%zu,"
        "\"secondary_player_native_id\":%u,\"secondary_avatar_native_id\":%u,\"secondary_flags\":%u,"
        "\"primary_flags\":%u,\"primary_alpha_bits\":%u,\"secondary_alpha_bits\":%u,"
        "\"primary_alpha_readable\":%s,\"secondary_alpha_readable\":%s,\"presentation_settled\":%s,"
        "\"frame_clock_observed\":%llu,\"frame_clock_used_as_simulation_gate\":false,\"experimental\":true",
        status.request, status.epoch, value.player[0], value.actor[0], double(floating(value.actor[5])),
        double(floating(value.actor[6])), double(floating(value.actor[7])), double(floating(value.actor[12])),
        double(floating(value.actor[13])), double(floating(value.actor[14])), value.actor[21], value.actor[22],
        value.actor[26], value.actor[27], value.actor[28], value.target ? "true" : "false",
        value.combat_target ? "true" : "false", value.nouns.size(), value.player[7], value.item_statuses.size(),
        value.unlocked.size(), value.secondary.empty() ? 0u : value.secondary[0],
        value.secondary.empty() ? 0u : value.secondary[2], value.secondary.empty() ? 0u : value.secondary[25],
        value.actor[23], value.alpha_bits[0], value.alpha_bits[1], value.alpha_readable[0] ? "true" : "false",
        value.alpha_readable[1] ? "true" : "false", value.presentation_settled ? "true" : "false", value.frame_clock);
    sink(name, fields);
}

int __fastcall delta_hook(Time* self, void*, int milliseconds) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const int result = delta_original(self, milliseconds);
    if (on_thread() && status.owns_pause && reinterpret_cast<uintptr_t>(self) == owned_manager &&
        caller == base + 0x945827) {
        last_native_delta = result;
        if (result == 0) {
            ++zero_deltas;
        } else {
            ++nonzero_deltas;
        }
    }
    return result;
}

int __fastcall pause_hook(Time* self, void*, Pause type) {
    const int result = pause_original(self, type);
    if (on_thread() && status.owns_pause && reinterpret_cast<uintptr_t>(self) == owned_manager) {
        ++foreign_pause_calls;
        reject("foreign_pause_during_observation");
    }
    return result;
}

int __fastcall resume_hook(Time* self, void*, Pause type) {
    const bool ours = on_thread() && status.owns_pause && reinterpret_cast<uintptr_t>(self) == owned_manager;
    const int before = ours && type == gameplay ? count_original(self, type) : 0;
    const int result = resume_original(self, type);
    if (ours) {
        ++foreign_pause_calls;
        if (type == gameplay && (before <= 1 || result != before - 1)) {
            ownership_ambiguous = true;
            status.owns_pause = false;
        }
        reject("foreign_resume_during_observation");
    }
    return result;
}

NativeHook hooks[] = {{reinterpret_cast<void**>(&pause_original), reinterpret_cast<void*>(pause_hook)},
                      {reinterpret_cast<void**>(&resume_original), reinterpret_cast<void*>(resume_hook)},
                      {reinterpret_cast<void**>(&delta_original), reinterpret_cast<void*>(delta_hook)}};

bool prefix(uint32_t rva, const unsigned char* bytes, size_t length) {
    return image_end > base && rva <= image_end - base && length <= image_end - base - rva &&
           !memcmp(reinterpret_cast<const void*>(base + rva), bytes, length);
}
} // namespace

bool initialize_native_quiescence_probe(uintptr_t image_base, uintptr_t end, DWORD thread,
                                        NativeQuiescenceEvent callback) {
    if (initialized || GetCurrentThreadId() != thread || end <= image_base) {
        return false;
    }
    base = image_base;
    image_end = end;
    engine_thread = thread;
    sink = callback;
    wchar_t value[64]{};
    if (!GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", value, _countof(value)) ||
        !worker::generation_from_hex(value, generation) || !supervised_context()) {
        return false;
    }
    const unsigned char pause_bytes[] = {0x56, 0x57, 0x8b, 0xf1, 0x8d, 0x44, 0x24, 0x0c, 0x50, 0x8d, 0x4e, 0x4c, 0xe8,
                                         0xbf, 0x16, 0xb1, 0xff, 0x8b, 0x4e, 0x68, 0x8b, 0x00, 0xff, 0x04, 0xc1};
    const unsigned char resume_bytes[] = {0x56, 0x57, 0x8b, 0xf1, 0x8d, 0x44, 0x24, 0x0c, 0x50, 0x8d, 0x4e, 0x4c, 0xe8,
                                          0x8f, 0x16, 0xb1, 0xff, 0x8b, 0x4e, 0x68, 0x8b, 0x00, 0xff, 0x0c, 0xc1};
    const unsigned char count_bytes[] = {0x51, 0x56, 0x8b, 0xf1, 0x8d, 0x44, 0x24, 0x0c, 0x50,
                                         0x8d, 0x4c, 0x24, 0x08, 0x51, 0x8d, 0x4e, 0x4c};
    const unsigned char delta_bytes[] = {0x83, 0xec, 0x14, 0xf6, 0x41, 0x48, 0x01, 0xb8, 0x64, 0, 0, 0};
    const unsigned char call_bytes[] = {0xe8, 0x79, 0xc5, 0xde, 0xff};
    unsigned char get_bytes[] = {0xa1, 0, 0, 0, 0, 0xc3};
    const auto address = static_cast<uint32_t>(base + 0x127eb04);
    memcpy(get_bytes + 1, &address, 4);
    if (!prefix(0x732330, pause_bytes, sizeof(pause_bytes)) || !prefix(0x732360, resume_bytes, sizeof(resume_bytes)) ||
        !prefix(0x7321e0, count_bytes, sizeof(count_bytes)) || !prefix(0x731da0, delta_bytes, sizeof(delta_bytes)) ||
        !prefix(0x73d480, get_bytes, sizeof(get_bytes)) || !prefix(0x945822, call_bytes, sizeof(call_bytes))) {
        return false;
    }
    pause_original = reinterpret_cast<TimeCall>(base + 0x732330);
    resume_original = reinterpret_cast<TimeCall>(base + 0x732360);
    count_original = reinterpret_cast<TimeCall>(base + 0x7321e0);
    delta_original = reinterpret_cast<DeltaCall>(base + 0x731da0);
    if (change_hooks(hooks, _countof(hooks), true) != NO_ERROR) {
        return false;
    }
    hooks_attached = true;
    initialized = true;
    event("native_quiescence_bindings_checked");
    return true;
}

namespace {
// A new settling period under the held pause: the delta counts and the clocks of the previous one are discarded.
void restart_settling(uint64_t app_ticks) {
    zero_deltas = nonzero_deltas = zero_at_begin = nonzero_at_begin = 0;
    last_native_delta = 0;
    acquired_app = app_ticks;
    acquired_ms = GetTickCount64();
    begin_ms = 0;
    presentation_wait_reported = false;
}

// The one original Pause call of a hold, and the check that it added exactly one gameplay pause and changed no
// other counter.
worker::Result take_gameplay_pause(Time* time, const std::vector<std::array<int32_t, 2>>& original_pauses,
                                   uint64_t epoch, uint64_t app_ticks) {
    using Result = worker::Result;
    owned_manager = reinterpret_cast<uintptr_t>(time);
    status.owns_pause = true;
    status.phase = NativeQuiescencePhase::settling;
    foreign_pause_calls = 0;
    restart_settling(app_ticks);
    busy = true;
    const int result = pause_original(time, gameplay);
    busy = false;
    status.gameplay_count = count_original(time, gameplay);
    status.pause_flags = time->mFlags;
    auto expected = original_pauses;
    const auto index = static_cast<size_t>(time->mPauseMaps.find(gameplay)->second);
    ++expected[index][0];
    if (ownership_ambiguous || !status.owns_pause || result != 1 || status.gameplay_count != 1 ||
        (time->mFlags & 1) == 0 || pause_values(time) != expected || status.phase == NativeQuiescencePhase::rejected ||
        native_actor_worker_status().epoch != epoch) {
        reject("original_pause_ack_mismatch");
        return Result::failed;
    }
    event("native_quiescence_pause_ack", "original_pause_returned_one_measurement_pending");
    return Result::accepted;
}

// Admission of a hold, in order: the caller's context, a hold already owned, the scene epoch, a time manager
// with no gameplay pause, a comparable scene, then the input fence. Only then is the original paused.
worker::Result acquire_pause(uint64_t request, uint64_t epoch, uint64_t app_ticks, bool authenticated) {
    using Result = worker::Result;
    if (!on_thread() || busy || !request || !epoch || !app_ticks ||
        (authenticated ? !supervised_context() || !native_network_transfer_context(request, epoch)
                       : !developer_context())) {
        return Result::unavailable;
    }
    if (status.owns_pause) {
        if (status.request != request || status.epoch != epoch) {
            return Result::busy;
        }
        return status.phase == NativeQuiescencePhase::rejected ? Result::failed : Result::accepted;
    }
    if (ownership_ambiguous) {
        return Result::failed;
    }
    if (native_actor_worker_status().epoch != epoch) {
        return Result::stale;
    }
    auto time = time_manager();
    if (!time || count_original(time, gameplay) != 0 || time->mFlags != 0) {
        return Result::unavailable;
    }
    status = {};
    status.request = request;
    status.epoch = epoch;
    status.app_end = app_ticks;
    authenticated_hold = authenticated;
    Snapshot initial;
    if (!snapshot(initial)) {
        event("native_quiescence_pause_rejected", "single_primary_context");
        return Result::unavailable;
    }
    const auto original_pauses = pause_values(time);
    const auto input_epoch = native_actor_fence_network_inputs("native_quiescence_probe");
    if (!input_epoch) {
        return Result::failed;
    }
    status.input_epoch = input_epoch;
    return take_gameplay_pause(time, original_pauses, epoch, app_ticks);
}

} // namespace

worker::Result request_native_quiescence_pause(uint64_t request, uint64_t epoch, uint64_t app_ticks) {
    return acquire_pause(request, epoch, app_ticks, false);
}

worker::Result request_native_transfer_quiescence(uint64_t request, uint64_t epoch, uint64_t app_ticks) {
    return acquire_pause(request, epoch, app_ticks, true);
}

bool restart_native_transfer_observation(uint64_t request, uint64_t epoch, uint64_t app_ticks) {
    if (!on_thread() || busy || !authenticated_hold || !held_context() || !status.owns_pause || ownership_ambiguous ||
        request != status.request || epoch != status.epoch || !app_ticks ||
        native_actor_worker_status().epoch != epoch) {
        return false;
    }
    auto time = time_manager();
    if (!time || reinterpret_cast<uintptr_t>(time) != owned_manager || count_original(time, gameplay) != 1 ||
        (time->mFlags & 1) == 0) {
        return false;
    }
    status.ready = false;
    status.phase = NativeQuiescencePhase::settling;
    status.elapsed_ms = status.app_begin = status.ai_begin = 0;
    status.zero_native_deltas = status.nonzero_native_deltas = 0;
    restart_settling(app_ticks);
    event("native_quiescence_reobserve", "authorized_native_state_change_requires_new_window");
    return true;
}

namespace {
// The pause this probe holds must still be its own: the same context, scene and time manager, counted exactly
// once and in force. False after recording why it is not.
bool owned_pause_intact(const worker::Message& worker_status, Time* time) {
    if (!held_context() || worker_status.epoch != status.epoch || !time ||
        reinterpret_cast<uintptr_t>(time) != owned_manager) {
        reject("pause_owner_lifecycle_changed");
        return false;
    }
    status.gameplay_count = count_original(time, gameplay);
    status.pause_flags = time->mFlags;
    if (status.gameplay_count < 1) {
        ownership_ambiguous = true;
        status.owns_pause = false;
        reject("owned_pause_consumed");
        return false;
    }
    if (status.gameplay_count != 1 || (time->mFlags & 1) == 0) {
        reject("native_pause_state_changed");
        return false;
    }
    return true;
}

// The start of the observation window: the snapshot, pause counters and counts that every later update is
// compared with.
void open_observation_window(uint64_t app_ticks, const worker::Message& worker_status, Time* time) {
    begin_pauses = pause_values(time);
    begin_ms = GetTickCount64();
    status.app_begin = app_ticks;
    status.ai_begin = worker_status.values[2];
    status.player_native_id = begin.player[0];
    status.avatar_native_id = begin.actor[0];
    status.nouns = static_cast<uint32_t>(begin.nouns.size());
    zero_at_begin = zero_deltas;
    nonzero_at_begin = nonzero_deltas;
    status.zero_native_deltas = status.nonzero_native_deltas = 0;
    status.phase = NativeQuiescencePhase::observing;
    snapshot_event("native_quiescence_begin_state", begin);
    event("native_quiescence_begin", "settling_update_complete");
}

// Settling: the original update must have run with a zero delta, and never with another, and the camera fade
// must have left the creatures opaque, before the window opens.
void settle(uint64_t app_ticks, const worker::Message& worker_status, Time* time) {
    if (app_ticks < acquired_app + 2 || zero_deltas == 0) {
        if (GetTickCount64() - acquired_ms > zero_delta_timeout_ms) {
            reject("no_original_zero_delta_after_pause");
        }
        return;
    }
    if (nonzero_deltas) {
        reject("nonzero_original_delta_during_pause");
        return;
    }
    if (!snapshot(begin)) {
        reject("begin_primary_snapshot");
        return;
    }
    if (!begin.presentation_settled) {
        if (!presentation_wait_reported) {
            snapshot_event("native_quiescence_transparency_wait", begin);
            presentation_wait_reported = true;
        }
        if (GetTickCount64() - acquired_ms >= transparency_timeout_ms) {
            snapshot_event("native_quiescence_transparency_timeout", begin);
            reject("native_camera_transparency_not_settled");
        }
        return;
    }
    open_observation_window(app_ticks, worker_status, time);
}

// Observing, and measured afterwards: nothing may differ from the start of the window. The first update past
// the window's length reports the measurement.
void observe(Time* time) {
    status.elapsed_ms = GetTickCount64() - begin_ms;
    if (status.ai_end != status.ai_begin) {
        reject("original_ai_counter_advanced");
        return;
    }
    if (status.nonzero_native_deltas) {
        reject("nonzero_original_delta_during_window");
        return;
    }
    if (pause_values(time) != begin_pauses) {
        reject("native_pause_counters_changed");
        return;
    }
    Snapshot current;
    if (!snapshot(current)) {
        reject("current_primary_snapshot");
        return;
    }
    if (const auto reason = difference(begin, current)) {
        snapshot_event("native_quiescence_changed_state", current);
        reject(reason);
        return;
    }
    if (status.phase == NativeQuiescencePhase::observing && status.elapsed_ms >= observation_window_ms) {
        if (status.app_end <= status.app_begin || !status.zero_native_deltas) {
            reject("missing_live_native_frame_witness");
            return;
        }
        status.phase = NativeQuiescencePhase::measured;
        status.ready = true;
        snapshot_event("native_quiescence_end_state", current);
        event("native_quiescence_observed", "continuous_three_second_native_state_stability");
    }
}
} // namespace

// One engine update under a held pause: refresh the counters, check the pause is still ours, then the one step
// the phase calls for.
void update_native_quiescence_probe(uint64_t app_ticks) {
    if (!on_thread() || busy || !status.owns_pause || status.phase == NativeQuiescencePhase::rejected) {
        return;
    }
    const auto worker_status = native_actor_worker_status();
    auto time = time_manager();
    status.app_end = app_ticks;
    status.ai_end = worker_status.values[2];
    status.zero_native_deltas = zero_deltas - zero_at_begin;
    status.nonzero_native_deltas = nonzero_deltas - nonzero_at_begin;
    if (!owned_pause_intact(worker_status, time)) {
        return;
    }
    if (status.phase == NativeQuiescencePhase::settling) {
        settle(app_ticks, worker_status, time);
        return;
    }
    observe(time);
}

worker::Result request_native_quiescence_resume(uint64_t request, uint64_t epoch) {
    using Result = worker::Result;
    if (!on_thread() || busy || !request || !held_context()) {
        return Result::unavailable;
    }
    if (epoch != status.epoch || native_actor_worker_status().epoch != epoch) {
        return Result::stale;
    }
    if (request != status.request) {
        return Result::stale;
    }
    if (!status.owns_pause || ownership_ambiguous) {
        return Result::unavailable;
    }
    auto time = time_manager();
    if (!time || reinterpret_cast<uintptr_t>(time) != owned_manager) {
        return Result::unavailable;
    }
    const int count = count_original(time, gameplay);
    if (count < 1) {
        status.owns_pause = false;
        ownership_ambiguous = true;
        reject("owned_pause_consumed_before_resume");
        return Result::failed;
    }
    auto expected = pause_values(time);
    --expected[static_cast<size_t>(time->mPauseMaps.find(gameplay)->second)][0];
    // Consume our one token before the original emits synchronous messages.
    // Never issue a second Resume, even if a callback changes native counters.
    status.owns_pause = false;
    status.ready = false;
    busy = true;
    const int result = resume_original(time, gameplay);
    busy = false;
    status.gameplay_count = count_original(time, gameplay);
    status.pause_flags = time->mFlags;
    if (result != count - 1 || status.gameplay_count != count - 1 || pause_values(time) != expected) {
        reject("single_resume_return_or_foreign_counter_changed");
        return Result::failed;
    }
    if (!native_actor_resume_network_inputs(status.input_epoch)) {
        reject("input_epoch_resume_rejected");
        return Result::failed;
    }
    status.phase = NativeQuiescencePhase::released;
    event("native_quiescence_resume_return", "one_owned_increment_released_other_counters_preserved");
    return Result::accepted;
}

NativeQuiescenceStatus native_quiescence_probe_status(uint64_t epoch) {
    if (!on_thread() || epoch != status.epoch) {
        return {};
    }
    const bool ready = native_quiescence_probe_ready(epoch);
    auto value = status;
    value.ready = ready;
    return value;
}

bool native_quiescence_probe_ready(uint64_t epoch) {
    if (!on_thread() || busy || epoch != status.epoch || !status.owns_pause || ownership_ambiguous ||
        status.phase != NativeQuiescencePhase::measured || !status.ready) {
        return false;
    }
    const auto current = native_actor_worker_status();
    auto time = time_manager();
    Snapshot fresh;
    const char* reason = nullptr;
    if (!held_context()) {
        reason = "ready_context_lost";
    } else if (current.epoch != epoch) {
        reason = "ready_scene_changed";
    } else if (current.values[2] != status.ai_begin) {
        reason = "ready_original_ai_advanced";
    } else if (!time || reinterpret_cast<uintptr_t>(time) != owned_manager) {
        reason = "ready_time_manager_changed";
    } else if (count_original(time, gameplay) != 1 || (time->mFlags & 1) == 0) {
        reason = "ready_owned_pause_changed";
    } else if (pause_values(time) != begin_pauses) {
        reason = "ready_pause_counters_changed";
    } else if (nonzero_deltas != nonzero_at_begin) {
        reason = "ready_nonzero_original_delta";
    } else if (!snapshot(fresh)) {
        reason = "ready_snapshot_unavailable";
    } else if ((reason = difference(begin, fresh)) != nullptr) {
        snapshot_event("native_quiescence_ready_previous_state", begin);
        snapshot_event("native_quiescence_changed_state", fresh);
    }
    if (reason) {
        reject(reason);
        return false;
    }
    return true;
}

void dispose_native_quiescence_probe() {
    if (!on_thread() || busy) {
        return;
    }
    if (status.owns_pause) {
        const auto result = request_native_quiescence_resume(status.request, status.epoch);
        if (result != worker::Result::accepted) {
            event("native_quiescence_dispose_ownership_unresolved", "same_epoch_release_not_proven");
        }
    }
    if (hooks_attached && change_hooks(hooks, _countof(hooks), false) != NO_ERROR) {
        fatal("quiescence_hook_detach");
    }
    hooks_attached = false;
    initialized = false;
}
} // namespace sporemp
