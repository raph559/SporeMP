#pragma once
#include "native_player_context.h"
#include "../network/relationship_wire.h"
#include <Spore/Simulator/cCreatureAnimal.h>
#include <array>

namespace sporemp {
struct NativeRelationshipState {
    bool present = false;
    // Original five-word value: relation, native count, befriended, killed,
    // native value. The following node word is a link, not value storage.
    std::array<uint32_t, 5> words{};
};

bool prepare_native_relationships(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event_sink,
                                  bool (*check)());
NativeHook native_relationship_manager_binding();
NativeHook native_relationship_action_binding();
bool enable_native_relationships(Simulator::cCreatureAnimal* actor, uint64_t actor_id, uint64_t epoch);
bool native_relationship_checkpoint_idle(uint64_t epoch);
bool write_native_relationship_checkpoint(uint64_t epoch, Simulator::ISerializerWriteStream* stream);
bool read_native_relationship_checkpoint(Simulator::ISerializerReadStream* stream);
bool adopt_native_relationship_checkpoint(Simulator::cCreatureAnimal* actor, uint64_t actor_id, uint64_t epoch);
void discard_native_relationship_checkpoint();
void retire_native_relationships();
// No insertion. false means unreadable; true with !present is valid empty.
bool native_relationship_read(uint32_t owner, const ResourceKey& key, NativeRelationshipState& result);
// Caller authenticates source/target ownership and generation. This records
// native branch prerequisites only; it cannot promise future combat state.
bool native_relationship_preflight(Simulator::cCreatureAnimal* source, uint64_t source_id,
                                   Simulator::cCreatureAnimal* target, uint64_t target_id, uint64_t epoch);
// A's native combat or feeding after a foreign checkpoint. Do not overlap
// B's bounded relationship observation, which requires primary state stability.
bool native_relationship_primary_input_ready(uint64_t epoch);
// The second player's own state is readable and no damage or relationship callback of its is running.
bool native_relationship_secondary_input_ready(uint64_t epoch);
// Sticky for the failed scene epoch, including retire/re-enable attempts.
// Never ready while an original callback is active.
bool native_relationship_failure_ready(uint64_t epoch, const char*& reason);
void sample_native_relationships(uint64_t epoch);
// Authority only: owner B's complete first-map relationships in ascending key
// order (count and entries; the caller owns identity and revision). No insertion.
bool native_relationship_replication_ready(uint64_t epoch);
bool capture_native_relationships(uint64_t epoch, network::RelationshipFrame& frame);
// Owner-2 client only: make B's private relationship values equal a complete
// authority snapshot. Values only: no original relationship action, reward or
// history runs, and the goal cards are re-evaluated by the existing display path.
// `changed` is false when the client already held exactly these values.
bool apply_native_relationships(uint64_t epoch, const network::RelationshipFrame& frame, bool& changed);
// Owner-2 client only: re-evaluate the original species goal cards against
// B's adopted relationship values. A replica earns nothing here.
void present_native_relationship_goals(uint64_t epoch);

// Construct around every C2EE80 call. It selects B's display copy only for an
// authenticated owner-2 client and seeds the cached values first.
class NativeRelationshipGoalScope {
public:
    explicit NativeRelationshipGoalScope(void* goal);
    ~NativeRelationshipGoalScope();
    void finish(uint32_t first_text_id, uint32_t second_text_id);
    NativeRelationshipGoalScope(const NativeRelationshipGoalScope&) = delete;
    NativeRelationshipGoalScope& operator=(const NativeRelationshipGoalScope&) = delete;

private:
    bool entered_ = false;
};

// Original destruction/pool callbacks invalidate cached identities even if an
// allocator later reuses their native ID and address.
void invalidate_native_relationship_entity(uint32_t native_id);

// Every engine-thread damage invocation must construct a scope, including
// null/untracked attackers. A neutral scope masks a nested outer B scope.
class NativeRelationshipDamageScope {
public:
    NativeRelationshipDamageScope(Simulator::cCreatureAnimal* source, uint32_t owner, uint64_t source_id,
                                  Simulator::cCreatureAnimal* target, uint64_t target_id, uint64_t epoch);
    ~NativeRelationshipDamageScope();
    NativeRelationshipDamageScope(const NativeRelationshipDamageScope&) = delete;
    NativeRelationshipDamageScope& operator=(const NativeRelationshipDamageScope&) = delete;

private:
    NativeRelationshipDamageScope* previous_ = nullptr;
    Simulator::cCreatureAnimal* source_ = nullptr;
    Simulator::cCreatureAnimal* target_ = nullptr;
    uint64_t source_id_ = 0, target_id_ = 0, serial_ = 0;
    uint32_t target_native_id_ = UINT32_MAX;
    bool entered_ = false, secondary_ = false;
    friend struct NativeRelationshipScopeAccess;
};
} // namespace sporemp
