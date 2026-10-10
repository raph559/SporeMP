#include "native_hatch.h"
#include "native_hatch_abi.h"
#include "native_actors.h"
#include "native_award_context.h"
#include "native_death.h"
#include "native_network.h"
#include "native_replica.h"
#include "native_social_abi.h"
#include "native_actor_abi.h"
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/Simulator/cCreatureAbility.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Manager = Simulator::cGameNounManager;
using Tick = NativeSocialAbi<Animal>::Tick;
using Decide = NativeActorAbi<Animal, Animal, Simulator::cCombatant, Math::Vector3>::Decide;
using Construct = void*(__thiscall*)(void*);
using Load = void(__thiscall*)(void*);
using Destroy = void*(__thiscall*)(void*, uint32_t);
using Age = void(__thiscall*)(Animal*, int);
using Growth = void(__thiscall*)(Animal*, float);
using HerdActive = bool(__thiscall*)(void*);
using PresentationClear = void(__thiscall*)(void*, bool, bool);
using PresentationShow = void(__thiscall*)(void*, uint32_t);
using PresentationRefresh = void(__thiscall*)(void*, uint32_t);
uintptr_t base = 0, end = 0;
DWORD thread = 0;
PlayerContextEvent sink = nullptr;
bool (*check_thread)() = nullptr;
Construct construct = nullptr;
Load load = nullptr;
Destroy destroy = nullptr;
Age age_original = nullptr;
Growth growth_original = nullptr;
HerdActive herd_active_original = nullptr;
Decide baby_decide_original = nullptr, mother_decide_original = nullptr;
Decide sequence_decide_original = nullptr;
Tick baby_begin_original = nullptr, baby_tick_original = nullptr;
Tick mother_begin_original = nullptr, mother_tick_original = nullptr, sequence_original = nullptr,
     corpse_original = nullptr;
Tick sequence_begin_original = nullptr;
PresentationClear presentation_clear_original = nullptr;
PresentationShow presentation_show_original = nullptr;
PresentationRefresh presentation_refresh_original = nullptr;
NativeHatchAbi::Eligibility eligibility_original = nullptr;
// Native constructor/destructor own all internal containers and references.
// No static intrusive pointer destructor executes under the loader lock.
alignas(4) std::array<unsigned char, 0x70> drama{};
std::array<unsigned char, 0x70> primary_snapshot{};
void* primary_drama = nullptr;
Animal* owner = nullptr;
uint64_t epoch = 0, calls = 0;
uint32_t owner_id = UINT32_MAX, mother_id = UINT32_MAX;
bool constructed = false, selected = false, blocked = false;
void* sequence_state = nullptr;
network::HatchFrame lesson{};
network::HatchFrame shown_frame{};
uint64_t lesson_serial = 0, consumed_lesson = 0;
bool lesson_finished = false, client_hint = false;

struct EligibilitySample {
    uint64_t serial = 0;
    uint32_t original = 0, projected = 0;
};

std::array<EligibilitySample, 88> eligibility_samples{};

bool on_thread() {
    return thread && GetCurrentThreadId() == thread && check_thread && check_thread();
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

uint32_t word(const void* address, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, static_cast<const unsigned char*>(address) + offset, 4);
    return value;
}

void*& singleton() {
    return *reinterpret_cast<void**>(base + 0x129e43c);
}

// The word of the bridge-owned drama that the events report as "drama_flags".
uint32_t drama_flags() {
    return word(drama.data(), 0x6c);
}

// The original object that shows and clears the lesson hint on a client.
void* hint_presentation() {
    return *reinterpret_cast<void**>(base + 0x11fcc54);
}

template <size_t N>
bool prefix(uint32_t rva, const unsigned char (&bytes)[N]) {
    return rva < end - base && N <= end - base - rva && readable(reinterpret_cast<void*>(base + rva), N) &&
           !std::memcmp(reinterpret_cast<void*>(base + rva), bytes, N);
}

Animal* current(Animal* actor) {
    auto manager = Manager::Get();
    if (!actor || !manager) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(actor)) {
            return !noun.mbIsDestroyed && !noun.field_20 && readable(actor, sizeof(*actor)) &&
                           !actor->mbMarkedForDeletion && *reinterpret_cast<uintptr_t*>(actor) == base + 0x106a080
                       ? actor
                       : nullptr;
        }
    }
    return nullptr;
}

bool valid_owner() {
    uint32_t id = UINT32_MAX;
    return on_thread() && !native_replica_is_client() && native_network_death_authority() &&
           epoch == native_actor_worker_status().epoch && native_actor_owner_native_id(2, id) && id == owner_id &&
           current(owner) && owner->mID == id;
}

