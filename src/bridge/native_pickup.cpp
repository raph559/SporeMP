#include "native_pickup.h"
#include "native_replica.h"
#include "native_branch.h"
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <cmath>
#include <cstdio>
#include <cstring>

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
bool(*thread_check)() = nullptr;
Order order_original = nullptr;
NativePickupTick tick_original = nullptr;
NativePickupDispatch tick_dispatch = nullptr;
bool(*classify_first_feed)() = nullptr;
bool(*honor_claim)() = nullptr;
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
SPOREMP_NATIVE_BRANCH(first_feed_hook,retain_first_feed_classification,first_feed_original,first_feed_resume)
// At D7106E EBP already contains corpse+B50 and ECX points to that native smart
// pointer. D7107B TEST EBP/JNZ returns original false without replacing it. No
// manual refcount/claim change and no stack adjustment is bypassed.
SPOREMP_NATIVE_BRANCH(claim_hook,retain_claim_classification,claim_original,claim_resume)
bool __cdecl tick_hook(Animal* actor,double clock,uint32_t flags,uintptr_t parameter,
    void* state,void* activation,float delta) {
    // Root applies the immutable replica mutation fence before its thread
    // check. A foreign callback must not bypass that fence through this leaf.
    if (!tick_dispatch) return tick_original(actor,clock,flags,parameter,state,activation,delta);
    return tick_dispatch(tick_original,actor,clock,flags,parameter,state,activation,delta);
}
bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto begin = reinterpret_cast<uintptr_t>(address), region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin >= region && size <= info.RegionSize && begin - region <= info.RegionSize - size;
}
template<size_t N> bool bytes(uint32_t rva, const unsigned char (&expected)[N]) {
    if (!image_base || image_end <= image_base || rva > image_end-image_base || N > image_end-image_base-rva) return false;
    const auto address = reinterpret_cast<const void*>(image_base+rva);
    return readable(address,N) && !std::memcmp(address,expected,N);
}
Animal* current(Animal* candidate, uint32_t expected_native_id = UINT32_MAX) {
    auto manager = Manager::Get();
    if (!candidate || !manager || Simulator::GetGameModeID() != kGameCreature) return nullptr;
    for (auto& noun : manager->mNouns) if (&noun == static_cast<Noun*>(candidate)) {
        if (!readable(candidate,sizeof(Animal)) || noun.mbIsDestroyed || noun.field_20 ||
            candidate->mbMarkedForDeletion || !candidate->mbEnabled ||
            *reinterpret_cast<const uintptr_t*>(candidate) != image_base+0x106a080 ||
            (expected_native_id != UINT32_MAX && noun.mID != expected_native_id)) return nullptr;
        return candidate;
    }
    return nullptr;
}
bool original_order_present(Animal* actor, Animal* corpse) {
    const auto tree = reinterpret_cast<const unsigned char*>(static_cast<uintptr_t>(actor->field_B4C));
    if (!readable(tree,0x1d0)) return false;
    for (unsigned i=0;i<8;++i) {
        const auto entry = tree+8+i*0x38;
        uint32_t mask=0,extra=0; Animal* target=nullptr;
        std::memcpy(&mask,entry,4); std::memcpy(&extra,entry+4,4); std::memcpy(&target,entry+0x2c,4);
        if (mask == 0x40000 && extra == 0 && target == corpse) return true;
    }
    return false;
}
void event(const char* name, uint32_t actor, uint32_t corpse, bool queued) {
    if (!sink) return;
    char fields[256]{};
    sprintf_s(fields,",\"actor_native_id\":%u,\"corpse_native_id\":%u,\"order_present\":%s,\"reward_inferred\":false",
        actor,corpse,queued?"true":"false");
    sink(name,fields);
}
}

