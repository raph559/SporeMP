#include "native_pickup.h"
#include "native_replica.h"
#include "native_network.h"
#include "native_branch.h"
#include "../network/steering.h"
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Spore/Simulator/cInteractiveOrnament.h>
#include <Spore/Graphics/Model.h>
#include <Spore/Graphics/IModelWorld.h>
#include "../network/protocol.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace sporemp {
namespace {
using Animal = Simulator::cCreatureAnimal;
using Noun = Simulator::cGameData;
using Manager = Simulator::cGameNounManager;
// C0C070: primary Animal ECX; eleven 32-bit argument slots; RET 2Ch.
// EAX retains the original order-record address, but this leaf never retains it.
using Order = NativePickupAbi<Animal>::Order;
uintptr_t image_base = 0, image_end = 0;
DWORD engine_thread = 0;
PlayerContextEvent sink = nullptr;
bool (*thread_check)() = nullptr;
Order order_original = nullptr;
// B0B330: the routine that stocks plants with fruit, thiscall on the object at 167EB34, two stack words, RET 8
// (static export c02-fruit-03).
void(__thiscall* stock_original)(void*, uint32_t, uint32_t) = nullptr;
// C3FA10: the call the stocking routine makes on a fruit it shows, thiscall on the fruit with (1, 0), RET 8 (static
// export c02-fruit-05): it sets the fruit's byte at +12Dh and passes it on to the model's world.
void(__thiscall* fruit_shown_original)(void*, uint32_t, uint32_t) = nullptr;
// B05800: a fruit's holder, thiscall on the fruit, no stack words (static export c02-fruit-01).
void*(__thiscall* fruit_holder_original)(void*) = nullptr;
NativePickupAbi<Animal>::Input input_original = nullptr;
NativePickupAbi<Animal>::Classify classify_original = nullptr;
uint64_t active_input_call = 0;
uint64_t last_ornament_sample = 0;
unsigned ornament_samples = 0;
NativePickupTick tick_original = nullptr;
NativePickupDispatch tick_dispatch = nullptr;
bool (*classify_first_feed)() = nullptr;
bool (*honor_claim)() = nullptr;
void* first_feed_original = nullptr;
void* first_feed_resume = nullptr;
void* claim_original = nullptr;
void* claim_resume = nullptr;

bool on_thread() {
    return engine_thread && GetCurrentThreadId() == engine_thread && thread_check && thread_check();
}

bool __cdecl retain_first_feed_classification() {
    return !on_thread() || !classify_first_feed || !classify_first_feed();
}

bool __cdecl retain_claim_classification() {
    return !on_thread() || !honor_claim || !honor_claim();
}
// At D71367 the native animation return has already been stored at state+0C.
// D71378 begins with a fresh corpse load then CMP eaten; skipped MOV/SHR/TEST/JZ
// changes no stack or live local and neither EAX nor prior flags is read there.
SPOREMP_NATIVE_BRANCH(first_feed_hook, retain_first_feed_classification, first_feed_original, first_feed_resume)
// At D7106E EBP already contains corpse+B50 and ECX points to that native smart
// pointer. D7107B TEST EBP/JNZ returns original false without replacing it. No
// manual refcount/claim change and no stack adjustment is bypassed.
SPOREMP_NATIVE_BRANCH(claim_hook, retain_claim_classification, claim_original, claim_resume)

bool __cdecl tick_hook(Animal* actor, double clock, uint32_t flags, uintptr_t parameter, void* state, void* activation,
                       float delta) {
    // Root applies the immutable replica mutation fence before its thread
    // check. A foreign callback must not bypass that fence through this leaf.
    if (!tick_dispatch) {
        return tick_original(actor, clock, flags, parameter, state, activation, delta);
    }
    return tick_dispatch(tick_original, actor, clock, flags, parameter, state, activation, delta);
}

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
bool bytes(uint32_t rva, const unsigned char (&expected)[N]) {
    if (!image_base || image_end <= image_base || rva > image_end - image_base || N > image_end - image_base - rva) {
        return false;
    }
    const auto address = reinterpret_cast<const void*>(image_base + rva);
    return readable(address, N) && !std::memcmp(address, expected, N);
}

Animal* current(void* borrowed, uint32_t expected_native_id = UINT32_MAX) {
    auto manager = Manager::Get();
    if (!borrowed || !manager || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (static_cast<void*>(&noun) == borrowed) {
            if (noun.GetNounID() != Animal::NOUN_ID) {
                return nullptr;
            }
            auto candidate = static_cast<Animal*>(&noun);
            if (!readable(candidate, sizeof(Animal)) || noun.mbIsDestroyed || noun.field_20 ||
                candidate->mbMarkedForDeletion || !candidate->mbEnabled ||
                *reinterpret_cast<const uintptr_t*>(candidate) != image_base + 0x106a080 ||
                (expected_native_id != UINT32_MAX && noun.mID != expected_native_id)) {
                return nullptr;
            }
            return candidate;
        }
    }
    return nullptr;
}

Simulator::cInteractiveOrnament* current_fossil(void* borrowed, uint32_t expected_native_id) {
    auto manager = Manager::Get();
    if (!borrowed || !manager || Simulator::GetGameModeID() != kGameCreature) {
        return nullptr;
    }
    for (auto& noun : manager->mNouns) {
        if (static_cast<void*>(&noun) == borrowed) {
            if (noun.mbIsDestroyed || noun.field_20 || noun.mID != expected_native_id ||
                noun.GetNounID() != Simulator::cInteractiveOrnament::NOUN_ID ||
                !readable(&noun, sizeof(Simulator::cInteractiveOrnament))) {
                return nullptr;
            }
            auto resource = static_cast<Simulator::cInteractiveOrnament*>(&noun);
            return resource->mType == Simulator::kOrnamentBone && resource->mbPickable && resource->mbIsTangible &&
                           resource->mbEnabled && !resource->mpWhoIsInteractingWithMe
                       ? resource
                       : nullptr;
        }
    }
    return nullptr;
}

uint32_t __cdecl classify_hook(void* picked) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint64_t call = on_thread() && caller == image_base + 0x932fe3 ? active_input_call : 0;
    // Observe the decision already requested by the original input handler.
    // Never call classification a second time, retain picked, or inspect it.
    const uint32_t result = classify_original(picked);
    if (call && sink) {
        char fields[160]{};
        sprintf_s(fields, ",\"call\":%llu,\"classification\":%u,\"picked_present\":%s,\"observation_only\":true", call,
                  result, picked ? "true" : "false");
        sink("native_pickup_classification_observed", fields);
    }
    return result;
}

// What a click picked, resolved only by comparing the borrowed object with the current census: neither opaque
// argument of the handler is dereferenced or retained.
struct Click {
    void* picked = nullptr;
    // The picked noun, the creature it is if it is one, and the avatar of this game.
    Noun* noun = nullptr;
    Animal* creature = nullptr;
    Animal* actor = nullptr;
    uint32_t caller_rva = 0;
};

Click resolve_click(void* picked, uintptr_t caller) {
    Click click;
    click.picked = picked;
    click.caller_rva = static_cast<uint32_t>(caller - image_base);
    auto manager = Manager::Get();
    if (manager) {
        for (auto& noun : manager->mNouns) {
            if (static_cast<void*>(&noun) == picked && !noun.mbIsDestroyed && !noun.field_20) {
                click.noun = &noun;
            }
        }
        for (auto& noun : manager->mNouns) {
            if (static_cast<void*>(&noun) == picked && noun.GetNounID() == Animal::NOUN_ID) {
                click.creature = current(static_cast<Animal*>(&noun));
            }
        }
    }
    click.actor = manager ? current(manager->GetAvatar()) : nullptr;
    return click;
}

// A client's click on a resource is consumed before any replica-local order, claim or reward: only the authority
// runs the original interaction.
bool client_consumes_clicks() {
    return native_replica_is_client() && !native_replica_bootstrap_open();
}

