#include "native_actor_abi.h"
#include "detour_transaction.h"
#include <cstdio>
#include <cstring>
#include <iterator>
int native_award_abi_checks();
int native_persistence_abi_checks();

namespace {
// HOST fixtures only. Arithmetic is an argument checksum, not simulated combat.
struct Vector { float x,y,z; };
struct Combatant {
    int calls=0;
    float checksum=0;
    uint32_t political=0;
    int kind=0;
    const Vector* direction=nullptr;
    Combatant* source=nullptr;
    __declspec(noinline) int damage(float amount,uint32_t id,int type,const Vector& vector,Combatant* attacker) {
        ++calls;checksum+=amount+vector.x+vector.y+vector.z;
        political=id;kind=type;direction=&vector;source=attacker;
        return type-7;
    }
};
struct Creature {
    int selections=0,strikes=0;
    int index=0;
    bool first=false,second=false;
    Combatant source;
    int set_calls=0,done_calls=0;
    uint32_t animation=0;
    __declspec(noinline) void set_attack(int value) {++set_calls;index=value;}
    __declspec(noinline) bool animation_done(uint32_t value) {++done_calls;animation=value;return value==0xf1234567;}
    __declspec(noinline) int select(int value,bool a,bool b) {
        ++selections;index=value;first=a;second=b;
        return a && !b ? value : -1;
    }
    __declspec(noinline) bool strike(Combatant* target,int value,Vector* vector) {
        ++strikes;
        if(!target || !vector) return false;
        int (Combatant::*volatile call)(float,uint32_t,int,const Vector&,Combatant*)=&Combatant::damage;
        const int result=(target->*call)(1.25f,0xfedcba98,value,*vector,&source);
        return result==0;
    }
};
struct PlayerFixture {
    int messages=0,removes=0;
    uint32_t last_id=0;
    void* last_payload=nullptr;
    __declspec(noinline) bool message(uint32_t id,void* payload) {
        ++messages;last_id=id;last_payload=payload;return id==0xf1234567 && payload;
    }
    __declspec(noinline) void remove() {++removes;last_payload=nullptr;}
};
using PlayerAbi=sporemp::NativePlayerAbi<PlayerFixture,PlayerFixture>;
PlayerAbi::Message player_message_original=nullptr;
PlayerAbi::RemoveOwner player_remove_original=nullptr;
int player_messages=0,player_removes=0;
bool __fastcall player_message_hook(PlayerFixture* self,void*,uint32_t id,void* payload) {
    ++player_messages;return player_message_original(self,id,payload);
}
void __fastcall player_remove_hook(PlayerFixture* self,void*) {++player_removes;player_remove_original(self);}
using Abi=sporemp::NativeActorAbi<Creature,Creature,Combatant,Vector>;
Abi::SelectAbility select_original=nullptr;
Abi::Strike strike_original=nullptr;
Abi::Damage damage_original=nullptr;
Abi::SetAttack set_original=nullptr;
Abi::AnimationDone done_original=nullptr;
Abi::AttackStop stop_original=nullptr;
Abi::Decide decide_original=nullptr;
Abi::MemoryCreature memory_original=nullptr;
struct DecideArguments {
    Creature* actor=nullptr;double time=0;uint32_t flags=0;void* scratch=nullptr;
    bool* dirty=nullptr;void* decider=nullptr;void* context=nullptr;int calls=0;
} decide_args;
int decide_hooks=0,memory_hooks=0,memory_calls=0;
__declspec(noinline) float __cdecl decide_fixture(Creature* actor,double time,uint32_t flags,
        void* scratch,bool* dirty,void* decider,void* context) {
    decide_args={actor,time,flags,scratch,dirty,decider,context,decide_args.calls+1};
    if(dirty)*dirty=true;
    return actor?0.625f:-0.375f;
}
float __cdecl decide_hook(Creature* actor,double time,uint32_t flags,void* scratch,bool* dirty,void* decider,void* context) {
    ++decide_hooks;return decide_original(actor,time,flags,scratch,dirty,decider,context);
}
__declspec(noinline) Creature* __cdecl memory_fixture(void* memory) {
    ++memory_calls;return static_cast<Creature*>(memory);
}
Creature* __cdecl memory_hook(void* memory) {++memory_hooks;return memory_original(memory);}
struct StopArguments {void* context=nullptr;Creature* actor=nullptr;uint32_t flags=0;Combatant* target=nullptr;Creature* animal=nullptr;int calls=0;} stop_args;
int stop_hooks=0;
__declspec(noinline) bool __cdecl stop_fixture(void* context,Creature* actor,uint32_t flags,Combatant* target,Creature* animal) {
    stop_args={context,actor,flags,target,animal,stop_args.calls+1};
    return flags==0xfedcba98 && target==nullptr;
}
bool __cdecl stop_hook(void* context,Creature* actor,uint32_t flags,Combatant* target,Creature* animal) {
    ++stop_hooks;return stop_original(context,actor,flags,target,animal);
}
int select_calls=0,strike_calls=0,damage_calls=0,nested_damage=0;
int strike_depth=0;
void __fastcall set_hook(Creature* self,void*,int index) {set_original(self,index);}
bool __fastcall done_hook(Creature* self,void*,uint32_t animation) {return done_original(self,animation);}
int __fastcall select_hook(Creature* self,void*,int index,bool a,bool b) {
    ++select_calls;return select_original(self,index,a,b);
}
bool __fastcall strike_hook(Creature* self,void*,Combatant* target,int index,Vector* position) {
    ++strike_calls;++strike_depth;
    const bool result=strike_original(self,target,index,position);
    --strike_depth;return result;
}
int __fastcall damage_hook(Combatant* self,void*,float amount,uint32_t id,int type,const Vector& vector,Combatant* source) {
    ++damage_calls;if(strike_depth==1)++nested_damage;
    return damage_original(self,amount,id,type,vector,source);
}
template<class Function,class Member> void address(Function& function,Member member) {
    static_assert(sizeof(function)==sizeof(member),"Win32 member pointer representation");
    memcpy(&function,&member,sizeof(function));
}
}
int main() {
    int failures=0;
    const auto check=[&](bool ok,const char* message) {
        std::printf("%s: %s (HOST ABI FIXTURE; SPORE NOT LOADED)\n",ok?"PASS":"FAIL",message);
        if(!ok)++failures;
    };
    address(select_original,&Creature::select);
    address(strike_original,&Creature::strike);
    address(damage_original,&Combatant::damage);
    address(set_original,&Creature::set_attack);
    address(done_original,&Creature::animation_done);
    address(player_message_original,&PlayerFixture::message);
    address(player_remove_original,&PlayerFixture::remove);
    stop_original=&stop_fixture;
    decide_original=&decide_fixture;
    memory_original=&memory_fixture;
    const sporemp::NativeHook hooks[]={
        {reinterpret_cast<void**>(&select_original),reinterpret_cast<void*>(select_hook)},
        {reinterpret_cast<void**>(&strike_original),reinterpret_cast<void*>(strike_hook)},
        {reinterpret_cast<void**>(&damage_original),reinterpret_cast<void*>(damage_hook)},
        {reinterpret_cast<void**>(&set_original),reinterpret_cast<void*>(set_hook)},
        {reinterpret_cast<void**>(&done_original),reinterpret_cast<void*>(done_hook)},
        {reinterpret_cast<void**>(&stop_original),reinterpret_cast<void*>(stop_hook)},
        {reinterpret_cast<void**>(&decide_original),reinterpret_cast<void*>(decide_hook)},
        {reinterpret_cast<void**>(&memory_original),reinterpret_cast<void*>(memory_hook)},
        {reinterpret_cast<void**>(&player_message_original),reinterpret_cast<void*>(player_message_hook)},
        {reinterpret_cast<void**>(&player_remove_original),reinterpret_cast<void*>(player_remove_hook)}
    };
    Creature baseline,observed;
    Combatant baseline_target,observed_target;
    Vector position{2,3,4};
    check(baseline.select(31,true,false)==31 && baseline.select(19,false,true)==-1 &&
        baseline.strike(&baseline_target,7,&position) && !baseline.strike(nullptr,3,nullptr),
        "direct reference exercises both selector and strike return paths");
    const LONG attached=sporemp::change_hooks(hooks,std::size(hooks),true);
    check(attached==NO_ERROR,"ten-signature Detours transaction attaches");
    if(attached==NO_ERROR) {
        check(observed.select(31,true,false)==31 && observed.select(19,false,true)==-1 &&
            observed.index==19 && !observed.first && observed.second && observed.selections==baseline.selections,
            "integer and both boolean stack arguments and -1 result survive interception");
        check(observed.strike(&observed_target,7,&position) && !observed.strike(nullptr,3,nullptr) &&
            observed.strikes==baseline.strikes && observed_target.calls==baseline_target.calls,
            "target pointer and optional vector pointer preserve true and false strike paths");
        check(observed_target.checksum==baseline_target.checksum && observed_target.political==0xfedcba98 &&
            observed_target.kind==7 && observed_target.direction==&position && observed_target.source==&observed.source,
            "damage this pointer, float, full-width ID, type, reference and source retain exact values");
        check(select_calls==2 && strike_calls==2 && damage_calls==1 && nested_damage==1 && strike_depth==0,
            "nested native-shaped call executes each original exactly once and unwinds its scope");
        observed.set_attack(-1);
        check(observed.index==-1 && observed.set_calls==1,"void thiscall preserves the signed attack sentinel exactly once");
        check(observed.animation_done(0xf1234567) && !observed.animation_done(0x81234567) &&
            observed.animation==0x81234567 && observed.done_calls==2,"animation query preserves full-width IDs and both boolean results");
        Abi::AttackStop volatile stop_call=&stop_fixture;
        check(stop_call(&position,&observed,0xfedcba98,nullptr,&baseline) &&
            stop_args.context==&position && stop_args.actor==&observed && stop_args.flags==0xfedcba98 &&
            stop_args.target==nullptr && stop_args.animal==&baseline && stop_args.calls==1 && stop_hooks==1,
            "five-word cdecl preserves opaque context, full-width flags, null target and true return");
        check(!stop_call(nullptr,&baseline,0x81234567,&observed_target,nullptr) &&
            stop_args.context==nullptr && stop_args.actor==&baseline && stop_args.flags==0x81234567 &&
            stop_args.target==&observed_target && stop_args.animal==nullptr && stop_args.calls==2 && stop_hooks==2,
            "cdecl false path preserves all five argument positions and calls the original once");
        Abi::Decide volatile decide_call=&decide_fixture;
        bool dirty=false;
        constexpr double native_clock=987654321.123456;
        check(decide_call(&observed,native_clock,0xfedcba98,&position,&dirty,&baseline,&observed_target)==0.625f &&
            decide_args.actor==&observed && decide_args.time==native_clock && decide_args.flags==0xfedcba98 &&
            decide_args.scratch==&position && decide_args.dirty==&dirty && dirty &&
            decide_args.decider==&baseline && decide_args.context==&observed_target &&
            decide_args.calls==1 && decide_hooks==1,
            "eight-word cdecl preserves double clock, all pointers, output byte and x87 score exactly once");
        check(decide_call(nullptr,-native_clock,0x81234567,nullptr,nullptr,nullptr,nullptr)==-0.375f &&
            !decide_args.actor && decide_args.time==-native_clock && decide_args.flags==0x81234567 &&
            !decide_args.scratch && !decide_args.dirty && !decide_args.decider && !decide_args.context &&
            decide_args.calls==2 && decide_hooks==2,
            "decision cdecl null arguments and negative x87 result survive interception");
        Abi::MemoryCreature volatile memory_call=&memory_fixture;
        check(memory_call(&observed)==&observed && memory_call(nullptr)==nullptr && memory_calls==2 && memory_hooks==2,
            "one-word cdecl memory lookup returns the original pointer or null exactly once");
        PlayerFixture player;
        check(player.message(0xf1234567,&position) && player.last_id==0xf1234567 && player.last_payload==&position &&
            player.messages==1 && player_messages==1,"native player listener this pointer, full-width ID and payload survive exactly once");
        check(!player.message(0x81234567,nullptr) && player.last_id==0x81234567 && !player.last_payload &&
            player.messages==2 && player_messages==2,"native player message preserves false result and null payload");
        player.remove();
        check(player.removes==1 && player_removes==1,"native player RemoveOwner preserves receiver and one original call");
        check(sporemp::change_hooks(hooks,std::size(hooks),false)==NO_ERROR,"all ten hooks detach");
        const int previous=select_calls;
        check(observed.select(5,true,false)==5 && select_calls==previous,"detached selector runs directly");
    }
    failures+=native_award_abi_checks();
    failures+=native_persistence_abi_checks();
    extern int native_scene_abi_checks();
    if(native_scene_abi_checks()<0)++failures;
    return failures?1:0;
}
