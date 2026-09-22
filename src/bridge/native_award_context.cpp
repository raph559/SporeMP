#include "native_award_context.h"
#include "native_replica.h"
#include "native_award_abi.h"
#include "native_pickup.h"
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <intrin.h>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sporemp {
namespace {
using Animal=Simulator::cCreatureAnimal;
using Creature=Simulator::cCreatureBase;
using Player=Simulator::cPlayer;
using Manager=Simulator::cGameNounManager;
using Spatial=Simulator::cSpatialObject;
using Stage=Simulator::cCreatureGameData;
using Strategy=App::cCreatureModeStrategy;
// These signatures are pinned to the inspected x86 instructions, not inferred C.
using Abi=NativeAwardAbi<Animal,Spatial,Player,Manager,Strategy>;
using Owned=Abi::Owned;
using Amount=Abi::Amount;
using PlayerGet=Abi::PlayerGet;
using AvatarGet=Abi::AvatarGet;
using DisplayAdd=Abi::DisplayAdd;
using Action=Abi::Action;
using Update=Abi::Update;
using Goal=Abi::Goal;
using InitDeque=void(__thiscall*)(void*,uint32_t);
using ObjectCall=void(__thiscall*)(void*);
using CounterTick=void(__thiscall*)(void*,uint32_t);
using StageReset=void(__cdecl*)();
Owned owned_original=nullptr;
Amount amount_original=nullptr;
PlayerGet player_original=nullptr;
AvatarGet avatar_original=nullptr;
DisplayAdd display_original=nullptr;
Action action_original=nullptr;
Update update_original=nullptr;
Goal goal_original=nullptr;
InitDeque deque_init=nullptr;
ObjectCall deque_destroy=nullptr,timer_init=nullptr,timer_destroy=nullptr;
CounterTick counter_tick=nullptr;
StageReset stage_reset=nullptr;
uintptr_t image_base=0;
DWORD engine_thread=0;
PlayerContextEvent sink=nullptr;
bool(*thread_check)()=nullptr;
Animal* b_actor=nullptr;
uint32_t b_native_id=UINT32_MAX;
uint64_t b_id=0,b_epoch=0;
bool ready=false,context_active=false,world_selected=false,world_saved_a=false,b_world_pending=false;
bool alternate_world=false,boundary_reported=false;
uint64_t counter_updates=0,world_updates=0,awards=0;
Stage b_stage{};
// Last observed value survives reward-context cleanup for a still-present
// corpse. This scalar is presentation evidence, never restored native state.
uint64_t retired_actor_id=0,retired_epoch=0;
float retired_dna=0;
bool retired_dna_valid=false;
// Native display counter: state +00, native deque +04 (40 bytes), timer +30.
// Constructors/destructors and queue processing are original game functions.
// There is no SDK/container destructor at DLL unload and no native pointer leaves the bridge.
alignas(8) std::array<unsigned char,0x50> b_counter{};
Animal* damage_actor=nullptr;
uint32_t damage_owner=0;
uint64_t damage_id=0;
Animal* pickup_actor=nullptr;
Animal* pickup_corpse=nullptr;
uint32_t pickup_owner=0,pickup_corpse_native_id=UINT32_MAX;
uint64_t pickup_id=0;
bool pickup_granted=false;

void event(const char* name,const char* format="",...) {
    if(!sink || GetCurrentThreadId()!=engine_thread)return;
    char fields[1536]{};va_list args;va_start(args,format);
    const int result=vsprintf_s(fields,format,args);va_end(args);
    if(result>=0)sink(name,fields);
}
uint32_t caller(void* p) {return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)-image_base);}
bool live() {
    if(!ready || GetCurrentThreadId()!=engine_thread || !native_second_player(b_epoch) || !Manager::Get())return false;
    for(auto& noun:Manager::Get()->mNouns)if(&noun==static_cast<Simulator::cGameData*>(b_actor))
        return !noun.mbIsDestroyed && !noun.field_20 && noun.mID==b_native_id && !b_actor->mbDead;
    return false;
}
bool owned_damage() {return damage_owner==2 && damage_actor==b_actor && damage_id==b_id && live();}
bool readable(const void* address,size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if(!address || !VirtualQuery(address,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto begin=reinterpret_cast<uintptr_t>(address),region=reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin>=region && size<=info.RegionSize && begin-region<=info.RegionSize-size;
}
Animal* current_corpse(Animal* candidate,uint32_t expected=UINT32_MAX) {
    auto manager=Manager::Get();
    if(!candidate || !manager)return nullptr;
    for(auto& noun:manager->mNouns)if(&noun==static_cast<Simulator::cGameData*>(candidate)) {
        if(!readable(candidate,sizeof(Animal)) || noun.mbIsDestroyed || noun.field_20 ||
           candidate->mbMarkedForDeletion || !candidate->mbEnabled || !candidate->mbDead ||
           *reinterpret_cast<const uintptr_t*>(candidate)!=image_base+0x106a080 ||
           (expected!=UINT32_MAX && noun.mID!=expected))return nullptr;
        return candidate;
    }
    return nullptr;
}
bool owned_pickup() {
    if(GetCurrentThreadId()!=engine_thread || pickup_owner!=2 || pickup_actor!=b_actor || pickup_id!=b_id ||
       pickup_corpse_native_id==UINT32_MAX || !thread_check || !thread_check() || !live())return false;
    auto corpse=current_corpse(pickup_corpse,pickup_corpse_native_id);
    return corpse && corpse->mpWhoIsInteractingWithMe.get()==pickup_actor;
}
struct Progress {
    float dna,goal,combat,social;
};
Progress progress(Player* player,float dna) {
    return {dna,player->mCurrentGoalProgress,player->mCombatTraitProgress,player->mSocialTraitProgress};
}
Player* first_player() {
    auto manager=Manager::Get();
    auto candidate=manager && player_original?player_original(manager):nullptr;
    if(!candidate)return nullptr;
    for(auto& noun:manager->mNouns)if(&noun==static_cast<Simulator::cGameData*>(candidate))
        return !noun.mbIsDestroyed && !noun.field_20 && readable(candidate,sizeof(Player))?candidate:nullptr;
    return nullptr;
}
void pickup_progress_event(const char* name,const char* kind,uint32_t where,float amount,
    const Progress& a_before,const Progress& a_after,const Progress& b_before,const Progress& b_after) {
    event(name,",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"reward_kind\":\"%s\",\"caller_rva\":%u,"
        "\"amount\":%.9g,\"a_dna_before\":%.9g,\"a_dna_after\":%.9g,\"b_dna_before\":%.9g,\"b_dna_after\":%.9g,"
        "\"a_goal_before\":%.9g,\"a_goal_after\":%.9g,\"b_goal_before\":%.9g,\"b_goal_after\":%.9g,"
        "\"a_combat_before\":%.9g,\"a_combat_after\":%.9g,\"b_combat_before\":%.9g,\"b_combat_after\":%.9g,"
        "\"a_social_before\":%.9g,\"a_social_after\":%.9g,\"b_social_before\":%.9g,\"b_social_after\":%.9g,"
        "\"scope_first_feed_return_observed\":%s,\"native_dna_increased\":%s,\"awards\":%llu",
        b_id,pickup_corpse_native_id,kind,where,double(amount),
        double(a_before.dna),double(a_after.dna),double(b_before.dna),double(b_after.dna),
        double(a_before.goal),double(a_after.goal),double(b_before.goal),double(b_after.goal),
        double(a_before.combat),double(a_after.combat),double(b_before.combat),double(b_after.combat),
        double(a_before.social),double(a_after.social),double(b_before.social),double(b_after.social),
        pickup_granted?"true":"false",
        std::isfinite(b_before.dna) && std::isfinite(b_after.dna) && b_after.dna>b_before.dna?"true":"false",awards);
}
// Enter only around a known native reward calculation/application or B counter.
// Original world AI/physics never run under this switch. Scoped getters route
// identities without replacing the noun manager's avatar/player pointers.
class StageScope {
    Stage saved{};
    bool outer;
public:
    StageScope():outer(!context_active) {
        if(outer) {saved=*Stage::Get();*Stage::Get()=b_stage;context_active=true;}
    }
    ~StageScope() {
        if(outer) {b_stage=*Stage::Get();*Stage::Get()=saved;context_active=false;}
    }
};
bool __fastcall owned_hook(Spatial* self,void*) {
    const auto where=caller(_ReturnAddress());
    const bool result=owned_original(self);
    if(!result && GetCurrentThreadId()==engine_thread &&
       (where==0x807ac9 || where==0x807b87) && owned_damage() &&
       self==static_cast<Spatial*>(b_actor)) {
        event("native_reward_owner_classified",",\"owner\":2,\"actor\":%llu,\"caller_rva\":%u",b_id,where);
        return true;
    }
    return result;
}
float __fastcall amount_hook(Animal* self,void*,bool social) {
    if(GetCurrentThreadId()!=engine_thread || caller(_ReturnAddress())!=0x807be4 || !owned_damage())
        return amount_original(self,social);
    StageScope scope;
    const float amount=amount_original(self,social);
    event("native_owned_reward_amount",",\"owner\":2,\"actor\":%llu,\"victim_native_id\":%u,\"amount\":%.9g",b_id,self->mID,double(amount));
    return amount;
}
Player* __fastcall player_hook(Manager* self,void*) {
    auto result=player_original(self);
    if(GetCurrentThreadId()==engine_thread &&
       (context_active || (caller(_ReturnAddress())==0x807c0a && owned_damage()))) {
        if(auto player=native_second_player(b_epoch))return player;
    }
    return result;
}
Animal* __fastcall avatar_hook(Manager* self,void*) {
    auto result=avatar_original(self);
    return GetCurrentThreadId()==engine_thread && context_active && ready?b_actor:result;
}
void __fastcall display_hook(void* self,void*,float amount) {
    if(GetCurrentThreadId()==engine_thread && context_active && ready) {
        // Preserve the native add routine by adjusting its receiver so its own
        // +128 selects B's constructed counter. That routine touches no other field.
        display_original(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(b_counter.data())-0x128),amount);
        event("native_owned_reward_enqueued",",\"owner\":2,\"actor\":%llu,\"amount\":%.9g",b_id,double(amount));
    } else display_original(self,amount);
}
void __fastcall action_hook(Strategy* self,void*,uint32_t id,void* payload) {
    if(!native_replica_allows_action(id)) return;
    const auto where=caller(_ReturnAddress());
    if(GetCurrentThreadId()==engine_thread && id==native_pickup_action &&
       where==0x9713dc && pickup_owner==2) {
        // D713B7..D713D7 initializes exactly three words. A fourth SDK-inferred
        // payload word is not present at this callsite and must never be read.
        std::array<uintptr_t,3> words{};
        const bool valid_payload=readable(payload,sizeof(words));
        if(valid_payload)std::memcpy(words.data(),payload,sizeof(words));
        auto player=owned_pickup()?native_second_player(b_epoch):nullptr;
        auto first=player?first_player():nullptr;
        if(!player || !first || context_active || !valid_payload ||
           words[0]!=reinterpret_cast<uintptr_t>(pickup_actor) ||
           words[1]!=reinterpret_cast<uintptr_t>(pickup_corpse) || words[2]!=0) {
            event("native_owned_pickup_action_rejected",",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"action\":%u,\"caller_rva\":%u",
                pickup_id,pickup_corpse_native_id,id,where);
            return;
        }
        const auto a_before=progress(first,Stage::Get()->mEvolutionPoints);
        const auto b_before=progress(player,b_stage.mEvolutionPoints);
        // Only this original action (and its synchronous native handlers) uses
        // B's campaign/player getters. The whole eating tick stays unmodified.
        {StageScope scope;action_original(self,id,payload);}
        pickup_progress_event("native_owned_pickup_action_applied","eat_meat_action",where,0,
            a_before,progress(first,Stage::Get()->mEvolutionPoints),
            b_before,progress(player,b_stage.mEvolutionPoints));
        return;
    }
    if(GetCurrentThreadId()!=engine_thread || id!=0x045ab96e || !ready) {action_original(self,id,payload);return;}
    const bool selected_b=context_active;
    const bool previous=self->field_48;
    action_original(self,id,payload);
    if(selected_b) {
        b_world_pending=self->field_48;self->field_48=previous;
        event("native_owned_reward_action",",\"owner\":2,\"actor\":%llu,\"action\":%u,\"world_pending\":%s",
            b_id,id,b_world_pending?"true":"false");
    } else if(world_selected) {world_saved_a=self->field_48;self->field_48=previous;}
}
float __cdecl goal_hook() {
    if(GetCurrentThreadId()==engine_thread && caller(_ReturnAddress())==0x945aa9 && live() && b_world_pending) {
        auto strategy=Strategy::Get();
        auto a=avatar_original(Manager::Get());
        // Current M03 fixture uses the same native species profile. Divergent
        // species require a separate qualified world-selection binding.
        if(strategy && a && a->mpSpeciesProfile==b_actor->mpSpeciesProfile &&
           (!strategy->field_48 || alternate_world)) {
            world_saved_a=strategy->field_48;world_selected=true;
            strategy->field_48=b_world_pending;
            StageScope scope;
            return goal_original();
        }
    }
    return goal_original();
}
void __fastcall update_hook(Strategy* self,void*,float dt,float elapsed) {
    if(GetCurrentThreadId()!=engine_thread || !ready) {update_original(self,dt,elapsed);return;}
    world_selected=false;
    // The original complete mode update executes exactly once per engine call.
    // Only its existing herd-evolution input uses a pending B goal on selected ticks.
    update_original(self,dt,elapsed);
    if(world_selected) {
        b_world_pending=self->field_48;self->field_48=world_saved_a;++world_updates;
        if(world_updates<=3 || !b_world_pending)event("native_owned_world_update",",\"owner\":2,\"actor\":%llu,\"updates\":%llu,\"pending\":%s",
            b_id,world_updates,b_world_pending?"true":"false");
    }
    world_selected=false;alternate_world=!alternate_world;
}
}
NativePickupOwnerScope::NativePickupOwnerScope(Animal* actor,uint32_t owner,uint64_t id,Animal* corpse)
    :previous_actor(pickup_actor),previous_corpse(pickup_corpse),previous_owner(pickup_owner),
     previous_corpse_native_id(pickup_corpse_native_id),previous_id(pickup_id),previous_granted(pickup_granted) {
    pickup_actor=actor;pickup_corpse=corpse;pickup_owner=owner;pickup_id=id;pickup_corpse_native_id=UINT32_MAX;
    pickup_granted=false;
    if(GetCurrentThreadId()==engine_thread && thread_check && thread_check())
        if(auto current=current_corpse(corpse))pickup_corpse_native_id=current->mID;
}
NativePickupOwnerScope::~NativePickupOwnerScope() {
    pickup_actor=previous_actor;pickup_corpse=previous_corpse;pickup_owner=previous_owner;
    pickup_corpse_native_id=previous_corpse_native_id;pickup_id=previous_id;
    pickup_granted=previous_granted;
}
bool native_pickup_award_scope_ready() {return owned_pickup();}
bool native_pickup_award_scope_granted() {return GetCurrentThreadId()==engine_thread && pickup_owner==2 && pickup_granted;}
NativeDamageOwnerScope::NativeDamageOwnerScope(Animal* actor,uint32_t owner,uint64_t id)
    :previous_actor(damage_actor),previous_owner(damage_owner),previous_id(damage_id) {
    damage_actor=actor;damage_owner=owner;damage_id=id;
}
NativeDamageOwnerScope::~NativeDamageOwnerScope() {damage_actor=previous_actor;damage_owner=previous_owner;damage_id=previous_id;}
#define BINDING(name,original,hook) NativeHook name(){return {reinterpret_cast<void**>(&original),reinterpret_cast<void*>(hook)};}
BINDING(native_award_owned_binding,owned_original,owned_hook)
BINDING(native_award_amount_binding,amount_original,amount_hook)
BINDING(native_award_player_binding,player_original,player_hook)
BINDING(native_award_avatar_binding,avatar_original,avatar_hook)
BINDING(native_award_display_binding,display_original,display_hook)
BINDING(native_award_action_binding,action_original,action_hook)
BINDING(native_award_update_binding,update_original,update_hook)
BINDING(native_award_goal_binding,goal_original,goal_hook)
#undef BINDING

