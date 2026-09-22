#pragma once
#include "native_player_context.h"
#include "native_pickup_abi.h"
#include <cstdint>
namespace Simulator { class cCreatureAnimal; }

namespace sporemp {
// Pinned original Creature corpse-eating callback. BC9477/BC969E pass eight
// stack words and caller-clean 20h. D70FD0 returns bool in AL with plain RET;
// actor is entry ESP+4, behavior_state ESP+18h, and delta ESP+20h. Opaque native
// callback values remain inside this bridge, never serialized or manufactured.
using NativePickupTick = NativePickupAbi<Simulator::cCreatureAnimal>::Tick;
// Dispatch must forward the original callback exactly once after its own
// replica/authority admission. Only the root adapter owns actor/epoch scope.
using NativePickupDispatch = bool(*)(NativePickupTick,Simulator::cCreatureAnimal*,
    double,uint32_t,uintptr_t,void*,void*,float);
inline constexpr uint32_t native_pickup_tick_rva = 0x970fd0;
inline constexpr uint32_t native_pickup_reward_caller_rva = 0x9713b2;
inline constexpr uint32_t native_pickup_action = 0xd335362d;

struct NativePickupSnapshot {
    uint32_t actor_native_id = UINT32_MAX, corpse_native_id = UINT32_MAX;
    uint32_t claimant_native_id = UINT32_MAX, phase = 0, animation = 0;
    float food = 0, hunger = 0, health = 0;
    bool eaten = false, claimant_present = false, claimed_by_actor = false;
    bool avatar_classified = false, first_feed = false;
};

// Compatibility/executable checks must already have passed. Preparation checks
// exact pinned instructions and records engine-thread identity; it installs no
// hook and performs no native action. The root actor adapter owns integration.
bool prepare_native_pickup(uintptr_t base, uintptr_t end, DWORD thread,
    PlayerContextEvent event, bool(*thread_check)(),NativePickupDispatch dispatch,
    bool(*first_feed_owner)(),bool(*honor_owned_claim)());
NativePickupTick native_pickup_tick_entry();
NativeHook native_pickup_tick_binding();
NativeHook native_pickup_first_feed_binding();
NativeHook native_pickup_claim_binding();
// first_feed_owner is true only for a freshly validated scoped B actor: skips
// only the first-feed avatar classification, retaining eaten/diet/native award
// checks. honor_owned_claim is true only when another current owned actor holds
// the actual native claim: routes through original non-avatar claim refusal.
// Both callbacks return false outside the root's original tick scope. Neither
// branch modifies the actor's avatar bit or a native claim/payload field.

// Caller first fences authority, scene, actor/target generation and ownership.
// The leaf independently checks current noun membership and exact native IDs.
// Calls original SetCreatureTarget and the actor-local C0C070 order helper.
// It deliberately omits C032F0's global-avatar posse fanout. Success means the
// original retained order is read back, never that eating or reward happened.
// It retains no actor/corpse pointer and never writes claim/eaten/food/DNA.
bool native_pickup_order(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id,
    Simulator::cCreatureAnimal* corpse, uint32_t corpse_native_id);

// Read-only view valid only inside the original callback's current state
// lifetime. Resolve state->corpse and claimant through a fresh noun census.
bool native_pickup_snapshot(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id,
    const void* behavior_state, NativePickupSnapshot& result);
}
