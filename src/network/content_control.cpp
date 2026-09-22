#include "content_control.h"
#include <algorithm>

namespace sporemp::network {
bool ContentControl::connect(uint64_t connection,uint64_t player,bool authority) {
    if(!registry_.connect(connection,player,authority))return false;
    for(auto p=peers_.begin();p!=peers_.end();)if(!authority&&!p->second.authority&&p->second.player==player)p=peers_.erase(p);else ++p;
    Peer p;p.player=player;p.authority=authority;peers_.emplace(connection,std::move(p));return true;
}
void ContentControl::disconnect(uint64_t connection) {
    auto p=peers_.find(connection);const bool authority=p!=peers_.end()&&p->second.authority;
    registry_.disconnect(connection);peers_.erase(connection);
    if(authority)for(auto& remaining:peers_)remaining.second.observation={};
}
void ContentControl::fence(){registry_.fence();for(auto& p:peers_)p.second.observation={};}
std::vector<ContentDispatch> ContentControl::receive(uint64_t connection,const ContentFrame& in,uint64_t now) {
    std::vector<ContentDispatch> out;ContentFrame reply;reply.op=in.op;reply.request=in.request;reply.transaction=in.transaction;
    auto reject=[&](ContentFailure reason){reply.failure=reason;out.push_back({connection,reply});return out;};
    auto p=peers_.find(connection);if(p==peers_.end())return reject(ContentFailure::unknown_connection);
    auto& peer=p->second;
    if(now-peer.rate_window>=1000){peer.rate_window=now;peer.requests=0;}
    if(++peer.requests>2048)return reject(ContentFailure::capacity);
    for(auto& existing:peers_)if(existing.second.observation.transaction&&
       !registry_.observation_admission(existing.first,existing.second.observation.transaction,now))existing.second.observation={};
    if(!in.request||in.request<=peer.last_request)return reject(ContentFailure::stale_request);
    peer.last_request=in.request;
    const bool dependency_failure=in.op==ContentOp::dependency_failure;
    if((dependency_failure?(in.failure!=ContentFailure::missing_part||!in.missing.group||!in.missing.instance||in.missing.type!=0x00b1b104):
       (in.failure!=ContentFailure::none||in.missing.group||in.missing.instance||in.missing.type))||in.world_index||
       !content_request_op(in.op)||in.data.size()>content_chunk_bytes)return reject(ContentFailure::malformed);
    const bool data_op=in.op==ContentOp::chunk||in.op==ContentOp::observation_chunk;
    const bool identity_op=in.op==ContentOp::begin||in.op==ContentOp::offer||in.op==ContentOp::observation_begin;
    const bool total_op=in.op==ContentOp::offer||in.op==ContentOp::observation_begin;
    const bool offset_op=data_op||in.op==ContentOp::fetch||in.op==ContentOp::dependencies;
    if((!data_op&&!in.data.empty())||(!identity_op&&in.identity!=Digest{})||(!total_op&&in.total)||(!offset_op&&in.offset)||
       ((in.op==ContentOp::begin||in.op==ContentOp::current)?in.transaction!=0:in.transaction==0))return reject(ContentFailure::malformed);
    ContentDecision d;
    switch(in.op) {
    case ContentOp::begin:d=registry_.begin(connection,in.identity,now);break;
    case ContentOp::offer:d=registry_.offer(connection,in.transaction,in.identity,in.total,now);break;
    case ContentOp::chunk:d=registry_.append(connection,in.transaction,in.offset,in.data,now);break;
    case ContentOp::seal:d=registry_.seal(connection,in.transaction,now);break;
    case ContentOp::cancel:d=registry_.cancel(connection,in.transaction,now);break;
    case ContentOp::dependency_failure:d=registry_.dependency_failure(connection,in.transaction,in.missing,now);break;
    case ContentOp::dependencies: {
        d=registry_.observation_admission(connection,in.transaction,now);if(!d)break;
        const auto manifest=registry_.proposed(in.transaction);std::vector<uint8_t> bytes;
        if(!manifest)return reject(ContentFailure::not_ready);
        if(!encode_dependencies(*manifest,bytes)||!content_digest(bytes.data(),bytes.size(),reply.identity))return reject(ContentFailure::internal_hash);
        if(in.offset>=bytes.size())return reject(ContentFailure::invalid_offset);
        reply.total=static_cast<uint32_t>(bytes.size());reply.offset=in.offset;
        const auto end=std::min(bytes.size(),size_t(in.offset)+content_chunk_bytes);
        reply.data.assign(bytes.begin()+in.offset,bytes.begin()+end);break;
    }
    case ContentOp::fetch: {
        const auto bytes=registry_.candidate(connection,in.transaction,now);
        if(!bytes)return reject(ContentFailure::not_ready);
        if(in.offset>=bytes->size())return reject(ContentFailure::invalid_offset);
        reply.total=static_cast<uint32_t>(bytes->size());reply.offset=in.offset;
        const auto end=std::min(bytes->size(),size_t(in.offset)+content_chunk_bytes);
        reply.data.assign(bytes->begin()+in.offset,bytes->begin()+end);break;
    }
    case ContentOp::observation_begin:
        d=registry_.observation_admission(connection,in.transaction,now);if(!d)break;
        if(peer.observation.transaction)return reject(ContentFailure::transaction_exists);
        if(in.total<292||in.total>max_observation_bytes||in.identity==Digest{})return reject(ContentFailure::invalid_size);
        peer.observation.transaction=in.transaction;peer.observation.total=in.total;peer.observation.hash=in.identity;
        peer.observation.bytes.reserve(in.total);break;
    case ContentOp::observation_chunk: {
        d=registry_.observation_admission(connection,in.transaction,now);if(!d){peer.observation={};break;}
        auto& u=peer.observation;
        if(u.transaction!=in.transaction)return reject(ContentFailure::unknown_transaction);
        if(in.data.empty()||in.offset!=u.bytes.size()||in.data.size()>u.total-u.bytes.size())return reject(ContentFailure::invalid_offset);
        u.bytes.insert(u.bytes.end(),in.data.begin(),in.data.end());break;
    }
    case ContentOp::observation_end: {
        d=registry_.observation_admission(connection,in.transaction,now);if(!d){peer.observation={};break;}
        auto& u=peer.observation;
        if(u.transaction!=in.transaction)return reject(ContentFailure::unknown_transaction);
        if(u.bytes.size()!=u.total)return reject(ContentFailure::invalid_size);
        Digest hash{};ContentObservation observed;
        if(!content_digest(u.bytes.data(),u.bytes.size(),hash)||hash!=u.hash){u={};return reject(ContentFailure::hash_mismatch);}
        if(!decode_observation(u.bytes,observed)){u={};return reject(ContentFailure::invalid_native_observation);}
        u={};d=peer.authority?registry_.attest(connection,in.transaction,observed,now):registry_.ready(connection,in.transaction,observed,now);
        break;
    }
    case ContentOp::commit: {
        const auto m=registry_.proposed(in.transaction);if(m)reply.identity=m->version;
        d=registry_.commit(connection,in.transaction,now);if(!d)reply.identity={};break;
    }
    case ContentOp::current: {
        if(peer.authority)return reject(ContentFailure::wrong_owner);
        reply.identity=registry_.current(peer.player);
        if(auto m=registry_.published(reply.identity))reply.data.assign(m->png.begin(),m->png.end());
        break;
    }
    default:return reject(ContentFailure::malformed);
    }
    reply.failure=d.failure;reply.missing=d.missing;reply.world_index=d.world_index;
    if(in.op==ContentOp::begin)reply.transaction=d.transaction;
    if(!d.detail.empty())reply.data.assign(d.detail.begin(),d.detail.end());
    out.push_back({connection,reply});
    if(!d)return out;
    if(in.op==ContentOp::dependency_failure) {
        const auto m=registry_.proposed(in.transaction);
        if(m)for(const auto& target:peers_)if(!target.second.authority&&target.second.player==m->owner) {
            ContentFrame event;event.op=ContentOp::dependency_event;event.transaction=in.transaction;
            event.failure=ContentFailure::missing_part;event.missing=in.missing;out.push_back({target.first,event});
        }
    }
    if(in.op==ContentOp::seal) {
        ContentFrame event;event.op=ContentOp::validate_event;event.transaction=in.transaction;
        for(const auto& target:peers_)if(target.second.authority)out.push_back({target.first,event});
    }
    if(in.op==ContentOp::observation_end&&peer.authority) {
        auto m=registry_.proposed(in.transaction);
        if(m)for(const auto& target:peers_)if(!target.second.authority) {
            ContentFrame event;event.op=ContentOp::load_event;event.transaction=in.transaction;event.identity=m->png;event.total=m->png_bytes;
            event.data.assign(m->native_properties.begin(),m->native_properties.end());out.push_back({target.first,event});
        }
    }
    if(in.op==ContentOp::commit||in.op==ContentOp::cancel) {
        for(auto& target:peers_) {
            if(target.second.observation.transaction==in.transaction)target.second.observation={};
            ContentFrame event;event.op=in.op==ContentOp::commit?ContentOp::published_event:ContentOp::cancelled_event;
            event.transaction=in.transaction;event.identity=reply.identity;out.push_back({target.first,event});
        }
    }
    return out;
}
}
