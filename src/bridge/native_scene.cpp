#include "native_scene.h"
#include "native_checkpoint.h"
#include "native_scene_ownership.h"
#include "native_scene_portrait_abi.h"
#include "native_observation.h"
#include <Spore/Simulator/cPlayer.h>
#include "native_actors.h"
#include "native_pickup.h"
#include "native_replica.h"
#include "native_life_presentation.h"
#include "../network/steering.h"
#include <Spore/App/IGameModeManager.h>
#include <Spore/App/ICameraManager.h>
#include <Spore/App/ICamera.h>
#include <Spore/App/cViewer.h>
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/UTFWin/Image.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/Simulator/cNest.h>
#include <Spore/Simulator/cInteractiveOrnament.h>
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
using Fossil = Simulator::cInteractiveOrnament;
using Nest = Simulator::cNest;
using EntityKind = network::EntityKind;
using Result = NativeSceneResult;
constexpr auto capacity = NativeSceneFrame::capacity;
DWORD engine_thread = 0;
uintptr_t image = 0;
uint64_t capture_epoch = 0, next_incarnation = 0, admitted_epoch = 0;
uint64_t next_capture_sample = 0;
size_t last_inactive_shell_count = SIZE_MAX, last_unbound_shell_count = SIZE_MAX, last_pooled_shell_count = SIZE_MAX;
uint32_t controlled_owner = 0;
bool ready = false;
bool herd_diagnostic_bindings = false;
bool herd_profile_bindings = false;
bool pool_lifecycle_bindings = false;

// The noun the original keeps for a player of the game it runs in.
constexpr uint32_t native_player_noun_id = 0x02c21781;

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
    EntityKind kind = EntityKind::animal;
    uint64_t fossil_tick = 0;
    // When a fruit's sample was last applied, by the system's millisecond clock.
    uint64_t applied_ms = 0;
    bool fossil_lifetime_fenced = false;
    // The recharging abilities last received for a player's creature.
    std::array<uint32_t, 3> recharging{};
    // Model drift: whether the last pose relocated the model, and the gap that relocation left.
    bool drift_relocated = false;
    float drift_residual = 0;
};

std::array<Seen, capacity> observed{};
std::array<Binding, capacity> bindings{};

struct ReplicaOwned {
    uint64_t epoch = 0;
    uint32_t owner = 0, player_id = UINT32_MAX;
    uintptr_t player = 0, primary_profile = 0;
    ResourceKey default_key{};
    std::array<Binding, 3> actors{};
    std::array<uint32_t, 3> pool_cycles{};
    bool projecting = false, admitted = false, presentation = false, retired = false;
    uint64_t normalizations = 0;
} replica_owned;

// The creature on this client whose recharge the original draws and advances: its own.
uintptr_t recharge_creature = 0;

bool on_thread() {
    return engine_thread && engine_thread == GetCurrentThreadId();
}

void event(const char* name, const char* reason, uint64_t id = 0, uint32_t local = UINT32_MAX) {
    char fields[384]{};
    sprintf_s(fields, ",\"reason\":\"%s\",\"remote_entity\":%llu,\"native_id\":%u", reason, id, local);
    native_actor_worker_event(name, fields);
}

void health_context(Animal* animal, char (&fields)[896]);
void presentation_context(Animal* animal, char (&fields)[1024]);

void actor_sample(const char* name, Animal* animal, const NativeSceneEntity& entity) {
    const auto& position = animal->GetPosition();
    const auto& orientation = animal->GetOrientation();
    const auto& velocity = animal->mVelocity;
    float native_dna = 0;
    bool dna_observed = false;
    if (entity.owner) {
        if (!native_replica_is_client()) {
            bool current_native = false;
            dna_observed = native_actor_owner_dna(static_cast<uint32_t>(entity.owner), native_dna, &current_native) &&
                           current_native;
        } else if (entity.owner == controlled_owner && animal == Manager::Get()->GetAvatar()) {
            native_dna = Simulator::cCreatureGameData::GetEvolutionPoints();
            dna_observed = std::isfinite(native_dna) && native_dna >= 0;
        }
    }
    char dna_value[48] = "null";
    if (dna_observed) {
        sprintf_s(dna_value, "%.9g", double(native_dna));
    }
    char health[896]{};
    health_context(animal, health);
    uint32_t native_animation = 0;
    int native_animation_index = -1;
    const bool animation_observed =
        native_replica_is_client() && native_life_read_animation(animal, native_animation, native_animation_index);
    char presentation[1024]{};
    if (entity.owner) {
        presentation_context(animal, presentation);
    }
    char fields[4096]{};
    sprintf_s(
        fields,
        ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"owner\":%llu,\"species\":[%u,%u,%u],"
        "\"position\":[%.9g,%.9g,%.9g],\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"velocity\":[%.9g,%.9g,%.9g],"
        "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"source_dna\":%.9g,\"native_dna\":%s,\"dna_native_observed\":"
        "%s,"
        "\"dead\":%s,\"is_native_avatar\":%s,\"source_tick\":%llu,"
        "\"source_age\":%u,\"source_alpha\":%u,\"source_combatant_state\":%u,\"source_scale\":%.9g,"
        "\"source_fed_on\":%u,\"source_food\":%.9g,\"source_pickup_owner\":%u,\"native_fed_on\":%u,\"native_food\":%."
        "9g,\"native_pool_cycle\":%u,"
        "\"native_animation_observed\":%s,\"native_animation\":%u,\"native_animation_index\":%d%s%s",
        entity.id, entity.generation, animal->mID, entity.owner, entity.species_instance, entity.species_type,
        entity.species_group, double(position.x), double(position.y), double(position.z), double(orientation.x),
        double(orientation.y), double(orientation.z), double(orientation.w), double(velocity.x), double(velocity.y),
        double(velocity.z), double(animal->mHealthPoints), double(animal->mEnergy), double(animal->mHunger),
        double(entity.dna), dna_value, dna_observed ? "true" : "false", animal->mbDead ? "true" : "false",
        animal == Manager::Get()->GetAvatar() ? "true" : "false", entity.tick, entity.age, entity.alpha,
        entity.combatant_state, double(entity.scale), entity.fed_on, double(entity.food), entity.pickup_owner,
        unsigned(*reinterpret_cast<const unsigned char*>(&animal->mbHasBeenEaten)), double(animal->mFoodValue),
        static_cast<uint32_t>(animal->field_E54), animation_observed ? "true" : "false", native_animation,
        native_animation_index, health, presentation);
    native_actor_worker_event(name, fields);
}

bool available() {
    return on_thread() && ready && Simulator::GetGameModeID() == kGameCreature && Manager::Get();
}

bool inactive_shell(Animal* animal);
bool unbound_shell(Animal* animal);
bool pooled_shell(Animal* animal);
bool scalar_context_binding(Animal* animal);

bool present(Noun& noun) {
    if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) {
        return false;
    }
    auto animal = object_cast<Animal>(&noun);
    return animal && !animal->mbMarkedForDeletion && !inactive_shell(animal) && !unbound_shell(animal) &&
           !pooled_shell(animal);
}

bool living(Noun& noun) {
    return present(noun) && !object_cast<Animal>(&noun)->mbDead;
}

Animal* find(uint32_t id, uintptr_t expected = 0) {
    if (!available() || id == UINT32_MAX) {
        return nullptr;
    }
    Animal* result = nullptr;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mID == id) {
            if (result || !present(noun)) {
                return nullptr;
            }
            auto animal = object_cast<Animal>(&noun);
            if (expected && reinterpret_cast<uintptr_t>(animal) != expected) {
                return nullptr;
            }
            result = animal;
        }
    }
    return result;
}

bool spatial_binding(Animal* animal) {
    if (!animal) {
        return false;
    }
    auto spatial = static_cast<Spatial*>(animal);
    // SDK cbf9206, cCreatureBase.h:95–100 and cSpatialObject.h:67–77.
    // Installed dc04aee5... secondary vtable 01469EC0; checked against the
    // pinned PE with objdump. SetPosition/SetOrientation receive ECX=spatial
    // plus one pointer and RET 4; no raw primary-this casts are used.
    if (reinterpret_cast<uintptr_t>(spatial) - reinterpret_cast<uintptr_t>(animal) != 0xc0) {
        return false;
    }
    auto table = *reinterpret_cast<const uintptr_t* const*>(spatial);
    return reinterpret_cast<uintptr_t>(table) == image + 0x1069ec0 && table[0x2c / 4] == image + 0x2e6560 &&
           table[0x30 / 4] == image + 0x57e7c0 && table[0x34 / 4] == image + 0x837230 &&
           table[0x38 / 4] == image + 0x80dea0 && table[0x3c / 4] == image + 0x80df10 &&
           table[0x40 / 4] == image + 0x80e750 && table[0x44 / 4] == image + 0x80c040 &&
           table[0x64 / 4] == image + 0x8892f0;
}

bool inactive_shell(Animal* animal) {
    if (!animal || !spatial_binding(animal)) {
        return false;
    }
    // Native run fe2ae109... / actors-11820, sequence124+: preallocated
    // animal nouns exist before receiving a species, herd or render model.
    // Exclude only the complete observed inactive default signature. A
    // partially initialized or enabled animal still fails normal admission.
    const auto spatial = static_cast<Spatial*>(animal);
    return !animal->mpSpeciesProfile && !animal->mHerd && !animal->field_E84 && !animal->mSpeciesKey.instanceID &&
           !animal->mSpeciesKey.typeID && !animal->mSpeciesKey.groupID && !animal->mArchetype &&
           !animal->mGeneralFlags && !animal->mpAnimatedCreature && !spatial->mbEnabled && !spatial->mpModel &&
           !spatial->mpModelWorld && spatial->mPosition.x == 0 && spatial->mPosition.y == 0 &&
           spatial->mPosition.z == 0 && spatial->mOrientation.x == 0 && spatial->mOrientation.y == 0 &&
           spatial->mOrientation.z == 0 && spatial->mOrientation.w == 1 && animal->mHealthPoints == 1 &&
           animal->mEnergy == 1000 && animal->mHunger == 100;
}

bool unbound_shell(Animal* animal) {
    if (!animal || !spatial_binding(animal) || !scalar_context_binding(animal)) {
        return false;
    }
    const auto spatial = static_cast<Spatial*>(animal);
    // Haagos native run 54fdd617 / actors-28384, IDs 81-84: original nouns
    // with retained vitals/orientation but no species, herd, or render state.
    // Exclude only the fully disabled, unbound state. If the original engine
    // activates one, the next census must admit it normally or fail closed.
    return animal != Manager::Get()->GetAvatar() && !(animal->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar) &&
           !animal->mpSpeciesProfile && !animal->mHerd && !animal->field_E84 && !animal->mSpeciesKey.instanceID &&
           !animal->mSpeciesKey.typeID && !animal->mSpeciesKey.groupID && !animal->mGeneralFlags &&
           !animal->mpAnimatedCreature && spatial->mbFixed && !spatial->mbEnabled && !spatial->mpModel &&
           !spatial->mpModelWorld && spatial->mPosition.x == 0 && spatial->mPosition.y == 0 &&
           spatial->mPosition.z == 0;
}

const char* unsupported_reason(Animal* animal) {
    if (!animal) {
        return "animal_cast_missing";
    }
    if (!spatial_binding(animal)) {
        return "spatial_binding_mismatch";
    }
    if (!animal->mpSpeciesProfile) {
        return "species_profile_missing";
    }
    if (!animal->mHerd) {
        return "herd_missing";
    }
    if (!animal->field_E84) {
        return "native_archetype_data_missing";
    }
    // A retained pointer alone does not validate the herd lifetime.
    auto herd = animal->mHerd.get();
    for (auto& noun : Manager::Get()->mNouns) {
        if (&noun == static_cast<Noun*>(herd)) {
            if (noun.mbIsDestroyed || noun.field_20) {
                return "herd_unavailable";
            }
            return noun.GetNounID() == Simulator::cHerd::NOUN_ID ? nullptr : "herd_type_mismatch";
        }
    }
    return "herd_absent_from_census";
}

bool supported(Animal* animal) {
    return !unsupported_reason(animal);
}

bool scalar_context_binding(Animal* animal) {
    if (!animal) {
        return false;
    }
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
    if (!pool_lifecycle_bindings || !animal || !spatial_binding(animal) || !scalar_context_binding(animal)) {
        return false;
    }
    const auto spatial = static_cast<Spatial*>(animal);
    const auto address = reinterpret_cast<uintptr_t>(animal);
    if (reinterpret_cast<uintptr_t>(&animal->mHerd) - address != 0x1674 ||
        reinterpret_cast<uintptr_t>(&animal->mSpeciesKey) - address != 0xb28 ||
        reinterpret_cast<uintptr_t>(&spatial->mbFixed) - address != 0x131 ||
        reinterpret_cast<uintptr_t>(&spatial->mbEnabled) - address != 0x135 ||
        reinterpret_cast<uintptr_t>(&spatial->mpModel) - address != 0x15c ||
        reinterpret_cast<uintptr_t>(&spatial->mpModelWorld) - address != 0x160) {
        return false;
    }
    // Pinned ACCEC0 returns a retained noun to its original pool: it clears
    // the active slot, fixes/disables the spatial object, resets its transform
    // and render descriptor, increments E54, then C04700(0) releases its herd.
    // Native12 NPC75 retained its profile/animation/vitals in exactly this
    // unrendered state. Original Write C0BD80 also clears all three species
    // words for disabled, unrendered nouns (C0BE7B). Native .81 NPC97 was
    // already pooled before Save, then exposed exactly this cleared key.
    // A partial key, ordinary death or an enabled noun is still unsupported.
    const bool species_retained =
        animal->mSpeciesKey.instanceID && animal->mSpeciesKey.typeID && animal->mSpeciesKey.groupID;
    const bool species_cleared =
        !animal->mSpeciesKey.instanceID && !animal->mSpeciesKey.typeID && !animal->mSpeciesKey.groupID;
    return animal != Manager::Get()->GetAvatar() && !(animal->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar) &&
           animal->field_E54 != 0 && !animal->mHerd && animal->mpSpeciesProfile && animal->field_E84 &&
           (species_retained || species_cleared) && animal->mArchetype && animal->mpAnimatedCreature &&
           spatial->mbFixed && !spatial->mbEnabled && !spatial->mpModel && !spatial->mpModelWorld &&
           spatial->mPosition.x == 0 && spatial->mPosition.y == 0 && spatial->mPosition.z == 0;
}

void retire_incarnation(Seen& seen, const char* reason, uint32_t pool_cycle) {
    char fields[320]{};
    sprintf_s(fields,
              ",\"reason\":\"%s\",\"native_id\":%u,\"remote_entity\":%llu,\"entity_generation\":%llu,"
              "\"previous_pool_cycle\":%u,\"native_pool_cycle\":%u",
              reason, seen.native_id, (capture_epoch << 32) | seen.native_id, seen.generation, seen.pool_cycle,
              pool_cycle);
    native_actor_worker_event("scene_native_incarnation_retired", fields);
    seen = {};
}

bool readable_span(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto begin = reinterpret_cast<uintptr_t>(address), region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin >= region && size <= info.RegionSize && begin - region <= info.RegionSize - size;
}

bool scale_presentation_available(Animal* animal, float scale) {
    // C0E750 -> C0DB10 reads these bounded portions of the existing native
    // objects. Their references remain owned by this live noun on its engine
    // thread. No virtual calls are made through the animated pointer here.
    if (!readable_span(animal->mpAnimatedCreature.get(), 0x40) || !readable_span(animal->mpSpeciesProfile, 0x560)) {
        return false;
    }
    const auto& dimensions = animal->mpSpeciesProfile->mBoundingBox.lower;
    const float x = dimensions.x * scale, y = dimensions.y * scale, z = dimensions.z * scale;
    return std::isfinite(dimensions.x) && std::isfinite(dimensions.y) && std::isfinite(dimensions.z) &&
           std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(x * x + y * y + z * z);
}

// STATIC pinned dc04aee5: primary 146A080+7C -> C04170, ECX=animal, no stack arguments, x87 float return.
bool base_maximum_health_binding(Animal* animal) {
    const auto primary = *reinterpret_cast<const uintptr_t* const*>(animal);
    const std::array<unsigned char, 9> base_prefix{0x83, 0xec, 0x08, 0x0f, 0x57, 0xc0, 0x56, 0x8b, 0xf1};
    return reinterpret_cast<uintptr_t>(primary) == image + 0x106a080 && primary[0x7c / 4] == image + 0x804170 &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x804170), base_prefix.data(), base_prefix.size());
}

// STATIC pinned dc04aee5: effective max C05D50 receives ECX=animal+5A8 and calls the primary virtual of
// base_maximum_health_binding before applying age/flags.
bool effective_maximum_health_binding(Animal* animal) {
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    const auto table = *reinterpret_cast<const uintptr_t* const*>(combatant);
    return reinterpret_cast<uintptr_t>(combatant) - reinterpret_cast<uintptr_t>(animal) == 0x5a8 &&
           reinterpret_cast<uintptr_t>(table) == image + 0x1069e30 && table[0x58 / 4] == image + 0x805d50;
}

// Writes a finite value as a JSON number; `out` keeps what it held (its "null") otherwise.
void format_finite(char (&out)[48], float value) {
    if (std::isfinite(value)) {
        sprintf_s(out, "%.9g", double(value));
    }
}

// Whether the original's avatar is a present creature of the current noun census.
bool avatar_in_census(Animal* avatar) {
    if (!avatar) {
        return false;
    }
    for (auto& noun : Manager::Get()->mNouns) {
        if (&noun == static_cast<Noun*>(avatar) && present(noun)) {
            return true;
        }
    }
    return false;
}

void health_context(Animal* animal, char (&fields)[896]) {
    strcpy_s(fields, ",\"health_context\":{\"observed\":false}");
    if (!available() || !animal || !supported(animal)) {
        return;
    }
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    // These diagnostics never replace the independently enforced HP limit.
    const bool base_binding = base_maximum_health_binding(animal);
    const bool maximum_binding = base_binding && effective_maximum_health_binding(animal);
    char maximum[48] = "null", base[48] = "null", raw[48] = "null", species[48] = "null", herd[48] = "null",
         scale[48] = "null";
    if (maximum_binding) {
        format_finite(maximum, animal->GetMaxHitPoints());
    }
    if (base_binding) {
        format_finite(base, animal->GetBaseMaxHitPoints());
    }
    format_finite(raw, animal->mMaxHealthPoints);
    format_finite(species, animal->mpSpeciesProfile->mAdditionalHealth);
    // supported() confirmed this exact live herd in the noun census. Never
    // dereference the opaque archetype field_E84 just to enrich a diagnostic.
    format_finite(herd, animal->mHerd->mHitpointOverride);
    format_finite(scale, animal->GetScale());
    const auto stage = Simulator::cCreatureGameData::Get();
    char brain[24] = "null";
    if (stage) {
        sprintf_s(brain, "%d", stage->mCurrentBrainLevel);
    }
    const auto avatar = Manager::Get()->GetAvatar();
    const char* same_avatar_species =
        avatar_in_census(avatar) ? (animal->mpSpeciesProfile == avatar->mpSpeciesProfile ? "true" : "false") : "null";
    sprintf_s(fields,
              ",\"health_context\":{\"observed\":true,\"maximum_binding\":%s,\"base_binding\":%s,"
              "\"native_max\":%s,\"native_base_max\":%s,\"raw_max\":%s,\"age\":%d,\"flags\":%u,\"combatant_state\":%d,"
              "\"stage_brain\":%s,\"species_health\":%s,\"herd_health_override\":%s,\"herd_native_id\":%u,"
              "\"same_avatar_species\":%s,\"species\":[%u,%u,%u],\"native_archetype_present\":%s,\"native_scale\":%s}",
              maximum_binding ? "true" : "false", base_binding ? "true" : "false", maximum, base, raw, animal->mAge,
              unsigned(animal->mGeneralFlags), combatant->field_34, brain, species, herd, animal->mHerd->mID,
              same_avatar_species, animal->mSpeciesKey.instanceID, animal->mSpeciesKey.typeID,
              animal->mSpeciesKey.groupID, animal->field_E84 ? "true" : "false", scale);
}

void capture_rejection(Animal* animal, const char* reason) {
    auto spatial = static_cast<Spatial*>(animal);
    const auto table = *reinterpret_cast<const uintptr_t* const*>(spatial);
    uint32_t herd_id = UINT32_MAX;
    bool herd_in_census = false;
    if (animal->mHerd) {
        for (auto& noun : Manager::Get()->mNouns) {
            if (&noun == static_cast<Noun*>(animal->mHerd.get())) {
                herd_in_census = true;
                herd_id = noun.mID;
                break;
            }
        }
    }
    char fields[1536]{};
    sprintf_s(fields,
              ",\"reason\":\"%s\",\"native_id\":%u,\"spatial_offset\":%u,\"spatial_vtable_rva\":%u,"
              "\"get_position_rva\":%u,\"get_orientation_rva\":%u,\"set_position_rva\":%u,\"set_orientation_rva\":%u,"
              "\"species_profile_present\":%s,\"native_archetype_data_present\":%s,\"herd_present\":%s,\"herd_in_"
              "census\":%s,\"herd_native_id\":%u,"
              "\"species\":[%u,%u,%u],\"archetype\":%u,\"spatial_enabled\":%s,\"model_present\":%s,\"model_world_"
              "present\":%s,"
              "\"animated_creature_present\":%s,\"flags\":%d,\"position\":[%.9g,%.9g,%.9g],\"orientation\":[%.9g,%.9g,%"
              ".9g,%.9g],"
              "\"health\":%.9g,\"energy\":%.9g,\"hunger\":%.9g,\"spatial_fixed\":%s,\"native_pool_cycle\":%u",
              reason, animal->mID, unsigned(reinterpret_cast<uintptr_t>(spatial) - reinterpret_cast<uintptr_t>(animal)),
              unsigned(reinterpret_cast<uintptr_t>(table) - image), unsigned(table[0x2c / 4] - image),
              unsigned(table[0x30 / 4] - image), unsigned(table[0x38 / 4] - image), unsigned(table[0x3c / 4] - image),
              animal->mpSpeciesProfile ? "true" : "false", animal->field_E84 ? "true" : "false",
              animal->mHerd ? "true" : "false", herd_in_census ? "true" : "false", herd_id,
              animal->mSpeciesKey.instanceID, animal->mSpeciesKey.typeID, animal->mSpeciesKey.groupID,
              unsigned(animal->mArchetype), spatial->mbEnabled ? "true" : "false", spatial->mpModel ? "true" : "false",
              spatial->mpModelWorld ? "true" : "false", animal->mpAnimatedCreature ? "true" : "false",
              animal->mGeneralFlags, double(spatial->mPosition.x), double(spatial->mPosition.y),
              double(spatial->mPosition.z), double(spatial->mOrientation.x), double(spatial->mOrientation.y),
              double(spatial->mOrientation.z), double(spatial->mOrientation.w), double(animal->mHealthPoints),
              double(animal->mEnergy), double(animal->mHunger), spatial->mbFixed ? "true" : "false",
              static_cast<uint32_t>(animal->field_E54));
    native_actor_worker_event("scene_capture_rejected", fields);
}

bool finite(float value, float limit) {
    return std::isfinite(value) && std::abs(value) <= limit;
}