void event(const char* name, uint32_t callback = 0, uint32_t state_before = 0, uint32_t state_after = 0) {
    if (!sink || !on_thread()) {
        return;
    }
    const auto tree = owner ? reinterpret_cast<const void*>(static_cast<uintptr_t>(owner->field_B4C)) : nullptr;
    const auto root = readable(tree, 0x1dc) ? word(tree, 0) : 0;
    char fields[620]{};
    sprintf_s(fields,
              ",\"native_slot\":2,\"native_id\":%u,\"mother_native_id\":%u,\"epoch\":%llu,\"callback_rva\":%u,\"state_"
              "before\":%u,\"state_after\":%u,\"drama_flags\":%u,\"age\":%d,\"scale\":%.9g,\"health\":%.9g,\"root_"
              "rva\":%u,\"behavior_id\":%u,\"primary_unchanged\":%s",
              owner_id, mother_id, epoch, callback, state_before, state_after, constructed ? drama_flags() : 0,
              owner ? int(owner->mAge) : -1, owner ? double(owner->GetScale()) : 0,
              owner ? double(owner->mHealthPoints) : 0,
              root >= base && root < end ? static_cast<uint32_t>(root - base) : 0,
              readable(tree, 0x1dc) ? word(tree, 0x1d8) : 0, blocked ? "false" : "true");
    sink(name, fields);
}

bool select() {
    if (selected || blocked || !constructed || !valid_owner() || !readable(singleton(), drama.size()) ||
        singleton() == drama.data()) {
        return false;
    }
    primary_drama = singleton();
    std::memcpy(primary_snapshot.data(), primary_drama, primary_snapshot.size());
    singleton() = drama.data();
    selected = true;
    return true;
}

bool unselect() {
    if (!selected) {
        return false;
    }
    const bool unchanged =
        singleton() == drama.data() && !std::memcmp(primary_snapshot.data(), primary_drama, primary_snapshot.size());
    singleton() = primary_drama;
    primary_drama = nullptr;
    selected = false;
    if (!unchanged) {
        blocked = true;
        event("native_hatch_primary_changed");
    }
    return unchanged;
}

class PlayerClassification {
    Animal* actor;
    int prior;

public:
    explicit PlayerClassification(Animal* animal) : actor(animal), prior(animal->mGeneralFlags & 0x200) {
        animal->mGeneralFlags |= 0x200;
    }

    ~PlayerClassification() {
        actor->mGeneralFlags = (actor->mGeneralFlags & ~0x200) | prior;
    }
};

void remember_mother() {
    mother_id = UINT32_MAX;
    const auto mother = reinterpret_cast<Animal*>(word(drama.data(), 0x14));
    if (current(mother) && mother != owner && mother->mHerd == owner->mHerd) {
        mother_id = mother->mID;
    }
}

bool related(Animal* actor) {
    return actor == owner || (current(owner) && current(actor) && owner->mHerd && actor->mHerd == owner->mHerd);
}

struct LeafCall {
    Tick original;
    Animal* actor;
    double time;
    uint32_t flags, data;
    void* state;
    void* context;
    float dt;
    uint32_t result = 0;
};

bool leaf(Animal* actor, void* opaque) {
    auto& call = *static_cast<LeafCall*>(opaque);
    if (!select()) {
        return false;
    }
    const auto before = readable(call.state, 4) ? word(call.state, 0) : UINT32_MAX;
    auto prior_state = sequence_state;
    if (call.original == sequence_original) {
        sequence_state = call.state;
    }
    {
        PlayerClassification classification(actor);
        call.result = call.original(call.actor, call.time, call.flags, call.data, call.state, call.context, call.dt);
    }
    sequence_state = prior_state;
    remember_mother();
    const auto after = readable(call.state, 4) ? word(call.state, 0) : UINT32_MAX;
    const bool unchanged = unselect();
    if (before != after || calls++ < 12) {
        event("native_hatch_leaf_return",
              call.original == sequence_original         ? 0x964a30
              : call.original == baby_tick_original      ? 0x963f70
              : call.original == baby_begin_original     ? 0x962f10
              : call.original == mother_begin_original   ? 0x963070
              : call.original == sequence_begin_original ? 0x962d40
                                                         : 0x961bd0,
              before, after);
    }
    return unchanged;
}

uint32_t dispatch(Tick original, Animal* actor, double time, uint32_t flags, uint32_t data, void* state, void* context,
                  float dt) {
    if (!constructed || !related(actor)) {
        return original(actor, time, flags, data, state, context, dt);
    }
    if (blocked || !valid_owner()) {
        return 0;
    }
    if (selected) {
        return original(actor, time, flags, data, state, context, dt);
    }
    LeafCall call{original, actor, time, flags, data, state, context, dt};
    if (!call_native_secondary_death(owner_id, epoch, leaf, &call)) {
        blocked = true;
        event("native_hatch_leaf_rejected");
        return 0;
    }
    return call.result;
}

