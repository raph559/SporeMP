#pragma once
#include "native_player_context.h"
#include "native_pickup_abi.h"
#include <cstdint>

namespace Simulator {
class cCreatureAnimal;
class cInteractiveOrnament;
class cGameData;
} // namespace Simulator

namespace sporemp {
// Pinned original Creature corpse-eating callback. BC9477/BC969E pass eight
// stack words and caller-clean 20h. D70FD0 returns bool in AL with plain RET;
// actor is entry ESP+4, behavior_state ESP+18h, and delta ESP+20h. Opaque native
// callback values remain inside this bridge, never serialized or manufactured.
using NativePickupTick = NativePickupAbi<Simulator::cCreatureAnimal>::Tick;
// Dispatch must forward the original callback exactly once after its own
// replica/authority admission. Only the root adapter owns actor/epoch scope.
using NativePickupDispatch = bool (*)(NativePickupTick, Simulator::cCreatureAnimal*, double, uint32_t, uintptr_t, void*,
                                      void*, float);
inline constexpr uint32_t native_pickup_tick_rva = 0x970fd0;
inline constexpr uint32_t native_pickup_reward_caller_rva = 0x9713b2;
inline constexpr uint32_t native_pickup_action = 0xd335362d;
NativeHook native_pickup_input_observer_binding();
NativeHook native_pickup_classify_observer_binding();
// Bounded read-only census of original type-2 fossils. No spawn, movement,
// collection, unlock, claim, or runtime object pointer leaves this observer.
void sample_native_ornaments();
// Fresh, unique, live type-2 fossil lookup for a developer approach probe.
// Copies three finite coordinates, never a pointer or an unlock outcome.
bool native_fossil_position(uint32_t native_id, float (&position)[3]);

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
bool prepare_native_pickup(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event,
                           bool (*thread_check)(), NativePickupDispatch dispatch, bool (*first_feed_owner)(),
                           bool (*honor_owned_claim)());
NativePickupTick native_pickup_tick_entry();
NativeHook native_pickup_tick_binding();
// Read-only observation of primary-avatar social orders through the same
// pinned eleven-slot native order helper. Does not add a command or a reward.
NativeHook native_social_order_observer_binding();
NativeHook native_pickup_first_feed_binding();
NativeHook native_pickup_claim_binding();
// Observer of the original's routine that stocks plants with fruit (B0B330); the original runs unchanged.
NativeHook native_plant_food_stock_observer_binding();
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
// The original's own order for a click on a fruit (trial364), given on the authority for a player's creature.
bool native_plant_food_order(Simulator::cCreatureAnimal* actor, uint32_t actor_id, uint32_t fruit_native_id,
                             uint64_t seen_place);

// A fruit as it is shared: where it lies, how it is turned, and its packed state (network::pack_fruit_state).
struct NativePlantFood {
    uint32_t native_id = UINT32_MAX;
    uintptr_t address = 0;
    float place[3]{}, turn[4]{0, 0, 0, 1}, scale = 1;
    uint32_t model[3]{};
    uint32_t state = UINT32_MAX;
};

// Read one fruit of this game's world; false for anything that is not a live fruit with readable values.
bool native_plant_food_read(Simulator::cGameData* noun, NativePlantFood& out);
Simulator::cGameData* native_plant_food_find(uint32_t native_id, uintptr_t expected = 0);
// On a client: make this world's fruit lie, turn, show and be pickable as the authority's does.
bool native_plant_food_apply(Simulator::cGameData* noun, const NativePlantFood& wanted);
// Whether this game's world holds a live fruit of that number, and whether it lies at that packed place.
bool native_plant_food_present(uint32_t fruit_native_id);
bool native_plant_food_at(uint32_t fruit_native_id, uint64_t place);
bool native_pickup_order(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id,
                         Simulator::cCreatureAnimal* corpse, uint32_t corpse_native_id);
// Original type-2 fossil order observed in trials73/76. The scene caller fences
// its incarnation; this leaf rechecks current membership and original claim.
bool native_fossil_order(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id,
                         Simulator::cInteractiveOrnament* fossil, uint32_t fossil_native_id);

// Read-only view valid only inside the original callback's current state
// lifetime. Resolve state->corpse and claimant through a fresh noun census.
bool native_pickup_snapshot(Simulator::cCreatureAnimal* actor, uint32_t actor_native_id, const void* behavior_state,
                            NativePickupSnapshot& result);
} // namespace sporemp