bool valid(const NativeSceneEntity& entity) {
    if (!entity.id || !entity.generation || entity.native_id == UINT32_MAX || entity.herd_native_id == UINT32_MAX ||
        entity.owner > 2) {
        return false;
    }
    if (entity.kind == EntityKind::animal &&
        (!entity.species_instance || !entity.species_type || !entity.species_group)) {
        return false;
    }
    for (auto coordinate : {entity.x, entity.y, entity.z}) {
        if (!finite(coordinate, 1e7f)) {
            return false;
        }
    }
    for (auto speed : {entity.vx, entity.vy, entity.vz}) {
        if (!finite(speed, 1e5f)) {
            return false;
        }
    }
    float length = 0;
    for (auto component : {entity.qx, entity.qy, entity.qz, entity.qw}) {
        if (!finite(component, 1.01f)) {
            return false;
        }
        length += component * component;
    }
    if (length < 0.98f || length > 1.02f) {
        return false;
    }
    if (!network::valid_entity(entity)) {
        return false;
    }
    // Native11 observed lethal damage reach zero health before mbDead became
    // true. Preserve that intermediate native state; valid_entity still checks
    // nonnegative health. Trial54 also observed an original reloaded NPC corpse
    // with positive health: retain both scalars rather than manufacture revival
    // or alter the original game. Owned dead actors still require zero health.
    return finite(entity.health, 1e6f) && finite(entity.energy, 1e6f) && finite(entity.hunger, 1e6f);
}

bool same_species(Animal* animal, const NativeSceneEntity& entity) {
    return supported(animal) && animal->mSpeciesKey.instanceID == entity.species_instance &&
           animal->mSpeciesKey.typeID == entity.species_type && animal->mSpeciesKey.groupID == entity.species_group &&
           static_cast<uint32_t>(animal->mArchetype) == entity.archetype;
}

bool same_fixture(Animal* animal, const NativeSceneEntity& entity) {
    return same_species(animal, entity) && animal->mHerd->mID == entity.herd_native_id;
}

Fossil* fossil(Noun& noun, bool require_collectible = true) {
    if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Fossil::NOUN_ID ||
        !readable_span(&noun, sizeof(Fossil))) {
        return nullptr;
    }
    auto result = static_cast<Fossil*>(&noun);
    const auto address = reinterpret_cast<uintptr_t>(result);
    auto spatial = static_cast<Spatial*>(result);
    // SDK cbf9206, cInteractableObject/cInteractiveOrnament/cSpatialObject.
    // The original D869B0 collection clears pickable at primary+A3 (spatial+6F).
    // Trial73 observes the same live type2 census and its original retirement.
    if (reinterpret_cast<uintptr_t>(spatial) - address != 0x34 ||
        reinterpret_cast<uintptr_t>(&result->mType) - address != 0x110 ||
        reinterpret_cast<uintptr_t>(&result->mpWhoIsInteractingWithMe) - address != 0x108 ||
        reinterpret_cast<uintptr_t>(&spatial->mbPickable) - address != 0xa3 ||
        result->mType != Simulator::kOrnamentBone ||
        (require_collectible && (!spatial->mbIsTangible || !spatial->mbPickable))) {
        return nullptr;
    }
    return result;
}

Fossil* find_fossil(uint32_t id, uintptr_t expected = 0, bool require_collectible = true) {
    if (!available() || id == UINT32_MAX) {
        return nullptr;
    }
    Fossil* result = nullptr;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mID == id) {
            auto candidate = fossil(noun, require_collectible);
            if (result || !candidate || (expected && reinterpret_cast<uintptr_t>(candidate) != expected)) {
                return nullptr;
            }
            result = candidate;
        }
    }
    return result;
}

// Why this client has no collectible fossil of that native ID, for the trace.
const char* fossil_absence(uint32_t id) {
    if (!available()) {
        return "scene_unavailable";
    }
    const char* reason = "noun_absent";
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mID == id) {
            if (noun.mbIsDestroyed || noun.field_20) {
                reason = "noun_destroyed";
                continue;
            }
            if (noun.GetNounID() != Fossil::NOUN_ID) {
                reason = "noun_of_another_kind";
                continue;
            }
            auto found = fossil(noun, false);
            if (!found) {
                return "not_a_bone_ornament";
            }
            auto spatial = static_cast<Spatial*>(found);
            return !spatial->mbIsTangible ? "fossil_not_tangible"
                   : !spatial->mbPickable ? "fossil_not_pickable"
                                          : "fossil_differs";
        }
    }
    return reason;
}

// Whether a native value is finite and within a thousandth of the authority's.
bool nearly_equal(float native, float wanted) {
    return std::isfinite(native) && std::abs(native - wanted) <= 0.001f;
}

// Whether a noun lies, is turned and is scaled as the entity says. An orientation with all four components negated
// is accepted as the same one.
bool same_placement(const Spatial& spatial, const NativeSceneEntity& entity) {
    const auto& position = spatial.mPosition;
    const auto& orientation = spatial.mOrientation;
    const bool same_orientation = nearly_equal(orientation.x, entity.qx) && nearly_equal(orientation.y, entity.qy) &&
                                  nearly_equal(orientation.z, entity.qz) && nearly_equal(orientation.w, entity.qw);
    const bool opposite_orientation =
        nearly_equal(-orientation.x, entity.qx) && nearly_equal(-orientation.y, entity.qy) &&
        nearly_equal(-orientation.z, entity.qz) && nearly_equal(-orientation.w, entity.qw);
    return nearly_equal(position.x, entity.x) && nearly_equal(position.y, entity.y) &&
           nearly_equal(position.z, entity.z) && (same_orientation || opposite_orientation) &&
           nearly_equal(spatial.mScale, entity.scale);
}

bool matches_model_key(const ResourceKey& key, const NativeSceneEntity& entity) {
    return key.instanceID == entity.model_instance && key.typeID == entity.model_type &&
           key.groupID == entity.model_group;
}

bool same_fossil(Fossil* value, const NativeSceneEntity& entity) {
    if (!value || entity.kind != EntityKind::fossil) {
        return false;
    }
    const auto spatial = static_cast<Spatial*>(value);
    // This increment admits an existing original static fossil in the same
    // checked native fixture. It does not invent a factory or move a resource
    // with an unverified spatial binding. Missing/changed templates fail closed.
    return matches_model_key(spatial->mModelKey, entity) && same_placement(*spatial, entity);
}

void fossil_sample(const char* name, Fossil* value, const NativeSceneEntity& entity) {
    const auto spatial = static_cast<Spatial*>(value);
    char fields[640]{};
    sprintf_s(fields,
              ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"source_tick\":%llu,"
              "\"model\":[%u,%u,%u],\"position\":[%.9g,%.9g,%.9g],\"tangible\":%s,\"claimed\":%s,\"owner\":0",
              entity.id, entity.generation, value->mID, entity.tick, entity.model_instance, entity.model_type,
              entity.model_group, double(spatial->mPosition.x), double(spatial->mPosition.y),
              double(spatial->mPosition.z), spatial->mbIsTangible ? "true" : "false",
              value->mpWhoIsInteractingWithMe ? "true" : "false");
    native_actor_worker_event(name, fields);
}

Herd* nest_herd(uint32_t native_id) {
    if (!native_id || native_id == UINT32_MAX || !Manager::Get()) {
        return nullptr;
    }
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mID == native_id) {
            if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Herd::NOUN_ID ||
                !readable_span(&noun, sizeof(Herd))) {
                return nullptr;
            }
            return static_cast<Herd*>(&noun);
        }
    }
    return nullptr;
}

Nest* nest(Noun& noun) {
    if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Nest::NOUN_ID ||
        !readable_span(&noun, sizeof(Nest))) {
        return nullptr;
    }
    auto value = static_cast<Nest*>(&noun);
    auto herd = value->mpHerd.get();
    if (!herd || !readable_span(herd, sizeof(Herd)) || nest_herd(herd->mID) != herd || herd->mpNest.get() != value) {
        return nullptr;
    }
    return value;
}

Nest* find_nest(uint32_t native_id, uintptr_t expected = 0) {
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mID == native_id) {
            auto value = nest(noun);
            return value && (!expected || reinterpret_cast<uintptr_t>(value) == expected) ? value : nullptr;
        }
    }
    return nullptr;
}

bool nest_herd_matches(Herd* herd, const NativeSceneEntity& entity) {
    return herd && herd->mID == entity.herd_native_id && herd->mOwnerSpeciesKey.instanceID == entity.species_instance &&
           herd->mOwnerSpeciesKey.typeID == entity.species_type &&
           herd->mOwnerSpeciesKey.groupID == entity.species_group && herd->mArchetype == entity.archetype;
}

bool same_nest_pose(Nest* value, const NativeSceneEntity& entity) {
    if (!value || entity.kind != EntityKind::nest || !nest_herd_matches(value->mpHerd.get(), entity)) {
        return false;
    }
    return same_placement(*static_cast<Spatial*>(value), entity);
}

bool same_nest(Nest* value, const NativeSceneEntity& entity) {
    if (!same_nest_pose(value, entity)) {
        return false;
    }
    return matches_model_key(static_cast<Spatial*>(value)->mModelKey, entity);
}

bool apply_nest_model(Nest* value, const NativeSceneEntity& entity) {
    if (same_nest(value, entity)) {
        return true;
    }
    if (!same_nest_pose(value, entity) || !native_replica_allows(replica::Mutation::apply_state)) {
        return false;
    }
    auto spatial = static_cast<Spatial*>(value);
    const auto table = *reinterpret_cast<const uintptr_t* const*>(spatial);
    // SDK cbf9206 cSpatialObject +94, pinned C889A0 thiscall(ResourceKey&),
    // RET4. Its sole virtual tail sets the model-change flag at spatial +73.
    // Project the authority's model identity; do not run nest/world simulation.
    constexpr unsigned char setter[] = {0x8b, 0x91, 0x90, 0, 0, 0, 0x8b, 0x44, 0x24, 4, 0x3b, 0x10};
    constexpr unsigned char flag[] = {0x8a, 0x44, 0x24, 4, 0x88, 0x41, 0x73, 0xc2, 4, 0};
    if (!readable_span(table, 0x98) || table[0x94 / 4] != image + 0x8889a0 || table[0x8c / 4] != image + 0x6d2710 ||
        std::memcmp(reinterpret_cast<void*>(image + 0x8889a0), setter, sizeof(setter)) ||
        std::memcmp(reinterpret_cast<void*>(image + 0x6d2710), flag, sizeof(flag))) {
        return false;
    }
    spatial->SetModelKey(ResourceKey(entity.model_instance, entity.model_type, entity.model_group));
    const bool applied = same_nest(value, entity);
    event("scene_nest_model_projected", applied ? "original_setter_readback" : "model_readback_failed", entity.id,
          value->mID);
    return applied;
}

Nest* nest_template(const NativeSceneEntity& entity) {
    auto herd = nest_herd(entity.herd_native_id);
    if (!nest_herd_matches(herd, entity) || !herd->mpNest) {
        return nullptr;
    }
    auto value = find_nest(herd->mpNest->mID, reinterpret_cast<uintptr_t>(herd->mpNest.get()));
    return same_nest(value, entity) ? value : nullptr;
}

bool nest_factory_binding() {
    // SDK cbf9206, B20DD0 thiscall(CreateNest(position reference, herd)), RET8.
    // Existing native trial39 establishes this factory. No simulation/reward
    // function is synthesized, and the primary avatar herd is never replaced.
    constexpr unsigned char prefix[] = {0x83, 0xec, 0x1c, 0x53, 0x56, 0x57, 0x68, 0x22,
                                        0x61, 0xaa, 0x52, 0xe8, 0x10, 0xfe, 0xff, 0xff};
    constexpr unsigned char tail[] = {0x8b, 0xc7, 0x5f, 0x5e, 0x5b, 0x83, 0xc4, 0x1c, 0xc2, 8, 0};
    return GetAddress(Simulator::cGameNounManager, CreateNest) == image + 0x720dd0 &&
           !std::memcmp(reinterpret_cast<void*>(image + 0x720dd0), prefix, sizeof(prefix)) &&
           !std::memcmp(reinterpret_cast<void*>(image + 0x720e63), tail, sizeof(tail));
}

void nest_sample(const char*, Nest*, const NativeSceneEntity&);

Nest* create_nest(const NativeSceneEntity& entity) {
    auto herd = nest_herd(entity.herd_native_id);
    if (!nest_factory_binding() || !nest_herd_matches(herd, entity) || herd->mpNest) {
        return nullptr;
    }
    const auto primary_herd = Manager::Get()->mpAvatarHerd;
    const auto primary_avatar = Manager::Get()->GetAvatar();
    const auto primary_player = Manager::Get()->GetPlayer();
    // Reuse the original herd input used by the authority. The factory projects
    // that input onto terrain; feeding its output back as an input is not an
    // identity transform. Validate the complete observed result afterward.
    const auto initial = herd->mInitialPosition;
    if (!std::isfinite(initial.x) || !std::isfinite(initial.y) || !std::isfinite(initial.z)) {
        return nullptr;
    }
    auto value = Manager::Get()->CreateNest(initial, herd);
    if (value && !value->mbIsDestroyed && value->mpHerd.get() == herd) {
        herd->mpNest = value;
    }
    if (value && find_nest(value->mID, reinterpret_cast<uintptr_t>(value)) == value) {
        nest_sample("scene_native_nest_factory_observed", value, entity);
    }
    if (!apply_nest_model(value, entity)) {
        return nullptr;
    }
    const bool primary_unchanged = Manager::Get()->mpAvatarHerd == primary_herd &&
                                   Manager::Get()->GetAvatar() == primary_avatar &&
                                   Manager::Get()->GetPlayer() == primary_player;
    if (!primary_unchanged || !same_nest(value, entity)) {
        event("scene_nest_factory_rejected", primary_unchanged ? "source_state_mismatch" : "primary_identity_changed",
              entity.id, value ? value->mID : UINT32_MAX);
        return nullptr;
    }
    return value;
}

void nest_sample(const char* name, Nest* value, const NativeSceneEntity& entity) {
    const auto spatial = static_cast<Spatial*>(value);
    const auto& orientation = spatial->mOrientation;
    const auto& key = spatial->mModelKey;
    char fields[1536]{};
    sprintf_s(
        fields,
        ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"herd_native_id\":%u,"
        "\"species\":[%u,%u,%u],\"position\":[%.9g,%.9g,%.9g],\"flags\":%d,\"native_links_exact\":true,"
        "\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"scale\":%.9g,\"model\":[%u,%u,%u],\"archetype\":%u,"
        "\"expected_position\":[%.9g,%.9g,%.9g],\"expected_orientation\":[%.9g,%.9g,%.9g,%.9g],"
        "\"expected_scale\":%.9g,\"expected_model\":[%u,%u,%u],\"expected_archetype\":%u,\"source_state_matches\":%s",
        entity.id, entity.generation, value->mID, value->mpHerd->mID, entity.species_instance, entity.species_type,
        entity.species_group, double(value->mPosition.x), double(value->mPosition.y), double(value->mPosition.z),
        value->mFlags, double(orientation.x), double(orientation.y), double(orientation.z), double(orientation.w),
        double(spatial->mScale), key.instanceID, key.typeID, key.groupID, value->mpHerd->mArchetype, double(entity.x),
        double(entity.y), double(entity.z), double(entity.qx), double(entity.qy), double(entity.qz), double(entity.qw),
        double(entity.scale), entity.model_instance, entity.model_type, entity.model_group, entity.archetype,
        same_nest(value, entity) ? "true" : "false");
    native_actor_worker_event(name, fields);
}

bool mapped(uint32_t id) {
    for (const auto& bound : bindings) {
        if (bound.id && bound.native_id == id) {
            return true;
        }
    }
    return false;
}

Binding* binding(uint64_t id, uint64_t generation) {
    for (auto& bound : bindings) {
        if (bound.id == id && bound.generation == generation) {
            return &bound;
        }
    }
    return nullptr;
}

Binding* empty_binding() {
    for (auto& bound : bindings) {
        if (!bound.id) {
            return &bound;
        }
    }
    return nullptr;
}

// A fresh record of the native noun that stands for an entity in this process. A noun of no player has owner 0.
Binding make_binding(const NativeSceneEntity& entity, uint32_t native_id, const void* noun, uint64_t owner,
                     EntityKind kind = EntityKind::animal) {
    Binding bound{entity.id, entity.generation, native_id, reinterpret_cast<uintptr_t>(noun), true, owner};
    bound.kind = kind;
    return bound;
}

Animal* template_for(const NativeSceneEntity& entity) {
    for (auto& noun : Manager::Get()->mNouns) {
        if (living(noun)) {
            auto candidate = object_cast<Animal>(&noun);
            if (same_species(candidate, entity)) {
                return candidate;
            }
        }
    }
    return nullptr;
}