bool can_act(const Animal* actor) {
    return actor && !actor->mbDead && actor->mHealthPoints > 0;
}

void record_forwarded(const char* name, const char* target_field, const Animal* actor, uint32_t target_native_id,
                      bool sent) {
    if (!sink) {
        return;
    }
    char fields[192]{};
    sprintf_s(fields, ",\"actor_native_id\":%u,\"%s\":%u,\"sent\":%s,\"local_order_written\":false",
              actor ? actor->mID : UINT32_MAX, target_field, target_native_id, sent ? "true" : "false");
    sink(name, fields);
}

// The original type-2 resource route. An unavailable or stale mapping also stays consumed.
bool forward_fossil_click(const Click& click) {
    if (!client_consumes_clicks() || !click.noun ||
        click.noun->GetNounID() != Simulator::cInteractiveOrnament::NOUN_ID ||
        !readable(click.noun, sizeof(Simulator::cInteractiveOrnament)) ||
        static_cast<Simulator::cInteractiveOrnament*>(click.noun)->mType != Simulator::kOrnamentBone) {
        return false;
    }
    const bool sent =
        can_act(click.actor) && current_fossil(click.picked, click.noun->mID) &&
        native_network_fossil_input(click.actor->mID, reinterpret_cast<uintptr_t>(click.actor), click.noun->mID,
                                    reinterpret_cast<uintptr_t>(click.noun), click.caller_rva);
    record_forwarded("native_fossil_ui_forwarded", "target_native_id", click.actor, click.noun->mID, sent);
    return true;
}

// Trial364: the original picks the fruit itself (noun kFruit, classification 23) and writes its order on the
// client's copy of the creature, where nothing runs it. The click is consumed; the authority gives the same order to
// the creature it simulates.
bool forward_fruit_click(const Click& click) {
    if (!client_consumes_clicks() || !click.noun || click.noun->GetNounID() != Simulator::kFruit) {
        return false;
    }
    const auto seen = object_cast<Simulator::cSpatialObject>(click.noun);
    const float place[3]{seen ? seen->mPosition.x : 0, seen ? seen->mPosition.y : 0, seen ? seen->mPosition.z : 0};
    const bool sent = can_act(click.actor) && seen && readable(seen, sizeof(Simulator::cSpatialObject)) &&
                      native_network_plant_food_input(click.actor->mID, reinterpret_cast<uintptr_t>(click.actor),
                                                      click.noun->mID, network::pack_place(place), click.caller_rva);
    record_forwarded("native_plant_food_ui_forwarded", "fruit_native_id", click.actor, click.noun->mID, sent);
    return true;
}

// Trial57 observes the original corpse click through this exact void dispatch and C032F0/C0C070. It is consumed
// before the local order and the global-avatar posse fanout; only the authority executes native eating.
bool forward_corpse_click(const Click& click) {
    const Animal* corpse = click.creature;
    if (!client_consumes_clicks() || !corpse || !corpse->mbDead) {
        return false;
    }
    const bool sent = can_act(click.actor) && std::isfinite(corpse->mFoodValue) && corpse->mFoodValue > 0 &&
                      native_network_pickup_input(click.actor->mID, reinterpret_cast<uintptr_t>(click.actor),
                                                  corpse->mID, reinterpret_cast<uintptr_t>(corpse), click.caller_rva);
    record_forwarded("native_pickup_ui_forwarded", "target_native_id", click.actor, corpse->mID, sent);
    return true;
}

// Pinned SDK cInteractiveOrnament/cSpatialObject layout. Reads only a live census object of this exact noun type; no
// native call or flag change is used to make a visible ornament collectible.
void record_ornament_state(uint64_t call, Noun* noun) {
    if (noun->GetNounID() != Simulator::cInteractiveOrnament::NOUN_ID ||
        !readable(noun, sizeof(Simulator::cInteractiveOrnament))) {
        return;
    }
    auto ornament = static_cast<Simulator::cInteractiveOrnament*>(noun);
    auto spatial = static_cast<Simulator::cSpatialObject*>(ornament);
    char detail[384]{};
    sprintf_s(detail,
              ",\"call\":%llu,\"native_id\":%u,\"ornament_type\":%d,\"spatial_flags\":%u,"
              "\"ornament_flags\":%u,\"tangible\":%s,\"enabled\":%s,\"pickable\":%s,"
              "\"claimed\":%s,\"observation_only\":true",
              call, noun->mID, ornament->mType, spatial->mFlags, static_cast<unsigned>(ornament->mFlags),
              spatial->mbIsTangible ? "true" : "false", spatial->mbEnabled ? "true" : "false",
              spatial->mbPickable ? "true" : "false", ornament->mpWhoIsInteractingWithMe ? "true" : "false");
    sink("native_ornament_input_state", detail);
}

// Records what one of the first 512 clicks picked, and returns its number; 0 when it is not recorded.
uint64_t record_click(const Click& click) {
    static uint64_t count = 0;
    if (count >= 512 || !sink) {
        return 0;
    }
    const uint64_t call = ++count;
    if (click.noun) {
        char object_fields[192]{};
        sprintf_s(object_fields,
                  ",\"call\":%llu,\"native_id\":%u,\"noun_id\":%u,\"replica\":%s,\"observation_only\":true", call,
                  click.noun->mID, click.noun->GetNounID(), native_replica_is_client() ? "true" : "false");
        sink("native_pickup_object_observed", object_fields);
        record_ornament_state(call, click.noun);
    }
    const Animal* target = click.creature;
    char fields[320]{};
    sprintf_s(fields,
              ",\"call\":%llu,\"caller_rva\":%u,\"replica\":%s,\"actor_native_id\":%u,"
              "\"picked_present\":%s,\"target_resolved\":%s,\"target_native_id\":%u,\"target_dead\":%s",
              call, click.caller_rva, native_replica_is_client() ? "true" : "false",
              click.actor ? click.actor->mID : UINT32_MAX, click.picked ? "true" : "false", target ? "true" : "false",
              target ? target->mID : UINT32_MAX, target && target->mbDead ? "true" : "false");
    sink("native_pickup_ui_observed", fields);
    return call;
}

// Observes the pinned original message-to-interaction route, and on a client forwards a click on a fossil, a fruit
// or a corpse to the authority instead of letting the original act on the client's copy.
void __fastcall input_hook(void* strategy, void*, void* picked) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    uint64_t call = 0;
    if (on_thread() && caller == image_base + 0x9356ab) {
        const Click click = resolve_click(picked, caller);
        if (forward_fossil_click(click) || forward_fruit_click(click) || forward_corpse_click(click)) {
            return;
        }
        call = record_click(click);
    }
    const bool engine_call = on_thread();
    const uint64_t previous = engine_call ? active_input_call : 0;
    if (engine_call) {
        active_input_call = call;
    }
    input_original(strategy, picked);
    if (engine_call) {
        active_input_call = previous;
    }
    if (call) {
        char fields[96]{};
        sprintf_s(fields, ",\"call\":%llu,\"forwarded_unchanged\":true", call);
        sink("native_pickup_ui_return", fields);
    }
}

bool original_order_present(Animal* actor, Animal* corpse) {
    const auto tree = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(actor->field_B4C));
    if (!readable(tree, 0x1d0)) {
        return false;
    }
    for (unsigned i = 0; i < 8; ++i) {
        const auto entry = tree + 8 + i * 0x38;
        uint32_t mask = 0, extra = 0;
        Animal* target = nullptr;
        std::memcpy(&mask, entry, 4);
        std::memcpy(&extra, entry + 4, 4);
        std::memcpy(&target, entry + 0x2c, 4);
        if (mask == 0x40000 && extra == 0 && target == corpse) {
            return true;
        }
    }
    return false;
}

