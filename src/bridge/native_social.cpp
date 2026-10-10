#include "native_social.h"
#include "native_actors.h"
#include "native_death.h"
#include "native_social_abi.h"
#include "native_award_context.h"
#include "native_pickup_abi.h"
#include "native_replica.h"
#include "native_network.h"
#include "native_social_observation.h"
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/Simulator/cCreatureDisplayStrategy.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameTimeManager.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Manager = Simulator::cGameNounManager;
using Input = NativeSocialAbi<Animal>::Input;
using Deactivate = NativeSocialAbi<Animal>::Deactivate;
using SelectRoot = NativeSocialAbi<Animal>::SelectRoot;
using Tick = NativeSocialAbi<Animal>::Tick;
using Result = NativeSocialAbi<Animal>::Result;
using Pause = NativeSocialAbi<Animal>::Pause;
using Order = NativePickupAbi<Animal>::Order;
uintptr_t image_base = 0, image_end = 0;
DWORD engine_thread = 0;
PlayerContextEvent sink = nullptr;
bool (*thread_check)() = nullptr;
Input input_original = nullptr;
Order order_entry = nullptr;
Deactivate deactivate_entry = nullptr;
SelectRoot select_root_entry = nullptr;
Tick tick_original = nullptr;
Result result_original = nullptr;
Pause pause_original = nullptr, resume_original = nullptr;
NativeSocialAbi<Animal>::MeterVisible meter_visible = nullptr;
NativeSocialAbi<Animal>::MeterRelation meter_relation_original = nullptr;
void* observed_meter = nullptr;
uint32_t observed_meter_relation = 0;
bool presented_meter = false;
Animal* probe_actor = nullptr;
uint32_t probe_native_id = UINT32_MAX, probe_mode = 0;
uint64_t probe_actor_id = 0, probe_epoch = 0, social_tick_calls = 0, social_tick_observed = 0;
uint64_t input_calls = 0;
uint64_t result_calls = 0;
uint64_t pause_calls = 0;
bool pause_exhausted = false;
bool completed_owner_result = false;
bool network_probe = false;
bool persistent_network_controller = false;
bool exhausted = false;

// D48AB0 is the pinned original ability-bar/shortcut dispatch; this is where its call of the input handler D1E930
// returns (social_code_pinned checks that call at RVA 0x948ad4).
constexpr uint32_t ability_bar_caller_rva = 0x948adb;
// The measured pair of behavior-tree roots: the one an ordinary creature has, and the one the original avatar has.
constexpr uint32_t creature_root_rva = 0x11906f0;
constexpr uint32_t avatar_root_rva = 0x1190800;
// The behavior a creature's tree reports (the word at 0x1d8) while it is in the original SocialRespond.
constexpr uint32_t social_respond_behavior = 0x941bd4bf;
// The original drawing caches of the social meter that E33230 clears before it releases them.
constexpr size_t meter_cache_offsets[]{0x144, 0x1e8, 0x1f0, 0x1f4};

bool on_thread() {
    return engine_thread && GetCurrentThreadId() == engine_thread && thread_check && thread_check();
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

template <size_t N>
bool bytes(uint32_t rva, const unsigned char (&expected)[N]) {
    if (!image_base || image_end <= image_base || rva > image_end - image_base || N > image_end - image_base - rva) {
        return false;
    }
    const auto address = reinterpret_cast<const void*>(image_base + rva);
    return readable(address, N) && !std::memcmp(address, expected, N);
}

Animal* current(Animal* actor, uint32_t id = UINT32_MAX) {
    auto manager = Manager::Get();
    if (!actor || !manager || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(actor)) {
            if (!readable(actor, sizeof(Animal)) || noun.mbIsDestroyed || noun.field_20 || actor->mbMarkedForDeletion ||
                !actor->mbEnabled || (id != UINT32_MAX && noun.mID != id) ||
                *reinterpret_cast<const uintptr_t*>(actor) != image_base + 0x106a080) {
                return nullptr;
            }
            return actor;
        }
    }
    return nullptr;
}

bool living(Animal* actor) {
    return actor && !actor->mbDead && std::isfinite(actor->mHealthPoints) && actor->mHealthPoints > 0;
}

const unsigned char* tree(Animal* actor) {
    const auto address = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(actor->field_B4C));
    return readable(address, 0x1dc) ? address : nullptr;
}

uint32_t word(const unsigned char* data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data + offset, 4);
    return value;
}

const unsigned char* order(Animal* actor, uint32_t mask) {
    const auto behavior_tree = tree(actor);
    if (!behavior_tree) {
        return nullptr;
    }
    for (unsigned i = 0; i < 8; ++i) {
        const auto entry = behavior_tree + 8 + i * 0x38;
        if (word(entry, 0) == mask && word(entry, 4) == 0) {
            return entry;
        }
    }
    return nullptr;
}

uint32_t target_id(Animal* actor) {
    if (!actor->mpCombatantTarget) {
        return UINT32_MAX;
    }
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) {
            continue;
        }
        auto candidate = static_cast<Animal*>(&noun);
        if (static_cast<Simulator::cCombatant*>(candidate) == actor->mpCombatantTarget && current(candidate)) {
            return candidate->mID;
        }
    }
    return UINT32_MAX;
}

// The creature's own ability in that slot, classified by the host-tested rule; no GA/Tribal powers.
AbilityRoute ability_route(Animal* actor, uint32_t index, int* type = nullptr) {
    if (type) {
        *type = -1;
    }
    if (index >= 88) {
        return AbilityRoute::local;
    }
    const int count = actor->GetAbilitiesCount();
    if (count < 0 || count > 88 || index >= static_cast<unsigned>(count)) {
        return AbilityRoute::local;
    }
    const auto ability = actor->GetAbility(static_cast<int>(index));
    if (!ability) {
        return AbilityRoute::local;
    }
    static_assert(Simulator::cCreatureAbility::kCategoryAttack == 1);
    if (type) {
        *type = int(ability->mType);
    }
    return client_ability_route(int(ability->mType), ability->mCategory);
}

bool social_ability(Animal* actor, uint32_t index) {
    return ability_route(actor, index) == AbilityRoute::social;
}

bool combat_ability(Animal* actor, uint32_t index) {
    return ability_route(actor, index) == AbilityRoute::combat;
}

bool owned_probe_actor(Animal* actor) {
    return probe_mode >= 2 && actor == probe_actor && on_thread() && current(actor, probe_native_id) && living(actor) &&
           !(actor->mGeneralFlags & 0x300) && tree(actor) && word(tree(actor), 0) == image_base + avatar_root_rva &&
           native_awards_ready(probe_actor_id, probe_epoch) && Manager::Get()->mPosseMembers.empty();
}

// The RVA of a return address inside the image; 0 for one outside it.
uint32_t caller_rva(uintptr_t caller) {
    return caller >= image_base && caller < image_end ? static_cast<uint32_t>(caller - image_base) : 0;
}

