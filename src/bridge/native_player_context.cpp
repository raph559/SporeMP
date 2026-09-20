#include "native_player_context.h"
#include "native_award_context.h"
#include "native_actor_abi.h"
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/App/IMessageManager.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstddef>

namespace sporemp {
namespace {
using Player = Simulator::cPlayer;
using Noun = Simulator::cGameData;
using Manager = Simulator::cGameNounManager;
using Listener = App::IMessageListener;
using Abi = NativePlayerAbi<Listener, Noun>;
constexpr uint32_t player_noun_id=0x02c21781, player_cast_id=0x02c216ed;
constexpr uint32_t player_messages[]={0x04bef1e3,0x044f1189,0x05e902d3,0x06524498,0x06526395,
    0x06667038,0x06666683,0x06527231,0x06527eaf,0x01a0219e};
static_assert(sizeof(Player)==0x12d8 && offsetof(Player,mCurrentGoalProgress)==0x10f0 &&
    offsetof(Player,mGoalProgressTotal)==0x10f4 && offsetof(Player,mpCRGItems)==0x10e8 &&
    offsetof(Player,mSocialTraitProgress)==0x1278 && offsetof(Player,mCombatTraitProgress)==0x127c,
    "Pinned cPlayer fields; runtime identity is checked separately from SDK TYPE");
Abi::Message message_original=nullptr;
Abi::RemoveOwner remove_original=nullptr;
PlayerContextEvent event_sink=nullptr;
bool(*check_thread)()=nullptr;
uintptr_t image_base=0;
DWORD engine_thread=0;
// One explicit native reference, released on the engine thread. No intrusive
// pointer destructor runs during bridge DLL unloading.
Player* second=nullptr;
std::atomic<uintptr_t> second_listener{0};
uint64_t scene_epoch=0, context_generation=0;
uint32_t native_id=UINT32_MAX, remove_calls=0;
bool retiring=false;

void event(const char* name,const char* format="",...) {
    if(!event_sink || GetCurrentThreadId()!=engine_thread)return;
    char fields[1536]{};
    va_list args;va_start(args,format);const int count=vsprintf_s(fields,format,args);va_end(args);
    if(count>=0)event_sink(name,fields);
}
bool player_layout(Noun* noun) {
    if(!noun || *reinterpret_cast<uintptr_t*>(noun)!=image_base+0x1072cc8)return false;
    const auto listener=reinterpret_cast<uintptr_t>(noun)+0x34;
    return *reinterpret_cast<const uintptr_t*>(listener)==image_base+0x1072ca4 &&
        noun->GetNounID()==player_noun_id && noun->GetCastID()==player_cast_id &&
        noun->Cast(player_cast_id)==noun;
}
bool live_second(uint64_t epoch) {
    if(!second || retiring || scene_epoch!=epoch || !Manager::Get())return false;
    for(auto& noun:Manager::Get()->mNouns)if(&noun==static_cast<Noun*>(second))
        return !noun.mbIsDestroyed && !noun.field_20 && noun.mID==native_id && player_layout(&noun);
    return false;
}
void snapshot(Player* player,uint32_t owner,const char* name) {
    if(!player || !player_layout(player))return;
    auto items=player->mpCRGItems.get();
    event(name,",\"owner\":%u,\"player_native_id\":%u,\"context_generation\":%llu,"
        "\"unique_game_id\":%u,\"goal_progress\":%.9g,\"goal_total\":%.9g,"
        "\"social_progress\":%.9g,\"combat_progress\":%.9g,\"items_initialized\":%s,"
        "\"selection_groups\":%u,\"item_definitions\":%u,\"unlocked_items\":%u,\"destroyed\":%s",
        owner,player->mID,owner==2?context_generation:0,player->mUniqueGameID,
        double(player->mCurrentGoalProgress),double(player->mGoalProgressTotal),
        double(player->mSocialTraitProgress),double(player->mCombatTraitProgress),items?"true":"false",
        static_cast<unsigned>(player->mSelectionGroups.size()),
        items?static_cast<unsigned>(items->mUnlockableItems.size()):0,
        items?static_cast<unsigned>(items->mUnlockedItems.size()):0,player->mbIsDestroyed?"true":"false");
}
bool __fastcall message_hook(Listener* self,void*,uint32_t id,void* payload) {
    if(reinterpret_cast<uintptr_t>(self)!=second_listener.load())return message_original(self,id,payload);
    if(!check_thread || !check_thread())return false;
    // Native 01A0219E handles a resource/configuration notification against
    // this player's own mpCRGItems. Its original checks the payload phase.
    // The remaining registered messages drive the ONE campaign's cheats,
    // UI, achievements or Space actions; B is not their owner. They continue
    // through A's original listener once. No owned reward event is discarded:
    // reward dispatch is not installed by this lifecycle increment.
    if(id!=0x01a0219e || retiring) {
        event("native_player_message_unowned",",\"owner\":2,\"message\":%u,\"retiring\":%s",id,retiring?"true":"false");
        return false;
    }
    const bool result=message_original(self,id,payload);
    event("native_player_message_return",",\"owner\":2,\"message\":%u,\"result\":%s",id,result?"true":"false");
    return result;
}
void __fastcall remove_hook(Noun* self,void*) {
    const bool ours=GetCurrentThreadId()==engine_thread && self==static_cast<Noun*>(second);
    if(ours) {++remove_calls;event("native_player_remove_enter",",\"owner\":2,\"player_native_id\":%u,\"call_count\":%u",native_id,remove_calls);}
    remove_original(self);
    if(ours)event("native_player_remove_return",",\"owner\":2,\"items_cleared\":%s,\"selection_groups\":%u",
        second->mpCRGItems?"false":"true",static_cast<unsigned>(second->mSelectionGroups.size()));
}
}
NativeHook native_player_message_binding() {return {reinterpret_cast<void**>(&message_original),reinterpret_cast<void*>(message_hook)};}
NativeHook native_player_remove_binding() {return {reinterpret_cast<void**>(&remove_original),reinterpret_cast<void*>(remove_hook)};}

bool prepare_native_player_context(uintptr_t base,uintptr_t end,DWORD thread,PlayerContextEvent sink,bool(*thread_check)()) {
    image_base=base;engine_thread=thread;event_sink=sink;check_thread=thread_check;
    struct Code {uint32_t rva;unsigned char bytes[16];};
    const Code code[]={
        {0x720bf0,{0x55,0x8b,0xe9,0x56,0xb9,0x88,0xc9,0x67,0x01,0xe8,0x42,0xda,0x10,0x00,0xe8,0x1d}},
        {0x87c870,{0x8b,0x44,0x24,0x04,0x56,0x57,0x50,0x8b,0xf1,0xe8,0x12,0xbc,0xe9,0xff,0x8d,0x8e}},
        {0x87c3c0,{0x56,0x8b,0xf1,0x8b,0x4e,0x2c,0x57,0x33,0xff,0x3b,0xcf,0x74,0x0a,0x89,0x7e,0x2c}},
        {0x877ed0,{0x8b,0x44,0x24,0x04,0x83,0xec,0x0c,0x3d,0xe3,0xf1,0xbe,0x04,0x75,0x10,0x83,0xc1}},
        {0x8755d0,{0x8b,0x44,0x24,0x04,0x3d,0xed,0x16,0xc2,0x02,0x75,0x05,0x8b,0xc1,0xc2,0x04,0x00}}
    };
    for(const auto& item:code) {
        unsigned char expected[16];memcpy(expected,item.bytes,sizeof(expected));
        // B20BF4's MOV ECX,imm32 has an IMAGE_REL_BASED_HIGHLOW entry at
        // RVA720BF5. Compare its exact relocated address, not preferred VA.
        if(item.rva==0x720bf0) {
            const uint32_t relocated=static_cast<uint32_t>(base+0x127c988);
            memcpy(expected+5,&relocated,sizeof(relocated));
        }
        if(item.rva>end-base || sizeof(expected)>end-base-item.rva ||
            memcmp(reinterpret_cast<const void*>(base+item.rva),expected,sizeof(expected))) {
            event("player_binding_rejected",",\"rva\":%u",item.rva);return false;
        }
    }
    const struct {uint32_t slot,rva;} slots[]={
        {0x1072cc8+0x28,0x87c870},{0x1072cc8+0x44,0x87c3c0},
        {0x1072cc8+0x38,0x879f90},{0x1072cc8+0x0c,0x8755d0},
        {0x1072cc8+0x20,0x8755f0},{0x1072ca4+4,0x877ed0}
    };
    for(const auto& item:slots)if(item.slot>end-base || sizeof(uintptr_t)>end-base-item.slot ||
        *reinterpret_cast<const uintptr_t*>(base+item.slot)!=base+item.rva) {
        event("player_vtable_rejected",",\"slot_rva\":%u",item.slot);return false;
    }
    message_original=reinterpret_cast<Abi::Message>(base+0x877ed0);
    remove_original=reinterpret_cast<Abi::RemoveOwner>(base+0x87c3c0);
    event("player_bindings_checked",",\"code_prefixes\":5,\"virtual_slots\":6,\"native_cast_id\":%u",player_cast_id);
    return true;
}
bool create_native_player_context(uint64_t epoch) {
    if(GetCurrentThreadId()!=engine_thread || second || !Manager::Get())return false;
    auto manager=Manager::Get();auto first=manager->GetPlayer();
    if(!player_layout(first) || first->mbIsDestroyed)return false;
    auto avatar=manager->GetAvatar();
    const float dna=Simulator::cCreatureGameData::GetEvolutionPoints();
    snapshot(first,1,"native_player_before_create");
    // Follow EnsurePlayer's verified factory + retained reference + owner
    // initialization, while preserving manager->mpPlayer and its avatar.
    auto noun=manager->CreateInstance(player_noun_id);
    if(!player_layout(noun)) {
        event("native_player_factory_rejected");if(noun)manager->DestroyInstance(noun);return false;
    }
    second=static_cast<Player*>(noun);static_cast<Noun*>(second)->AddRef();
    native_id=second->mID;scene_epoch=epoch;++context_generation;remove_calls=0;
    second_listener=reinterpret_cast<uintptr_t>(static_cast<Listener*>(second));
    second->SetGameDataOwner(nullptr);
    const bool independent=manager->GetPlayer()==first && manager->GetAvatar()==avatar && second!=first &&
        second->mpCRGItems && second->mpCRGItems!=first->mpCRGItems && second->mSelectionGroups.size()==10 &&
        Simulator::cCreatureGameData::GetEvolutionPoints()==dna;
    event("native_player_created",",\"owner\":2,\"player_native_id\":%u,\"context_generation\":%llu,\"independent_initialization\":%s",
        native_id,context_generation,independent?"true":"false");
    snapshot(first,1,"native_player_after_create");snapshot(second,2,"native_player_after_create");
    if(!independent) {retire_native_player_context("initialization_rejected");return false;}
    return true;
}
void sample_native_player_context(uint64_t epoch) {
    if(GetCurrentThreadId()!=engine_thread || !second || !Manager::Get())return;
    snapshot(Manager::Get()->GetPlayer(),1,"native_player_state");
    if(live_second(epoch))snapshot(second,2,"native_player_state");
    else event("native_player_context_unavailable",",\"owner\":2,\"player_native_id\":%u",native_id);
}
void retire_native_player_context(const char* reason) {
    if(GetCurrentThreadId()!=engine_thread || !second || retiring)return;
    retire_native_awards();
    retiring=true;
    event("native_player_retire",",\"owner\":2,\"player_native_id\":%u,\"reason\":\"%s\"",native_id,reason);
    // Our strong reference covers synchronous RemoveOwner and verification,
    // even when the manager has already destroyed this noun during scene exit.
    if(!second->mbIsDestroyed && Manager::Get())Manager::Get()->DestroyInstance(second);
    unsigned remaining_listeners=0;
    if(auto manager=App::IMessageManager::Get())for(auto id:player_messages)
        if(manager->RemoveListener(static_cast<Listener*>(second),id))++remaining_listeners;
    event("native_player_retired",",\"owner\":2,\"player_native_id\":%u,\"destroyed\":%s,"
        "\"native_remove_calls\":%u,\"extra_listener_cleanup\":%u",
        native_id,second->mbIsDestroyed?"true":"false",remove_calls,remaining_listeners);
    second_listener=0;auto previous=second;second=nullptr;native_id=UINT32_MAX;scene_epoch=0;
    static_cast<Noun*>(previous)->Release();retiring=false;
}
Simulator::cPlayer* native_second_player(uint64_t epoch) {
    return GetCurrentThreadId()==engine_thread && live_second(epoch)?second:nullptr;
}
}