bool herd_diagnostic_binding(Noun& noun) {
    if (!herd_diagnostic_bindings) {
        return false;
    }
    const auto table = *reinterpret_cast<const uintptr_t* const*>(&noun);
    // Pinned dc04aee5 PE: primary cHerd vtable 01471B90, Cast C69F50,
    // noun ID C6A110 and cast ID C6C0A0. Do not use an unchecked herd cast.
    return reinterpret_cast<uintptr_t>(table) == image + 0x1071b90 && readable_span(&noun, sizeof(Herd)) &&
           table[0] == image + 0x86ab00 && table[1] == image + 0x3b87b0 && table[0x0c / 4] == image + 0x869f50 &&
           table[0x20 / 4] == image + 0x86a110 && table[0x38 / 4] == image + 0x86c0a0;
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

// The noun as a herd, once its class table and the SDK's herd layout are verified; null otherwise.
Herd* verified_herd(Noun& noun) {
    if (!herd_diagnostic_binding(noun)) {
        return nullptr;
    }
    auto herd = object_cast<Herd>(&noun);
    return herd && static_cast<Noun*>(herd) == &noun && herd_diagnostic_layout(herd) ? herd : nullptr;
}

bool matches_species_key(const ResourceKey& key, const NativeSceneEntity& entity) {
    return key.instanceID == entity.species_instance && key.typeID == entity.species_type &&
           key.groupID == entity.species_group;
}

bool diagnostic_profile_readable(const void* profile, size_t size = 0x510) {
    // Checked fixed spans only, never unchecked SDK container iteration.
    // Readability is not a claim of profile ownership or retained lifetime.
    MEMORY_BASIC_INFORMATION info{};
    if (!readable_span(profile, size) || !VirtualQuery(profile, &info, sizeof(info))) {
        return false;
    }
    const auto protection = info.Protect & 0xff;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

void presentation_context(Animal* animal, char (&fields)[1024]) {
    // Read-only, main-thread observations of the current census member. SDK
    // cbf9206: Spatial +C0, AnimatedCreature position +4/model +180, Model
    // flags +4/transform +8. No presentation or culling operation is called.
    const auto spatial = static_cast<Spatial*>(animal);
    const auto animated = animal->mpAnimatedCreature.get();
    const bool animated_readable = diagnostic_profile_readable(animated, sizeof(*animated));
    const auto model = animated_readable ? animated->mpModel.get() : nullptr;
    const bool model_readable = diagnostic_profile_readable(model, sizeof(*model));
    char animated_position[128] = "null", model_position[128] = "null", model_scale[48] = "null", alpha[48] = "null";
    const auto position = [](char(&out)[128], const Math::Vector3& value) {
        if (std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z)) {
            sprintf_s(out, "[%.9g,%.9g,%.9g]", double(value.x), double(value.y), double(value.z));
        }
    };
    if (animated_readable) {
        position(animated_position, animated->mPosition);
    }
    if (model_readable) {
        position(model_position, model->mTransform.GetOffset());
        if (std::isfinite(model->mTransform.GetScale())) {
            sprintf_s(model_scale, "%.9g", double(model->mTransform.GetScale()));
        }
        if (std::isfinite(model->mColor.a)) {
            sprintf_s(alpha, "%.9g", double(model->mColor.a));
        }
    }
    sprintf_s(fields,
              ",\"presentation_context\":{\"spatial_enabled\":%s,\"spatial_in_view\":%s,"
              "\"spatial_fixed\":%s,\"spatial_model_present\":%s,\"spatial_world_present\":%s,"
              "\"animated_readable\":%s,\"animated_position\":%s,\"model_readable\":%s,"
              "\"same_spatial_model\":%s,\"model_world_present\":%s,\"model_flags\":%u,"
              "\"model_position\":%s,\"model_scale\":%s,\"model_alpha\":%s}",
              spatial->mbEnabled ? "true" : "false", spatial->mbInView ? "true" : "false",
              spatial->mbFixed ? "true" : "false", spatial->mpModel ? "true" : "false",
              spatial->mpModelWorld ? "true" : "false", animated_readable ? "true" : "false", animated_position,
              model_readable ? "true" : "false", model && model == spatial->mpModel.get() ? "true" : "false",
              model_readable && model->mpWorld ? "true" : "false", model_readable ? unsigned(model->mFlags) : 0,
              model_position, model_scale, alpha);
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
    if (!diagnostic_profile_readable(reinterpret_cast<void*>(map), 0x20)) {
        result.reason = "map_header_unreadable";
        return result;
    }
    const uintptr_t buckets = native_word(map + 4);
    result.buckets = native_word(map + 8);
    if (!result.buckets || result.buckets > bucket_limit ||
        !diagnostic_profile_readable(reinterpret_cast<void*>(buckets), (size_t(result.buckets) + 1) * 4)) {
        result.reason = "map_bucket_span";
        return result;
    }
    const uint32_t hash = words == 3 ? key[0] ^ key[2] : key[0];
    uintptr_t node = native_word(buckets + (hash % result.buckets) * 4);
    std::array<uintptr_t, chain_limit> visited{};
    while (node) {
        if (result.nodes == chain_limit) {
            result.reason = "map_chain_limit";
            return result;
        }
        for (uint32_t i = 0; i < result.nodes; ++i) {
            if (visited[i] == node) {
                result.reason = "map_chain_cycle";
                return result;
            }
        }
        if (!diagnostic_profile_readable(reinterpret_cast<void*>(node), next_offset + 4)) {
            result.reason = "map_node_unreadable";
            return result;
        }
        visited[result.nodes++] = node;
        bool matches = true;
        for (size_t i = 0; i < words; ++i) {
            if (native_word(node + i * 4) != key[i]) {
                matches = false;
            }
        }
        if (matches) {
            ++result.matches;
            result.node = node;
        }
        node = native_word(node + next_offset);
    }
    result.complete = true;
    result.reason = result.matches == 1 ? "unique_key" : result.matches ? "duplicate_key" : "key_absent";
    return result;
}

struct HerdProfileRead {
    Simulator::cSpeciesProfile* profile = nullptr;
    uintptr_t archetype = 0;
    bool profile_qualified = false;
    bool qualified = false;
};

// The species manager's archetype map, at +B0h: one key word, the next node of a bucket at +444h.
MapRead inspect_archetype_map(uintptr_t species_manager, uint32_t key) {
    return inspect_native_map(species_manager + 0xb0, &key, 1, 0x444);
}

// What the species manager's own species map holds for the profile of a herd, and how far that was established.
struct ProfileOwnership {
    // The herd is a verified herd noun with neither mbIsDestroyed nor field_20 set.
    bool live = false;
    Editors::cSpeciesManager* manager = nullptr;
    uintptr_t manager_address = 0;
    bool manager_binding = false;
    MapRead species;
    // The map holds exactly one entry for the entity's species key; `profile` is that entry's.
    bool unique = false;
    Simulator::cSpeciesProfile* profile = nullptr;
    bool same_profile = false, full_profile = false, profile_key = false, sequence = false;
    bool owned = false, owner_key = false;
    const char* reason = nullptr;
};

ProfileOwnership read_profile_ownership(const NativeSceneEntity& entity, Herd* herd) {
    ProfileOwnership read;
    read.live = herd && herd_diagnostic_binding(*herd) && herd_diagnostic_layout(herd) && !herd->mbIsDestroyed &&
                !herd->field_20;
    // Get() is only the checked singleton load, never GetSpeciesProfile (which
    // loads/allocates), nor 4DF740 (which initializes a missing creatureSeq).
    read.manager = herd_profile_bindings && on_thread() ? Editors::cSpeciesManager::Get() : nullptr;
    read.manager_address = reinterpret_cast<uintptr_t>(read.manager);
    read.manager_binding = read.manager && diagnostic_profile_readable(read.manager, 0xe4) &&
                           native_word(read.manager_address) == image + 0xff08c8 &&
                           native_word(image + 0xff08c8) == image + 0xdb410 &&
                           native_word(image + 0xff08cc) == image + 0xdcf40;
    const uint32_t key[]{entity.species_instance, entity.species_type, entity.species_group};
    if (read.manager_binding && read.live) {
        read.species = inspect_native_map(read.manager_address + 4, key, 3, 0x10);
    }
    read.unique = read.species.complete && read.species.matches == 1;
    read.profile =
        read.unique ? reinterpret_cast<Simulator::cSpeciesProfile*>(native_word(read.species.node + 0xc)) : nullptr;
    const auto profile = read.profile;
    read.same_profile = read.live && profile && profile == herd->mpSpeciesProfile;
    read.full_profile =
        read.same_profile && diagnostic_profile_readable(profile, sizeof(*profile)) &&
        reinterpret_cast<uintptr_t>(&profile->mCreatureKey) - reinterpret_cast<uintptr_t>(profile) == 0x504 &&
        reinterpret_cast<uintptr_t>(&profile->creatureSeq) - reinterpret_cast<uintptr_t>(profile) == 0x53c;
    read.profile_key = read.full_profile && matches_species_key(profile->mCreatureKey, entity);
    read.sequence = read.full_profile && profile->creatureSeq != 0;
    read.owned = read.manager_binding && read.unique && read.same_profile;
    read.owner_key = read.live && matches_species_key(herd->mOwnerSpeciesKey, entity);
    read.reason = !herd_profile_bindings                 ? "binding_mismatch"
                  : !read.live                           ? "herd_not_live"
                  : !read.manager_binding                ? "manager_binding"
                  : !read.unique                         ? read.species.reason
                  : !read.same_profile                   ? "profile_owner_mismatch"
                  : !read.full_profile                   ? "profile_unreadable"
                  : !read.profile_key || !read.owner_key ? "species_key_mismatch"
                  : !read.sequence                       ? "creature_sequence_missing"
                                                         : "current_manager_owned_profile";
    return read;
}

void report_profile_ownership(const NativeSceneEntity& entity, Herd* herd, const char* phase,
                              const ProfileOwnership& read) {
    char fields[1280]{};
    sprintf_s(fields,
              ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_tick\":%llu,"
              "\"candidate_native_id\":%u,\"bindings_qualified\":%s,\"manager_present\":%s,\"manager_vtable_match\":%s,"
              "\"species_bucket_count\":%u,\"species_chain_nodes\":%u,\"species_chain_complete\":%s,\"species_key_"
              "matches\":%u,"
              "\"map_profile_present\":%s,\"map_profile_matches_herd\":%s,\"profile_full_readable\":%s,\"profile_key_"
              "matches\":%s,"
              "\"creature_sequence_present\":%s,\"profile_owned_now\":%s,\"reason\":\"%s\"",
              phase, entity.id, entity.generation, entity.tick, herd ? herd->mID : UINT32_MAX,
              herd_profile_bindings ? "true" : "false", read.manager ? "true" : "false",
              read.manager_binding ? "true" : "false", read.species.buckets, read.species.nodes,
              read.species.complete ? "true" : "false", read.species.matches, read.profile ? "true" : "false",
              read.same_profile ? "true" : "false", read.full_profile ? "true" : "false",
              read.profile_key ? "true" : "false", read.sequence ? "true" : "false", read.owned ? "true" : "false",
              read.reason);
    native_actor_worker_event("scene_herd_profile_owner", fields);
}

// The archetype entry the species manager holds for a herd: the entry of the herd's raw archetype, or the one that
// entry lists for the herd's generation.
struct ArchetypeRead {
    MapRead base, variant;
    uintptr_t entry = 0;
    uint32_t generation_count = 0, generation_index = UINT32_MAX, variant_key = 0;
    bool generation_observed = false, variant_attempted = false;
    // An entry was established; `animal_archetype` is its word at +10h, which the entity's archetype is compared with.
    bool resolved = false;
    uint32_t animal_archetype = 0;
    bool matches_wire = false;
    const char* reason = nullptr;
};

// For a herd of a later generation: the entry the base entry's generation vector lists for it, where it lists one.
void resolve_generation_entry(uintptr_t species_manager, Herd* herd, ArchetypeRead& read) {
    const uintptr_t begin = native_word(read.entry + 0x3c4), end = native_word(read.entry + 0x3c8);
    if (end < begin || (end - begin) % 4 || (end - begin) / 4 > 1024 ||
        (end != begin && !diagnostic_profile_readable(reinterpret_cast<void*>(begin), end - begin))) {
        read.resolved = false;
        read.reason = "generation_vector_span";
        return;
    }
    read.generation_observed = true;
    read.generation_count = static_cast<uint32_t>((end - begin) / 4);
    read.generation_index = static_cast<uint32_t>(herd->mGeneration) - 1;
    if (read.generation_index >= read.generation_count) {
        return;
    }
    read.variant_attempted = true;
    read.variant_key = native_word(begin + read.generation_index * 4);
    read.variant = inspect_archetype_map(species_manager, read.variant_key);
    if (!read.variant.complete || read.variant.matches > 1) {
        read.resolved = false;
        read.reason = read.variant.reason;
    } else if (read.variant.matches == 1) {
        read.entry = read.variant.node + 4;
        read.reason = "generation_entry";
    }
}

void resolve_archetype_entry(uintptr_t species_manager, Herd* herd, ArchetypeRead& read) {
    read.base = inspect_archetype_map(species_manager, herd->mArchetype);
    if (!read.base.complete || read.base.matches != 1) {
        read.reason = read.base.reason;
        return;
    }
    read.entry = read.base.node + 4;
    read.resolved = true;
    read.reason = "base_entry";
    if (herd->mGeneration != 0) {
        resolve_generation_entry(species_manager, herd, read);
    }
}

ArchetypeRead resolve_herd_archetype(const NativeSceneEntity& entity, Herd* herd, const ProfileOwnership& ownership) {
    ArchetypeRead read;
    read.reason = ownership.live && herd->mGeneration < 0 ? "negative_herd_generation" : "archetype_unobserved";
    // The observed fixture is generation zero. Reject a negative SDK value;
    // do not qualify an unknown lifecycle state via unsigned index fallback.
    if (ownership.manager_binding && ownership.live && herd->mGeneration >= 0) {
        resolve_archetype_entry(ownership.manager_address, herd, read);
    }
    read.animal_archetype = read.resolved ? native_word(read.entry + 0x10) : 0;
    read.matches_wire = read.resolved && read.animal_archetype == entity.archetype;
    return read;
}

void report_herd_archetype(const NativeSceneEntity& entity, Herd* herd, const char* phase, Animal* source, bool live,
                           const ArchetypeRead& read) {
    char fields[1280]{};
    sprintf_s(
        fields,
        ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_tick\":%llu,"
        "\"candidate_native_id\":%u,\"raw_herd_archetype\":%u,\"herd_generation\":%d,"
        "\"base_chain_nodes\":%u,\"base_chain_complete\":%s,\"base_key_matches\":%u,"
        "\"generation_vector_observed\":%s,\"generation_count\":%u,\"generation_index\":%u,\"variant_attempted\":%"
        "s,\"variant_key\":%u,"
        "\"variant_chain_nodes\":%u,\"variant_chain_complete\":%s,\"variant_key_matches\":%u,"
        "\"resolution_observed\":%s,\"resolved_animal_archetype\":%u,\"matches_wire_archetype\":%s,"
        "\"authority_entry_matches\":%s,\"reason\":\"%s\"",
        phase, entity.id, entity.generation, entity.tick, herd ? herd->mID : UINT32_MAX, live ? herd->mArchetype : 0,
        live ? herd->mGeneration : 0, read.base.nodes, read.base.complete ? "true" : "false", read.base.matches,
        read.generation_observed ? "true" : "false", read.generation_count, read.generation_index,
        read.variant_attempted ? "true" : "false", read.variant_key, read.variant.nodes,
        read.variant.complete ? "true" : "false", read.variant.matches, read.resolved ? "true" : "false",
        read.animal_archetype, read.matches_wire ? "true" : "false",
        source && read.resolved ? (reinterpret_cast<uintptr_t>(source->field_E84) == read.entry ? "true" : "false")
                                : "null",
        read.reason);
    native_actor_worker_event("scene_herd_archetype", fields);
}

// Whether the species manager owns the profile a herd holds and resolves the herd's archetype to the entity's.
// Records both findings unless `diagnostics` is false.
HerdProfileRead inspect_herd_profile(const NativeSceneEntity& entity, Herd* herd, const char* phase,
                                     Animal* source = nullptr, bool diagnostics = true) {
    const ProfileOwnership ownership = read_profile_ownership(entity, herd);
    if (diagnostics) {
        report_profile_ownership(entity, herd, phase, ownership);
    }
    const ArchetypeRead archetype = resolve_herd_archetype(entity, herd, ownership);
    if (diagnostics) {
        report_herd_archetype(entity, herd, phase, source, ownership.live, archetype);
    }
    HerdProfileRead result;
    result.profile_qualified =
        ownership.owned && ownership.full_profile && ownership.profile_key && ownership.sequence && ownership.owner_key;
    result.qualified = result.profile_qualified && archetype.matches_wire;
    if (result.qualified) {
        result.profile = ownership.profile;
        result.archetype = archetype.entry;
    }
    return result;
}

Herd* existing_herd_for(const NativeSceneEntity& entity, const char* phase, bool diagnostics) {
    constexpr size_t noun_limit = 16384;
    if (!available() || !native_replica_is_client() || !herd_profile_bindings || entity.owner) {
        return nullptr;
    }
    Herd* candidate = nullptr;
    size_t inspected = 0, matches = 0;
    bool complete = true;
    for (auto& noun : Manager::Get()->mNouns) {
        if (inspected++ == noun_limit) {
            complete = false;
            break;
        }
        if (noun.mID != entity.herd_native_id) {
            continue;
        }
        ++matches;
        auto herd = verified_herd(noun);
        if (herd && !herd->mbIsDestroyed && !herd->field_20) {
            candidate = herd;
        }
    }
    const auto profile = complete && matches == 1 && candidate
                             ? inspect_herd_profile(entity, candidate, phase, nullptr, diagnostics)
                             : HerdProfileRead{};
    if (diagnostics) {
        char fields[640]{};
        sprintf_s(fields,
                  ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_herd\":%u,"
                  "\"census_complete\":%s,\"nouns_inspected\":%zu,\"noun_limit\":16384,\"exact_id_matches\":%zu,"
                  "\"qualified\":%s,\"herd_enabled_observed\":%s,\"herd_enabled_byte\":%u,\"herd_enable_called\":false",
                  phase, entity.id, entity.generation, entity.herd_native_id, complete ? "true" : "false", inspected,
                  matches, profile.qualified ? "true" : "false", candidate ? "true" : "false",
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
    const bool profile_readable =
        profile && diagnostic_profile_readable(profile) &&
        reinterpret_cast<uintptr_t>(&profile->mCreatureKey) - reinterpret_cast<uintptr_t>(profile) == 0x504;
    const ResourceKey key = profile_readable ? profile->mCreatureKey : ResourceKey();
    const ResourceKey owner_key = live ? herd->mOwnerSpeciesKey : ResourceKey();
    char fields[1536]{};
    sprintf_s(fields,
              ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
              "\"source_tick\":%llu,\"source_herd\":%u,\"source_species\":[%u,%u,%u],\"source_animal_archetype\":%u,"
              "\"candidate_native_id\":%u,\"exact_herd_id\":%s,\"vtable_binding\":%s,\"sdk_layout\":%s,"
              "\"destroyed\":%s,\"field_20\":%s,\"live_manager_herd\":%s,\"herd_scalars_observed\":%s,"
              "\"herd_archetype\":%u,\"herd_archetype_group\":%u,\"herd_generation\":%d,\"herd_enabled_byte\":%u,"
              "\"owner_species_key\":[%u,%u,%u],\"owner_key_matches\":%s,\"profile_present\":%s,\"profile_header_"
              "readable\":%s,"
              "\"profile_key\":[%u,%u,%u],\"profile_key_matches\":%s,\"authority_animal_observed\":%s,"
              "\"authority_herd_reference_matches\":%s,\"authority_profile_reference_matches\":%s,"
              "\"lifetime_basis\":\"current_engine_thread_manager_census\",\"profile_ownership_proven\":false,"
              "\"selection_performed\":false",
              phase, entity.id, entity.generation, entity.native_id, entity.tick, entity.herd_native_id,
              entity.species_instance, entity.species_type, entity.species_group, entity.archetype, noun.mID,
              noun.mID == entity.herd_native_id ? "true" : "false", binding_ok ? "true" : "false",
              layout_ok ? "true" : "false", noun.mbIsDestroyed ? "true" : "false", noun.field_20 ? "true" : "false",
              live ? "true" : "false", live ? "true" : "false", live ? herd->mArchetype : 0,
              live ? herd->mArchetypeGroup : 0, live ? herd->mGeneration : 0,
              live ? unsigned(*reinterpret_cast<const unsigned char*>(&herd->mbEnabled)) : 0, owner_key.instanceID,
              owner_key.typeID, owner_key.groupID, live && matches_species_key(owner_key, entity) ? "true" : "false",
              profile ? "true" : "false", profile_readable ? "true" : "false", key.instanceID, key.typeID, key.groupID,
              profile_readable && matches_species_key(key, entity) ? "true" : "false", source ? "true" : "false",
              source && live && source->mHerd.get() == herd ? "true" : "false",
              source && profile && source->mpSpeciesProfile == profile ? "true" : "false");
    native_actor_worker_event("scene_herd_candidate", fields);
    if (live) {
        inspect_herd_profile(entity, herd, phase, source);
    }
}

void herd_census_diagnostic(const NativeSceneEntity& entity, const char* phase, Animal* source = nullptr) {
    constexpr size_t noun_limit = 16384, alternative_limit = 4, exact_limit = 4;
    size_t inspected = 0, herd_count = 0, id_matches = 0, qualified_id_matches = 0, owner_key_matches = 0;
    size_t exact_logged = 0, alternatives_logged = 0;
    bool truncated = false;
    // Fresh census only: no stored native pointer, native AddRef, factory,
    // profile lookup/loading, herd enabling or gameplay callback is introduced.
    for (auto& noun : Manager::Get()->mNouns) {
        if (inspected == noun_limit) {
            truncated = true;
            break;
        }
        ++inspected;
        const bool exact = noun.mID == entity.herd_native_id;
        if (exact) {
            ++id_matches;
        }
        bool owner_match = false;
        if (auto herd = verified_herd(noun)) {
            ++herd_count;
            if (!noun.mbIsDestroyed && !noun.field_20) {
                if (exact) {
                    ++qualified_id_matches;
                }
                owner_match = matches_species_key(herd->mOwnerSpeciesKey, entity);
                if (owner_match) {
                    ++owner_key_matches;
                }
            }
        }
        if (exact && exact_logged < exact_limit) {
            herd_candidate_diagnostic(entity, phase, noun, source);
            ++exact_logged;
        } else if (!exact && owner_match && alternatives_logged < alternative_limit) {
            herd_candidate_diagnostic(entity, phase, noun, source);
            ++alternatives_logged;
        }
    }
    char fields[768]{};
    sprintf_s(
        fields,
        ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
        "\"source_tick\":%llu,\"source_herd\":%u,\"bindings_qualified\":%s,\"nouns_inspected\":%zu,\"noun_limit\":%zu,"
        "\"census_complete\":%s,\"herds_with_verified_layout\":%zu,\"exact_id_matches\":%zu,\"live_exact_herds\":%zu,"
        "\"owner_key_matches\":%zu,\"exact_candidates_logged\":%zu,\"alternatives_logged\":%zu,\"selection_performed\":"
        "false",
        phase, entity.id, entity.generation, entity.native_id, entity.tick, entity.herd_native_id,
        herd_diagnostic_bindings ? "true" : "false", inspected, noun_limit, truncated ? "false" : "true", herd_count,
        id_matches, qualified_id_matches, owner_key_matches, exact_logged, alternatives_logged);
    native_actor_worker_event("scene_herd_census", fields);
}

void spawn_template_diagnostic(const NativeSceneEntity& entity, Animal* selected, const char* phase) {
    // Read-only, engine-thread diagnostics at a spawn boundary. Missing-template
    // counts are capped and do not select, retain or enable an alternative noun.
    size_t inspected = 0, present_count = 0, living_count = 0, species_count = 0;
    size_t archetype_count = 0, eligible_count = 0;
    bool truncated = false;
    if (!selected) {
        for (auto& noun : Manager::Get()->mNouns) {
            if (inspected == 4096) {
                truncated = true;
                break;
            }
            ++inspected;
            if (!present(noun)) {
                continue;
            }
            ++present_count;
            auto animal = object_cast<Animal>(&noun);
            if (!animal->mbDead) {
                ++living_count;
            }
            if (animal->mSpeciesKey.instanceID != entity.species_instance ||
                animal->mSpeciesKey.typeID != entity.species_type ||
                animal->mSpeciesKey.groupID != entity.species_group) {
                continue;
            }
            ++species_count;
            if (static_cast<uint32_t>(animal->mArchetype) != entity.archetype) {
                continue;
            }
            ++archetype_count;
            if (!animal->mbDead && supported(animal)) {
                ++eligible_count;
            }
        }
    }
    char fields[1024]{};
    sprintf_s(fields,
              ",\"phase\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
              "\"source_species\":[%u,%u,%u],\"source_archetype\":%u,\"source_herd\":%u,"
              "\"template_selected\":%s,\"template_native_id\":%u,\"template_herd\":%u,"
              "\"census_observed\":%s,\"census_limit\":4096,\"census_truncated\":%s,\"nouns_inspected\":%zu,"
              "\"present_animals\":%zu,\"living_animals\":%zu,\"same_species_key\":%zu,\"same_archetype\":%zu,"
              "\"eligible_templates\":%zu",
              phase, entity.id, entity.generation, entity.native_id, entity.species_instance, entity.species_type,
              entity.species_group, entity.archetype, entity.herd_native_id, selected ? "true" : "false",
              selected ? selected->mID : UINT32_MAX, selected ? selected->mHerd->mID : UINT32_MAX,
              selected ? "false" : "true", truncated ? "true" : "false", inspected, present_count, living_count,
              species_count, archetype_count, eligible_count);
    native_actor_worker_event("scene_spawn_template", fields);
    if (!selected) {
        herd_census_diagnostic(entity, phase);
    }
}

void spawn_factory_diagnostic(const NativeSceneEntity& entity, const char* outcome, uint32_t local_id = UINT32_MAX) {
    char fields[384]{};
    sprintf_s(fields,
              ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
              "\"factory_outcome\":\"%s\",\"native_id\":%u",
              entity.id, entity.generation, entity.native_id, outcome, local_id);
    native_actor_worker_event("scene_spawn_factory", fields);
}

bool projection_rejected(Animal* animal, const NativeSceneEntity& entity, const char* reason, float maximum = -1,
                         bool context_reverted = false) {
    char health[896]{};
    health_context(animal, health);
    char fields[1536]{};
    sprintf_s(fields,
              ",\"reason\":\"%s\",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,"
              "\"source_tick\":%llu,\"source_health\":%.9g,\"source_life_state\":%u,\"local_health\":%.9g,\"local_max_"
              "health\":%.9g,\"local_dead\":%s,"
              "\"source_age\":%u,\"source_alpha\":%u,\"source_combatant_state\":%u,\"source_scale\":%.9g,\"native_"
              "context_reverted\":%s%s",
              reason, entity.id, entity.generation, animal ? animal->mID : UINT32_MAX, entity.tick,
              double(entity.health), entity.life_state, animal ? double(animal->mHealthPoints) : -1.0, double(maximum),
              animal && animal->mbDead ? "true" : "false", entity.age, entity.alpha, entity.combatant_state,
              double(entity.scale), context_reverted ? "true" : "false", health);
    native_actor_worker_event("scene_projection_rejected", fields);
    return false;
}

constexpr int alpha_mask = Simulator::kCreatureFlagIsAlpha;
static_assert(alpha_mask == 1);

// The scalar context a creature held before a pose was applied: kept to undo a rejected pose and to tell what a
// pose changed.
struct CreatureContext {
    int age = 0, flags = 0, combatant_state = 0;
    float scale = 0;
};

CreatureContext read_context(Animal* animal) {
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    CreatureContext context;
    context.age = animal->mAge;
    context.flags = animal->mGeneralFlags;
    context.combatant_state = combatant->field_34;
    context.scale = animal->GetScale();
    return context;
}

// The flags a pose leaves on a creature: the authority's alpha bit, every other bit as it was.
int flags_with_alpha(int flags, const NativeSceneEntity& entity) {
    return (flags & ~alpha_mask) | (entity.alpha ? alpha_mask : 0);
}

// Apply only the authority's qualified scalar context. The local avatar
// bit and every flag other than alpha remain local. Do not call GrowUp,
// Heal or death/revive callbacks: they execute original gameplay effects.
void stage_context(Animal* animal, const NativeSceneEntity& entity, const CreatureContext& old) {
    animal->mAge = static_cast<int>(entity.age);
    animal->mGeneralFlags = flags_with_alpha(old.flags, entity);
    static_cast<Simulator::cCombatant*>(animal)->field_34 = static_cast<int>(entity.combatant_state);
}

// Puts back the age, flags and combatant state; a rejected pose has not changed the scale yet.
void restore_context(Animal* animal, const CreatureContext& old) {
    animal->mAge = old.age;
    animal->mGeneralFlags = old.flags;
    static_cast<Simulator::cCombatant*>(animal)->field_34 = old.combatant_state;
}

// Replica values update original model/physics presentation through the
// actual Creature override. Worker movement still uses native WalkTo.
void move_creature(Animal* animal, const NativeSceneEntity& entity, const Math::Vector3& position,
                   const Math::Quaternion& orientation, bool relocate_model) {
    const auto& previous_position = animal->GetPosition();
    const auto& previous_orientation = animal->GetOrientation();
    const bool controlled_pose_changed =
        entity.owner == controlled_owner && animal == Manager::Get()->GetAvatar() &&
        (position.x != previous_position.x || position.y != previous_position.y || position.z != previous_position.z ||
         orientation.x != previous_orientation.x || orientation.y != previous_orientation.y ||
         orientation.z != previous_orientation.z || orientation.w != previous_orientation.w);
    if (relocate_model || controlled_pose_changed) {
        // SetPosition alone leaves the controlled avatar's animation root at
        // its previous location (trials81/82), unlike the NPC presentation path.
        // Original C0C040 -> C888D0 also marks mbTeleport; original view E8C100
        // consumes it through A04710 to relocate the model's internal root.
        // Apply only admitted authority poses. No local gameplay AI is run.
        animal->Teleport(position, orientation);
        if (relocate_model) {
            actor_sample("scene_native_model_relocated", animal, entity);
        }
    } else {
        animal->SetPosition(position);
        animal->SetOrientation(orientation);
    }
}

// The authority's velocity, health, life state, nutrition, energy and hunger, and its DNA for this game's own player.
void apply_vitals(Animal* animal, const NativeSceneEntity& entity) {
    // The pinned SDK's SetVelocity implementation is this exact scalar copy;
    // animation choice remains original cCreatureBase::Update behavior.
    animal->mVelocity = Math::Vector3(entity.vx, entity.vy, entity.vz);
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
    if (entity.owner && entity.owner == controlled_owner) {
        Simulator::cCreatureGameData::SetEvolutionPoints(entity.dna);
    }
}

// Rereads what a pose wrote. Returns the reason of the first value the original does not hold, or null.
const char* pose_readback_failure(Animal* animal, const NativeSceneEntity& entity, const CreatureContext& old,
                                  const Math::Vector3& position, const Math::Quaternion& orientation) {
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    const auto& applied = animal->GetPosition();
    const auto& rotated = animal->GetOrientation();
    if (applied.x != position.x || applied.y != position.y || applied.z != position.z) {
        return "position_readback";
    }
    if (rotated.x != orientation.x || rotated.y != orientation.y || rotated.z != orientation.z ||
        rotated.w != orientation.w) {
        return "orientation_readback";
    }
    if (animal->mAge != static_cast<int>(entity.age) || animal->mGeneralFlags != flags_with_alpha(old.flags, entity) ||
        combatant->field_34 != static_cast<int>(entity.combatant_state) || animal->mHealthPoints != entity.health ||
        animal->mbDead != (entity.life_state == 1) || animal->mbHasBeenEaten != (entity.fed_on == 1) ||
        animal->mFoodValue != entity.food) {
        return "native_context_readback";
    }
    if (animal->GetScale() != entity.scale) {
        return "native_scale_readback";
    }
    return nullptr;
}

bool apply_pose(Animal* animal, const NativeSceneEntity& entity, bool relocate_model) {
    if (!same_species(animal, entity)) {
        return projection_rejected(animal, entity, "species_or_native_binding");
    }
    if (!valid(entity)) {
        return projection_rejected(animal, entity, "invalid_authoritative_values");
    }
    if (!native_replica_allows(replica::Mutation::apply_state)) {
        return projection_rejected(animal, entity, "application_scope");
    }
    if (!scalar_context_binding(animal)) {
        return projection_rejected(animal, entity, "native_context_binding");
    }
    const auto combatant = static_cast<Simulator::cCombatant*>(animal);
    const CreatureContext old = read_context(animal);
    if (!std::isfinite(old.scale) || old.scale <= 0 || old.scale > 1000) {
        return projection_rejected(animal, entity, "unsupported_native_scale");
    }
    stage_context(animal, entity, old);
    const auto maximum = animal->GetMaxHitPoints();
    if (!std::isfinite(maximum) || maximum <= 0 || entity.health > maximum) {
        restore_context(animal, old);
        // local_max_health records the rejected staged-context calculation;
        // health_context independently rereads the restored native values.
        return projection_rejected(animal, entity, "maximum_health_mismatch", maximum, true);
    }
    if (old.scale != entity.scale) {
        // C0E750 -> C0DB10 requires both pointers even though the setter's
        // first animation write is conditional. Supported live native objects
        // retain their SDK intrusive animation reference on this engine thread.
        if (!scale_presentation_available(animal, entity.scale)) {
            restore_context(animal, old);
            return projection_rejected(animal, entity, "scale_presentation_unavailable", maximum, true);
        }
        // Use the exact authority observation. CalculateScale consumes native
        // RNG/global-avatar context; GrowUp also changes gameplay. SetScale
        // updates existing model scale/bounds, not baby/adult model selection.
        animal->SetScale(entity.scale);
    }
    const Math::Vector3 position(entity.x, entity.y, entity.z);
    const Math::Quaternion orientation(entity.qx, entity.qy, entity.qz, entity.qw);
    move_creature(animal, entity, position, orientation, relocate_model);
    const bool was_dead = animal->mbDead;
    apply_vitals(animal, entity);
    if (const char* failure = pose_readback_failure(animal, entity, old, position, orientation)) {
        return projection_rejected(animal, entity, failure, maximum);
    }
    if (was_dead != animal->mbDead) {
        actor_sample("scene_native_life_state_applied", animal, entity);
    }
    if (old.age != animal->mAge || old.flags != animal->mGeneralFlags || old.combatant_state != combatant->field_34 ||
        old.scale != entity.scale) {
        actor_sample("scene_native_context_applied", animal, entity);
    }
    return true;
}

Animal* create(const NativeSceneEntity& entity, bool diagnose_spawn = false) {
    auto source = template_for(entity);
    if (diagnose_spawn) {
        spawn_template_diagnostic(entity, source, "factory_selection");
    }
    auto herd = source ? source->mHerd.get() : existing_herd_for(entity, "factory_fallback", true);
    if (!herd) {
        return nullptr;
    }
    // Run09 observed exact herd 1772/profile on an inactive replica. Use only
    // its existing manager-owned profile; do not enable the herd or load one.
    // Recheck immediately before the original factory, in the same apply scope.
    const auto fallback =
        source ? HerdProfileRead{} : inspect_herd_profile(entity, herd, "factory_recheck", nullptr, false);
    if (!source && (!fallback.qualified || !native_replica_allows(replica::Mutation::apply_state))) {
        return nullptr;
    }
    const auto species = source ? source->mpSpeciesProfile : fallback.profile;
    // Native factory C09B40 has the proven six-word cdecl ABI and retains a
    // live native herd. A null herd is unsafe in this Creature campaign.
    auto result = Animal::Create(Math::Vector3(entity.x, entity.y, entity.z), species, static_cast<int>(entity.age),
                                 herd, false, false);
    if (!result) {
        if (diagnose_spawn || !source) {
            spawn_factory_diagnostic(entity, "returned_null");
        }
        return nullptr;
    }
    if (!same_species(result, entity) ||
        (!source && (result->mHerd.get() != herd || result->mpSpeciesProfile != species ||
                     reinterpret_cast<uintptr_t>(result->field_E84) != fallback.archetype))) {
        if (diagnose_spawn || !source) {
            spawn_factory_diagnostic(entity, "species_or_support_mismatch_destroy", result->mID);
        }
        Manager::Get()->DestroyInstance(result);
        return nullptr;
    }
    if (diagnose_spawn || !source) {
        spawn_factory_diagnostic(entity, "qualified_native_result", result->mID);
    }
    return result;
}

// Whether the model of a resting creature is drawn far enough from the authority's place to be relocated
// (native_scene_model_drifted). The first gap measured after a relocation is kept as the residual it left.
bool model_drifted(Animal* animal, const NativeSceneEntity& entity, Binding& local) {
    // Same read-only spans as presentation_context: AnimatedCreature model +180, Model transform +8.
    const auto animated = animal->mpAnimatedCreature.get();
    const auto model = diagnostic_profile_readable(animated, sizeof(*animated)) ? animated->mpModel.get() : nullptr;
    if (!diagnostic_profile_readable(model, sizeof(*model))) {
        return false;
    }
    const auto drawn = model->mTransform.GetOffset();
    const float dx = drawn.x - entity.x, dy = drawn.y - entity.y, dz = drawn.z - entity.z;
    const float gap = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (local.drift_relocated) {
        local.drift_residual = gap;
        local.drift_relocated = false;
    }
    if (!native_scene_model_drifted(entity.vx == 0 && entity.vy == 0 && entity.vz == 0, gap, local.drift_residual)) {
        return false;
    }
    local.drift_relocated = true;
    static unsigned recorded = 0;
    if (recorded++ < 96) {
        char fields[200]{};
        sprintf_s(fields, ",\"remote_entity\":%llu,\"owner\":%llu,\"native_id\":%u,\"gap\":%.4g,\"limit\":%.4g",
                  entity.id, entity.owner, animal->mID, double(gap), double(native_model_drift_limit));
        native_actor_worker_event("scene_native_model_drift", fields);
    }
    return true;
}

bool apply_bound_pose(Animal* animal, const NativeSceneEntity& entity, Binding& local) {
    const bool was_dead = animal->mbDead;
    const bool was_fed_on = animal->mbHasBeenEaten;
    const float old_food = animal->mFoodValue;
    bool relocate_model = !local.presentation_seen ||
                          local.presentation_model != reinterpret_cast<uintptr_t>(animal->mpAnimatedCreature.get());
    if (!relocate_model) {
        relocate_model = model_drifted(animal, entity, local);
    }
    if (!apply_pose(animal, entity, relocate_model)) {
        return false;
    }
    const bool dead = entity.life_state == 1;
    const auto model = reinterpret_cast<uintptr_t>(animal->mpAnimatedCreature.get());
    const bool changed =
        local.presentation_seen && (local.presentation_dead != dead || local.presentation_model != model);
    const bool present_life = dead ? !local.presentation_seen || changed : was_dead || changed;
    if (present_life && !native_life_present(animal, dead, entity.id, entity.generation)) {
        return projection_rejected(animal, entity, "life_presentation");
    }
    local.presentation_seen = true;
    local.presentation_dead = dead;
    local.presentation_model = model;
    // Record each changed native nutrition readback at its actual source tick,
    // including a fresh fed baseline or a newly observed beneficiary. The
    // beneficiary is replicated metadata; food/first-feed are native rereads.
    if (was_fed_on != animal->mbHasBeenEaten || old_food != animal->mFoodValue ||
        (!local.pickup_seen && entity.fed_on) || (local.pickup_seen && local.pickup_owner != entity.pickup_owner)) {
        actor_sample("scene_native_pickup_state_applied", animal, entity);
    }
    local.pickup_seen = true;
    local.pickup_owner = entity.pickup_owner;
    return true;
}

struct Projection {
    const NativeSceneEntity* entity;
    Binding* local;
};

void present_recharge(Animal* animal, const NativeSceneEntity& entity, Binding& local) {
    const auto edges = native_recharge_edges(local.recharging, entity.recharging);
    local.recharging = entity.recharging;
    recharge_creature = reinterpret_cast<uintptr_t>(animal);
    if (!(edges.started[0] | edges.started[1] | edges.started[2] | edges.finished[0] | edges.finished[1] |
          edges.finished[2])) {
        return;
    }
    const auto count = static_cast<uint32_t>(animal->GetAbilitiesCount());
    const float begun = 0, done = 1;
    for (uint32_t i = 0; i < 88 && i < count && i < animal->mAbilityStates.size(); ++i) {
        const uint32_t mask = 1u << (i % 32);
        const bool started = edges.started[i / 32] & mask, finished = edges.finished[i / 32] & mask;
        if (!started && !finished) {
            continue;
        }
        // As static C19900 leaves an ability that has just ended, and C15470 one that has recharged.
        animal->mRechargingAbilityBits.set(i, started);
        std::memcpy(&animal->mAbilityStates[i].field_04, started ? &begun : &done, sizeof(float));
        static unsigned recorded = 0;
        if (recorded++ < 256) {
            char fields[160]{};
            sprintf_s(fields,
                      ",\"remote_entity\":%llu,\"owner\":%llu,\"native_id\":%u,\"ability_index\":%u,\"recharging\":%s",
                      entity.id, entity.owner, animal->mID, i, started ? "true" : "false");
            native_actor_worker_event("scene_recharge_presented", fields);
        }
    }
}

bool project_nest(const NativeSceneEntity& entity, Binding& local) {
    auto value = find_nest(local.native_id, local.address);
    if (!local.live || local.kind != EntityKind::nest || local.owner || !apply_nest_model(value, entity)) {
        return false;
    }
    if (!local.presentation_seen) {
        nest_sample("scene_native_nest_applied", value, entity);
        local.presentation_seen = true;
    }
    local.fossil_tick = entity.tick;
    return true;
}

// The authority's place, turn, state and model for a fruit.
NativePlantFood wanted_plant_food(const NativeSceneEntity& entity) {
    NativePlantFood wanted;
    wanted.place[0] = entity.x;
    wanted.place[1] = entity.y;
    wanted.place[2] = entity.z;
    wanted.turn[0] = entity.qx;
    wanted.turn[1] = entity.qy;
    wanted.turn[2] = entity.qz;
    wanted.turn[3] = entity.qw;
    wanted.state = entity.fruit_state;
    wanted.model[0] = entity.model_instance;
    wanted.model[1] = entity.model_type;
    wanted.model[2] = entity.model_group;
    return wanted;
}

// A sample applied before and unchanged: say so when this game has moved the fruit since.
void report_fruit_put_back(const NativeSceneEntity& entity, const Binding& local, Noun* value,
                           const NativePlantFood& wanted) {
    NativePlantFood held;
    if (!local.presentation_seen || local.fossil_tick != entity.tick || !native_plant_food_read(value, held)) {
        return;
    }
    const float dx = held.place[0] - wanted.place[0], dy = held.place[1] - wanted.place[1],
                dz = held.place[2] - wanted.place[2];
    static unsigned put_back = 0;
    if (dx * dx + dy * dy + dz * dz > 0.0625f && put_back++ < 64) {
        char fields[320]{};
        sprintf_s(fields,
                  ",\"remote_entity\":%llu,\"native_id\":%u,\"moved_here_to\":[%.6g,%.6g,%.6g],\"authority\":[%"
                  ".6g,%.6g,%.6g],\"state\":[%u,%u]",
                  entity.id, local.native_id, double(held.place[0]), double(held.place[1]), double(held.place[2]),
                  double(wanted.place[0]), double(wanted.place[1]), double(wanted.place[2]), held.state, wanted.state);
        native_actor_worker_event("scene_native_fruit_put_back", fields);
    }
}

// The authority's place, turn and state for this fruit, applied to this world's fruit of the same number.
bool project_fruit(const NativeSceneEntity& entity, Binding& local) {
    auto value = native_plant_food_find(local.native_id, local.address);
    const NativePlantFood wanted = wanted_plant_food(entity);
    report_fruit_put_back(entity, local, value, wanted);
    if (!local.live || local.kind != EntityKind::fruit || local.owner || !native_plant_food_apply(value, wanted)) {
        event("scene_fruit_projection_rejected", value ? "fruit_not_applied" : "fruit_not_present", entity.id,
              local.native_id);
        return false;
    }
    if (!local.presentation_seen) {
        static unsigned recorded = 0;
        if (recorded++ < 8) {
            event("scene_native_fruit_applied", "authority_place_and_state", entity.id, local.native_id);
        }
        local.presentation_seen = true;
    }
    local.fossil_tick = entity.tick;
    local.applied_ms = GetTickCount64();
    return true;
}

bool project_fossil(const NativeSceneEntity& entity, Binding& local) {
    auto value = find_fossil(local.native_id, local.address);
    if (!local.live || local.kind != EntityKind::fossil || local.owner || !same_fossil(value, entity)) {
        event("scene_fossil_projection_rejected", value ? "template_changed" : "collectible_not_present", entity.id,
              local.native_id);
        if (auto raw_value = find_fossil(local.native_id, local.address, false)) {
            fossil_sample("scene_fossil_projection_native_state", raw_value, entity);
        }
        return false;
    }
    if (!local.presentation_seen) {
        fossil_sample("scene_native_fossil_applied", value, entity);
        local.presentation_seen = true;
    }
    local.fossil_tick = entity.tick;
    return true;
}

bool project_creature(const NativeSceneEntity& entity, Binding& local) {
    auto animal = find(local.native_id, local.address);
    if (!animal || !local.live || local.owner != entity.owner) {
        return projection_rejected(animal, entity, "native_binding_or_owner");
    }
    if (!apply_bound_pose(animal, entity, local)) {
        return false;
    }
    if (entity.owner && entity.owner == controlled_owner) {
        present_recharge(animal, entity, local);
    }
    const auto now = GetTickCount64();
    if (now >= local.next_sample) {
        local.next_sample = now + 1000;
        actor_sample("scene_native_replica_sample", animal, entity);
    }
    return true;
}

// Applies the authority's sample of one entity to the native noun bound to it.
bool project_entity(const NativeSceneEntity& entity, Binding& local) {
    if (entity.kind == EntityKind::nest) {
        return project_nest(entity, local);
    }
    if (entity.kind == EntityKind::fruit) {
        return project_fruit(entity, local);
    }
    if (entity.kind == EntityKind::fossil) {
        return project_fossil(entity, local);
    }
    return project_creature(entity, local);
}

bool project_callback(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    return project_entity(*args.entity, *args.local);
}

// Binds the nest this world already holds for the entity's herd, or one made by the original factory.
bool spawn_nest(const NativeSceneEntity& entity, Binding& local) {
    auto value = nest_template(entity);
    if (!value) {
        value = create_nest(entity);
    }
    if (!value || mapped(value->mID)) {
        return false;
    }
    local = make_binding(entity, value->mID, value, 0, EntityKind::nest);
    return project_nest(entity, local);
}

// Binds this world's own fruit of the entity's number.
bool spawn_fruit(const NativeSceneEntity& entity, Binding& local) {
    auto value = mapped(entity.native_id) ? nullptr : native_plant_food_find(entity.native_id);
    if (!value) {
        return false;
    }
    local = make_binding(entity, value->mID, value, 0, EntityKind::fruit);
    return project_fruit(entity, local);
}

// Binds this world's own fossil of the entity's number.
bool spawn_fossil(const NativeSceneEntity& entity, Binding& local) {
    auto value = find_fossil(entity.native_id);
    if (!same_fossil(value, entity) || mapped(value->mID)) {
        return false;
    }
    local = make_binding(entity, value->mID, value, 0, EntityKind::fossil);
    return project_fossil(entity, local);
}

bool spawn_creature(const NativeSceneEntity& entity, Binding& local) {
    auto animal = create(entity, true);
    if (!animal) {
        return false;
    }
    local = make_binding(entity, animal->mID, animal, entity.owner);
    return apply_bound_pose(animal, entity, local);
}

// Gives an entity of the authority a native noun in this world and applies its first sample.
bool spawn_entity(const NativeSceneEntity& entity, Binding& local) {
    if (entity.kind == EntityKind::nest) {
        return spawn_nest(entity, local);
    }
    if (entity.kind == EntityKind::fruit) {
        return spawn_fruit(entity, local);
    }
    if (entity.kind == EntityKind::fossil) {
        return spawn_fossil(entity, local);
    }
    return spawn_creature(entity, local);
}

bool spawn_callback(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    return spawn_entity(*args.entity, *args.local);
}

bool despawn_nest(Binding& local) {
    auto value = find_nest(local.native_id, local.address);
    if (!value || local.owner) {
        return false;
    }
    local.live = false;
    auto herd = value->mpHerd;
    if (herd && herd->mpNest.get() == value) {
        herd->mpNest = nullptr;
    }
    Manager::Get()->DestroyInstance(value);
    event("scene_native_nest_removed", "authority_retired_nest", local.id, local.native_id);
    return true;
}

// The authority's original removed the fruit's noun (an eaten fruit in a fresh world); the generic noun
// manager removes this world's the same way.
bool despawn_fruit(Binding& local) {
    auto value = native_plant_food_find(local.native_id, local.address);
    if (!value || local.owner) {
        return false;
    }
    const auto id = local.id;
    const auto native_id = local.native_id;
    local.live = false;
    Manager::Get()->DestroyInstance(value);
    event("scene_native_fruit_removed", "authority_removed_fruit", id, native_id);
    return true;
}

bool despawn_fossil(Binding& local) {
    auto value = find_fossil(local.native_id, local.address, false);
    if (!value || local.owner) {
        return false;
    }
    const auto id = local.id, generation = local.generation;
    const auto native_id = local.native_id;
    local.live = false;
    // The already-qualified generic noun manager owns disposal, model and
    // attractor cleanup. Never run collection or an unlock on the replica.
    Manager::Get()->DestroyInstance(value);
    char fields[240]{};
    sprintf_s(fields,
              ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"local_award_executed\":false", id,
              generation, native_id);
    native_actor_worker_event("scene_native_fossil_removed", fields);
    return true;
}

bool despawn_creature(Binding& local) {
    auto animal = find(local.native_id, local.address);
    if (!animal || animal == Manager::Get()->GetAvatar()) {
        return false;
    }
    // Keep the tombstone before the original destructor can call back.
    local.live = false;
    Manager::Get()->DestroyInstance(animal);
    return true;
}

bool despawn_callback(void* raw) {
    auto& local = *static_cast<Binding*>(raw);
    if (local.kind == EntityKind::nest) {
        return despawn_nest(local);
    }
    if (local.kind == EntityKind::fruit) {
        return despawn_fruit(local);
    }
    if (local.kind == EntityKind::fossil) {
        return despawn_fossil(local);
    }
    return despawn_creature(local);
}

bool owned_reference_binding(Animal* animal) {
    if (!animal || !readable_span(animal, sizeof(Animal)) || !animal->mHerd ||
        !herd_diagnostic_binding(*animal->mHerd)) {
        return false;
    }
    const auto table = *reinterpret_cast<const uintptr_t* const*>(animal);
    return reinterpret_cast<uintptr_t>(table) == image + 0x106a080 && table[0] == image + 0x86ab00 &&
           table[1] == image + 0x3b87b0;
}

bool owned_herd(Animal* animal, bool diagnostics = false) {
    const auto reject = [&](const char* reason) {
        if (diagnostics) {
            event("scene_owned_preflight_rejected", reason, 0, animal ? animal->mID : UINT32_MAX);
        }
        return false;
    };
    if (!animal || !supported(animal) || !owned_reference_binding(animal) || animal->mbDead || !animal->mHerd ||
        animal->mHerd->mbIsDestroyed || animal->mHerd->field_20 ||
        animal->mHerd->mpSpeciesProfile != animal->mpSpeciesProfile ||
        animal->mHerd->mOwnerSpeciesKey != animal->mSpeciesKey) {
        return reject("owned_herd_lifetime_reference_or_species");
    }
    NativeSceneEntity profile;
    profile.species_instance = animal->mSpeciesKey.instanceID;
    profile.species_type = animal->mSpeciesKey.typeID;
    profile.species_group = animal->mSpeciesKey.groupID;
    profile.archetype = static_cast<uint32_t>(animal->mArchetype);
    const auto ownership = inspect_herd_profile(profile, animal->mHerd.get(), "owned_profile", animal, diagnostics);
    if (!ownership.profile_qualified) {
        return reject("owned_herd_profile");
    }
    // Original C09C14 selects 4E0220 for the default species: that lookup uses
    // archetype AED08DD4, generation zero, independently of the herd's NPC
    // archetype. C0C2E0 stores its entry at +E84 and entry+10 at +E80.
    // Trial122 observed this original default-avatar path. Foreign factories
    // retain the herd-derived archetype and the existing full qualification.
    auto species = Editors::cSpeciesManager::Get();
    if (animal->mSpeciesKey == species->mDefaultKey) {
        const uint32_t default_archetype = 0xaed08dd4;
        const auto original = inspect_archetype_map(reinterpret_cast<uintptr_t>(species), default_archetype);
        if (!original.complete || original.matches != 1 || profile.archetype != default_archetype ||
            native_word(original.node + 4 + 0x10) != default_archetype ||
            reinterpret_cast<uintptr_t>(animal->field_E84) != original.node + 4) {
            return reject("owned_default_archetype");
        }
    } else if (!ownership.qualified || reinterpret_cast<uintptr_t>(animal->field_E84) != ownership.archetype) {
        return reject("owned_foreign_archetype");
    }
    if (animal->GetCurrentBrainLevel() != 0) {
        return reject("owned_brain_level");
    }
    unsigned matches = 0;
    for (const auto& member : animal->mHerd->mHerd) {
        if (!member || member->mbIsDestroyed || member->mbMarkedForDeletion || member->mHerd != animal->mHerd) {
            return reject("owned_herd_member_lifetime");
        }
        if (member.get() == animal) {
            ++matches;
        }
    }
    return matches == 1 || reject("owned_herd_member_count");
}

bool one_avatar(Animal* expected) {
    unsigned count = 0;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.GetNounID() == Animal::NOUN_ID) {
            auto actor = object_cast<Animal>(&noun);
            if (actor && (actor->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar)) {
                if (actor != expected || !present(noun)) {
                    return false;
                }
                ++count;
            }
        }
    }
    return count == 1;
}

Animal* create_owned(const NativeSceneEntity& entity) {
    auto source = template_for(entity);
    if (!source || !same_species(source, entity) || !owned_herd(source) || entity.age != 1 || entity.life_state) {
        return nullptr;
    }
    constexpr unsigned char prefix[] = {0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x33, 0xc0, 0x57, 0x8b, 0xf9, 0x85, 0xdb};
    constexpr unsigned char tail[] = {0x8b, 0xc6, 0x5e, 0x5f, 0x5b, 0xc2, 0x18, 0x00};
    if (GetAddress(Simulator::cGameNounManager, CreateHerd) != image + 0x723920 ||
        std::memcmp(reinterpret_cast<const void*>(image + 0x723920), prefix, sizeof(prefix)) ||
        std::memcmp(reinterpret_cast<const void*>(image + 0x723a0e), tail, sizeof(tail))) {
        return nullptr;
    }
    auto manager = Manager::Get();
    const auto avatar = manager->mpAvatar;
    const auto herd_before = manager->mpAvatarHerd;
    auto species = Editors::cSpeciesManager::Get();
    const auto default_key = species->mDefaultKey;
    const Math::Vector3 position(entity.x, entity.y, entity.z);
    cHerdPtr herd = manager->CreateHerd(position, source->mpSpeciesProfile, 1, false,
                                        static_cast<int>(Simulator::CreaturePersonality::Guard), false);
    if (!herd || !herd->mHerd.empty() || herd == source->mHerd || herd == herd_before ||
        manager->GetAvatar() != avatar.get() || manager->mpAvatarHerd != herd_before ||
        species->mDefaultKey != default_key) {
        return nullptr;
    }
    // Same native campaign inputs as qualified create_in_herd; no model/profile
    // or default species-key replacement. The original factory owns insertion.
    herd->mArchetype = source->mHerd->mArchetype;
    herd->mArchetypeGroup = source->mHerd->mArchetypeGroup;
    herd->mGeneration = source->mHerd->mGeneration;
    herd->mOwnerSpeciesKey = source->mHerd->mOwnerSpeciesKey;
    auto result =
        Animal::Create(position, source->mpSpeciesProfile, static_cast<int>(entity.age), herd.get(), false, false);
    if (!same_species(result, entity) || !owned_herd(result) || result->mHerd != herd || herd->mHerd.size() != 1 ||
        manager->GetAvatar() != avatar.get() || manager->mpAvatarHerd != herd_before ||
        species->mDefaultKey != default_key) {
        return nullptr;
    }
    event("scene_owned_native_factory", "separate_original_herd", entity.id, result->mID);
    return result;
}

bool owned_preflight_rejected(const char* reason, uint32_t native_id = UINT32_MAX) {
    event("scene_owned_preflight_rejected", reason, 0, native_id);
    return false;
}

// The two players' creatures of a baseline. Fails unless they are of distinct species, alive and of age 1, and an
// avatar can be assigned and replica state applied now.
bool select_owned_pair(const NativeSceneFrame& frame, NativeOwnedSpecies& pair) {
    if (!select_native_owned_species(frame.entities.data(), frame.count, pair) || !pair.distinct ||
        !native_avatar_assignment_ready() || !native_replica_allows(replica::Mutation::apply_state)) {
        return owned_preflight_rejected("owned_pair_or_projection");
    }
    for (unsigned owner = 1; owner <= 2; ++owner) {
        if (pair.owner[owner]->life_state || pair.owner[owner]->age != 1) {
            return owned_preflight_rejected("owned_life_or_age");
        }
    }
    return true;
}

// The original's own objects that an owned projection reads and changes, taken once for one baseline.
struct OwnedScene {
    Manager* manager = nullptr;
    Animal* avatar = nullptr;
    Simulator::cPlayer* player = nullptr;
    Editors::cSpeciesManager* species = nullptr;
    uint64_t epoch = 0;
};

// Fails unless the original holds a live player, an avatar in the avatar herd and brain level zero, its noun
// manager has the SDK's layout, and exactly one creature is flagged as the avatar.
bool read_owned_scene(OwnedScene& scene) {
    auto manager = Manager::Get();
    auto avatar = manager->GetAvatar();
    auto player = manager->GetPlayer();
    auto species = Editors::cSpeciesManager::Get();
    auto stage = Simulator::cCreatureGameData::Get();
    const auto epoch = native_actor_worker_status().epoch;
    if (!epoch || !avatar || !player || !species || !stage || stage->mCurrentBrainLevel != 0 || player->mbIsDestroyed ||
        player->field_20 || manager->mpAvatarHerd != avatar->mHerd ||
        reinterpret_cast<uintptr_t>(&manager->mpAvatar) - reinterpret_cast<uintptr_t>(manager) != 0x54 ||
        reinterpret_cast<uintptr_t>(&manager->mpAvatarHerd) - reinterpret_cast<uintptr_t>(manager) != 0x58 ||
        reinterpret_cast<uintptr_t>(&manager->mpPlayer) - reinterpret_cast<uintptr_t>(manager) != 0x74) {
        return owned_preflight_rejected("original_player_stage_or_manager_layout");
    }
    if (!owned_herd(avatar, true)) {
        return false;
    }
    if (!one_avatar(avatar)) {
        return owned_preflight_rejected("original_avatar_flag_census");
    }
    scene = {manager, avatar, player, species, epoch};
    return true;
}

// The second player's noun and creature as a restored checkpoint of this epoch left them in this world, if this
// world was restored from one.
struct RestoredPair {
    bool restored = false;
    uint32_t player_id = UINT32_MAX;
    Animal* second = nullptr;
};

// Fails when a restored checkpoint's two creatures are not the baseline's pair, each in its own original herd.
bool read_restored_pair(uint64_t epoch, const NativeOwnedSpecies& pair, RestoredPair& restored) {
    NativeActorCheckpoint saved_pair;
    restored.restored = native_checkpoint_foreign_pair(epoch, saved_pair, restored.player_id);
    if (!restored.restored) {
        return true;
    }
    auto first = find(saved_pair.actors[0].native_id);
    auto second = find(saved_pair.actors[1].native_id);
    restored.second = second;
    if (!same_species(first, *pair.owner[1])) {
        return owned_preflight_rejected("checkpoint_primary_species_or_archetype");
    }
    if (!same_species(second, *pair.owner[2])) {
        return owned_preflight_rejected("checkpoint_secondary_species_or_archetype");
    }
    if (!owned_herd(first, true) || !owned_herd(second, true) || first == second || first->mHerd == second->mHerd ||
        first->mHerd->mID != saved_pair.actors[0].herd_native_id ||
        second->mHerd->mID != saved_pair.actors[1].herd_native_id) {
        return owned_preflight_rejected("checkpoint_owned_herds");
    }
    return true;
}

// Fails unless the original's player nouns are exactly its own and, after a restore, the second player's.
bool check_native_players(const OwnedScene& scene, const RestoredPair& restored) {
    unsigned player_count = 0;
    for (auto& noun : scene.manager->mNouns) {
        if (noun.GetNounID() == native_player_noun_id) {
            if (&noun != static_cast<Noun*>(scene.player) &&
                (!restored.restored || noun.mID != restored.player_id ||
                 &noun != static_cast<Noun*>(native_second_player(scene.epoch)))) {
                return owned_preflight_rejected("unexpected_native_player", noun.mID);
            }
            // Player nouns are authenticated above; the scene predicate accepts
            // only animal nouns, so check the player's own lifetime flags here.
            if (noun.mbIsDestroyed || noun.field_20) {
                return owned_preflight_rejected("native_player_not_present", noun.mID);
            }
            ++player_count;
        }
    }
    if (player_count != (restored.restored ? 2u : 1u)) {
        return owned_preflight_rejected("native_player_count");
    }
    return true;
}

// Starts the record of this epoch's owned creatures. Every client initially loads original A. Preserve that native
// noun for canonical A even when B becomes the local rendered avatar.
bool adopt_owned_epoch(const OwnedScene& scene, const NativeOwnedSpecies& pair, const NativeSceneMapping& mapping,
                       const RestoredPair& restored) {
    const auto avatar = scene.avatar;
    if (!same_species(avatar, *pair.owner[1]) || scene.species->mDefaultKey != avatar->mSpeciesKey) {
        return owned_preflight_rejected("initial_primary_species");
    }
    replica_owned = {};
    replica_owned.epoch = scene.epoch;
    replica_owned.owner = mapping.controlled_owner;
    replica_owned.player_id = scene.player->mID;
    replica_owned.player = reinterpret_cast<uintptr_t>(scene.player);
    replica_owned.primary_profile = reinterpret_cast<uintptr_t>(avatar->mpSpeciesProfile);
    replica_owned.default_key = scene.species->mDefaultKey;
    replica_owned.actors[1] = make_binding(*pair.owner[1], avatar->mID, avatar, 1);
    replica_owned.pool_cycles[1] = static_cast<uint32_t>(avatar->field_E54);
    if (restored.restored) {
        replica_owned.actors[2] = make_binding(*pair.owner[2], restored.second->mID, restored.second, 2);
        replica_owned.pool_cycles[2] = static_cast<uint32_t>(restored.second->field_E54);
        event("scene_owned_checkpoint_adopted", "original_saved_actor_and_herd", pair.owner[2]->id,
              restored.second->mID);
    }
    return true;
}

// Finds the first player's creature and finds or creates the second's. Preflight and reserve A before creating B.
// It must never be mistaken for an unowned NPC by native-ID matching or retired during baseline cleanup.
bool resolve_owned_actors(const NativeOwnedSpecies& pair, Animal*& first, Animal*& second) {
    first = find(replica_owned.actors[1].native_id, replica_owned.actors[1].address);
    if (!same_species(first, *pair.owner[1]) || !owned_herd(first) ||
        reinterpret_cast<uintptr_t>(first->mpSpeciesProfile) != replica_owned.primary_profile ||
        static_cast<uint32_t>(first->field_E54) != replica_owned.pool_cycles[1]) {
        return false;
    }
    second = replica_owned.actors[2].live ? find(replica_owned.actors[2].native_id, replica_owned.actors[2].address)
                                          : nullptr;
    if (replica_owned.actors[2].live &&
        (!second || static_cast<uint32_t>(second->field_E54) != replica_owned.pool_cycles[2])) {
        return false;
    }
    if (!second) {
        second = create_owned(*pair.owner[2]);
        if (second) {
            replica_owned.pool_cycles[2] = static_cast<uint32_t>(second->field_E54);
        }
    }
    if (!same_species(second, *pair.owner[2]) || !owned_herd(second) || second == first ||
        second->mHerd == first->mHerd || second->mHerd->mHerd.size() != 1) {
        return false;
    }
    replica_owned.actors[2] = make_binding(*pair.owner[2], second->mID, second, 2);
    return true;
}

// Binds each player's entity of the frame to that player's creature.
void bind_owned_entities(const NativeSceneFrame& frame, Animal* first, Animal* second) {
    for (size_t i = 0; i < frame.count; ++i) {
        if (frame.entities[i].owner) {
            const auto& entity = frame.entities[i];
            auto actor = entity.owner == 1 ? first : second;
            bindings[i] = make_binding(entity, actor->mID, actor, entity.owner);
            replica_owned.actors[static_cast<size_t>(entity.owner)] = bindings[i];
        }
    }
}

// Makes `desired` the original's avatar and its herd the avatar herd. Puts the previous ones back, and ends the
// projection, when the original does not then hold exactly that.
bool promote_avatar(const OwnedScene& scene, Animal* desired, uint64_t entity_id) {
    const auto manager = scene.manager;
    // SDK intrusive references keep both old/new native actors and herds alive
    // across the original SetAvatar reference exchange and failure rollback.
    cCreatureAnimalPtr previous = scene.avatar, next = desired;
    cHerdPtr previous_herd = manager->mpAvatarHerd, next_herd = desired->mHerd;
    manager->SetAvatar(next.get());
    manager->mpAvatarHerd = next_herd;
    if (manager->GetAvatar() != next.get() || manager->mpAvatarHerd != next->mHerd ||
        manager->GetPlayer() != scene.player || scene.species->mDefaultKey != replica_owned.default_key ||
        !one_avatar(next.get())) {
        manager->SetAvatar(previous.get());
        manager->mpAvatarHerd = previous_herd;
        replica_owned.projecting = false;
        return false;
    }
    event("scene_owned_avatar_promoted", "original_set_avatar_and_owned_herd", entity_id, desired->mID);
    return true;
}

// Reserves both players' creatures in this world for a two-owner baseline and makes the controlled one the
// original's avatar.
bool prepare_owned(const NativeSceneFrame& frame, const NativeSceneMapping& mapping) {
    NativeOwnedSpecies pair;
    OwnedScene scene;
    RestoredPair restored;
    if (!select_owned_pair(frame, pair) || !read_owned_scene(scene) ||
        !read_restored_pair(scene.epoch, pair, restored) || !check_native_players(scene, restored)) {
        return false;
    }
    if (replica_owned.epoch != scene.epoch && !adopt_owned_epoch(scene, pair, mapping, restored)) {
        return false;
    }
    if (replica_owned.retired || replica_owned.owner != mapping.controlled_owner ||
        replica_owned.player != reinterpret_cast<uintptr_t>(scene.player) ||
        replica_owned.player_id != scene.player->mID || scene.species->mDefaultKey != replica_owned.default_key) {
        return false;
    }
    Animal* first = nullptr;
    Animal* second = nullptr;
    if (!resolve_owned_actors(pair, first, second)) {
        return false;
    }
    bind_owned_entities(frame, first, second);
    replica_owned.projecting = true;
    return promote_avatar(scene, mapping.controlled_owner == 1 ? first : second,
                          pair.owner[mapping.controlled_owner]->id);
}

void portrait_skipped(const char* reason) {
    event("scene_owned_portrait_skipped", reason);
}

// Whether a two-owner baseline of this frame's epoch is committed on this client with the second player's creature
// as its avatar.
bool foreign_baseline_committed(const NativeSceneFrame& frame, const NativeSceneMapping& mapping) {
    if (!available() || !native_replica_is_client() || !native_replica_allows(replica::Mutation::apply_state) ||
        !mapping.experimental_foreign_owned || mapping.controlled_owner != 2 || replica_owned.owner != 2 ||
        replica_owned.retired || replica_owned.projecting || !replica_owned.admitted || !replica_owned.presentation ||
        !frame.epoch || admitted_epoch != frame.epoch || replica_owned.epoch != frame.epoch ||
        native_actor_worker_status().epoch != frame.epoch) {
        return false;
    }
    return true;
}

// Whether both players' creatures, the avatar, the player and the default species are still those the committed
// baseline recorded, and the second player's creature is this client's admitted entity of that player. Fills
// `identity` with that entity.
bool owned_pair_current(const NativeSceneFrame& frame, const NativeOwnedSpecies& pair, Manager* manager,
                        Editors::cSpeciesManager* species, Animal* first, Animal* second,
                        NativeSceneLocalIdentity& identity) {
    if (!first || !second || !same_species(first, *pair.owner[1]) || !same_species(second, *pair.owner[2]) ||
        !living(*first) || !living(*second) || second->mAge != 1 || !owned_herd(first) || !owned_herd(second) ||
        !one_avatar(second) || static_cast<uint32_t>(first->field_E54) != replica_owned.pool_cycles[1] ||
        static_cast<uint32_t>(second->field_E54) != replica_owned.pool_cycles[2] || !species ||
        species->mDefaultKey != replica_owned.default_key ||
        reinterpret_cast<uintptr_t>(first->mpSpeciesProfile) != replica_owned.primary_profile ||
        manager->GetAvatar() != second || manager->mpAvatarHerd != second->mHerd ||
        reinterpret_cast<uintptr_t>(manager->GetPlayer()) != replica_owned.player ||
        manager->GetPlayer()->mID != replica_owned.player_id ||
        !readable_span(second->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile)) ||
        second->mpSpeciesProfile->mCreatureKey != second->mSpeciesKey ||
        !native_scene_local_identity(frame.epoch, second->mID, reinterpret_cast<uintptr_t>(second), identity) ||
        identity.owner != 2 || identity.id != pair.owner[2]->id || identity.generation != pair.owner[2]->generation) {
        return false;
    }
    return true;
}

