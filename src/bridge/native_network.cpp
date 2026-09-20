#include "native_network.h"
#include "native_actors.h"
#include "native_scene.h"
#include "native_replica.h"
#include "native_persistence.h"
#include "../network/peer.h"
#include <Spore/App/IMessageManager.h>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sporemp {
namespace {
using namespace network;
std::unique_ptr<Peer> peer;
PeerConfig config;
DWORD engine_thread=0;
App::IMessageManager* messages=nullptr;
std::filesystem::path output_directory;
bool listening=false,welcomed=false,ready=false,failed=false,load_queued=false,setup_queued=false,local_ready=false;
bool owns_persistence=false,baseline_complete=false;
bool connect_started=false;
bool startup_menu_ready=false;
// Pinned original GGE intro completion/skip message. All four native post
// sites enter GGE UI state 6 and dispose the intro before queued delivery.
constexpr uint32_t startup_menu_ready_message=0x066f8f25;
uint64_t player=0,scene=0,baseline=0,published_epoch=0,request_sequence=0;
ULONGLONG next_snapshot=0,next_retry=0,started=0,last_summary=0;
size_t baseline_count=0;
NativeSceneFrame staging;
std::map<uint64_t,Entity> published;
struct Motion { Entity previous{},target{}; ULONGLONG received=0; };
std::map<uint64_t,Motion> motion;
std::map<uint64_t,uint64_t> tombstones;
HWND game_window=nullptr;
WNDPROC original_window_proc=nullptr;
std::array<bool,5> held{};
std::array<bool,5> pressed{};
ULONGLONG next_input=0;
bool on_thread() { return engine_thread && engine_thread==GetCurrentThreadId(); }
void event(const char* name,const std::string& fields="") { native_actor_worker_event(name,fields.c_str()); }
std::string quoted(const std::string& value) {
    std::string result="\"";
    for (unsigned char c:value) {
        if(c=='"'||c=='\\') {result+='\\';result+=static_cast<char>(c);}
        else if(c>=32 && c<127) result+=static_cast<char>(c);
    }
    return result+'"';
}
void status(const char* state,const std::string& detail) {
    event("network_status",",\"state\":"+quoted(state)+",\"player_id\":"+std::to_string(player)+",\"baseline_sequence\":"+std::to_string(baseline)+",\"detail\":"+quoted(detail));
    // Bounded, nonsecret player status. Atomic replace keeps launcher reads whole.
    const auto temporary=output_directory/L"network-status.tmp";
    const auto destination=output_directory/L"network-status.json";
    std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
    stream<<"{\"state\":"<<quoted(state)<<",\"player_id\":"<<player<<",\"baseline_sequence\":"<<baseline<<",\"detail\":"<<quoted(detail)<<"}\n";
    stream.close();
    MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING);
}
void quarantine(const std::string& reason) {
    if(failed) return;
    failed=true;ready=false;motion.clear();held={};pressed={};
    native_replica_arm_network();reset_native_scene_baseline();
    status("error",reason);
}
bool send(Packet packet) {
    if(!peer || failed || !peer->send(packet)) {quarantine("Network output queue unavailable.");return false;}
    return true;
}
Entity encode_entity(const NativeSceneEntity& in,uint64_t) { return in; }
NativeSceneEntity decode_entity(const Entity& in) { return in; }
bool intention(Verb verb,int direction=0) {
    if(!ready || failed || config.role!=Role::player || player<1 || player>2) return false;
    auto controlled=std::find_if(motion.begin(),motion.end(),[](const auto& item){return item.second.target.owner==player;});
    if(controlled==motion.end())return false;
    Packet packet;packet.kind=Kind::action;packet.scene=scene;packet.baseline=baseline;packet.player=player;
    packet.entity=controlled->second.target;packet.verb=verb;packet.direction=direction;packet.request=++request_sequence;
    event("network_intention",",\"request\":"+std::to_string(packet.request)+",\"player\":"+std::to_string(player)+",\"entity\":"+std::to_string(packet.entity.id)+",\"verb\":"+std::to_string(static_cast<unsigned>(verb)));
    return send(packet);
}
int movement_key(WPARAM key) {
    if(key=='W')return 0;if(key=='D')return 1;if(key=='S')return 2;if(key=='A')return 3;if(key==VK_SPACE)return 4;return -1;
}
LRESULT CALLBACK input_window(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    const int key=movement_key(wparam);
    if(on_thread() && config.role==Role::player && local_ready && key>=0 &&
        (message==WM_KEYDOWN || message==WM_KEYUP) && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000)) {
        const size_t index=static_cast<size_t>(key);
        if(message==WM_KEYDOWN) {if(!held[index])pressed[index]=true;held[index]=true;}
        else held[index]=false;
        return 0;
    }
    if(message==WM_KILLFOCUS) {held={};pressed={};}
    return CallWindowProcW(original_window_proc,window,message,wparam,lparam);
}
BOOL CALLBACK find_window(HWND window,LPARAM) {
    DWORD process=0;const auto thread=GetWindowThreadProcessId(window,&process);
    if(process==GetCurrentProcessId() && thread==engine_thread && IsWindowVisible(window) && GetWindow(window,GW_OWNER)==nullptr) {
        game_window=window;return FALSE;
    }
    return TRUE;
}
void install_input() {
    if(config.role!=Role::player || original_window_proc)return;
    EnumWindows(find_window,0);
    if(!game_window)return;
    SetLastError(0);
    const auto previous=SetWindowLongPtrW(game_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(input_window));
    if(!previous && GetLastError()) {game_window=nullptr;quarantine("Game input window could not be attached.");return;}
    original_window_proc=reinterpret_cast<WNDPROC>(previous);
    event("network_input_attached",",\"keys\":\"WASD Space\"");
}
void native_bootstrap() {
    auto state=native_actor_worker_status();
    if(owns_persistence)update_native_persistence(state);
    if(!load_queued && startup_menu_ready && state.values[0]==static_cast<uint64_t>(worker::Phase::menu)) {
        worker::Message request;request.op=worker::Op::load;request.epoch=state.epoch;request.sequence=1;
        if(native_persistence_command(request,state)==worker::Result::accepted) {load_queued=true;status("connecting","Loading the shared Creature scene.");}
    }
    if(native_persistence_snapshot().state==NativePersistenceState::failed) {quarantine("The shared Creature fixture could not be loaded.");return;}
    if(native_persistence_snapshot().state!=NativePersistenceState::loaded)return;
    if(!setup_queued && (!state.values[3] || !state.values[4])) {
        worker::Message request;request.op=worker::Op::setup;request.epoch=state.epoch;request.sequence=2;
        if(native_actor_worker_command(request)==worker::Result::accepted)setup_queued=true;
        return;
    }
    if(state.values[3] && state.values[4]) {local_ready=true;install_input();}
}
void publish_scene() {
    const auto now=GetTickCount64();if(!welcomed || !local_ready || now<next_snapshot)return;
    next_snapshot=now+50;
    NativeSceneFrame frame;
    const auto result=capture_native_scene(frame);
    if(result!=NativeSceneResult::accepted) {quarantine(std::string("Native scene capture failed: ")+native_scene_result_name(result));return;}
    const bool begin=published_epoch!=frame.epoch;
    if(begin) {scene=frame.epoch;baseline++;published.clear();published_epoch=frame.epoch;
        Packet packet;packet.kind=Kind::scene_begin;packet.scene=scene;packet.baseline=baseline;packet.count=static_cast<uint32_t>(frame.count);if(!send(packet))return;}
    std::map<uint64_t,Entity> current;
    for(size_t i=0;i<frame.count;++i)current.emplace(frame.entities[i].id,encode_entity(frame.entities[i],now));
    for(const auto& old:published)if(!current.count(old.first) || current.at(old.first).generation!=old.second.generation) {
        Packet packet;packet.kind=Kind::despawn;packet.scene=scene;packet.baseline=baseline;packet.entity=old.second;packet.entity.tick=now;if(!send(packet))return;
        event("network_despawn_published",",\"entity\":"+std::to_string(old.first));
    }
    for(const auto& item:current) {
        Packet packet;packet.kind=begin || !published.count(item.first) || published.at(item.first).generation!=item.second.generation?Kind::entity:Kind::motion;
        packet.scene=scene;packet.baseline=baseline;packet.entity=item.second;
        if(!send(packet))return;
        const auto& e=item.second;
        if(e.owner) {
            char fields[1024]{};
            sprintf_s(fields,",\"entity\":%llu,\"generation\":%llu,\"owner\":%llu,\"tick\":%llu,\"position\":[%.9g,%.9g,%.9g],\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"velocity\":[%.9g,%.9g,%.9g]",e.id,e.generation,e.owner,e.tick,double(e.x),double(e.y),double(e.z),double(e.qx),double(e.qy),double(e.qz),double(e.qw),double(e.vx),double(e.vy),double(e.vz));
            event("network_authority_pose",fields);
        }
    }
    published=std::move(current);
    if(begin) {Packet packet;packet.kind=Kind::scene_end;packet.scene=scene;packet.baseline=baseline;packet.count=static_cast<uint32_t>(frame.count);send(packet);ready=true;status("connected","Dedicated Creature scene is running.");}
}
void apply_baseline() {
    if(!baseline_complete || !local_ready || failed)return;
    NativeSceneMapping mapping;
    if(!native_actor_owner_native_id(1,mapping.avatar_native_id) || !native_actor_owner_native_id(2,mapping.other_native_id))return;
    mapping.controlled_owner=static_cast<uint32_t>(player);
    native_replica_arm_network();
    const auto result=apply_native_scene_baseline(staging,mapping);
    baseline_complete=false;
    if(result!=NativeSceneResult::accepted) {quarantine(std::string("Shared scene could not be applied: ")+native_scene_result_name(result));return;}
    Packet ack;ack.kind=Kind::baseline_ack;ack.scene=scene;ack.baseline=baseline;ack.count=static_cast<uint32_t>(staging.count);
    if(send(ack)){ready=true;status("connected","Joined the shared Creature scene. Use WASD to move and Space to jump.");}
}
void authority_action(const Packet& packet) {
    Packet reply;reply.kind=Kind::action_result;reply.scene=scene;reply.baseline=baseline;reply.player=packet.player;reply.request=packet.request;reply.entity=packet.entity;
    auto state=native_actor_worker_status();
    auto found=published.find(packet.entity.id);
    if(!ready || packet.scene!=scene || found==published.end() || found->second.generation!=packet.entity.generation || found->second.owner!=packet.player || packet.player<1 || packet.player>2)reply.error=Error::ownership;
    else {
        worker::Message command;command.epoch=state.epoch;command.sequence=packet.request;
        command.values[0]=packet.player;command.values[1]=state.values[packet.player==1?3:4];command.values[2]=static_cast<uint64_t>(packet.direction);
        if(packet.verb==Verb::move)command.op=worker::Op::move;
        else if(packet.verb==Verb::jump)command.op=worker::Op::jump;
        else if(packet.verb==Verb::stop)command.op=worker::Op::stop;
        else {reply.error=Error::not_ready;send(reply);return;}
        const auto result=native_actor_worker_command(command);
        reply.error=result==worker::Result::accepted?Error::none:Error::not_ready;
        event("network_native_intention",",\"request\":"+std::to_string(packet.request)+",\"player\":"+std::to_string(packet.player)+",\"entity\":"+std::to_string(packet.entity.id)+",\"queued\":"+(result==worker::Result::accepted?"true":"false"));
    }
    send(reply);
}
void receive_packet(const Packet& packet) {
    if(packet.kind==Kind::welcome) {welcomed=true;player=packet.player;status("connecting","Authenticated. Waiting for the shared scene.");return;}
    if(packet.kind==Kind::reject) {
        if(config.role==Role::player && packet.error==Error::authority_lost) {
            ready=false;baseline_complete=false;motion.clear();held={};pressed={};
            reset_native_scene_baseline();native_replica_arm_network();
            status("disconnected","The dedicated worker disconnected. Waiting for a fresh shared scene.");
            return;
        }
        quarantine(std::string("Server rejected the session: ")+error_name(packet.error));return;
    }
    if(config.role==Role::authority) {if(packet.kind==Kind::action)authority_action(packet);return;}
    if(packet.kind==Kind::scene_begin) {
        if(packet.baseline<=baseline || !packet.scene || packet.count>NativeSceneFrame::capacity) {quarantine("Invalid or oversized scene baseline.");return;}
        scene=packet.scene;baseline=packet.baseline;baseline_count=packet.count;staging={};staging.epoch=scene;
        motion.clear();tombstones.clear();ready=false;baseline_complete=false;return;
    }
    if(packet.scene!=scene || packet.baseline!=baseline)return;
    if(packet.kind==Kind::entity) {
        if(!valid_entity(packet.entity) || motion.count(packet.entity.id) ||
            (tombstones.count(packet.entity.id) && packet.entity.generation<=tombstones.at(packet.entity.id))) {quarantine("Invalid entity lifecycle.");return;}
        if(!ready) {
            if(staging.count>=baseline_count || staging.count>=NativeSceneFrame::capacity){quarantine("Scene baseline entity overflow.");return;}
            staging.entities[staging.count++]=decode_entity(packet.entity);
        } else if(spawn_native_scene_entity(decode_entity(packet.entity))!=NativeSceneResult::accepted) {quarantine("Native entity spawn failed.");return;}
        motion.emplace(packet.entity.id,Motion{packet.entity,packet.entity,GetTickCount64()});
    } else if(packet.kind==Kind::scene_end) {
        if(staging.count!=baseline_count || packet.count!=baseline_count){quarantine("Incomplete scene baseline.");return;}
        if(!baseline_count) {
            reset_native_scene_baseline();native_replica_arm_network();held={};pressed={};
            Packet ack;ack.kind=Kind::baseline_ack;ack.scene=scene;ack.baseline=baseline;ack.count=0;
            if(send(ack))status("connecting","Waiting for the dedicated worker to load the shared scene.");
            return;
        }
        baseline_complete=true;apply_baseline();
    } else if(packet.kind==Kind::motion) {
        auto found=motion.find(packet.entity.id);
        if(found==motion.end() || found->second.target.generation!=packet.entity.generation || packet.entity.tick<=found->second.target.tick || found->second.target.owner!=packet.entity.owner)return;
        found->second={found->second.target,packet.entity,GetTickCount64()};
    } else if(packet.kind==Kind::despawn) {
        auto found=motion.find(packet.entity.id);if(found==motion.end() || found->second.target.generation!=packet.entity.generation)return;
        if(ready && despawn_native_scene_entity(packet.entity.id,packet.entity.generation)!=NativeSceneResult::accepted) {quarantine("Native entity removal failed.");return;}
        tombstones[packet.entity.id]=packet.entity.generation;motion.erase(found);
    } else if(packet.kind==Kind::action_result) {
        event("network_action_result",",\"request\":"+std::to_string(packet.request)+",\"error\":"+quoted(error_name(packet.error))+",\"accepted_is_queued\":true");
    }
}
void project_motion() {
    if(!ready || failed)return;
    const auto now=GetTickCount64();
    for(const auto& item:motion) {
        const auto& sample=item.second;
        const float alpha=std::min(1.0f,static_cast<float>(now-sample.received)/50.0f);
        auto entity=decode_entity(sample.target);
        entity.x=sample.previous.x+(sample.target.x-sample.previous.x)*alpha;
        entity.y=sample.previous.y+(sample.target.y-sample.previous.y)*alpha;
        entity.z=sample.previous.z+(sample.target.z-sample.previous.z)*alpha;
        float oldq[4]={sample.previous.qx,sample.previous.qy,sample.previous.qz,sample.previous.qw};
        float quaternion[4]={entity.qx,entity.qy,entity.qz,entity.qw};
        float dot=0;for(size_t i=0;i<4;++i)dot+=oldq[i]*quaternion[i];
        float length=0;for(size_t i=0;i<4;++i){quaternion[i]=(dot<0?-oldq[i]:oldq[i])*(1-alpha)+quaternion[i]*alpha;length+=quaternion[i]*quaternion[i];}
        if(length>0)for(auto& q:quaternion)q/=std::sqrt(length);
        entity.qx=quaternion[0];entity.qy=quaternion[1];entity.qz=quaternion[2];entity.qw=quaternion[3];
        if(project_native_scene_entity(entity)!=NativeSceneResult::accepted){quarantine("Native motion projection failed.");return;}
    }
    if(now>=next_input) {
        next_input=now+180;
        for(size_t i=0;i<4;++i)if(held[i]||pressed[i]){intention(Verb::move,static_cast<int>(i));break;}
        if(pressed[4])intention(Verb::jump);
        pressed={};
    }
}
class Listener final:public App::IUnmanagedMessageListener {
    bool HandleMessage(uint32_t id,void*) override {
        if(id==startup_menu_ready_message && on_thread() && peer && !failed) {
            startup_menu_ready=true;event("network_startup_menu_ready");return false;
        }
        if(id!=App::kMsgAppUpdate || !on_thread() || !peer || failed)return false;
        if(!local_ready)native_bootstrap();
        if(local_ready && !connect_started && !failed) {
            std::string error;
            if(!peer->start(config,error)){quarantine(error);return false;}
            connect_started=true;
        }
        Event incoming;size_t handled=0;
        // One legal full scene plus a following full motion frame can be drained
        // per update without starving the bounded transport queue at 20 Hz.
        while(handled++<2*NativeSceneFrame::capacity+2 && peer->poll(incoming)) {
            if(incoming.kind==EventKind::packet)receive_packet(incoming.packet);
            else if(incoming.kind==EventKind::disconnected) {
                ready=welcomed=false;motion.clear();reset_native_scene_baseline();native_replica_arm_network();
                held={};pressed={};
                if(config.role==Role::authority)published_epoch=0;
                status("disconnected",incoming.detail);next_retry=GetTickCount64()+1000;
            }
            if(failed)break;
        }
        if(failed)return false;
        if(!welcomed && next_retry && GetTickCount64()>=next_retry) {
            std::string error;
            if(!peer->try_restart(config,error)) {
                if(error=="network_thread_still_exiting"){next_retry=GetTickCount64()+100;return false;}
                quarantine(error);return false;
            }
            next_retry=0;status("connecting","Reconnecting to the same player identity.");
        }
        if(config.role==Role::authority)publish_scene();
        else {apply_baseline();project_motion();}
        const auto now=GetTickCount64();
        if(now-last_summary>=1000) {last_summary=now;event("network_scene_sample",",\"ready\":"+std::string(ready?"true":"false")+",\"player\":"+std::to_string(player)+",\"scene\":"+std::to_string(scene)+",\"baseline\":"+std::to_string(baseline)+",\"entities\":"+std::to_string(config.role==Role::authority?published.size():motion.size()));}
        if(!local_ready && now-started>120000)quarantine("Shared Creature scene was not ready within two minutes.");
        return false;
    }
} listener;
}
void initialize_native_network(const wchar_t* directory) {
    wchar_t path[32768]{};
    const auto length=GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG",path,_countof(path));
    if(!length)return;
    engine_thread=GetCurrentThreadId();output_directory=directory;started=GetTickCount64();
    std::string error;
    if(length>=_countof(path) || !read_peer_config(path,config,error) || !initialize_native_scene()){quarantine("Network/native scene initialization failed: "+error);return;}
    wchar_t supervisor[32]{};
    owns_persistence=!GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR",supervisor,_countof(supervisor));
    if(owns_persistence) {
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto pe=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
        if(!initialize_native_persistence(base,base+pe->OptionalHeader.SizeOfImage,engine_thread,native_actor_worker_event)){quarantine("Native fixture loader is unavailable.");return;}
    }
    peer=std::make_unique<Peer>();
    messages=App::IMessageManager::Get();
    if(!messages){quarantine("Native app updates are unavailable.");return;}
    messages->AddUnmanagedListener(&listener,startup_menu_ready_message);
    messages->AddUnmanagedListener(&listener,App::kMsgAppUpdate);listening=true;
    status("connecting","Connecting to the shared universe.");
}
void native_network_scene_exit() {
    if(!on_thread() || !peer || !local_ready)return;
    quarantine("The native scene was closed. Rejoin from the launcher.");
}
worker::Result native_network_command(const worker::Message& request) {
    if(!on_thread() || !peer)return worker::Result::unavailable;
    if(request.op==worker::Op::jump)return intention(Verb::jump)?worker::Result::accepted:worker::Result::unavailable;
    if(request.op==worker::Op::move && request.values[2]<=3)return intention(Verb::move,static_cast<int>(request.values[2]))?worker::Result::accepted:worker::Result::unavailable;
    return worker::Result::unavailable;
}
void dispose_native_network() {
    if(!on_thread())return;
    if(listening){messages->RemoveListener(&listener,App::kMsgAppUpdate);messages->RemoveListener(&listener,startup_menu_ready_message);listening=false;}
    if(original_window_proc && IsWindow(game_window))SetWindowLongPtrW(game_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original_window_proc));
    original_window_proc=nullptr;game_window=nullptr;
    if(peer){peer->stop();peer.reset();status("disconnected","The game session has closed.");}
    dispose_native_scene();
    if(owns_persistence)dispose_native_persistence();
}
}
