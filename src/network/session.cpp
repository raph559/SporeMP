#include "session.h"
#include <utility>

namespace sporemp::network {
namespace { bool same_secret(const Digest& a,const Digest& b){uint32_t diff=0;for(size_t i=0;i<a.size();++i)diff|=uint32_t(a[i]^b[i]);return diff==0;} }
Session::Session(SessionConfig c):config_(std::move(c)),content_(config_.identity.content,config_.identity.world) {}
bool Session::connect(uint64_t id){if(!id||connections_.size()>=8||connections_.count(id))return false;connections_.emplace(id,Connection{});return true;}
void Session::emit(std::vector<Dispatch>& out,uint64_t id,Packet m,bool close){auto i=connections_.find(id);if(i==connections_.end())return;m.session=config_.epoch;m.sequence=++i->second.out_sequence;out.push_back({id,m,close});}
void Session::reject(std::vector<Dispatch>& out,uint64_t id,Error e,bool close){Packet m;m.kind=Kind::reject;m.error=e;emit(out,id,m,close);}
void Session::baseline(std::vector<Dispatch>& out,uint64_t id){auto& c=connections_.at(id);c.ready=false;if(!complete_)return;c.baseline=++next_baseline_;Packet m;m.kind=Kind::scene_begin;m.scene=scene_;m.baseline=c.baseline;m.count=uint32_t(entities_.size());emit(out,id,m);for(const auto& entry:entities_){m.kind=Kind::entity;m.entity=entry.second;emit(out,id,m);}m.kind=Kind::scene_end;m.entity={};emit(out,id,m);}
void Session::publish(std::vector<Dispatch>& out,const Packet& in){for(const auto& pair:connections_){const auto& c=pair.second;if(c.authenticated&&!c.authority&&c.baseline){Packet m=in;m.baseline=c.baseline;emit(out,pair.first,m);}}}
std::vector<Dispatch> Session::disconnect(uint64_t id){std::vector<Dispatch> out;content_.disconnect(id);connections_.erase(id);if(authority_==id){authority_=0;complete_=false;building_=false;staging_.clear();for(auto& pair:connections_){pair.second.ready=false;pair.second.baseline=0;reject(out,pair.first,Error::authority_lost);}}return out;}
std::vector<Dispatch> Session::receive(uint64_t id,const Packet& m){
    std::vector<Dispatch> out;auto found=connections_.find(id);if(found==connections_.end())return out;auto& c=found->second;
    if(!m.sequence||m.sequence<=c.sequence){reject(out,id,Error::stale,true);return out;}c.sequence=m.sequence;
    if(!c.authenticated){
        if(m.kind!=Kind::hello||m.session){reject(out,id,Error::authentication,true);return out;}
        if(m.identity.build!=config_.identity.build||m.identity.executable!=config_.identity.executable||m.identity.content!=config_.identity.content){reject(out,id,Error::incompatible,true);return out;}
        for(size_t index=0;index<world_file_count;++index) {
            if(m.identity.world[index]==Digest{}||config_.identity.world[index]==Digest{}||m.identity.world[index]!=config_.identity.world[index]) {
                Packet rejection;rejection.kind=Kind::reject;rejection.error=Error::world_mismatch;rejection.world_index=static_cast<uint32_t>(index);emit(out,id,rejection,true);return out;
            }
        }
        if(m.identity.fixture!=config_.identity.fixture){reject(out,id,Error::incompatible,true);return out;}
        uint64_t player=0;
        if(m.role==Role::authority){if(!same_secret(m.credential,config_.authority)){reject(out,id,Error::authentication,true);return out;}if(authority_){reject(out,id,Error::ownership,true);return out;}authority_=id;c.authority=true;}
        else {if(same_secret(m.credential,config_.player1))player=1;else if(same_secret(m.credential,config_.player2))player=2;else{reject(out,id,Error::authentication,true);return out;}for(auto& old:connections_)if(old.first!=id&&old.second.authenticated&&!old.second.authority&&old.second.player==player){reject(out,old.first,Error::stale,true);old.second.authenticated=false;old.second.ready=false;}}
        if(!content_.connect(id,player,c.authority)){reject(out,id,Error::capacity,true);return out;}
        c.authenticated=true;c.player=player;Packet w;w.kind=Kind::welcome;w.player=player;w.role=c.authority?Role::authority:Role::player;w.identity=config_.identity;emit(out,id,w);if(!c.authority)baseline(out,id);return out;
    }
    if(m.session!=config_.epoch){reject(out,id,Error::stale,true);return out;}
    if(m.kind==Kind::ping){Packet p;p.kind=Kind::ping;emit(out,id,p);return out;}
    if(m.kind==Kind::content_request) {
        ContentFrame frame;if(!decode_content(m.content,frame)){reject(out,id,Error::malformed,true);return out;}
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        for(const auto& response:content_.receive(id,frame,static_cast<uint64_t>(now))) {
            Packet packet;packet.kind=Kind::content_response;
            if(!encode_content(response.frame,packet.content)){reject(out,id,Error::malformed,true);return out;}
            emit(out,response.connection,packet);
        }
        return out;
    }
    if(c.authority){
        if(m.kind==Kind::scene_begin){
            if(!m.scene||!m.baseline||m.count>max_entities||m.scene<scene_||(m.scene==scene_&&m.baseline<=source_baseline_)){reject(out,id,Error::stale,true);return out;}
            content_.fence();building_=true;complete_=false;staging_.clear();staging_scene_=m.scene;staging_baseline_=m.baseline;expected_=m.count;
            for(auto& entry:connections_)if(!entry.second.authority){entry.second.ready=false;entry.second.baseline=0;}return out;
        }
        if(building_){
            if(m.scene!=staging_scene_||m.baseline!=staging_baseline_){reject(out,id,Error::stale,true);return out;}
            if(m.kind==Kind::entity){if(!valid_entity(m.entity)||staging_.size()>=expected_||staging_.count(m.entity.id)){reject(out,id,Error::malformed,true);return out;}staging_.emplace(m.entity.id,m.entity);return out;}
            if(m.kind==Kind::scene_end){if(staging_.size()!=expected_||m.count!=expected_){reject(out,id,Error::malformed,true);return out;}size_t owner1=0,owner2=0;for(const auto& e:staging_){owner1+=e.second.owner==1;owner2+=e.second.owner==2;}if(expected_&&(owner1!=1||owner2!=1)){reject(out,id,Error::ownership,true);return out;}entities_=std::move(staging_);tombstones_.clear();scene_=staging_scene_;source_baseline_=staging_baseline_;building_=false;complete_=true;for(const auto& entry:connections_)if(entry.second.authenticated&&!entry.second.authority)baseline(out,entry.first);return out;}
            reject(out,id,Error::not_ready,true);return out;
        }
        if(!complete_){reject(out,id,Error::not_ready);return out;}
        if(m.scene!=scene_||m.baseline!=source_baseline_){reject(out,id,Error::stale);return out;}
        auto e=entities_.find(m.entity.id);
        if(m.kind==Kind::entity){auto tomb=tombstones_.find(m.entity.id);if(!valid_entity(m.entity)||e!=entities_.end()||(tomb!=tombstones_.end()&&m.entity.generation<=tomb->second)||entities_.size()>=max_entities){reject(out,id,Error::stale);return out;}if(m.entity.owner){for(const auto& live:entities_)if(live.second.owner==m.entity.owner){reject(out,id,Error::ownership);return out;}}entities_.emplace(m.entity.id,m.entity);publish(out,m);return out;}
        if(m.kind==Kind::motion||m.kind==Kind::despawn){if(e==entities_.end()||e->second.generation!=m.entity.generation||e->second.owner!=m.entity.owner||m.entity.tick<=e->second.tick){reject(out,id,Error::stale);return out;}if(m.kind==Kind::motion){if(!valid_entity(m.entity)){reject(out,id,Error::malformed);return out;}const auto& before=e->second;const auto& after=m.entity;if(before.native_id!=after.native_id||before.herd_native_id!=after.herd_native_id||before.species_instance!=after.species_instance||before.species_type!=after.species_type||before.species_group!=after.species_group||before.archetype!=after.archetype){reject(out,id,Error::ownership);return out;}e->second=m.entity;}else {if(tombstones_.size()>=max_entities&&!tombstones_.count(m.entity.id)){reject(out,id,Error::capacity,true);return out;}tombstones_[m.entity.id]=m.entity.generation;entities_.erase(e);}publish(out,m);return out;}
        if(m.kind==Kind::action_result){for(const auto& entry:connections_)if(entry.second.authenticated&&!entry.second.authority&&entry.second.player==m.player){Packet result=m;result.baseline=entry.second.baseline;emit(out,entry.first,result);}return out;}
        reject(out,id,Error::ownership);return out;
    }
    if(m.kind==Kind::baseline_ack){
        // ACKs already in flight when an authority replaces a baseline are
        // obsolete, not a connection failure. They cannot restore readiness.
        if(!complete_||!c.baseline||m.scene<scene_||(m.scene==scene_&&m.baseline<c.baseline))return out;
        if(m.scene!=scene_||m.baseline!=c.baseline){reject(out,id,Error::stale);return out;}c.ready=true;return out;
    }
    if(m.kind!=Kind::action){reject(out,id,Error::ownership);return out;}
    auto now=std::chrono::steady_clock::now();if(now-c.action_window>=std::chrono::seconds(1)){c.action_window=now;c.actions=0;}if(++c.actions>128){reject(out,id,Error::capacity,true);return out;}
    auto refuse_action=[&](Error reason,bool quarantine=false){Packet response;response.kind=quarantine?Kind::reject:Kind::action_result;response.error=reason;response.player=c.player;response.scene=m.scene;response.baseline=m.baseline;response.entity=m.entity;response.target=m.target;response.target_generation=m.target_generation;response.request=m.request?m.request:m.sequence;emit(out,id,response);};
    if(!authority_||!complete_||!c.ready){refuse_action(Error::not_ready);return out;}
    if(m.scene!=scene_||m.baseline!=c.baseline){refuse_action(Error::stale);return out;}
    if(!valid_action(m)){refuse_action(Error::malformed);return out;}
    auto actor=entities_.find(m.entity.id);
    if(actor==entities_.end()||actor->second.owner!=c.player||actor->second.generation!=m.entity.generation){refuse_action(Error::ownership,true);return out;}
    if(actor->second.life_state || actor->second.health<=0){refuse_action(Error::not_ready);return out;}
    if(targeted(m.verb)) {
        auto target=entities_.find(m.target);
        if(target==entities_.end()||target->second.generation!=m.target_generation){refuse_action(Error::stale);return out;}
        if(target->second.owner){refuse_action(Error::ownership);return out;}
        if(m.verb==Verb::pickup ? !target->second.life_state||target->second.food<=0 : target->second.life_state!=0||target->second.health<=0){refuse_action(Error::not_ready);return out;}
    }
    // Native IDs/vitals in a client action are untrusted. Forward the current
    // authoritative actor value after authenticating its owner/incarnation.
    Packet action=m;action.player=c.player;action.entity=actor->second;action.baseline=source_baseline_;if(!action.request)action.request=m.sequence;if(action.request<=c.last_request){reject(out,id,Error::stale);return out;}c.last_request=action.request;emit(out,authority_,action);return out;
}
}
