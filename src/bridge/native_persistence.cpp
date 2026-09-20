#include "native_persistence.h"
#include "native_persistence_abi.h"
#include "native_actors.h"
#include <Spore/Simulator/SubSystem/GamePersistenceManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/SpacePlayerData.h>
#include <Spore/Simulator/SubSystem/PlanetModel.h>
#include <Spore/App/IMessageManager.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sporemp {
namespace {
using Manager=Simulator::cGamePersistenceManager;
using Abi=NativePersistenceAbi<Manager>;
using State=NativePersistenceState;
constexpr wchar_t fixture_file[]=L"Satiria.spo";
constexpr char16_t fixture_name[]=u"Satiria";
constexpr uint64_t load_timeout_ms=90000;
uintptr_t image_base=0;
DWORD engine_thread=0;
NativePersistenceEvent event_sink=nullptr;
Abi::Save save_original=nullptr;
Abi::Load load_original=nullptr;
Abi::Reset reset_original=nullptr;
Abi::Get reset_manager_get=nullptr;
NativePersistenceSnapshot snapshot;
uint64_t requested_epoch=0,requested_ai=0,load_scene_epoch=0,load_scene_ai=0;
ULONGLONG operation_started=0;
bool available=false;

bool on_thread() {return engine_thread && GetCurrentThreadId()==engine_thread;}
void event(const char* name,const char* format="",...) {
    if(!on_thread() || !event_sink)return;
    char fields[1536]{};
    va_list args;va_start(args,format);const int count=vsprintf_s(fields,format,args);va_end(args);
    if(count>=0)event_sink(name,fields);
}
bool readable(const void* address,size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if(!address || !VirtualQuery(address,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
        (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto begin=reinterpret_cast<uintptr_t>(address);
    const auto region=reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin>=region && size<=info.RegionSize && begin-region<=info.RegionSize-size;
}
Manager* manager() {
    auto value=Manager::Get();
    return readable(value,0x4c) && *reinterpret_cast<const uintptr_t*>(value)==image_base+0x105f9a0 ? value:nullptr;
}
bool fixture_scene(const worker::Message& status) {
    if(status.values[0]!=static_cast<uint64_t>(worker::Phase::scene) ||
        status.values[5]!=kGameCreature || Simulator::GetGameModeID()!=kGameCreature)return false;
    auto nouns=Simulator::cGameNounManager::Get();
    if(!nouns || !nouns->GetPlayer() || !nouns->GetAvatar() ||
        nouns->GetPlayer()->mbIsDestroyed || nouns->GetAvatar()->mbIsDestroyed)return false;
    auto home=Simulator::GetPlayerHomePlanet();
    return home && home->mName==fixture_name;
}
bool fixture_menu(const worker::Message& status) {
    return status.values[0]==static_cast<uint64_t>(worker::Phase::menu) &&
        status.values[5]==kGGEMode && Simulator::GetGameModeID()==kGGEMode;
}
void fail(const char* reason,uint64_t observed_epoch) {
    snapshot.state=State::failed;
    event("native_persistence_failed",",\"request\":%llu,\"reason\":\"%s\",\"requested_epoch\":%llu,\"observed_epoch\":%llu",
        snapshot.request,reason,requested_epoch,observed_epoch);
}
}

bool initialize_native_persistence(uintptr_t base,uintptr_t end,DWORD thread,NativePersistenceEvent sink) {
    image_base=base;engine_thread=thread;event_sink=sink;available=false;snapshot={};
    if(!on_thread() || end<=base)return false;
    struct Prefix {uint32_t rva;unsigned char bytes[16];};
    // All three prefixes contain only position-relative operands. Getter MOV
    // operands below are relocated explicitly before their byte comparison.
    static const Prefix prefixes[]={
        {0x729600,{0x83,0xec,0x10,0x55,0x8b,0xe9,0xe8,0x95,0x3d,0x01,0x00,0x8b,0xc8,0xe8,0x7e,0x18}},
        {0x728020,{0x83,0xec,0x40,0x53,0x55,0x8b,0x6c,0x24,0x4c,0x56,0x57,0x33,0xff,0x8b,0xf1,0x55}},
        {0x77e010,{0x55,0x56,0x57,0x8b,0xf1,0xe8,0x36,0xf4,0xfb,0xff,0x8b,0xe8,0x8b,0x7d,0x24,0xe8}}
    };
    for(const auto& item:prefixes) {
        if(item.rva>end-base || sizeof(item.bytes)>end-base-item.rva ||
            memcmp(reinterpret_cast<const void*>(base+item.rva),item.bytes,sizeof(item.bytes))) {
            event("persistence_binding_rejected",",\"rva\":%u",item.rva);return false;
        }
    }
    const struct {uint32_t rva,global_rva;} getters[]={{0x73d440,0x127eaf4},{0x73d460,0x127eafc}};
    for(const auto& item:getters) {
        unsigned char bytes[]={0xa1,0,0,0,0,0xc3};
        const auto address=static_cast<uint32_t>(base+item.global_rva);
        memcpy(bytes+1,&address,sizeof(address));
        if(item.rva>end-base || sizeof(bytes)>end-base-item.rva ||
            memcmp(reinterpret_cast<const void*>(base+item.rva),bytes,sizeof(bytes))) {
            event("persistence_binding_rejected",",\"rva\":%u",item.rva);return false;
        }
    }
    save_original=reinterpret_cast<Abi::Save>(base+0x729600);
    load_original=reinterpret_cast<Abi::Load>(base+0x728020);
    reset_original=reinterpret_cast<Abi::Reset>(base+0x77e010);
    reset_manager_get=reinterpret_cast<Abi::Get>(base+0x73d460);
    if(!manager()) {event("persistence_manager_rejected");return false;}
    available=true;snapshot.state=State::idle;
    event("persistence_bindings_checked",",\"code_prefixes\":5,\"fixture\":\"Satiria.spo\",\"native_execution_qualified\":false");
    return true;
}

bool native_persistence_busy() {
    if(!on_thread())return true;
    return snapshot.state==State::save_pending || snapshot.state==State::saving ||
        snapshot.state==State::load_pending || snapshot.state==State::loading;
}
worker::Result native_persistence_command(const worker::Message& request,const worker::Message& status) {
    using worker::Result;using worker::Op;
    if(!on_thread() || !available || (request.op!=Op::save && request.op!=Op::load))return Result::unavailable;
    if(native_persistence_busy())return Result::busy;
    if(!request.sequence)return Result::invalid;
    if(request.epoch!=status.epoch)return Result::stale;
    for(auto scalar:request.values)if(scalar)return Result::invalid;
    if(!manager() || (request.op==Op::save ? !fixture_scene(status):!fixture_menu(status)))return Result::unavailable;
    snapshot.request=request.sequence;snapshot.native_save_result_valid=false;snapshot.native_save_result=false;
    snapshot.state=request.op==Op::save?State::save_pending:State::load_pending;
    requested_epoch=status.epoch;requested_ai=status.values[2];load_scene_epoch=load_scene_ai=0;
    operation_started=GetTickCount64();
    event(request.op==Op::save?"native_save_queued":"native_load_queued",
        ",\"request\":%llu,\"requested_epoch\":%llu,\"native_ai_before\":%llu",snapshot.request,requested_epoch,requested_ai);
    return Result::accepted;
}

void update_native_persistence(const worker::Message& status) {
    if(!on_thread() || !available)return;
    if(snapshot.state==State::loaded && (status.epoch!=load_scene_epoch || !fixture_scene(status))) {
        fail("loaded_scene_changed",status.epoch);return;
    }
    if(snapshot.state==State::save_pending || snapshot.state==State::load_pending) {
        const bool save=snapshot.state==State::save_pending;
        if(requested_epoch!=status.epoch || !(save?fixture_scene(status):fixture_menu(status))) {
            fail("scene_changed_before_dispatch",status.epoch);return;
        }
        auto persistence=manager();
        if(!persistence) {fail("persistence_manager_changed",status.epoch);return;}
        if(save) {
            // Snapshot at the actual serialization boundary, after intervening
            // native AI and queued console work, rather than at IPC admission.
            NativeActorCheckpoint checkpoint;
            if (!snapshot_native_actor_checkpoint(checkpoint, snapshot.request)) {
                fail("actor_checkpoint_unavailable", status.epoch); return;
            }
            snapshot.state=State::saving;
            event("native_save_enter",",\"request\":%llu,\"fixture\":\"Satiria.spo\"",snapshot.request);
            // The original Options caller E03417 resumes its save pause, then
            // calls B29600(manager,nullptr,true). Match its native full-save
            // branch with the explicitly verified controlled filename. Original
            // callbacks, serialization and backup marker still execute once.
            const bool result=save_original(persistence,fixture_file,true);
            const auto observed_epoch=native_actor_worker_status().epoch;
            snapshot.native_save_result_valid=true;snapshot.native_save_result=result;
            snapshot.state=result && observed_epoch==requested_epoch?State::saved:State::failed;
            event("native_save_return",",\"request\":%llu,\"native_success\":%s,\"durable\":false,\"requested_epoch\":%llu,\"observed_epoch\":%llu,\"elapsed_ms\":%llu",
                snapshot.request,result?"true":"false",requested_epoch,observed_epoch,GetTickCount64()-operation_started);
            return;
        }
        auto reset_manager=reset_manager_get();
        if(!readable(reset_manager,0x38) || *reinterpret_cast<const uintptr_t*>(reset_manager)!=image_base+0x10652b0 ||
            !Simulator::cPlanetModel::Get() || !App::IMessageManager::Get()) {
            fail("load_prerequisite_changed",status.epoch);return;
        }
        snapshot.state=State::loading;
        event("native_load_requested",",\"request\":%llu,\"fixture\":\"Satiria.spo\",\"requested_epoch\":%llu,\"native_ai_before\":%llu",
            snapshot.request,requested_epoch,requested_ai);
        // Ordinary saved-game UI DEBF4B..DEBF72: immediate pre-load message,
        // native reset manager, then filename loader. B28020 copies the name
        // and requests original LoadGame mode. It returns void, not success.
        // No guessed GameLoadParameters, species pointer, or selected star is
        // created: the original checkpoint loader restores native state.
        App::IMessageManager::Get()->MessageSend(0x0680c633,const_cast<wchar_t*>(fixture_file));
        reset_original(reset_manager);
        load_original(persistence,fixture_file);
        return;
    }
    if(snapshot.state!=State::loading)return;
    if(GetTickCount64()-operation_started>load_timeout_ms) {fail("load_timeout",status.epoch);return;}
    if(!fixture_scene(status) || status.epoch==requested_epoch)return;
    if(load_scene_epoch!=status.epoch) {
        load_scene_epoch=status.epoch;load_scene_ai=status.values[2];return;
    }
    if(status.values[2]<=load_scene_ai || status.values[2]<=requested_ai)return;
    snapshot.state=State::loaded;
    event("native_load_complete",",\"request\":%llu,\"requested_epoch\":%llu,\"observed_epoch\":%llu,\"native_ai_before\":%llu,\"scene_native_ai_before\":%llu,\"native_ai_after\":%llu,\"elapsed_ms\":%llu,\"identities_restored\":false",
        snapshot.request,requested_epoch,status.epoch,requested_ai,load_scene_ai,status.values[2],GetTickCount64()-operation_started);
}
void annotate_native_persistence(worker::Message& status) {
    if(!on_thread())return;
    status.values[8]=snapshot.request;status.values[9]=static_cast<uint64_t>(snapshot.state);
}
NativePersistenceSnapshot native_persistence_snapshot() {return on_thread()?snapshot:NativePersistenceSnapshot{};}
void dispose_native_persistence() {
    if(!on_thread())return;
    if(native_persistence_busy())event("native_persistence_disposed_pending",",\"request\":%llu,\"state\":%llu",snapshot.request,static_cast<uint64_t>(snapshot.state));
    available=false;save_original=nullptr;load_original=nullptr;reset_original=nullptr;reset_manager_get=nullptr;
    snapshot.state=State::unavailable;event_sink=nullptr;
}
}