// Supplement the launcher's executable hash with the exact original ABIs.
// D38840's singleton operand is relocated and therefore compared as an RVA.
bool portrait_code_pinned() {
    return *reinterpret_cast<const uint8_t*>(image + 0x938840) == 0xa1 &&
           native_word(image + 0x938841) == image + 0x129e294 &&
           *reinterpret_cast<const uint8_t*>(image + 0x938845) == 0xc3 &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x410650), native_portrait_lookup_bytes,
                        sizeof(native_portrait_lookup_bytes)) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x407ca0), native_portrait_setter_prefix,
                        sizeof(native_portrait_setter_prefix)) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x407d4c), native_portrait_setter_return,
                        sizeof(native_portrait_setter_return));
}

// The portrait control of the original Creature HUD. Retain the existing objects only for this synchronous
// engine-thread call. No raw UI pointer or native resource key is kept for subsequent frames.
struct PortraitControl {
    IWinProcPtr display;
    UILayoutPtr layout;
    IWindowPtr window;
};

// Fills `control` with the references found so far. Returns why the control cannot be used, or null when it can.
const char* find_portrait_control(PortraitControl& control) {
    auto strategy = App::cCreatureModeStrategy::Get();
    if (!readable_span(strategy, sizeof(*strategy)) ||
        !readable_span(strategy->mpDisplayStrategy.get(), sizeof(Simulator::cCreatureDisplayStrategy))) {
        return "display_unavailable";
    }
    auto display = strategy->mpDisplayStrategy.get();
    // Original D2CCA1 passes display+38 as IWinProc to the HUD window. Use
    // that declared reference interface; the SDK display type has three
    // ambiguous AddRef/Release bases and cannot itself be an intrusive owner.
    auto display_proc = static_cast<UTFWin::IWinProc*>(display);
    if (reinterpret_cast<uintptr_t>(display_proc) - reinterpret_cast<uintptr_t>(display) != 0x38) {
        return "display_reference_interface";
    }
    control.display = display_proc;
    auto ui = display->mpGlobalUI;
    if (!readable_span(ui, 0xc) || *reinterpret_cast<const uintptr_t*>(ui) != image + 0x107f1d8 ||
        !readable_span(ui->mpLayout.get(), sizeof(UTFWin::UILayout))) {
        return "global_layout_unavailable";
    }
    control.layout = ui->mpLayout;
    const auto lookup = reinterpret_cast<NativePortraitFindWindow<UTFWin::UILayout, UTFWin::IWindow>>(image + 0x410650);
    auto found = lookup(control.layout.get(), native_portrait_control, true);
    if (!readable_span(found, sizeof(uintptr_t))) {
        return "portrait_control_missing";
    }
    control.window = found;
    if (control.window->GetControlID() != native_portrait_control || display->mpGlobalUI != ui ||
        ui->mpLayout != control.layout) {
        return "portrait_control_changed";
    }
    return nullptr;
}