void* __fastcall order_hook(Animal* actor, void*, uint32_t mask, uint32_t extra, uint32_t clear_mask,
                            uint32_t clear_extra, float priority, uint32_t prerequisite, uint32_t prerequisite_extra,
                            uint32_t field_0c, uint32_t field_10, void* target, bool replace) {
    // C0C070 is already byte/ABI-qualified for the corpse-order adapter. Here
    // observe original social/corpse requests; forward every argument unchanged.
    // Never retain or serialize the native order/receiver/target addresses.
    static uint64_t count = 0;
    const auto return_address = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool corpse_order = mask == 0x40000 && extra == 0;
    const bool selected = corpse_order || (mask == 0x10000 && extra == 0) || (mask == 0 && extra == 0x10) ||
                          (mask == 0 && extra == 0x40000) || (mask == 0x20 && extra == 0);
    const bool observe = selected && GetCurrentThreadId() == engine_thread && on_thread() && count < 512 &&
                         return_address >= image_base && return_address < image_end && current(actor) &&
                         (actor->mGeneralFlags & 0x200) && sink && std::isfinite(priority);
    uint64_t call = 0;
    if (observe) {
        call = ++count;
        const auto resolved_target = current(target);
        // These social layouts use +0c as a small kind/ability index and
        // +10 as an integer option. Do not emit unknown opaque argument words.
        const bool scalars_valid = (field_0c < 88 || field_0c == UINT32_MAX) && field_10 <= 1;
        char fields[768]{};
        sprintf_s(fields,
                  ",\"call\":%llu,\"caller_rva\":%u,\"actor_native_id\":%u,"
                  "\"target_native_id\":%u,\"target_present\":%s,\"target_resolved\":%s,"
                  "\"mask\":%u,\"extra\":%u,\"clear_mask\":%u,\"clear_extra\":%u,\"priority\":%.9g,"
                  "\"prerequisite\":%u,\"prerequisite_extra\":%u,\"scalars_valid\":%s,\"field_0c\":%u,\"field_10\":%u,"
                  "\"replace\":%s",
                  call, static_cast<uint32_t>(return_address - image_base), actor->mID,
                  resolved_target ? resolved_target->mID : UINT32_MAX, target ? "true" : "false",
                  resolved_target ? "true" : "false", mask, extra, clear_mask, clear_extra, double(priority),
                  prerequisite, prerequisite_extra, scalars_valid ? "true" : "false",
                  scalars_valid ? field_0c : UINT32_MAX, scalars_valid ? field_10 : UINT32_MAX,
                  replace ? "true" : "false");
        sink(corpse_order ? "native_pickup_order_enter" : "native_social_order_enter", fields);
    }
    void* result = order_original(actor, mask, extra, clear_mask, clear_extra, priority, prerequisite,
                                  prerequisite_extra, field_0c, field_10, target, replace);
    if (call) {
        char fields[128]{};
        sprintf_s(fields, ",\"call\":%llu,\"order_returned\":%s", call, result ? "true" : "false");
        sink(corpse_order ? "native_pickup_order_return" : "native_social_order_return", fields);
    }
    return result;
}

void event(const char* name, uint32_t actor, uint32_t corpse, bool queued) {
    if (!sink) {
        return;
    }
    char fields[256]{};
    sprintf_s(fields, ",\"actor_native_id\":%u,\"corpse_native_id\":%u,\"order_present\":%s,\"reward_inferred\":false",
              actor, corpse, queued ? "true" : "false");
    sink(name, fields);
}
} // namespace

bool prepare_native_pickup(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event_sink, bool (*check)(),
                           NativePickupDispatch dispatch, bool (*first_feed_owner)(), bool (*honor_owned_claim)()) {
    order_original = nullptr;
    fruit_holder_original = nullptr;
    stock_original = nullptr;
    fruit_shown_original = nullptr;
    tick_original = nullptr;
    input_original = nullptr;
    classify_original = nullptr;
    active_input_call = 0;
    last_ornament_sample = 0;
    ornament_samples = 0;
    first_feed_original = nullptr;
    first_feed_resume = nullptr;
    claim_original = nullptr;
    claim_resume = nullptr;
    tick_dispatch = nullptr;
    classify_first_feed = nullptr;
    honor_claim = nullptr;
    image_base = base;
    image_end = end;
    engine_thread = thread;
    sink = event_sink;
    thread_check = check;
    // STATIC dc04aee5... / SDK cbf9206b9a823f0911cd9be0217104a49d72380b.
    // m07-corpse-click/C0C070 and m07-pickup-dispatch/D70FD0 instruction exports;
    // BC9160 caller corroboration is in m07-pickup-abi. No absolute relocation
    // lies within these guarded instruction spans. Native execution is separate.
    if (!thread || GetCurrentThreadId() != thread || !check || !dispatch || !first_feed_owner || !honor_owned_claim ||
        !bytes(0x932fd0, {0x83, 0xec, 0x28, 0x53, 0x57, 0x8b, 0x7c, 0x24, 0x34, 0x57, 0x89, 0x4c, 0x24, 0x0c, 0xe8,
                          0x4d, 0xd8, 0xff, 0xff}) ||
        !bytes(0x932fe3, {0x8b, 0xd8, 0x83, 0xc4, 0x04}) ||
        !bytes(0x930830, {0x51, 0xe8, 0x9a, 0xcd, 0xe0, 0xff, 0x8b, 0x40, 0x2c, 0x83, 0xf8, 0x01, 0x74, 0x05}) ||
        !bytes(0x930971, {0xc6, 0x44, 0x24, 0x0b, 0x00, 0xe8, 0x55, 0xf6, 0xff, 0xff, 0x83, 0xc4, 0x08}) ||
        !bytes(0x930985, {0xb8, 0x1c, 0x00, 0x00, 0x00, 0x59, 0xc3}) ||
        !bytes(0x93383c, {0x5f, 0x5b, 0x83, 0xc4, 0x28, 0xc2, 0x04, 0x00}) ||
        !bytes(0x933697, {0x8b, 0x16, 0x8b, 0x82, 0x84, 0,    0, 0,    0x6a, 0, 0x6a, 0, 0x6a,
                          0,    0x8b, 0xce, 0xff, 0xd0, 0x6a, 0, 0x57, 0x6a, 0, 0x6a, 5}) ||
        !bytes(0x93569e, {0x8b, 0x4c, 0x24, 0x10, 0x50, 0x83, 0xc1, 0xb8, 0xe8, 0x25, 0xd9, 0xff, 0xff, 0xb0, 0x01}) ||
        !bytes(0x80c070, {0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x55, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7c, 0x24, 0x14}) ||
        !bytes(0x80c159, {0x8b, 0x4c, 0x24, 0x30, 0x8b, 0x54, 0x24, 0x34, 0x5f, 0x5e, 0x5d,
                          0x89, 0x48, 0x0c, 0x89, 0x50, 0x10, 0x5b, 0xc2, 0x2c, 0x00}) ||
        !bytes(native_pickup_tick_rva,
               {0x83, 0xec, 0x60, 0x53, 0x57, 0x8b, 0xbc, 0x24, 0x80, 0x00, 0x00, 0x00, 0x8b, 0x47, 0x04}) ||
        !bytes(0x971398, {0xc6, 0x82, 0x5f, 0x0b, 0x00, 0x00, 0x01, 0xc6, 0x47, 0x08, 0x01}) ||
        !bytes(0x9713ad, {0xe8, 0xee, 0xd4, 0xfb, 0xff, 0x8b, 0x03, 0x83, 0xc4, 0x04}) ||
        !bytes(0x9713b7, {0x8d, 0x4c, 0x24, 0x58, 0x51}) ||
        !bytes(0x9713c2, {0x68, 0x2d, 0x36, 0x35, 0xd3, 0x89, 0x74, 0x24, 0x60, 0x89, 0x44, 0x24, 0x64,
                          0xc7, 0x44, 0x24, 0x68, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x84, 0x7f, 0xfc, 0xff}) ||
        !bytes(0x971367, {0x8b, 0x86, 0x58, 0x0b, 0x00, 0x00, 0xc1, 0xe8, 0x09, 0xa8, 0x01, 0x0f, 0x84, 0xaa, 0x02,
                          0x00, 0x00}) ||
        !bytes(0x971378, {0x8b, 0x0b, 0x80, 0xb9, 0x5f, 0x0b, 0x00, 0x00, 0x00}) ||
        !bytes(0x97105c, {0x8b, 0x03, 0x8b, 0xa8, 0x50, 0x0b, 0x00, 0x00, 0x8d, 0x88, 0x50, 0x0b, 0x00, 0x00, 0x3b,
                          0xee, 0x74, 0x1b}) ||
        !bytes(0x97106e, {0x8b, 0x86, 0x58, 0x0b, 0x00, 0x00, 0xc1, 0xe8, 0x09, 0xa8, 0x01, 0x75, 0x08}) ||
        !bytes(0x97107b, {0x85, 0xed, 0x0f, 0x85, 0x80, 0x05, 0x00, 0x00}) ||
        !bytes(0x7c9477, {0xff, 0xd1, 0x83, 0xc4, 0x20, 0x84, 0xc0}) ||
        !bytes(0x7c969e, {0xff, 0xd1, 0x83, 0xc4, 0x20, 0x84, 0xc0}) || !bytes(0x107a540, {0xff, 0xff, 0x7f, 0x7f}) ||
        // C02 plant food: the handler's fruit case and the holder routine it calls.
        !bytes(0x9335cc,
               {0x83, 0xbf, 0x6c, 0x01, 0x00, 0x00, 0x00, 0x75, 0x52, 0x8b, 0xcf, 0xe8, 0x24, 0x22, 0xdd, 0xff}) ||
        !bytes(0x933600, {0x8b, 0x8f, 0x64, 0x01, 0x00, 0x00, 0x57, 0x6a, 0x00, 0x89, 0x48,
                          0x10, 0x6a, 0x20, 0xc6, 0x86, 0x5c, 0x0b, 0x00, 0x00, 0x00}) ||
        !bytes(0x705800, {0x53, 0x8b, 0x99, 0x60, 0x01, 0x00, 0x00, 0x56, 0x8b, 0xb1, 0x58,
                          0x01, 0x00, 0x00, 0x57, 0x8b, 0xb9, 0x5c, 0x01, 0x00, 0x00}) ||
        !bytes(0x70b330, {0x81, 0xec, 0xcc, 0x04, 0x00, 0x00, 0x53, 0x55, 0x56, 0x57, 0x8b, 0xf1, 0x6a, 0x00}) ||
        !bytes(0x70baaa, {0xc2, 0x08, 0x00}) || !bytes(0x70b7cb, {0x83, 0x48, 0x04, 0x01}) ||
        !bytes(0x70b7ed, {0x6a, 0x00, 0x6a, 0x01, 0x8b, 0xce, 0xe8, 0x18, 0x42, 0x13, 0x00}) ||
        !bytes(0x83fa10, {0x53, 0x8b, 0x5c, 0x24, 0x08, 0x3a, 0x99, 0x2d, 0x01, 0x00, 0x00, 0x75, 0x07, 0x80, 0x7c,
                          0x24, 0x0c, 0x00})) {
        if (sink) {
            sink("pickup_binding_rejected", "");
        }
        return false;
    }
    order_original = reinterpret_cast<Order>(base + 0x80c070);
    fruit_holder_original = reinterpret_cast<void*(__thiscall*)(void*)>(base + 0x705800);
    stock_original = reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(base + 0x70b330);
    fruit_shown_original = reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(base + 0x83fa10);
    input_original = reinterpret_cast<NativePickupAbi<Animal>::Input>(base + 0x932fd0);
    classify_original = reinterpret_cast<NativePickupAbi<Animal>::Classify>(base + 0x930830);
    tick_original = reinterpret_cast<NativePickupTick>(base + native_pickup_tick_rva);
    first_feed_original = reinterpret_cast<void*>(base + 0x971367);
    first_feed_resume = reinterpret_cast<void*>(base + 0x971378);
    claim_original = reinterpret_cast<void*>(base + 0x97106e);
    claim_resume = reinterpret_cast<void*>(base + 0x97107b);
    tick_dispatch = dispatch;
    classify_first_feed = first_feed_owner;
    honor_claim = honor_owned_claim;
    if (sink) {
        sink("pickup_bindings_checked",
             ",\"order_stack_bytes\":44,\"tick_stack_bytes\":32,\"native_acceptance\":false");
    }
    return true;
}

