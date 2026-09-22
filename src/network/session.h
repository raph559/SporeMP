#pragma once
#include "protocol.h"
#include "content_control.h"
#include <map>
#include <chrono>
#include <vector>

namespace sporemp::network {
struct SessionConfig { Identity identity{}; Digest authority{},player1{},player2{}; uint64_t epoch=1; };
struct Dispatch { uint64_t connection=0; Packet packet{}; bool close=false; };
// Single coordinator thread only. Pure scalar policy; no socket/engine dependency.
class Session {
public:
    explicit Session(SessionConfig);
    bool connect(uint64_t connection);
    std::vector<Dispatch> receive(uint64_t connection,const Packet&);
    std::vector<Dispatch> disconnect(uint64_t connection);
    size_t entity_count() const noexcept { return entities_.size(); }
    uint64_t scene() const noexcept { return scene_; }
private:
    struct Connection { uint64_t player=0,sequence=0,out_sequence=0,baseline=0,last_request=0;bool authenticated=false,authority=false,ready=false;size_t actions=0;std::chrono::steady_clock::time_point action_window=std::chrono::steady_clock::now(); };
    SessionConfig config_;
    ContentControl content_;
    std::map<uint64_t,Connection> connections_;
    std::map<uint64_t,Entity> entities_,staging_;
    std::map<uint64_t,uint64_t> tombstones_;
    uint64_t authority_=0,scene_=0,source_baseline_=0,next_baseline_=0,staging_scene_=0,staging_baseline_=0;
    uint32_t expected_=0;
    bool building_=false,complete_=false;
    void emit(std::vector<Dispatch>&,uint64_t,Packet,bool close=false);
    void reject(std::vector<Dispatch>&,uint64_t,Error,bool close=false);
    void baseline(std::vector<Dispatch>&,uint64_t);
    void publish(std::vector<Dispatch>&,const Packet&);
};
}