bool prepare_native_pickup(uintptr_t base, uintptr_t end, DWORD thread,
    PlayerContextEvent event_sink, bool(*check)(),NativePickupDispatch dispatch,
    bool(*first_feed_owner)(),bool(*honor_owned_claim)()) {
    order_original=nullptr; tick_original=nullptr;
    first_feed_original=nullptr; first_feed_resume=nullptr; claim_original=nullptr; claim_resume=nullptr;
    tick_dispatch=nullptr; classify_first_feed=nullptr; honor_claim=nullptr;
    image_base=base; image_end=end; engine_thread=thread; sink=event_sink; thread_check=check;
    // STATIC dc04aee5... / SDK cbf9206b9a823f0911cd9be0217104a49d72380b.
    // m07-corpse-click/C0C070 and m07-pickup-dispatch/D70FD0 instruction exports;
    // BC9160 caller corroboration is in m07-pickup-abi. No absolute relocation
    // lies within these guarded instruction spans. Native execution is separate.
    if (!thread || GetCurrentThreadId()!=thread || !check || !dispatch || !first_feed_owner || !honor_owned_claim ||
        !bytes(0x80c070,{0x53,0x8b,0x5c,0x24,0x0c,0x55,0x56,0x8b,0xf1,0x57,0x8b,0x7c,0x24,0x14}) ||
        !bytes(0x80c159,{0x8b,0x4c,0x24,0x30,0x8b,0x54,0x24,0x34,0x5f,0x5e,0x5d,0x89,0x48,0x0c,0x89,0x50,0x10,0x5b,0xc2,0x2c,0x00}) ||
        !bytes(native_pickup_tick_rva,{0x83,0xec,0x60,0x53,0x57,0x8b,0xbc,0x24,0x80,0x00,0x00,0x00,0x8b,0x47,0x04}) ||
        !bytes(0x971398,{0xc6,0x82,0x5f,0x0b,0x00,0x00,0x01,0xc6,0x47,0x08,0x01}) ||
        !bytes(0x9713ad,{0xe8,0xee,0xd4,0xfb,0xff,0x8b,0x03,0x83,0xc4,0x04}) ||
        !bytes(0x9713b7,{0x8d,0x4c,0x24,0x58,0x51}) ||
        !bytes(0x9713c2,{0x68,0x2d,0x36,0x35,0xd3,0x89,0x74,0x24,0x60,0x89,0x44,0x24,0x64,
            0xc7,0x44,0x24,0x68,0x00,0x00,0x00,0x00,0xe8,0x84,0x7f,0xfc,0xff}) ||
        !bytes(0x971367,{0x8b,0x86,0x58,0x0b,0x00,0x00,0xc1,0xe8,0x09,0xa8,0x01,0x0f,0x84,0xaa,0x02,0x00,0x00}) ||
        !bytes(0x971378,{0x8b,0x0b,0x80,0xb9,0x5f,0x0b,0x00,0x00,0x00}) ||
        !bytes(0x97105c,{0x8b,0x03,0x8b,0xa8,0x50,0x0b,0x00,0x00,0x8d,0x88,0x50,0x0b,0x00,0x00,0x3b,0xee,0x74,0x1b}) ||
        !bytes(0x97106e,{0x8b,0x86,0x58,0x0b,0x00,0x00,0xc1,0xe8,0x09,0xa8,0x01,0x75,0x08}) ||
        !bytes(0x97107b,{0x85,0xed,0x0f,0x85,0x80,0x05,0x00,0x00}) ||
        !bytes(0x7c9477,{0xff,0xd1,0x83,0xc4,0x20,0x84,0xc0}) ||
        !bytes(0x7c969e,{0xff,0xd1,0x83,0xc4,0x20,0x84,0xc0}) ||
        !bytes(0x107a540,{0xff,0xff,0x7f,0x7f})) {
        if (sink) sink("pickup_binding_rejected","");
        return false;
    }
    order_original=reinterpret_cast<Order>(base+0x80c070);
    tick_original=reinterpret_cast<NativePickupTick>(base+native_pickup_tick_rva);
    first_feed_original=reinterpret_cast<void*>(base+0x971367);
    first_feed_resume=reinterpret_cast<void*>(base+0x971378);
    claim_original=reinterpret_cast<void*>(base+0x97106e);
    claim_resume=reinterpret_cast<void*>(base+0x97107b);
    tick_dispatch=dispatch; classify_first_feed=first_feed_owner; honor_claim=honor_owned_claim;
    if (sink) sink("pickup_bindings_checked",",\"order_stack_bytes\":44,\"tick_stack_bytes\":32,\"native_acceptance\":false");
    return true;
}
NativePickupTick native_pickup_tick_entry() { return on_thread()?tick_original:nullptr; }
NativeHook native_pickup_tick_binding() { return {reinterpret_cast<void**>(&tick_original),reinterpret_cast<void*>(tick_hook)}; }
NativeHook native_pickup_first_feed_binding() { return {&first_feed_original,reinterpret_cast<void*>(first_feed_hook)}; }
NativeHook native_pickup_claim_binding() { return {&claim_original,reinterpret_cast<void*>(claim_hook)}; }