#define HATCH_LEAF(name)                                                                                               \
    uint32_t __cdecl name##_hook(Animal* actor, double time, uint32_t flags, uint32_t data, void* state,               \
                                 void* context, float dt) {                                                            \
        return dispatch(name##_original, actor, time, flags, data, state, context, dt);                                \
    }
HATCH_LEAF(baby_begin)
HATCH_LEAF(baby_tick)
HATCH_LEAF(mother_begin)
HATCH_LEAF(mother_tick)
HATCH_LEAF(sequence)
HATCH_LEAF(sequence_begin)
#undef HATCH_LEAF

struct DecideCall {
    Decide original;
    Animal* actor;
    double time;
    uint32_t flags;
    void* scratch;
    bool* dirty;
    void* decider;
    void* context;
    float result = 0;
};

bool decide(Animal*, void* opaque) {
    auto& call = *static_cast<DecideCall*>(opaque);
    if (!select()) {
        return false;
    }
    call.result =
        call.original(call.actor, call.time, call.flags, call.scratch, call.dirty, call.decider, call.context);
    return unselect();
}

float dispatch_decide(Decide original, Animal* actor, double time, uint32_t flags, void* scratch, bool* dirty,
                      void* decider, void* context) {
    if (!constructed || !related(actor)) {
        return original(actor, time, flags, scratch, dirty, decider, context);
    }
    if (blocked || !valid_owner()) {
        return 0;
    }
    if (selected) {
        return original(actor, time, flags, scratch, dirty, decider, context);
    }
    DecideCall call{original, actor, time, flags, scratch, dirty, decider, context};
    if (!call_native_secondary_death(owner_id, epoch, decide, &call)) {
        blocked = true;
        event("native_hatch_decider_rejected");
        return 0;
    }
    return call.result;
}

float __cdecl baby_decide_hook(Animal* actor, double time, uint32_t flags, void* scratch, bool* dirty, void* decider,
                               void* context) {
    return dispatch_decide(baby_decide_original, actor, time, flags, scratch, dirty, decider, context);
}

float __cdecl mother_decide_hook(Animal* actor, double time, uint32_t flags, void* scratch, bool* dirty, void* decider,
                                 void* context) {
    return dispatch_decide(mother_decide_original, actor, time, flags, scratch, dirty, decider, context);
}

float __cdecl sequence_decide_hook(Animal* actor, double time, uint32_t flags, void* scratch, bool* dirty,
                                   void* decider, void* context) {
    return dispatch_decide(sequence_decide_original, actor, time, flags, scratch, dirty, decider, context);
}

// The creature's ability at this index, or null: only an index below 88 and below its ability count is read.
auto ability_at(Animal* actor, uint32_t index) {
    return index < 88 && static_cast<int>(index) < actor->GetAbilitiesCount() ? actor->GetAbility(index) : nullptr;
}

void __fastcall presentation_clear_hook(void* self, void*, bool a, bool b) {
    if (selected && valid_owner() && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x962f04) {
        event("native_hatch_primary_presentation_fenced", 0x27c310);
        return;
    }
    presentation_clear_original(self, a, b);
}

void reject_lesson() {
    blocked = true;
    event("native_hatch_lesson_rejected");
}

// The hint the original sequence asked to show becomes the second player's lesson, taken from the ability the
// sequence state names; the first player's presentation is not touched.
void offer_lesson(uint32_t hint) {
    if (!readable(sequence_state, 8)) {
        reject_lesson();
        return;
    }
    const auto index = word(sequence_state, 4);
    auto ability = ability_at(owner, index);
    if (!ability || !ability->mbUseForBabyGame || ability->mCategory < 0 || ability->mCategory > 1 || !hint) {
        reject_lesson();
        return;
    }
    lesson = {};
    lesson.serial = ++lesson_serial;
    lesson.active = 1;
    lesson.hint = hint;
    lesson.ability = index;
    lesson.mode = static_cast<uint32_t>(ability->mCategory);
    lesson_finished = false;
    event("native_hatch_primary_presentation_fenced", 0x27c7f0, hint);
}

void __fastcall presentation_show_hook(void* self, void*, uint32_t id) {
    if (selected && valid_owner() && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x964ed1) {
        offer_lesson(id);
        return;
    }
    presentation_show_original(self, id);
}

void __fastcall presentation_refresh_hook(void* self, void*, uint32_t reason) {
    if (selected && valid_owner() && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x92e549 &&
        self == *reinterpret_cast<void**>(base + 0x129e230)) {
        event("native_hatch_primary_presentation_fenced", 0x76f5e0, reason);
        return;
    }
    presentation_refresh_original(self, reason);
}

uint32_t __cdecl eligibility_hook(int index) {
    // D49560 asks D49220 before D48AB0 can dispatch normal ability input.
    // Preserve original modal/busy/resource refusals. The native hatch branch
    // otherwise permits only the designated mother's current ability; that
    // exact observation is already carried by the authenticated owner cue.
    const auto original = eligibility_original(index);
    if (!on_thread() || !native_replica_is_client() || index < 0 || index >= 88) {
        return original;
    }
    auto manager = Manager::Get();
    auto actor = manager ? current(manager->GetAvatar()) : nullptr;
    network::HatchFrame frame;
    if (!actor || actor->mbDead || actor->mHealthPoints <= 0 || actor->mAge != 0 || !(actor->mGeneralFlags & 0x200) ||
        !native_network_hatch_offer(actor->mID, reinterpret_cast<uintptr_t>(actor), frame)) {
        return original;
    }
    auto ability = index < actor->GetAbilitiesCount() ? actor->GetAbility(index) : nullptr;
    uint32_t projected = original;
    if (static_cast<uint32_t>(index) != frame.ability || !ability || !ability->mbUseForBabyGame ||
        static_cast<uint32_t>(ability->mCategory) != frame.mode) {
        projected = 1;
    } else if (original == 0 || original == 3 || original == 4) {
        projected = 0;
    }
    // Codes3/4 are the original missing combat/animal-target refusals. A
    // lesson has no such target. Other original refusals remain unchanged.
    auto& sample = eligibility_samples[static_cast<size_t>(index)];
    if (sink && (sample.serial != frame.serial || sample.original != original || sample.projected != projected)) {
        char fields[280]{};
        sprintf_s(fields,
                  ",\"native_id\":%u,\"serial\":%llu,\"ability_index\":%d,\"expected_ability\":%u,\"original_result\":%"
                  "u,\"projected_result\":%u,\"caller_rva\":%u",
                  actor->mID, frame.serial, index, frame.ability, original, projected,
                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - base));
        sink("native_hatch_client_eligibility", fields);
        sample = {frame.serial, original, projected};
    }
    return projected;
}

