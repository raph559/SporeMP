#include "native_scene.h"
#include "native_actors.h"
#include "native_replica.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <Windows.h>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Manager = Simulator::cGameNounManager;
using Noun = Simulator::cGameData;
using Spatial = Simulator::cSpatialObject;
using Result = NativeSceneResult;
constexpr auto capacity = NativeSceneFrame::capacity;
DWORD engine_thread = 0;
uintptr_t image = 0;
uint64_t capture_epoch = 0, next_incarnation = 0, admitted_epoch = 0;
uint64_t next_capture_sample = 0;
size_t last_inactive_shell_count = SIZE_MAX;
uint32_t controlled_owner = 0;
bool ready = false;
struct Seen { uint32_t native_id = UINT32_MAX; uintptr_t address = 0; uint64_t generation = 0; bool visited = false; };
struct Binding { uint64_t id = 0, generation = 0; uint32_t native_id = UINT32_MAX; uintptr_t address = 0; bool live = false; uint64_t owner = 0, next_sample = 0; };
std::array<Seen, capacity> observed{};
std::array<Binding, capacity> bindings{};

bool on_thread() { return engine_thread && engine_thread == GetCurrentThreadId(); }
void event(const char* name, const char* reason, uint64_t id = 0, uint32_t local = UINT32_MAX) {
    char fields[384]{};
    sprintf_s(fields, ",\"reason\":\"%s\",\"remote_entity\":%llu,\"native_id\":%u", reason, id, local);
    native_actor_worker_event(name, fields);
}
void actor_sample(const char* name, Animal* animal, const NativeSceneEntity& entity) {
    const auto& p = animal->GetPosition();
    const auto& q = animal->GetOrientation();
    const auto& v = animal->mVelocity;
    char fields[1024]{};
    sprintf_s(fields, ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"owner\":%llu,"
        "\"position\":[%.9g,%.9g,%.9g],\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"velocity\":[%.9g,%.9g,%.9g],"
        "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"is_native_avatar\":%s,\"source_tick\":%llu",
        entity.id, entity.generation, animal->mID, entity.owner, double(p.x), double(p.y), double(p.z),
        double(q.x), double(q.y), double(q.z), double(q.w), double(v.x), double(v.y), double(v.z),
        double(animal->mHealthPoints), double(animal->mEnergy), double(animal->mHunger),
        animal == Manager::Get()->GetAvatar() ? "true" : "false", entity.tick);
    native_actor_worker_event(name, fields);
}
bool available() {
    return on_thread() && ready && Simulator::GetGameModeID() == kGameCreature && Manager::Get();
}
bool inactive_shell(Animal* animal);
bool living(Noun& noun) {
    if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) return false;
    auto animal = object_cast<Animal>(&noun);
    return animal && !animal->mbDead && !animal->mbMarkedForDeletion && !inactive_shell(animal);
}
Animal* find(uint32_t id, uintptr_t expected = 0) {
    if (!available() || id == UINT32_MAX) return nullptr;
    Animal* result = nullptr;
    for (auto& noun : Manager::Get()->mNouns) if (noun.mID == id) {
        if (result || !living(noun)) return nullptr;
        auto animal = object_cast<Animal>(&noun);
        if (expected && reinterpret_cast<uintptr_t>(animal) != expected) return nullptr;
        result = animal;
    }
    return result;
}
bool spatial_binding(Animal* animal) {
    if (!animal) return false;
    auto spatial = static_cast<Spatial*>(animal);
    // SDK cbf9206, cCreatureBase.h:95–100 and cSpatialObject.h:67–77.
    // Installed dc04aee5... secondary vtable 01469EC0; checked against the
    // pinned PE with objdump. SetPosition/SetOrientation receive ECX=spatial
    // plus one pointer and RET 4; no raw primary-this casts are used.
    if (reinterpret_cast<uintptr_t>(spatial) - reinterpret_cast<uintptr_t>(animal) != 0xc0) return false;
    auto table = *reinterpret_cast<const uintptr_t* const*>(spatial);
    return reinterpret_cast<uintptr_t>(table) == image + 0x1069ec0 &&
        table[0x2c / 4] == image + 0x2e6560 && table[0x30 / 4] == image + 0x57e7c0 &&
        table[0x38 / 4] == image + 0x80dea0 && table[0x3c / 4] == image + 0x80df10;
}
bool inactive_shell(Animal* animal) {
    if (!animal || !spatial_binding(animal)) return false;
    // Native run fe2ae109... / actors-11820, sequence124+: preallocated
    // animal nouns exist before receiving a species, herd or render model.
    // Exclude only the complete observed inactive default signature. A
    // partially initialized or enabled animal still fails normal admission.
    const auto spatial = static_cast<Spatial*>(animal);
    return !animal->mpSpeciesProfile && !animal->mHerd && !animal->field_E84 &&
        !animal->mSpeciesKey.instanceID && !animal->mSpeciesKey.typeID && !animal->mSpeciesKey.groupID &&
        !animal->mArchetype && !animal->mGeneralFlags && !animal->mpAnimatedCreature &&
        !spatial->mbEnabled && !spatial->mpModel && !spatial->mpModelWorld &&
        spatial->mPosition.x == 0 && spatial->mPosition.y == 0 && spatial->mPosition.z == 0 &&
        spatial->mOrientation.x == 0 && spatial->mOrientation.y == 0 && spatial->mOrientation.z == 0 && spatial->mOrientation.w == 1 &&
        animal->mHealthPoints == 1 && animal->mEnergy == 1000 && animal->mHunger == 100;
}
const char* unsupported_reason(Animal* animal) {
    if (!animal) return "animal_cast_missing";
    if (!spatial_binding(animal)) return "spatial_binding_mismatch";
    if (!animal->mpSpeciesProfile) return "species_profile_missing";
    if (!animal->mHerd) return "herd_missing";
    if (!animal->field_E84) return "native_archetype_data_missing";
    // A retained pointer alone does not validate the herd lifetime.
    auto herd = animal->mHerd.get();
    for (auto& noun : Manager::Get()->mNouns) if (&noun == static_cast<Noun*>(herd)) {
        if (noun.mbIsDestroyed || noun.field_20) return "herd_unavailable";
        return noun.GetNounID() == Simulator::cHerd::NOUN_ID ? nullptr : "herd_type_mismatch";
    }
    return "herd_absent_from_census";
}
bool supported(Animal* animal) { return !unsupported_reason(animal); }
void capture_rejection(Animal* animal, const char* reason) {
    auto spatial = static_cast<Spatial*>(animal);
    const auto table = *reinterpret_cast<const uintptr_t* const*>(spatial);
    uint32_t herd_id = UINT32_MAX;
    bool herd_in_census = false;
    if (animal->mHerd) for (auto& noun : Manager::Get()->mNouns) {
        if (&noun == static_cast<Noun*>(animal->mHerd.get())) {
            herd_in_census = true; herd_id = noun.mID; break;
        }
    }
    char fields[1536]{};
    sprintf_s(fields, ",\"reason\":\"%s\",\"native_id\":%u,\"spatial_offset\":%u,\"spatial_vtable_rva\":%u,"
        "\"get_position_rva\":%u,\"get_orientation_rva\":%u,\"set_position_rva\":%u,\"set_orientation_rva\":%u,"
        "\"species_profile_present\":%s,\"native_archetype_data_present\":%s,\"herd_present\":%s,\"herd_in_census\":%s,\"herd_native_id\":%u,"
        "\"species\":[%u,%u,%u],\"archetype\":%u,\"spatial_enabled\":%s,\"model_present\":%s,\"model_world_present\":%s,"
        "\"animated_creature_present\":%s,\"flags\":%d,\"position\":[%.9g,%.9g,%.9g],\"orientation\":[%.9g,%.9g,%.9g,%.9g],"
        "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g",
        reason, animal->mID, unsigned(reinterpret_cast<uintptr_t>(spatial) - reinterpret_cast<uintptr_t>(animal)),
        unsigned(reinterpret_cast<uintptr_t>(table) - image), unsigned(table[0x2c / 4] - image), unsigned(table[0x30 / 4] - image),
        unsigned(table[0x38 / 4] - image), unsigned(table[0x3c / 4] - image),
        animal->mpSpeciesProfile ? "true" : "false", animal->field_E84 ? "true" : "false", animal->mHerd ? "true" : "false",
        herd_in_census ? "true" : "false", herd_id, animal->mSpeciesKey.instanceID, animal->mSpeciesKey.typeID, animal->mSpeciesKey.groupID,
        unsigned(animal->mArchetype), spatial->mbEnabled ? "true" : "false", spatial->mpModel ? "true" : "false", spatial->mpModelWorld ? "true" : "false",
        animal->mpAnimatedCreature ? "true" : "false", animal->mGeneralFlags, double(spatial->mPosition.x), double(spatial->mPosition.y),
        double(spatial->mPosition.z), double(spatial->mOrientation.x), double(spatial->mOrientation.y), double(spatial->mOrientation.z),
        double(spatial->mOrientation.w), double(animal->mHealthPoints), double(animal->mEnergy), double(animal->mHunger));
    native_actor_worker_event("scene_capture_rejected", fields);
}
bool finite(float value, float limit) { return std::isfinite(value) && std::abs(value) <= limit; }
bool valid(const NativeSceneEntity& e) {
    if (!e.id || !e.generation || e.native_id == UINT32_MAX || e.herd_native_id == UINT32_MAX || e.owner > 2) return false;
    if (!e.species_instance || !e.species_type || !e.species_group) return false;
    for (auto x : {e.x, e.y, e.z}) if (!finite(x, 1e7f)) return false;
    for (auto x : {e.vx, e.vy, e.vz}) if (!finite(x, 1e5f)) return false;
    float length = 0;
    for (auto x : {e.qx, e.qy, e.qz, e.qw}) { if (!finite(x, 1.01f)) return false; length += x * x; }
    if (length < 0.98f || length > 1.02f) return false;
    return replica::valid({e.health, e.energy, e.hunger, e.dna});
}
bool same_species(Animal* animal, const NativeSceneEntity& entity) {
    return supported(animal) && animal->mSpeciesKey.instanceID == entity.species_instance &&
        animal->mSpeciesKey.typeID == entity.species_type && animal->mSpeciesKey.groupID == entity.species_group &&
        static_cast<uint32_t>(animal->mArchetype) == entity.archetype;
}
bool same_fixture(Animal* animal, const NativeSceneEntity& entity) {
    return same_species(animal, entity) && animal->mHerd->mID == entity.herd_native_id;
}
bool mapped(uint32_t id) {
    for (const auto& b : bindings) if (b.id && b.native_id == id) return true;
    return false;
}
Binding* binding(uint64_t id, uint64_t generation) {
    for (auto& b : bindings) if (b.id == id && b.generation == generation) return &b;
    return nullptr;
}
Binding* empty_binding() {
    for (auto& b : bindings) if (!b.id) return &b;
    return nullptr;
}
Animal* template_for(const NativeSceneEntity& entity) {
    for (auto& noun : Manager::Get()->mNouns) if (living(noun)) {
        auto candidate = object_cast<Animal>(&noun);
        if (same_species(candidate, entity)) return candidate;
    }
    return nullptr;
}
bool apply_pose(Animal* animal, const NativeSceneEntity& entity) {
    if (!same_species(animal, entity) || !valid(entity) || !native_replica_allows(replica::Mutation::apply_state)) return false;
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    const auto table = *reinterpret_cast<const uintptr_t* const*>(combatant);
    if (table[0x58 / 4] != image + 0x805d50) return false;
    const auto maximum = animal->GetMaxHitPoints();
    if (!std::isfinite(maximum) || maximum <= 0 || entity.health > maximum) return false;
    // Replica values update original model/physics presentation through the
    // actual Creature override. Worker movement still uses native WalkTo.
    const Math::Vector3 position(entity.x, entity.y, entity.z);
    const Math::Quaternion orientation(entity.qx, entity.qy, entity.qz, entity.qw);
    animal->SetPosition(position);
    animal->SetOrientation(orientation);
    // The pinned SDK's SetVelocity implementation is this exact scalar copy;
    // animation choice remains original cCreatureBase::Update behavior.
    animal->mVelocity = Math::Vector3(entity.vx, entity.vy, entity.vz);
    animal->mHealthPoints = entity.health;
    animal->mEnergy = entity.energy;
    animal->mHunger = entity.hunger;
    // Only owner A's stage DNA getter is qualified. Do not invent B balances.
    if (entity.owner == 1 && controlled_owner == 1) Simulator::cCreatureGameData::SetEvolutionPoints(entity.dna);
    const auto& applied = animal->GetPosition();
    const auto& rotated = animal->GetOrientation();
    return applied.x == position.x && applied.y == position.y && applied.z == position.z &&
        rotated.x == orientation.x && rotated.y == orientation.y && rotated.z == orientation.z && rotated.w == orientation.w;
}
Animal* create(const NativeSceneEntity& entity) {
    auto source = template_for(entity);
    if (!source) return nullptr;
    const auto species = source->mpSpeciesProfile;
    auto herd = source->mHerd.get();
    // Native factory C09B40 has the proven six-word cdecl ABI and retains a
    // live native herd. A null herd is unsafe in this Creature campaign.
    auto result = Animal::Create(Math::Vector3(entity.x, entity.y, entity.z),
        species, 1, herd, false, false);
    if (!result) return nullptr;
    if (!same_species(result, entity)) {
        Manager::Get()->DestroyInstance(result);
        return nullptr;
    }
    return result;
}
struct Projection { const NativeSceneEntity* entity; Binding* local; };
bool project_callback(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    auto animal = find(args.local->native_id, args.local->address);
    if (!animal || !args.local->live || args.local->owner != args.entity->owner || !apply_pose(animal, *args.entity)) {
        event("scene_projection_rejected", "native_binding_or_pose", args.entity->id, args.local->native_id);
        return false;
    }
    const auto now = GetTickCount64();
    if (args.entity->owner && now >= args.local->next_sample) {
        args.local->next_sample = now + 1000;
        actor_sample("scene_native_replica_sample", animal, *args.entity);
    }
    return true;
}
bool spawn_callback(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    auto animal = create(*args.entity);
    if (!animal) return false;
    *args.local = {args.entity->id, args.entity->generation, animal->mID, reinterpret_cast<uintptr_t>(animal), true, args.entity->owner};
    return apply_pose(animal, *args.entity);
}
bool despawn_callback(void* raw) {
    auto& local = *static_cast<Binding*>(raw);
    auto animal = find(local.native_id, local.address);
    if (!animal || animal == Manager::Get()->GetAvatar()) return false;
    // Keep the tombstone before the original destructor can call back.
    local.live = false;
    Manager::Get()->DestroyInstance(animal);
    return true;
}
struct Baseline { const NativeSceneFrame* frame; const NativeSceneMapping* mapping; Result result = Result::native_failure; };
bool baseline_callback(void* raw) {
    auto& args = *static_cast<Baseline*>(raw);
    const auto& frame = *args.frame;
    const auto& mapping = *args.mapping;
    bindings = {};
    controlled_owner = mapping.controlled_owner;
    // Reserve both controlled local nouns first. A remote NPC native ID may
    // equal a locally generated B ID: process-local ID equality cannot steal
    // the local avatar/other-player mapping, regardless of baseline ordering.
    for (size_t i = 0; i < frame.count; ++i) {
        const auto& e = frame.entities[i];
        if (!e.owner) continue;
        const auto local_id = e.owner == controlled_owner ? mapping.avatar_native_id : mapping.other_native_id;
        auto animal = find(local_id);
        if (!animal || !same_species(animal, e) || mapped(local_id)) { args.result = Result::binding_mismatch; return false; }
        bindings[i] = {e.id, e.generation, local_id, reinterpret_cast<uintptr_t>(animal), true, e.owner};
    }
    // Resolve all NPC candidates before any native mutation. Native-ID matches
    // are admission checks only, coupled to the same qualified scene fixture.
    for (size_t i = 0; i < frame.count; ++i) {
        const auto& e = frame.entities[i];
        if (e.owner) continue;
        const auto local_id = e.native_id;
        auto animal = mapped(local_id) ? nullptr : find(local_id);
        if (animal && same_fixture(animal, e)) {
            bindings[i] = {e.id, e.generation, local_id, reinterpret_cast<uintptr_t>(animal), true, e.owner};
        } else if (!template_for(e)) { args.result = Result::missing_template; return false; }
    }
    for (size_t i = 0; i < frame.count; ++i) {
        auto& b = bindings[i];
        const auto& e = frame.entities[i];
        if (!b.live) {
            auto animal = create(e);
            if (!animal) return false;
            b = {e.id, e.generation, animal->mID, reinterpret_cast<uintptr_t>(animal), true, e.owner};
            event("scene_replica_spawn", "baseline_missing_noun", e.id, animal->mID);
        }
        Projection projection{&e, &b};
        if (!project_callback(&projection)) return false;
    }
    // Enumerate IDs first: destroying nouns while traversing the native list
    // would invalidate the iterator. The actual avatar is never removed.
    std::array<uint32_t, capacity> remove{};
    size_t count = 0;
    for (auto& noun : Manager::Get()->mNouns) if (living(noun) && !mapped(noun.mID)) {
        if (count == remove.size() || object_cast<Animal>(&noun) == Manager::Get()->GetAvatar()) return false;
        remove[count++] = noun.mID;
    }
    for (size_t i = 0; i < count; ++i) if (auto animal = find(remove[i])) {
        Manager::Get()->DestroyInstance(animal);
        event("scene_replica_despawn", "baseline_extra_noun", 0, remove[i]);
    }
    admitted_epoch = frame.epoch;
    args.result = Result::accepted;
    return true;
}
}

