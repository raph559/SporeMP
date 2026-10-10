#pragma once
#include "../network/protocol.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace Simulator {
class cCreatureAnimal;
}

namespace sporemp {
// Engine-thread adapter values, not native layouts or a transport schema.
// Resource keys are instance/type/group; orientations are x/y/z/w.
using NativeSceneEntity = network::Entity;

struct NativeSceneFrame {
    static constexpr size_t capacity = network::max_entities;
    uint64_t epoch = 0;
    size_t count = 0;
    std::array<NativeSceneEntity, capacity> entities{};
};

struct NativeSceneMapping {
    // Explicit local fixture candidates. The first must actually be GetAvatar().
    uint32_t avatar_native_id = UINT32_MAX, other_native_id = UINT32_MAX;
    uint32_t controlled_owner = 0;
    // Authenticated two-owner baseline, distinct species, living brain-zero mode.
    bool experimental_foreign_owned = false;
};
enum class NativeSceneResult {
    accepted,
    unavailable,
    wrong_thread,
    invalid,
    full,
    stale,
    unsupported_entity,
    binding_mismatch,
    missing_template,
    native_failure
};
// Only the pinned D2E720 normalization caller may request this local profile.
// No pointer is serialized and the species-manager default key stays unchanged.
void* native_scene_replica_normalization(void* species_manager, void* original_profile);
bool initialize_native_scene();
void dispose_native_scene();
// Capture retains present dead nouns as explicit scalar life_state=1, keeping
// their incarnation through a same-noun native revival. Destruction and the
// pinned native pool-return cycle retire the incarnation. A retained unrendered
// pooled noun is omitted only after its complete inactive signature qualifies.
// Replica presentation uses the qualified corpse
// and revival idle poses; cause-specific death reactions/UI are not replayed.
// Capture uses local noun IDs as candidate IDs with tracked incarnations. The
// coordinator assigns globally scoped identities; these are not persistent IDs.
// primary_owner=0 is the qualified two-avatar fixture; 1 or 2 requires one
// original native primary player/flag and no other bound avatar.
NativeSceneResult capture_native_scene(NativeSceneFrame& frame, uint32_t primary_owner = 0);
// Revalidate the current native incarnation immediately before converting a
// published entity to an engine command. A matching process-local noun ID and
// address are insufficient: destruction or a pool cycle can occur between two
// scene publications. The checked original E54 pool counter fences that reuse.
// Matching corpses are valid identities; callers enforce command eligibility.
NativeSceneResult validate_native_scene_action_entity(uint64_t epoch, const NativeSceneEntity& entity);
// Caller authenticates and fences the complete baseline before this operation.
// A failed projection is quarantined; it must not be retried as the same baseline.
NativeSceneResult apply_native_scene_baseline(const NativeSceneFrame& frame, const NativeSceneMapping& mapping);
NativeSceneResult spawn_native_scene_entity(const NativeSceneEntity& entity);
NativeSceneResult despawn_native_scene_entity(uint64_t id, uint64_t generation);
NativeSceneResult project_native_scene_entity(const NativeSceneEntity& entity);
// Whether the original may advance the recharge drawn on this creature: a client's own creature only.
bool native_scene_recharge_ticks(const Simulator::cCreatureAnimal* creature);
// Whether this client holds a live native counterpart of that shared entity.
bool native_scene_entity_bound(uint64_t id);

struct NativeSceneLocalIdentity {
    uint64_t id = 0, generation = 0, owner = 0;
};

// Reverse the admitted replica mapping on the engine thread. Local noun/address
// must still identify the same current binding; never serialize native addresses.
// Eating may resolve a retained corpse. Other input routes require a living actor.
bool native_scene_local_identity(uint64_t epoch, uint32_t native_id, uintptr_t address,
                                 NativeSceneLocalIdentity& result, bool allow_corpse = false);
bool native_scene_local_fossil_identity(uint64_t epoch, uint32_t native_id, uintptr_t address,
                                        NativeSceneLocalIdentity& result);
bool native_scene_collect_fossil(uint64_t epoch, uint64_t entity, uint64_t generation, uint32_t native_id,
                                 Simulator::cCreatureAnimal* actor, uint32_t actor_native_id);
// Read the original rendered camera rays for the admitted local avatar.
// Output is a quantized unit direction; no position or native pointer crosses the wire.
bool native_scene_camera_input(uint64_t epoch, int horizontal, int forward, int32_t& packed);
// Client only. Where an admitted entity is drawn in this process's own game window, in client
// pixels, from the same original camera rays. False when it is not in the part of the view that the
// original HUD leaves free.
// `height_percent` says how high on the creature's own box to aim: a creature lying down is only
// hit low (trial195).
bool native_scene_entity_screen_point(uint64_t epoch, uint64_t entity, unsigned height_percent, int& x, int& y);
// Forget client admission on disconnect/scene exit; do not reenable native AI.
void reset_native_scene_baseline();
// Call before original noun destruction, or after a successful native pool
// return while the retained noun is still valid. This keeps identical
// address/native-ID reuse from inheriting a previous incarnation.
void invalidate_native_scene_entity(uint32_t native_id);
// Deny only local retirement of an admitted replica fossil. Authoritative
// projection, bootstrap, scene exit and unbound resources retain native lifetime.
bool preserve_native_scene_fossil(uintptr_t local_address, const char* operation, uint32_t caller_rva);

inline void native_scene_invalidated(uint32_t native_id) {
    invalidate_native_scene_entity(native_id);
}

const char* native_scene_result_name(NativeSceneResult result) noexcept;
} // namespace sporemp