void __fastcall age_hook(Animal* actor, void*, int age) {
    if (selected && actor == owner && valid_owner()) {
        const auto before = static_cast<uint32_t>(actor->mAge);
        {
            PlayerClassification classification(actor);
            age_original(actor, age);
        }
        event("native_hatch_age_return", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - base),
              before, static_cast<uint32_t>(actor->mAge));
    } else {
        age_original(actor, age);
    }
}

struct GrowthCall {
    float dt;
};

bool growth(Animal* actor, void* opaque) {
    if (!select()) {
        return false;
    }
    const auto before = word(actor, 0x1688);
    {
        PlayerClassification classification(actor);
        growth_original(actor, static_cast<GrowthCall*>(opaque)->dt);
    }
    const auto after = word(actor, 0x1688);
    const bool unchanged = unselect();
    if (before != after) {
        event("native_hatch_growth_return", 0x804fb0, before, after);
    }
    return unchanged;
}

void __fastcall growth_hook(Animal* actor, void*, float dt) {
    if (!constructed || actor != owner) {
        growth_original(actor, dt);
        return;
    }
    if (blocked || !valid_owner()) {
        return;
    }
    if (!word(actor, 0x1688)) {
        growth_original(actor, dt);
        return;
    }
    if (selected) {
        growth_original(actor, dt);
        return;
    }
    GrowthCall call{dt};
    if (!call_native_secondary_death(owner_id, epoch, growth, &call)) {
        blocked = true;
        event("native_hatch_growth_rejected");
    }
}

uint32_t __cdecl corpse_hook(Animal* actor, double time, uint32_t flags, uint32_t data, void* state, void* context,
                             float dt) {
    NativeDeathRequest pending;
    if (on_thread() && !native_replica_is_client() && native_network_death_authority() &&
        pending_native_death(2, pending) && current(actor) && actor->mID == pending.native_id) {
        // D8E380 tests the native player flag before the NPC expiry branch.
        // Let the original corpse tick retain its player, with all timers and
        // animation work intact. No persistent classification change.
        PlayerClassification classification(actor);
        return corpse_original(actor, time, flags, data, state, context, dt);
    }
    return corpse_original(actor, time, flags, data, state, context, dt);
}