NativePickupTick native_pickup_tick_entry() {
    return on_thread() ? tick_original : nullptr;
}

NativeHook native_social_order_observer_binding() {
    return {reinterpret_cast<void**>(&order_original), reinterpret_cast<void*>(order_hook)};
}

NativeHook native_pickup_input_observer_binding() {
    return {reinterpret_cast<void**>(&input_original), reinterpret_cast<void*>(input_hook)};
}

NativeHook native_pickup_classify_observer_binding() {
    return {reinterpret_cast<void**>(&classify_original), reinterpret_cast<void*>(classify_hook)};
}

NativeHook native_pickup_tick_binding() {
    return {reinterpret_cast<void**>(&tick_original), reinterpret_cast<void*>(tick_hook)};
}

NativeHook native_pickup_first_feed_binding() {
    return {&first_feed_original, reinterpret_cast<void*>(first_feed_hook)};
}

NativeHook native_pickup_claim_binding() {
    return {&claim_original, reinterpret_cast<void*>(claim_hook)};
}

namespace {
// The SDK declares no class for a fruit. These are the words of a fruit noun this file reads, by their byte
// offsets, each from the original's own code: the click handler reads the state and the slot (static D335CC,
// D33600), the holder lookup B05800 the three holder words, the stocking routine B0B330 writes them all.
namespace fruit_word {
constexpr size_t holder_type = 0x158, holder_key = 0x15c, holder = 0x160, slot = 0x164;
// 0 on a holder, 1 and 2 off it, 4 not pickable, 6 put away at the planet's centre.
constexpr size_t state = 0x16c;
// The bytes of a fruit noun that must be readable before any of these words is.
constexpr size_t readable_bytes = 0x170;
} // namespace fruit_word

constexpr uint32_t fruit_state_on_holder = 0;

bool fruit_readable(const Noun* noun) {
    return readable(noun, fruit_word::readable_bytes);
}

uint32_t read_fruit_word(const Noun* noun, size_t offset) {
    return reinterpret_cast<const uint32_t*>(noun)[offset / 4];
}

// Fruits on a holder, as the stocking routine leaves the ones it places.
unsigned held_fruit() {
    unsigned held = 0;
    auto manager = Manager::Get();
    if (manager) {
        for (auto& noun : manager->mNouns) {
            if (!noun.mbIsDestroyed && !noun.field_20 && noun.GetNounID() == Simulator::kFruit &&
                fruit_readable(&noun) && read_fruit_word(&noun, fruit_word::state) == fruit_state_on_holder) {
                ++held;
            }
        }
    }
    return held;
}

// Where each fruit lies and what holder it names, read only, for comparing a run's before and after.
struct FruitPlace {
    uint32_t id = 0;
    float place[3]{};
    uint32_t holder[4]{};
    bool held = false;
};

size_t fruit_places(std::array<FruitPlace, 256>& out) {
    size_t count = 0;
    auto manager = Manager::Get();
    if (manager) {
        for (auto& noun : manager->mNouns) {
            if (count == out.size()) {
                break;
            }
            if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Simulator::kFruit ||
                !fruit_readable(&noun)) {
                continue;
            }
            auto spatial = object_cast<Simulator::cSpatialObject>(&noun);
            if (!spatial || !readable(spatial, sizeof(Simulator::cSpatialObject))) {
                continue;
            }
            out[count++] = {noun.mID,
                            {spatial->mPosition.x, spatial->mPosition.y, spatial->mPosition.z},
                            {read_fruit_word(&noun, fruit_word::holder_type),
                             read_fruit_word(&noun, fruit_word::holder_key), read_fruit_word(&noun, fruit_word::holder),
                             read_fruit_word(&noun, fruit_word::slot)},
                            read_fruit_word(&noun, fruit_word::state) == fruit_state_on_holder};
        }
    }
    return count;
}

