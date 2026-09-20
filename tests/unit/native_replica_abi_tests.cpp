#include "native_replica_abi.h"
#include "native_replica.h"
#include "native_persistence_abi.h"
#include "replica_policy.h"
#include "detour_transaction.h"
#include <cstdio>
#include <cstring>

namespace {
struct Fixture {
    unsigned hunger_calls=0, save_calls=0, load_calls=0;
    float dt=0;
    const wchar_t* filename=nullptr;
    unsigned mutations=0, instance=0, group=0;
    int level=0, cost=0;
    bool removed=false;
    unsigned long long scene_clock=0;
    unsigned ability_index=0;
    unsigned message_calls=0, message_id=0;
    void* message_payload=nullptr;
    unsigned social_calls=0;
    __declspec(noinline) void hunger(float value) { ++hunger_calls; dt=value; }
    __declspec(noinline) bool save(const wchar_t* path,bool silent) { ++save_calls; filename=path; return !silent && path; }
    __declspec(noinline) void load(const wchar_t* path) { ++load_calls; filename=path; }
    __declspec(noinline) void lifecycle() { ++mutations; }
    __declspec(noinline) void brain(int value) { ++mutations; level=value; }
    __declspec(noinline) void remove(bool value) { ++mutations; removed=value; }
    __declspec(noinline) bool unlock(unsigned i,unsigned g,int c) { ++mutations; instance=i; group=g; cost=c; return c>=0; }
    __declspec(noinline) bool lock(unsigned i,unsigned g) { ++mutations; instance=i; group=g; return i!=g; }
    __declspec(noinline) void herd_update(unsigned elapsed) { ++mutations; group=elapsed; }
    __declspec(noinline) void scene_events(unsigned long long clock) { ++mutations; scene_clock=clock; }
    __declspec(noinline) void ability_use(unsigned index) { ++mutations; ability_index=index; }
    __declspec(noinline) bool message(unsigned id, void* payload) {
        ++message_calls; message_id=id; message_payload=payload; return payload==this;
    }
    __declspec(noinline) void social(unsigned* first, unsigned* second) {
        ++social_calls; *first=0xdc9fb12c; *second=0x281fbe06;
    }
};
struct Position { float x,y,z; };
unsigned creates=0;
Position created_at{};
int created_age=0;
Fixture* created_species=nullptr;
Fixture* created_herd=nullptr;
bool created_avatar=false,created_cast=false;
__declspec(noinline) Fixture* __cdecl create(const Position& at,Fixture* species,int age,Fixture* herd,bool avatar,bool cast) {
    ++creates; created_at=at;created_species=species;created_herd=herd;created_age=age;created_avatar=avatar;created_cast=cast;
    return avatar ? species : nullptr;
}
float dna=0;
unsigned dna_calls=0;
__declspec(noinline) void __cdecl set_dna(float value) { dna=value; ++dna_calls; }
using Abi=sporemp::NativeReplicaAbi<Fixture>;
using Save=sporemp::NativePersistenceAbi<Fixture>::Save;
using Load=sporemp::NativePersistenceAbi<Fixture>::Load;
Abi::Hunger hunger_original=nullptr;
Abi::DnaSet dna_original=set_dna;
Save save_original=nullptr;
Load load_original=nullptr;
Abi::Lifecycle life_original=nullptr;
Abi::Brain brain_original=nullptr;
Abi::Remove remove_original=nullptr;
Abi::Unlock unlock_original=nullptr;
Abi::Lock lock_original=nullptr;
Abi::HerdUpdate herd_original=nullptr;
Abi::SceneEvents scene_original=nullptr;
Abi::AbilityUse ability_original=nullptr;
Abi::PersistenceMessage message_original=nullptr;
Abi::SocialResult social_original=nullptr;
using Create=sporemp::NativeReplicaCreateAbi<Fixture,Position,Fixture,Fixture>::Create;
Create create_original=create;
sporemp::replica::Policy* policy=nullptr;
void __fastcall hunger_hook(Fixture* self,void*,float dt) {
    if(policy->allow(sporemp::replica::Mutation::timer)) hunger_original(self,dt);
}
bool __fastcall save_hook(Fixture* self,void*,const wchar_t* path,bool silent) {
    return policy->allow(sporemp::replica::Mutation::save) && save_original(self,path,silent);
}
void __fastcall load_hook(Fixture* self,void*,const wchar_t* path) {
    if(policy->allow(sporemp::replica::Mutation::spawn)) load_original(self,path);
}
void __cdecl dna_hook(float value) {
    using sporemp::replica::Mutation;
    if(policy->allow(Mutation::apply_state) || policy->allow(Mutation::progression)) dna_original(value);
}
void __fastcall life_hook(Fixture* self,void*) { if(policy->allow(sporemp::replica::Mutation::death)) life_original(self); }
void __fastcall brain_hook(Fixture* self,void*,int value) { if(policy->allow(sporemp::replica::Mutation::progression)) brain_original(self,value); }
void __fastcall remove_hook(Fixture* self,void*,bool value) { if(policy->allow(sporemp::replica::Mutation::death)) remove_original(self,value); }
bool __fastcall unlock_hook(void* self,void*,unsigned i,unsigned g,int c) { return policy->allow(sporemp::replica::Mutation::pickup) && unlock_original(self,i,g,c); }
bool __fastcall lock_hook(void* self,void*,unsigned i,unsigned g) { return policy->allow(sporemp::replica::Mutation::pickup) && lock_original(self,i,g); }
void __fastcall herd_hook(void* self,void*,unsigned dt) { if(policy->allow(sporemp::replica::Mutation::spawn)) herd_original(self,dt); }
void __fastcall scene_hook(void* self,void*,unsigned long long clock) { if(policy->allow(sporemp::replica::Mutation::timer)) scene_original(self,clock); }
void __fastcall ability_hook(Fixture* self,void*,unsigned index) { if(policy->allow(sporemp::replica::Mutation::ability)) ability_original(self,index); }
bool __fastcall message_hook(void* self,void*,unsigned message,void* payload) {
    for(auto id:sporemp::replica_save_messages)
        if(id==message && !policy->allow(sporemp::replica::Mutation::save)) return false;
    return message_original(self,message,payload);
}
void __fastcall social_hook(void* self,void*,unsigned* first,unsigned* second) { social_original(self,first,second); }
Fixture* __cdecl create_hook(const Position& at,Fixture* species,int age,Fixture* herd,bool avatar,bool cast) {
    return policy->allow(sporemp::replica::Mutation::spawn) ? create_original(at,species,age,herd,avatar,cast) : nullptr;
}
template<class Function,class Member> Function address(Member member) {
    Function function;
    static_assert(sizeof(member)==sizeof(function));
    memcpy(&function,&member,sizeof(function));return function;
}
bool project(void*,uint64_t,const sporemp::replica::Vitals& value) {
    Abi::DnaSet volatile call=set_dna;
    call(value.dna);return dna==value.dna;
}
}
int native_replica_abi_checks() {
    using namespace sporemp;
    using namespace sporemp::replica;
    int failures=0,checks=0;
    auto check=[&](bool value,const char* label){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL replica ABI: %s\n",label);}};
    hunger_original=address<Abi::Hunger>(&Fixture::hunger);
    save_original=address<Save>(&Fixture::save);
    load_original=address<Load>(&Fixture::load);
    Abi::Hunger volatile hunger=hunger_original;
    Save volatile save=save_original;
    Load volatile load=load_original;
    Abi::DnaSet volatile set=set_dna;
    life_original=address<Abi::Lifecycle>(&Fixture::lifecycle);
    brain_original=address<Abi::Brain>(&Fixture::brain);
    remove_original=address<Abi::Remove>(&Fixture::remove);
    unlock_original=address<Abi::Unlock>(&Fixture::unlock);
    lock_original=address<Abi::Lock>(&Fixture::lock);
    herd_original=address<Abi::HerdUpdate>(&Fixture::herd_update);
    scene_original=address<Abi::SceneEvents>(&Fixture::scene_events);
    ability_original=address<Abi::AbilityUse>(&Fixture::ability_use);
    message_original=address<Abi::PersistenceMessage>(&Fixture::message);
    social_original=address<Abi::SocialResult>(&Fixture::social);
    Abi::Lifecycle volatile life=life_original;
    Abi::Brain volatile brain=brain_original;
    Abi::Remove volatile remove=remove_original;
    Abi::Unlock volatile unlock=unlock_original;
    Abi::Lock volatile lock=lock_original;
    Create volatile make=create;
    Abi::HerdUpdate volatile herd_tick=herd_original;
    Abi::SceneEvents volatile scene_tick=scene_original;
    Abi::AbilityUse volatile ability_use=ability_original;
    Abi::PersistenceMessage volatile message=message_original;
    Abi::SocialResult volatile social=social_original;
    const NativeHook hooks[]={
        {reinterpret_cast<void**>(&hunger_original),reinterpret_cast<void*>(hunger_hook)},
        {reinterpret_cast<void**>(&save_original),reinterpret_cast<void*>(save_hook)},
        {reinterpret_cast<void**>(&load_original),reinterpret_cast<void*>(load_hook)},
        {reinterpret_cast<void**>(&dna_original),reinterpret_cast<void*>(dna_hook)},
        {reinterpret_cast<void**>(&life_original),reinterpret_cast<void*>(life_hook)},
        {reinterpret_cast<void**>(&brain_original),reinterpret_cast<void*>(brain_hook)},
        {reinterpret_cast<void**>(&remove_original),reinterpret_cast<void*>(remove_hook)},
        {reinterpret_cast<void**>(&unlock_original),reinterpret_cast<void*>(unlock_hook)},
        {reinterpret_cast<void**>(&lock_original),reinterpret_cast<void*>(lock_hook)},
        {reinterpret_cast<void**>(&create_original),reinterpret_cast<void*>(create_hook)},
        {reinterpret_cast<void**>(&herd_original),reinterpret_cast<void*>(herd_hook)},
        {reinterpret_cast<void**>(&scene_original),reinterpret_cast<void*>(scene_hook)},
        {reinterpret_cast<void**>(&ability_original),reinterpret_cast<void*>(ability_hook)},
        {reinterpret_cast<void**>(&message_original),reinterpret_cast<void*>(message_hook)},
        {reinterpret_cast<void**>(&social_original),reinterpret_cast<void*>(social_hook)}
    };
    Policy authority(Role::authority),replica(Role::replica);policy=&authority;
    const auto attach=change_hooks(hooks,std::size(hooks),true);
    check(attach==NO_ERROR,"fifteen actual Detours attach");
    if(attach!=NO_ERROR)return failures;
    Fixture fixture;
    unsigned social_first=0,social_second=0;
    social(&fixture,&social_first,&social_second);
    check(fixture.social_calls==1 && social_first==0xdc9fb12c && social_second==0x281fbe06,
        "social thiscall preserves native receiver and writes both full-width output pointers");
    hunger(&fixture,0.125f);set(8.75f);
    check(save(&fixture,L"fixture.spo",false) && !save(&fixture,L"fixture.spo",true),"original true/false save result and two stack arguments");
    check(fixture.hunger_calls==1 && fixture.dt==0.125f && dna_calls==1 && dna==8.75f && fixture.save_calls==2,"authority calls originals exactly once");
    const wchar_t load_name[]=L"native-shaped-load.spo";
    load(&fixture,load_name);
    check(fixture.load_calls==1 && fixture.filename==load_name,"filename load preserves receiver and pointer under authority");
    life(&fixture);brain(&fixture,-7);remove(&fixture,true);
    check(unlock(&fixture,123,456,3) && fixture.instance==123 && fixture.group==456 && fixture.cost==3 &&
        !unlock(&fixture,7,8,-1),"native-shaped part grant preserves all three arguments and true/false AL");
    check(lock(&fixture,5,6) && !lock(&fixture,7,7) && fixture.instance==7 && fixture.group==7,"native-shaped lock preserves both arguments and AL");
    check(fixture.mutations==7 && fixture.level==-7 && fixture.removed,"lifecycle/int/bool thiscall receivers preserved");
    herd_tick(&fixture,0xf1234567);
    check(fixture.mutations==8 && fixture.group==0xf1234567,"herd receiver and full-width millisecond word preserved");
    scene_tick(&fixture,0x8123456789abcdefULL);
    check(fixture.mutations==9 && fixture.scene_clock==0x8123456789abcdefULL,"scene clock preserves both stack words including high bit");
    ability_use(&fixture,0xf1234567);
    check(fixture.mutations==10 && fixture.ability_index==0xf1234567,"ability-use receiver and full unsigned index preserved");
    check(message(&fixture,0xf1234567,&fixture) && fixture.message_id==0xf1234567 &&
        fixture.message_payload==&fixture && !message(&fixture,replica_save_messages[2],nullptr) &&
        fixture.message_calls==2,"persistence message preserves receiver, full ID, payload and true/false AL");
    const Position at{1.5f,-2.5f,3.75f};Fixture herd;
    check(make(at,&fixture,3,&herd,true,false)==&fixture && created_species==&fixture && created_herd==&herd &&
        created_at.z==3.75f && created_age==3 && created_avatar && !created_cast,"cdecl six-argument factory pointer and arguments");
    check(!make(at,&fixture,2,&herd,false,true) && creates==2 && created_cast,"factory original null result retained");
    policy=&replica;
    bool save_messages_denied=true;
    for(auto id:replica_save_messages) save_messages_denied &= !message(&fixture,id,&fixture);
    check(save_messages_denied && fixture.message_calls==2,"autosave, deferred and menu save messages denied before original body");
    bool balanced=true,denied=true;
    for(unsigned i=0;i<4096;++i){
        uintptr_t before=0,after=0;
        __asm mov before,esp
        hunger(&fixture,0.25f);
        __asm mov after,esp
        balanced=balanced && before==after;
        __asm mov before,esp
        life(&fixture);brain(&fixture,99);remove(&fixture,false);herd_tick(&fixture,1000);scene_tick(&fixture,123);ability_use(&fixture,5);
        load(&fixture,L"replica-local-load.spo");
        social(&fixture,&social_first,&social_second);
        const bool message_result=message(&fixture,replica_save_messages[2],&fixture);
        const bool unlock_result=unlock(&fixture,9,10,11), lock_result=lock(&fixture,9,10);
        const auto created=make(at,&fixture,4,&herd,true,true);
        __asm mov after,esp
        balanced=balanced && before==after;
        denied=denied && !unlock_result && !lock_result && !created && !message_result;
        __asm mov before,esp
        const bool returned=save(&fixture,L"replica.spo",(i&1)!=0);
        __asm mov after,esp
        balanced=balanced && before==after;denied=denied && !returned;
        __asm mov before,esp
        set(999);
        __asm mov after,esp
        balanced=balanced && before==after;
    }
    check(balanced && denied,"4096 suppressed thiscall/cdecl calls preserve ESP and false AL");
    check(fixture.hunger_calls==1 && fixture.save_calls==2 && dna_calls==1,"denied callbacks never enter original bodies");
    check(fixture.load_calls==1 && fixture.filename==load_name,"4096 replica loads preserve prior filename without entering original");
    check(fixture.message_calls==2 && message(&fixture,0x062d91c2,&fixture) && fixture.message_calls==3 &&
        fixture.message_id==0x062d91c2,"non-save message retains native dispatch while replica save remains denied");
    check(fixture.mutations==10 && creates==2 && fixture.level==-7 && fixture.removed && fixture.group==0xf1234567 &&
        fixture.scene_clock==0x8123456789abcdefULL && fixture.ability_index==0xf1234567,"denied lifecycle/parts/factory/herd/scene/ability have no original writes");
    Registry registry(replica);const Fence fence{{1,2},3};
    registry.begin(fence,1);registry.bind({4,5},6);
    check(registry.apply({fence,{4,5},1,1,{7,8,9,10}},project,nullptr)==Decision::accepted && dna==10 && dna_calls==2,"scoped absolute projection reaches original setter once");
    check(!replica.may_publish(),"native setter callback does not promote role");
    check(fixture.social_calls==4097 && social_first==0xdc9fb12c && social_second==0x281fbe06,
        "presentation outputs continue while gameplay is denied");
    check(change_hooks(hooks,std::size(hooks),false)==NO_ERROR,"all fifteen hooks detach");
    set(12);check(dna==12 && dna_calls==3,"detached original behavior restored in HOST fixture");
    std::printf("%d HOST Detours/Win32 replica ABI checks; original SPORE NOT RUN.\n",checks);
    return failures;
}