// D405F0 state3 resets NPC herds after A's cinematic. B's
// owned herd must not be healed or relocated by A's restart.
// True, and recorded, when this is the herd of the second player's current creature.
bool second_player_herd(void* herd) {
    uint32_t id = UINT32_MAX;
    auto manager = Manager::Get();
    if (!manager || !native_actor_owner_native_id(2, id)) {
        return false;
    }
    for (auto& noun : manager->mNouns) {
        if (noun.mID != id || noun.mbIsDestroyed || noun.field_20) {
            continue;
        }
        auto actor = static_cast<Animal*>(&noun);
        if (current(actor) && actor->mHerd.get() == herd) {
            if (sink) {
                char fields[180]{};
                sprintf_s(fields,
                          ",\"native_slot\":2,\"native_id\":%u,\"herd_native_id\":%u,\"caller_rva\":%u,"
                          "\"native_active\":true",
                          id, actor->mHerd->mID, 0x94069e);
                sink("native_death_peer_herd_reset_fenced", fields);
            }
            return true;
        }
    }
    return false;
}

bool __fastcall herd_active_hook(void* herd, void*) {
    const bool active = herd_active_original(herd);
    if (active && on_thread() && native_network_death_authority() &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x94069e && second_player_herd(herd)) {
        return false;
    }
    return active;
}

NativeHook hooks[] = {
    {reinterpret_cast<void**>(&age_original), reinterpret_cast<void*>(age_hook)},
    {reinterpret_cast<void**>(&growth_original), reinterpret_cast<void*>(growth_hook)},
    {reinterpret_cast<void**>(&baby_begin_original), reinterpret_cast<void*>(baby_begin_hook)},
    {reinterpret_cast<void**>(&baby_tick_original), reinterpret_cast<void*>(baby_tick_hook)},
    {reinterpret_cast<void**>(&mother_begin_original), reinterpret_cast<void*>(mother_begin_hook)},
    {reinterpret_cast<void**>(&mother_tick_original), reinterpret_cast<void*>(mother_tick_hook)},
    {reinterpret_cast<void**>(&sequence_original), reinterpret_cast<void*>(sequence_hook)},
    {reinterpret_cast<void**>(&corpse_original), reinterpret_cast<void*>(corpse_hook)},
    {reinterpret_cast<void**>(&herd_active_original), reinterpret_cast<void*>(herd_active_hook)},
    {reinterpret_cast<void**>(&baby_decide_original), reinterpret_cast<void*>(baby_decide_hook)},
    {reinterpret_cast<void**>(&mother_decide_original), reinterpret_cast<void*>(mother_decide_hook)},
    {reinterpret_cast<void**>(&sequence_decide_original), reinterpret_cast<void*>(sequence_decide_hook)},
    {reinterpret_cast<void**>(&sequence_begin_original), reinterpret_cast<void*>(sequence_begin_hook)},
    {reinterpret_cast<void**>(&presentation_clear_original), reinterpret_cast<void*>(presentation_clear_hook)},
    {reinterpret_cast<void**>(&presentation_show_original), reinterpret_cast<void*>(presentation_show_hook)},
    {reinterpret_cast<void**>(&presentation_refresh_original), reinterpret_cast<void*>(presentation_refresh_hook)},
    {reinterpret_cast<void**>(&eligibility_original), reinterpret_cast<void*>(eligibility_hook)}};

