#include "native_life_presentation.h"
#include "native_life_abi.h"
#include "native_actors.h"
#include "native_replica.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Animated = Anim::AnimatedCreature;
using Abi = NativeLifeAbi<Animal, Animated>;
constexpr uint32_t corpse_idle = 0x05807346, living_idle = 0x02481de5;

bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto begin = reinterpret_cast<uintptr_t>(address), region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin >= region && size <= info.RegionSize && begin - region <= info.RegionSize - size;
}

template <size_t N>
bool bytes(uintptr_t image, uint32_t rva, const unsigned char (&expected)[N]) {
    const auto address = reinterpret_cast<const void*>(image + rva);
    return readable(address, N) && !std::memcmp(address, expected, N);
}

// STATIC pinned dc04aee5..., SDK cbf9206. These are original model operations;
// no C08210, C02D00, D8E380, C103B0/C10390 or behavior activation is invoked.
bool code_binding(uintptr_t image) {
    return bytes(image, 0x60c5d0,
                 {0x8b, 0x44, 0x24, 0x08, 0x8b, 0x54, 0x24, 0x04, 0x50, 0x52, 0x51, 0xe8, 0xf0, 0xfb, 0xff, 0xff}) &&
           bytes(image, 0x6029b0, {0x8b, 0x44, 0x24, 0x08, 0x8b, 0x54, 0x24, 0x04, 0x50, 0x8b, 0x81,
                                   0x84, 0x01, 0x00, 0x00, 0x52, 0x50, 0xe8, 0x6a, 0xd7, 0xff, 0xff}) &&
           bytes(image, 0x6029d0,
                 {0x8b, 0x44, 0x24, 0x04, 0x8b, 0x89, 0x84, 0x01, 0x00, 0x00, 0x50, 0x51, 0xe8, 0xaf, 0xf0, 0xff,
                  0xff}) &&
           bytes(image, 0x6028c0, {0x8b, 0x44, 0x24, 0x08, 0x8b, 0x54, 0x24, 0x04, 0x50, 0x8b, 0x81,
                                   0x84, 0x01, 0x00, 0x00, 0x52, 0x50, 0xe8, 0x9a, 0xd6, 0xff, 0xff}) &&
           bytes(image, 0x6028e0, {0x8b, 0x44, 0x24, 0x08, 0x8b, 0x54, 0x24, 0x04, 0x50, 0x8b, 0x81,
                                   0x84, 0x01, 0x00, 0x00, 0x52, 0x50, 0xe8, 0xca, 0xd6, 0xff, 0xff}) &&
           bytes(image, 0x602920, {0x8b, 0x44, 0x24, 0x08, 0x8b, 0x54, 0x24, 0x04, 0x50, 0x8b, 0x81,
                                   0x84, 0x01, 0x00, 0x00, 0x52, 0x50, 0xe8, 0x3a, 0xd7, 0xff, 0xff}) &&
           bytes(image, 0x605090, {0x56, 0x57, 0x8b, 0xf9, 0x8b, 0x87, 0x84, 0x01, 0x00, 0x00}) &&
           bytes(image, 0x602b00,
                 {0x8b, 0x89, 0x84, 0x01, 0x00, 0x00, 0x85, 0xc9, 0x74, 0x05, 0xe9, 0xb1, 0xdd, 0xff, 0xff}) &&
           bytes(image, 0x6048d0, {0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0, 0x74, 0x12}) &&
           bytes(image, 0x80c710, {0x56, 0xe8, 0xfa, 0xf1, 0xf4, 0xff, 0x3d, 0x10, 0x4c, 0x65, 0x01});
}