// What the observer of the stocking routine keeps between calls. Engine thread only.
struct StockObservation {
    uint64_t calls = 0, recorded = 0, skipped = 0;
    ULONGLONG next_record = 0;
    // Fruit places before and after one run, and how many moved fruits were recorded so far.
    std::array<FruitPlace, 256> before{}, after{};
    unsigned moves_recorded = 0;
};

StockObservation stock_observation;
constexpr unsigned max_stock_records = 240, max_stock_moves = 400;

// A client does not run the routine. Recorded at the first skip and every 4000th.
void record_update_fenced(uint32_t first, uint32_t second) {
    auto& skipped = stock_observation.skipped;
    if (sink && (++skipped == 1 || skipped % 4000 == 0)) {
        char fields[96]{};
        sprintf_s(fields, ",\"skipped\":%llu,\"arguments\":[%u,%u]", skipped, first, second);
        sink("native_plant_food_update_fenced", fields);
    }
}

// Each fruit this run moved or put on a holder: which, from where to where, and the holder it now names.
void record_fruit_moves(size_t before_count) {
    auto& state = stock_observation;
    const size_t after_count = before_count ? fruit_places(state.after) : 0;
    for (size_t i = 0; i < after_count && state.moves_recorded < max_stock_moves; ++i) {
        const FruitPlace& now = state.after[i];
        for (size_t j = 0; j < before_count; ++j) {
            const FruitPlace& was = state.before[j];
            if (was.id != now.id) {
                continue;
            }
            const float dx = now.place[0] - was.place[0], dy = now.place[1] - was.place[1],
                        dz = now.place[2] - was.place[2];
            if (dx * dx + dy * dy + dz * dz > 0.25f || was.held != now.held) {
                char row[384]{};
                sprintf_s(row,
                          ",\"call\":%llu,\"native_id\":%u,\"from\":[%.5g,%.5g,%.5g],\"to\":[%.5g,%.5g,%.5g],\"on_"
                          "holder\":[%s,%s],"
                          "\"holder\":[%u,%u,%u,%u],\"replica\":%s,\"observation_only\":true",
                          state.calls, now.id, double(was.place[0]), double(was.place[1]), double(was.place[2]),
                          double(now.place[0]), double(now.place[1]), double(now.place[2]), was.held ? "true" : "false",
                          now.held ? "true" : "false", now.holder[0], now.holder[1], now.holder[2], now.holder[3],
                          native_replica_is_client() ? "true" : "false");
                sink("native_plant_food_stock_placed", row);
                ++state.moves_recorded;
            }
            break;
        }
    }
}

// How often the routine runs and whether a run changed the number of fruits on holders, at most once a second.
void record_stock_run(uint32_t first, uint32_t second, unsigned held_before) {
    auto& state = stock_observation;
    const unsigned held_after = held_fruit();
    const auto now = GetTickCount64();
    if (held_after == held_before && now < state.next_record) {
        return;
    }
    state.next_record = now + 1000;
    ++state.recorded;
    char fields[256]{};
    sprintf_s(fields,
              ",\"calls\":%llu,\"arguments\":[%u,%u],\"held_before\":%u,\"held_after\":%u,\"replica\":%s,\"observation_"
              "only\":true",
              state.calls, first, second, held_before, held_after, native_replica_is_client() ? "true" : "false");
    sink("native_plant_food_stock_observed", fields);
}

// This routine is the whole update of the original's fruit: it stocks the plants around the avatar of the game it
// runs in, lets fruit fall and lie, and puts fruit away. Each game ran it for its own avatar, so a client showed
// fruit where the authority held none (trials 378, 405); skipping only its stocking block left the rest moving 50
// fruits on its own (trial409). A client does not run it: every fruit's place and state come from the authority as a
// shared entity. On the authority the hook only observes (C02): every argument goes to the original unchanged.
void __fastcall stock_hook(void* self, void*, uint32_t first, uint32_t second) {
    if (on_thread() && native_replica_is_client()) {
        record_update_fenced(first, second);
        return;
    }
    auto& state = stock_observation;
    const bool observe =
        on_thread() && sink && Simulator::GetGameModeID() == kGameCreature && state.recorded < max_stock_records;
    const unsigned held_before = observe ? held_fruit() : 0;
    const size_t before_count = observe && state.moves_recorded < max_stock_moves ? fruit_places(state.before) : 0;
    stock_original(self, first, second);
    ++state.calls;
    if (!observe) {
        return;
    }
    record_fruit_moves(before_count);
    record_stock_run(first, second, held_before);
}

} // namespace

NativeHook native_plant_food_stock_observer_binding() {
    return {reinterpret_cast<void**>(&stock_original), reinterpret_cast<void*>(stock_hook)};
}

namespace {
// The distance between two places, as the census rows record it.
double census_distance(const Math::Vector3& place, const Math::Vector3& from) {
    const double x = double(place.x) - from.x, y = double(place.y) - from.y, z = double(place.z) - from.z;
    return std::sqrt(x * x + y * y + z * z);
}

// The authority's fossils: every bone ornament with its place and flags, at most 128 rows.
void record_fossil_census(Manager* manager, const Animal* avatar) {
    unsigned ornaments = 0, bones = 0, emitted = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20 || noun.GetNounID() != Simulator::cInteractiveOrnament::NOUN_ID ||
            !readable(&noun, sizeof(Simulator::cInteractiveOrnament))) {
            continue;
        }
        ++ornaments;
        auto ornament = static_cast<Simulator::cInteractiveOrnament*>(&noun);
        if (ornament->mType != Simulator::kOrnamentBone) {
            continue;
        }
        ++bones;
        if (emitted >= 128) {
            continue;
        }
        auto spatial = static_cast<Simulator::cSpatialObject*>(ornament);
        const auto place = spatial->mPosition;
        const double distance = census_distance(place, avatar->mPosition);
        if (!std::isfinite(distance)) {
            continue;
        }
        char fields[512]{};
        sprintf_s(fields,
                  ",\"sample\":%u,\"native_id\":%u,\"ornament_type\":%d,\"position\":[%.9g,%.9g,%.9g],"
                  "\"avatar_native_id\":%u,\"distance\":%.9g,\"model\":[%u,%u,%u],\"tangible\":%s,\"enabled\":%s,"
                  "\"pickable\":%s,\"in_view\":%s,\"claimed\":%s,\"observation_only\":true",
                  ornament_samples, noun.mID, ornament->mType, double(place.x), double(place.y), double(place.z),
                  avatar->mID, distance, spatial->mModelKey.instanceID, spatial->mModelKey.typeID,
                  spatial->mModelKey.groupID, spatial->mbIsTangible ? "true" : "false",
                  spatial->mbEnabled ? "true" : "false", spatial->mbPickable ? "true" : "false",
                  spatial->mbInView ? "true" : "false", ornament->mpWhoIsInteractingWithMe ? "true" : "false");
        sink("native_fossil_census", fields);
        ++emitted;
    }
    char fields[192]{};
    sprintf_s(fields, ",\"sample\":%u,\"ornaments\":%u,\"bones\":%u,\"emitted\":%u,\"observation_only\":true",
              ornament_samples, ornaments, bones, emitted);
    sink("native_ornament_census_complete", fields);
}

