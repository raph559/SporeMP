#pragma once
#include "native_player_context.h"
#include "native_tutorial_pending.h"
#include <Spore/Simulator/cCreatureAnimal.h>
#include <array>

namespace sporemp {
bool prepare_native_awards(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event_sink, bool (*check)());
NativeHook native_award_owned_binding();
NativeHook native_award_amount_binding();
NativeHook native_award_player_binding();
NativeHook native_award_avatar_binding();
NativeHook native_award_display_binding();
NativeHook native_award_action_binding();
NativeHook native_award_property_binding();
NativeHook native_award_update_binding();
NativeHook native_award_goal_binding();
NativeHook native_award_tutorial_binding();
NativeHook native_award_species_binding();
// Foreign initialization is an explicit local experiment. Its exact actor
// must come from the fresh native factory probe or an authenticated schema4
// checkpoint. World evolution and secondary death remain unqualified.
bool enable_native_awards(Simulator::cCreatureAnimal* actor, uint64_t actor_id, uint64_t epoch,
                          bool experimental_foreign = false);
// Engine-thread scalar view of the qualified B context. Reading never enters
// StageScope, calls a reward routine, or substitutes the manager's player.
bool native_awards_ready(uint64_t actor_id, uint64_t epoch);
// Opt-in local presentation experiment. Retain the first-DNA request for B
// without marking its native tutorial completed or scheduling a world pause.
// Pending requests prevent a checkpoint and are cancelled explicitly on retire.
bool defer_native_secondary_tutorial(uint64_t actor_id, uint64_t epoch);
bool pending_native_tutorial(NativeTutorialRequest& request, uint32_t owner = 2);
// Network ownership and generation are checked before entering these leaves.
// Completion records the original native key only after the client's observed
// cinematic cycle. Neither queueing nor transport acceptance acknowledges it.
bool complete_native_tutorial(const NativeTutorialRequest& request);
enum class NativeTutorialPresentation {
    idle,
    active,
    completed,
    failed
};
bool present_native_tutorial(uint32_t token, uint32_t caption);
NativeTutorialPresentation native_tutorial_presentation();
void reset_native_tutorial_presentation();
void observe_native_tutorial_pause(bool pausing, uint32_t type, uint32_t caller_rva, int counter, int flags);
// No active owner scope or unfinished reward counter at the save boundary.
bool native_awards_checkpoint_idle(uint64_t epoch, bool allow_foreign = false);
// Diagnostic: one bit per part of the guard above that currently refuses, in the order they are tested.
uint32_t native_awards_checkpoint_busy(uint64_t epoch);

struct NativeAwardCheckpoint {
    std::array<uint32_t, 20> words{};
};

bool valid_native_award_checkpoint(const NativeAwardCheckpoint& checkpoint);
bool snapshot_native_awards(NativeAwardCheckpoint& checkpoint, uint64_t epoch, bool allow_foreign = false);
// Original D2E300 writes the actual secondary Stage as its data argument.
// No global StageScope or reward application is entered; pending work rejects.
bool write_native_secondary_award_stage(uint64_t epoch, uint32_t player_native_id, uint32_t avatar_native_id,
                                        Simulator::ISerializerWriteStream* stream, NativeAwardCheckpoint& observed);
bool restore_native_awards(Simulator::cCreatureAnimal* actor, uint64_t id, uint64_t epoch,
                           const NativeAwardCheckpoint& checkpoint, bool foreign_checkpoint = false);
bool native_awards_read_dna(uint64_t actor_id, uint64_t epoch, float& dna, bool allow_retired_corpse = false,
                            bool* current_native = nullptr);
void retire_native_awards();
void update_native_awards(uint64_t epoch);
// Engine-thread adapter for the separately audited original secondary death
// handler/restart/hatch leaves. The owner may be dead; world updates remain outside.
bool call_native_secondary_death(uint32_t native_id, uint64_t epoch, bool (*call)(Simulator::cCreatureAnimal*, void*),
                                 void* context);
void sample_native_awards();
// Read-only raw social UI fields from the current Creature display root.
void sample_native_social_ui(uint64_t epoch);
bool dispatch_native_owned_award(float amount, void(__cdecl* original)(float), uint32_t caller_rva);
// True only within a root-validated original corpse tick, for the current live
// B identity and the same current corpse still claimed by B. It does not enter
// StageScope or mutate the original actor classification/claim.
bool native_pickup_award_scope_ready();
bool native_social_award_scope_ready();
// Whole original-result snapshots, including changes before action dispatch.
void sample_native_social_owner_result(bool entering, uint64_t call, uint32_t animation);
// C2EE80 snapshots around the existing replica detour. A one-shot receipt of
// B's exact native count/relationship transition can select B only for the
// original social objective's avatar, DNA and history leaves. The full updater
// stays in the primary world context; scoring and relationships remain native.
uint64_t begin_native_social_objective_observation(void* receiver, uintptr_t caller_address);
void end_native_social_objective_observation(void* receiver, uint64_t call);

// Result-only ownership selected from the original social callback's actor
// argument. This scope itself changes no stage, avatar, score or relationship.
class NativeSocialOwnerScope {
public:
    NativeSocialOwnerScope(Simulator::cCreatureAnimal* actor, Simulator::cCreatureAnimal* target, uint64_t actor_id);
    ~NativeSocialOwnerScope();
    NativeSocialOwnerScope(const NativeSocialOwnerScope&) = delete;
    NativeSocialOwnerScope& operator=(const NativeSocialOwnerScope&) = delete;

private:
    Simulator::cCreatureAnimal* previous_actor;
    Simulator::cCreatureAnimal* previous_target;
    uint64_t previous_id;
    uint32_t previous_target_id;
};

// Per-scope observation, reset/restored by RAII. True only after the exact
// original first-feed DNA call returned under the valid B context with a finite
// nonnegative native amount and a consistent finite DNA balance. Zero is a valid
// configured native amount and must leave DNA unchanged; a positive amount may
// be capped by the original routine. This does not prove positive DNA or food
// benefit: pair the original eaten transition with separately observed native
// food consumption and actor nutrition before claiming a resource benefit.
bool native_pickup_award_scope_granted();

class NativePickupOwnerScope {
public:
    NativePickupOwnerScope(Simulator::cCreatureAnimal* actor, uint32_t owner, uint64_t id,
                           Simulator::cCreatureAnimal* corpse);
    ~NativePickupOwnerScope();
    NativePickupOwnerScope(const NativePickupOwnerScope&) = delete;
    NativePickupOwnerScope& operator=(const NativePickupOwnerScope&) = delete;

private:
    Simulator::cCreatureAnimal* previous_actor;
    Simulator::cCreatureAnimal* previous_corpse;
    uint32_t previous_owner, previous_corpse_native_id;
    uint64_t previous_id;
    bool previous_granted;
};

class NativeDamageOwnerScope {
public:
    NativeDamageOwnerScope(Simulator::cCreatureAnimal* actor, uint32_t owner, uint64_t id);
    ~NativeDamageOwnerScope();
    NativeDamageOwnerScope(const NativeDamageOwnerScope&) = delete;
    NativeDamageOwnerScope& operator=(const NativeDamageOwnerScope&) = delete;

private:
    Simulator::cCreatureAnimal* previous_actor;
    uint32_t previous_owner;
    uint64_t previous_id;
    bool previous_foreign_latched;
    uint64_t previous_foreign_epoch;
};
} // namespace sporemp
