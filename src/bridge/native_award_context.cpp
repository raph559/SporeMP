#include "native_award_context.h"
#include "native_replica.h"
#include "native_award_abi.h"
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
// Native display counter: state +00, native deque +04 (40 bytes), timer +30.
// Constructors/destructors and queue processing are original game functions.
// There is no SDK/container destructor at DLL unload and no native pointer leaves the bridge.
alignas(8) std::array<unsigned char,0x50> b_counter{};
Animal* damage_actor=nullptr;
uint32_t damage_owner=0;
uint64_t damage_id=0;

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
    deque_init(b_counter.data()+4,0);timer_init(b_counter.data()+0x30);
    ready=true;
    {StageScope scope;stage_reset();Stage::Get()->mCurrentBrainLevel=actor->GetCurrentBrainLevel();Stage::CalculateAvatarNormalizingScale();}
    player->mGoalProgressTotal=total;
    b_world_pending=false;boundary_reported=false;awards=counter_updates=world_updates=0;
    event("native_awards_enabled",",\"owner\":2,\"actor\":%llu,\"player_native_id\":%u,\"goal_total\":%.9g,\"brain\":%d",
        b_id,player->mID,double(total),b_stage.mCurrentBrainLevel);
    sample_native_awards();return true;
}
bool dispatch_native_owned_award(float amount,void(__cdecl* original)(float),uint32_t where) {
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
    deque_destroy(b_counter.data()+4);timer_destroy(b_counter.data()+0x30);
    ready=false;b_actor=nullptr;b_native_id=UINT32_MAX;
    event("native_awards_retired",",\"owner\":2,\"actor\":%llu,\"awards\":%llu,\"counter_updates\":%llu,\"world_updates\":%llu",
        b_id,awards,counter_updates,world_updates);
    b_id=b_epoch=0;b_world_pending=false;
}
}