int observe_pause(bool pausing, void* receiver, uint32_t type, uintptr_t caller) {
    const auto original = pausing ? pause_original : resume_original;
    auto manager = static_cast<Simulator::cGameTimeManager*>(receiver);
    const bool observed = probe_mode >= 2 && on_thread() && sink && receiver == Simulator::cGameTimeManager::Get() &&
                          readable(manager, sizeof(*manager));
    const int flags_before = observed ? manager->mFlags : 0;
    const bool result_scope = observed && native_social_award_scope_ready();
    // Read-only observation of original stacked pause/resume. Never suppress,
    // balance or change a native pause from this diagnostic hook.
    const int result = original(receiver, type);
    if (on_thread() && receiver == Simulator::cGameTimeManager::Get() && readable(manager, sizeof(*manager))) {
        observe_native_tutorial_pause(pausing, type, caller_rva(caller), result, manager->mFlags);
        observe_native_death_pause(pausing, type, caller_rva(caller), result, manager->mFlags);
    }
    if (observed) {
        if (pause_calls < 512) {
            char fields[450]{};
            sprintf_s(fields,
                      ",\"call\":%llu,\"pausing\":%s,\"type\":%u,\"caller_rva\":%u,\"counter_after\":%d,\"flags_"
                      "before\":%d,\"flags_after\":%d,\"owned_result_scope\":%s",
                      ++pause_calls, pausing ? "true" : "false", type, caller_rva(caller), result, flags_before,
                      manager->mFlags, result_scope ? "true" : "false");
            sink("native_social_pause_observed", fields);
        } else if (!pause_exhausted) {
            pause_exhausted = true;
            sink("native_social_pause_observation_exhausted", ",\"limit\":512");
        }
    }
    return result;
}

