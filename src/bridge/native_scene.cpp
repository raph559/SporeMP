#include "native_scene.h"
#include "native_actors.h"
#include "native_replica.h"
#include "native_life_presentation.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/Editors/SpeciesManager.h>
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
using Herd = Simulator::cHerd;
using Result = NativeSceneResult;
constexpr auto capacity = NativeSceneFrame::capacity;
DWORD engine_thread = 0;
uintptr_t image = 0;
uint64_t capture_epoch = 0, next_incarnation = 0, admitted_epoch = 0;
uint64_t next_capture_sample = 0;
size_t last_inactive_shell_count = SIZE_MAX, last_pooled_shell_count = SIZE_MAX;
uint32_t controlled_owner = 0;
bool ready = false;
bool herd_diagnostic_bindings = false;
bool herd_profile_bindings = false;
bool pool_lifecycle_bindings = false;
struct Seen {
    uint32_t native_id = UINT32_MAX;
    uintptr_t address = 0;
    uint64_t generation = 0;
    uint32_t pool_cycle = 0;
    bool visited = false;
};
struct Binding {
    uint64_t id = 0, generation = 0;
    uint32_t native_id = UINT32_MAX;
    uintptr_t address = 0;
    bool live = false;
    uint64_t owner = 0, next_sample = 0;
    bool presentation_seen = false, presentation_dead = false;
    uintptr_t presentation_model = 0;
    bool pickup_seen = false;
    uint32_t pickup_owner = 0;
};
std::array<Seen, capacity> observed{};
std::array<Binding, capacity> bindings{};

