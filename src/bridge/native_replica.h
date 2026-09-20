#pragma once
#include "replica_policy.h"
#include "../worker/protocol.h"
namespace Simulator { class cCreatureAnimal; }
namespace sporemp {
void initialize_native_replica();
void dispose_native_replica();
bool native_replica_allows(replica::Mutation mutation) noexcept;
bool native_replica_allows_action(uint32_t action) noexcept;
// D47FC0 part-grant dispatch cases, checked against the pinned instruction export.
inline constexpr uint32_t replica_part_actions[] = {
    0x049b56d7, 0x05371f11, 0x060b4123, 0x061b1320, 0xd335362c, 0xd3353635, 0xd335363a
};
// B29960's save request, deferred save and immediate/menu save branches.
// Other messages (including load-state reset) must retain their native behavior.
inline constexpr uint32_t replica_save_messages[] = {0x01cd20f0, 0x0685dbfa, 0x0689c9b9};
bool native_replica_is_client() noexcept;
bool native_replica_bootstrap_open() noexcept;
// Engine-thread-only, after M06 wire/scene/entity validation. Lifecycle permits
// only explicit native factory/removal, never population/AI/rewards/load/save.
bool native_replica_project(bool(*callback)(void*), void* context, bool lifecycle);
void native_replica_arm_network();
bool native_replica_queue_charm_probe(Simulator::cCreatureAnimal* animal);
void native_replica_invalidated(uint64_t local_id);
void native_replica_scene_exit();
void sample_native_replica(bool force_counters = false);
worker::Result native_replica_command(const worker::Message& request);
}
