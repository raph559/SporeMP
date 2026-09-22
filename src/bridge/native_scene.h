#pragma once
#include "../network/protocol.h"
#include <array>
#include <cstddef>
#include <cstdint>

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
};
enum class NativeSceneResult {
    accepted, unavailable, wrong_thread, invalid, full, stale, unsupported_entity,
    binding_mismatch, missing_template, native_failure
};
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
NativeSceneResult capture_native_scene(NativeSceneFrame& frame);
// Revalidate the current native incarnation immediately before converting a
// published entity to an engine command. A matching process-local noun ID and
// address are insufficient: destruction or a pool cycle can occur between two
// scene publications. The checked original E54 pool counter fences that reuse.
// Matching corpses are valid identities; callers enforce command eligibility.
NativeSceneResult validate_native_scene_action_entity(uint64_t epoch, const NativeSceneEntity& entity);
// Caller authenticates and fences the complete baseline before this operation.
// A failed projection is quarantined; it must not be retried as the same baseline.
NativeSceneResult apply_native_scene_baseline(const NativeSceneFrame& frame,
                                             const NativeSceneMapping& mapping);
NativeSceneResult spawn_native_scene_entity(const NativeSceneEntity& entity);
NativeSceneResult despawn_native_scene_entity(uint64_t id, uint64_t generation);
NativeSceneResult project_native_scene_entity(const NativeSceneEntity& entity);
// Forget client admission on disconnect/scene exit; do not reenable native AI.
void reset_native_scene_baseline();
// Call before original noun destruction, or after a successful native pool
// return while the retained noun is still valid. This keeps identical
// address/native-ID reuse from inheriting a previous incarnation.
void invalidate_native_scene_entity(uint32_t native_id);
inline void native_scene_invalidated(uint32_t native_id) { invalidate_native_scene_entity(native_id); }
const char* native_scene_result_name(NativeSceneResult result) noexcept;
}