int __fastcall pause_hook(void* receiver, void*, uint32_t type) {
    return observe_pause(true, receiver, type, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

int __fastcall resume_hook(void* receiver, void*, uint32_t type) {
    return observe_pause(false, receiver, type, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

void __fastcall meter_relation_hook(void* receiver, void*, uint32_t relation) {
    meter_relation_original(receiver, relation);
    if (on_thread() && readable(receiver, 0x200) &&
        word(static_cast<const unsigned char*>(receiver), 0) == image_base + native_social_ui_vtable_rva &&
        relation >= 1 && relation <= 6) {
        observed_meter = receiver;
        observed_meter_relation = relation;
    }
}

class SocialAvatarClassification {
    Animal* actor;
    uint32_t id, previous;

public:
    explicit SocialAvatarClassification(Animal* value)
        : actor(value), id(value->mID), previous(value->mGeneralFlags & 0x200) {
        actor->mGeneralFlags |= 0x200;
    }

    ~SocialAvatarClassification() {
        if (current(actor, id)) {
            actor->mGeneralFlags = (actor->mGeneralFlags & ~0x200) | previous;
        }
    }
};

uint32_t __cdecl tick_hook(Animal* actor, double time, uint32_t flags, uint32_t data, void* state, void* context,
                           float delta) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (probe_mode < 2 || actor != probe_actor || !on_thread()) {
        return tick_original(actor, time, flags, data, state, context, delta);
    }
    // BC9160 dispatches the same eight-word tick both after activation and on
    // the retained active branch. Both original callsites have caller cleanup.
    const bool dispatch = caller == image_base + 0x7c9479 || caller == image_base + 0x7c96a0;
    const bool owner = owned_probe_actor(actor), state_valid = readable(state, 0x1c);
    if (probe_mode >= 2 && actor == probe_actor && on_thread() && social_tick_observed++ < 8 && sink) {
        char fields[256]{};
        sprintf_s(fields, ",\"caller_rva\":%u,\"dispatch_valid\":%s,\"owner_valid\":%s,\"state_valid\":%s",
                  caller_rva(caller), dispatch ? "true" : "false", owner ? "true" : "false",
                  state_valid ? "true" : "false");
        sink("native_social_probe_tick_entry", fields);
    }
    if (!dispatch || !owner || !state_valid) {
        return tick_original(actor, time, flags, data, state, context, delta);
    }
    // D9C100's audited avatar branches issue the original greeting, target
    // order, energy cost and response data. Only this local probe's actor is
    // classified for this call. No manager root or StageScope surrounds AI.
    const auto state_bytes = static_cast<const unsigned char*>(state);
    const auto phase = word(state_bytes, 0), animation = word(state_bytes, 8), ability = word(state_bytes, 0xc);
    const auto call = ++social_tick_calls;
    uint32_t result;
    {
        SocialAvatarClassification classification(actor);
        result = tick_original(actor, time, flags, data, state, context, delta);
    }
    if (sink && current(actor, probe_native_id) && readable(state, 0x1c) &&
        (call == 1 || phase != word(state_bytes, 0) || animation != word(state_bytes, 8) ||
         ability != word(state_bytes, 0xc))) {
        char fields[450]{};
        sprintf_s(fields,
                  ",\"call\":%llu,\"actor\":%llu,\"actor_native_id\":%u,\"target_native_id\":%u,\"phase_before\":%u,"
                  "\"phase_after\":%u,\"animation_before\":%u,\"animation_after\":%u,\"ability_before\":%u,\"ability_"
                  "after\":%u,\"actor_flags_after\":%u,\"result_low_byte\":%u",
                  call, probe_actor_id, actor->mID, target_id(actor), phase, word(state_bytes, 0), animation,
                  word(state_bytes, 8), ability, word(state_bytes, 0xc), static_cast<uint32_t>(actor->mGeneralFlags),
                  result & 0xff);
        sink("native_social_owned_tick", fields);
    }
    return result;
}

uint32_t __fastcall result_hook(void* self, void*, Animal* npc, Animal* beneficiary) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (caller != image_base + 0x9a4a00 || probe_mode < 2 || beneficiary != probe_actor || !on_thread()) {
        return result_original(self, npc, beneficiary);
    }
    npc = current(npc);
    if (!owned_probe_actor(beneficiary) || !living(npc)) {
        if (sink) {
            sink("native_social_owned_result_rejected", ",\"reason\":\"owner_or_target_unavailable\"");
        }
        return UINT32_MAX;
    }
    NativeSocialOwnerScope owner(beneficiary, npc, probe_actor_id);
    if (!native_social_award_scope_ready()) {
        if (sink) {
            sink("native_social_owned_result_rejected", ",\"reason\":\"reward_context_unavailable\"");
        }
        return UINT32_MAX;
    }
    // Result callback only, after original score completion. Its nested reward
    // and action leaves select B separately; relationship calculation stays native.
    const auto call = ++result_calls;
    sample_native_social_owner_result(true, call, UINT32_MAX);
    uint32_t animation;
    {
        SocialAvatarClassification classification(beneficiary);
        animation = result_original(self, npc, beneficiary);
    }
    sample_native_social_owner_result(false, call, animation);
    // The NPC result callback can finish while B's SocialRespond remains at
    // phase 2 awaiting further input. Remember that measured result boundary;
    // behavior-idle alone is not a reliable completion predicate.
    completed_owner_result = true;
    return animation;
}

void snapshot(const char* name, Animal* actor, uint64_t call, uint64_t actor_id, uint32_t index) {
    if (!sink) {
        return;
    }
    const auto behavior_tree = actor ? tree(actor) : nullptr;
    const auto reply = behavior_tree ? order(actor, 0x10000000) : nullptr;
    const auto start = behavior_tree ? order(actor, 0x10000) : nullptr;
    Animal* start_target = nullptr;
    if (start) {
        std::memcpy(&start_target, start + 0x2c, 4);
    }
    start_target = current(start_target);
    // The pinned SDK tree header and C04440/C09570 agree on these roots and
    // constant flags. This is read-only evidence, not a decision override.
    const auto root = behavior_tree ? static_cast<uintptr_t>(word(behavior_tree, 0)) : 0;
    const bool root_valid = root >= image_base && root < image_end && image_end - root >= 0x88 &&
                            readable(reinterpret_cast<const void*>(root), 0x88);
    const bool extended = behavior_tree && readable(behavior_tree, 0x600);
    const bool reply_valid = reply && word(reply, 0xc) == 0 && word(reply, 0x10) < 88;
    float priority = 0;
    if (reply) {
        std::memcpy(&priority, reply + 8, 4);
    }
    char fields[1024]{};
    sprintf_s(fields,
              ",\"call\":%llu,\"actor\":%llu,\"actor_native_id\":%u,\"ability_index\":%u,"
              "\"resolved\":%s,\"behavior\":%u,\"target_native_id\":%u,\"reply_present\":%s,"
              "\"reply_valid\":%s,\"reply_ability\":%u,\"reply_priority\":%.9g,"
              "\"root_valid\":%s,\"root_rva\":%u,\"root_id\":%u,\"actor_flags\":%u,"
              "\"constant_flags_valid\":%s,\"constant_flags\":%u,\"order_mask\":%u,\"order_extra\":%u,"
              "\"start_present\":%s,\"start_target_native_id\":%u",
              call, actor_id, actor ? actor->mID : UINT32_MAX, index, behavior_tree ? "true" : "false",
              behavior_tree ? word(behavior_tree, 0x1d8) : 0, actor ? target_id(actor) : UINT32_MAX,
              reply ? "true" : "false", reply_valid ? "true" : "false", reply_valid ? word(reply, 0x10) : UINT32_MAX,
              std::isfinite(priority) ? double(priority) : 0.0, root_valid ? "true" : "false",
              root_valid ? static_cast<uint32_t>(root - image_base) : 0,
              root_valid ? word(reinterpret_cast<const unsigned char*>(root), 0) : 0,
              actor ? static_cast<uint32_t>(actor->mGeneralFlags) : 0, extended ? "true" : "false",
              extended ? word(behavior_tree, 0x5fc) : 0, behavior_tree ? word(behavior_tree, 0x1c8) : 0,
              behavior_tree ? word(behavior_tree, 0x1cc) : 0, start ? "true" : "false",
              start_target ? start_target->mID : UINT32_MAX);
    sink(name, fields);
}

// An ability-bar input of a client past its bootstrap: the replica sends an intention instead of acting on its copy.
bool client_bar_input(Animal* actor, uintptr_t caller) {
    return caller == image_base + ability_bar_caller_rva && on_thread() && current(actor) &&
           native_replica_is_client() && !native_replica_bootstrap_open();
}

// One row for an input that was consumed here and sent to the authority instead.
void record_forwarded(const char* name, const Animal* actor, int index, bool sent) {
    if (!sink) {
        return;
    }
    char fields[160]{};
    sprintf_s(fields, ",\"actor_native_id\":%u,\"ability_index\":%d,\"sent\":%s,\"local_order_written\":false",
              actor->mID, index, sent ? "true" : "false");
    sink(name, fields);
}

bool forward_hatch_input(Animal* actor, int index, uintptr_t caller) {
    if (caller != image_base + ability_bar_caller_rva || !on_thread() || !current(actor) ||
        !native_network_hatch_active()) {
        return false;
    }
    const bool sent =
        native_network_hatch_input(actor->mID, reinterpret_cast<uintptr_t>(actor), index, ability_bar_caller_rva);
    record_forwarded("native_hatch_ui_forwarded", actor, index, sent);
    return true;
}

// The current creature the actor has as its combatant target; the last one of the census that matches.
Animal* combatant_target(Animal* actor) {
    Animal* target = nullptr;
    for (auto& noun : Manager::Get()->mNouns) {
        if (!noun.mbIsDestroyed && noun.GetNounID() == Animal::NOUN_ID) {
            auto candidate = static_cast<Animal*>(&noun);
            if (static_cast<Simulator::cCombatant*>(candidate) == actor->mpCombatantTarget && current(candidate)) {
                target = candidate;
            }
        }
    }
    return target;
}

// D48AB0 is the pinned original ability-bar/shortcut dispatch. Its void
// return permits consuming the input before D1E930 writes local orders.
// Original selection remains local presentation; no local AI/reward is run.
bool forward_ability_input(Animal* actor, int index, uintptr_t caller) {
    if (!client_bar_input(actor, caller) ||
        !(social_ability(actor, static_cast<uint32_t>(index)) || combat_ability(actor, static_cast<uint32_t>(index)))) {
        return false;
    }
    Animal* target = combatant_target(actor);
    const bool combat = combat_ability(actor, static_cast<uint32_t>(index));
    const auto forward = combat ? native_network_combat_input : native_network_social_input;
    const bool sent = actor == Manager::Get()->GetAvatar() && living(actor) && living(target) &&
                      forward(actor->mID, reinterpret_cast<uintptr_t>(actor), target->mID,
                              reinterpret_cast<uintptr_t>(target), index, static_cast<uint32_t>(caller - image_base));
    if (sink) {
        char fields[240]{};
        sprintf_s(fields,
                  ",\"actor_native_id\":%u,\"target_native_id\":%u,\"ability_index\":%d,\"sent\":%s,\"local_order_"
                  "written\":false",
                  actor->mID, target ? target->mID : UINT32_MAX, index, sent ? "true" : "false");
        sink(combat ? "native_combat_ui_forwarded" : "native_social_ui_forwarded", fields);
    }
    return true;
}

// Any other ability of the bar stays with the original on this client; name it so a trace shows it.
void record_local_ability(Animal* actor, int index, int type) {
    if (sink && actor == Manager::Get()->GetAvatar()) {
        char fields[160]{};
        sprintf_s(fields, ",\"actor_native_id\":%u,\"ability_index\":%d,\"ability_type\":%d,\"sent\":false", actor->mID,
                  index, type);
        sink("native_ability_ui_local", fields);
    }
}

// The bar's Jump button is the player's own jump, as its Space key is: the authority jumps the creature. Consumed
// here, where trial274 saw the original jump the client's copy alone. False for every other ability, which the
// caller leaves to the original.
bool forward_jump_input(Animal* actor, int index, uintptr_t caller) {
    if (!client_bar_input(actor, caller)) {
        return false;
    }
    int type = -1;
    if (ability_route(actor, static_cast<uint32_t>(index), &type) != AbilityRoute::jump) {
        record_local_ability(actor, index, type);
        return false;
    }
    const bool sent = actor == Manager::Get()->GetAvatar() && living(actor) &&
                      native_network_jump_input(actor->mID, reinterpret_cast<uintptr_t>(actor),
                                                static_cast<uint32_t>(caller - image_base));
    record_forwarded("native_jump_ui_forwarded", actor, index, sent);
    return true;
}

// Explicit local mode 2 reserves the original singleton social meter.
// Concurrent social ownership is unqualified, so refuse a competing
// primary social input rather than mixing two original score contexts.
bool reject_competing_social_input(Animal* actor, int index, uintptr_t caller) {
    if (caller != image_base + ability_bar_caller_rva || !owned_probe_actor(probe_actor) || !current(actor) ||
        actor == probe_actor || !social_ability(actor, static_cast<uint32_t>(index))) {
        return false;
    }
    if (sink) {
        sink("native_social_probe_competing_input_rejected", ",\"reason\":\"exclusive_local_probe\"");
    }
    return true;
}

// The number given to an observed social input, and the creature's native ID when it was given; call 0 is an input
// that is not observed.
struct ObservedInput {
    uint64_t call = 0;
    uint32_t native_id = UINT32_MAX;
};

// Records the state before one of the first 512 social inputs of a creature that carries the 0x200 flag.
ObservedInput observe_input_enter(Animal* actor, int index, uintptr_t caller) {
    ObservedInput observed;
    if (caller != image_base + ability_bar_caller_rva || !on_thread() || !current(actor) ||
        !(actor->mGeneralFlags & 0x200) || !social_ability(actor, static_cast<uint32_t>(index))) {
        return observed;
    }
    if (input_calls < 512) {
        observed.call = ++input_calls;
        observed.native_id = actor->mID;
        snapshot("native_social_input_enter", actor, observed.call, 0, static_cast<uint32_t>(index));
    } else if (!exhausted && sink) {
        exhausted = true;
        sink("native_social_input_observation_exhausted", ",\"limit\":512");
    }
    return observed;
}

void __cdecl input_hook(Animal* actor, int index) {
    // The original ability bar selects the actual native index. Replicas send
    // an intention and consume this dispatch before any local order is written.
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (forward_hatch_input(actor, index, caller) || forward_ability_input(actor, index, caller) ||
        forward_jump_input(actor, index, caller) || reject_competing_social_input(actor, index, caller)) {
        return;
    }
    const ObservedInput observed = observe_input_enter(actor, index, caller);
    input_original(actor, index);
    if (observed.call) {
        snapshot("native_social_input_return", current(actor, observed.native_id), observed.call, 0,
                 static_cast<uint32_t>(index));
    }
}

void reset_social_state(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event, bool (*check)()) {
    meter_visible = nullptr;
    meter_relation_original = nullptr;
    observed_meter = nullptr;
    observed_meter_relation = 0;
    presented_meter = false;
    input_original = nullptr;
    order_entry = nullptr;
    deactivate_entry = nullptr;
    select_root_entry = nullptr;
    tick_original = nullptr;
    result_original = nullptr;
    probe_actor = nullptr;
    probe_mode = 0;
    probe_native_id = UINT32_MAX;
    pause_original = resume_original = nullptr;
    pause_calls = 0;
    pause_exhausted = false;
    completed_owner_result = false;
    network_probe = false;
    persistent_network_controller = false;
    probe_actor_id = probe_epoch = social_tick_calls = social_tick_observed = 0;
    image_base = base;
    image_end = end;
    engine_thread = thread;
    sink = event;
    thread_check = check;
    input_calls = result_calls = 0;
    exhausted = false;
}

// Exact dc04aee5... x86 exports D1E930/D48AB0 and the already qualified
// C0C070 actor-local helper. Social response branch has no global fanout.
bool social_code_pinned() {
    return bytes(0x91e930, {0x83, 0xec, 0x1c, 0x57, 0x8b, 0x7c, 0x24, 0x28, 0x83, 0xff, 0xff}) &&
           bytes(0x948ad4, {0x56, 0x57, 0xe8, 0x55, 0x5e, 0xfd, 0xff, 0x83, 0xc4, 0x08}) &&
           bytes(0x91ed37,
                 {0x6a, 0x00, 0x68, 0x00, 0x00, 0x00, 0x10, 0x8d, 0x48, 0x08, 0xe8, 0x1a, 0xac, 0xea, 0xff}) &&
           bytes(0x91ed5e, {0x89, 0x78, 0x10, 0xc7, 0x40, 0x0c, 0x00, 0x00, 0x00, 0x00}) &&
           bytes(0x80c070, {0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x55, 0x56, 0x8b, 0xf1}) &&
           bytes(0x1073bf0, {0x00, 0x00, 0x20, 0x41}) && bytes(0x107a540, {0xff, 0xff, 0x7f, 0x7f}) &&
           bytes(0x80d500, {0x83, 0xec, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x10}) &&
           bytes(0x7cb7d0, {0x56, 0x57, 0x8b, 0xf9, 0x83, 0x3f, 0x00}) && bytes(0x7cb81f, {0xc2, 0x04, 0x00}) &&
           bytes(0x81cdb8, {0x56, 0xe8, 0x42, 0x07, 0xff, 0xff, 0x8b, 0x8e, 0x4c, 0x0b, 0x00, 0x00,
                            0x8b, 0x11, 0x83, 0xc4, 0x04, 0x52, 0xe8, 0x01, 0xea, 0xfa, 0xff}) &&
           bytes(0x11906f0, {0x01, 0x00, 0x00, 0x00}) && bytes(0x1190800, {0x48, 0xf0, 0xbe, 0x31}) &&
           bytes(0x99c100, {0x55, 0x8b, 0x6c, 0x24, 0x08, 0x56, 0x8b, 0x74, 0x24, 0x20}) &&
           bytes(0x7c9476, {0x50, 0xff, 0xd1, 0x83, 0xc4, 0x20, 0x84, 0xc0}) &&
           bytes(0x7c969d, {0x50, 0xff, 0xd1, 0x83, 0xc4, 0x20, 0x84, 0xc0}) &&
           bytes(0x99bbd0, {0x83, 0xec, 0x2c, 0x53, 0x55, 0x56, 0x57}) &&
           bytes(0x99bd2f, {0xe8, 0x0c, 0xc0, 0x1c, 0x00, 0x8b, 0xc8, 0xe8, 0x05, 0x9b, 0xed, 0xff}) &&
           bytes(0x9a49f7, {0x50, 0x57, 0x8b, 0xce, 0xe8, 0xd0, 0x71, 0xff, 0xff}) &&
           bytes(0x99be1e, {0xc2, 0x08, 0x00}) &&
           bytes(0x732330, {0x56, 0x57, 0x8b, 0xf1, 0x8d, 0x44, 0x24, 0x0c, 0x50, 0x8d, 0x4e, 0x4c}) &&
           bytes(0x732346, {0xff, 0x04, 0xc1, 0x8d, 0x3c, 0xc1}) &&
           bytes(0x732353, {0x8b, 0x07, 0x5f, 0x5e, 0xc2, 0x04, 0x00}) &&
           bytes(0x732360, {0x56, 0x57, 0x8b, 0xf1, 0x8d, 0x44, 0x24, 0x0c, 0x50, 0x8d, 0x4e, 0x4c}) &&
           bytes(0x732376, {0xff, 0x0c, 0xc1, 0x8d, 0x3c, 0xc1}) &&
           bytes(0x732383, {0x8b, 0x07, 0x5f, 0x5e, 0xc2, 0x04, 0x00});
}

// The original code the social meter's presentation relies on. Two entries are bound from it: E319D0 (visible)
// and E31C70 (relation).
bool meter_code_pinned() {
    return bytes(0xa319d0, {0x83, 0xc1, 0x0c, 0xe9, 0x58, 0xec, 0x9d, 0xff}) &&
           bytes(0x410630, {0x8b, 0x49, 0x14, 0x85, 0xc9, 0x74, 0x05, 0xe9, 0x74, 0xfd, 0xff, 0xff, 0x32, 0xc0, 0xc2,
                            0x04, 0x00}) &&
           bytes(0x410110, {0x8b, 0x41, 0x14, 0x85, 0xc0, 0x74, 0x07, 0x8a, 0x80, 0x8c, 0x00, 0x00, 0x00, 0xc3}) &&
           bytes(0x4103b0, {0x55, 0x8b, 0xe9, 0x83, 0xbd, 0x88, 0x00, 0x00, 0x00, 0x00, 0x74, 0x6a}) &&
           bytes(0x410420, {0xb0, 0x01, 0x5d, 0xc2, 0x04, 0x00}) &&
           bytes(0x410430, {0xb0, 0x01, 0x5d, 0xc2, 0x04, 0x00}) &&
           bytes(0xa31c70, {0x8b, 0x44, 0x24, 0x04, 0x48, 0x83, 0xf8, 0x05}) && bytes(0xa31d03, {0xc2, 0x04, 0x00}) &&
           bytes(0xa33148, {0x8d, 0x86, 0xbc, 0x00, 0x00, 0x00, 0x89, 0x86, 0xb4, 0x00, 0x00, 0x00}) &&
           bytes(0xa33184, {0x83, 0xc0, 0x78, 0x89, 0x86, 0xac, 0x00, 0x00, 0x00}) &&
           bytes(0xa33193, {0x8d, 0x86, 0x60, 0x01, 0x00, 0x00}) &&
           bytes(0xa331cf, {0x83, 0xc0, 0x78, 0x89, 0x86, 0x50, 0x01, 0x00, 0x00}) &&
           bytes(0xa33ae1, {0x8d, 0x41, 0x14, 0x89, 0x46, 0x04, 0x85, 0xc9}) &&
           bytes(0xa33aeb,
                 {0x89, 0x11, 0x89, 0x69, 0x04, 0x89, 0x51, 0x08, 0x8b, 0xc5, 0x89, 0x41, 0x0c, 0x89, 0x51, 0x10}) &&
           bytes(0xa33284, {0x85, 0xc9, 0x74, 0x0d, 0xc7, 0x07, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x01, 0x8b, 0x50, 0x04,
                            0xff, 0xd2});
}
} // namespace

bool native_combat_ability(Animal* actor, uint32_t index) {
    return on_thread() && current(actor) && combat_ability(actor, index);
}

bool prepare_native_social(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event, bool (*check)()) {
    reset_social_state(base, end, thread, event, check);
    if (!thread || GetCurrentThreadId() != thread || !check || !social_code_pinned()) {
        if (sink) {
            sink("social_input_binding_rejected", "");
        }
        return false;
    }
    input_original = reinterpret_cast<Input>(base + 0x91e930);
    if (!meter_code_pinned()) {
        if (sink) {
            sink("social_meter_binding_rejected", "");
        }
        return false;
    }
    meter_visible = reinterpret_cast<NativeSocialAbi<Animal>::MeterVisible>(base + 0xa319d0);
    meter_relation_original = reinterpret_cast<NativeSocialAbi<Animal>::MeterRelation>(base + 0xa31c70);
    presented_meter = false;
    order_entry = reinterpret_cast<Order>(base + 0x80c070);
    deactivate_entry = reinterpret_cast<Deactivate>(base + 0x80d500);
    select_root_entry = reinterpret_cast<SelectRoot>(base + 0x7cb7d0);
    tick_original = reinterpret_cast<Tick>(base + 0x99c100);
    result_original = reinterpret_cast<Result>(base + 0x99bbd0);
    pause_original = reinterpret_cast<Pause>(base + 0x732330);
    resume_original = reinterpret_cast<Pause>(base + 0x732360);
    if (sink) {
        sink("social_input_bindings_checked",
             ",\"input_stack_bytes\":8,\"order_stack_bytes\":44,\"native_acceptance\":false");
    }
    return true;
}

NativeHook native_social_input_binding() {
    return {reinterpret_cast<void**>(&input_original), reinterpret_cast<void*>(input_hook)};
}

NativeHook native_social_tick_binding() {
    return {reinterpret_cast<void**>(&tick_original), reinterpret_cast<void*>(tick_hook)};
}

NativeHook native_social_result_binding() {
    return {reinterpret_cast<void**>(&result_original), reinterpret_cast<void*>(result_hook)};
}

NativeHook native_social_pause_binding() {
    return {reinterpret_cast<void**>(&pause_original), reinterpret_cast<void*>(pause_hook)};
}

NativeHook native_social_resume_binding() {
    return {reinterpret_cast<void**>(&resume_original), reinterpret_cast<void*>(resume_hook)};
}

NativeHook native_social_meter_relation_binding() {
    return {reinterpret_cast<void**>(&meter_relation_original), reinterpret_cast<void*>(meter_relation_hook)};
}

void sample_native_social_actor(Animal* actor, uint64_t actor_id) {
    if (on_thread() && current(actor)) {
        snapshot("native_social_actor_state", actor, 0, actor_id, UINT32_MAX);
    }
}

namespace {
// Mode 2 and above reserve the original singleton social meter: the owner's awards must be ready, the pack empty,
// and no current creature may be in one of the two behaviors below or hold a 0x10000 order.
bool social_meter_free(uint64_t bridge_id, uint64_t epoch) {
    if (!native_awards_ready(bridge_id, epoch) || !Manager::Get()->mPosseMembers.empty()) {
        return false;
    }
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) {
            continue;
        }
        auto candidate = current(static_cast<Animal*>(&noun));
        const auto candidate_tree = candidate ? tree(candidate) : nullptr;
        if (candidate_tree && (word(candidate_tree, 0x1d8) == social_respond_behavior ||
                               word(candidate_tree, 0x1d8) == 0x341bd4be || order(candidate, 0x10000))) {
            return false;
        }
    }
    return true;
}