bool initialize_native_scene() {
    if (engine_thread && !on_thread()) return false;
    engine_thread = GetCurrentThreadId();
    image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // Static PE instruction prefixes supplement the host's full pinned hash.
    const std::array<unsigned char, 8> position{0x56, 0x57, 0x8b, 0x7c, 0x24, 0x0c, 0x57, 0x8b};
    const std::array<unsigned char, 8> orientation{0x83, 0xec, 0x10, 0x56, 0x8b, 0x74, 0x24, 0x18};
    ready = image && !std::memcmp(reinterpret_cast<const void*>(image + 0x80dea0), position.data(), position.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80df10), orientation.data(), orientation.size());
    event("scene_adapter_ready", ready ? "pinned_pose_bindings" : "binding_mismatch");
    return ready;
}
void dispose_native_scene() {
    if (!on_thread()) return;
    observed = {}; bindings = {}; capture_epoch = admitted_epoch = 0; controlled_owner = 0;
    ready = false; engine_thread = 0; image = 0;
}
NativeSceneResult capture_native_scene(NativeSceneFrame& frame) {
    frame.count = 0; frame.epoch = 0;
    if (!on_thread()) return Result::wrong_thread;
    if (!available()) return Result::unavailable;
    const auto status = native_actor_worker_status();
    if (!status.epoch) return Result::unavailable;
    if (capture_epoch != status.epoch) { observed = {}; capture_epoch = status.epoch; last_inactive_shell_count = SIZE_MAX; }
    for (auto& s : observed) s.visited = false;
    uint32_t a = UINT32_MAX, b = UINT32_MAX;
    if (!native_actor_owner_native_id(1, a) || !native_actor_owner_native_id(2, b) || a == b) return Result::unavailable;
    const auto now = GetTickCount64();
    const bool sample = now >= next_capture_sample;
    // Diagnose the complete bounded census before returning a failure. A low
    // native ID alone does not establish that an unsupported noun is a pool
    // placeholder, so do not silently omit it from the shared scene.
    size_t unsupported_count = 0, inactive_shell_count = 0;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) continue;
        auto animal = object_cast<Animal>(&noun);
        if (!animal || animal->mbDead || animal->mbMarkedForDeletion) continue;
        if (inactive_shell(animal)) { ++inactive_shell_count; continue; }
        if (const auto reason = unsupported_reason(animal)) {
            if (unsupported_count < 32) capture_rejection(animal, reason);
            ++unsupported_count;
        }
    }
    if (inactive_shell_count != last_inactive_shell_count) {
        last_inactive_shell_count = inactive_shell_count;
        char fields[160]{};
        sprintf_s(fields, ",\"count\":%u,\"reason\":\"observed_inactive_default_signature\"", static_cast<unsigned>(inactive_shell_count));
        native_actor_worker_event("scene_inactive_shell_census", fields);
    }
    if (unsupported_count) {
        event("scene_capture_rejection_count", "unsupported_living_entities", 0, static_cast<uint32_t>(unsupported_count));
        return Result::unsupported_entity;
    }
    for (auto& noun : Manager::Get()->mNouns) if (living(noun)) {
        if (frame.count == capacity) return Result::full;
        auto animal = object_cast<Animal>(&noun);
        Seen* seen = nullptr;
        for (auto& s : observed) if (s.native_id == animal->mID && s.address == reinterpret_cast<uintptr_t>(animal)) { seen = &s; break; }
        if (seen && seen->visited) return Result::binding_mismatch;
        if (!seen) {
            for (auto& s : observed) if (!s.address) { seen = &s; break; }
            if (!seen) return Result::full;
            *seen = {animal->mID, reinterpret_cast<uintptr_t>(animal), ++next_incarnation, false};
        }
        seen->visited = true;
        auto& e = frame.entities[frame.count++];
        e = {};
        if (status.epoch > UINT32_MAX) return Result::unsupported_entity;
        e.id = (status.epoch << 32) | animal->mID; e.generation = seen->generation;
        e.tick = GetTickCount64();
        e.native_id = animal->mID; e.herd_native_id = animal->mHerd->mID;
        e.species_instance = animal->mSpeciesKey.instanceID;
        e.species_type = animal->mSpeciesKey.typeID;
        e.species_group = animal->mSpeciesKey.groupID;
        e.archetype = static_cast<uint32_t>(animal->mArchetype);
        e.owner = animal->mID == a ? 1u : animal->mID == b ? 2u : 0u;
        const auto& p = animal->GetPosition(); const auto& q = animal->GetOrientation(); const auto& v = animal->mVelocity;
        e.x = p.x; e.y = p.y; e.z = p.z; e.qx = q.x; e.qy = q.y; e.qz = q.z; e.qw = q.w;
        e.vx = v.x; e.vy = v.y; e.vz = v.z;
        e.health = animal->mHealthPoints; e.energy = animal->mEnergy; e.hunger = animal->mHunger;
        if (animal == Manager::Get()->GetAvatar()) e.dna = Simulator::cCreatureGameData::GetEvolutionPoints();
        if (!valid(e)) { event("scene_capture_rejected", "unsupported_native_values", e.id, e.native_id); return Result::unsupported_entity; }
        if (sample && e.owner) actor_sample("scene_native_authority_sample", animal, e);
    }
    for (auto& s : observed) if (!s.visited) s = {};
    frame.epoch = status.epoch;
    if (sample) next_capture_sample = now + 1000;
    return Result::accepted;
}
NativeSceneResult apply_native_scene_baseline(const NativeSceneFrame& frame, const NativeSceneMapping& mapping) {
    if (!on_thread()) return Result::wrong_thread;
    if (!available() || !native_replica_is_client()) return Result::unavailable;
    if (!frame.epoch || !frame.count || frame.count > capacity || mapping.controlled_owner < 1 || mapping.controlled_owner > 2 ||
        mapping.avatar_native_id == mapping.other_native_id) return Result::invalid;
    auto avatar = find(mapping.avatar_native_id), other = find(mapping.other_native_id);
    if (!avatar || !other || avatar != Manager::Get()->GetAvatar()) return Result::binding_mismatch;
    size_t owners[3]{};
    for (size_t i = 0; i < frame.count; ++i) {
        if (!valid(frame.entities[i])) return Result::invalid;
        ++owners[frame.entities[i].owner];
        for (size_t j = 0; j < i; ++j) if (frame.entities[j].id == frame.entities[i].id || frame.entities[j].native_id == frame.entities[i].native_id) return Result::invalid;
    }
    if (owners[1] != 1 || owners[2] != 1) return Result::invalid;
    Baseline operation{&frame, &mapping};
    if (!native_replica_project(baseline_callback, &operation, true)) {
        reset_native_scene_baseline();
        event("scene_baseline_rejected", native_scene_result_name(operation.result));
        return operation.result;
    }
    event("scene_baseline_applied", "living_creature_fixture", 0, static_cast<uint32_t>(frame.count));
    return Result::accepted;
}
NativeSceneResult spawn_native_scene_entity(const NativeSceneEntity& entity) {
    if (!on_thread()) return Result::wrong_thread;
    if (!available() || !admitted_epoch || !native_replica_is_client()) return Result::unavailable;
    if (!valid(entity) || entity.owner) return Result::invalid;
    Binding* slot = nullptr;
    for (auto& b : bindings) if (b.id == entity.id) {
        if (b.live || entity.generation <= b.generation) return Result::stale;
        slot = &b; break;
    }
    if (!slot) slot = empty_binding();
    if (!slot) return Result::full;
    if (!template_for(entity)) return Result::missing_template;
    Projection args{&entity, slot};
    if (!native_replica_project(spawn_callback, &args, true)) { reset_native_scene_baseline(); return Result::native_failure; }
    event("scene_replica_spawn", "authoritative_spawn", entity.id, slot->native_id);
    return Result::accepted;
}
NativeSceneResult despawn_native_scene_entity(uint64_t id, uint64_t generation) {
    if (!on_thread()) return Result::wrong_thread;
    if (!available() || !admitted_epoch || !native_replica_is_client()) return Result::unavailable;
    auto slot = binding(id, generation);
    if (!slot || !slot->live) return Result::stale;
    const auto local = slot->native_id;
    if (!native_replica_project(despawn_callback, slot, true)) { reset_native_scene_baseline(); return Result::native_failure; }
    event("scene_replica_despawn", "authoritative_despawn", id, local);
    return Result::accepted;
}
NativeSceneResult project_native_scene_entity(const NativeSceneEntity& entity) {
    if (!on_thread()) return Result::wrong_thread;
    if (!available() || !admitted_epoch || !native_replica_is_client()) return Result::unavailable;
    if (!valid(entity)) return Result::invalid;
    auto slot = binding(entity.id, entity.generation);
    if (!slot || !slot->live) return Result::stale;
    Projection args{&entity, slot};
    if (!native_replica_project(project_callback, &args, false)) { reset_native_scene_baseline(); return Result::native_failure; }
    return Result::accepted;
}
void reset_native_scene_baseline() {
    if (!on_thread()) return;
    bindings = {}; admitted_epoch = 0; controlled_owner = 0;
}
void invalidate_native_scene_entity(uint32_t native_id) {
    if (!on_thread()) return;
    for (auto& s : observed) if (s.native_id == native_id) s = {};
    for (auto& b : bindings) if (b.native_id == native_id) b.live = false;
}
const char* native_scene_result_name(NativeSceneResult result) noexcept {
    constexpr const char* names[]{"accepted", "unavailable", "wrong_thread", "invalid", "full", "stale", "unsupported_entity", "binding_mismatch", "missing_template", "native_failure"};
    auto index = static_cast<size_t>(result);
    return index < std::size(names) ? names[index] : "invalid";
}
}