// C02: the nouns plant food comes as. Read only: identity, kind, and, where the original's own cast gives a spatial
// object, the pinned cSpatialObject fields; for a fruit also its state word. Every fruit is listed, and the plants
// within 40 units of the avatar; at most 256 rows.
void record_plant_food_census(Manager* manager, const Animal* avatar) {
    unsigned plants = 0, groups = 0, fruits = 0, rows = 0;
    for (auto& noun : manager->mNouns) {
        if (noun.mbIsDestroyed || noun.field_20) {
            continue;
        }
        const auto kind = noun.GetNounID();
        if (kind != Simulator::kFruit && kind != Simulator::kFruitGroup && kind != Simulator::kGamePlant) {
            continue;
        }
        ++(kind == Simulator::kFruit ? fruits : kind == Simulator::kFruitGroup ? groups : plants);
        auto spatial = object_cast<Simulator::cSpatialObject>(&noun);
        if (rows >= 256 || kind == Simulator::kFruitGroup || !spatial ||
            !readable(spatial, sizeof(Simulator::cSpatialObject))) {
            continue;
        }
        const auto place = spatial->mPosition;
        const double distance = census_distance(place, avatar->mPosition);
        if (!std::isfinite(distance) || (kind == Simulator::kGamePlant && distance > 40.0)) {
            continue;
        }
        // The word the original's click handler reads to send the order to the fruit's holder (static D335CC).
        const bool state_known = kind == Simulator::kFruit && fruit_readable(&noun);
        const uint32_t state = state_known ? read_fruit_word(&noun, fruit_word::state) : 0;
        char row[512]{};
        sprintf_s(
            row,
            ",\"sample\":%u,\"native_id\":%u,\"noun_id\":%u,\"position\":[%.9g,%.9g,%.9g],\"distance\":%.9g,"
            "\"model\":[%u,%u,%u],\"scale\":%.9g,\"tangible\":%s,\"enabled\":%s,\"pickable\":%s,\"in_view\":%s,\"on_"
            "holder\":%s,\"state\":%d,\"observation_only\":true",
            ornament_samples, noun.mID, kind, double(place.x), double(place.y), double(place.z), distance,
            spatial->mModelKey.instanceID, spatial->mModelKey.typeID, spatial->mModelKey.groupID,
            double(spatial->mScale), spatial->mbIsTangible ? "true" : "false", spatial->mbEnabled ? "true" : "false",
            spatial->mbPickable ? "true" : "false", spatial->mbInView ? "true" : "false",
            state_known ? (state == fruit_state_on_holder ? "true" : "false") : "null",
            state_known ? static_cast<int>(state) : -1);
        sink("native_plant_food_census", row);
        ++rows;
    }
    char fields[192]{};
    sprintf_s(fields,
              ",\"sample\":%u,\"plants\":%u,\"fruit_groups\":%u,\"fruits\":%u,\"emitted\":%u,\"observation_only\":true",
              ornament_samples, plants, groups, fruits, rows);
    sink("native_plant_food_census_complete", fields);
}

} // namespace

// Every ten seconds, 24 times at most. The fossil census is the authority's. The plant food census is taken in
// every game, so that the three worlds can be compared (trial377: a fruit the authority held where a client's click
// found none).
void sample_native_ornaments() {
    const bool client = native_replica_is_client();
    if (!on_thread() || !sink || ornament_samples >= 24 || Simulator::GetGameModeID() != kGameCreature) {
        return;
    }
    const auto now = GetTickCount64();
    if (ornament_samples && now - last_ornament_sample < 10000) {
        return;
    }
    auto manager = Manager::Get();
    auto avatar = manager ? current(manager->GetAvatar()) : nullptr;
    if (!avatar) {
        return;
    }
    last_ornament_sample = now;
    ++ornament_samples;
    if (!client) {
        record_fossil_census(manager, avatar);
    }
    record_plant_food_census(manager, avatar);
}

bool native_fossil_position(uint32_t native_id, float (&position)[3]) {
    if (!on_thread() || native_replica_is_client() || native_id == UINT32_MAX ||
        Simulator::GetGameModeID() != kGameCreature) {
        return false;
    }
    auto manager = Manager::Get();
    if (!manager) {
        return false;
    }
    bool found = false;
    for (auto& noun : manager->mNouns) {
        if (noun.mID == native_id) {
            if (found || noun.mbIsDestroyed || noun.field_20 ||
                noun.GetNounID() != Simulator::cInteractiveOrnament::NOUN_ID ||
                !readable(&noun, sizeof(Simulator::cInteractiveOrnament))) {
                return false;
            }
            auto ornament = static_cast<Simulator::cInteractiveOrnament*>(&noun);
            auto spatial = static_cast<Simulator::cSpatialObject*>(ornament);
            if (ornament->mType != Simulator::kOrnamentBone || !spatial->mbIsTangible || !spatial->mbEnabled ||
                !spatial->mbPickable || ornament->mpWhoIsInteractingWithMe) {
                return false;
            }
            position[0] = spatial->mPosition.x;
            position[1] = spatial->mPosition.y;
            position[2] = spatial->mPosition.z;
            if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) {
                return false;
            }
            found = true;
        }
    }
    return found;
}

namespace {
// The spatial object of a live fruit noun whose words can be read, or null.
Simulator::cSpatialObject* fruit_spatial(Noun* noun) {
    if (!noun || noun->mbIsDestroyed || noun->field_20 || noun->GetNounID() != Simulator::kFruit ||
        !fruit_readable(noun)) {
        return nullptr;
    }
    auto spatial = object_cast<Simulator::cSpatialObject>(noun);
    return spatial && readable(spatial, sizeof(Simulator::cSpatialObject)) ? spatial : nullptr;
}

// The stocking routine shows a fruit by setting bit 0 of its model's flags and putting the model in its world
// (static B0B7CBh), and hides it the other way round.
constexpr int model_flag_shown = 1;
} // namespace

bool native_plant_food_read(Noun* noun, NativePlantFood& out) {
    out = {};
    auto spatial = on_thread() ? fruit_spatial(noun) : nullptr;
    if (!spatial) {
        return false;
    }
    network::FruitState state;
    state.state = read_fruit_word(noun, fruit_word::state);
    state.pickable = spatial->mbPickable;
    state.tangible = spatial->mbIsTangible;
    state.enabled = spatial->mbEnabled;
    state.supported = spatial->mbSupported;
    const auto model = spatial->GetModel();
    state.in_world = model && readable(model, 8) && (model->mFlags & model_flag_shown);
    out.state = network::pack_fruit_state(state);
    if (out.state == UINT32_MAX) {
        return false;
    }
    out.native_id = noun->mID;
    out.address = reinterpret_cast<uintptr_t>(noun);
    const auto& place = spatial->mPosition;
    const auto& turn = spatial->mOrientation;
    const auto& model_key = spatial->mModelKey;
    out.place[0] = place.x;
    out.place[1] = place.y;
    out.place[2] = place.z;
    out.turn[0] = turn.x;
    out.turn[1] = turn.y;
    out.turn[2] = turn.z;
    out.turn[3] = turn.w;
    out.scale = spatial->mScale;
    out.model[0] = model_key.instanceID;
    out.model[1] = model_key.typeID;
    out.model[2] = model_key.groupID;
    return true;
}

Noun* native_plant_food_find(uint32_t native_id, uintptr_t expected) {
    auto manager = on_thread() ? Manager::Get() : nullptr;
    Noun* found = nullptr;
    if (manager) {
        for (auto& noun : manager->mNouns) {
            if (noun.mID == native_id && noun.GetNounID() == Simulator::kFruit) {
                if (found || noun.mbIsDestroyed || noun.field_20 ||
                    (expected && reinterpret_cast<uintptr_t>(&noun) != expected)) {
                    return nullptr;
                }
                found = &noun;
            }
        }
    }
    return found;
}