// Original C1CDB8..C1CDCF sequence: deactivate the current native behavior,
// then let BCB7D0 release/reset orders, scratch storage and native schedule.
// No raw writes to those structures and no replacement AI are used.
// The actor is resolved again after the deactivation. False when it, its tree or the root of that tree is no longer
// the expected one; no root is selected then.
bool switch_root(Animal*& actor, uint32_t actor_native_id, const unsigned char* behavior_tree, uintptr_t from_root,
                 uintptr_t to_root) {
    deactivate_entry(actor);
    actor = current(actor, actor_native_id);
    if (!actor || tree(actor) != behavior_tree || word(behavior_tree, 0) != from_root) {
        return false;
    }
    select_root_entry(const_cast<unsigned char*>(behavior_tree), reinterpret_cast<const void*>(to_root));
    return true;
}

// Restore the original tree if presentation ownership could not be
// admitted. No mode-3 success may leave an unarmed deferral behind.
void undo_root_switch(Animal* actor, uint32_t actor_native_id, const unsigned char* behavior_tree,
                      uintptr_t selected_root, uintptr_t original_root) {
    switch_root(actor, actor_native_id, behavior_tree, selected_root, original_root);
    if (sink) {
        sink("native_social_tree_probe_rejected", ",\"reason\":\"tutorial_deferral_unavailable\"");
    }
}