// The original code this file binds, calls or recognises as a caller, and the three globals it reads (RVAs
// 0x11fcc54, 0x129e43c and 0x129e230).
bool hatch_code_pinned() {
    const unsigned char ctor[] = {0x53, 0x56, 0x57, 0x8b, 0xf1, 0xe8, 0x46, 0xcd, 0xe0, 0xff};
    const unsigned char init[] = {0x83, 0xec, 0x3c, 0x53, 0x8b, 0xd9, 0xe8, 0xe5, 0xd8, 0xe0, 0xff};
    const unsigned char dtor[] = {0x56, 0x8b, 0xf1, 0xe8, 0xb8, 0xf4, 0xff, 0xff};
    const unsigned char dtor_tail[] = {0x8b, 0xc6, 0x5e, 0xc2, 4, 0};
    const unsigned char age[] = {0x56, 0x8b, 0xf1, 0xe8, 0xd8, 0x69, 0xf5, 0xff};
    const unsigned char growth[] = {0x83, 0xec, 8, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x88, 0x16, 0, 0};
    const unsigned char baby_begin[] = {0x83, 0xec, 0x1c, 0xe8, 0xf8, 0xe6, 0xfe, 0xff};
    const unsigned char baby_tick[] = {0xe8, 0x9b, 0xd6, 0xfe, 0xff, 0x8b, 0x48, 0x6c};
    const unsigned char mother_begin[] = {0x83, 0xec, 0x34, 0x56, 0x57, 0xe8, 0x86, 0xa3, 0xdd, 0xff};
    const unsigned char mother_tick[] = {0xe8, 0x2b, 0xb8, 0xdd, 0xff, 0x8b, 0xc8, 0xe8, 0x24, 0xe1, 0xdb, 0xff};
    const unsigned char sequence[] = {0x83, 0xec, 0x30, 0x53, 0x55, 0x56, 0x57, 0xe8, 0xc4, 0x89, 0xdd, 0xff};
    const unsigned char corpse[] = {0x83, 0xec, 0x50, 0x53, 0x57, 0xe8, 0x86, 0xd5, 0xdc, 0xff};
    const unsigned char herd_active[] = {0x8a, 0x81, 0x18, 2, 0, 0, 0xc3};
    const unsigned char herd_caller[] = {0x8b, 0xcb, 0xe8, 0x22, 0x9b, 0xf2, 0xff, 0x84, 0xc0};
    const unsigned char baby_decide[] = {0xe8, 0xab, 0xfa, 0xfe, 0xff, 0x8b, 0x48, 0x6c};
    const unsigned char mother_decide[] = {0x56, 0xe8, 0x6a, 0xb8, 0xdd, 0xff, 0x8b,
                                           0xc8, 0xe8, 0x63, 0xe1, 0xdb, 0xff};
    const unsigned char sequence_decide[] = {0xe8, 0x0b, 0xfb, 0xfe, 0xff, 0x8b, 0x48, 0x6c};
    const unsigned char sequence_begin[] = {0x83, 0xec, 0x30, 0x53, 0x55, 0x56, 0x57, 0xe8, 0xb4, 0xa6, 0xdd, 0xff};
    const unsigned char presentation_clear[] = {0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x4c};
    const unsigned char presentation_show[] = {0x56, 0x8b, 0xf1, 0x8b, 0x46, 0x20};
    const unsigned char presentation_refresh[] = {0xc6, 0x41, 0x70, 1, 0xc2, 4, 0};
    const unsigned char clear_caller[] = {0x8b, 0xc8, 0xe8, 0x0c, 0x94, 0x91, 0xff};
    const unsigned char show_caller[] = {0x8b, 0xc8, 0xe8, 0x1f, 0x79, 0x91, 0xff};
    const unsigned char refresh_caller[] = {0x6a, 0, 0xff, 0xd0};
    const unsigned char input[] = {0x83, 0xec, 0x1c, 0x57, 0x8b, 0x7c, 0x24, 0x28};
    const unsigned char eligibility[] = {0x53, 0x56, 0x57, 0xe8, 0xe8, 0x53, 0xfd, 0xff};
    const unsigned char eligibility_caller[] = {0x53, 0xe8, 0xb3, 0xfc, 0xff, 0xff, 0x83, 0xc4, 4};
    if (!prefix(0x94ea40, ctor) || !prefix(0x94e020, init) || !prefix(0x94eac0, dtor) || !prefix(0x94ead8, dtor_tail) ||
        !prefix(0x804f30, age) || !prefix(0x804fb0, growth) || !prefix(0x962f10, baby_begin) ||
        !prefix(0x963f70, baby_tick) || !prefix(0x963070, mother_begin) || !prefix(0x961bd0, mother_tick) ||
        !prefix(0x964a30, sequence) || !prefix(0x98e380, corpse) || !prefix(0x86a1c0, herd_active) ||
        !prefix(0x940697, herd_caller) || !prefix(0x961b60, baby_decide) || !prefix(0x961b90, mother_decide) ||
        !prefix(0x961b00, sequence_decide) || !prefix(0x962d40, sequence_begin) ||
        !prefix(0x27c310, presentation_clear) || !prefix(0x27c7f0, presentation_show) ||
        !prefix(0x76f5e0, presentation_refresh) || !prefix(0x962efd, clear_caller) || !prefix(0x964eca, show_caller) ||
        !prefix(0x92e545, refresh_caller) || !prefix(0x91e930, input) || !prefix(0x949220, eligibility) ||
        !prefix(0x949567, eligibility_caller) || !readable(reinterpret_cast<void*>(base + 0x11fcc54), 4) ||
        !readable(reinterpret_cast<void*>(base + 0x129e43c), 4) ||
        !readable(reinterpret_cast<void*>(base + 0x129e230), 4)) {
        return false;
    }
    return true;
}
} // namespace