namespace {
// A fruit's place, turn and model, through the spatial object's own setters and only where they differ. The
// original gives a fruit it places another model (trial410).
void place_fruit(Simulator::cSpatialObject* spatial, const NativePlantFood& wanted) {
    const auto& place = spatial->mPosition;
    const auto& turn = spatial->mOrientation;
    const float dx = place.x - wanted.place[0], dy = place.y - wanted.place[1], dz = place.z - wanted.place[2];
    if (dx * dx + dy * dy + dz * dz > 1e-6f) {
        spatial->SetPosition(Math::Vector3(wanted.place[0], wanted.place[1], wanted.place[2]));
    }
    if (std::abs(turn.x - wanted.turn[0]) + std::abs(turn.y - wanted.turn[1]) + std::abs(turn.z - wanted.turn[2]) +
            std::abs(turn.w - wanted.turn[3]) >
        1e-5f) {
        spatial->SetOrientation(Math::Quaternion(wanted.turn[0], wanted.turn[1], wanted.turn[2], wanted.turn[3]));
    }
    const auto& model_key = spatial->mModelKey;
    if (wanted.model[0] && (model_key.instanceID != wanted.model[0] || model_key.typeID != wanted.model[1] ||
                            model_key.groupID != wanted.model[2])) {
        spatial->SetModelKey(ResourceKey(wanted.model[0], wanted.model[1], wanted.model[2]));
    }
}

// The fruit's model in or out of the model world, with the call the stocking routine makes after showing one.
void show_fruit(Noun* noun, Simulator::cSpatialObject* spatial, bool shown) {
    const auto model = spatial->GetModel();
    if (!model || !readable(model, 8) || !model->mpWorld || ((model->mFlags & model_flag_shown) != 0) == shown) {
        return;
    }
    if (shown) {
        model->mFlags |= model_flag_shown;
        model->mpWorld->SetInWorld(model, true);
        fruit_shown_original(noun, 1, 0);
    } else {
        model->mFlags &= ~model_flag_shown;
        model->mpWorld->SetInWorld(model, false);
    }
}
} // namespace

// A client gives its fruit what the authority holds for it, with the same writes the original's own routine makes
// on a fruit it places or puts away (static B0B330h): its place and turn, its state word, its flags, and its model
// in or out of the world.
bool native_plant_food_apply(Noun* noun, const NativePlantFood& wanted) {
    network::FruitState state;
    if (!on_thread() || !native_replica_is_client() || !fruit_shown_original ||
        !network::unpack_fruit_state(wanted.state, state)) {
        return false;
    }
    auto spatial = fruit_spatial(noun);
    if (!spatial) {
        return false;
    }
    place_fruit(spatial, wanted);
    reinterpret_cast<uint32_t*>(noun)[fruit_word::state / 4] = state.state;
    spatial->mbPickable = state.pickable;
    spatial->mbIsTangible = state.tangible;
    spatial->mbEnabled = state.enabled;
    spatial->mbSupported = state.supported;
    show_fruit(noun, spatial, state.in_world);
    return true;
}

bool native_plant_food_present(uint32_t fruit_native_id) {
    auto manager = on_thread() ? Manager::Get() : nullptr;
    if (manager) {
        for (auto& noun : manager->mNouns) {
            if (noun.mID == fruit_native_id && noun.GetNounID() == Simulator::kFruit && !noun.mbIsDestroyed &&
                !noun.field_20) {
                return true;
            }
        }
    }
    return false;
}

bool native_plant_food_at(uint32_t fruit_native_id, uint64_t place) {
    auto manager = on_thread() ? Manager::Get() : nullptr;
    if (manager) {
        for (auto& noun : manager->mNouns) {
            if (noun.mID == fruit_native_id && noun.GetNounID() == Simulator::kFruit && !noun.mbIsDestroyed &&
                !noun.field_20) {
                auto spatial = object_cast<Simulator::cSpatialObject>(&noun);
                if (!spatial || !readable(spatial, sizeof(Simulator::cSpatialObject))) {
                    return false;
                }
                const float held[3]{spatial->mPosition.x, spatial->mPosition.y, spatial->mPosition.z};
                return network::same_place(place, held);
            }
        }
    }
    return false;
}

namespace {
// Whether an order can be given to this creature as the original gives it: its virtual order routine is the pinned
// one, and its behaviour tree, which the order is written into, can be read.
bool order_route_pinned(const Animal* actor) {
    const auto table = *reinterpret_cast<const uintptr_t* const*>(actor);
    return readable(table, 0x88) && table[0x84 / 4] == image_base + 0x803df0 &&
           readable(reinterpret_cast<const void*>(static_cast<uintptr_t>(actor->field_B4C)), 0x1d0);
}

// The priority the original's own click handler passes with an order: the float it reads at this address, whose
// bytes are pinned when the bindings are prepared.
float order_priority() {
    float priority = 0;
    std::memcpy(&priority, reinterpret_cast<const void*>(image_base + 0x107a540), 4);
    return priority;
}

// The fruit lies elsewhere in this world than where the player's own world showed it: no order is given.
void record_fruit_elsewhere(uint32_t actor_id, uint32_t fruit_native_id, uint64_t seen_place,
                            const float (&held_at)[3]) {
    if (!sink) {
        return;
    }
    char fields[256]{};
    float seen[3]{};
    network::unpack_place(seen_place, seen);
    sprintf_s(fields,
              ",\"actor_native_id\":%u,\"fruit_native_id\":%u,\"order_returned\":false,\"reason\":\"fruit_"
              "elsewhere_in_this_world\","
              "\"seen\":[%.4g,%.4g,%.4g],\"held\":[%.4g,%.4g,%.4g]",
              actor_id, fruit_native_id, double(seen[0]), double(seen[1]), double(seen[2]), double(held_at[0]),
              double(held_at[1]), double(held_at[2]));
    sink("native_plant_food_order_dispatched", fields);
}
} // namespace

// The authority orders a player's creature to a fruit, as the original's own handler does for a click on a fruit
// on the creature it controls (static D32FD0 case 17h at D335A4, exports m07-corpse-click and c02-fruit-01; its
// order seen in trial364's client trace): no creature target; where the fruit is on a holder the order goes to that
// holder (B05800) and carries the fruit's slot in its own word at +10h, otherwise it goes to the fruit; C0C070 with
// the corpse order's words except the first, 20h; then the creature's byte at +B5Ch is cleared. The handler's last
// call, D2F940, draws the local player's pointer feedback and is not made here.
bool native_plant_food_order(Animal* actor, uint32_t actor_id, uint32_t fruit_native_id, uint64_t seen_place) {
    if (!on_thread() || !order_original || actor_id == UINT32_MAX || fruit_native_id == UINT32_MAX ||
        native_replica_is_client() || !native_replica_allows(replica::Mutation::pickup) ||
        !native_replica_allows(replica::Mutation::targeting)) {
        return false;
    }
    actor = current(actor, actor_id);
    Noun* fruit = native_plant_food_find(fruit_native_id);
    auto spatial = fruit ? object_cast<Simulator::cSpatialObject>(fruit) : nullptr;
    if (!actor || actor->mbDead || !std::isfinite(actor->mHealthPoints) || actor->mHealthPoints <= 0 || !spatial ||
        !readable(spatial, sizeof(Simulator::cSpatialObject)) || !spatial->mbPickable || !spatial->mbIsTangible ||
        !spatial->mbEnabled) {
        return false;
    }
    // Each game re-placed part of its fruit on its own (trial378). The creature is sent only to the fruit the
    // player saw: this world's fruit of that number must lie where the player's world showed it.
    const float held_at[3]{spatial->mPosition.x, spatial->mPosition.y, spatial->mPosition.z};
    if (!network::same_place(seen_place, held_at)) {
        record_fruit_elsewhere(actor_id, fruit_native_id, seen_place, held_at);
        return false;
    }
    if (!order_route_pinned(actor)) {
        return false;
    }
    if (!fruit_holder_original || !fruit_readable(fruit)) {
        return false;
    }
    const bool on_holder = read_fruit_word(fruit, fruit_word::state) == fruit_state_on_holder;
    void* target = on_holder ? fruit_holder_original(fruit) : fruit;
    const uint32_t slot = read_fruit_word(fruit, fruit_word::slot);
    if (!target) {
        if (sink) {
            sink("native_plant_food_order_dispatched", ",\"order_returned\":false,\"reason\":\"fruit_without_holder\"");
        }
        return false;
    }
    actor->SetCreatureTarget(nullptr, false, 0);
    actor = current(actor, actor_id);
    if (!actor || actor->mbDead) {
        return false;
    }
    const float priority = order_priority();
    void* order = order_original(actor, 0x20, 0, 0x2788b9, 0x40049000, priority, 0x100000, 0, 0, 0, target, false);
    if (order && on_holder && readable(order, 0x14)) {
        std::memcpy(static_cast<unsigned char*>(order) + 0x10, &slot, 4);
    }
    actor = current(actor, actor_id);
    if (actor) {
        actor->field_B5C = false;
    }
    if (sink) {
        char fields[224]{};
        sprintf_s(
            fields,
            ",\"actor_native_id\":%u,\"fruit_native_id\":%u,\"through_holder\":%s,\"slot\":%u,\"order_returned\":%s",
            actor_id, fruit_native_id, on_holder ? "true" : "false", slot, order ? "true" : "false");
        sink("native_plant_food_order_dispatched", fields);
    }
    return order != nullptr;
}

