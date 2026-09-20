#include "native_award_abi.h"
#include "detour_transaction.h"
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {
struct Object {
    int amount_calls=0,update_calls=0,action_calls=0;
    bool social=false;
    float dt=0,elapsed=0;
    uint32_t action_id=0;
    void* payload=nullptr;
    __declspec(noinline) float amount(bool value) {++amount_calls;social=value;return value?1.25f:-0.375f;}
    __declspec(noinline) void update(float first,float second) {++update_calls;dt=first;elapsed=second;}
    __declspec(noinline) void action(uint32_t id,void* data) {++action_calls;action_id=id;payload=data;}
};
struct Manager {
    unsigned char padding[0x74]{};
    Object* player=nullptr;
    // Intentionally four bytes + alignment padding in optimized x86, matching
    // the game's folded getter shape; Detours must handle the short function.
    __declspec(noinline) Object* get() {return player;}
};
using Abi=sporemp::NativeAwardAbi<Object,Object,Object,Manager,Object>;
Abi::Amount amount_original=nullptr;
Abi::Update update_original=nullptr;
Abi::Action action_original=nullptr;
Abi::PlayerGet get_original=nullptr;
int amounts=0,updates=0,actions=0,gets=0;
float __fastcall amount_hook(Object* self,void*,bool social) {++amounts;return amount_original(self,social);}
void __fastcall update_hook(Object* self,void*,float dt,float elapsed) {++updates;update_original(self,dt,elapsed);}
void __fastcall action_hook(Object* self,void*,uint32_t id,void* payload) {++actions;action_original(self,id,payload);}
Object* __fastcall get_hook(Manager* self,void*) {++gets;return get_original(self);}
template<class Function,class Member> void address(Function& fn,Member member) {
    static_assert(sizeof(fn)==sizeof(member));memcpy(&fn,&member,sizeof(fn));
}
}
int native_award_abi_checks() {
    int failed=0;
    auto check=[&](bool value,const char* label) {std::printf("%s: %s (HOST ABI; SPORE NOT LOADED)\n",value?"PASS":"FAIL",label);if(!value)++failed;};
    address(amount_original,&Object::amount);address(update_original,&Object::update);
    address(action_original,&Object::action);address(get_original,&Manager::get);
    const sporemp::NativeHook hooks[]={
        {reinterpret_cast<void**>(&amount_original),reinterpret_cast<void*>(amount_hook)},
        {reinterpret_cast<void**>(&update_original),reinterpret_cast<void*>(update_hook)},
        {reinterpret_cast<void**>(&action_original),reinterpret_cast<void*>(action_hook)},
        {reinterpret_cast<void**>(&get_original),reinterpret_cast<void*>(get_hook)}};
    Object o;Manager m;m.player=&o;
    const auto status=sporemp::change_hooks(hooks,std::size(hooks),true);
    std::printf("award hook transaction status=%ld; getter prefix=%02x %02x %02x %02x %02x %02x %02x %02x\n",status,
        ((unsigned char*)get_original)[0],((unsigned char*)get_original)[1],((unsigned char*)get_original)[2],((unsigned char*)get_original)[3],
        ((unsigned char*)get_original)[4],((unsigned char*)get_original)[5],((unsigned char*)get_original)[6],((unsigned char*)get_original)[7]);
    check(status==NO_ERROR,"four award signature hooks attach, including the tiny getter");
    if(status!=NO_ERROR) {
        for(size_t i=0;i<std::size(hooks);++i) {
            const auto single=sporemp::change_hooks(hooks+i,1,true);
            std::printf("single award hook %u attach=%ld\n",unsigned(i),single);
            if(single==NO_ERROR)std::printf("single detach=%ld\n",sporemp::change_hooks(hooks+i,1,false));
        }
        return failed;
    }
    float(Object::*volatile amount)(bool)=&Object::amount;
    void(Object::*volatile update)(float,float)=&Object::update;
    void(Object::*volatile action)(uint32_t,void*)=&Object::action;
    Object*(Manager::*volatile get)()=&Manager::get;
    check((o.*amount)(true)==1.25f && o.social && amounts==1 && o.amount_calls==1,"thiscall bool argument and positive x87 award return");
    check((o.*amount)(false)==-0.375f && !o.social && amounts==2 && o.amount_calls==2,"false argument and negative x87 result preserve stack and count");
    (o.*update)(0.016125f,-123.5f);
    check(o.dt==0.016125f && o.elapsed==-123.5f && o.update_calls==1 && updates==1,"mode update preserves both float slots and exactly one original");
    (o.*action)(0xf1234567,&m);
    check(o.action_id==0xf1234567 && o.payload==&m && o.action_calls==1 && actions==1,"void action preserves full-width ID and payload");
    check((m.*get)()==&o && gets==1,"tiny native-shaped manager getter retains its pointer result");
    m.player=nullptr;
    check((m.*get)()==nullptr && gets==2,"tiny getter retains null and exact original count");
    check(sporemp::change_hooks(hooks,std::size(hooks),false)==NO_ERROR,"all award ABI hooks detach");
    return failed;
}