bool prepare_native_hatch(uintptr_t image, uintptr_t image_end, DWORD engine_thread, PlayerContextEvent events,
                          bool (*check)()) {
    base = image;
    end = image_end;
    thread = engine_thread;
    sink = events;
    check_thread = check;
    if (!hatch_code_pinned()) {
        return false;
    }
    construct = reinterpret_cast<Construct>(base + 0x94ea40);
    load = reinterpret_cast<Load>(base + 0x94e020);
    destroy = reinterpret_cast<Destroy>(base + 0x94eac0);
    age_original = reinterpret_cast<Age>(base + 0x804f30);
    growth_original = reinterpret_cast<Growth>(base + 0x804fb0);
    baby_begin_original = reinterpret_cast<Tick>(base + 0x962f10);
    baby_tick_original = reinterpret_cast<Tick>(base + 0x963f70);
    mother_begin_original = reinterpret_cast<Tick>(base + 0x963070);
    mother_tick_original = reinterpret_cast<Tick>(base + 0x961bd0);
    sequence_original = reinterpret_cast<Tick>(base + 0x964a30);
    corpse_original = reinterpret_cast<Tick>(base + 0x98e380);
    herd_active_original = reinterpret_cast<HerdActive>(base + 0x86a1c0);
    baby_decide_original = reinterpret_cast<Decide>(base + 0x961b60);
    mother_decide_original = reinterpret_cast<Decide>(base + 0x961b90);
    sequence_decide_original = reinterpret_cast<Decide>(base + 0x961b00);
    sequence_begin_original = reinterpret_cast<Tick>(base + 0x962d40);
    presentation_clear_original = reinterpret_cast<PresentationClear>(base + 0x27c310);
    presentation_show_original = reinterpret_cast<PresentationShow>(base + 0x27c7f0);
    presentation_refresh_original = reinterpret_cast<PresentationRefresh>(base + 0x76f5e0);
    eligibility_original = reinterpret_cast<NativeHatchAbi::Eligibility>(base + 0x949220);
    return true;
}

NativeHook native_hatch_binding(unsigned index) {
    return hooks[index];
}

bool native_secondary_hatch_ready(Animal* actor, uint64_t current_epoch) {
    uint32_t id = UINT32_MAX;
    if (!on_thread() || selected || blocked || native_replica_is_client() || !native_network_death_authority() ||
        current_epoch != native_actor_worker_status().epoch || !native_actor_owner_native_id(2, id) ||
        !current(actor) || actor->mID != id || actor->mAge != 1 || word(actor, 0x1688) ||
        !Manager::Get()->mPosseMembers.empty() || !readable(singleton(), drama.size())) {
        return false;
    }
    // Reuse only a completed original cycle for the same current owner. The
    // first restart after a fresh load has no bridge-owned drama to retire.
    return !constructed ||
           (owner == actor && owner_id == id && epoch == current_epoch && valid_owner() && !drama_flags());
}

bool begin_native_secondary_hatch(Animal* actor, uint64_t current_epoch) {
    if (!native_secondary_hatch_ready(actor, current_epoch)) {
        return false;
    }
    owner = actor;
    owner_id = actor->mID;
    epoch = current_epoch;
    if (!constructed) {
        auto primary = singleton();
        std::array<unsigned char, 0x70> before{};
        std::memcpy(before.data(), primary, before.size());
        construct(drama.data());
        constructed = true;
        load(drama.data());
        const bool unchanged = singleton() == drama.data() && !std::memcmp(before.data(), primary, before.size());
        singleton() = primary;
        if (!unchanged || word(drama.data(), 0) != base + 0x107b53c || !word(drama.data(), 0x64)) {
            blocked = true;
            event("native_hatch_initialization_rejected");
            return false;
        }
        event("native_hatch_constructed");
    }
    if (!select()) {
        return false;
    }
    // Invalidate the previous cue without reusing its serial. Native original
    // activation chooses the next mother and decides which lessons are needed.
    mother_id = UINT32_MAX;
    sequence_state = nullptr;
    consumed_lesson = 0;
    lesson = {};
    lesson.serial = ++lesson_serial;
    lesson_finished = false;
    return true;
}

bool end_native_secondary_hatch() {
    if (!selected) {
        return false;
    }
    remember_mother();
    const bool active = drama_flags() == 0x10 && mother_id != UINT32_MAX;
    const bool unchanged = unselect();
    event("native_hatch_restart_return");
    if (!active || !unchanged) {
        blocked = true;
    }
    return active && unchanged;
}

bool native_secondary_hatch_selected(const void* manager) {
    return on_thread() && selected && manager == drama.data();
}

bool native_hatch_checkpoint_idle() {
    return !blocked && !selected &&
           (!constructed || (!drama_flags() && valid_owner() && owner->mAge == 1 && !word(owner, 0x1688)));
}