// Asks the original setter for the second player's creature in the HUD portrait, and records whether every identity
// of the committed baseline is unchanged afterwards.
void set_owned_portrait(UTFWin::IWindow* window, Manager* manager, Editors::cSpeciesManager* species, Animal* first,
                        Animal* second, const NativeSceneLocalIdentity& identity) {
    cCreatureAnimalPtr keep_first = first, keep_second = second;
    cHerdPtr first_herd = first->mHerd, second_herd = second->mHerd;
    const auto first_key = first->mSpeciesKey, default_key = species->mDefaultKey;
    const auto second_key = second->mSpeciesKey;
    const auto first_id = first->mID, player_id = manager->GetPlayer()->mID;
    const auto first_profile = first->mpSpeciesProfile, second_profile = second->mpSpeciesProfile;
    const auto player = manager->GetPlayer();
    ResourceKey requested = second_profile->mCreatureKey;
    requested.typeID = native_portrait_png;
    const auto setter = reinterpret_cast<NativePortraitSetBackground<UTFWin::IWindow, ResourceKey>>(image + 0x407ca0);
    const bool returned = setter(window, requested, -1);
    const bool identities_unchanged =
        find(first_id, reinterpret_cast<uintptr_t>(first)) == first &&
        find(replica_owned.actors[2].native_id, reinterpret_cast<uintptr_t>(second)) == second &&
        first->mpSpeciesProfile == first_profile && second->mpSpeciesProfile == second_profile &&
        first->mSpeciesKey == first_key && second->mSpeciesKey == second_key && first->mHerd == first_herd &&
        second->mHerd == second_herd && manager->GetAvatar() == second && manager->mpAvatarHerd == second_herd &&
        manager->GetPlayer() == player && player->mID == player_id && species->mDefaultKey == default_key;
    char fields[900]{};
    sprintf_s(fields,
              ",\"owner\":2,\"entity\":%llu,\"generation\":%llu,\"native_id\":%u,\"control_id\":%u,"
              "\"requested_key\":[%u,%u,%u],\"original_return\":%s,\"identities_unchanged\":%s,"
              "\"a_native_id\":%u,\"a_species\":[%u,%u,%u],\"player_native_id\":%u,\"default_key\":[%u,%u,%u],"
              "\"history_updated\":false,\"visual_acceptance\":false",
              identity.id, identity.generation, second->mID, native_portrait_control, requested.instanceID,
              requested.typeID, requested.groupID, returned ? "true" : "false", identities_unchanged ? "true" : "false",
              first_id, first_key.instanceID, first_key.typeID, first_key.groupID, player_id, default_key.instanceID,
              default_key.typeID, default_key.groupID);
    native_actor_worker_event("scene_owned_portrait_result", fields);
}

// This is one presentation attempt after a foreign B baseline has committed.
// D2C000's broader refresh also changes ability mode; never call that routine.
void refresh_owned_portrait(const NativeSceneFrame& frame, const NativeSceneMapping& mapping) {
    if (!foreign_baseline_committed(frame, mapping)) {
        portrait_skipped("uncommitted_foreign_context");
        return;
    }
    NativeOwnedSpecies pair;
    if (!select_native_owned_species(frame.entities.data(), frame.count, pair) || !pair.distinct) {
        portrait_skipped("owned_pair");
        return;
    }
    auto manager = Manager::Get();
    auto species = Editors::cSpeciesManager::Get();
    auto first = find(replica_owned.actors[1].native_id, replica_owned.actors[1].address);
    auto second = find(replica_owned.actors[2].native_id, replica_owned.actors[2].address);
    NativeSceneLocalIdentity identity;
    if (!owned_pair_current(frame, pair, manager, species, first, second, identity)) {
        portrait_skipped("owned_identity");
        return;
    }
    if (!portrait_code_pinned()) {
        portrait_skipped("original_ui_binding");
        return;
    }
    PortraitControl control;
    if (const char* reason = find_portrait_control(control)) {
        portrait_skipped(reason);
        return;
    }
    set_owned_portrait(control.window.get(), manager, species, first, second, identity);
}

struct Baseline {
    const NativeSceneFrame* frame;
    const NativeSceneMapping* mapping;
    Result result = Result::native_failure;
};

struct OwnedProjectionRollback {
    bool active = false;
    Result* result = nullptr;
    ReplicaOwned previous{};
    cCreatureAnimalPtr avatar;
    cHerdPtr herd;
    Simulator::cCreatureGameData stage{};

    OwnedProjectionRollback(bool enabled, Result& outcome) : active(enabled), result(&outcome) {
        if (active) {
            previous = replica_owned;
            avatar = Manager::Get()->mpAvatar;
            herd = Manager::Get()->mpAvatarHerd;
            stage = *Simulator::cCreatureGameData::Get();
        }
    }