bool native_pickup_order(Animal* actor, uint32_t actor_id, Animal* corpse, uint32_t corpse_id) {
    if (!on_thread() || !order_original || actor_id == UINT32_MAX || corpse_id == UINT32_MAX || actor == corpse ||
        native_replica_is_client() || !native_replica_allows(replica::Mutation::pickup) ||
        !native_replica_allows(replica::Mutation::targeting)) {
        return false;
    }
    actor = current(actor, actor_id);
    corpse = current(corpse, corpse_id);
    if (!actor || !corpse || actor->mbDead || !std::isfinite(actor->mHealthPoints) || actor->mHealthPoints <= 0 ||
        !corpse->mbDead || !std::isfinite(corpse->mFoodValue) || corpse->mFoodValue <= 0 ||
        (actor->mSpeciesKey.instanceID == corpse->mSpeciesKey.instanceID &&
         actor->mSpeciesKey.typeID == corpse->mSpeciesKey.typeID &&
         actor->mSpeciesKey.groupID == corpse->mSpeciesKey.groupID)) {
        return false;
    }
    if (!order_route_pinned(actor)) {
        return false;
    }
    // D33168 uses (target,true,0), then C032F0 forwards this exact order. Using
    // C0C070 excludes only the wrapper's global-avatar/posse fanout, which is
    // not qualified for an independently owned B actor. No native claim is set.
    actor->SetCreatureTarget(static_cast<Simulator::cCombatant*>(corpse), true, 0);
    actor = current(actor, actor_id);
    corpse = current(corpse, corpse_id);
    if (!actor || !corpse || actor->mbDead || !corpse->mbDead) {
        return false;
    }
    const float priority = order_priority();
    order_original(actor, 0x40000, 0, 0x2788b9, 0x40049000, priority, 0x100000, 0, 0, 0, corpse, false);
    actor = current(actor, actor_id);
    corpse = current(corpse, corpse_id);
    const bool queued = actor && corpse && original_order_present(actor, corpse);
    event("native_corpse_order_dispatched", actor_id, corpse_id, queued);
    return queued;
}

bool native_fossil_order(Animal* actor, uint32_t actor_id, Simulator::cInteractiveOrnament* resource,
                         uint32_t resource_id) {
    if (!on_thread() || !order_original || actor_id == UINT32_MAX || resource_id == UINT32_MAX ||
        native_replica_is_client() || !native_replica_allows(replica::Mutation::pickup) ||
        !native_replica_allows(replica::Mutation::targeting)) {
        return false;
    }
    actor = current(actor, actor_id);
    resource = current_fossil(resource, resource_id);
    if (!actor || !resource || actor->mbDead || !std::isfinite(actor->mHealthPoints) || actor->mHealthPoints <= 0) {
        return false;
    }
    if (!order_route_pinned(actor)) {
        return false;
    }
    // D32FD0 classification26, observed unchanged in native73/76. C032F0
    // forwards this eleven-word fossil order; C0C070 omits only posse fanout.
    actor->SetCreatureTarget(nullptr, false, 0);
    actor = current(actor, actor_id);
    resource = current_fossil(resource, resource_id);
    if (!actor || !resource || actor->mbDead) {
        return false;
    }
    const float priority = order_priority();
    order_original(actor, 0, 0x40000, 0x2788b9, 0x40049000, priority, 0x100000, 0, 5, 0, resource, false);
    actor = current(actor, actor_id);
    resource = current_fossil(resource, resource_id);
    bool queued = false;
    if (actor && resource) {
        const auto tree = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(actor->field_B4C));
        if (readable(tree, 0x1d0)) {
            for (unsigned i = 0; i < 8; ++i) {
                const auto entry = tree + 8 + i * 0x38;
                uint32_t mask = 0, extra = 0, kind = 0;
                void* target = nullptr;
                std::memcpy(&mask, entry, 4);
                std::memcpy(&extra, entry + 4, 4);
                std::memcpy(&kind, entry + 0xc, 4);
                std::memcpy(&target, entry + 0x2c, 4);
                if (!mask && extra == 0x40000 && kind == 5 && target == resource) {
                    queued = true;
                }
            }
        }
    }
    if (sink) {
        char fields[192]{};
        sprintf_s(fields,
                  ",\"actor_native_id\":%u,\"target_native_id\":%u,\"order_present\":%s,\"reward_inferred\":false",
                  actor_id, resource_id, queued ? "true" : "false");
        sink("native_fossil_order_dispatched", fields);
    }
    return queued;
}

bool native_pickup_snapshot(Animal* actor, uint32_t actor_id, const void* behavior_state,
                            NativePickupSnapshot& result) {
    result = {};
    if (!on_thread() || !tick_original || actor_id == UINT32_MAX || !readable(behavior_state, 0x18)) {
        return false;
    }
    actor = current(actor, actor_id);
    if (!actor) {
        return false;
    }
    const auto state = static_cast<const unsigned char*>(behavior_state);
    Animal* corpse = nullptr;
    std::memcpy(&corpse, state + 4, 4);
    corpse = current(corpse);
    if (!corpse || !corpse->mbDead || !std::isfinite(corpse->mFoodValue) || !std::isfinite(actor->mHunger) ||
        !std::isfinite(actor->mHealthPoints)) {
        return false;
    }
    const auto claimant = corpse->mpWhoIsInteractingWithMe.get();
    result.actor_native_id = actor_id;
    result.corpse_native_id = corpse->mID;
    std::memcpy(&result.phase, state, 4);
    std::memcpy(&result.animation, state + 0xc, 4);
    if (result.phase > 5) {
        result = {};
        return false;
    }
    result.food = corpse->mFoodValue;
    result.hunger = actor->mHunger;
    result.health = actor->mHealthPoints;
    result.eaten = corpse->mbHasBeenEaten;
    result.first_feed = state[8] != 0;
    result.avatar_classified = (actor->mGeneralFlags & 0x200) != 0;
    result.claimant_present = claimant != nullptr;
    result.claimed_by_actor = claimant == actor;
    // A claimant need not be an Animal. Compare against the census first and
    // obtain only its ID; never dereference an unvalidated native smart pointer.
    if (claimant) {
        auto manager = Manager::Get();
        bool found = false;
        for (auto& noun : manager->mNouns) {
            if (&noun == static_cast<Noun*>(claimant)) {
                if (!noun.mbIsDestroyed && !noun.field_20) {
                    result.claimant_native_id = noun.mID;
                    found = true;
                }
                break;
            }
        }
        if (!found) {
            result = {};
            return false;
        }
    }
    return true;
}
} // namespace sporemp