bool native_pickup_order(Animal* actor, uint32_t actor_id, Animal* corpse, uint32_t corpse_id) {
    if (!on_thread() || !order_original || actor_id==UINT32_MAX || corpse_id==UINT32_MAX || actor==corpse ||
        native_replica_is_client() || !native_replica_allows(replica::Mutation::pickup) ||
        !native_replica_allows(replica::Mutation::targeting)) return false;
    actor=current(actor,actor_id); corpse=current(corpse,corpse_id);
    if (!actor || !corpse || actor->mbDead || !std::isfinite(actor->mHealthPoints) || actor->mHealthPoints<=0 ||
        !corpse->mbDead || !std::isfinite(corpse->mFoodValue) || corpse->mFoodValue<=0 ||
        (actor->mSpeciesKey.instanceID==corpse->mSpeciesKey.instanceID &&
         actor->mSpeciesKey.typeID==corpse->mSpeciesKey.typeID && actor->mSpeciesKey.groupID==corpse->mSpeciesKey.groupID)) return false;
    const auto table = *reinterpret_cast<const uintptr_t* const*>(actor);
    if (!readable(table,0x88) || table[0x84/4] != image_base+0x803df0 ||
        !readable(reinterpret_cast<const void*>(static_cast<uintptr_t>(actor->field_B4C)),0x1d0)) return false;
    // D33168 uses (target,true,0), then C032F0 forwards this exact order. Using
    // C0C070 excludes only the wrapper's global-avatar/posse fanout, which is
    // not qualified for an independently owned B actor. No native claim is set.
    actor->SetCreatureTarget(static_cast<Simulator::cCombatant*>(corpse),true,0);
    actor=current(actor,actor_id); corpse=current(corpse,corpse_id);
    if (!actor || !corpse || actor->mbDead || !corpse->mbDead) return false;
    float priority=0; std::memcpy(&priority,reinterpret_cast<const void*>(image_base+0x107a540),4);
    order_original(actor,0x40000,0,0x2788b9,0x40049000,priority,0x100000,0,0,0,corpse,false);
    actor=current(actor,actor_id); corpse=current(corpse,corpse_id);
    const bool queued=actor && corpse && original_order_present(actor,corpse);
    event("native_corpse_order_dispatched",actor_id,corpse_id,queued);
    return queued;
}

bool native_pickup_snapshot(Animal* actor, uint32_t actor_id, const void* behavior_state, NativePickupSnapshot& result) {
    result={};
    if (!on_thread() || !tick_original || actor_id==UINT32_MAX || !readable(behavior_state,0x18)) return false;
    actor=current(actor,actor_id);
    if (!actor) return false;
    const auto state=static_cast<const unsigned char*>(behavior_state);
    Animal* corpse=nullptr;
    std::memcpy(&corpse,state+4,4);
    corpse=current(corpse);
    if (!corpse || !corpse->mbDead || !std::isfinite(corpse->mFoodValue) ||
        !std::isfinite(actor->mHunger) || !std::isfinite(actor->mHealthPoints)) return false;
    const auto claimant=corpse->mpWhoIsInteractingWithMe.get();
    result.actor_native_id=actor_id; result.corpse_native_id=corpse->mID;
    std::memcpy(&result.phase,state,4); std::memcpy(&result.animation,state+0xc,4);
    if (result.phase>5) { result={}; return false; }
    result.food=corpse->mFoodValue; result.hunger=actor->mHunger; result.health=actor->mHealthPoints;
    result.eaten=corpse->mbHasBeenEaten; result.first_feed=state[8]!=0;
    result.avatar_classified=(actor->mGeneralFlags&0x200)!=0;
    result.claimant_present=claimant!=nullptr; result.claimed_by_actor=claimant==actor;
    // A claimant need not be an Animal. Compare against the census first and
    // obtain only its ID; never dereference an unvalidated native smart pointer.
    if (claimant) {
        auto manager=Manager::Get();
        bool found=false;
        for (auto& noun:manager->mNouns) if (&noun==static_cast<Noun*>(claimant)) {
            if (!noun.mbIsDestroyed && !noun.field_20) { result.claimant_native_id=noun.mID; found=true; }
            break;
        }
        if (!found) { result={}; return false; }
    }
    return true;
}
}