    ~OwnedProjectionRollback() {
        if (!active) {
            return;
        }
        replica_owned.projecting = false;
        if (*result == Result::accepted) {
            return;
        }
        replica_owned.admitted = replica_owned.presentation = false;
        // Still inside the replica projection callback: rollback does not open
        // an authority mutation path or run outside the lifecycle admission.
        const bool retired = replica_owned.retired;
        if (avatar && owned_herd(avatar.get()) && native_avatar_assignment_ready()) {
            Manager::Get()->SetAvatar(avatar.get());
            Manager::Get()->mpAvatarHerd = herd;
            *Simulator::cCreatureGameData::Get() = stage;
            event("scene_owned_promotion_rolled_back", "baseline_rejected", 0, avatar->mID);
            bool reusable = previous.presentation && !retired && previous.epoch == native_actor_worker_status().epoch &&
                            previous.player == reinterpret_cast<uintptr_t>(Manager::Get()->GetPlayer()) &&
                            previous.actors[previous.owner].address == reinterpret_cast<uintptr_t>(avatar.get()) &&
                            one_avatar(avatar.get());
            for (unsigned owner = 1; reusable && owner <= 2; ++owner) {
                const auto& saved = previous.actors[owner];
                auto actor = find(saved.native_id, saved.address);
                reusable = actor && owned_herd(actor) &&
                           static_cast<uint32_t>(actor->field_E54) == previous.pool_cycles[owner];
            }
            if (reusable) {
                replica_owned = previous;
                replica_owned.projecting = replica_owned.admitted = false;
                return;
            }
        }
        // An initial failed promotion cannot be rehabilitated by rejoining the
        // same partially mutated native scene. A fresh local load is required.
        replica_owned.retired = true;
    }
};

// Without the two-owner projection: binds each player's entity to the creature the mapping names for that player.
bool bind_fixture_owned(const NativeSceneFrame& frame, const NativeSceneMapping& mapping) {
    for (size_t i = 0; i < frame.count; ++i) {
        const auto& entity = frame.entities[i];
        if (!entity.owner) {
            continue;
        }
        const auto local_id = entity.owner == controlled_owner ? mapping.avatar_native_id : mapping.other_native_id;
        auto animal = find(local_id);
        if (!animal || !same_species(animal, entity) || mapped(local_id)) {
            return false;
        }
        bindings[i] = make_binding(entity, local_id, animal, entity.owner);
    }
    return true;
}

// Binds this world's own fossil of the entity's number. False when the baseline cannot go on without it.
bool match_fossil(const NativeSceneEntity& entity, Binding& slot) {
    const auto local_id = entity.native_id;
    auto value = mapped(local_id) ? nullptr : find_fossil(local_id);
    // Which entity a client could not match is recorded: trial346's restore was refused without a word.
    if (!same_fossil(value, entity)) {
        const bool absent = !value && !mapped(local_id);
        event(absent ? "scene_baseline_fossil_unmirrored" : "scene_baseline_template_missing",
              mapped(local_id) ? "fossil_already_bound" : fossil_absence(local_id), entity.id, local_id);
        return native_scene_fossil_unmirrored(entity.kind, absent);
    }
    slot = make_binding(entity, local_id, value, 0, EntityKind::fossil);
    return true;
}

// Binds this world's own fruit of the entity's number; a fruit this world does not hold is left unmirrored.
void match_fruit(const NativeSceneEntity& entity, Binding& slot) {
    const auto local_id = entity.native_id;
    auto value = mapped(local_id) ? nullptr : native_plant_food_find(local_id);
    if (!value) {
        event("scene_baseline_fruit_unmirrored", "fruit_absent", entity.id, local_id);
        return;
    }
    slot = make_binding(entity, local_id, value, 0, EntityKind::fruit);
}

// Binds the nest this world holds for the entity's herd, if it holds one. False when the herd differs, or its nest
// is not the entity's or is already bound.
bool match_nest(const NativeSceneEntity& entity, Binding& slot) {
    auto herd = nest_herd(entity.herd_native_id);
    auto value = nest_template(entity);
    if (!nest_herd_matches(herd, entity) || (herd->mpNest && !value) || (value && mapped(value->mID))) {
        event("scene_baseline_template_missing",
              !nest_herd_matches(herd, entity) ? "nest_herd_differs"
              : !value                         ? "nest_absent"
                                               : "nest_already_bound",
              entity.id, entity.herd_native_id);
        return false;
    }
    if (value) {
        slot = make_binding(entity, value->mID, value, 0, EntityKind::nest);
    }
    return true;
}

// Binds this world's creature of the entity's number when it is of the entity's species, archetype and herd. False
// when there is neither such a creature nor a living template or existing herd to create one from.
bool match_creature(const NativeSceneEntity& entity, Binding& slot) {
    const auto local_id = entity.native_id;
    auto animal = mapped(local_id) ? nullptr : find(local_id);
    if (animal && same_fixture(animal, entity)) {
        slot = make_binding(entity, local_id, animal, entity.owner);
    } else if (!template_for(entity) && !existing_herd_for(entity, "baseline_preflight", true)) {
        spawn_template_diagnostic(entity, nullptr, "baseline_preflight");
        return false;
    }
    return true;
}

// Resolve all NPC candidates before any native mutation. Native-ID matches
// are admission checks only, coupled to the same qualified scene fixture.
bool match_unowned(const NativeSceneFrame& frame) {
    for (size_t i = 0; i < frame.count; ++i) {
        const auto& entity = frame.entities[i];
        if (entity.owner) {
            continue;
        }
        if (entity.kind == EntityKind::fossil) {
            if (!match_fossil(entity, bindings[i])) {
                return false;
            }
        } else if (entity.kind == EntityKind::fruit) {
            match_fruit(entity, bindings[i]);
        } else if (entity.kind == EntityKind::nest) {
            if (!match_nest(entity, bindings[i])) {
                return false;
            }
        } else if (!match_creature(entity, bindings[i])) {
            return false;
        }
    }
    return true;
}

// Gives an entity that matched nothing a noun of its own (a nest or a creature; an unmatched fossil or fruit stays
// unmirrored), then applies the entity's sample.
bool realize_entity(const NativeSceneEntity& entity, Binding& local) {
    if (!local.live && entity.kind == EntityKind::nest) {
        return spawn_nest(entity, local);
    }
    if (!local.live && (entity.kind == EntityKind::fossil || entity.kind == EntityKind::fruit)) {
        return true;
    }
    if (!local.live) {
        auto animal = create(entity);
        if (!animal) {
            return false;
        }
        local = make_binding(entity, animal->mID, animal, entity.owner);
        event("scene_replica_spawn", "baseline_missing_noun", entity.id, animal->mID);
    }
    return project_entity(entity, local);
}

// Removes every present creature no entity is bound to. Enumerate IDs first: destroying nouns while traversing
// the native list would invalidate the iterator. The actual avatar is never removed.
bool remove_unbound_creatures() {
    std::array<uint32_t, capacity> remove{};
    size_t count = 0;
    for (auto& noun : Manager::Get()->mNouns) {
        if (present(noun) && !mapped(noun.mID)) {
            if (count == remove.size() || object_cast<Animal>(&noun) == Manager::Get()->GetAvatar()) {
                return false;
            }
            remove[count++] = noun.mID;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (auto animal = find(remove[i])) {
            Manager::Get()->DestroyInstance(animal);
            event("scene_replica_despawn", "baseline_extra_noun", 0, remove[i]);
        }
    }
    return true;
}

// Removes every collectible fossil no entity is bound to, enumerated first for the same reason.
bool remove_unbound_fossils() {
    std::array<uint32_t, capacity> remove{};
    size_t count = 0;
    for (auto& noun : Manager::Get()->mNouns) {
        if (fossil(noun) && !mapped(noun.mID)) {
            if (count == remove.size()) {
                return false;
            }
            remove[count++] = noun.mID;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (auto value = find_fossil(remove[i])) {
            Manager::Get()->DestroyInstance(value);
            event("scene_native_fossil_baseline_removed", "absent_from_authority", 0, remove[i]);
        }
    }
    return true;
}

// Removes every nest no entity is bound to, enumerated first for the same reason.
bool remove_unbound_nests() {
    std::array<uint32_t, capacity> remove{};
    size_t count = 0;
    for (auto& noun : Manager::Get()->mNouns) {
        if (nest(noun) && !mapped(noun.mID)) {
            if (count == remove.size()) {
                return false;
            }
            remove[count++] = noun.mID;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (auto value = find_nest(remove[i])) {
            auto herd = value->mpHerd;
            if (herd && herd->mpNest.get() == value) {
                herd->mpNest = nullptr;
            }
            Manager::Get()->DestroyInstance(value);
            event("scene_native_nest_baseline_removed", "absent_from_authority", 0, remove[i]);
        }
    }
    return true;
}

bool baseline_callback(void* raw) {
    auto& args = *static_cast<Baseline*>(raw);
    const auto& frame = *args.frame;
    const auto& mapping = *args.mapping;
    OwnedProjectionRollback rollback(mapping.experimental_foreign_owned, args.result);
    bindings = {};
    controlled_owner = mapping.controlled_owner;
    // Reserve both controlled local nouns first. A remote NPC native ID may
    // equal a locally generated B ID: process-local ID equality cannot steal
    // the local avatar/other-player mapping, regardless of baseline ordering.
    if (mapping.experimental_foreign_owned ? !prepare_owned(frame, mapping) : !bind_fixture_owned(frame, mapping)) {
        args.result = Result::binding_mismatch;
        return false;
    }
    if (!match_unowned(frame)) {
        args.result = Result::missing_template;
        return false;
    }
    for (size_t i = 0; i < frame.count; ++i) {
        if (!realize_entity(frame.entities[i], bindings[i])) {
            return false;
        }
    }
    if (!remove_unbound_creatures() || !remove_unbound_fossils() || !remove_unbound_nests()) {
        return false;
    }
    admitted_epoch = frame.epoch;
    if (mapping.experimental_foreign_owned) {
        replica_owned.projecting = false;
        replica_owned.admitted = replica_owned.presentation = true;
    }
    args.result = Result::accepted;
    if (mapping.experimental_foreign_owned && mapping.controlled_owner == 2) {
        refresh_owned_portrait(frame, mapping);
    }
    return true;
}
} // namespace

void* native_scene_replica_normalization(void* species_manager, void* original_profile) {
    if (!available() || !native_replica_is_client() || replica_owned.retired ||
        (!replica_owned.projecting && !replica_owned.presentation) ||
        replica_owned.epoch != native_actor_worker_status().epoch || replica_owned.owner != 2 ||
        reinterpret_cast<uintptr_t>(original_profile) != replica_owned.primary_profile) {
        return nullptr;
    }
    auto manager = Manager::Get();
    auto species = Editors::cSpeciesManager::Get();
    auto stage = Simulator::cCreatureGameData::Get();
    const auto& local = replica_owned.actors[2];
    auto actor = find(local.native_id, local.address);
    if (!species || species != species_manager || species->mDefaultKey != replica_owned.default_key || !actor ||
        !owned_herd(actor) || static_cast<uint32_t>(actor->field_E54) != replica_owned.pool_cycles[2] || !stage ||
        stage->mCurrentBrainLevel != 0 || manager->GetAvatar() != actor || manager->mpAvatarHerd != actor->mHerd ||
        reinterpret_cast<uintptr_t>(manager->GetPlayer()) != replica_owned.player || !one_avatar(actor) ||
        actor->mpSpeciesProfile == original_profile ||
        !readable_span(actor->mpSpeciesProfile, sizeof(Simulator::cSpeciesProfile)) ||
        actor->mpSpeciesProfile->mCreatureKey != actor->mSpeciesKey) {
        return nullptr;
    }
    ++replica_owned.normalizations;
    if (replica_owned.normalizations <= 3) {
        event("scene_owned_normalization_selected", "pinned_original_normalization_caller", local.id, actor->mID);
    }
    return actor->mpSpeciesProfile;
}

namespace {
// Static PE instruction prefixes supplement the host's full pinned hash. Each predicate compares the loaded
// original with the instruction bytes that one group of bindings relies on.

// Keep optional herd diagnostics/fallback bindings separate from the
// already-qualified living-template path and normal scene admission.
bool herd_diagnostic_code_pinned() {
    const std::array<unsigned char, 20> herd_cast{0x8b, 0xc1, 0x8b, 0x4c, 0x24, 0x04, 0x81, 0xf9, 0x6e, 0x51,
                                                  0x3f, 0xee, 0x74, 0x13, 0x81, 0xf9, 0x17, 0x61, 0xaa, 0x52};
    const std::array<unsigned char, 6> herd_noun{0xb8, 0x8e, 0x41, 0xbe, 0x01, 0xc3};
    const std::array<unsigned char, 6> herd_type{0xb8, 0x17, 0x61, 0xaa, 0x52, 0xc3};
    const std::array<unsigned char, 12> herd_archetype_generation{0x8b, 0x8f, 0xf0, 0, 0, 0, 0x8b, 0x97, 0x88, 0, 0, 0};
    const std::array<unsigned char, 27> herd_profile_key{0x8b, 0x86, 0xa4, 0,    0,    0,    0x85, 0xc0, 0x74,
                                                         0x23, 0x8b, 0x90, 0x04, 0x05, 0,    0,    0x05, 0x04,
                                                         0x05, 0,    0,    0x89, 0x96, 0x98, 0,    0,    0};
    return image && !std::memcmp(reinterpret_cast<const void*>(image + 0x869f50), herd_cast.data(), herd_cast.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x86a110), herd_noun.data(), herd_noun.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x86c0a0), herd_type.data(), herd_type.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x809c26), herd_archetype_generation.data(),
                        herd_archetype_generation.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x86c9ec), herd_profile_key.data(),
                        herd_profile_key.size());
}

// The species manager reads behind inspect_herd_profile. Both absolute operands have IMAGE_REL_BASED_HIGHLOW
// relocations in the pinned PE. Validate their loaded image-relative targets, not file bytes.
bool herd_profile_code_pinned() {
    const std::array<unsigned char, 4> species_get{0x55, 0x8b, 0xec, 0xa1};
    const std::array<unsigned char, 2> species_get_return{0x5d, 0xc3};
    const std::array<unsigned char, 2> species_table{0xc7, 0x01};
    const std::array<unsigned char, 15> species_bucket{0xf7, 0x71, 0x08, 0x89, 0x55, 0xfc, 0x8b, 0x55,
                                                       0xc4, 0x8b, 0x42, 0x04, 0x8b, 0x4d, 0xfc};
    const std::array<unsigned char, 6> species_next{0x8b, 0x45, 0xdc, 0x8b, 0x48, 0x10};
    const std::array<unsigned char, 9> archetype_next{0x8b, 0x45, 0xd8, 0x8b, 0x88, 0x44, 0x04, 0, 0};
    const std::array<unsigned char, 9> generation_vector{0x8b, 0x45, 0xf4, 0x8b, 0x88, 0xc4, 0x03, 0, 0};
    const std::array<unsigned char, 9> archetype_value{0x8b, 0x40, 0x10, 0x89, 0x81, 0x80, 0x0e, 0, 0};
    static_assert(sizeof(void*) == 4 && sizeof(Simulator::cSpeciesProfile) == 0xa18);
    return !std::memcmp(reinterpret_cast<const void*>(image + 0x1090), species_get.data(), species_get.size()) &&
           native_word(image + 0x1094) == image + 0x11d0c24 &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x1098), species_get_return.data(),
                        species_get_return.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0xdb335), species_table.data(), species_table.size()) &&
           native_word(image + 0xdb337) == image + 0xff08c8 &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0xe100f), species_bucket.data(), species_bucket.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0xe1026), species_next.data(), species_next.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0xe1586), archetype_next.data(), archetype_next.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0xe0179), generation_vector.data(),
                        generation_vector.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80c2ee), archetype_value.data(),
                        archetype_value.size());
}

// The original's pool lifecycle of creature nouns (see pooled_shell): the instructions the array names describe.
bool pool_lifecycle_code_pinned() {
    const std::array<unsigned char, 7> pool_spatial_disable{0xc6, 0x46, 0x71, 0x01, 0x88, 0x5e, 0x75};
    const std::array<unsigned char, 6> pool_counter_increment{0xff, 0x86, 0x54, 0x0e, 0, 0};
    const std::array<unsigned char, 6> pool_counter_initialize{0x89, 0x9e, 0x54, 0x0e, 0, 0};
    const std::array<unsigned char, 6> pool_herd_assignment{0x89, 0xbe, 0x74, 0x16, 0, 0};
    const std::array<unsigned char, 24> pooled_save_clear_species{0x33, 0xc0, 0x33, 0xc9, 0x89, 0x86, 0x28, 0x0b,
                                                                  0,    0,    0x33, 0xd2, 0x89, 0x8e, 0x2c, 0x0b,
                                                                  0,    0,    0x89, 0x96, 0x30, 0x0b, 0,    0};
    const std::array<unsigned char, 7> inactive_update_gate{0x83, 0xbe, 0x74, 0x16, 0, 0, 0};
    return image &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x6ccfdf), pool_spatial_disable.data(),
                        pool_spatial_disable.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x6cd0d5), pool_counter_increment.data(),
                        pool_counter_increment.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x81fd5d), pool_counter_initialize.data(),
                        pool_counter_initialize.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80472f), pool_herd_assignment.data(),
                        pool_herd_assignment.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80be7b), pooled_save_clear_species.data(),
                        pooled_save_clear_species.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80ae33), inactive_update_gate.data(),
                        inactive_update_gate.size());
}

// The original's pose, life state, first-feed, food, age, alpha, combatant state, base maximum health and scale
// instructions that the scene's reads and writes of a creature rely on.
bool pose_code_pinned() {
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
    const std::array<unsigned char, 31> teleport{0x8b, 0x44, 0x24, 0x08, 0x56, 0x8b, 0xf1, 0x8b, 0x4c, 0x24, 0x08,
                                                 0x50, 0x51, 0x8b, 0xce, 0xe8, 0x7c, 0xc8, 0x07, 0x00, 0xc6, 0x86,
                                                 0x9d, 0x0a, 0x00, 0x00, 0x01, 0x5e, 0xc2, 0x08, 0x00};
    const std::array<unsigned char, 17> spatial_teleport{0x56, 0x8b, 0xf1, 0x8b, 0x4c, 0x24, 0x08, 0x8b, 0x06,
                                                         0x8b, 0x50, 0x38, 0x51, 0x8b, 0xce, 0xff, 0xd2};
    return image && !std::memcmp(reinterpret_cast<const void*>(image + 0x80c040), teleport.data(), teleport.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x8888d0), spatial_teleport.data(),
                        spatial_teleport.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80dea0), position.data(), position.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80df10), orientation.data(), orientation.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80838f), death_field.data(), death_field.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80bb00), revive_field.data(), revive_field.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x97137a), first_feed_read.data(),
                        first_feed_read.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x971398), first_feed_write.data(),
                        first_feed_write.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80bc70), food_read.data(), food_read.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x970ac2), food_write.data(), food_write.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80b8d2), age_field.data(), age_field.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x8223c6), age_write.data(), age_write.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x805e6e), alpha_field.data(), alpha_field.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x4e8230), combatant_state_read.data(),
                        combatant_state_read.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x7fd1a1), combatant_state_write.data(),
                        combatant_state_write.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x804170), base_maximum.data(), base_maximum.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x837230), get_scale.data(), get_scale.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x80e750), set_scale.data(), set_scale.size()) &&
           !std::memcmp(reinterpret_cast<const void*>(image + 0x8892f0), set_extents.data(), set_extents.size());
}
} // namespace

bool initialize_native_scene() {
    if (engine_thread && !on_thread()) {
        return false;
    }
    engine_thread = GetCurrentThreadId();
    image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    herd_diagnostic_bindings = herd_diagnostic_code_pinned();
    herd_profile_bindings = herd_diagnostic_bindings && herd_profile_code_pinned();
    pool_lifecycle_bindings = pool_lifecycle_code_pinned();
    ready = pose_code_pinned() && pool_lifecycle_bindings;
    event("scene_adapter_ready", ready ? "pinned_pose_bindings" : "binding_mismatch");
    event("scene_herd_diagnostic_ready", herd_diagnostic_bindings ? "pinned_herd_profile_reads" : "binding_mismatch");
    event("scene_herd_profile_ready", herd_profile_bindings ? "pinned_current_manager_ownership" : "binding_mismatch");
    event("scene_pool_lifecycle_ready", pool_lifecycle_bindings ? "pinned_original_pool_return" : "binding_mismatch");
    return ready;
}

void dispose_native_scene() {
    if (!on_thread()) {
        return;
    }
    observed = {};
    bindings = {};
    replica_owned = {};
    capture_epoch = admitted_epoch = 0;
    controlled_owner = 0;
    ready = false;
    herd_diagnostic_bindings = herd_profile_bindings = pool_lifecycle_bindings = false;
    engine_thread = 0;
    image = 0;
}

namespace {
// The original's own numbers for the two players' creatures, and which of them this game holds.
struct OwnedActors {
    uint32_t first = UINT32_MAX, second = UINT32_MAX;
    bool has_first = false, has_second = false;

    bool owns(uint32_t native_id) const noexcept {
        return native_id == first || native_id == second;
    }

    uint32_t owner_of(uint32_t native_id) const noexcept {
        return native_id == first ? 1u : native_id == second ? 2u : 0u;
    }
};

OwnedActors owned_actors() {
    OwnedActors owned;
    owned.has_first = native_actor_owner_native_id(1, owned.first);
    owned.has_second = native_actor_owner_native_id(2, owned.second);
    return owned;
}

// A dedicated authority (primary owner 0) holds both players' creatures. A game that plays one of them holds
// exactly that one, as its original avatar, with one native player and one creature flagged as the avatar.
Result check_owned_actors(uint32_t primary_owner, const OwnedActors& owned) {
    if (primary_owner == 0) {
        const bool both = owned.has_first && owned.has_second && owned.first != owned.second;
        return both ? Result::accepted : Result::unavailable;
    }
    if (primary_owner > 2 || owned.has_first == owned.has_second ||
        (primary_owner == 1 ? !owned.has_first : !owned.has_second)) {
        return Result::binding_mismatch;
    }
    auto manager = Manager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    if (!avatar || avatar->mID != (primary_owner == 1 ? owned.first : owned.second)) {
        return Result::binding_mismatch;
    }
    size_t avatar_flags = 0, native_players = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20) {
            continue;
        }
        if (noun.GetNounID() == native_player_noun_id) {
            ++native_players;
        }
        if (noun.GetNounID() == Animal::NOUN_ID) {
            auto animal = object_cast<Animal>(&noun);
            if (animal && !animal->mbMarkedForDeletion &&
                (animal->mGeneralFlags & Simulator::kCreatureFlagIsPlayerAvatar)) {
                ++avatar_flags;
            }
        }
    }
    return native_players == 1 && avatar_flags == 1 ? Result::accepted : Result::binding_mismatch;
}

// Records a count of left-out creature nouns when it differs from the last one recorded.
void report_shell_count(const char* name, const char* reason, size_t count, size_t& last) {
    if (count == last) {
        return;
    }
    last = count;
    char fields[160]{};
    sprintf_s(fields, ",\"count\":%u,\"reason\":\"%s\"", static_cast<unsigned>(count), reason);
    native_actor_worker_event(name, fields);
}