bool on_thread() { return engine_thread && engine_thread == GetCurrentThreadId(); }
void event(const char* name, const char* reason, uint64_t id = 0, uint32_t local = UINT32_MAX) {
    char fields[384]{};
    sprintf_s(fields, ",\"reason\":\"%s\",\"remote_entity\":%llu,\"native_id\":%u", reason, id, local);
    native_actor_worker_event(name, fields);
}
void health_context(Animal* animal, char (&fields)[896]);
void actor_sample(const char* name, Animal* animal, const NativeSceneEntity& entity) {
    const auto& p = animal->GetPosition();
    const auto& q = animal->GetOrientation();
    const auto& v = animal->mVelocity;
    float native_dna = 0;
    bool dna_observed = false;
    if (entity.owner) {
        if (!native_replica_is_client()) {
            bool current_native = false;
            dna_observed = native_actor_owner_dna(static_cast<uint32_t>(entity.owner), native_dna, &current_native) && current_native;
        }
        else if (entity.owner == controlled_owner && animal == Manager::Get()->GetAvatar()) {
            native_dna = Simulator::cCreatureGameData::GetEvolutionPoints();
            dna_observed = std::isfinite(native_dna) && native_dna >= 0;
        }
    }
    char dna_value[48] = "null";
    if (dna_observed) sprintf_s(dna_value, "%.9g", double(native_dna));
    char health[896]{};
    health_context(animal, health);
    uint32_t native_animation = 0;
    int native_animation_index = -1;
    const bool animation_observed = native_replica_is_client() &&
        native_life_read_animation(animal, native_animation, native_animation_index);
    char fields[2048]{};
    sprintf_s(fields, ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"owner\":%llu,"
        "\"position\":[%.9g,%.9g,%.9g],\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"velocity\":[%.9g,%.9g,%.9g],"
        "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"source_dna\":%.9g,\"native_dna\":%s,\"dna_native_observed\":%s,"
        "\"dead\":%s,\"is_native_avatar\":%s,\"source_tick\":%llu,"
        "\"source_age\":%u,\"source_alpha\":%u,\"source_combatant_state\":%u,\"source_scale\":%.9g,"
        "\"source_fed_on\":%u,\"source_food\":%.9g,\"source_pickup_owner\":%u,\"native_fed_on\":%u,\"native_food\":%.9g,\"native_pool_cycle\":%u,"
        "\"native_animation_observed\":%s,\"native_animation\":%u,\"native_animation_index\":%d%s",
        entity.id, entity.generation, animal->mID, entity.owner, double(p.x), double(p.y), double(p.z),
        double(q.x), double(q.y), double(q.z), double(q.w), double(v.x), double(v.y), double(v.z),
        double(animal->mHealthPoints), double(animal->mEnergy), double(animal->mHunger), double(entity.dna), dna_value, dna_observed ? "true" : "false",
        animal->mbDead ? "true" : "false",
        animal == Manager::Get()->GetAvatar() ? "true" : "false", entity.tick,
        entity.age, entity.alpha, entity.combatant_state, double(entity.scale),
        entity.fed_on, double(entity.food), entity.pickup_owner,
        unsigned(*reinterpret_cast<const unsigned char*>(&animal->mbHasBeenEaten)), double(animal->mFoodValue), static_cast<uint32_t>(animal->field_E54),
        animation_observed ? "true" : "false", native_animation, native_animation_index, health);
    native_actor_worker_event(name, fields);
}
bool available() {
    return on_thread() && ready && Simulator::GetGameModeID() == kGameCreature && Manager::Get();
}
bool inactive_shell(Animal* animal);
bool pooled_shell(Animal* animal);
bool present(Noun& noun) {
    if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) return false;
    auto animal = object_cast<Animal>(&noun);
    return animal && !animal->mbMarkedForDeletion && !inactive_shell(animal) && !pooled_shell(animal);
}
bool living(Noun& noun) { return present(noun) && !object_cast<Animal>(&noun)->mbDead; }
Animal* find(uint32_t id, uintptr_t expected = 0) {
    if (!available() || id == UINT32_MAX) return nullptr;
    Animal* result = nullptr;
    for (auto& noun : Manager::Get()->mNouns) if (noun.mID == id) {
        if (result || !present(noun)) return nullptr;
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
        table[0x34 / 4] == image + 0x837230 &&
        table[0x38 / 4] == image + 0x80dea0 && table[0x3c / 4] == image + 0x80df10 &&
        table[0x40 / 4] == image + 0x80e750 && table[0x64 / 4] == image + 0x8892f0;
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
bool scalar_context_binding(Animal* animal) {
    if (!animal) return false;
    const auto address = reinterpret_cast<uintptr_t>(animal);
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    const auto primary = *reinterpret_cast<const uintptr_t* const*>(animal);
    const auto table = *reinterpret_cast<const uintptr_t* const*>(combatant);
    // SDK cbf9206 layout and original C0B8D0/C05E6E/008E8230 reads,
    // C223C6/BFD1A1 writes; their instruction bytes are checked at startup.
    return reinterpret_cast<uintptr_t>(&animal->mAge) - address == 0xb34 &&
        reinterpret_cast<uintptr_t>(&animal->mGeneralFlags) - address == 0xb58 &&
        reinterpret_cast<uintptr_t>(&animal->mbDead) - address == 0xb5e &&
        reinterpret_cast<uintptr_t>(&animal->mbHasBeenEaten) - address == 0xb5f &&
        reinterpret_cast<uintptr_t>(&animal->mFoodValue) - address == 0xb84 &&
        reinterpret_cast<uintptr_t>(&animal->field_E54) - address == 0xe54 &&
        reinterpret_cast<uintptr_t>(&animal->mScale) - address == 0x124 &&
        reinterpret_cast<uintptr_t>(combatant) - address == 0x5a8 &&
        reinterpret_cast<uintptr_t>(&combatant->field_34) - address == 0x5dc &&
        reinterpret_cast<uintptr_t>(primary) == image + 0x106a080 && primary[0x7c / 4] == image + 0x804170 &&
        reinterpret_cast<uintptr_t>(table) == image + 0x1069e30 && table[0x58 / 4] == image + 0x805d50;
}
bool pooled_shell(Animal* animal) {
    if (!pool_lifecycle_bindings || !animal || !spatial_binding(animal) || !scalar_context_binding(animal)) return false;
    const auto spatial = static_cast<Spatial*>(animal);
    const auto address = reinterpret_cast<uintptr_t>(animal);
    if (reinterpret_cast<uintptr_t>(&animal->mHerd) - address != 0x1674 ||
        reinterpret_cast<uintptr_t>(&spatial->mbFixed) - address != 0x131 ||
        reinterpret_cast<uintptr_t>(&spatial->mbEnabled) - address != 0x135 ||
        reinterpret_cast<uintptr_t>(&spatial->mpModel) - address != 0x15c ||
        reinterpret_cast<uintptr_t>(&spatial->mpModelWorld) - address != 0x160) return false;
    // Pinned ACCEC0 returns a retained noun to its original pool: it clears
    // the active slot, fixes/disables the spatial object, resets its transform
    // and render descriptor, increments E54, then C04700(0) releases its herd.
    // Native12 NPC75 retained its profile/animation/vitals in exactly this
    // unrendered state. Ordinary death alone does not satisfy this signature.
    return animal != Manager::Get()->GetAvatar() && !(animal->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar) &&
        animal->field_E54 != 0 && !animal->mHerd && animal->mpSpeciesProfile && animal->field_E84 &&
        animal->mSpeciesKey.instanceID && animal->mSpeciesKey.typeID && animal->mSpeciesKey.groupID &&
        animal->mArchetype && animal->mpAnimatedCreature && spatial->mbFixed && !spatial->mbEnabled &&
        !spatial->mpModel && !spatial->mpModelWorld &&
        spatial->mPosition.x == 0 && spatial->mPosition.y == 0 && spatial->mPosition.z == 0;
}
void retire_incarnation(Seen& seen, const char* reason, uint32_t pool_cycle) {
    char fields[320]{};
    sprintf_s(fields, ",\"reason\":\"%s\",\"native_id\":%u,\"remote_entity\":%llu,\"entity_generation\":%llu,"
        "\"previous_pool_cycle\":%u,\"native_pool_cycle\":%u", reason, seen.native_id,
        (capture_epoch << 32) | seen.native_id, seen.generation, seen.pool_cycle, pool_cycle);
    native_actor_worker_event("scene_native_incarnation_retired", fields);
    seen = {};
}
bool readable_span(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto begin = reinterpret_cast<uintptr_t>(address), region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin >= region && size <= info.RegionSize && begin - region <= info.RegionSize - size;
}
bool scale_presentation_available(Animal* animal, float scale) {
    // C0E750 -> C0DB10 reads these bounded portions of the existing native
    // objects. Their references remain owned by this live noun on its engine
    // thread. No virtual calls are made through the animated pointer here.
    if (!readable_span(animal->mpAnimatedCreature.get(), 0x40) ||
        !readable_span(animal->mpSpeciesProfile, 0x560)) return false;
    const auto& dimensions = animal->mpSpeciesProfile->mBoundingBox.lower;
    const float x = dimensions.x * scale, y = dimensions.y * scale, z = dimensions.z * scale;
    return std::isfinite(dimensions.x) && std::isfinite(dimensions.y) && std::isfinite(dimensions.z) &&
        std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(x * x + y * y + z * z);
}
void health_context(Animal* animal, char (&fields)[896]) {
    strcpy_s(fields, ",\"health_context\":{\"observed\":false}");
    if (!available() || !animal || !supported(animal)) return;
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    const auto primary = *reinterpret_cast<const uintptr_t* const*>(animal);
    const auto table = *reinterpret_cast<const uintptr_t* const*>(combatant);
    // STATIC pinned dc04aee5: primary 146A080+7C -> C04170, ECX=animal,
    // no stack arguments, x87 float return. Effective max C05D50 receives
    // ECX=animal+5A8 and calls that primary virtual before applying age/flags.
    // These diagnostics never replace the independently enforced HP limit.
    const std::array<unsigned char, 9> base_prefix{0x83, 0xec, 0x08, 0x0f, 0x57, 0xc0, 0x56, 0x8b, 0xf1};
    const bool base_binding = reinterpret_cast<uintptr_t>(primary) == image + 0x106a080 &&
        primary[0x7c / 4] == image + 0x804170 &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x804170), base_prefix.data(), base_prefix.size());
    const bool maximum_binding = base_binding &&
        reinterpret_cast<uintptr_t>(combatant) - reinterpret_cast<uintptr_t>(animal) == 0x5a8 &&
        reinterpret_cast<uintptr_t>(table) == image + 0x1069e30 && table[0x58 / 4] == image + 0x805d50;
    char maximum[48] = "null", base[48] = "null", raw[48] = "null", species[48] = "null", herd[48] = "null", scale[48] = "null";
    const auto number = [](char (&out)[48], float value) {
        if (std::isfinite(value)) sprintf_s(out, "%.9g", double(value));
    };
    if (maximum_binding) number(maximum, animal->GetMaxHitPoints());
    if (base_binding) number(base, animal->GetBaseMaxHitPoints());
    number(raw, animal->mMaxHealthPoints);
    number(species, animal->mpSpeciesProfile->mAdditionalHealth);
    // supported() confirmed this exact live herd in the noun census. Never
    // dereference the opaque archetype field_E84 just to enrich a diagnostic.
    number(herd, animal->mHerd->mHitpointOverride);
    number(scale, animal->GetScale());
    const auto stage = Simulator::cCreatureGameData::Get();
    char brain[24] = "null";
    if (stage) sprintf_s(brain, "%d", stage->mCurrentBrainLevel);
    const auto avatar = Manager::Get()->GetAvatar();
    bool avatar_present = false;
    if (avatar) for (auto& noun : Manager::Get()->mNouns) {
        if (&noun == static_cast<Noun*>(avatar) && present(noun)) { avatar_present = true; break; }
    }
    const char* same_avatar_species = avatar_present ?
        (animal->mpSpeciesProfile == avatar->mpSpeciesProfile ? "true" : "false") : "null";
    sprintf_s(fields, ",\"health_context\":{\"observed\":true,\"maximum_binding\":%s,\"base_binding\":%s,"
        "\"native_max\":%s,\"native_base_max\":%s,\"raw_max\":%s,\"age\":%d,\"flags\":%u,\"combatant_state\":%d,"
        "\"stage_brain\":%s,\"species_health\":%s,\"herd_health_override\":%s,\"herd_native_id\":%u,"
        "\"same_avatar_species\":%s,\"species\":[%u,%u,%u],\"native_archetype_present\":%s,\"native_scale\":%s}",
        maximum_binding ? "true" : "false", base_binding ? "true" : "false", maximum, base, raw,
        animal->mAge, unsigned(animal->mGeneralFlags), combatant->field_34, brain, species, herd, animal->mHerd->mID,
        same_avatar_species, animal->mSpeciesKey.instanceID, animal->mSpeciesKey.typeID, animal->mSpeciesKey.groupID,
        animal->field_E84 ? "true" : "false", scale);
}
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
        "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"spatial_fixed\":%s,\"native_pool_cycle\":%u",
        reason, animal->mID, unsigned(reinterpret_cast<uintptr_t>(spatial) - reinterpret_cast<uintptr_t>(animal)),
        unsigned(reinterpret_cast<uintptr_t>(table) - image), unsigned(table[0x2c / 4] - image), unsigned(table[0x30 / 4] - image),
        unsigned(table[0x38 / 4] - image), unsigned(table[0x3c / 4] - image),
        animal->mpSpeciesProfile ? "true" : "false", animal->field_E84 ? "true" : "false", animal->mHerd ? "true" : "false",
        herd_in_census ? "true" : "false", herd_id, animal->mSpeciesKey.instanceID, animal->mSpeciesKey.typeID, animal->mSpeciesKey.groupID,
        unsigned(animal->mArchetype), spatial->mbEnabled ? "true" : "false", spatial->mpModel ? "true" : "false", spatial->mpModelWorld ? "true" : "false",
        animal->mpAnimatedCreature ? "true" : "false", animal->mGeneralFlags, double(spatial->mPosition.x), double(spatial->mPosition.y),
        double(spatial->mPosition.z), double(spatial->mOrientation.x), double(spatial->mOrientation.y), double(spatial->mOrientation.z),
        double(spatial->mOrientation.w), double(animal->mHealthPoints), double(animal->mEnergy), double(animal->mHunger),
        spatial->mbFixed ? "true" : "false", static_cast<uint32_t>(animal->field_E54));
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
    if (!network::valid_entity(e)) return false;
    // Native11 observed lethal damage reach zero health before mbDead became
    // true. Preserve that intermediate native state; valid_entity still checks
    // nonnegative health and requires exactly zero health for an explicit corpse.
    return finite(e.health, 1e6f) &&
        finite(e.energy, 1e6f) && finite(e.hunger, 1e6f);
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
bool herd_diagnostic_binding(Noun& noun) {
    if (!herd_diagnostic_bindings) return false;
    const auto table = *reinterpret_cast<const uintptr_t* const*>(&noun);
    // Pinned dc04aee5 PE: primary cHerd vtable 01471B90, Cast C69F50,
    // noun ID C6A110 and cast ID C6C0A0. Do not use an unchecked herd cast.
    return reinterpret_cast<uintptr_t>(table) == image + 0x1071b90 && readable_span(&noun, sizeof(Herd)) &&
        table[0] == image + 0x86ab00 && table[1] == image + 0x3b87b0 &&
        table[0x0c / 4] == image + 0x869f50 && table[0x20 / 4] == image + 0x86a110 &&
        table[0x38 / 4] == image + 0x86c0a0;
}
bool herd_diagnostic_layout(Herd* herd) {
    const auto address = reinterpret_cast<uintptr_t>(herd);
    // SDK cbf9206 cHerd.h; C09C26 reads generation/archetype, and C6C9EC
    // copies the raw profile's +504 key into the herd's +98 species key.
    return herd && reinterpret_cast<uintptr_t>(&herd->mArchetype) - address == 0x88 &&
        reinterpret_cast<uintptr_t>(&herd->mArchetypeGroup) - address == 0x8c &&
        reinterpret_cast<uintptr_t>(&herd->mOwnerSpeciesKey) - address == 0x98 &&
        reinterpret_cast<uintptr_t>(&herd->mpSpeciesProfile) - address == 0xa4 &&
        reinterpret_cast<uintptr_t>(&herd->mGeneration) - address == 0xf0 &&
        reinterpret_cast<uintptr_t>(&herd->mbEnabled) - address == 0x218;
}
bool matches_species_key(const ResourceKey& key, const NativeSceneEntity& entity) {
    return key.instanceID == entity.species_instance && key.typeID == entity.species_type && key.groupID == entity.species_group;
}
bool diagnostic_profile_readable(const void* profile, size_t size = 0x510) {
    // Checked fixed spans only, never unchecked SDK container iteration.
    // Readability is not a claim of profile ownership or retained lifetime.
    MEMORY_BASIC_INFORMATION info{};
    if (!readable_span(profile, size) || !VirtualQuery(profile, &info, sizeof(info))) return false;
    const auto protection = info.Protect & 0xff;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
uint32_t native_word(uintptr_t address) {
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
struct MapRead {
    uintptr_t node = 0;
    uint32_t buckets = 0, nodes = 0, matches = 0;
    bool complete = false;
    const char* reason = "map_unobserved";
};
MapRead inspect_native_map(uintptr_t map, const uint32_t* key, size_t words, size_t next_offset) {
    // 4E0FF0 and 4E1550 are the pinned native find implementations. The SDK
    // incorrectly declares the species map's pointer value as a full profile.
    // Read only its requested bucket, including every node to reject duplicates.
    constexpr uint32_t bucket_limit = 16384, chain_limit = 256;
    MapRead result;
    if (!diagnostic_profile_readable(reinterpret_cast<void*>(map), 0x20)) { result.reason = "map_header_unreadable"; return result; }
    const uintptr_t buckets = native_word(map + 4);
    result.buckets = native_word(map + 8);
    if (!result.buckets || result.buckets > bucket_limit ||
        !diagnostic_profile_readable(reinterpret_cast<void*>(buckets), (size_t(result.buckets) + 1) * 4)) {
        result.reason = "map_bucket_span"; return result;
    }
    const uint32_t hash = words == 3 ? key[0] ^ key[2] : key[0];
    uintptr_t node = native_word(buckets + (hash % result.buckets) * 4);
    std::array<uintptr_t, chain_limit> visited{};
    while (node) {
        if (result.nodes == chain_limit) { result.reason = "map_chain_limit"; return result; }
        for (uint32_t i = 0; i < result.nodes; ++i) if (visited[i] == node) {
            result.reason = "map_chain_cycle"; return result;
        }
        if (!diagnostic_profile_readable(reinterpret_cast<void*>(node), next_offset + 4)) {
            result.reason = "map_node_unreadable"; return result;
        }
        visited[result.nodes++] = node;
        bool matches = true;
        for (size_t i = 0; i < words; ++i) if (native_word(node + i * 4) != key[i]) matches = false;
        if (matches) { ++result.matches; result.node = node; }
        node = native_word(node + next_offset);
    }
    result.complete = true;
    result.reason = result.matches == 1 ? "unique_key" : result.matches ? "duplicate_key" : "key_absent";
    return result;
}
struct HerdProfileRead {
    Simulator::cSpeciesProfile* profile = nullptr;
    uintptr_t archetype = 0;
    bool qualified = false;
};
HerdProfileRead inspect_herd_profile(const NativeSceneEntity& entity, Herd* herd, const char* phase,
    Animal* source = nullptr, bool diagnostics = true) {
    HerdProfileRead result;
    const bool live = herd && herd_diagnostic_binding(*herd) && herd_diagnostic_layout(herd) &&
        !herd->mbIsDestroyed && !herd->field_20;
    // Get() is only the checked singleton load, never GetSpeciesProfile (which
    // loads/allocates), nor 4DF740 (which initializes a missing creatureSeq).
    auto manager = herd_profile_bindings && on_thread() ? Editors::cSpeciesManager::Get() : nullptr;
    const auto address = reinterpret_cast<uintptr_t>(manager);
    const bool manager_binding = manager && diagnostic_profile_readable(manager, 0xe4) &&
        native_word(address) == image + 0xff08c8 && native_word(image + 0xff08c8) == image + 0xdb410 &&
        native_word(image + 0xff08cc) == image + 0xdcf40;
    const uint32_t key[]{entity.species_instance, entity.species_type, entity.species_group};
    MapRead species;
    if (manager_binding && live) species = inspect_native_map(address + 4, key, 3, 0x10);
    const bool unique = species.complete && species.matches == 1;
    auto profile = unique ? reinterpret_cast<Simulator::cSpeciesProfile*>(native_word(species.node + 0xc)) : nullptr;
    const bool same_profile = live && profile && profile == herd->mpSpeciesProfile;
    const bool full_profile = same_profile && diagnostic_profile_readable(profile, sizeof(*profile)) &&
        reinterpret_cast<uintptr_t>(&profile->mCreatureKey) - reinterpret_cast<uintptr_t>(profile) == 0x504 &&
        reinterpret_cast<uintptr_t>(&profile->creatureSeq) - reinterpret_cast<uintptr_t>(profile) == 0x53c;
    const bool profile_key = full_profile && matches_species_key(profile->mCreatureKey, entity);
    const bool sequence = full_profile && profile->creatureSeq != 0;
    const bool owned = manager_binding && unique && same_profile;
    const bool owner_key = live && matches_species_key(herd->mOwnerSpeciesKey, entity);
    const char* profile_reason = !herd_profile_bindings ? "binding_mismatch" : !live ? "herd_not_live" :
        !manager_binding ? "manager_binding" : !unique ? species.reason : !same_profile ? "profile_owner_mismatch" :
        !full_profile ? "profile_unreadable" : !profile_key || !owner_key ? "species_key_mismatch" :
        !sequence ? "creature_sequence_missing" : "current_manager_owned_profile";
    if (diagnostics) {
        char fields[1280]{};
        sprintf_s(fields, ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_tick\":%llu,"
            "\"candidate_native_id\":%u,\"bindings_qualified\":%s,\"manager_present\":%s,\"manager_vtable_match\":%s,"
            "\"species_bucket_count\":%u,\"species_chain_nodes\":%u,\"species_chain_complete\":%s,\"species_key_matches\":%u,"
            "\"map_profile_present\":%s,\"map_profile_matches_herd\":%s,\"profile_full_readable\":%s,\"profile_key_matches\":%s,"
            "\"creature_sequence_present\":%s,\"profile_owned_now\":%s,\"reason\":\"%s\"",
            phase, entity.id, entity.generation, entity.tick, herd ? herd->mID : UINT32_MAX,
            herd_profile_bindings ? "true" : "false", manager ? "true" : "false", manager_binding ? "true" : "false",
            species.buckets, species.nodes, species.complete ? "true" : "false", species.matches,
            profile ? "true" : "false", same_profile ? "true" : "false", full_profile ? "true" : "false", profile_key ? "true" : "false",
            sequence ? "true" : "false", owned ? "true" : "false", profile_reason);
        native_actor_worker_event("scene_herd_profile_owner", fields);
    }
    MapRead base, variant;
    uintptr_t archetype = 0;
    uint32_t generation_count = 0, generation_index = UINT32_MAX, variant_key = 0;
    bool generation_observed = false, variant_attempted = false, resolution = false;
    const char* archetype_reason = live && herd->mGeneration < 0 ? "negative_herd_generation" : "archetype_unobserved";
    // The observed fixture is generation zero. Reject a negative SDK value;
    // do not qualify an unknown lifecycle state via unsigned index fallback.
    if (manager_binding && live && herd->mGeneration >= 0) {
        const uint32_t raw_key = herd->mArchetype;
        base = inspect_native_map(address + 0xb0, &raw_key, 1, 0x444);
        if (base.complete && base.matches == 1) {
            archetype = base.node + 4;
            resolution = true;
            archetype_reason = "base_entry";
            if (herd->mGeneration != 0) {
                const uintptr_t begin = native_word(archetype + 0x3c4), end = native_word(archetype + 0x3c8);
                if (end < begin || (end - begin) % 4 || (end - begin) / 4 > 1024 ||
                    (end != begin && !diagnostic_profile_readable(reinterpret_cast<void*>(begin), end - begin))) {
                    resolution = false; archetype_reason = "generation_vector_span";
                } else {
                    generation_observed = true;
                    generation_count = static_cast<uint32_t>((end - begin) / 4);
                    generation_index = static_cast<uint32_t>(herd->mGeneration) - 1;
                    if (generation_index < generation_count) {
                        variant_attempted = true;
                        variant_key = native_word(begin + generation_index * 4);
                        variant = inspect_native_map(address + 0xb0, &variant_key, 1, 0x444);
                        if (!variant.complete || variant.matches > 1) { resolution = false; archetype_reason = variant.reason; }
                        else if (variant.matches == 1) { archetype = variant.node + 4; archetype_reason = "generation_entry"; }
                    }
                }
            }
        } else archetype_reason = base.reason;
    }
    const uint32_t resolved = resolution ? native_word(archetype + 0x10) : 0;
    const bool matches_wire = resolution && resolved == entity.archetype;
    if (diagnostics) {
        char fields[1280]{};
        sprintf_s(fields, ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_tick\":%llu,"
            "\"candidate_native_id\":%u,\"raw_herd_archetype\":%u,\"herd_generation\":%d,"
            "\"base_chain_nodes\":%u,\"base_chain_complete\":%s,\"base_key_matches\":%u,"
            "\"generation_vector_observed\":%s,\"generation_count\":%u,\"generation_index\":%u,\"variant_attempted\":%s,\"variant_key\":%u,"
            "\"variant_chain_nodes\":%u,\"variant_chain_complete\":%s,\"variant_key_matches\":%u,"
            "\"resolution_observed\":%s,\"resolved_animal_archetype\":%u,\"matches_wire_archetype\":%s,"
            "\"authority_entry_matches\":%s,\"reason\":\"%s\"",
            phase, entity.id, entity.generation, entity.tick, herd ? herd->mID : UINT32_MAX,
            live ? herd->mArchetype : 0, live ? herd->mGeneration : 0, base.nodes, base.complete ? "true" : "false", base.matches,
            generation_observed ? "true" : "false", generation_count, generation_index, variant_attempted ? "true" : "false", variant_key,
            variant.nodes, variant.complete ? "true" : "false", variant.matches, resolution ? "true" : "false", resolved,
            matches_wire ? "true" : "false", source && resolution ?
                (reinterpret_cast<uintptr_t>(source->field_E84) == archetype ? "true" : "false") : "null", archetype_reason);
        native_actor_worker_event("scene_herd_archetype", fields);
    }
    result.qualified = owned && full_profile && profile_key && sequence && owner_key && matches_wire;
    if (result.qualified) { result.profile = profile; result.archetype = archetype; }
    return result;
}
Herd* existing_herd_for(const NativeSceneEntity& entity, const char* phase, bool diagnostics) {
    constexpr size_t noun_limit = 16384;
    if (!available() || !native_replica_is_client() || !herd_profile_bindings || entity.owner) return nullptr;
    Herd* candidate = nullptr;
    size_t inspected = 0, matches = 0;
    bool complete = true;
    for (auto& noun : Manager::Get()->mNouns) {
        if (inspected++ == noun_limit) { complete = false; break; }
        if (noun.mID != entity.herd_native_id) continue;
        ++matches;
        if (!herd_diagnostic_binding(noun)) continue;
        auto herd = object_cast<Herd>(&noun);
        if (herd && static_cast<Noun*>(herd) == &noun && herd_diagnostic_layout(herd) &&
            !herd->mbIsDestroyed && !herd->field_20) candidate = herd;
    }
    const auto profile = complete && matches == 1 && candidate ?
        inspect_herd_profile(entity, candidate, phase, nullptr, diagnostics) : HerdProfileRead{};
    if (diagnostics) {
        char fields[640]{};
        sprintf_s(fields, ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_herd\":%u,"
            "\"census_complete\":%s,\"nouns_inspected\":%zu,\"noun_limit\":16384,\"exact_id_matches\":%zu,"
            "\"qualified\":%s,\"herd_enabled_observed\":%s,\"herd_enabled_byte\":%u,\"herd_enable_called\":false",
            phase, entity.id, entity.generation, entity.herd_native_id, complete ? "true" : "false", inspected, matches,
            profile.qualified ? "true" : "false", candidate ? "true" : "false",
            candidate ? unsigned(*reinterpret_cast<const unsigned char*>(&candidate->mbEnabled)) : 0);
        native_actor_worker_event("scene_existing_herd_preflight", fields);
    }
    return profile.qualified ? candidate : nullptr;
}
void herd_candidate_diagnostic(const NativeSceneEntity& entity, const char* phase, Noun& noun, Animal* source) {
    const bool binding_ok = herd_diagnostic_binding(noun);
    auto herd = binding_ok ? object_cast<Herd>(&noun) : nullptr;
    const bool layout_ok = herd && static_cast<Noun*>(herd) == &noun && herd_diagnostic_layout(herd);
    const bool live = layout_ok && !noun.mbIsDestroyed && !noun.field_20;
    const auto profile = live ? herd->mpSpeciesProfile : nullptr;
    const bool profile_readable = profile && diagnostic_profile_readable(profile) &&
        reinterpret_cast<uintptr_t>(&profile->mCreatureKey) - reinterpret_cast<uintptr_t>(profile) == 0x504;
    const ResourceKey key = profile_readable ? profile->mCreatureKey : ResourceKey();
    const ResourceKey owner_key = live ? herd->mOwnerSpeciesKey : ResourceKey();
    char fields[1536]{};
    sprintf_s(fields, ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
        "\"source_tick\":%llu,\"source_herd\":%u,\"source_species\":[%u,%u,%u],\"source_animal_archetype\":%u,"
        "\"candidate_native_id\":%u,\"exact_herd_id\":%s,\"vtable_binding\":%s,\"sdk_layout\":%s,"
        "\"destroyed\":%s,\"field_20\":%s,\"live_manager_herd\":%s,\"herd_scalars_observed\":%s,"
        "\"herd_archetype\":%u,\"herd_archetype_group\":%u,\"herd_generation\":%d,\"herd_enabled_byte\":%u,"
        "\"owner_species_key\":[%u,%u,%u],\"owner_key_matches\":%s,\"profile_present\":%s,\"profile_header_readable\":%s,"
        "\"profile_key\":[%u,%u,%u],\"profile_key_matches\":%s,\"authority_animal_observed\":%s,"
        "\"authority_herd_reference_matches\":%s,\"authority_profile_reference_matches\":%s,"
        "\"lifetime_basis\":\"current_engine_thread_manager_census\",\"profile_ownership_proven\":false,\"selection_performed\":false",
        phase, entity.id, entity.generation, entity.native_id, entity.tick, entity.herd_native_id,
        entity.species_instance, entity.species_type, entity.species_group, entity.archetype,
        noun.mID, noun.mID == entity.herd_native_id ? "true" : "false", binding_ok ? "true" : "false", layout_ok ? "true" : "false",
        noun.mbIsDestroyed ? "true" : "false", noun.field_20 ? "true" : "false", live ? "true" : "false", live ? "true" : "false",
        live ? herd->mArchetype : 0, live ? herd->mArchetypeGroup : 0, live ? herd->mGeneration : 0,
        live ? unsigned(*reinterpret_cast<const unsigned char*>(&herd->mbEnabled)) : 0,
        owner_key.instanceID, owner_key.typeID, owner_key.groupID, live && matches_species_key(owner_key, entity) ? "true" : "false",
        profile ? "true" : "false", profile_readable ? "true" : "false", key.instanceID, key.typeID, key.groupID,
        profile_readable && matches_species_key(key, entity) ? "true" : "false", source ? "true" : "false",
        source && live && source->mHerd.get() == herd ? "true" : "false",
        source && profile && source->mpSpeciesProfile == profile ? "true" : "false");
    native_actor_worker_event("scene_herd_candidate", fields);
    if (live) inspect_herd_profile(entity, herd, phase, source);
}
void herd_census_diagnostic(const NativeSceneEntity& entity, const char* phase, Animal* source = nullptr) {
    constexpr size_t noun_limit = 16384, alternative_limit = 4, exact_limit = 4;
    size_t inspected = 0, herd_count = 0, id_matches = 0, qualified_id_matches = 0, owner_key_matches = 0;
    size_t exact_logged = 0, alternatives_logged = 0;
    bool truncated = false;
    // Fresh census only: no stored native pointer, native AddRef, factory,
    // profile lookup/loading, herd enabling or gameplay callback is introduced.
    for (auto& noun : Manager::Get()->mNouns) {
        if (inspected == noun_limit) { truncated = true; break; }
        ++inspected;
        const bool exact = noun.mID == entity.herd_native_id;
        if (exact) ++id_matches;
        bool owner_match = false;
        if (herd_diagnostic_binding(noun)) {
            auto herd = object_cast<Herd>(&noun);
            if (herd && static_cast<Noun*>(herd) == &noun && herd_diagnostic_layout(herd)) {
                ++herd_count;
                if (!noun.mbIsDestroyed && !noun.field_20) {
                    if (exact) ++qualified_id_matches;
                    owner_match = matches_species_key(herd->mOwnerSpeciesKey, entity);
                    if (owner_match) ++owner_key_matches;
                }
            }
        }
        if (exact && exact_logged < exact_limit) { herd_candidate_diagnostic(entity, phase, noun, source); ++exact_logged; }
        else if (!exact && owner_match && alternatives_logged < alternative_limit) {
            herd_candidate_diagnostic(entity, phase, noun, source); ++alternatives_logged;
        }
    }
    char fields[768]{};
    sprintf_s(fields, ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
        "\"source_tick\":%llu,\"source_herd\":%u,\"bindings_qualified\":%s,\"nouns_inspected\":%zu,\"noun_limit\":%zu,"
        "\"census_complete\":%s,\"herds_with_verified_layout\":%zu,\"exact_id_matches\":%zu,\"live_exact_herds\":%zu,"
        "\"owner_key_matches\":%zu,\"exact_candidates_logged\":%zu,\"alternatives_logged\":%zu,\"selection_performed\":false",
        phase, entity.id, entity.generation, entity.native_id, entity.tick, entity.herd_native_id,
        herd_diagnostic_bindings ? "true" : "false", inspected, noun_limit, truncated ? "false" : "true",
        herd_count, id_matches, qualified_id_matches, owner_key_matches, exact_logged, alternatives_logged);
    native_actor_worker_event("scene_herd_census", fields);
}
void spawn_template_diagnostic(const NativeSceneEntity& entity, Animal* selected, const char* phase) {
    // Read-only, engine-thread diagnostics at a spawn boundary. Missing-template
    // counts are capped and do not select, retain or enable an alternative noun.
    size_t inspected = 0, present_count = 0, living_count = 0, species_count = 0;
    size_t archetype_count = 0, eligible_count = 0;
    bool truncated = false;
    if (!selected) for (auto& noun : Manager::Get()->mNouns) {
        if (inspected == 4096) { truncated = true; break; }
        ++inspected;
        if (!present(noun)) continue;
        ++present_count;
        auto animal = object_cast<Animal>(&noun);
        if (!animal->mbDead) ++living_count;
        if (animal->mSpeciesKey.instanceID != entity.species_instance ||
            animal->mSpeciesKey.typeID != entity.species_type || animal->mSpeciesKey.groupID != entity.species_group) continue;
        ++species_count;
        if (static_cast<uint32_t>(animal->mArchetype) != entity.archetype) continue;
        ++archetype_count;
        if (!animal->mbDead && supported(animal)) ++eligible_count;
    }
    char fields[1024]{};
    sprintf_s(fields, ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
        "\"source_species\":[%u,%u,%u],\"source_archetype\":%u,\"source_herd\":%u,"
        "\"template_selected\":%s,\"template_native_id\":%u,\"template_herd\":%u,"
        "\"census_observed\":%s,\"census_limit\":4096,\"census_truncated\":%s,\"nouns_inspected\":%zu,"
        "\"present_animals\":%zu,\"living_animals\":%zu,\"same_species_key\":%zu,\"same_archetype\":%zu,\"eligible_templates\":%zu",
        phase, entity.id, entity.generation, entity.native_id, entity.species_instance, entity.species_type, entity.species_group,
        entity.archetype, entity.herd_native_id, selected ? "true" : "false", selected ? selected->mID : UINT32_MAX,
        selected ? selected->mHerd->mID : UINT32_MAX, selected ? "false" : "true", truncated ? "true" : "false",
        inspected, present_count, living_count, species_count, archetype_count, eligible_count);
    native_actor_worker_event("scene_spawn_template", fields);
    if (!selected) herd_census_diagnostic(entity, phase);
}
void spawn_factory_diagnostic(const NativeSceneEntity& entity, const char* outcome, uint32_t local_id = UINT32_MAX) {
    char fields[384]{};
    sprintf_s(fields, ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
        "\"factory_outcome\":\"%s\",\"native_id\":%u", entity.id, entity.generation, entity.native_id, outcome, local_id);
    native_actor_worker_event("scene_spawn_factory", fields);
}
bool projection_rejected(Animal* animal, const NativeSceneEntity& entity, const char* reason, float maximum = -1, bool context_reverted = false) {
    char health[896]{};
    health_context(animal, health);
    char fields[1536]{};
    sprintf_s(fields, ",\"reason\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,"
        "\"source_tick\":%llu,\"source_health\":%.9g,\"source_life_state\":%u,\"local_health\":%.9g,\"local_max_health\":%.9g,\"local_dead\":%s,"
        "\"source_age\":%u,\"source_alpha\":%u,\"source_combatant_state\":%u,\"source_scale\":%.9g,\"native_context_reverted\":%s%s",
        reason, entity.id, entity.generation, animal ? animal->mID : UINT32_MAX, entity.tick, double(entity.health),
        entity.life_state, animal ? double(animal->mHealthPoints) : -1.0, double(maximum), animal && animal->mbDead ? "true" : "false",
        entity.age, entity.alpha, entity.combatant_state, double(entity.scale), context_reverted ? "true" : "false", health);
    native_actor_worker_event("scene_projection_rejected", fields);
    return false;
}
bool apply_pose(Animal* animal, const NativeSceneEntity& entity) {
    if (!same_species(animal, entity)) return projection_rejected(animal, entity, "species_or_native_binding");
    if (!valid(entity)) return projection_rejected(animal, entity, "invalid_authoritative_values");
    if (!native_replica_allows(replica::Mutation::apply_state)) return projection_rejected(animal, entity, "application_scope");
    if (!scalar_context_binding(animal)) return projection_rejected(animal, entity, "native_context_binding");
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    constexpr int alpha_mask = Simulator::kCreatureFlagIsAlpha;
    static_assert(alpha_mask == 1);
    const int old_age = animal->mAge, old_flags = animal->mGeneralFlags, old_state = combatant->field_34;
    const float old_scale = animal->GetScale();
    if (!std::isfinite(old_scale) || old_scale <= 0 || old_scale > 1000)
        return projection_rejected(animal, entity, "unsupported_native_scale");
    const auto restore_context = [&]() {
        animal->mAge = old_age;
        animal->mGeneralFlags = old_flags;
        combatant->field_34 = old_state;
    };
    // Apply only the authority's qualified scalar context. The local avatar
    // bit and every flag other than alpha remain local. Do not call GrowUp,
    // Heal or death/revive callbacks: they execute original gameplay effects.
    animal->mAge = static_cast<int>(entity.age);
    animal->mGeneralFlags = (old_flags & ~alpha_mask) | (entity.alpha ? alpha_mask : 0);
    combatant->field_34 = static_cast<int>(entity.combatant_state);
    const auto maximum = animal->GetMaxHitPoints();
    if (!std::isfinite(maximum) || maximum <= 0 || entity.health > maximum) {
        restore_context();
        // local_max_health records the rejected staged-context calculation;
        // health_context independently rereads the restored native values.
        return projection_rejected(animal, entity, "maximum_health_mismatch", maximum, true);
    }
    if (old_scale != entity.scale) {
        // C0E750 -> C0DB10 requires both pointers even though the setter's
        // first animation write is conditional. Supported live native objects
        // retain their SDK intrusive animation reference on this engine thread.
        if (!scale_presentation_available(animal, entity.scale)) {
            restore_context();
            return projection_rejected(animal, entity, "scale_presentation_unavailable", maximum, true);
        }
        // Use the exact authority observation. CalculateScale consumes native
        // RNG/global-avatar context; GrowUp also changes gameplay. SetScale
        // updates existing model scale/bounds, not baby/adult model selection.
        animal->SetScale(entity.scale);
    }
    // Replica values update original model/physics presentation through the
    // actual Creature override. Worker movement still uses native WalkTo.
    const Math::Vector3 position(entity.x, entity.y, entity.z);
    const Math::Quaternion orientation(entity.qx, entity.qy, entity.qz, entity.qw);
    animal->SetPosition(position);
    animal->SetOrientation(orientation);
    // The pinned SDK's SetVelocity implementation is this exact scalar copy;
    // animation choice remains original cCreatureBase::Update behavior.
    animal->mVelocity = Math::Vector3(entity.vx, entity.vy, entity.vz);
    const bool was_dead = animal->mbDead;
    animal->mHealthPoints = entity.health;
    // SDK cbf9206 cCreatureBase.h:280, pinned C0838F's byte store and
    // C0BB00's clear identify mbDead at +B5E. Project the observed scalar state only; calling
    // native death/revive handlers would replay behavior/reward/UI side effects.
    // This does not claim native death animation or game-over UI replication.
    animal->mbDead = entity.life_state == 1;
    // Absolute replica state only. B5F records the original first-feed bonus;
    // it does not mean all nutrition is gone. Never replay consume/award code.
    animal->mbHasBeenEaten = entity.fed_on == 1;
    animal->mFoodValue = entity.food;
    animal->mEnergy = entity.energy;
    animal->mHunger = entity.hunger;
    if (entity.owner && entity.owner == controlled_owner) Simulator::cCreatureGameData::SetEvolutionPoints(entity.dna);
    const auto& applied = animal->GetPosition();
    const auto& rotated = animal->GetOrientation();
    if (applied.x != position.x || applied.y != position.y || applied.z != position.z)
        return projection_rejected(animal, entity, "position_readback", maximum);
    if (rotated.x != orientation.x || rotated.y != orientation.y || rotated.z != orientation.z || rotated.w != orientation.w)
        return projection_rejected(animal, entity, "orientation_readback", maximum);
    if (animal->mAge != static_cast<int>(entity.age) ||
        animal->mGeneralFlags != ((old_flags & ~alpha_mask) | (entity.alpha ? alpha_mask : 0)) ||
        combatant->field_34 != static_cast<int>(entity.combatant_state) ||
        animal->mHealthPoints != entity.health || animal->mbDead != (entity.life_state == 1) ||
        animal->mbHasBeenEaten != (entity.fed_on == 1) || animal->mFoodValue != entity.food)
        return projection_rejected(animal, entity, "native_context_readback", maximum);
    if (animal->GetScale() != entity.scale)
        return projection_rejected(animal, entity, "native_scale_readback", maximum);
    if (was_dead != animal->mbDead) actor_sample("scene_native_life_state_applied", animal, entity);
    if (old_age != animal->mAge || old_flags != animal->mGeneralFlags || old_state != combatant->field_34 || old_scale != entity.scale)
        actor_sample("scene_native_context_applied", animal, entity);
    return true;
}
Animal* create(const NativeSceneEntity& entity, bool diagnose_spawn = false) {
    auto source = template_for(entity);
    if (diagnose_spawn) spawn_template_diagnostic(entity, source, "factory_selection");
    auto herd = source ? source->mHerd.get() : existing_herd_for(entity, "factory_fallback", true);
    if (!herd) return nullptr;
    // Run09 observed exact herd 1772/profile on an inactive replica. Use only
    // its existing manager-owned profile; do not enable the herd or load one.
    // Recheck immediately before the original factory, in the same apply scope.
    const auto fallback = source ? HerdProfileRead{} : inspect_herd_profile(entity, herd, "factory_recheck", nullptr, false);
    if (!source && (!fallback.qualified || !native_replica_allows(replica::Mutation::apply_state))) return nullptr;
    const auto species = source ? source->mpSpeciesProfile : fallback.profile;
    // Native factory C09B40 has the proven six-word cdecl ABI and retains a
    // live native herd. A null herd is unsafe in this Creature campaign.
    auto result = Animal::Create(Math::Vector3(entity.x, entity.y, entity.z),
        species, static_cast<int>(entity.age), herd, false, false);
    if (!result) {
        if (diagnose_spawn || !source) spawn_factory_diagnostic(entity, "returned_null");
        return nullptr;
    }
    if (!same_species(result, entity) || (!source &&
        (result->mHerd.get() != herd || result->mpSpeciesProfile != species ||
            reinterpret_cast<uintptr_t>(result->field_E84) != fallback.archetype))) {
        if (diagnose_spawn || !source) spawn_factory_diagnostic(entity, "species_or_support_mismatch_destroy", result->mID);
        Manager::Get()->DestroyInstance(result);
        return nullptr;
    }
    if (diagnose_spawn || !source) spawn_factory_diagnostic(entity, "qualified_native_result", result->mID);
    return result;
}
bool apply_bound_pose(Animal* animal, const NativeSceneEntity& entity, Binding& local) {
    const bool was_dead = animal->mbDead;
    const bool was_fed_on = animal->mbHasBeenEaten;
    const float old_food = animal->mFoodValue;
    if (!apply_pose(animal, entity)) return false;
    const bool dead = entity.life_state == 1;
    const auto model = reinterpret_cast<uintptr_t>(animal->mpAnimatedCreature.get());
    const bool changed = local.presentation_seen &&
        (local.presentation_dead != dead || local.presentation_model != model);
    const bool present_life = dead ? !local.presentation_seen || changed : was_dead || changed;
    if (present_life && !native_life_present(animal, dead, entity.id, entity.generation))
        return projection_rejected(animal, entity, "life_presentation");
    local.presentation_seen = true;
    local.presentation_dead = dead;
    local.presentation_model = model;
    // Record each changed native nutrition readback at its actual source tick,
    // including a fresh fed baseline or a newly observed beneficiary. The
    // beneficiary is replicated metadata; food/first-feed are native rereads.
    if (was_fed_on != animal->mbHasBeenEaten || old_food != animal->mFoodValue ||
        (!local.pickup_seen && entity.fed_on) || (local.pickup_seen && local.pickup_owner != entity.pickup_owner))
        actor_sample("scene_native_pickup_state_applied", animal, entity);
    local.pickup_seen = true;
    local.pickup_owner = entity.pickup_owner;
    return true;
}
struct Projection { const NativeSceneEntity* entity; Binding* local; };
bool project_callback(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    auto animal = find(args.local->native_id, args.local->address);
    if (!animal || !args.local->live || args.local->owner != args.entity->owner)
        return projection_rejected(animal, *args.entity, "native_binding_or_owner");
    if (!apply_bound_pose(animal, *args.entity, *args.local)) return false;
    const auto now = GetTickCount64();
    if (now >= args.local->next_sample) {
        args.local->next_sample = now + 1000;
        actor_sample("scene_native_replica_sample", animal, *args.entity);
    }
    return true;
}
bool spawn_callback(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    auto animal = create(*args.entity, true);
    if (!animal) return false;
    *args.local = {args.entity->id, args.entity->generation, animal->mID, reinterpret_cast<uintptr_t>(animal), true, args.entity->owner};
    return apply_bound_pose(animal, *args.entity, *args.local);
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
        } else if (!template_for(e) && !existing_herd_for(e, "baseline_preflight", true)) {
            args.result = Result::missing_template; return false;
        }
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
    for (auto& noun : Manager::Get()->mNouns) if (present(noun) && !mapped(noun.mID)) {
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
    const std::array<unsigned char, 7> death_field{0xc6, 0x86, 0x5e, 0x0b, 0, 0, 1};
    const std::array<unsigned char, 7> revive_field{0xc6, 0x81, 0x5e, 0x0b, 0, 0, 0};
    const std::array<unsigned char, 7> first_feed_read{0x80, 0xb9, 0x5f, 0x0b, 0, 0, 0};
    const std::array<unsigned char, 7> first_feed_write{0xc6, 0x82, 0x5f, 0x0b, 0, 0, 1};
    const std::array<unsigned char, 7> food_read{0xd9, 0x81, 0x84, 0x0b, 0, 0, 0xc3};
    const std::array<unsigned char, 8> food_write{0xf3, 0x0f, 0x11, 0x88, 0x84, 0x0b, 0, 0};
    const std::array<unsigned char, 7> age_field{0x83, 0xb9, 0x34, 0x0b, 0, 0, 1};
    const std::array<unsigned char, 10> age_write{0xc7, 0x86, 0x34, 0x0b, 0, 0, 1, 0, 0, 0};
    const std::array<unsigned char, 7> alpha_field{0xf6, 0x87, 0xb0, 0x05, 0, 0, 1};
    const std::array<unsigned char, 4> combatant_state_read{0x8b, 0x41, 0x34, 0xc3};
    const std::array<unsigned char, 7> combatant_state_write{0xc7, 0x46, 0x34, 2, 0, 0, 0};
    const std::array<unsigned char, 9> base_maximum{0x83, 0xec, 0x08, 0x0f, 0x57, 0xc0, 0x56, 0x8b, 0xf1};
    const std::array<unsigned char, 4> get_scale{0xd9, 0x41, 0x64, 0xc3};
    const std::array<unsigned char, 9> set_scale{0xf3, 0x0f, 0x10, 0x44, 0x24, 0x04, 0x56, 0x8b, 0xf1};
    const std::array<unsigned char, 7> set_extents{0x83, 0xec, 0x2c, 0x8b, 0x44, 0x24, 0x30};
    const std::array<unsigned char, 20> herd_cast{0x8b, 0xc1, 0x8b, 0x4c, 0x24, 0x04, 0x81, 0xf9, 0x6e, 0x51,
        0x3f, 0xee, 0x74, 0x13, 0x81, 0xf9, 0x17, 0x61, 0xaa, 0x52};
    const std::array<unsigned char, 6> herd_noun{0xb8, 0x8e, 0x41, 0xbe, 0x01, 0xc3};
    const std::array<unsigned char, 6> herd_type{0xb8, 0x17, 0x61, 0xaa, 0x52, 0xc3};
    const std::array<unsigned char, 12> herd_archetype_generation{0x8b, 0x8f, 0xf0, 0, 0, 0, 0x8b, 0x97, 0x88, 0, 0, 0};
    const std::array<unsigned char, 27> herd_profile_key{0x8b, 0x86, 0xa4, 0, 0, 0, 0x85, 0xc0, 0x74, 0x23,
        0x8b, 0x90, 0x04, 0x05, 0, 0, 0x05, 0x04, 0x05, 0, 0, 0x89, 0x96, 0x98, 0, 0, 0};
    // Keep optional herd diagnostics/fallback bindings separate from the
    // already-qualified living-template path and normal scene admission.
    herd_diagnostic_bindings = image &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x869f50), herd_cast.data(), herd_cast.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x86a110), herd_noun.data(), herd_noun.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x86c0a0), herd_type.data(), herd_type.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x809c26), herd_archetype_generation.data(), herd_archetype_generation.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x86c9ec), herd_profile_key.data(), herd_profile_key.size());
    // Both absolute operands have IMAGE_REL_BASED_HIGHLOW relocations in the
    // pinned PE. Validate their loaded image-relative targets, not file bytes.
    const std::array<unsigned char, 4> species_get{0x55, 0x8b, 0xec, 0xa1};
    const std::array<unsigned char, 2> species_get_return{0x5d, 0xc3};
    const std::array<unsigned char, 2> species_table{0xc7, 0x01};
    const std::array<unsigned char, 15> species_bucket{0xf7, 0x71, 0x08, 0x89, 0x55, 0xfc, 0x8b, 0x55, 0xc4,
        0x8b, 0x42, 0x04, 0x8b, 0x4d, 0xfc};
    const std::array<unsigned char, 6> species_next{0x8b, 0x45, 0xdc, 0x8b, 0x48, 0x10};
    const std::array<unsigned char, 9> archetype_next{0x8b, 0x45, 0xd8, 0x8b, 0x88, 0x44, 0x04, 0, 0};
    const std::array<unsigned char, 9> generation_vector{0x8b, 0x45, 0xf4, 0x8b, 0x88, 0xc4, 0x03, 0, 0};
    const std::array<unsigned char, 9> archetype_value{0x8b, 0x40, 0x10, 0x89, 0x81, 0x80, 0x0e, 0, 0};
    static_assert(sizeof(void*) == 4 && sizeof(Simulator::cSpeciesProfile) == 0xa18);
    herd_profile_bindings = herd_diagnostic_bindings &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x1090), species_get.data(), species_get.size()) &&
        native_word(image + 0x1094) == image + 0x11d0c24 &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x1098), species_get_return.data(), species_get_return.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0xdb335), species_table.data(), species_table.size()) &&
        native_word(image + 0xdb337) == image + 0xff08c8 &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0xe100f), species_bucket.data(), species_bucket.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0xe1026), species_next.data(), species_next.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0xe1586), archetype_next.data(), archetype_next.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0xe0179), generation_vector.data(), generation_vector.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80c2ee), archetype_value.data(), archetype_value.size());
    const std::array<unsigned char, 7> pool_spatial_disable{0xc6, 0x46, 0x71, 0x01, 0x88, 0x5e, 0x75};
    const std::array<unsigned char, 6> pool_counter_increment{0xff, 0x86, 0x54, 0x0e, 0, 0};
    const std::array<unsigned char, 6> pool_counter_initialize{0x89, 0x9e, 0x54, 0x0e, 0, 0};
    const std::array<unsigned char, 6> pool_herd_assignment{0x89, 0xbe, 0x74, 0x16, 0, 0};
    const std::array<unsigned char, 7> inactive_update_gate{0x83, 0xbe, 0x74, 0x16, 0, 0, 0};
    pool_lifecycle_bindings = image &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x6ccfdf), pool_spatial_disable.data(), pool_spatial_disable.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x6cd0d5), pool_counter_increment.data(), pool_counter_increment.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x81fd5d), pool_counter_initialize.data(), pool_counter_initialize.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80472f), pool_herd_assignment.data(), pool_herd_assignment.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80ae33), inactive_update_gate.data(), inactive_update_gate.size());
    ready = image && !std::memcmp(reinterpret_cast<const void*>(image + 0x80dea0), position.data(), position.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80df10), orientation.data(), orientation.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80838f), death_field.data(), death_field.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80bb00), revive_field.data(), revive_field.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x97137a), first_feed_read.data(), first_feed_read.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x971398), first_feed_write.data(), first_feed_write.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80bc70), food_read.data(), food_read.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x970ac2), food_write.data(), food_write.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80b8d2), age_field.data(), age_field.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x8223c6), age_write.data(), age_write.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x805e6e), alpha_field.data(), alpha_field.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x4e8230), combatant_state_read.data(), combatant_state_read.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x7fd1a1), combatant_state_write.data(), combatant_state_write.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x804170), base_maximum.data(), base_maximum.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x837230), get_scale.data(), get_scale.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x80e750), set_scale.data(), set_scale.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image + 0x8892f0), set_extents.data(), set_extents.size()) && pool_lifecycle_bindings;
    event("scene_adapter_ready", ready ? "pinned_pose_bindings" : "binding_mismatch");
    event("scene_herd_diagnostic_ready", herd_diagnostic_bindings ? "pinned_herd_profile_reads" : "binding_mismatch");
    event("scene_herd_profile_ready", herd_profile_bindings ? "pinned_current_manager_ownership" : "binding_mismatch");
    event("scene_pool_lifecycle_ready", pool_lifecycle_bindings ? "pinned_original_pool_return" : "binding_mismatch");
    return ready;
}
void dispose_native_scene() {
    if (!on_thread()) return;
    observed = {}; bindings = {}; capture_epoch = admitted_epoch = 0; controlled_owner = 0;
    ready = false; herd_diagnostic_bindings = herd_profile_bindings = pool_lifecycle_bindings = false; engine_thread = 0; image = 0;
}
NativeSceneResult capture_native_scene(NativeSceneFrame& frame) {
    frame.count = 0; frame.epoch = 0;
    if (!on_thread()) return Result::wrong_thread;
    if (!available()) return Result::unavailable;
    const auto status = native_actor_worker_status();
    if (!status.epoch) return Result::unavailable;
    if (capture_epoch != status.epoch) {
        observed = {}; capture_epoch = status.epoch;
        last_inactive_shell_count = last_pooled_shell_count = SIZE_MAX;
    }
    for (auto& s : observed) s.visited = false;
    uint32_t a = UINT32_MAX, b = UINT32_MAX;
    if (!native_actor_owner_native_id(1, a) || !native_actor_owner_native_id(2, b) || a == b) return Result::unavailable;
    const auto now = GetTickCount64();
    const bool sample = now >= next_capture_sample;
    // Diagnose the complete bounded census before returning a failure. A low
    // native ID alone does not establish that an unsupported noun is a pool
    // placeholder, so do not silently omit it from the shared scene.
    size_t unsupported_count = 0, inactive_shell_count = 0, pooled_shell_count = 0;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) continue;
        auto animal = object_cast<Animal>(&noun);
        if (!animal || animal->mbMarkedForDeletion) continue;
        if (inactive_shell(animal)) { ++inactive_shell_count; continue; }
        if (pooled_shell(animal)) {
            if (animal->mID == a || animal->mID == b) {
                capture_rejection(animal, "owned_actor_pool_return");
                return Result::unsupported_entity;
            }
            ++pooled_shell_count;
            for (auto& seen : observed) if (seen.native_id == animal->mID && seen.address == reinterpret_cast<uintptr_t>(animal))
                retire_incarnation(seen, "observed_original_pool_return", static_cast<uint32_t>(animal->field_E54));
            continue;
        }
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
    if (pooled_shell_count != last_pooled_shell_count) {
        last_pooled_shell_count = pooled_shell_count;
        char fields[160]{};
        sprintf_s(fields, ",\"count\":%u,\"reason\":\"original_retained_pool_signature\"", static_cast<unsigned>(pooled_shell_count));
        native_actor_worker_event("scene_pooled_shell_census", fields);
    }
    if (unsupported_count) {
        event("scene_capture_rejection_count", "unsupported_present_entities", 0, static_cast<uint32_t>(unsupported_count));
        return Result::unsupported_entity;
    }
    std::array<Animal*, capacity> captured{};
    for (auto& noun : Manager::Get()->mNouns) if (present(noun)) {
        if (frame.count == capacity) return Result::full;
        auto animal = object_cast<Animal>(&noun);
        Seen* seen = nullptr;
        bool first_capture = false;
        for (auto& s : observed) if (s.native_id == animal->mID && s.address == reinterpret_cast<uintptr_t>(animal)) { seen = &s; break; }
        if (seen && seen->visited) return Result::binding_mismatch;
        const auto pool_cycle = static_cast<uint32_t>(animal->field_E54);
        if (seen && seen->pool_cycle != pool_cycle) {
            retire_incarnation(*seen, "native_pool_cycle_changed", pool_cycle);
            seen = nullptr;
        }
        if (!seen) {
            for (auto& s : observed) if (!s.address) { seen = &s; break; }
            if (!seen) return Result::full;
            *seen = {animal->mID, reinterpret_cast<uintptr_t>(animal), ++next_incarnation, pool_cycle, false};
            first_capture = true;
        }
        seen->visited = true;
        captured[frame.count] = animal;
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
        e.life_state = animal->mbDead ? 1u : 0u;
        const auto combatant = static_cast<Simulator::cCombatant*>(animal);
        if (!scalar_context_binding(animal)) {
            event("scene_capture_rejected", "native_context_binding", e.id, e.native_id);
            return Result::binding_mismatch;
        }
        const auto fed_on = *reinterpret_cast<const unsigned char*>(&animal->mbHasBeenEaten);
        if (fed_on > 1) {
            event("scene_capture_rejected", "unsupported_native_first_feed_flag", e.id, e.native_id);
            return Result::unsupported_entity;
        }
        e.fed_on = fed_on;
        e.food = animal->mFoodValue;
        e.pickup_owner = fed_on ? native_actor_pickup_owner(animal->mID) : 0;
        if ((animal->mAge != 0 && animal->mAge != 1) || (combatant->field_34 != 0 && combatant->field_34 != 2)) {
            actor_sample("scene_capture_context_rejected", animal, e);
            event("scene_capture_rejected", "unsupported_native_context", e.id, e.native_id);
            return Result::unsupported_entity;
        }
        e.age = static_cast<uint32_t>(animal->mAge);
        e.alpha = (animal->mGeneralFlags & Simulator::kCreatureFlagIsAlpha) ? 1u : 0u;
        e.combatant_state = static_cast<uint32_t>(combatant->field_34);
        e.scale = animal->GetScale();
        if (e.owner && !native_actor_owner_dna(static_cast<uint32_t>(e.owner), e.dna)) {
            event("scene_capture_rejected", "owner_dna_unavailable", e.id, e.native_id);
            return Result::unavailable;
        }
        if (!valid(e)) { event("scene_capture_rejected", "unsupported_native_values", e.id, e.native_id); return Result::unsupported_entity; }
        if (first_capture) herd_census_diagnostic(e, "authority_first_capture", animal);
        if (sample) actor_sample("scene_native_authority_sample", animal, e);
    }
    for (auto& s : observed) if (!s.visited) s = {};
    frame.epoch = status.epoch;
    // Native12 showed that one-second samples can miss every shared food
    // revision while original feeding changes it continuously. Retain an
    // exact-tick source reread for each fed corpse in this bounded successful
    // frame. Pointers exist only on this engine-thread capture stack.
    for (size_t i = 0; i < frame.count; ++i) if (frame.entities[i].life_state == 1 && frame.entities[i].fed_on)
        actor_sample("scene_native_pickup_state_published", captured[i], frame.entities[i]);
    if (sample) next_capture_sample = now + 1000;
    return Result::accepted;
}
NativeSceneResult validate_native_scene_action_entity(uint64_t epoch, const NativeSceneEntity& entity) {
    if (!on_thread()) return Result::wrong_thread;
    if (!available() || native_replica_is_client()) return Result::unavailable;
    if (!epoch || epoch != capture_epoch || epoch != native_actor_worker_status().epoch ||
        epoch > UINT32_MAX || entity.id != ((epoch << 32) | entity.native_id)) return Result::stale;
    if (!valid(entity)) return Result::invalid;
    const Seen* incarnation = nullptr;
    for (const auto& seen : observed) if (seen.native_id == entity.native_id && seen.generation == entity.generation) {
        if (incarnation) return Result::binding_mismatch;
        incarnation = &seen;
    }
    // The original destruction hook clears Seen before the native destructor.
    // Even reuse of both the same ID and address therefore fails this fence.
    if (!incarnation || !incarnation->address) return Result::stale;
    auto animal = find(entity.native_id, incarnation->address);
    // Validate current identity/life state for both living actors and corpse
    // targets. The caller separately admits pickup vs combat command semantics.
    if (!animal || static_cast<uint32_t>(animal->field_E54) != incarnation->pool_cycle ||
        animal->mbDead != (entity.life_state == 1) || !same_fixture(animal, entity)) return Result::stale;
    if (entity.owner) {
        uint32_t current_native_id = UINT32_MAX;
        if (!native_actor_owner_native_id(static_cast<uint32_t>(entity.owner), current_native_id) ||
            current_native_id != entity.native_id) return Result::binding_mismatch;
    }
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
    event("scene_baseline_applied", "creature_fixture_scalar_life_state", 0, static_cast<uint32_t>(frame.count));
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
    if (!template_for(entity) && !existing_herd_for(entity, "spawn_preflight", true)) {
        spawn_template_diagnostic(entity, nullptr, "spawn_preflight");
        return Result::missing_template;
    }
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