bool prepare_native_awards(uintptr_t base,uintptr_t end,DWORD thread,PlayerContextEvent event_sink,bool(*check)()) {
    image_base=base;engine_thread=thread;sink=event_sink;thread_check=check;
    // Generated from the already SHA-256-pinned PE. Relocation entries compare
    // their exact rebased address; they are never wildcarded.
    struct Prefix {uint32_t rva;unsigned char bytes[20];uint32_t relocations;unsigned length;};
    static const Prefix prefixes[]={
#include "native_award_prefixes.inc"
    };
    for(const auto& item:prefixes) {
        unsigned char bytes[20];memcpy(bytes,item.bytes,sizeof(bytes));
        for(unsigned i=0;i+4<=item.length;++i)if(item.relocations&(1u<<i)) {
            uint32_t value;memcpy(&value,bytes+i,4);value+=static_cast<uint32_t>(base)-0x400000;memcpy(bytes+i,&value,4);
        }
        if(item.rva>end-base || item.length>end-base-item.rva ||
           memcmp(reinterpret_cast<void*>(base+item.rva),bytes,item.length)) {
            event("award_binding_rejected",",\"rva\":%u",item.rva);return false;
        }
    }
    owned_original=reinterpret_cast<Owned>(base+0x802ff0);
    amount_original=reinterpret_cast<Amount>(base+0x8042a0);
    player_original=reinterpret_cast<PlayerGet>(base+0xb67d40);
    avatar_original=reinterpret_cast<AvatarGet>(base+0x71fd00);
    display_original=reinterpret_cast<DisplayAdd>(base+0x92e2e0);
    action_original=reinterpret_cast<Action>(base+0x939360);
    update_original=reinterpret_cast<Update>(base+0x945740);
    goal_original=reinterpret_cast<Goal>(base+0x92e360);
    deque_init=reinterpret_cast<InitDeque>(base+0x92c490);
    deque_destroy=reinterpret_cast<ObjectCall>(base+0x92c940);
    timer_init=reinterpret_cast<ObjectCall>(base+0x7639d0);
    timer_destroy=reinterpret_cast<ObjectCall>(base+0x7639f0);
    counter_tick=reinterpret_cast<CounterTick>(base+0x92d0b0);
    stage_reset=reinterpret_cast<StageReset>(base+0x92e980);
    event("award_bindings_checked",",\"code_prefixes\":%u",unsigned(std::size(prefixes)));
    return true;
}
bool enable_native_awards(Animal* actor,uint64_t id,uint64_t epoch) {
    if(GetCurrentThreadId()!=engine_thread || ready || !actor || actor->mbDead || !thread_check || !thread_check())return false;
    auto player=native_second_player(epoch);auto manager=Manager::Get();
    auto first=manager?manager->GetPlayer():nullptr;auto avatar=manager?manager->GetAvatar():nullptr;
    auto strategy=Strategy::Get();
    if(!player || !first || !avatar || !strategy || !strategy->mpDisplayStrategy ||
       actor->mpSpeciesProfile!=avatar->mpSpeciesProfile || actor->GetCurrentBrainLevel()!=0 ||
       player->mCurrentGoalProgress!=0 || player->mGoalProgressTotal!=1)return false;
    // Native stage configuration, same addition order as D43E76..D43EAE.
    float total=Stage::GetEvoPointsToNextBrainLevel(3);
    for(int level=2;level>=0;--level)total+=Stage::GetEvoPointsToNextBrainLevel(level);
    if(!std::isfinite(total) || total<=0 || total!=first->mGoalProgressTotal)return false;
    b_actor=actor;b_native_id=actor->mID;b_id=id;b_epoch=epoch;b_stage={};b_counter.fill(0);
    retired_actor_id=retired_epoch=0;retired_dna_valid=false;
    deque_init(b_counter.data()+4,0);timer_init(b_counter.data()+0x30);
    ready=true;
    {StageScope scope;stage_reset();Stage::Get()->mCurrentBrainLevel=actor->GetCurrentBrainLevel();Stage::CalculateAvatarNormalizingScale();}
    player->mGoalProgressTotal=total;
    b_world_pending=false;boundary_reported=false;awards=counter_updates=world_updates=0;
    event("native_awards_enabled",",\"owner\":2,\"actor\":%llu,\"player_native_id\":%u,\"goal_total\":%.9g,\"brain\":%d",
        b_id,player->mID,double(total),b_stage.mCurrentBrainLevel);
    sample_native_awards();return true;
}
bool native_awards_ready(uint64_t id,uint64_t epoch) {
    return id && epoch && id==b_id && epoch==b_epoch && live();
}
bool native_awards_read_dna(uint64_t id,uint64_t epoch,float& dna,bool allow_retired_corpse,bool* current_native) {
    if(current_native)*current_native=false;
    if(GetCurrentThreadId()!=engine_thread || !id || !epoch)return false;
    if(ready && id==b_id && epoch==b_epoch && native_second_player(epoch) &&
       (allow_retired_corpse || live()) && std::isfinite(b_stage.mEvolutionPoints) && b_stage.mEvolutionPoints>=0) {
        dna=b_stage.mEvolutionPoints;if(current_native)*current_native=true;return true;
    }
    if(allow_retired_corpse && retired_dna_valid && id==retired_actor_id && epoch==retired_epoch) {
        dna=retired_dna;return true;
    }
    return false;
}
bool dispatch_native_owned_award(float amount,void(__cdecl* original)(float),uint32_t where) {
    if(GetCurrentThreadId()==engine_thread && where==native_pickup_reward_caller_rva && pickup_owner==2) {
        auto player=owned_pickup()?native_second_player(b_epoch):nullptr;
        auto first=player?first_player():nullptr;
        if(!player || !first || context_active || !original) {
            event("native_owned_pickup_reward_rejected",",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"caller_rva\":%u",
                pickup_id,pickup_corpse_native_id,where);
            return true;
        }
        const auto a_before=progress(first,Stage::Get()->mEvolutionPoints);
        const auto b_before=progress(player,b_stage.mEvolutionPoints);
        {StageScope scope;original(amount);}
        // D713A3 reads the configured original float, which can legitimately be
        // zero. D2E8A0 then returns without changing DNA. For positive amounts
        // its original cap may retain or reduce the expected increase. Observe
        // the returned call; never replace its amount or infer nutrition here.
        const float after=b_stage.mEvolutionPoints;
        const float uncapped=b_before.dna+amount;
        const bool returned=std::isfinite(amount) && amount>=0 && std::isfinite(b_before.dna) &&
            std::isfinite(after) && std::isfinite(uncapped) && after>=b_before.dna &&
            (amount==0 ? after==b_before.dna : after<=uncapped);
        pickup_granted=pickup_granted || returned;
        ++awards;
        pickup_progress_event("native_owned_pickup_reward_applied","corpse_first_feed",where,amount,
            a_before,progress(first,Stage::Get()->mEvolutionPoints),
            b_before,progress(player,b_stage.mEvolutionPoints));
        return true;
    }
    if(GetCurrentThreadId()==engine_thread && context_active && pickup_owner==2 && where==0x946cd3) {
        // D46BE0's optional property DNA effect is already inside EatMeat's
        // StageScope. Preserve one original application; do not consult the
        // unrelated damage scope or enter another owner context.
        if(!owned_pickup() || !original) {
            event("native_owned_pickup_property_reward_rejected",",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,\"caller_rva\":%u",
                pickup_id,pickup_corpse_native_id,where);
            return true;
        }
        const float before=Stage::Get()->mEvolutionPoints;
        original(amount);++awards;
        event("native_owned_pickup_property_reward_applied",",\"owner\":2,\"actor\":%llu,\"corpse_native_id\":%u,"
            "\"reward_kind\":\"corpse_property\",\"caller_rva\":%u,\"amount\":%.9g,\"dna_before\":%.9g,\"dna_after\":%.9g,\"awards\":%llu",
            b_id,pickup_corpse_native_id,where,double(amount),double(before),double(Stage::Get()->mEvolutionPoints),awards);
        return true;
    }
    if(GetCurrentThreadId()!=engine_thread || where!=0x807bf1 || !owned_damage())return false;
    auto player=native_second_player(b_epoch);
    const float before=b_stage.mEvolutionPoints,goal=player->mCurrentGoalProgress;
    {StageScope scope;original(amount);}
    ++awards;
    event("native_owned_reward_applied",",\"owner\":2,\"actor\":%llu,\"amount\":%.9g,\"dna_before\":%.9g,\"dna_after\":%.9g,"
        "\"goal_before\":%.9g,\"goal_after\":%.9g,\"awards\":%llu",
        b_id,double(amount),double(before),double(b_stage.mEvolutionPoints),double(goal),double(player->mCurrentGoalProgress),awards);
    return true;
}
void update_native_awards(uint64_t epoch) {
    if(GetCurrentThreadId()!=engine_thread || !ready)return;
    if(epoch!=b_epoch || !live()) {retire_native_awards();return;}
    auto player=native_second_player(epoch);
    // M03 qualifies the first native brain-level segment. Native brain upgrades
    // also touch shared species/cinematics, so retain queued work at that boundary
    // until the later campaign progression adapter is qualified. Never drop it.
    const float threshold=Stage::GetEvoPointsToNextBrainLevel(0);
    if(player->mCurrentGoalProgress>=threshold) {
        if(!boundary_reported)event("native_reward_stage_boundary",",\"owner\":2,\"actor\":%llu,\"queued_work_retained\":true",b_id);
        boundary_reported=true;return;
    }
    const auto before=*reinterpret_cast<uintptr_t*>(b_counter.data()+0xc);
    {StageScope scope;counter_tick(b_counter.data(),0);}
    ++counter_updates;
    if(before!=*reinterpret_cast<uintptr_t*>(b_counter.data()+0xc))
        event("native_owned_reward_dequeued",",\"owner\":2,\"actor\":%llu,\"counter_updates\":%llu",b_id,counter_updates);
}
void sample_native_awards() {
    if(GetCurrentThreadId()!=engine_thread || !ready)return;
    auto player=native_second_player(b_epoch);if(!player)return;
    const bool empty=*reinterpret_cast<uintptr_t*>(b_counter.data()+0xc)==*reinterpret_cast<uintptr_t*>(b_counter.data()+0x1c);
    event("native_owned_reward_state",",\"owner\":2,\"actor\":%llu,\"dna\":%.9g,\"goal\":%.9g,\"goal_total\":%.9g,"
        "\"combat_progress\":%.9g,\"brain\":%d,\"queue_empty\":%s,\"counter_state\":%u,\"counter_updates\":%llu,\"world_updates\":%llu,\"awards\":%llu",
        b_id,double(b_stage.mEvolutionPoints),double(player->mCurrentGoalProgress),double(player->mGoalProgressTotal),
        double(player->mCombatTraitProgress),b_stage.mCurrentBrainLevel,empty?"true":"false",
        *reinterpret_cast<uint32_t*>(b_counter.data()),counter_updates,world_updates,awards);
}
void retire_native_awards() {
    if(GetCurrentThreadId()!=engine_thread || !ready)return;
    sample_native_awards();
    retired_actor_id=b_id;retired_epoch=b_epoch;retired_dna=b_stage.mEvolutionPoints;
    retired_dna_valid=std::isfinite(retired_dna) && retired_dna>=0;
    deque_destroy(b_counter.data()+4);timer_destroy(b_counter.data()+0x30);
    ready=false;b_actor=nullptr;b_native_id=UINT32_MAX;
    event("native_awards_retired",",\"owner\":2,\"actor\":%llu,\"awards\":%llu,\"counter_updates\":%llu,\"world_updates\":%llu",
        b_id,awards,counter_updates,world_updates);
    b_id=b_epoch=0;b_world_pending=false;
}
}