// What the probe remembers once a root was selected: its owner while it is enabled or while a persistent controller
// keeps the avatar root, nothing otherwise.
void remember_probe(Animal* actor, uint32_t actor_native_id, uint64_t bridge_id, uint64_t epoch, uint32_t mode,
                    bool retained_controller) {
    const bool enable = mode != 0;
    completed_owner_result = false;
    network_probe = false;
    const bool retain = enable || retained_controller;
    probe_mode = !enable && retained_controller ? 1 : mode;
    probe_actor = retain ? actor : nullptr;
    probe_native_id = retain ? actor_native_id : UINT32_MAX;
    probe_actor_id = retain ? bridge_id : 0;
    probe_epoch = retain ? epoch : 0;
    social_tick_calls = social_tick_observed = 0;
}

void record_tree_probe(uint32_t actor_native_id, uint32_t mode, bool applied, uintptr_t old_root, uintptr_t new_root) {
    if (!sink) {
        return;
    }
    char fields[300]{};
    sprintf_s(fields,
              ",\"actor_native_id\":%u,\"mode\":%u,\"enable\":%s,\"applied\":%s,\"before_root_rva\":%u,\"requested_"
              "root_rva\":%u,\"ownership_qualified\":false",
              actor_native_id, mode, mode != 0 ? "true" : "false", applied ? "true" : "false",
              static_cast<uint32_t>(old_root - image_base), static_cast<uint32_t>(new_root - image_base));
    sink("native_social_tree_probe", fields);
}
} // namespace