bool model_binding(Animal* animal, uintptr_t image, Animated*& animated) {
    animated = nullptr;
    auto manager = Simulator::cGameNounManager::Get();
    if (!animal || !manager || Simulator::GetGameModeID() != kGameCreature) {
        return false;
    }
    bool found = false;
    for (auto& noun : manager->mNouns) {
        if (&noun == static_cast<Simulator::cGameData*>(animal)) {
            found = !noun.mbIsDestroyed && !noun.field_20;
            break;
        }
    }
    if (!found || !readable(animal, sizeof(Animal)) || animal->mbMarkedForDeletion ||
        *reinterpret_cast<const uintptr_t*>(animal) != image + 0x106a080) {
        return false;
    }
    animated = animal->mpAnimatedCreature.get();
    if (!readable(animated, sizeof(Animated))) {
        return false;
    }
    const auto table = *reinterpret_cast<const uintptr_t* const*>(animated);
    if (reinterpret_cast<uintptr_t>(table) != image + 0x1048af0 || table[0x08 / 4] != image + 0x60c5d0 ||
        table[0x0c / 4] != image + 0x6029b0 || table[0x18 / 4] != image + 0x6029d0 ||
        table[0x3c / 4] != image + 0x6028c0 || table[0x40 / 4] != image + 0x6028e0 ||
        table[0x48 / 4] != image + 0x602920 || table[0x58 / 4] != image + 0x605090) {
        return false;
    }
    auto queue = reinterpret_cast<const Anim::anim_qb*>(animated->field_184);
    if (!readable(queue, sizeof(*queue)) || !readable(animated->p_cid, sizeof(Anim::anim_cid)) ||
        animated->p_cid->pCreature.get() != animated || !readable(animated->mpAnimWorld, 0x20)) {
        return false;
    }
    // A0C1D0 resolves the original animation resource manager through +190/+1C.
    const auto world = reinterpret_cast<const unsigned char*>(animated->mpAnimWorld);
    const auto resource = *reinterpret_cast<const void* const*>(world + 0x1c);
    if (!readable(resource, 0x18)) {
        return false;
    }
    // Original queue clear A008C0 uses this fixed sixteen-element index array.
    for (int value : queue->field_F4C) {
        if (value < -1 || value >= 16) {
            return false;
        }
    }
    return true;
}

void read_current(Animated* animated, uint32_t& animation, int& index) {
    animation = 0;
    index = 0;
    // A05090 is RET 10h and has no meaningful return value. Do not interpret
    // the SDK's inferred int result as success; only initialized outputs count.
    const auto table = *reinterpret_cast<const uintptr_t* const*>(animated);
    reinterpret_cast<Abi::Read>(table[0x58 / 4])(animated, &animation, nullptr, nullptr, &index);
}

bool rejected(uint64_t remote_entity, uint64_t generation, bool dead, const char* reason) {
    char fields[320]{};
    sprintf_s(fields, ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"dead\":%s,\"reason\":\"%s\"", remote_entity,
              generation, dead ? "true" : "false", reason);
    native_actor_worker_event("native_life_presentation_rejected", fields);
    return false;
}

// Why the presentation has no admitted entity, code or model to work on, or null when it has. The image and the
// animated model are outputs; the image is only looked up once the application scope is admitted.
const char* binding_refusal(Animal* animal, uint64_t remote_entity, uint64_t generation, uintptr_t& image,
                            Animated*& animated) {
    if (!native_replica_is_client() || !native_replica_allows(replica::Mutation::apply_state)) {
        return "application_scope";
    }
    image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!remote_entity || !generation) {
        return "entity_identity";
    }
    if (!image || !code_binding(image)) {
        return "code_binding";
    }
    if (!model_binding(animal, image, animated)) {
        return "model_lifetime_binding";
    }
    return nullptr;
}

// Why the creature's own state does not admit the presentation, or null when it does.
const char* state_refusal(Animal* animal, bool dead, uintptr_t image) {
    // D8E4E0 skips the original corpse animation while this native byte is set.
    if (dead && animal->field_166D) {
        return "native_animation_suppressed";
    }
    if (!native_life_state_admitted(dead, animal->mbDead, animal->mHealthPoints,
                                    static_cast<Simulator::cCombatant*>(animal)->field_34)) {
        return "life_state_mismatch";
    }
    // C0C240 and C0C710 read the current noun's archetype metadata. The special
    // epic branch selects a different corpse resource and is outside this slice.
    const auto archetype = reinterpret_cast<const unsigned char*>(animal->field_E84);
    const auto configuration = *reinterpret_cast<const void* const*>(image + 0x128d900);
    if (!readable(archetype, 0x389)) {
        return "archetype_metadata";
    }
    if (archetype[0x388]) {
        return "alternate_corpse_style_unsupported";
    }
    if (!readable(configuration, 0x20)) {
        return "animation_configuration";
    }
    return nullptr;
}

