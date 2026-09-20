#include "native_replica.h"
#include "native_actors.h"
#include "native_persistence_abi.h"
#include "native_replica_abi.h"
#include "native_branch.h"
#include "detour_transaction.h"
#include <Spore/Simulator/SubSystem/GamePersistenceManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

namespace sporemp {
namespace {
using replica::Decision;
using replica::Mutation;
using replica::Role;
std::unique_ptr<replica::Policy> policy;
std::unique_ptr<replica::Registry> objects;
replica::Fence source{};
std::atomic<DWORD> engine_thread{0};
std::atomic<bool> client{false}, armed{false};
std::atomic<uint64_t> foreign{0};
uint64_t exported = 0;
ULONGLONG next_counter_sample = 0;
bool attached = false;
bool network_lifecycle = false;
using Persistence = Simulator::cGamePersistenceManager;
using Save = NativePersistenceAbi<Persistence>::Save;
Save save_original = nullptr;
using Load = NativePersistenceAbi<Persistence>::Load;
Load load_original = nullptr;
using Abi = NativeReplicaAbi<Simulator::cCreatureAnimal>;
using Hunger = Abi::Hunger;
Hunger hunger_original = nullptr;
Abi::DnaSet dna_set_original = nullptr;
Abi::Hunger cooldown_original = nullptr;
Abi::Lifecycle death_original = nullptr, revive_original = nullptr, grow_original = nullptr;
Abi::Brain brain_original = nullptr;
Abi::Remove remove_original = nullptr;
Abi::Unlock unlock_original = nullptr;
Abi::Lock lock_original = nullptr;
Abi::HerdUpdate herd_original = nullptr;
Abi::Population population_original = nullptr, population_event_original = nullptr;
Abi::SceneEvents scene_events_original = nullptr;
Abi::AbilityUse ability_use_original = nullptr;
Abi::PersistenceMessage persistence_message_original = nullptr;
void* charm_branch_original = nullptr;
void* charm_branch_resume = nullptr;
void* social_branch_original = nullptr;
void* social_branch_resume = nullptr;
void* charge_branch_original = nullptr;
void* charge_branch_resume = nullptr;
void* attack_timer_original = nullptr;
void* attack_timer_resume = nullptr;
void* attack_reset_original = nullptr;
void* attack_reset_resume = nullptr;
void* menu_load_original = nullptr;
void* menu_load_resume = nullptr;
void* menu_start_original = nullptr;
void* menu_start_resume = nullptr;
void* menu_click_original = nullptr;
void* menu_click_resume = nullptr;
Abi::AnimalUpdate animal_update_original = nullptr;
Abi::SocialResult social_result_original = nullptr;
Simulator::cCreatureAnimal* charm_probe = nullptr;
uint64_t charm_probe_sequence = 0, social_readouts = 0;
using Animal = Simulator::cCreatureAnimal;
using Create = NativeReplicaCreateAbi<Animal, Math::Vector3, Simulator::cSpeciesProfile, Simulator::cHerd>::Create;
Create create_original = nullptr;
enum Path : size_t { cooldown, death, revive, grow, brain, remove, unlock, lock, create, herd_update,
    population, population_event, scene_events, ability_use, part_action,
    save_request, save_deferred, save_menu, charm_expiry, social_result, charge_impact, attack_timer, filename_load, menu_load, menu_start, menu_click, path_count };
const char* path_names[] = {"cooldown", "death", "revive", "grow", "brain", "remove", "unlock", "lock", "create", "herd_update",
    "population", "population_event", "scene_events", "ability_use", "part_action",
    "save_request", "save_deferred", "save_menu", "charm_expiry", "social_result", "charge_impact", "attack_timer", "filename_load", "menu_load", "menu_start", "menu_click"};
uint64_t path_allowed[path_count]{}, path_blocked[path_count]{};
bool on_thread();
void event(const char* name, const char* format, ...);
bool allow_path(Path path, Mutation mutation) {
    const bool allowed = native_replica_allows(mutation);
    if (on_thread() && (!client || armed)) {
        ++(allowed ? path_allowed[path] : path_blocked[path]);
        if (!allowed && path_blocked[path] <= 4)
            event("replica_path_denied", ",\"path\":\"%s\",\"attempt\":%llu", path_names[path], path_blocked[path]);
    }
    return allowed;
}
uint64_t projected_local = 0, projected_sample = 0;
replica::Vitals projected{};

bool on_thread() { return engine_thread && GetCurrentThreadId() == engine_thread; }
void event(const char* name, const char* format = "", ...) {
    if (!on_thread()) return;
    char fields[1800]{};
    va_list args; va_start(args, format); const int count = vsprintf_s(fields, format, args); va_end(args);
    if (count >= 0) native_actor_worker_event(name, fields);
}
bool apply(void*, uint64_t local_id, const replica::Vitals& values) {
    return native_actor_apply_vitals(local_id, values);
}
bool __fastcall save_hook(Persistence* self, void*, const wchar_t* path, bool a) {
    if (!native_replica_allows(Mutation::save)) return false;
    return save_original(self, path, a);
}
void __fastcall load_hook(Persistence* self, void*, const wchar_t* path) {
    // The original galaxy menu and M04 loader share this filename entry.
    // Cut before it sets the loading flag, changes the filename or enters
    // LoadGame mode. A disconnected replica never reopens bootstrap.
    if (allow_path(filename_load, Mutation::spawn)) load_original(self, path);
}
bool __fastcall persistence_message_hook(void* self, void*, unsigned message, void* payload) {
    // The original listener inlines a full save; it never calls B29600.
    // Deny before it pauses/schedules, modifies progress or enters serialization.
    for (size_t i = 0; i < _countof(replica_save_messages); ++i) {
        if (message == replica_save_messages[i] &&
            !allow_path(static_cast<Path>(save_request + i), Mutation::save)) return false;
    }
    return persistence_message_original(self, message, payload);
}
void __fastcall hunger_hook(Simulator::cCreatureAnimal* self, void*, float dt) {
    if (!native_replica_allows(Mutation::timer)) return;
    hunger_original(self, dt);
}
void __cdecl dna_set_hook(float value) {
    if (native_replica_allows(Mutation::apply_state) || native_replica_allows(Mutation::progression)) dna_set_original(value);
}
void __fastcall cooldown_hook(Animal* self, void*, float dt) {
    if (allow_path(cooldown, Mutation::timer)) cooldown_original(self, dt);
}
void __fastcall ability_use_hook(Animal* self, void*, unsigned index) {
    if (allow_path(ability_use, Mutation::ability)) ability_use_original(self, index);
}
void __fastcall death_hook(Animal* self, void*) {
    if (allow_path(death, Mutation::death)) death_original(self);
}
void __fastcall revive_hook(Animal* self, void*) {
    if (allow_path(revive, Mutation::death)) revive_original(self);
}
void __fastcall grow_hook(Animal* self, void*) {
    if (allow_path(grow, Mutation::progression)) grow_original(self);
}
void __fastcall brain_hook(Animal* self, void*, int level) {
    if (allow_path(brain, Mutation::progression)) brain_original(self, level);
}
void __fastcall remove_hook(Animal* self, void*, bool immediate) {
    if (on_thread() && network_lifecycle && policy && policy->applying()) {
        remove_original(self, immediate); return;
    }
    if (allow_path(remove, Mutation::death)) remove_original(self, immediate);
}
bool __fastcall unlock_hook(void* self, void*, unsigned instance, unsigned group, int cost) {
    return allow_path(unlock, Mutation::pickup) && unlock_original(self, instance, group, cost);
}
bool __fastcall lock_hook(void* self, void*, unsigned instance, unsigned group) {
    return allow_path(lock, Mutation::pickup) && lock_original(self, instance, group);
}
Animal* __cdecl create_hook(const Math::Vector3& at, Simulator::cSpeciesProfile* species, int age,
    Simulator::cHerd* herd, bool avatar, bool cast) {
    if (on_thread() && network_lifecycle && policy && policy->applying())
        return create_original(at, species, age, herd, avatar, cast);
    if (!allow_path(create, Mutation::spawn)) return nullptr;
    return create_original(at, species, age, herd, avatar, cast);
}
void __fastcall herd_hook(void* self, void*, unsigned dt) {
    // Herd population/egg timers, pregnancy, culling and patrol choices.
    // Native per-animal animation updates are separate and remain running.
    if (allow_path(herd_update, Mutation::spawn)) herd_original(self, dt);
}
void __fastcall population_hook(void* self, void*) {
    // Native D4F868 dereferences the factory result without checking null.
    // Deny the whole population request before its native allocations/side effects.
    if (allow_path(population, Mutation::spawn)) population_original(self);
}
void __fastcall population_event_hook(void* self, void*) {
    if (allow_path(population_event, Mutation::spawn)) population_event_original(self);
}
void __fastcall scene_events_hook(void* self, void*, unsigned long long clock) {
    if (allow_path(scene_events, Mutation::timer)) scene_events_original(self, clock);
}
bool __cdecl allow_charm_expiry() { return allow_path(charm_expiry, Mutation::timer); }
bool __cdecl allow_social_result() { return allow_path(social_result, Mutation::progression); }
bool __cdecl allow_charge_impact() { return allow_path(charge_impact, Mutation::damage); }
bool __cdecl allow_attack_timer() { return allow_path(attack_timer, Mutation::timer); }
bool __cdecl allow_menu_load() { return allow_path(menu_load, Mutation::spawn); }
bool __cdecl allow_menu_start() { return allow_path(menu_start, Mutation::spawn); }
bool __cdecl allow_menu_click() { return allow_path(menu_click, Mutation::spawn); }
// C0B065..C0B0C6 only decrements/releases charm state. C0AE30's movement,
// animation, effect updates and final graphics work execute on both paths.
SPOREMP_NATIVE_BRANCH(charm_branch_hook, allow_charm_expiry, charm_branch_original, charm_branch_resume)
// C2EEE7..C2F0F6 dispatches reward/progression notifications. Native relationship
// reads/cache updates precede it; the original tail still fills BOTH UI outputs.
SPOREMP_NATIVE_BRANCH(social_branch_hook, allow_social_result, social_branch_original, social_branch_resume)
// Original C1E8B0 charge movement precedes this slice. The slice allocates a
// collision query, records hit targets, strikes them and adds target impulses
// even when strike returns false. Skip before query lifetime begins, resuming
// the original animation completion check with the same native stack frame.
SPOREMP_NATIVE_BRANCH(charge_branch_hook, allow_charge_impact, charge_branch_original, charge_branch_resume)
// Freeze only the authoritative no-attack timer. The hit-event consumed byte,
// native effects and animation completion still execute, avoiding repeated FX.
SPOREMP_NATIVE_BRANCH(attack_timer_hook, allow_attack_timer, attack_timer_original, attack_timer_resume)
SPOREMP_NATIVE_BRANCH(attack_reset_hook, allow_attack_timer, attack_reset_original, attack_reset_resume)
// DEC130 drives the saved/new-world action, including its loading-screen
// countdown BEFORE the filename loader runs. Skip before those writes/calls.
// The original epilogue restores ESI/ESP and pops its opaque argument (RET 4).
SPOREMP_NATIVE_BRANCH(menu_load_hook, allow_menu_load, menu_load_original, menu_load_resume)
// The saved-world Play action hides its controls and schedules DEC130 before
// that callback runs. Cut its load-only branch before either operation. The
// other DF2380 branch, including ordinary selection/camera UI, stays original.
SPOREMP_NATIVE_BRANCH(menu_start_hook, allow_menu_start, menu_start_original, menu_start_resume)
// Ordinary galaxy saved-world Play: event 287259F6, control 0375408C.
// Skip only this action before DF6D80 hides the panel and the controller is
// scheduled. The original handled=true epilogue preserves other menu controls.
SPOREMP_NATIVE_BRANCH(menu_click_hook, allow_menu_click, menu_click_original, menu_click_resume)
void __fastcall animal_update_hook(Animal* self, void*, unsigned milliseconds) {
    const bool probe = on_thread() && self == charm_probe;
    const float saved_time = probe ? self->mCharmTime : 0;
    const float saved_attack_time = probe ? self->mNoAttackTimer : 0;
    if (probe) {
        charm_probe = nullptr;
        // One marked adversarial local timer value, restored in this same native
        // callback. This does not fabricate a worker outcome or an actual charm.
        self->mCharmTime = 0.000001f;
        self->mNoAttackTimer = 0.000001f;
    }
    animal_update_original(self, milliseconds);
    if (probe) {
        const bool denied = self->mCharmTime == 0.000001f;
        const float observed_time = self->mCharmTime;
        self->mCharmTime = saved_time;
        const float attack_time = self->mNoAttackTimer;
        self->mNoAttackTimer = saved_attack_time;
        event("replica_attack_timer_probe", ",\"probe\":%llu,\"native_update_ms\":%u,\"denied\":%s,"
            "\"restored\":%s,\"attempted_time\":%.9g,\"observed_time\":%.9g,\"original_time\":%.9g,"
            "\"adversarial_field_fixture\":true,\"actual_ability\":false",
            charm_probe_sequence,milliseconds,attack_time==0.000001f?"true":"false",
            self->mNoAttackTimer==saved_attack_time?"true":"false",double(0.000001f),double(attack_time),double(saved_attack_time));
        event("replica_charm_probe", ",\"probe\":%llu,\"native_update_ms\":%u,\"denied\":%s,"
            "\"restored\":%s,\"attempted_time\":%.9g,\"observed_time\":%.9g,\"original_time\":%.9g,"
            "\"adversarial_field_fixture\":true,\"actual_social_charm\":false",
            charm_probe_sequence,milliseconds,denied?"true":"false",self->mCharmTime==saved_time?"true":"false",
            double(0.000001f),double(observed_time),double(saved_time));
    }
}
void __fastcall social_result_hook(void* self, void*, unsigned* first, unsigned* second) {
    const bool inspect = on_thread() && client && armed;
    unsigned before = 0;
    if (inspect) memcpy(&before,static_cast<const char*>(self)+0x9c,4);
    social_result_original(self,first,second);
    if (inspect) {
        unsigned after = 0;
        memcpy(&after,static_cast<const char*>(self)+0x9c,4);
        ++social_readouts;
        if (social_readouts <= 16 || before != after)
            event("replica_social_readout", ",\"sample\":%llu,\"first_text_id\":%u,\"second_text_id\":%u,"
                "\"award_unchanged\":%s,\"original_ui_tail\":true",social_readouts,*first,*second,before==after?"true":"false");
    }
}
NativeHook hooks[] = {
    {reinterpret_cast<void**>(&save_original), reinterpret_cast<void*>(save_hook)},
    {reinterpret_cast<void**>(&load_original), reinterpret_cast<void*>(load_hook)},
    {reinterpret_cast<void**>(&hunger_original), reinterpret_cast<void*>(hunger_hook)},
    {reinterpret_cast<void**>(&dna_set_original), reinterpret_cast<void*>(dna_set_hook)},
    {reinterpret_cast<void**>(&cooldown_original), reinterpret_cast<void*>(cooldown_hook)},
    {reinterpret_cast<void**>(&death_original), reinterpret_cast<void*>(death_hook)},
    {reinterpret_cast<void**>(&revive_original), reinterpret_cast<void*>(revive_hook)},
    {reinterpret_cast<void**>(&grow_original), reinterpret_cast<void*>(grow_hook)},
    {reinterpret_cast<void**>(&brain_original), reinterpret_cast<void*>(brain_hook)},
    {reinterpret_cast<void**>(&remove_original), reinterpret_cast<void*>(remove_hook)},
    {reinterpret_cast<void**>(&unlock_original), reinterpret_cast<void*>(unlock_hook)},
    {reinterpret_cast<void**>(&lock_original), reinterpret_cast<void*>(lock_hook)},
    {reinterpret_cast<void**>(&create_original), reinterpret_cast<void*>(create_hook)},
    {reinterpret_cast<void**>(&herd_original), reinterpret_cast<void*>(herd_hook)},
    {reinterpret_cast<void**>(&population_original), reinterpret_cast<void*>(population_hook)},
    {reinterpret_cast<void**>(&population_event_original), reinterpret_cast<void*>(population_event_hook)},
    {reinterpret_cast<void**>(&scene_events_original), reinterpret_cast<void*>(scene_events_hook)},
    {reinterpret_cast<void**>(&ability_use_original), reinterpret_cast<void*>(ability_use_hook)},
    {reinterpret_cast<void**>(&persistence_message_original), reinterpret_cast<void*>(persistence_message_hook)},
    {&charm_branch_original, reinterpret_cast<void*>(charm_branch_hook)},
    {&social_branch_original, reinterpret_cast<void*>(social_branch_hook)},
    {&charge_branch_original, reinterpret_cast<void*>(charge_branch_hook)},
    {&attack_timer_original, reinterpret_cast<void*>(attack_timer_hook)},
    {&attack_reset_original, reinterpret_cast<void*>(attack_reset_hook)},
    {&menu_load_original, reinterpret_cast<void*>(menu_load_hook)},
    {&menu_start_original, reinterpret_cast<void*>(menu_start_hook)},
    {&menu_click_original, reinterpret_cast<void*>(menu_click_hook)},
    {reinterpret_cast<void**>(&animal_update_original), reinterpret_cast<void*>(animal_update_hook)},
    {reinterpret_cast<void**>(&social_result_original), reinterpret_cast<void*>(social_result_hook)}
};
bool decode_float(uint64_t value, float& result) {
    if (value > UINT32_MAX) return false;
    const auto bits = static_cast<uint32_t>(value);
    memcpy(&result, &bits, sizeof(bits)); return true;
}
uint32_t bits(float value) { uint32_t result; memcpy(&result, &value, sizeof(result)); return result; }
worker::Result result(Decision decision) {
    using worker::Result;
    return decision == Decision::accepted ? Result::accepted : decision == Decision::stale ? Result::stale :
        decision == Decision::busy || decision == Decision::full ? Result::busy : Result::invalid;
}
}
bool native_replica_is_client() noexcept { return client.load(); }
bool native_replica_bootstrap_open() noexcept { return client.load() && !armed.load(); }
void native_replica_arm_network() {
    if (on_thread() && client && policy) { armed = true; projected_local = projected_sample = 0; }
}
bool native_replica_project(bool(*callback)(void*), void* context, bool lifecycle) {
    if (!on_thread() || !client || !armed || !policy || policy->applying() || network_lifecycle) return false;
    network_lifecycle = lifecycle;
    const bool result = policy->project(callback, context);
    network_lifecycle = false;
    return result;
}
bool native_replica_queue_charm_probe(Animal* animal) {
    if (!animal || !on_thread() || !client || !armed || charm_probe || animal->mpCharmer) return false;
    charm_probe = animal; ++charm_probe_sequence; return true;
}
bool native_replica_allows_action(uint32_t action) noexcept {
    if (action == 0x045ab96e) return native_replica_allows(Mutation::reward);
    for (const auto id : replica_part_actions)
        if (id == action) return allow_path(part_action, Mutation::pickup);
    return true;
}
bool native_replica_allows(Mutation mutation) noexcept {
    // Off has no policy object, hooks or counters. Role cannot change at runtime.
    if (!engine_thread) return true;
    if (!on_thread()) { ++foreign; return !client.load(); }
    if (!policy) return false;
    // A replica's existing native save must finish loading before adoption.
    // This isolated bootstrap is never exportable and cannot save. Once armed,
    // disconnect/scene exit never returns to the bootstrap exception.
    if (client && !armed && mutation != Mutation::save && mutation != Mutation::apply_state) return true;
    return policy->allow(mutation);
}
void native_replica_invalidated(uint64_t id) {
    // No diagnostic receiver may outlive a native invalidation boundary.
    if (on_thread()) charm_probe = nullptr;
    if (objects && on_thread() && id) objects->invalidate(id);
    if (id == projected_local) projected_local = 0;
}
void native_replica_scene_exit() {
    if (!objects || !on_thread()) return;
    charm_probe = nullptr;
    objects->disconnect();
    projected_local = 0;
    event("replica_disconnected", ",\"reason\":\"scene_exit\",\"armed\":%s", armed ? "true" : "false");
}
void sample_native_replica(bool force_counters) {
    if (!policy || !on_thread()) return;
    const auto& counters = policy->counters();
    if (client && objects->link() == replica::Link::active && projected_local && projected_sample) {
        replica::Vitals current;
        const bool readable = native_actor_read_vitals(projected_local, current);
        const bool equal = readable && current.health == projected.health && current.energy == projected.energy &&
            current.hunger == projected.hunger && current.dna == projected.dna;
        event("replica_projection_audit", ",\"actor\":%llu,\"sample\":%llu,\"readable\":%s,\"matches\":%s,"
            "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"dna\":%.9g",
            projected_local, projected_sample, readable?"true":"false", equal?"true":"false",
            double(current.health),double(current.energy),double(current.hunger),double(current.dna));
        if (!equal) {
            objects->disconnect();
            event("replica_drift_quarantined", ",\"actor\":%llu,\"sample\":%llu", projected_local, projected_sample);
            projected_local = 0;
        }
    }
    // Keep every mutation counted and every projection check intact. Repeating
    // all cumulative counters every app sample exhausted the bounded M03 trace
    // in the .20 soak. Commands/disposal force a complete counter snapshot.
    const auto now = GetTickCount64();
    if (!force_counters && now < next_counter_sample) return;
    next_counter_sample = now + 5000;
    event("replica_audit", ",\"role\":\"%s\",\"armed\":%s,\"link\":%u,\"baseline\":%llu,"
        "\"applied\":%llu,\"rejected\":%llu,\"invalidated\":%llu,\"outbound_blocked\":%llu,\"foreign\":%llu",
        client ? "replica" : "authority", armed ? "true" : "false", unsigned(objects->link()), objects->baseline(),
        counters.applied, counters.rejected, counters.invalidated, counters.outbound_blocked, foreign.load());
    for (size_t i = 0; i < static_cast<size_t>(Mutation::count); ++i)
        event("replica_mutation_audit", ",\"domain\":\"%s\",\"allowed\":%llu,\"blocked\":%llu",
            replica::name(static_cast<Mutation>(i)), counters.allowed[i], counters.blocked[i]);
    for (size_t i = 0; i < path_count; ++i)
        event("replica_path_audit", ",\"path\":\"%s\",\"allowed\":%llu,\"blocked\":%llu", path_names[i], path_allowed[i], path_blocked[i]);
}
worker::Result native_replica_command(const worker::Message& request) {
    using worker::Result;
    if (!policy || !on_thread()) return Result::unavailable;
    const auto& v = request.values;
    const size_t used = v[0] == 1 || v[0] == 2 ? 5 : v[0] == 3 ? 9 :
        v[0] == 4 || v[0] == 5 ? 2 : v[0] == 6 || v[0] == 7 ? 1 : 0;
    if (!used) return Result::invalid;
    for (size_t i = used; i < v.size(); ++i) if (v[i]) return Result::invalid;
    Decision decision = Decision::invalid;
    if (v[0] == 5) {
        replica::Vitals values;
        if (!policy->may_publish()) { sample_native_replica(true); return Result::unavailable; }
        if (!native_actor_read_vitals(v[1], values) || exported == UINT64_MAX) return Result::unavailable;
        ++exported;
        event("replica_source_sample", ",\"request\":%llu,\"worker_low\":%llu,\"worker_high\":%llu,\"source_scene\":%llu,"
            "\"source_entity\":%llu,\"entity_generation\":1,\"sample\":%llu,\"health_bits\":%u,"
            "\"energy_bits\":%u,\"hunger_bits\":%u,\"dna_bits\":%u,\"native_state_sample\":true",
            request.sequence, source.worker[0], source.worker[1], request.epoch, v[1], exported,
            bits(values.health), bits(values.energy), bits(values.hunger), bits(values.dna));
        return Result::accepted;
    }
    if (v[0] == 6) { sample_native_replica(true); return Result::accepted; }
    if (!client) return Result::unavailable;
    if (v[0] == 7) {
        if (!armed || policy->applying()) return Result::unavailable;
        const bool passed = native_actor_probe_replica_denials();
        sample_native_replica(true);
        return passed ? Result::accepted : Result::invalid;
    }
    if (v[0] == 1) {
        const replica::Fence fence{{v[1], v[2]}, v[3]};
        replica::Vitals current;
        const auto status = native_actor_worker_status();
        if (!native_actor_read_vitals(status.values[3], current)) return Result::unavailable;
        decision = objects->begin(fence, v[4]);
        if (decision == Decision::accepted) { armed = true; projected_local = projected_sample = 0; }
    } else if (v[0] == 2) {
        replica::Vitals current;
        if (v[1] != objects->baseline()) return Result::stale;
        // v[2] remote entity, v[3] incarnation, v[4] existing local avatar ID.
        if (!native_actor_read_vitals(v[4], current)) return Result::unavailable;
        decision = objects->bind({v[2], v[3]}, v[4]);
        if (decision == Decision::accepted) projected_local = v[4];
    } else if (v[0] == 3) {
        replica::Update update;
        update.fence = objects->fence(); update.baseline = v[1];
        update.entity = {v[2], v[3]}; update.sequence = v[4];
        if (!decode_float(v[5], update.state.health) || !decode_float(v[6], update.state.energy) ||
            !decode_float(v[7], update.state.hunger) || !decode_float(v[8], update.state.dna)) return Result::invalid;
        decision = objects->apply(update, apply, nullptr);
        if (decision == Decision::accepted) { projected = update.state; projected_sample = update.sequence; }
    } else if (v[0] == 4) {
        if (v[1] != objects->baseline()) return Result::stale;
        objects->disconnect(); projected_local = 0; decision = Decision::accepted;
    }
    event("replica_request", ",\"request\":%llu,\"operation\":%llu,\"baseline\":%llu,\"decision\":\"%s\"",
        request.sequence, v[0], objects->baseline(), replica::name(decision));
    sample_native_replica(true);
    return result(decision);
}
void initialize_native_replica() {
    wchar_t role[32]{}, generation[64]{};
    const auto length = GetEnvironmentVariableW(L"SPOREMP_M05_ROLE", role, _countof(role));
    if (!length) return;
    engine_thread = GetCurrentThreadId();
    client = wcscmp(role, L"replica") == 0;
    worker::Generation identity{};
    if ((wcscmp(role, L"authority") && !client) || length >= _countof(role) ||
        GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", generation, _countof(generation)) != 32 ||
        !worker::generation_from_hex(generation, identity)) { PostQuitMessage(36); return; }
    policy = std::make_unique<replica::Policy>(client ? Role::replica : Role::authority);
    objects = std::make_unique<replica::Registry>(*policy);
    memcpy(source.worker.data(), identity.data(), identity.size());
    // Same B29600 prefix and Save ABI already inspected/qualified in M04.
    const unsigned char prefix[] = {0x83,0xec,0x10,0x55,0x8b,0xe9,0xe8,0x95,0x3d,0x01,0x00,0x8b,0xc8,0xe8,0x7e,0x18};
    const auto image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    save_original = reinterpret_cast<Save>(image + 0x729600);
    load_original = reinterpret_cast<Load>(image + 0x728020);
    hunger_original = reinterpret_cast<Hunger>(image + 0x802e10);
    dna_set_original = reinterpret_cast<Abi::DnaSet>(image + 0x92e480);
    cooldown_original = reinterpret_cast<Abi::Hunger>(image + 0x815470);
    death_original = reinterpret_cast<Abi::Lifecycle>(image + 0x808210);
    revive_original = reinterpret_cast<Abi::Lifecycle>(image + 0x802d00);
    grow_original = reinterpret_cast<Abi::Lifecycle>(image + 0x8223a0);
    brain_original = reinterpret_cast<Abi::Brain>(image + 0x802ab0);
    remove_original = reinterpret_cast<Abi::Remove>(image + 0x803280);
    unlock_original = reinterpret_cast<Abi::Unlock>(image + 0x196da0);
    lock_original = reinterpret_cast<Abi::Lock>(image + 0x196e10);
    create_original = reinterpret_cast<Create>(image + 0x809b40);
    herd_original = reinterpret_cast<Abi::HerdUpdate>(image + 0x86d960);
    population_original = reinterpret_cast<Abi::Population>(image + 0x94f290);
    population_event_original = reinterpret_cast<Abi::Population>(image + 0x94edd0);
    scene_events_original = reinterpret_cast<Abi::SceneEvents>(image + 0x952270);
    ability_use_original = reinterpret_cast<Abi::AbilityUse>(image + 0x819900);
    persistence_message_original = reinterpret_cast<Abi::PersistenceMessage>(image + 0x729960);
    charm_branch_original = reinterpret_cast<void*>(image + 0x80b065);
    charm_branch_resume = reinterpret_cast<void*>(image + 0x80b0c6);
    social_branch_original = reinterpret_cast<void*>(image + 0x82eee7);
    social_branch_resume = reinterpret_cast<void*>(image + 0x82f0f6);
    charge_branch_original = reinterpret_cast<void*>(image + 0x81ebdb);
    charge_branch_resume = reinterpret_cast<void*>(image + 0x81ea58);
    attack_timer_original = reinterpret_cast<void*>(image + 0x81e8ba);
    attack_timer_resume = reinterpret_cast<void*>(image + 0x81e8dc);
    attack_reset_original = reinterpret_cast<void*>(image + 0x81f107);
    attack_reset_resume = reinterpret_cast<void*>(image + 0x81f10f);
    menu_load_original = reinterpret_cast<void*>(image + 0x9ec139);
    menu_load_resume = reinterpret_cast<void*>(image + 0x9ec30c);
    menu_start_original = reinterpret_cast<void*>(image + 0x9f2391);
    menu_start_resume = reinterpret_cast<void*>(image + 0x9f250c);
    menu_click_original = reinterpret_cast<void*>(image + 0x9f7a26);
    menu_click_resume = reinterpret_cast<void*>(image + 0x9f7a8c);
    animal_update_original = reinterpret_cast<Abi::AnimalUpdate>(image + 0x80ae30);
    social_result_original = reinterpret_cast<Abi::SocialResult>(image + 0x82ee80);
    // Prefixes and virtual slots from the pinned PE and instruction exports in
    // m05-mutations-02 / m05-outcomes-01 / m05-grants-01. No inferred prototypes.
    struct Entry { uintptr_t rva; unsigned char bytes[14]; size_t size; uintptr_t slot; };
    const Entry entries[] = {
        {0x728020,{0x83,0xec,0x40,0x53,0x55,0x8b,0x6c,0x24,0x4c,0x56,0x57,0x33,0xff},13,0},
        {0x9ec130,{0x83,0xec,0x2c,0x56,0x8b,0xf1,0x8b,0x46,0x0c},9,0},
        {0x9ec139,{0x85,0xc0,0x0f,0x84,0xcb,0x01,0,0},8,0},
        {0x9ec30c,{0x5e,0x83,0xc4,0x2c,0xc2,0x04,0},7,0},
        {0x9f2391,{0xe8,0xca,0xa0,0xff,0xff,0x84,0xc0,0x0f,0x84,0x6e,0x01,0,0},13,0},
        {0x9f250c,{0x5f,0x5e,0x83,0xc4,0x10,0xc2,0x04,0},8,0},
        {0x9f7a26,{0xe8,0x45,0x8c,0xc2,0xff,0x85,0xc0,0x74,0x0b},9,0},
        {0x9f7a8c,{0x5f,0xb0,0x01,0x5b,0xc2,0x08,0},7,0},
        {0x815470,{0x83,0xec,0x18,0x53,0x8b,0xd9,0x56,0x8d,0xb3,0xf8,0x0b,0,0,0x33},14,0},
        {0x808210,{0x83,0xec,0x78,0x53,0x55,0x56,0x8b,0xf1,0x8b,0x46,0x58,0x8b,0x50,0x34},14,0x8c},
        {0x802d00,{0x56,0x57,0x8b,0xf1,0xe8,0xf7,0x8d,0,0,0xe8,0x02,0xa7,0xf3,0xff},14,0x90},
        {0x8223a0,{0x51,0x53,0x55,0x56,0x8b,0xf1,0x8b,0x86,0xa8,0x05,0,0,0x8b,0x50},14,0x78},
        {0x802ab0,{0x8b,0x81,0x58,0x0b,0,0,0xc1,0xe8,0x09,0xa8,0x01,0x74,0x0a,0x8b},14,0xdc},
        {0x803280,{0x8a,0x44,0x24,0x04,0x84,0xc0,0x56,0x8b,0xf1,0x0f,0x94,0xc1,0x88,0x8e},14,0xc8},
        {0x196da0,{0x8b,0x44,0x24,0x08,0x56,0x8b,0xf1,0x8b,0x4c,0x24,0x08,0x50,0x51,0x8b},14,0},
        {0x196e10,{0x56,0x8b,0xf1,0x8d,0x44,0x24,0x08,0x50,0x8d,0x8e,0,0x4d,0,0},14,0},
        {0x809b40,{0x83,0xec,0x48,0x53,0x55,0x56,0x57,0xe8,0xb4,0x2f,0xa7,0xff,0x8b,0x5c},14,0},
        {0x86d960,{0x83,0xec,0x2c,0x53,0x55,0x56,0x57,0x8b,0xf1,0xe8,0x92,0xfa,0xec,0xff},14,0},
        {0x94f290,{0x81,0xec,0xf8,0,0,0,0x55,0x56,0x8b,0xf1,0x57,0x89,0x74,0x24},14,0},
        {0x94edd0,{0x83,0xec,0x24,0x53,0x55,0x8b,0xd9,0xe8,0x24,0xe6,0xde,0xff,0x8b,0xc8},14,0},
        {0x952270,{0x83,0xec,0x54,0x8b,0x44,0x24,0x5c,0x56,0x8b,0xf1,0x8b,0x4c,0x24,0x5c},14,0},
        {0x819900,{0x53,0x8b,0x5c,0x24,0x08,0x56,0x8b,0xf1,0x83,0xfb,0x58,0x0f,0x83,0xb2},14,0},
        {0x729960,{0x8b,0x44,0x24,0x04,0x83,0xec,0x30,0x53,0x56,0x57,0x8b,0xf9,0x3d,0xfa},14,0},
        // Complete instructions on each side of the branch; no absolute operands.
        {0x80b065,{0xf3,0x0f,0x10,0x86,0x9c,0x16,0,0},8,0},
        {0x80b0c6,{0x8b,0x86,0x54,0x0b,0,0,0x85,0xc0,0x74,0x0a},10,0},
        {0x82eee7,{0x39,0x6e,0x7c,0x0f,0x84,0x06,0x02,0,0},9,0},
        {0x82f0f6,{0xe8,0x85,0x48,0x12,0,0x8b,0xc8,0xe8,0xbe,0x3c,0x12,0},12,0},
        {0x81ebdb,{0x80,0xbd,0x01,0x01,0,0,0,0x0f,0x84,0x70,0xfe,0xff,0xff},13,0},
        {0x81ea58,{0x8b,0x86,0x90,0x0e,0,0,0x50,0x8b,0xce},9,0},
        {0x81e8ba,{0xf3,0x0f,0x10,0x86,0x78,0x0b,0,0},8,0},
        {0x81e8dc,{0x8b,0x06,0x8b,0x90,0xb0,0,0,0},8,0},
        {0x81f107,{0xf3,0x0f,0x11,0x86,0x78,0x0b,0,0},8,0},
        {0x81f10f,{0xc6,0x07,0x01,0x8b,0x96,0x8c,0x0e,0,0},9,0},
        {0x80ae30,{0x56,0x8b,0xf1,0x83,0xbe,0x74,0x16,0,0,0,0x0f,0x84,0xa2,0x02},14,0x60},
        {0x82ee80,{0x83,0xec,0x24,0x53,0x55,0x56,0x57,0x8b,0xf1,0xe8,0x32,0xe7,0xf0,0xff},14,0},
        // D47FC0 is not detoured here. Its action cases are denied by the existing
        // native Strategy action hook before it calls the part-grant subhandler.
        {0x947fc0,{0x55,0x8b,0xec,0x83,0xe4,0xf8,0x83,0xec,0x74,0x53,0x8b,0x5d,0x0c,0x83},14,0}
    };
    for (const auto& entry : entries) {
        if (memcmp(reinterpret_cast<const void*>(image + entry.rva), entry.bytes, entry.size) ||
            (entry.slot && *reinterpret_cast<const uintptr_t*>(image + 0x106a080 + entry.slot) != image + entry.rva)) {
            event("replica_binding_rejected", ",\"rva\":%u", unsigned(entry.rva)); PostQuitMessage(36); return;
        }
    }
    const unsigned char hunger_prefix[] = {0x83,0xec,0x08,0x56,0x8b,0xf1,0x8b,0x86,0xc0,0,0,0,0x8b,0x50,0x58,0x8d};
    const unsigned char maximum_prefix[] = {0x83,0xec,0x18,0x56,0x57,0x8b,0xf9,0x8b,0x87,0x58,0xfa,0xff,0xff,0x8b,0x50,0x7c};
    // D2E480 is a scalar MOV to the same global D2E350 reads; relocated
    // operands are checked explicitly. Neither routine dispatches a reward.
    unsigned char setter[] = {0xf3,0x0f,0x10,0x44,0x24,0x04,0xf3,0x0f,0x11,0x05,0,0,0,0,0xc3};
    unsigned char getter[] = {0xd9,0x05,0,0,0,0,0xc3};
    const auto dna_global = static_cast<uint32_t>(image + 0x129e398);
    memcpy(setter+10, &dna_global, 4); memcpy(getter+2, &dna_global, 4);
    if (memcmp(reinterpret_cast<const void*>(save_original), prefix, sizeof(prefix)) ||
        *reinterpret_cast<const uintptr_t*>(image + 0x107e314) != image + 0x9ec130 ||
        *reinterpret_cast<const uintptr_t*>(image + 0x105f9a4) != image + 0x729960 ||
        memcmp(reinterpret_cast<const void*>(hunger_original), hunger_prefix, sizeof(hunger_prefix)) ||
        *reinterpret_cast<const uintptr_t*>(image + 0x106a080 + 0x88) != image + 0x802e10 ||
        *reinterpret_cast<const uintptr_t*>(image + 0x1069e30 + 0x58) != image + 0x805d50 ||
        memcmp(reinterpret_cast<const void*>(image + 0x805d50), maximum_prefix, sizeof(maximum_prefix)) ||
        memcmp(reinterpret_cast<const void*>(image + 0x92e480), setter, sizeof(setter)) ||
        memcmp(reinterpret_cast<const void*>(image + 0x92e350), getter, sizeof(getter)) ||
        change_hooks(hooks, _countof(hooks), true) != NO_ERROR) { PostQuitMessage(36); return; }
    attached = true;
    event("replica_policy_ready", ",\"role\":\"%s\",\"fixture\":\"living_creature_avatar_vitals\","
        "\"native_acceptance\":\"NOT_VERIFIED\",\"whole_scene_isolation\":false", client ? "replica" : "authority");
}
void dispose_native_replica() {
    if (!on_thread()) return;
    if (objects) objects->disconnect();
    sample_native_replica(true);
    if (attached) {
        const auto status = change_hooks(hooks, _countof(hooks), false);
        event("replica_policy_stop", ",\"detach_status\":%ld", status);
        if (status == NO_ERROR) attached = false;
    }
    // Keep replica denial latched until the original process exits, including
    // subsequent SDK disposal callbacks. There is no role reset to authority.
}
}