bool native_social_tree_probe(Animal* actor, uint32_t actor_native_id, uint64_t bridge_id, uint64_t epoch,
                              uint32_t mode) {
    if (!on_thread() || !deactivate_entry || !select_root_entry || actor_native_id == UINT32_MAX || !bridge_id ||
        !epoch || mode > 3 || native_replica_is_client() || !native_replica_allows(replica::Mutation::ai)) {
        return false;
    }
    const bool enable = mode != 0;
    actor = current(actor, actor_native_id);
    // Only the measured ordinary-creature root <-> original-avatar root pair.
    // Selection itself leaves flags, manager avatar, player identity and scoring
    // untouched. Mode 2 separately classifies B in audited social callbacks.
    if (!living(actor) || (actor->mGeneralFlags & 0x300) || !tree(actor)) {
        return false;
    }
    auto primary = current(Manager::Get()->GetAvatar());
    const auto primary_tree = primary ? tree(primary) : nullptr;
    if (!primary || primary == actor || !primary_tree || !(primary->mGeneralFlags & 0x200) ||
        word(primary_tree, 0) != image_base + avatar_root_rva) {
        return false;
    }
    if (mode >= 2 && !social_meter_free(bridge_id, epoch)) {
        return false;
    }
    const bool retained_controller =
        persistent_network_controller && probe_actor == actor && probe_actor_id == bridge_id && probe_epoch == epoch;
    const bool entering_social = retained_controller && probe_mode == 1 && mode >= 2;
    const auto old_root = image_base + (enable && !entering_social ? creature_root_rva : avatar_root_rva);
    const auto new_root = image_base + (enable || retained_controller ? avatar_root_rva : creature_root_rva);
    const auto old_tree = tree(actor);
    if (word(old_tree, 0) != old_root || !readable(old_tree, 0x600)) {
        return false;
    }
    const auto constant_flags = word(old_tree, 0x5fc);
    const auto primary_flags = primary->mGeneralFlags;
    const auto primary_id = primary->mID;
    snapshot("native_social_tree_before", actor, 0, 0, UINT32_MAX);
    if (!switch_root(actor, actor_native_id, old_tree, old_root, new_root)) {
        return false;
    }
    actor = current(actor, actor_native_id);
    primary = current(primary, primary_id);
    const auto after = actor ? tree(actor) : nullptr;
    const bool applied = actor && primary && after == old_tree && word(after, 0) == new_root &&
                         !(actor->mGeneralFlags & 0x300) && word(after, 0x5fc) == constant_flags &&
                         primary->mGeneralFlags == primary_flags && tree(primary) == primary_tree &&
                         word(primary_tree, 0) == image_base + avatar_root_rva &&
                         Manager::Get()->GetAvatar() == primary;
    snapshot("native_social_tree_after", actor, 0, 0, UINT32_MAX);
    if (applied && mode == 3 && !defer_native_secondary_tutorial(bridge_id, epoch)) {
        undo_root_switch(actor, actor_native_id, old_tree, new_root, old_root);
        return false;
    }
    if (applied) {
        remember_probe(actor, actor_native_id, bridge_id, epoch, mode, retained_controller);
    }
    record_tree_probe(actor_native_id, mode, applied, old_root, new_root);
    return applied;
}

bool native_social_probe(Animal* actor, uint32_t actor_native_id, Animal* target, uint32_t target_native_id,
                         uint32_t ability) {
    if (!on_thread() || !order_entry || actor_native_id == UINT32_MAX || target_native_id == UINT32_MAX ||
        actor == target || native_replica_is_client() || !native_replica_allows(replica::Mutation::ability) ||
        !native_replica_allows(replica::Mutation::targeting)) {
        return false;
    }
    // The local worker command must respect the same exclusive probe as UI
    // input. A second owner cannot start a competing singleton social meter.
    if (probe_mode >= 2 && actor != probe_actor && owned_probe_actor(probe_actor)) {
        return false;
    }
    actor = current(actor, actor_native_id);
    target = current(target, target_native_id);
    if (!living(actor) || !living(target) || !tree(actor) || !actor->field_E84 || !target->field_E84 ||
        actor->mpSpeciesProfile == target->mpSpeciesProfile) {
        return false;
    }
    const bool start = ability == UINT32_MAX;
    if (!start && (!social_ability(actor, ability) || word(tree(actor), 0x1d8) != social_respond_behavior ||
                   actor->mpCombatantTarget != static_cast<Simulator::cCombatant*>(target) ||
                   actor->mIntentionTowardsTarget != 2)) {
        return false;
    }
    snapshot("native_social_probe_before", actor, 0, 0, ability);
    float priority = 0;
    std::memcpy(&priority, reinterpret_cast<const void*>(image_base + (start ? 0x107a540 : 0x1073bf0)), 4);
    // Start matches the observed C032F0->C0C070 arguments, omitting only the
    // wrapper's unqualified global-avatar posse fanout. Response reaches the
    // same original BC9960 as D1E930. Zero clear/prerequisite masks remove no
    // orders and create no prerequisite. The helper writes the two original
    // input data words (0, ability), never social score, strength or rewards.
    order_entry(actor, start ? 0x10000 : 0x10000000, 0, start ? 0x2788b9 : 0, start ? 0x40049000 : 0, priority,
                start ? 0x100000 : 0, 0, 0, start ? 0 : ability, start ? target : nullptr, false);
    actor = current(actor, actor_native_id);
    target = current(target, target_native_id);
    const auto retained = actor ? order(actor, start ? 0x10000 : 0x10000000) : nullptr;
    Animal* retained_target = nullptr;
    if (retained) {
        std::memcpy(&retained_target, retained + 0x2c, 4);
    }
    const bool queued = actor && target && retained && word(retained, 0xc) == 0 &&
                        word(retained, 0x10) == (start ? 0 : ability) && retained_target == (start ? target : nullptr);
    if (queued && start && actor == probe_actor) {
        completed_owner_result = false;
    }
    snapshot("native_social_probe_after", actor, 0, 0, ability);
    if (sink) {
        char fields[256]{};
        sprintf_s(fields,
                  ",\"actor_native_id\":%u,\"target_native_id\":%u,\"ability_index\":%u,\"start\":%s,\"queued\":%s,"
                  "\"ownership_qualified\":false",
                  actor_native_id, target_native_id, ability, start ? "true" : "false", queued ? "true" : "false");
        sink("native_social_probe_order", fields);
    }
    return queued;
}

bool native_social_control(Animal* actor, uint32_t actor_native_id, Animal* target, uint32_t target_native_id,
                           uint32_t ability) {
    if (!on_thread() || native_replica_is_client() || !current(actor, actor_native_id) || !tree(actor)) {
        return false;
    }
    if (ability != UINT32_MAX && !social_ability(actor, ability)) {
        return false;
    }
    // This is the original UI branch recorded in D1E930: the authority's live
    // behavior selects greeting versus response, never a stale replica phase.
    const bool greet = ability == UINT32_MAX || word(tree(actor), 0x1d8) != social_respond_behavior;
    return native_social_probe(actor, actor_native_id, target, target_native_id, greet ? UINT32_MAX : ability);
}