// Original revival C122F0 records A048D0 before clearing the model queue.
// Clearing A02B00 -> A008C0 -> A003D0 releases animation resources and
// resets query data, including native visual/audio cleanup. It does not
// dispatch the query's creature/behavior callback at +DC.
// Returns the animation mode to give the idle that follows; a corpse keeps its queue and takes mode 1.
int clear_for_revival(Animated* animated, uintptr_t image, bool dead) {
    int mode = 1;
    if (!dead) {
        mode = reinterpret_cast<Abi::HasCurrentMode>(image + 0x6048d0)(animated, 0) ? 1 : 0;
        reinterpret_cast<Abi::Clear>(image + 0x602b00)(animated);
    }
    return mode;
}

// What was asked of the model and what it answered.
struct Presented {
    uint32_t animation = 0, mapped = 0;
    int index = 0, mode = 0;
    bool callback_cleared = false, started = false;
};

void record_presentation(Animal* animal, Animated* animated, uint64_t remote_entity, uint64_t generation, bool dead,
                         const Presented& presented) {
    uint32_t observed = 0;
    int observed_index = 0;
    read_current(animated, observed, observed_index);
    char fields[640]{};
    sprintf_s(fields,
              ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"native_id\":%u,\"dead\":%s,"
              "\"animation\":%u,\"mapped_animation\":%u,\"animation_index\":%d,\"observed_animation\":%u,\"observed_"
              "index\":%d,"
              "\"mode\":%d,\"callback_cleared\":%s,\"started\":%s,\"model_core_only\":true,\"visual_acceptance\":\"NOT_"
              "INFERRED\"",
              remote_entity, generation, animal->mID, dead ? "true" : "false", presented.animation, presented.mapped,
              presented.index, observed, observed_index, presented.mode, presented.callback_cleared ? "true" : "false",
              presented.started ? "true" : "false");
    native_actor_worker_event("native_life_presentation", fields);
}
} // namespace

bool native_life_read_animation(Animal* animal, uint32_t& animation, int& index) {
    animation = 0;
    index = 0;
    if (!native_replica_is_client() || !native_replica_allows(replica::Mutation::presentation)) {
        return false;
    }
    const auto image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Animated* animated = nullptr;
    if (!image || !code_binding(image) || !model_binding(animal, image, animated)) {
        return false;
    }
    read_current(animated, animation, index);
    return true;
}

bool native_life_present(Animal* animal, bool dead, uint64_t remote_entity, uint64_t generation) {
    const auto refuse = [&](const char* reason) {
        return rejected(remote_entity, generation, dead, reason);
    };
    uintptr_t image = 0;
    Animated* animated = nullptr;
    if (const char* reason = binding_refusal(animal, remote_entity, generation, image, animated)) {
        return refuse(reason);
    }
    if (const char* reason = state_refusal(animal, dead, image)) {
        return refuse(reason);
    }
    const auto original = dead ? corpse_idle : living_idle;
    uint32_t mapped = original;
    reinterpret_cast<Abi::Map>(image + 0x80c710)(animal, original, &mapped);
    if (!mapped) {
        return refuse("animation_mapping");
    }
    const int mode = clear_for_revival(animated, image, dead);
    const auto index = animated->LoadAnimation(mapped, nullptr);
    if (!index || (static_cast<unsigned>(index) & 0xff) < 1 || (static_cast<unsigned>(index) & 0xff) > 16) {
        return refuse("animation_load");
    }
    // Native resource loading configures loops/blending. Preserve those values.
    // Clear the gameplay callback before StartAnimation, then use precisely the
    // model operations in C12470. Do not register C103B0's behavior callbacks.
    const bool callback = animated->func48h(index, 0);
    const bool configured = animated->SetAnimationMode(index, mode) && animated->SetAnimationID(index, original) &&
                            animated->func3Ch(index, 0);
    const bool started = callback && configured && animated->StartAnimation(index);
    record_presentation(animal, animated, remote_entity, generation, dead,
                        {original, mapped, index, mode, callback, started});
    return started && animal->mpAnimatedCreature.get() == animated;
}
} // namespace sporemp