bool capture_native_hatch(uint32_t& native_id, uint64_t& current_epoch, network::HatchFrame& frame) {
    if (!constructed || selected || blocked || !valid_owner() || !lesson.serial) {
        return false;
    }
    if (!drama_flags() && !lesson_finished) {
        lesson = {};
        lesson.serial = ++lesson_serial;
        lesson_finished = true;
    }
    native_id = owner_id;
    current_epoch = epoch;
    frame = lesson;
    return true;
}

namespace {
struct InputCall {
    uint32_t choice;
};

bool lesson_input(Animal* actor, void* opaque) {
    const auto choice = static_cast<InputCall*>(opaque)->choice;
    auto ability = ability_at(actor, choice);
    if (!ability || !ability->mbUseForBabyGame || actor->mbDead || actor->mAge != 0 || drama_flags() != 0x10 ||
        !select()) {
        return false;
    }
    bool eligible = false;
    {
        PlayerClassification classification(actor);
        eligible = static_cast<Simulator::cSpatialObject*>(actor)->IsPlayerOwned();
        if (eligible) {
            reinterpret_cast<NativeSocialAbi<Animal>::Input>(base + 0x91e930)(actor, choice);
        }
    }
    const bool unchanged = unselect();
    event("native_hatch_input_return", 0x91e930, choice, eligible ? 1u : 0u);
    return eligible && unchanged;
}
} // namespace

bool apply_native_hatch_input(uint32_t native_id, uint64_t current_epoch, const network::HatchFrame& frame) {
    if (!network::valid_hatch(frame, true) || !valid_owner() || native_id != owner_id || current_epoch != epoch ||
        selected || blocked || !lesson.active || frame.serial == consumed_lesson) {
        return false;
    }
    auto expected = lesson;
    expected.entity = frame.entity;
    expected.generation = frame.generation;
    if (!network::same_hatch_offer(expected, frame)) {
        return false;
    }
    InputCall call{frame.choice};
    if (!call_native_secondary_death(native_id, current_epoch, lesson_input, &call)) {
        return false;
    }
    consumed_lesson = frame.serial;
    return true;
}

bool present_native_hatch(const network::HatchFrame& frame) {
    if (!on_thread() || !native_replica_is_client() || !network::valid_hatch(frame)) {
        return false;
    }
    if (shown_frame == frame) {
        return true;
    }
    auto presentation = hint_presentation();
    if (!readable(presentation, 0x50)) {
        return false;
    }
    if (frame.active) {
        auto manager = Manager::Get();
        auto actor = manager ? current(manager->GetAvatar()) : nullptr;
        if (!actor || actor->mbDead || actor->mAge != 0) {
            return false;
        }
        auto ability =
            static_cast<int>(frame.ability) < actor->GetAbilitiesCount() ? actor->GetAbility(frame.ability) : nullptr;
        if (!ability || !ability->mbUseForBabyGame || static_cast<uint32_t>(ability->mCategory) != frame.mode) {
            return false;
        }
        Simulator::cCreatureGameData::SetAbilityMode(static_cast<Simulator::cCreatureGameData::AbilityMode>(frame.mode),
                                                     false);
        presentation_show_original(presentation, frame.hint);
        client_hint = true;
    } else if (client_hint) {
        presentation_clear_original(presentation, false, true);
        client_hint = false;
    }
    shown_frame = frame;
    if (sink) {
        char fields[210]{};
        sprintf_s(fields, ",\"serial\":%llu,\"active\":%s,\"hint\":%u,\"ability\":%u,\"mode\":%u", frame.serial,
                  frame.active ? "true" : "false", frame.hint, frame.ability, frame.mode);
        sink("native_hatch_client_presented", fields);
    }
    return true;
}

void reset_native_hatch() {
    if (!on_thread()) {
        return;
    }
    if (client_hint && presentation_clear_original) {
        auto presentation = hint_presentation();
        if (readable(presentation, 0x50)) {
            presentation_clear_original(presentation, false, true);
        }
    }
    if (selected) {
        unselect();
    }
    if (constructed && destroy) {
        auto primary = singleton();
        singleton() = drama.data();
        // The original destructor releases its containers and mother reference;
        // unlike Deactivate it never selects a potentially retired avatar.
        destroy(drama.data(), 0);
        singleton() = primary;
    }
    drama.fill(0);
    constructed = selected = blocked = false;
    owner = nullptr;
    owner_id = mother_id = UINT32_MAX;
    epoch = calls = 0;
    sequence_state = nullptr;
    lesson = shown_frame = {};
    lesson_serial = consumed_lesson = 0;
    lesson_finished = client_hint = false;
    eligibility_samples = {};
}
} // namespace sporemp