namespace {
bool inline_meter_segments(unsigned char* ui, unsigned side, uint32_t& count) {
    const auto header = ui + 0xa4 + side * 0xa4;
    const auto storage = reinterpret_cast<uintptr_t>(ui + 0xbc + side * 0xa4);
    const auto begin = word(header, 0), end = word(header, 4), capacity = word(header, 8);
    if (begin != storage || capacity != storage + 120 || end < begin || end > capacity || (end - begin) % 20) {
        return false;
    }
    count = (end - begin) / 20;
    return true;
}

bool meter_cache_valid(unsigned char* ui, size_t offset) {
    Object* cache = nullptr;
    std::memcpy(&cache, ui + offset, sizeof(cache));
    if (!cache) {
        return true;
    }
    if (!readable(cache, sizeof(uintptr_t))) {
        return false;
    }
    const auto table = *reinterpret_cast<uintptr_t*>(cache);
    if (table < image_base || table > image_end - 8 || !readable(reinterpret_cast<void*>(table), 8)) {
        return false;
    }
    const auto release = *reinterpret_cast<uintptr_t*>(table + 4);
    return release >= image_base && release < image_end;
}

void invalidate_meter_cache(unsigned char* ui, size_t offset) {
    Object* cache = nullptr;
    std::memcpy(&cache, ui + offset, sizeof(cache));
    if (cache) {
        std::memset(ui + offset, 0, sizeof(cache));
        cache->Release();
    }
}

unsigned char* social_meter(NativeSocialMeter& value, unsigned char*& layout) {
    if (!on_thread() || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    auto strategy = App::cCreatureModeStrategy::Get();
    auto display = readable(strategy, sizeof(*strategy)) ? strategy->mpDisplayStrategy.get() : nullptr;
    auto ui = readable(display, sizeof(*display)) ? display->mpUISocialRainbow.get() : nullptr;
    if (!readable(ui, 0x200) ||
        !decode_native_social_meter(ui, 0x200, static_cast<uint32_t>(image_base + native_social_ui_vtable_rva),
                                    value)) {
        return nullptr;
    }
    auto bytes = reinterpret_cast<unsigned char*>(ui);
    std::memcpy(&layout, bytes + 0x20, sizeof(layout));
    if (!readable(layout, 0xa0) || layout[0x8c] > 1) {
        return nullptr;
    }
    return bytes;
}
} // namespace

bool capture_native_social_view(uint64_t epoch, uint32_t& owner, uint32_t& actor_id, uint32_t& target,
                                network::SocialFrame& frame) {
    owner = 0;
    actor_id = target = UINT32_MAX;
    frame = {};
    if (!on_thread() || native_replica_is_client() || !epoch) {
        return false;
    }
    Animal* actor = nullptr;
    if (probe_mode == 3 && network_probe && probe_epoch == epoch && owned_probe_actor(probe_actor)) {
        actor = probe_actor;
        owner = 2;
    } else if (!probe_mode) {
        actor = Manager::Get() ? Manager::Get()->GetAvatar() : nullptr;
        owner = 1;
    }
    if (!current(actor) || !living(actor) || !tree(actor) || word(tree(actor), 0x1d8) != social_respond_behavior) {
        return false;
    }
    target = target_id(actor);
    if (target == UINT32_MAX) {
        return false;
    }
    NativeSocialMeter meter;
    unsigned char* layout = nullptr;
    auto ui = social_meter(meter, layout);
    if (!ui) {
        return false;
    }
    actor_id = actor->mID;
    frame.visible = layout[0x8c];
    frame.flags = meter.flags[0] | (meter.flags[1] << 1) | (meter.flags[2] << 2);
    frame.mode = meter.field_44;
    frame.values = meter.values;
    frame.relation = observed_meter == ui ? observed_meter_relation : 0;
    for (unsigned side = 0; side < 2; ++side) {
        if (!inline_meter_segments(ui, side, frame.segment_count[side])) {
            return false;
        }
        for (unsigned index = 0; index < frame.segment_count[side]; ++index) {
            std::memcpy(&frame.segments[side][index], ui + 0xbc + side * 0xa4 + index * 20, 20);
        }
    }
    return true;
}

bool present_native_social_view(const network::SocialFrame& frame) {
    if (!on_thread() || !native_replica_is_client() || native_replica_bootstrap_open() ||
        !native_replica_allows(replica::Mutation::presentation) || !network::valid_social_frame(frame) ||
        !meter_visible) {
        return false;
    }
    NativeSocialMeter before;
    unsigned char* layout = nullptr;
    auto ui = social_meter(before, layout);
    if (!ui) {
        return false;
    }
    for (unsigned side = 0; side < 2; ++side) {
        uint32_t count = 0;
        if (!inline_meter_segments(ui, side, count)) {
            return false;
        }
    }
    for (size_t offset : meter_cache_offsets) {
        if (!meter_cache_valid(ui, offset)) {
            return false;
        }
    }
    // Constructor-owned inline storage holds six trivial five-word drawing
    // records. Never allocate, resize past that capacity, copy a remote
    // pointer, or touch a gameplay/scoring container.
    for (unsigned side = 0; side < 2; ++side) {
        auto storage = ui + 0xbc + side * 0xa4;
        for (unsigned index = 0; index < frame.segment_count[side]; ++index) {
            std::memcpy(storage + index * 20, &frame.segments[side][index], 20);
        }
        const auto end = reinterpret_cast<uintptr_t>(storage + frame.segment_count[side] * 20);
        std::memcpy(ui + 0xa8 + side * 0xa4, &end, sizeof(end));
    }
    // Match E33230's clear-before-Release of original drawing caches. The
    // original render callback recreates them using the copied scalar view.
    for (size_t offset : meter_cache_offsets) {
        invalidate_meter_cache(ui, offset);
    }
    // Exact observed presentation fields, never the authoritative social
    // context/relationships/rewards. The original UI renderer consumes them.
    constexpr size_t offsets[]{0x3c, 0x40, 0x134, 0x138, 0x13c, 0x1d8, 0x1dc, 0x1e0};
    for (size_t i = 0; i < frame.values.size(); ++i) {
        std::memcpy(ui + offsets[i], &frame.values[i], sizeof(float));
    }
    std::memcpy(ui + 0x44, &frame.mode, sizeof(frame.mode));
    ui[0x24] = 1;
    ui[0x25] = (frame.flags >> 1) & 1;
    ui[0x26] = (frame.flags >> 2) & 1;
    if (frame.relation) {
        meter_relation_original(ui, frame.relation);
    }
    if (bool(layout[0x8c]) != bool(frame.visible) && !meter_visible(ui, frame.visible != 0)) {
        return false;
    }
    presented_meter = frame.visible != 0;
    return layout[0x8c] == frame.visible;
}

void clear_native_social_view(bool hide) {
    if (!on_thread()) {
        return;
    }
    if (!hide) {
        presented_meter = false;
        return;
    }
    if (!presented_meter || !native_replica_is_client() || !meter_visible) {
        return;
    }
    NativeSocialMeter value;
    unsigned char* layout = nullptr;
    auto ui = social_meter(value, layout);
    if (ui) {
        meter_visible(ui, false);
    }
    presented_meter = false;
}

bool ensure_native_network_player_controller(Animal* actor, uint32_t native_id, uint64_t id, uint64_t epoch,
                                             bool experimental_foreign) {
    if (!on_thread() || native_replica_is_client() || !native_awards_ready(id, epoch)) {
        return false;
    }
    actor = current(actor, native_id);
    if (!living(actor) || (actor->mGeneralFlags & 0x300) || !tree(actor)) {
        return false;
    }
    if (persistent_network_controller) {
        return probe_actor == actor && probe_native_id == native_id && probe_actor_id == id && probe_epoch == epoch &&
               (probe_mode == 1 || probe_mode == 3) && word(tree(actor), 0) == image_base + avatar_root_rva;
    }
    auto manager = Manager::Get();
    auto primary = manager ? current(manager->GetAvatar()) : nullptr;
    if (!primary || primary == actor || !manager->mPosseMembers.empty() || probe_mode) {
        return false;
    }
    const bool foreign = primary->mpSpeciesProfile != actor->mpSpeciesProfile;
    if (foreign && (!experimental_foreign || !native_actor_experimental_foreign_ready(epoch) ||
                    actor->GetCurrentBrainLevel() != 0 || !actor->mHerd || actor->mHerd == primary->mHerd)) {
        return false;
    }
    const auto primary_profile = primary->mpSpeciesProfile, actor_profile = actor->mpSpeciesProfile;
    const auto primary_herd = primary->mHerd, actor_herd = actor->mHerd;
    const auto primary_key = primary->mSpeciesKey, actor_key = actor->mSpeciesKey;
    // Mode 1 uses the already pinned native deactivate/root-selection pair;
    // no social classification, reward path or species key is changed here.
    if (!native_social_tree_probe(actor, native_id, id, epoch, 1)) {
        return false;
    }
    if (primary->mpSpeciesProfile != primary_profile || actor->mpSpeciesProfile != actor_profile ||
        primary->mHerd != primary_herd || actor->mHerd != actor_herd || primary->mSpeciesKey != primary_key ||
        actor->mSpeciesKey != actor_key) {
        return false;
    }
    persistent_network_controller = true;
    if (sink) {
        sink("native_network_player_controller", ",\"owner\":2,\"original_avatar_root\":true,\"social_scope_active\":"
                                                 "false,\"manager_avatar_changed\":false");
    }
    return true;
}

bool native_network_player_controller_matches(Animal* actor, uint64_t& id) {
    if (!on_thread() || native_replica_is_client() || !persistent_network_controller ||
        (probe_mode != 1 && probe_mode != 3) || actor != probe_actor || !current(actor, probe_native_id) ||
        !living(actor) || (actor->mGeneralFlags & 0x300) || !tree(actor) ||
        word(tree(actor), 0) != image_base + avatar_root_rva || !native_awards_ready(probe_actor_id, probe_epoch) ||
        !Manager::Get()->mPosseMembers.empty()) {
        return false;
    }
    id = probe_actor_id;
    return true;
}

void reset_native_network_player_controller() {
    if (!on_thread()) {
        return;
    }
    probe_actor = nullptr;
    probe_native_id = UINT32_MAX;
    probe_mode = 0;
    probe_actor_id = probe_epoch = 0;
    completed_owner_result = network_probe = persistent_network_controller = false;
}

bool ensure_native_network_social_owner(Animal* actor, uint32_t native_id, uint64_t id, uint64_t epoch) {
    if (probe_mode == 3) {
        return probe_actor_id == id && probe_epoch == epoch && owned_probe_actor(actor);
    }
    const bool controller = persistent_network_controller && probe_mode == 1 && probe_actor == actor &&
                            probe_native_id == native_id && probe_actor_id == id && probe_epoch == epoch;
    if ((probe_mode && !controller) || !native_social_tree_probe(actor, native_id, id, epoch, 3)) {
        return false;
    }
    network_probe = true;
    return true;
}

bool finish_native_network_social_owner(uint64_t id, uint64_t epoch) {
    if (!on_thread()) {
        return false;
    }
    if (!probe_mode) {
        return true;
    }
    if (probe_mode == 1) {
        // First DNA can follow original combat without a social exchange.
        // A matching persistent controller has no social result to tear down.
        // The caller still verifies the exact owner tutorial receipt before
        // recording completion; preserve the existing avatar tree unchanged.
        uint64_t current_id = 0;
        return persistent_network_controller && !network_probe && !completed_owner_result && probe_actor_id == id &&
               probe_epoch == epoch && native_network_player_controller_matches(probe_actor, current_id) &&
               current_id == id && !order(probe_actor, 0x10000) && !order(probe_actor, 0x10000000);
    }
    if (probe_mode != 3 || probe_actor_id != id || probe_epoch != epoch || !owned_probe_actor(probe_actor)) {
        return false;
    }
    const auto native_tree = tree(probe_actor);
    if (!completed_owner_result || !native_tree || order(probe_actor, 0x10000) || order(probe_actor, 0x10000000)) {
        return false;
    }
    // This runs outside the tick/result hooks, only after the owning client's
    // measured tutorial cycle. Use the original deactivation/root reset to
    // clear the already-resulted exchange. A persistent human controller keeps
    // the original avatar root, but releases social attribution back to mode 1.
    return native_social_tree_probe(probe_actor, probe_native_id, id, epoch, 0);
}

bool cancel_native_network_social_owner(uint64_t id, uint64_t epoch) {
    if (!on_thread() || native_replica_is_client()) {
        return false;
    }
    if (probe_mode < 2) {
        return true;
    }
    if (!network_probe || probe_mode != 3 || probe_actor_id != id || probe_epoch != epoch ||
        !owned_probe_actor(probe_actor) || !native_awards_checkpoint_idle(epoch)) {
        return false;
    }
    const bool result_completed = completed_owner_result;
    // C0D500 runs the active original behavior's deactivation callback; BCB7D0
    // then releases native orders/scratch state. Do not zero social phases,
    // invent a result or modify the counterpart's behavior data directly.
    const bool reset = native_social_tree_probe(probe_actor, probe_native_id, id, epoch, 0);
    if (sink) {
        char fields[240]{};
        sprintf_s(fields,
                  ",\"actor\":%llu,\"applied\":%s,\"native_result_already_completed\":%s,\"reward_and_tutorial_idle\":"
                  "true,\"source\":\"authenticated_action\"",
                  id, reset ? "true" : "false", result_completed ? "true" : "false");
        sink("network_social_owner_cancelled", fields);
    }
    return reset;
}

void update_native_network_social_owner(uint64_t epoch) {
    if (!on_thread() || !network_probe || probe_mode != 3 || probe_epoch != epoch || !completed_owner_result ||
        !native_awards_checkpoint_idle(epoch)) {
        return;
    }
    // A previously completed First DNA key does not schedule another tutorial.
    // Its receipt therefore cannot be the only path that releases the original
    // SocialRespond tree. The same native result/idle boundary handles both
    // cases without clearing a pending tutorial or replaying any result.
    const auto id = probe_actor_id;
    if (finish_native_network_social_owner(id, epoch) && sink) {
        char fields[120]{};
        sprintf_s(fields, ",\"actor\":%llu,\"native_result_completed\":true,\"reward_and_tutorial_idle\":true", id);
        sink("network_social_owner_released", fields);
    }
}
} // namespace sporemp
