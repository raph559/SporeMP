#include "native_pickup_abi.h"
#include "detour_transaction.h"
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {
struct Animal {
    unsigned calls=0;
    uint32_t words[8]{};
    float priority=0;
    Animal* target=nullptr;
    bool replace=false;
    __declspec(noinline) void* order(uint32_t a,uint32_t b,uint32_t c,uint32_t d,float value,
        uint32_t e,uint32_t f,uint32_t g,uint32_t h,Animal* other,bool change) {
        ++calls; words[0]=a;words[1]=b;words[2]=c;words[3]=d;
        words[4]=e;words[5]=f;words[6]=g;words[7]=h;
        priority=value;target=other;replace=change;
        return other;
    }
};
using Abi=sporemp::NativePickupAbi<Animal>;
Abi::Order order_original=nullptr;
Abi::Tick tick_original=nullptr;
unsigned order_hooks=0,tick_hooks=0;
void* __fastcall order_hook(Animal* self,void*,uint32_t a,uint32_t b,uint32_t c,uint32_t d,float value,
    uint32_t e,uint32_t f,uint32_t g,uint32_t h,Animal* target,bool replace) {
    ++order_hooks;
    return order_original(self,a,b,c,d,value,e,f,g,h,target,replace);
}
struct TickArguments {
    Animal* actor=nullptr; double clock=0; uint32_t flags=0; uintptr_t parameter=0;
    void* state=nullptr; void* activation=nullptr; float delta=0; unsigned calls=0;
} tick_args;
__declspec(noinline) bool __cdecl tick_fixture(Animal* actor,double clock,uint32_t flags,
    uintptr_t parameter,void* state,void* activation,float delta) {
    tick_args={actor,clock,flags,parameter,state,activation,delta,tick_args.calls+1};
    return actor && flags==0xfedcba98;
}
bool __cdecl tick_hook(Animal* actor,double clock,uint32_t flags,uintptr_t parameter,
    void* state,void* activation,float delta) {
    ++tick_hooks;return tick_original(actor,clock,flags,parameter,state,activation,delta);
}
}

int native_pickup_abi_checks() {
    int failures=0;
    const auto check=[&](bool result,const char* description) {
        std::printf("%s: %s (HOST ABI FIXTURE; SPORE NOT LOADED)\n",result?"PASS":"FAIL",description);
        if(!result)++failures;
    };
    auto member=&Animal::order;
    static_assert(sizeof(member)==sizeof(order_original),"Win32 thiscall member representation");
    std::memcpy(&order_original,&member,sizeof(member));
    tick_original=&tick_fixture;
    sporemp::NativeHook hooks[]={
        {reinterpret_cast<void**>(&order_original),reinterpret_cast<void*>(order_hook)},
        {reinterpret_cast<void**>(&tick_original),reinterpret_cast<void*>(tick_hook)}
    };
    const auto attached=sporemp::change_hooks(hooks,std::size(hooks),true);
    check(attached==NO_ERROR,"pickup eleven-word thiscall and eight-word cdecl hook attachment");
    if(attached!=NO_ERROR)return failures;
    Animal actor,corpse;
    uint32_t expected[]={0x40000,0xfedcba98,0x2788b9,0x40049000,0x100000,0x81234567,0x80000000,0xffffffff};
    auto result=actor.order(expected[0],expected[1],expected[2],expected[3],12345.75f,
        expected[4],expected[5],expected[6],expected[7],&corpse,true);
    check(result==&corpse && actor.calls==1 && order_hooks==1 && actor.target==&corpse && actor.replace &&
        actor.priority==12345.75f && !std::memcmp(actor.words,expected,sizeof(expected)),
        "pickup thiscall preserves all eleven slots, receiver, float, boolean and pointer return exactly once");
    result=actor.order(0,0,0,0,-.5f,0,0,0,0,nullptr,false);
    check(!result && !actor.target && !actor.replace && actor.priority==-.5f && actor.calls==2 && order_hooks==2,
        "pickup thiscall preserves null/false/negative-float path");
    Abi::Tick volatile call=&tick_fixture;
    constexpr double clock=987654321.123456;
    check(call(&actor,clock,0xfedcba98,0xffffffff,&corpse,&actor,.125f) && tick_args.actor==&actor &&
        tick_args.clock==clock && tick_args.flags==0xfedcba98 && tick_args.parameter==0xffffffff &&
        tick_args.state==&corpse && tick_args.activation==&actor && tick_args.delta==.125f &&
        tick_args.calls==1 && tick_hooks==1,
        "pickup cdecl preserves double clock, high-bit parameter, both context pointers, dt and AL true");
    check(!call(nullptr,-clock,0x81234567,0,nullptr,nullptr,-.25f) && !tick_args.actor &&
        tick_args.clock==-clock && tick_args.flags==0x81234567 && !tick_args.parameter && !tick_args.state &&
        !tick_args.activation && tick_args.delta==-.25f && tick_args.calls==2 && tick_hooks==2,
        "pickup cdecl forwards false/null path once with all stack words preserved");
    uintptr_t before=0,after=0;
    __asm mov before,esp
    for(unsigned i=0;i<4096;++i) {
        call(&actor,clock,0xfedcba98,i,&corpse,nullptr,.25f);
        actor.order(i,0,0,0,.75f,0,0,0,0,&corpse,false);
    }
    __asm mov after,esp
    check(before==after && actor.calls==4098 && order_hooks==4098 && tick_args.calls==4098 && tick_hooks==4098,
        "4096 mixed pickup callbacks retain exact stack balance and one original call");
    check(sporemp::change_hooks(hooks,std::size(hooks),false)==NO_ERROR,"pickup ABI hooks detach");
    return failures;
}
#ifdef SPOREMP_PICKUP_ABI_STANDALONE
int main() { return native_pickup_abi_checks()?1:0; }
#endif