// Looks at every creature noun before any is captured. Shells and pool placeholders are counted and left out. A
// present creature the scene cannot carry fails the capture: a low native ID alone does not establish that an
// unsupported noun is a pool placeholder, so none is silently omitted from the shared scene.
Result census_creatures(const OwnedActors& owned) {
    size_t unsupported = 0, inactive = 0, unbound = 0, pooled = 0;
    for (auto& noun : Manager::Get()->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Animal::NOUN_ID) {
            continue;
        }
        auto animal = object_cast<Animal>(&noun);
        if (!animal || animal->mbMarkedForDeletion) {
            continue;
        }
        if (inactive_shell(animal)) {
            ++inactive;
            continue;
        }
        if (unbound_shell(animal)) {
            if (owned.owns(animal->mID)) {
                capture_rejection(animal, "owned_actor_unbound_shell");
                return Result::unsupported_entity;
            }
            ++unbound;
            continue;
        }
        if (pooled_shell(animal)) {
            if (owned.owns(animal->mID)) {
                capture_rejection(animal, "owned_actor_pool_return");
                return Result::unsupported_entity;
            }
            ++pooled;
            for (auto& seen : observed) {
                if (seen.native_id == animal->mID && seen.address == reinterpret_cast<uintptr_t>(animal)) {
                    retire_incarnation(seen, "observed_original_pool_return", static_cast<uint32_t>(animal->field_E54));
                }
            }
            continue;
        }
        if (const auto reason = unsupported_reason(animal)) {
            if (unsupported < 32) {
                capture_rejection(animal, reason);
            }
            ++unsupported;
        }
    }
    report_shell_count("scene_inactive_shell_census", "observed_inactive_default_signature", inactive,
                       last_inactive_shell_count);
    report_shell_count("scene_unbound_shell_census", "disabled_unbound_native_nouns", unbound,
                       last_unbound_shell_count);
    report_shell_count("scene_pooled_shell_census", "original_retained_pool_signature", pooled,
                       last_pooled_shell_count);
    if (unsupported) {
        event("scene_capture_rejection_count", "unsupported_present_entities", 0, static_cast<uint32_t>(unsupported));
        return Result::unsupported_entity;
    }
    return Result::accepted;
}

Seen* find_seen(uint32_t native_id, uintptr_t address) {
    for (auto& seen : observed) {
        if (seen.native_id == native_id && seen.address == address) {
            return &seen;
        }
    }
    return nullptr;
}

// A noun's record across captures, claimed for this frame. `first` says the noun starts a new incarnation.
struct Claim {
    Seen* seen = nullptr;
    bool first = false;
    Result result = Result::accepted;
};

// Claims `known`, or a free record for a noun not seen in earlier captures. Fails when the noun was already captured
// in this frame or no record is free.
Claim claim_seen(Seen* known, uint32_t native_id, uintptr_t address, uint32_t pool_cycle) {
    if (known && known->visited) {
        return {nullptr, false, Result::binding_mismatch};
    }
    Claim claim{known, false, Result::accepted};
    if (!claim.seen) {
        for (auto& seen : observed) {
            if (!seen.address) {
                claim.seen = &seen;
                break;
            }
        }
        if (!claim.seen) {
            return {nullptr, false, Result::full};
        }
        *claim.seen = {native_id, address, ++next_incarnation, pool_cycle, false};
        claim.first = true;
    }
    claim.seen->visited = true;
    return claim;
}

// A fossil, nest or fruit: these have no pool cycle.
Claim claim_seen(uint32_t native_id, uintptr_t address) {
    return claim_seen(find_seen(native_id, address), native_id, address, 0);
}

// A creature. The original reuses a creature noun from its pool under the same number and address: a changed
// pool cycle ends the incarnation seen before.
Claim claim_seen(Animal* animal) {
    const auto address = reinterpret_cast<uintptr_t>(animal);
    const auto pool_cycle = static_cast<uint32_t>(animal->field_E54);
    Seen* known = find_seen(animal->mID, address);
    if (known && known->visited) {
        return {nullptr, false, Result::binding_mismatch};
    }
    if (known && known->pool_cycle != pool_cycle) {
        retire_incarnation(*known, "native_pool_cycle_changed", pool_cycle);
        known = nullptr;
    }
    return claim_seen(known, animal->mID, address, pool_cycle);
}

uint64_t entity_id(uint64_t epoch, uint32_t native_id) noexcept {
    return (epoch << 32) | native_id;
}

void set_pose(NativeSceneEntity& entity, const Math::Vector3& position, const Math::Quaternion& orientation) {
    entity.x = position.x;
    entity.y = position.y;
    entity.z = position.z;
    entity.qx = orientation.x;
    entity.qy = orientation.y;
    entity.qz = orientation.z;
    entity.qw = orientation.w;
}

// The place, turn, scale and model of a noun that has no creature state.
void set_spatial(NativeSceneEntity& entity, const Spatial& spatial) {
    set_pose(entity, spatial.mPosition, spatial.mOrientation);
    entity.scale = spatial.mScale;
    entity.model_instance = spatial.mModelKey.instanceID;
    entity.model_type = spatial.mModelKey.typeID;
    entity.model_group = spatial.mModelKey.groupID;
}

// Records the rejected scalar values exactly, including non-finite float bit patterns, without emitting invalid
// JSON or pointers.
void report_invalid_scalars(const NativeSceneEntity& e) {
    const auto bits = [](float value) {
        uint32_t word = 0;
        std::memcpy(&word, &value, 4);
        return word;
    };
    char fields[1024]{};
    sprintf_s(fields,
              ",\"native_id\":%u,\"owner\":%llu,\"herd_native_id\":%u,"
              "\"species\":[%u,%u,%u],\"life_state\":%u,\"age\":%u,\"alpha\":%u,\"combatant_state\":%u,"
              "\"fed_on\":%u,\"pickup_owner\":%u,\"float_bits\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
              "%u,%u]",
              e.native_id, e.owner, e.herd_native_id, e.species_instance, e.species_type, e.species_group, e.life_state,
              e.age, e.alpha, e.combatant_state, e.fed_on, e.pickup_owner, bits(e.x), bits(e.y), bits(e.z), bits(e.qx),
              bits(e.qy), bits(e.qz), bits(e.qw), bits(e.vx), bits(e.vy), bits(e.vz), bits(e.health), bits(e.energy),
              bits(e.hunger), bits(e.dna), bits(e.scale), bits(e.food));
    native_actor_worker_event("scene_capture_invalid_scalars", fields);
}

// One present creature's identity and state. Fails the capture for a creature whose state the scene cannot carry.
Result read_creature(Animal* animal, uint64_t generation, uint64_t epoch, const OwnedActors& owned,
                     NativeSceneEntity& e) {
    e = {};
    if (epoch > UINT32_MAX) {
        return Result::unsupported_entity;
    }
    e.id = entity_id(epoch, animal->mID);
    e.generation = generation;
    e.tick = GetTickCount64();
    e.native_id = animal->mID;
    e.herd_native_id = animal->mHerd->mID;
    e.species_instance = animal->mSpeciesKey.instanceID;
    e.species_type = animal->mSpeciesKey.typeID;
    e.species_group = animal->mSpeciesKey.groupID;
    e.archetype = static_cast<uint32_t>(animal->mArchetype);
    e.owner = owned.owner_of(animal->mID);
    set_pose(e, animal->GetPosition(), animal->GetOrientation());
    e.vx = animal->mVelocity.x;
    e.vy = animal->mVelocity.y;
    e.vz = animal->mVelocity.z;
    e.health = animal->mHealthPoints;
    e.energy = animal->mEnergy;
    e.hunger = animal->mHunger;
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
    if (e.owner) {
        for (uint32_t ability = 0; ability < 88; ++ability) {
            if (animal->mRechargingAbilityBits.test(ability)) {
                e.recharging[ability / 32] |= 1u << (ability % 32);
            }
        }
    }
    if (e.owner && !native_actor_owner_dna(static_cast<uint32_t>(e.owner), e.dna)) {
        event("scene_capture_rejected", "owner_dna_unavailable", e.id, e.native_id);
        return Result::unavailable;
    }
    if (!valid(e)) {
        report_invalid_scalars(e);
        event("scene_capture_rejected", "unsupported_native_values", e.id, e.native_id);
        return Result::unsupported_entity;
    }
    return Result::accepted;
}

// Every present creature. `captured[i]` is the creature of `frame.entities[i]`.
Result capture_creatures(NativeSceneFrame& frame, uint64_t epoch, const OwnedActors& owned, bool sample,
                         std::array<Animal*, capacity>& captured) {
    for (auto& noun : Manager::Get()->mNouns) {
        if (!present(noun)) {
            continue;
        }
        if (frame.count == capacity) {
            return Result::full;
        }
        auto animal = object_cast<Animal>(&noun);
        const Claim claim = claim_seen(animal);
        if (claim.result != Result::accepted) {
            return claim.result;
        }
        captured[frame.count] = animal;
        auto& entity = frame.entities[frame.count++];
        const Result outcome = read_creature(animal, claim.seen->generation, epoch, owned, entity);
        if (outcome != Result::accepted) {
            return outcome;
        }
        if (claim.first) {
            herd_census_diagnostic(entity, "authority_first_capture", animal);
        }
        if (sample) {
            actor_sample("scene_native_authority_sample", animal, entity);
        }
    }
    return Result::accepted;
}

Result capture_fossils(NativeSceneFrame& frame, uint64_t epoch, uint64_t now) {
    for (auto& noun : Manager::Get()->mNouns) {
        auto value = fossil(noun);
        if (!value) {
            continue;
        }
        if (frame.count == capacity || epoch > UINT32_MAX) {
            return Result::full;
        }
        const Claim claim = claim_seen(noun.mID, reinterpret_cast<uintptr_t>(value));
        if (claim.result != Result::accepted) {
            return claim.result;
        }
        auto& entity = frame.entities[frame.count++];
        entity = {};
        entity.kind = EntityKind::fossil;
        entity.id = entity_id(epoch, noun.mID);
        entity.generation = claim.seen->generation;
        entity.tick = now;
        entity.native_id = noun.mID;
        set_spatial(entity, *static_cast<Spatial*>(value));
        if (!valid(entity)) {
            event("scene_fossil_capture_rejected", "unsupported_native_values", entity.id, entity.native_id);
            return Result::unsupported_entity;
        }
        if (claim.first) {
            fossil_sample("scene_native_fossil_published", value, entity);
        }
    }
    return Result::accepted;
}

// C02: every fruit of this world, with its place, turn and state. A fruit whose values the wire does not admit is
// left out rather than failing the scene.
Result capture_fruits(NativeSceneFrame& frame, uint64_t epoch, uint64_t now) {
    for (auto& noun : Manager::Get()->mNouns) {
        NativePlantFood food;
        if (noun.GetNounID() != Simulator::kFruit || !native_plant_food_read(&noun, food)) {
            continue;
        }
        if (frame.count == capacity || epoch > UINT32_MAX) {
            return Result::full;
        }
        NativeSceneEntity entity{};
        entity.kind = EntityKind::fruit;
        entity.id = entity_id(epoch, noun.mID);
        entity.generation = 1;
        entity.tick = now;
        entity.native_id = noun.mID;
        entity.x = food.place[0];
        entity.y = food.place[1];
        entity.z = food.place[2];
        entity.qx = food.turn[0];
        entity.qy = food.turn[1];
        entity.qz = food.turn[2];
        entity.qw = food.turn[3];
        entity.scale = food.scale;
        entity.model_instance = food.model[0];
        entity.model_type = food.model[1];
        entity.model_group = food.model[2];
        entity.fruit_state = food.state;
        if (!valid(entity)) {
            static unsigned recorded = 0;
            if (recorded++ < 16) {
                event("scene_fruit_capture_skipped", "values_not_admitted", entity.id, entity.native_id);
            }
            continue;
        }
        const Claim claim = claim_seen(noun.mID, food.address);
        if (claim.result != Result::accepted) {
            return claim.result;
        }
        entity.generation = claim.seen->generation;
        frame.entities[frame.count++] = entity;
    }
    return Result::accepted;
}

Result capture_nests(NativeSceneFrame& frame, uint64_t epoch, uint64_t now) {
    for (auto& noun : Manager::Get()->mNouns) {
        auto value = nest(noun);
        if (!value) {
            continue;
        }
        if (frame.count == capacity || epoch > UINT32_MAX) {
            return Result::full;
        }
        const Claim claim = claim_seen(noun.mID, reinterpret_cast<uintptr_t>(value));
        if (claim.result != Result::accepted) {
            return claim.result;
        }
        auto& entity = frame.entities[frame.count++];
        entity = {};
        entity.kind = EntityKind::nest;
        entity.id = entity_id(epoch, noun.mID);
        entity.generation = claim.seen->generation;
        entity.tick = now;
        entity.native_id = noun.mID;
        const auto herd = value->mpHerd;
        entity.herd_native_id = herd->mID;
        entity.archetype = herd->mArchetype;
        entity.species_instance = herd->mOwnerSpeciesKey.instanceID;
        entity.species_type = herd->mOwnerSpeciesKey.typeID;
        entity.species_group = herd->mOwnerSpeciesKey.groupID;
        set_spatial(entity, *static_cast<Spatial*>(value));
        if (!valid(entity)) {
            event("scene_nest_capture_rejected", "unsupported_native_values", entity.id, entity.native_id);
            return Result::unsupported_entity;
        }
        if (claim.first) {
            nest_sample("scene_native_nest_published", value, entity);
        }
    }
    return Result::accepted;
}
} // namespace

// The authority's scene for one frame: every present creature, then fossils, fruits and nests. A failure leaves the
// frame's epoch at zero.
NativeSceneResult capture_native_scene(NativeSceneFrame& frame, uint32_t primary_owner) {
    frame.count = 0;
    frame.epoch = 0;
    if (!on_thread()) {
        return Result::wrong_thread;
    }
    if (!available()) {
        return Result::unavailable;
    }
    const auto status = native_actor_worker_status();
    if (!status.epoch) {
        return Result::unavailable;
    }
    if (capture_epoch != status.epoch) {
        observed = {};
        capture_epoch = status.epoch;
        last_inactive_shell_count = last_unbound_shell_count = last_pooled_shell_count = SIZE_MAX;
    }
    for (auto& seen : observed) {
        seen.visited = false;
    }
    const OwnedActors owned = owned_actors();
    Result result = check_owned_actors(primary_owner, owned);
    if (result != Result::accepted) {
        return result;
    }
    const auto now = GetTickCount64();
    const bool sample = now >= next_capture_sample;
    result = census_creatures(owned);
    if (result != Result::accepted) {
        return result;
    }
    std::array<Animal*, capacity> captured{};
    result = capture_creatures(frame, status.epoch, owned, sample, captured);
    if (result == Result::accepted) {
        result = capture_fossils(frame, status.epoch, now);
    }
    if (result == Result::accepted) {
        result = capture_fruits(frame, status.epoch, now);
    }
    if (result == Result::accepted) {
        result = capture_nests(frame, status.epoch, now);
    }
    if (result != Result::accepted) {
        return result;
    }
    for (auto& seen : observed) {
        if (!seen.visited) {
            seen = {};
        }
    }
    frame.epoch = status.epoch;
    // Native12 showed that one-second samples can miss every shared food revision while original feeding changes it
    // continuously. Retain an exact-tick source reread for each fed corpse in this bounded successful frame. Pointers
    // exist only on this engine-thread capture stack.
    for (size_t i = 0; i < frame.count; ++i) {
        if (frame.entities[i].life_state == 1 && frame.entities[i].fed_on) {
            actor_sample("scene_native_pickup_state_published", captured[i], frame.entities[i]);
        }
    }
    if (sample) {
        next_capture_sample = now + 1000;
    }
    return Result::accepted;
}

namespace {
// The record of that generation of a noun, or null. `ambiguous` is set when two records claim it.
const Seen* find_incarnation(uint32_t native_id, uint64_t generation, bool& ambiguous) {
    const Seen* incarnation = nullptr;
    for (const auto& seen : observed) {
        if (seen.native_id == native_id && seen.generation == generation) {
            if (incarnation) {
                ambiguous = true;
                return nullptr;
            }
            incarnation = &seen;
        }
    }
    return incarnation;
}
} // namespace

NativeSceneResult validate_native_scene_action_entity(uint64_t epoch, const NativeSceneEntity& entity) {
    if (!on_thread()) {
        return Result::wrong_thread;
    }
    if (!available() || native_replica_is_client()) {
        return Result::unavailable;
    }
    if (!epoch || epoch != capture_epoch || epoch != native_actor_worker_status().epoch || epoch > UINT32_MAX ||
        entity.id != entity_id(epoch, entity.native_id)) {
        return Result::stale;
    }
    if (!valid(entity)) {
        return Result::invalid;
    }
    bool ambiguous = false;
    const Seen* incarnation = find_incarnation(entity.native_id, entity.generation, ambiguous);
    if (ambiguous) {
        return Result::binding_mismatch;
    }
    // The original destruction hook clears Seen before the native destructor.
    // Even reuse of both the same ID and address therefore fails this fence.
    if (!incarnation || !incarnation->address) {
        return Result::stale;
    }
    if (entity.kind == EntityKind::fossil) {
        return same_fossil(find_fossil(entity.native_id, incarnation->address), entity) ? Result::accepted
                                                                                        : Result::stale;
    }
    if (entity.kind == EntityKind::nest) {
        return same_nest(find_nest(entity.native_id, incarnation->address), entity) ? Result::accepted : Result::stale;
    }
    auto animal = find(entity.native_id, incarnation->address);
    // Validate current identity/life state for both living actors and corpse
    // targets. The caller separately admits pickup vs combat command semantics.
    if (!animal || static_cast<uint32_t>(animal->field_E54) != incarnation->pool_cycle ||
        animal->mbDead != (entity.life_state == 1) || !same_fixture(animal, entity)) {
        return Result::stale;
    }
    if (entity.owner) {
        uint32_t current_native_id = UINT32_MAX;
        if (!native_actor_owner_native_id(static_cast<uint32_t>(entity.owner), current_native_id) ||
            current_native_id != entity.native_id) {
            return Result::binding_mismatch;
        }
    }
    return Result::accepted;
}

namespace {
// What a two-owner baseline needs of this game before anything is changed: an avatar can be assigned now, the
// current avatar is in a qualified herd of its own that is the avatar herd, and the Creature stage's data exists.
bool foreign_owned_preflight() {
    const char* failure = nullptr;
    if (!native_avatar_assignment_ready(&failure)) {
        return owned_preflight_rejected(failure);
    }
    if (!owned_herd(Manager::Get()->GetAvatar(), true)) {
        return false;
    }
    if (!Simulator::cCreatureGameData::Get() || Manager::Get()->mpAvatarHerd != Manager::Get()->GetAvatar()->mHerd) {
        return owned_preflight_rejected("stage_or_current_avatar_herd");
    }
    return true;
}

// Whether every entity is valid, no two share an ID or a native ID, and the frame holds the players' creatures the
// mapping expects: one of each player, or with `one_primary` only the controlled player's.
bool baseline_entities_valid(const NativeSceneFrame& frame, const NativeSceneMapping& mapping, bool one_primary) {
    size_t owners[3]{};
    for (size_t i = 0; i < frame.count; ++i) {
        if (!valid(frame.entities[i])) {
            return false;
        }
        ++owners[frame.entities[i].owner];
        for (size_t j = 0; j < i; ++j) {
            if (frame.entities[j].id == frame.entities[i].id ||
                frame.entities[j].native_id == frame.entities[i].native_id) {
                return false;
            }
        }
    }
    if (one_primary) {
        if (owners[mapping.controlled_owner] != 1 || owners[3 - mapping.controlled_owner] != 0) {
            return false;
        }
    } else if (owners[1] != 1 || owners[2] != 1) {
        return false;
    }
    return true;
}
} // namespace

NativeSceneResult apply_native_scene_baseline(const NativeSceneFrame& frame, const NativeSceneMapping& mapping) {
    if (!on_thread()) {
        return Result::wrong_thread;
    }
    if (!available() || !native_replica_is_client()) {
        return Result::unavailable;
    }
    if (!frame.epoch || !frame.count || frame.count > capacity || mapping.controlled_owner < 1 ||
        mapping.controlled_owner > 2 ||
        (!mapping.experimental_foreign_owned && mapping.avatar_native_id == mapping.other_native_id)) {
        return Result::invalid;
    }
    const bool one_primary = !mapping.experimental_foreign_owned && mapping.other_native_id == UINT32_MAX;
    auto avatar = find(mapping.avatar_native_id), other = one_primary ? nullptr : find(mapping.other_native_id);
    if (!mapping.experimental_foreign_owned &&
        (!avatar || (!one_primary && !other) || avatar != Manager::Get()->GetAvatar())) {
        return Result::binding_mismatch;
    }
    if (mapping.experimental_foreign_owned && !foreign_owned_preflight()) {
        return Result::binding_mismatch;
    }
    if (!baseline_entities_valid(frame, mapping, one_primary)) {
        return Result::invalid;
    }
    Baseline operation{&frame, &mapping};
    if (!native_replica_project(baseline_callback, &operation, true)) {
        reset_native_scene_baseline();
        event("scene_baseline_rejected", native_scene_result_name(operation.result));
        return operation.result;
    }
    event("scene_baseline_applied", "creature_fixture_scalar_life_state", 0, static_cast<uint32_t>(frame.count));
    return Result::accepted;
}

namespace {
// Whether this world holds what a spawn of the entity starts from: its own fruit or fossil of that number, the
// entity's herd for a nest, a living template or an existing herd for a creature.
bool spawn_source_present(const NativeSceneEntity& entity) {
    if (entity.kind == EntityKind::fruit) {
        if (mapped(entity.native_id) || !native_plant_food_find(entity.native_id)) {
            return false;
        }
    } else if (entity.kind == EntityKind::fossil) {
        if (!same_fossil(find_fossil(entity.native_id), entity)) {
            return false;
        }
    } else if (entity.kind == EntityKind::nest) {
        auto herd = nest_herd(entity.herd_native_id);
        if (!nest_herd_matches(herd, entity) || (herd->mpNest && !nest_template(entity))) {
            return false;
        }
    } else if (!template_for(entity) && !existing_herd_for(entity, "spawn_preflight", true)) {
        spawn_template_diagnostic(entity, nullptr, "spawn_preflight");
        return false;
    }
    return true;
}
} // namespace

NativeSceneResult spawn_native_scene_entity(const NativeSceneEntity& entity) {
    if (!on_thread()) {
        return Result::wrong_thread;
    }
    if (!available() || !admitted_epoch || !native_replica_is_client()) {
        return Result::unavailable;
    }
    if (!valid(entity) || entity.owner) {
        return Result::invalid;
    }
    Binding* slot = nullptr;
    for (auto& bound : bindings) {
        if (bound.id == entity.id) {
            if (bound.live || entity.generation <= bound.generation) {
                return Result::stale;
            }
            slot = &bound;
            break;
        }
    }
    if (!slot) {
        slot = empty_binding();
    }
    if (!slot) {
        return Result::full;
    }
    if (!spawn_source_present(entity)) {
        return Result::missing_template;
    }
    Projection args{&entity, slot};
    if (!native_replica_project(spawn_callback, &args, true)) {
        reset_native_scene_baseline();
        return Result::native_failure;
    }
    event("scene_replica_spawn", "authoritative_spawn", entity.id, slot->native_id);
    return Result::accepted;
}

NativeSceneResult despawn_native_scene_entity(uint64_t id, uint64_t generation) {
    if (!on_thread()) {
        return Result::wrong_thread;
    }
    if (!available() || !admitted_epoch || !native_replica_is_client()) {
        return Result::unavailable;
    }
    auto slot = binding(id, generation);
    if (!slot || !slot->live) {
        return Result::stale;
    }
    const auto local = slot->native_id;
    if (!native_replica_project(despawn_callback, slot, true)) {
        reset_native_scene_baseline();
        return Result::native_failure;
    }
    event("scene_replica_despawn", "authoritative_despawn", id, local);
    return Result::accepted;
}

bool native_scene_recharge_ticks(const Simulator::cCreatureAnimal* creature) {
    if (!creature || reinterpret_cast<uintptr_t>(creature) != recharge_creature || !on_thread() || !admitted_epoch) {
        return false;
    }
    bool own = false;
    for (const auto& bound : bindings) {
        own =
            own || (bound.live && bound.owner && bound.owner == controlled_owner && bound.address == recharge_creature);
    }
    // The original's update also advances the ability state at +C04; a client's creature has none.
    return native_recharge_tick_admitted(native_replica_is_client(), own, creature->mRechargingAbilityBits.any(),
                                         creature->field_C04 || creature->field_C08 || creature->field_C0C ||
                                             creature->mInUseAbilityBits.any());
}

