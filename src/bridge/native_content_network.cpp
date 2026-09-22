#include "native_content_network.h"
#include "native_content.h"
#include "native_actors.h"
#include "native_persistence.h"
#include "../network/content_wire.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace sporemp {
namespace {
using namespace network;
enum class Phase {dependencies,check_parts,report_dependency,fetch,import,observe_begin,observe_chunk,observe_end,finished};
struct Job {
    uint64_t transaction=0,deadline=0;
    Phase phase=Phase::fetch;
    uint32_t total=0;
    Digest expected{};
    Digest native_properties{},dependency_hash{};
    uint32_t dependency_total=0;
    std::vector<uint8_t> dependency_bytes;
    ContentDependencies dependencies;
    ContentKey missing{};
    std::vector<uint8_t> bytes,observation;
    size_t sent=0;
};
struct Pending {uint64_t transaction=0;ContentOp op=ContentOp::fetch;uint32_t offset=0;};
DWORD thread=0;NativeContentSend output=nullptr;PeerConfig config;std::filesystem::path directory;
std::set<Digest> approved;std::map<uint64_t,Job> jobs;std::map<uint64_t,Pending> pending;
std::map<Digest,ContentKey> imported;
uint64_t sequence=0;
bool on_thread(){return thread&&GetCurrentThreadId()==thread;}
void event(const char* name,uint64_t transaction,const char* reason="",const Digest* published=nullptr) {
    char fields[384]{};sprintf_s(fields,",\"transaction\":%llu,\"reason\":\"%s\",\"authority\":%s,\"publication\":%s,\"version_sha256\":\"%s\"",
        transaction,reason,config.role==Role::authority?"true":"false",published?"true":"false",published?hex(*published).c_str():"");native_actor_worker_event(name,fields);
}
void remove(uint64_t transaction) {
    jobs.erase(transaction);for(auto p=pending.begin();p!=pending.end();)if(p->second.transaction==transaction)p=pending.erase(p);else ++p;
}
void fail(uint64_t transaction,const char* reason){event("network_content_failed",transaction,reason);remove(transaction);}
bool request(ContentFrame frame) {
    if(!output||sequence==UINT64_MAX||pending.size()>=2)return false;
    frame.request=++sequence;Packet packet;packet.kind=Kind::content_request;
    if(!encode_content(frame,packet.content)||!output(packet))return false;
    pending.emplace(frame.request,Pending{frame.transaction,frame.op,frame.offset});return true;
}
bool send_next(Job& job) {
    ContentFrame frame;frame.transaction=job.transaction;
    if(job.phase==Phase::dependencies){frame.op=ContentOp::dependencies;frame.offset=static_cast<uint32_t>(job.dependency_bytes.size());}
    else if(job.phase==Phase::report_dependency){frame.op=ContentOp::dependency_failure;frame.failure=ContentFailure::missing_part;frame.missing=job.missing;}
    else if(job.phase==Phase::fetch){frame.op=ContentOp::fetch;frame.offset=static_cast<uint32_t>(job.bytes.size());}
    else if(job.phase==Phase::observe_begin) {
        frame.op=ContentOp::observation_begin;frame.total=static_cast<uint32_t>(job.observation.size());
        if(!content_digest(job.observation.data(),job.observation.size(),frame.identity))return false;
    }else if(job.phase==Phase::observe_chunk) {
        frame.op=ContentOp::observation_chunk;frame.offset=static_cast<uint32_t>(job.sent);
        const auto end=std::min(job.sent+content_chunk_bytes,job.observation.size());
        frame.data.assign(job.observation.begin()+job.sent,job.observation.begin()+end);job.sent=end;
    }else if(job.phase==Phase::observe_end)frame.op=ContentOp::observation_end;
    else return false;
    return request(std::move(frame));
}
bool local_approvals() {
    const auto path=directory/L"m08-content-allowlist.txt";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};char bytes[4097]{};DWORD count=0;
    bool valid=GetFileInformationByHandle(file,&info)&&!info.nFileSizeHigh&&info.nFileSizeLow>0&&info.nFileSizeLow<=4096&&info.nNumberOfLinks==1&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    if(valid)valid=ReadFile(file,bytes,info.nFileSizeLow,&count,nullptr)&&count==info.nFileSizeLow;
    CloseHandle(file);if(!valid)return false;
    size_t offset=0;std::set<Digest> result;
    while(offset<count) {
        size_t end=offset;while(end<count&&bytes[end]!='\n')++end;
        std::string line(bytes+offset,bytes+end);if(!line.empty()&&line.back()=='\r')line.pop_back();Digest hash{};
        if(!parse_hex(line,hash)||hash==Digest{}||!result.insert(hash).second||result.size()>32)return false;
        offset=end+1;
    }
    if(result.empty())return false;approved=std::move(result);return true;
}
bool stage(const Digest& hash,const std::vector<uint8_t>& bytes) {
    const auto name=hex(hash);const auto path=directory/(L"content-"+std::wstring(name.begin(),name.end())+L".png");
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);return ok; // Preserve incomplete artifacts; never replace an unexpected file.
}
}
void initialize_native_content_network(const network::PeerConfig& peer,const std::filesystem::path& root,NativeContentSend send) {
    thread=GetCurrentThreadId();config=peer;directory=root;output=send;
    wchar_t supervisor[32]{};
    if(!GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR",supervisor,_countof(supervisor))||!local_approvals()) {
        approved.clear();event("network_content_disabled",0,"local_disposable_approval_required");return;
    }
    event("network_content_enabled",0,"locally_approved_png_only");
}
void receive_native_content_network(const network::Packet& packet) {
    if(!on_thread()||approved.empty()||packet.kind!=Kind::content_response)return;
    ContentFrame frame;if(!decode_content(packet.content,frame)){event("network_content_failed",0,"malformed_frame");return;}
    if(frame.request) {
        auto p=pending.find(frame.request);if(p==pending.end())return;const auto expected=p->second;pending.erase(p);
        auto j=jobs.find(expected.transaction);if(j==jobs.end())return;auto& job=j->second;
        if(frame.transaction!=expected.transaction||frame.op!=expected.op){fail(expected.transaction,"response_correlation");return;}
        if(frame.failure!=ContentFailure::none){fail(job.transaction,content_failure_name(frame.failure));return;}
        if(frame.op==ContentOp::dependency_failure){fail(job.transaction,"required_native_part_missing");return;}
        if(frame.op==ContentOp::dependencies) {
            if(job.phase!=Phase::dependencies||frame.total<316||frame.total>max_dependency_bytes||
               (job.dependency_total&&job.dependency_total!=frame.total)||frame.offset!=expected.offset||frame.offset!=job.dependency_bytes.size()||
               frame.offset>frame.total||frame.data.empty()||frame.data.size()!=std::min(size_t(content_chunk_bytes),size_t(frame.total)-frame.offset)||
               frame.identity==Digest{}||(job.dependency_hash!=Digest{}&&job.dependency_hash!=frame.identity)) {fail(job.transaction,"dependency_bounds_or_order");return;}
            job.dependency_total=frame.total;job.dependency_hash=frame.identity;
            job.dependency_bytes.insert(job.dependency_bytes.end(),frame.data.begin(),frame.data.end());
            if(job.dependency_bytes.size()==job.dependency_total) {
                Digest hash{};
                if(!content_digest(job.dependency_bytes.data(),job.dependency_bytes.size(),hash)||hash!=job.dependency_hash||
                   !decode_dependencies(job.dependency_bytes,job.dependencies)||job.dependencies.png!=job.expected||
                   job.dependencies.native_properties!=job.native_properties||job.dependencies.png_bytes!=job.total||
                   job.dependencies.installed_profile!=config.identity.content||job.dependencies.world!=config.identity.world) {
                    fail(job.transaction,"dependency_manifest_identity");return;
                }
                job.phase=Phase::check_parts;return;
            }
        }else if(frame.op==ContentOp::fetch) {
            if(job.phase!=Phase::fetch||frame.total<57||frame.total>ContentRegistry::max_png_bytes||
               (job.total&&job.total!=frame.total)||frame.offset!=expected.offset||frame.offset!=job.bytes.size()||
               frame.offset>frame.total||frame.data.empty()||frame.data.size()!=std::min(size_t(content_chunk_bytes),size_t(frame.total)-frame.offset)) {fail(job.transaction,"fetch_bounds_or_order");return;}
            job.total=frame.total;job.bytes.insert(job.bytes.end(),frame.data.begin(),frame.data.end());
            if(job.bytes.size()==job.total){job.phase=Phase::import;return;}
        }else if(frame.op==ContentOp::observation_begin)job.phase=Phase::observe_chunk;
        else if(frame.op==ContentOp::observation_chunk)job.phase=job.sent<job.observation.size()?Phase::observe_chunk:Phase::observe_end;
        else if(frame.op==ContentOp::observation_end) {
            event(config.role==Role::authority?"network_content_attested":"network_content_ready",job.transaction,"original_import_and_load");
            remove(job.transaction);return;
        }else {fail(job.transaction,"unexpected_response");return;}
        const auto transaction=job.transaction;if(!send_next(job))fail(transaction,"output_unavailable");return;
    }
    if(frame.op==ContentOp::cancelled_event||frame.op==ContentOp::published_event) {
        event(frame.op==ContentOp::published_event?"network_content_published":"network_content_cancelled",frame.transaction,"",
            frame.op==ContentOp::published_event?&frame.identity:nullptr);
        remove(frame.transaction);return;
    }
    const bool expected=config.role==Role::authority?frame.op==ContentOp::validate_event:frame.op==ContentOp::load_event;
    if(!expected||!frame.transaction)return;
    if(!local_approvals()){approved.clear();fence_native_content_network();event("network_content_failed",frame.transaction,"local_approval_unavailable");return;}
    if(jobs.count(frame.transaction)||jobs.size()>=2){event("network_content_failed",frame.transaction,"duplicate_or_capacity");return;}
    if(config.role==Role::player&&(!approved.count(frame.identity)||frame.total<57||frame.total>ContentRegistry::max_png_bytes||frame.data.size()!=32)) {
        event("network_content_failed",frame.transaction,"unapproved_or_oversized_content");return;
    }
    Job job;job.transaction=frame.transaction;job.deadline=GetTickCount64()+120000;job.expected=frame.identity;job.total=frame.total;
    if(config.role==Role::player){job.phase=Phase::dependencies;std::copy(frame.data.begin(),frame.data.end(),job.native_properties.begin());}
    auto inserted=jobs.emplace(job.transaction,std::move(job));event("network_content_fetch",frame.transaction);
    if(!send_next(inserted.first->second))fail(frame.transaction,"output_unavailable");
}
void update_native_content_network() {
    if(!on_thread()||approved.empty())return;
    std::vector<uint64_t> expired;
    for(const auto& j:jobs)if(GetTickCount64()>=j.second.deadline)expired.push_back(j.first);
    for(auto id:expired)fail(id,"bounded_native_content_deadline");
    if(native_persistence_busy()||Simulator::IsLoadingGameMode())return;
    const auto phase=native_actor_worker_status().values[0];
    if(phase!=uint64_t(worker::Phase::menu)&&phase!=uint64_t(worker::Phase::scene))return;
    for(auto& pair:jobs) {
        if(pair.second.phase==Phase::check_parts) {
            auto& job=pair.second;const auto transaction=job.transaction;
            if(!check_native_content_parts(job.dependencies.parts,next_native_content_request(),job.missing)) {
                if(!job.missing.group||!job.missing.instance){fail(transaction,"native_dependency_binding_unavailable");return;}
                char fields[256]{};sprintf_s(fields,",\"transaction\":%llu,\"group\":%u,\"instance\":%u,\"type\":%u,\"before_import\":true,\"readiness\":false",
                    transaction,job.missing.group,job.missing.instance,job.missing.type);
                native_actor_worker_event("network_content_missing_part",fields);job.phase=Phase::report_dependency;
            }else {event("network_content_dependencies_ready",transaction,"original_exact_and_mapped_records");job.phase=Phase::fetch;}
            if(!send_next(job))fail(transaction,"output_unavailable");return;
        }
        auto& job=pair.second;if(job.phase!=Phase::import)continue;const auto transaction=job.transaction;
        if(!local_approvals()){approved.clear();fence_native_content_network();event("network_content_failed",transaction,"local_approval_unavailable");return;}
        Digest hash{};if(!content_digest(job.bytes.data(),job.bytes.size(),hash)||(job.expected!=Digest{}&&job.expected!=hash)||!approved.count(hash)) {
            fail(transaction,"content_not_locally_approved");return;
        }
        ContentObservation observation;bool loaded=false;
        auto old=imported.find(hash);
        if(old!=imported.end()) {
            loaded=inspect_native_content(old->second.instance,old->second.type,old->second.group,next_native_content_request(),&observation);
            observation.png=hash;
        }else {
            if(!stage(hash,job.bytes)){fail(transaction,"quarantine_create_new_failed");return;}
            loaded=import_native_content_blob(hash,next_native_content_request(),observation);
            if(loaded)imported.emplace(hash,observation.native_key);
        }
        if(!loaded){fail(transaction,"original_import_or_inspection_failed");return;}
        observation.installed_profile=config.identity.content;observation.world=config.identity.world;
        if(!encode_observation(observation,job.observation)){fail(transaction,"native_value_bounds");return;}
        char fields[384]{};sprintf_s(fields,",\"transaction\":%llu,\"png_sha256\":\"%s\",\"instance\":%u,\"type\":%u,\"group\":%u,\"rigblocks\":%u,\"capabilities\":%u,\"publication\":false",
            transaction,hex(hash).c_str(),observation.native_key.instance,observation.native_key.type,observation.native_key.group,
            static_cast<unsigned>(observation.blocks.size()),static_cast<unsigned>(observation.capabilities.size()));
        native_actor_worker_event("network_content_native_observed",fields);
        job.bytes.clear();job.phase=Phase::observe_begin;
        if(!send_next(job))fail(transaction,"output_unavailable");return; // One original import at most per app update.
    }
}
void fence_native_content_network(){if(!on_thread())return;jobs.clear();pending.clear();}
void dispose_native_content_network(){if(!on_thread())return;fence_native_content_network();approved.clear();imported.clear();output=nullptr;thread=0;}
}