bool native_scene_entity_bound(uint64_t id) {
    if (!on_thread() || !id) {
        return false;
    }
    for (const auto& bound : bindings) {
        if (bound.live && bound.id == id) {
            return true;
        }
    }
    return false;
}

NativeSceneResult project_native_scene_entity(const NativeSceneEntity& entity) {
    if (!on_thread()) {
        return Result::wrong_thread;
    }
    if (!available() || !admitted_epoch || !native_replica_is_client()) {
        return Result::unavailable;
    }
    if (!valid(entity)) {
        return Result::invalid;
    }
    auto slot = binding(entity.id, entity.generation);
    if (!slot || !slot->live) {
        event("scene_projection_stale", slot ? "local_incarnation_retired" : "mapping_missing", entity.id,
              slot ? slot->native_id : UINT32_MAX);
        return Result::stale;
    }
    // Original destruction/pool callbacks invalidate the mapping immediately.
    // An unchanged static resource needs no repeated census walk per render frame.
    if ((entity.kind == EntityKind::fossil || entity.kind == EntityKind::nest || entity.kind == EntityKind::fruit) &&
        slot->kind == entity.kind && slot->presentation_seen && slot->fossil_tick == entity.tick &&
        !native_scene_static_recheck_due(entity.kind, slot->native_id, slot->applied_ms, GetTickCount64())) {
        return Result::accepted;
    }
    Projection args{&entity, slot};
    if (!native_replica_project(project_callback, &args, false)) {
        reset_native_scene_baseline();
        return Result::native_failure;
    }
    return Result::accepted;
}

bool native_scene_local_identity(uint64_t epoch, uint32_t native_id, uintptr_t address,
                                 NativeSceneLocalIdentity& result, bool allow_corpse) {
    result = {};
    if (!available() || !native_replica_is_client() || !epoch || admitted_epoch != epoch || !address) {
        return false;
    }
    auto animal = find(native_id, address);
    if (!animal || (animal->mbDead ? !allow_corpse : animal->mHealthPoints <= 0)) {
        return false;
    }
    for (const auto& bound : bindings) {
        if (bound.live && bound.native_id == native_id && bound.address == address) {
            if (result.id) {
                return false;
            }
            result = {bound.id, bound.generation, bound.owner};
        }
    }
    return result.id && result.generation;
}

bool native_scene_local_fossil_identity(uint64_t epoch, uint32_t native_id, uintptr_t address,
                                        NativeSceneLocalIdentity& result) {
    result = {};
    if (!on_thread() || !available() || !native_replica_is_client() || !epoch || admitted_epoch != epoch || !address ||
        !find_fossil(native_id, address)) {
        return false;
    }
    for (const auto& bound : bindings) {
        if (bound.live && bound.kind == EntityKind::fossil && bound.native_id == native_id &&
            bound.address == address) {
            if (result.id || bound.owner) {
                return false;
            }
            result = {bound.id, bound.generation, 0};
        }
    }
    return result.id && result.generation;
}

bool native_scene_collect_fossil(uint64_t epoch, uint64_t entity, uint64_t generation, uint32_t native_id,
                                 Animal* actor, uint32_t actor_native_id) {
    if (!on_thread() || !available() || native_replica_is_client() || !epoch || epoch > UINT32_MAX ||
        capture_epoch != epoch || native_actor_worker_status().epoch != epoch ||
        entity != entity_id(epoch, native_id) || !generation) {
        return false;
    }
    bool ambiguous = false;
    const Seen* incarnation = find_incarnation(native_id, generation, ambiguous);
    if (ambiguous) {
        return false;
    }
    auto resource = incarnation && incarnation->address ? find_fossil(native_id, incarnation->address) : nullptr;
    return resource && native_fossil_order(actor, actor_native_id, resource, native_id);
}

namespace {
// The original rendered camera of this process, after checking the bindings it is read through.
App::cViewer* verified_viewer(D3DVIEWPORT9& viewport) {
    // SDK cbf9206: IGameModeManager::Get (cdecl), virtual GetViewer at 58h
    // (thiscall, no arguments), cViewer::GetCameraToPoint (thiscall, two floats
    // and two Vector3 references, RET 10h). Static export m11-camera-input-20260927.
    // The singleton operand is relocated; compare its loaded RVA, not PE bytes.
    constexpr unsigned char viewer_prefix[] = {0x8b, 0x41, 0x30, 0xc3};
    constexpr unsigned char ray_prefix[] = {0x83, 0xec, 0x0c, 0x8b, 0x81, 0x70, 0x01,
                                            0x00, 0x00, 0x0f, 0xb7, 0x50, 0x78};
    constexpr unsigned char ray_return[] = {0xb0, 0x01, 0x83, 0xc4, 0x0c, 0xc2, 0x10, 0x00};
    if (*reinterpret_cast<const unsigned char*>(image + 0x27dcd0) != 0xa1 ||
        native_word(image + 0x27dcd1) != image + 0x11fd8b0 ||
        *reinterpret_cast<const unsigned char*>(image + 0x27dcd5) != 0xc3 ||
        std::memcmp(reinterpret_cast<const void*>(image + 0x1aadb0), viewer_prefix, sizeof(viewer_prefix)) ||
        std::memcmp(reinterpret_cast<const void*>(image + 0x3c4800), ray_prefix, sizeof(ray_prefix)) ||
        std::memcmp(reinterpret_cast<const void*>(image + 0x3c49c4), ray_return, sizeof(ray_return))) {
        return nullptr;
    }
    auto modes = App::IGameModeManager::Get();
    if (!readable_span(modes, 0x34)) {
        return nullptr;
    }
    auto table = *reinterpret_cast<const uintptr_t* const*>(modes);
    if (!readable_span(table, 0x5c) || table[0x58 / 4] != image + 0x1aadb0) {
        return nullptr;
    }
    auto viewer = modes->GetViewer();
    if (!readable_span(viewer, sizeof(*viewer)) || !readable_span(viewer->pCamera, sizeof(*viewer->pCamera))) {
        return nullptr;
    }
    viewport = viewer->GetViewport();
    if (viewport.Width < 32 || viewport.Height < 32 || viewport.Width > 16384 || viewport.Height > 16384 ||
        viewport.X > 16384 || viewport.Y > 16384) {
        return nullptr;
    }
    return viewer;
}

// The original creature camera, or null. Static export m11-creature-camera (2026-10-07, executable dc04aee5):
// its class table is image+107A2F8h, with OnEnter D26BB0h, OnExit D203E0h and Update D2AEC0h at 18h, 1Ch and 20h.
// Reached through SDK cbf9206 IGameModeManager::GetCameraManager (50h) and ICameraManager::GetActiveCamera
// (38h), both thiscall without arguments and both required to be code of the original image.
unsigned char* creature_camera() {
    const auto code = [](uintptr_t address) {
        MEMORY_BASIC_INFORMATION info{};
        return address && VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) &&
               info.State == MEM_COMMIT && reinterpret_cast<uintptr_t>(info.AllocationBase) == image &&
               (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
    };
    auto modes = App::IGameModeManager::Get();
    if (!readable_span(modes, 4)) {
        return nullptr;
    }
    const auto mode_table = *reinterpret_cast<const uintptr_t* const*>(modes);
    if (!readable_span(mode_table, 0x5c) || !code(mode_table[0x50 / 4])) {
        return nullptr;
    }
    auto cameras = modes->GetCameraManager();
    if (!readable_span(cameras, 4)) {
        return nullptr;
    }
    const auto manager_table = *reinterpret_cast<const uintptr_t* const*>(cameras);
    if (!readable_span(manager_table, 0x60) || !code(manager_table[0x38 / 4])) {
        return nullptr;
    }
    auto camera = reinterpret_cast<unsigned char*>(cameras->GetActiveCamera());
    if (!readable_span(camera, 0x318) || native_word(reinterpret_cast<uintptr_t>(camera)) != image + 0x107a2f8) {
        return nullptr;
    }
    const auto table = reinterpret_cast<const uintptr_t*>(image + 0x107a2f8);
    if (table[0x18 / 4] != image + 0x926bb0 || table[0x1c / 4] != image + 0x9203e0 ||
        table[0x20 / 4] != image + 0x92aec0) {
        return nullptr;
    }
    return camera;
}

// Its mode words, read only (same export, functions D2A630h, D24550h and its message handler D213B0h):
// +1ECh the mode in use (0 free: the reference heading only follows the ground; 1 follow: it turns toward the
// creature's direction at 2 per second; 2 another), +1F0h the mode asked for, +315h set while a free camera is held.
void creature_camera_mode(char* text, size_t size) {
    const auto camera = creature_camera();
    if (!camera) {
        sprintf_s(text, size, "null");
        return;
    }
    int mode = 0, asked = 0;
    std::memcpy(&mode, camera + 0x1ec, 4);
    std::memcpy(&asked, camera + 0x1f0, 4);
    sprintf_s(text, size, "[%d,%d,%u]", mode, asked, unsigned(camera[0x315]));
}

// The original's avatar when exactly one noun of the census is that creature and it is present; null otherwise.
Animal* avatar_actor() {
    auto manager = Manager::Get();
    auto candidate = manager ? manager->GetAvatar() : nullptr;
    if (!candidate) {
        return nullptr;
    }
    Animal* actor = nullptr;
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Noun*>(candidate)) {
            if (actor) {
                return nullptr;
            }
            actor = find(noun.mID, reinterpret_cast<uintptr_t>(candidate));
        }
    }
    return actor;
}

// The original camera's rays through the centre of the view and through a point a quarter of its width to the
// right.
struct SteeringRays {
    Math::Vector3 camera, center, right_camera, right_ray;
};

bool read_steering_rays(App::cViewer* viewer, const D3DVIEWPORT9& viewport, SteeringRays& rays) {
    const float x = float(viewport.X) + float(viewport.Width) * 0.5f,
                y = float(viewport.Y) + float(viewport.Height) * 0.5f;
    if (!viewer->GetCameraToPoint(x, y, rays.camera, rays.center) ||
        !viewer->GetCameraToPoint(x + float(viewport.Width) * 0.25f, y, rays.right_camera, rays.right_ray)) {
        return false;
    }
    for (float value :
         {rays.camera.x, rays.camera.y, rays.camera.z, rays.right_camera.x, rays.right_camera.y, rays.right_camera.z}) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

void report_camera_input(const NativeSceneLocalIdentity& identity, Animal* actor, int horizontal, int forward,
                         int32_t packed, const Math::Vector3& position, const SteeringRays& rays,
                         const network::SteeringVector& direction) {
    // The creature's own orientation rides along (diagnostic): trial273's camera turned with player 2's creature.
    const auto facing = actor->GetOrientation();
    char camera_mode[48]{};
    creature_camera_mode(camera_mode, sizeof(camera_mode));
    char fields[1200]{};
    sprintf_s(fields,
              ",\"owner\":%llu,\"entity\":%llu,\"generation\":%llu,\"native_id\":%u,\"horizontal\":%d,\"forward\":%d,"
              "\"packed\":%d,"
              "\"position\":[%.9g,%.9g,%.9g],\"camera\":[%.9g,%.9g,%.9g],\"center_ray\":[%.9g,%.9g,%.9g],\"right_ray\":"
              "[%.9g,%.9g,%.9g],\"direction\":[%.9g,%.9g,%.9g],"
              "\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"camera_mode\":%s",
              identity.owner, identity.id, identity.generation, actor->mID, horizontal, forward, packed,
              double(position.x), double(position.y), double(position.z), double(rays.camera.x), double(rays.camera.y),
              double(rays.camera.z), double(rays.center.x), double(rays.center.y), double(rays.center.z),
              double(rays.right_ray.x), double(rays.right_ray.y), double(rays.right_ray.z), double(direction.x),
              double(direction.y), double(direction.z), double(facing.x), double(facing.y), double(facing.z),
              double(facing.w), camera_mode);
    native_actor_worker_event("native_camera_input", fields);
}

// The one live binding of an entity; null when there is none or more than one.
const Binding* unique_live_binding(uint64_t id) {
    const Binding* found = nullptr;
    for (const auto& bound : bindings) {
        if (bound.live && bound.id == id) {
            if (found) {
                return nullptr;
            }
            found = &bound;
        }
    }
    return found;
}

// The original camera's rays through the centre of the view and through the points one step right of and below it.
struct ViewRays {
    float centre_x = 0, centre_y = 0, step = 0;
    Math::Vector3 camera, centre, right, down;
};

bool read_view_rays(App::cViewer* viewer, const D3DVIEWPORT9& viewport, ViewRays& rays) {
    rays.centre_x = float(viewport.X) + float(viewport.Width) * 0.5f;
    rays.centre_y = float(viewport.Y) + float(viewport.Height) * 0.5f;
    rays.step = float(viewport.Width) * 0.25f;
    Math::Vector3 other_camera;
    return viewer->GetCameraToPoint(rays.centre_x, rays.centre_y, rays.camera, rays.centre) &&
           viewer->GetCameraToPoint(rays.centre_x + rays.step, rays.centre_y, other_camera, rays.right) &&
           viewer->GetCameraToPoint(rays.centre_x, rays.centre_y + rays.step, other_camera, rays.down);
}

// How far above a creature's feet to aim, and whether that was measured on the creature's own box.
struct Aim {
    unsigned height_percent = 0;
    bool measured = false;
    float raise = 0;
};

// Aim at a chosen height of the creature's own box (SDK cbf9206 cSpatialObject::mLocalExtents, 18h before
// mScale, whose offset the bindings above already pin; model space is z-up). Trial193: half a unit
// above the feet passed over a small creature and the original picked nothing, six times.
Aim creature_aim(Animal* animal, unsigned height_percent) {
    const auto& box = animal->mLocalExtents;
    const float box_height = box.upper.z - box.lower.z;
    Aim aim;
    aim.height_percent = height_percent;
    aim.measured =
        reinterpret_cast<uintptr_t>(&animal->mScale) - reinterpret_cast<uintptr_t>(&animal->mLocalExtents) == 0x2c &&
        std::isfinite(box.lower.z) && std::isfinite(box.upper.z) && box_height > 0.02f && box_height < 60.0f &&
        box.lower.z > -30.0f && box.upper.z < 60.0f;
    aim.raise = aim.measured ? box.lower.z + box_height * float(height_percent) * 0.01f : float(height_percent) * 0.01f;
    return aim;
}

// The Creature HUD covers the view's bottom bar, its top-left cards and its edges (trial173: a point
// 24 pixels above the bottom edge hit the HUD, not the creature). Only the part it leaves free counts.
struct ClickableArea {
    float left = 0, top = 0, right = 0, bottom = 0;
};

ClickableArea clickable_area(const D3DVIEWPORT9& viewport) {
    ClickableArea area;
    area.left = float(viewport.X) + float(viewport.Width) * 0.16f;
    area.right = float(viewport.X) + float(viewport.Width) * 0.97f;
    area.top = float(viewport.Y) + float(viewport.Height) * 0.11f;
    area.bottom = float(viewport.Y) + float(viewport.Height) * 0.86f;
    return area;
}

// Where an entity's aim point is drawn in the window, and whether that is inside the clickable area.
struct ScreenPoint {
    bool projected = false;
    float x = 0, y = 0;
    bool inside = false;
};

void report_screen_point(uint64_t entity, Animal* animal, const Math::Vector3& position, const ViewRays& rays,
                         const Aim& aim, const D3DVIEWPORT9& viewport, const ClickableArea& area,
                         const ScreenPoint& point) {
    const auto& box = animal->mLocalExtents;
    char view_mode[48]{};
    creature_camera_mode(view_mode, sizeof(view_mode));
    char fields[1024]{};
    sprintf_s(fields,
              ",\"entity\":%llu,\"native_id\":%u,\"position\":[%.9g,%.9g,%.9g],\"camera\":[%.9g,%.9g,%.9g],"
              "\"centre_ray\":[%.9g,%.9g,%.9g],\"right_ray\":[%.9g,%.9g,%.9g],\"box_z\":[%.4g,%.4g],\"scale\":%.4g,"
              "\"raise\":%.4g,\"height_percent\":%u,\"box_measured\":%s,"
              "\"viewport\":[%lu,%lu,%lu,%lu],\"clickable\":[%.0f,%.0f,%.0f,%.0f],\"projected\":%s,\"point\":[%.3f,%."
              "3f],\"inside\":%s,\"camera_mode\":%s",
              entity, animal->mID, double(position.x), double(position.y), double(position.z), double(rays.camera.x),
              double(rays.camera.y), double(rays.camera.z), double(rays.centre.x), double(rays.centre.y),
              double(rays.centre.z), double(rays.right.x), double(rays.right.y), double(rays.right.z),
              double(box.lower.z), double(box.upper.z), double(animal->mScale), double(aim.raise), aim.height_percent,
              aim.measured ? "true" : "false", viewport.X, viewport.Y, viewport.Width, viewport.Height,
              double(area.left), double(area.top), double(area.right), double(area.bottom),
              point.projected ? "true" : "false", point.projected ? double(point.x) : 0.0,
              point.projected ? double(point.y) : 0.0, point.inside ? "true" : "false", view_mode);
    native_actor_worker_event("native_screen_point", fields);
}
} // namespace

bool native_scene_camera_input(uint64_t epoch, int horizontal, int forward, int32_t& packed) {
    packed = 0;
    if (!on_thread() || !available() || !native_replica_is_client() || admitted_epoch != epoch) {
        return false;
    }
    D3DVIEWPORT9 viewport{};
    auto viewer = verified_viewer(viewport);
    if (!viewer) {
        return false;
    }
    Animal* actor = avatar_actor();
    NativeSceneLocalIdentity identity{};
    if (!actor || !native_scene_local_identity(epoch, actor->mID, reinterpret_cast<uintptr_t>(actor), identity) ||
        identity.owner != controlled_owner) {
        return false;
    }
    SteeringRays rays;
    if (!read_steering_rays(viewer, viewport, rays)) {
        return false;
    }
    const auto position = actor->GetPosition();
    network::SteeringVector direction;
    if (!network::camera_steering({position.x, position.y, position.z}, {rays.center.x, rays.center.y, rays.center.z},
                                  {rays.right_ray.x, rays.right_ray.y, rays.right_ray.z}, horizontal, forward,
                                  direction) ||
        !network::pack_steering(direction, packed)) {
        return false;
    }
    report_camera_input(identity, actor, horizontal, forward, packed, position, rays, direction);
    return true;
}

bool native_scene_entity_screen_point(uint64_t epoch, uint64_t entity, unsigned height_percent, int& x, int& y) {
    x = y = 0;
    if (!on_thread() || !available() || !native_replica_is_client() || !epoch || admitted_epoch != epoch || !entity ||
        height_percent < 5 || height_percent > 95) {
        return false;
    }
    const Binding* bound = unique_live_binding(entity);
    auto animal = bound ? find(bound->native_id, bound->address) : nullptr;
    if (!animal) {
        return false;
    }
    D3DVIEWPORT9 viewport{};
    auto viewer = verified_viewer(viewport);
    if (!viewer) {
        return false;
    }
    ViewRays rays;
    if (!read_view_rays(viewer, viewport, rays)) {
        return false;
    }
    // Aim above the feet: positions are planet-centred, so the local up is the position's own direction.
    const auto position = animal->GetPosition();
    network::SteeringVector up{position.x, position.y, position.z};
    if (!network::steering_unit(up)) {
        return false;
    }
    const Aim aim = creature_aim(animal, height_percent);
    const network::SteeringVector target{position.x + up.x * aim.raise, position.y + up.y * aim.raise,
                                         position.z + up.z * aim.raise};
    float dx = 0, dy = 0;
    ScreenPoint point;
    point.projected = network::screen_point(
        {rays.camera.x, rays.camera.y, rays.camera.z}, {rays.centre.x, rays.centre.y, rays.centre.z},
        {rays.right.x, rays.right.y, rays.right.z}, {rays.down.x, rays.down.y, rays.down.z}, rays.step, target, dx, dy);
    point.x = rays.centre_x + dx;
    point.y = rays.centre_y + dy;
    const ClickableArea area = clickable_area(viewport);
    point.inside = point.projected && point.x >= area.left && point.y >= area.top && point.x <= area.right &&
                   point.y <= area.bottom;
    report_screen_point(entity, animal, position, rays, aim, viewport, area, point);
    if (!point.inside) {
        return false;
    }
    x = int(std::lround(point.x));
    y = int(std::lround(point.y));
    return true;
}

void reset_native_scene_baseline() {
    if (!on_thread()) {
        return;
    }
    bindings = {};
    admitted_epoch = 0;
    controlled_owner = 0;
    replica_owned.projecting = replica_owned.admitted = false;
    // Presentation keeps only the committed, revalidated local avatar/profile.
    // Network command admission remains cleared; the frozen view may normalize
    // correctly while disconnected and before a fresh authenticated baseline.
}

namespace {
// Ends this epoch's owned projection: the original is destroying or pooling a noun it relies on.
void retire_owned(const char* reason, uint64_t id, uint32_t native_id) {
    replica_owned.retired = true;
    replica_owned.admitted = replica_owned.projecting = replica_owned.presentation = false;
    event("scene_owned_incarnation_retired", reason, id, native_id);
}
} // namespace

void invalidate_native_scene_entity(uint32_t native_id) {
    if (!on_thread()) {
        return;
    }
    if (replica_owned.epoch && replica_owned.player_id == native_id) {
        retire_owned("original_player_destroy_callback", 0, native_id);
    }
    for (unsigned owner = 1; owner <= 2; ++owner) {
        if (replica_owned.actors[owner].live && replica_owned.actors[owner].native_id == native_id) {
            retire_owned("original_destroy_or_pool_callback", replica_owned.actors[owner].id, native_id);
        }
    }
    for (const auto& bound : bindings) {
        if (bound.live && bound.native_id == native_id && bound.kind == EntityKind::fossil) {
            event("scene_fossil_incarnation_retired", "original_lifetime_callback", bound.id, native_id);
        }
    }
    for (auto& seen : observed) {
        if (seen.native_id == native_id) {
            seen = {};
        }
    }
    for (auto& bound : bindings) {
        if (bound.native_id == native_id) {
            bound.live = false;
        }
    }
}

bool preserve_native_scene_fossil(uintptr_t local_address, const char* operation, uint32_t caller_rva) {
    if (!available() || !admitted_epoch || !native_replica_is_client() || !local_address) {
        return false;
    }
    for (auto& bound : bindings) {
        if (bound.live && bound.kind == EntityKind::fossil && bound.address == local_address) {
            if (native_replica_allows(replica::Mutation::apply_state)) {
                return false;
            }
            // Trial75: the original replica lifetime callback retired13480/13481/
            // 14493 while their authority incarnations remained present. Bindings
            // authenticate the borrowed address; never dereference it after disposal.
            if (!bound.fossil_lifetime_fenced) {
                bound.fossil_lifetime_fenced = true;
                char fields[320]{};
                sprintf_s(fields,
                          ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"operation\":\"%s\","
                          "\"caller_rva\":%u",
                          bound.id, bound.generation, bound.native_id, operation, caller_rva);
                native_actor_worker_event("scene_fossil_local_retirement_fenced", fields);
            }
            return true;
        }
    }
    return false;
}

const char* native_scene_result_name(NativeSceneResult result) noexcept {
    constexpr const char* names[]{
        "accepted", "unavailable",        "wrong_thread",     "invalid",          "full",
        "stale",    "unsupported_entity", "binding_mismatch", "missing_template", "native_failure"};
    auto index = static_cast<size_t>(result);
    return index < std::size(names) ? names[index] : "invalid";
}
} // namespace sporemp
