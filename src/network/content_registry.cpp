#include "content_registry.h"
#include "../bridge/content_png.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <limits>
#include <set>
#include <tuple>

namespace sporemp::network {
bool ContentKey::operator==(const ContentKey& x) const noexcept { return group==x.group&&instance==x.instance&&type==x.type; }
bool ContentKey::operator<(const ContentKey& x) const noexcept { return std::tie(group,instance,type)<std::tie(x.group,x.instance,x.type); }
namespace {
ContentDecision failure(ContentFailure f,uint64_t transaction=0) { ContentDecision d;d.failure=f;d.transaction=transaction;return d; }
void word(std::vector<uint8_t>& out,uint32_t v) { for(unsigned n=0;n<4;++n)out.push_back(uint8_t(v>>(8*n))); }
void wide(std::vector<uint8_t>& out,uint64_t v) { for(unsigned n=0;n<8;++n)out.push_back(uint8_t(v>>(8*n))); }
void digest(std::vector<uint8_t>& out,const Digest& d) { out.insert(out.end(),d.begin(),d.end()); }
void key(std::vector<uint8_t>& out,const ContentKey& k) { word(out,k.group);word(out,k.instance);word(out,k.type); }
bool native_key(const ContentKey& k,uint32_t type) { return k.group&&k.group!=UINT32_MAX&&k.instance&&k.instance!=UINT32_MAX&&k.type==type; }
bool manifest_digest(ContentManifest& m) {
    // Explicit versioned canonical encoding, never a native struct or pointer.
    std::vector<uint8_t> bytes;word(bytes,0x38434d53);word(bytes,1);wide(bytes,m.owner);wide(bytes,m.revision);
    digest(bytes,m.parent);digest(bytes,m.png);digest(bytes,m.native_properties);digest(bytes,m.installed_profile);
    for(const auto& d:m.world)digest(bytes,d);
    word(bytes,m.png_bytes);word(bytes,m.model_type);word(bytes,m.rigblocks);word(bytes,m.capabilities);
    word(bytes,static_cast<uint32_t>(m.parts.size()));for(const auto& k:m.parts)key(bytes,k);
    return content_digest(bytes.data(),bytes.size(),m.version);
}
}
bool content_digest(const uint8_t* bytes,size_t size,Digest& out) noexcept {
    if((size&&!bytes)||size>ULONG_MAX)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;Digest value{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok&&size)ok=BCryptHashData(hash,const_cast<PUCHAR>(bytes),static_cast<ULONG>(size),0)>=0;
    if(ok)ok=BCryptFinishHash(hash,value.data(),static_cast<ULONG>(value.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(ok)out=value;return ok;
}
const char* content_failure_name(ContentFailure f) noexcept {
    switch(f) {
#define CONTENT_ERROR(name) case ContentFailure::name:return #name
    CONTENT_ERROR(none);CONTENT_ERROR(unknown_connection);CONTENT_ERROR(wrong_owner);CONTENT_ERROR(wrong_authority);
    CONTENT_ERROR(authority_unavailable);CONTENT_ERROR(stale_base);CONTENT_ERROR(transaction_exists);CONTENT_ERROR(unknown_transaction);
    CONTENT_ERROR(expired);CONTENT_ERROR(wrong_phase);CONTENT_ERROR(capacity);CONTENT_ERROR(invalid_size);CONTENT_ERROR(invalid_offset);
    CONTENT_ERROR(hash_mismatch);CONTENT_ERROR(invalid_png);CONTENT_ERROR(invalid_native_observation);CONTENT_ERROR(missing_part);
    CONTENT_ERROR(installed_profile_mismatch);CONTENT_ERROR(world_mismatch);CONTENT_ERROR(properties_mismatch);CONTENT_ERROR(not_ready);
    CONTENT_ERROR(unchanged_content);CONTENT_ERROR(internal_hash);CONTENT_ERROR(stale_request);CONTENT_ERROR(malformed);
#undef CONTENT_ERROR
    }return "unknown";
}
ContentRegistry::ContentRegistry(Digest installed,WorldIdentity world):installed_(installed),world_(world) {}
bool ContentRegistry::connect(uint64_t connection,uint64_t player,bool authority) {
    if(!connection||peers_.count(connection)||peers_.size()>=8||
       (authority?player!=0||authority_!=0:player<1||player>2)||installed_==Digest{}||!valid_world_identity(world_))return false;
    if(!authority) {
        std::vector<uint64_t> replaced;
        for(const auto& p:peers_)if(!p.second.authority&&p.second.player==player)replaced.push_back(p.first);
        for(auto id:replaced)disconnect(id);
    }
    peers_.emplace(connection,Peer{player,authority});if(authority)authority_=connection;return true;
}
void ContentRegistry::disconnect(uint64_t connection) {
    if(!peers_.erase(connection))return;
    mappings_.erase(connection);
    if(connection==authority_) {authority_=0;fence();return;}
    // A replacement participant cannot inherit the prior connection's readiness.
    // Retaining the other owner's draft is safe; commit needs every live peer.
    for(auto i=transactions_.begin();i!=transactions_.end();) {
        if(i->second.connection==connection)i=transactions_.erase(i);
        else {i->second.mappings.erase(connection);++i;}
    }
}
void ContentRegistry::fence() { transactions_.clear();mappings_.clear(); }
void ContentRegistry::expire(uint64_t now) {
    for(auto i=transactions_.begin();i!=transactions_.end();)if(now>=i->second.deadline)i=transactions_.erase(i);else ++i;
}
ContentDecision ContentRegistry::access(uint64_t connection,uint64_t transaction,uint64_t now,bool owner) {
    auto p=peers_.find(connection);if(p==peers_.end())return failure(ContentFailure::unknown_connection,transaction);
    auto t=transactions_.find(transaction);if(t==transactions_.end())return failure(ContentFailure::unknown_transaction,transaction);
    if(owner&&t->second.connection!=connection)return failure(ContentFailure::wrong_owner,transaction);
    if(now>=t->second.deadline){transactions_.erase(t);return failure(ContentFailure::expired,transaction);}
    if(!authority_)return failure(ContentFailure::authority_unavailable,transaction);
    return failure(ContentFailure::none,transaction);
}
ContentDecision ContentRegistry::begin(uint64_t connection,const Digest& base,uint64_t now) {
    expire(now);auto p=peers_.find(connection);
    if(p==peers_.end())return failure(ContentFailure::unknown_connection);
    if(p->second.authority)return failure(ContentFailure::wrong_owner);
    if(!authority_)return failure(ContentFailure::authority_unavailable);
    if(current(p->second.player)!=base)return failure(ContentFailure::stale_base);
    for(const auto& t:transactions_)if(t.second.player==p->second.player)return failure(ContentFailure::transaction_exists);
    if(transactions_.size()>=2||next_transaction_==UINT64_MAX||now>UINT64_MAX-transaction_lifetime_ms)return failure(ContentFailure::capacity);
    Transaction t;t.connection=connection;t.player=p->second.player;t.base=base;t.deadline=now+transaction_lifetime_ms;
    auto id=++next_transaction_;transactions_.emplace(id,std::move(t));return failure(ContentFailure::none,id);
}
ContentDecision ContentRegistry::offer(uint64_t connection,uint64_t id,const Digest& png,uint32_t size,uint64_t now) {
    auto result=access(connection,id,now,true);if(!result)return result;auto& t=transactions_.at(id);
    if(t.phase!=Phase::draft)return failure(ContentFailure::wrong_phase,id);
    if(size<57||size>max_png_bytes)return failure(ContentFailure::invalid_size,id);
    if(png==Digest{})return failure(ContentFailure::hash_mismatch,id);
    if(versions_.size()>=max_versions||(!blobs_.count(png)&&size>max_cached_bytes-cached_bytes_))return failure(ContentFailure::capacity,id);
    t.png=png;t.bytes=size;t.data.reserve(size);t.phase=Phase::upload;return result;
}
ContentDecision ContentRegistry::append(uint64_t connection,uint64_t id,uint32_t offset,const std::vector<uint8_t>& bytes,uint64_t now) {
    auto result=access(connection,id,now,true);if(!result)return result;auto& t=transactions_.at(id);
    if(t.phase!=Phase::upload)return failure(ContentFailure::wrong_phase,id);
    if(bytes.empty()||bytes.size()>max_chunk_bytes)return failure(ContentFailure::invalid_size,id);
    if(offset!=t.data.size()||bytes.size()>t.bytes-t.data.size())return failure(ContentFailure::invalid_offset,id);
    t.data.insert(t.data.end(),bytes.begin(),bytes.end());return result;
}
ContentDecision ContentRegistry::seal(uint64_t connection,uint64_t id,uint64_t now) {
    auto result=access(connection,id,now,true);if(!result)return result;auto& t=transactions_.at(id);
    if(t.phase!=Phase::upload)return failure(ContentFailure::wrong_phase,id);
    if(t.data.size()!=t.bytes)return failure(ContentFailure::invalid_size,id);
    Digest actual{};if(!content_digest(t.data.data(),t.data.size(),actual))return failure(ContentFailure::internal_hash,id);
    if(actual!=t.png){transactions_.erase(id);return failure(ContentFailure::hash_mismatch,id);}
    if(auto error=creation_png_envelope(t.data.data(),t.data.size())) {
        result=failure(ContentFailure::invalid_png,id);result.detail=error;transactions_.erase(id);return result;
    }
    t.phase=Phase::sealed;return result;
}
ContentDecision ContentRegistry::observe(const ContentObservation& o,ContentManifest& m) const {
    if(o.png==Digest{}||!native_key(o.native_key,0x2b978c46)||o.model_type!=0x9ea3031a||o.blocks.empty()||o.blocks.size()>512||
       o.capabilities.size()>4096||o.resolved_parts.empty()||o.resolved_parts.size()>512)return failure(ContentFailure::invalid_native_observation);
    if(o.installed_profile!=installed_)return failure(ContentFailure::installed_profile_mismatch);
    for(size_t n=0;n<world_file_count;++n)if(o.world[n]!=world_[n]) {
        auto r=failure(ContentFailure::world_mismatch);r.world_index=static_cast<uint32_t>(n);return r;
    }
    std::set<ContentKey> required,resolved;
    for(const auto& part:o.resolved_parts)if(!native_key(part,0x00b1b104)||!resolved.insert(part).second)return failure(ContentFailure::invalid_native_observation);
    std::vector<uint8_t> bytes;word(bytes,0x38504d53);word(bytes,1);word(bytes,o.model_type);
    word(bytes,static_cast<uint32_t>(o.blocks.size()));word(bytes,static_cast<uint32_t>(o.capabilities.size()));
    for(size_t n=0;n<o.blocks.size();++n) {
        const auto& b=o.blocks[n];
        if(!native_key(b.part,0x00b1b104)||b.index!=static_cast<int32_t>(n)||b.parent<-1||b.parent>=static_cast<int32_t>(n)||
           b.symmetric<-1||b.symmetric>=static_cast<int32_t>(o.blocks.size())||b.symmetric==b.index||b.capability_start<0||
           b.capability_count<0||static_cast<size_t>(b.capability_start)>o.capabilities.size()||
           static_cast<size_t>(b.capability_count)>o.capabilities.size()-static_cast<size_t>(b.capability_start))return failure(ContentFailure::invalid_native_observation);
        required.insert(b.part);key(bytes,b.part);
        for(auto v:{b.index,b.parent,b.symmetric,b.flags,b.block_type,b.capability_start,b.capability_count})word(bytes,static_cast<uint32_t>(v));
    }
    for(const auto& part:required)if(!resolved.count(part)) {auto r=failure(ContentFailure::missing_part);r.missing=part;return r;}
    if(required!=resolved)return failure(ContentFailure::invalid_native_observation);
    for(const auto& c:o.capabilities) {
        if(c.level<-128||c.level>127||c.tag==std::array<uint8_t,4>{})return failure(ContentFailure::invalid_native_observation);
        bytes.insert(bytes.end(),c.tag.begin(),c.tag.end());word(bytes,static_cast<uint32_t>(c.level));
    }
    if(!content_digest(bytes.data(),bytes.size(),m.native_properties))return failure(ContentFailure::internal_hash);
    m.png=o.png;m.installed_profile=installed_;m.world=world_;m.model_type=o.model_type;
    m.rigblocks=static_cast<uint32_t>(o.blocks.size());m.capabilities=static_cast<uint32_t>(o.capabilities.size());
    m.parts.assign(required.begin(),required.end());return {};
}
ContentDecision ContentRegistry::attest(uint64_t connection,uint64_t id,const ContentObservation& observed,uint64_t now) {
    auto result=access(connection,id,now,false);if(!result)return result;
    if(connection!=authority_)return failure(ContentFailure::wrong_authority,id);
    auto& t=transactions_.at(id);if(t.phase!=Phase::sealed)return failure(ContentFailure::wrong_phase,id);
    if(observed.png!=t.png)return failure(ContentFailure::hash_mismatch,id);
    ContentManifest m;result=observe(observed,m);result.transaction=id;if(!result)return result;
    if(t.base!=Digest{}&&versions_.at(t.base).png==t.png)return failure(ContentFailure::unchanged_content,id);
    // Identical approved bytes under the same complete profile must never gain
    // different gameplay properties through a later attestation or cache entry.
    for(const auto& old:versions_)if(old.second.png==t.png&&old.second.native_properties!=m.native_properties)return failure(ContentFailure::properties_mismatch,id);
    m.parent=t.base;m.owner=t.player;m.revision=t.base==Digest{}?1:versions_.at(t.base).revision+1;m.png_bytes=t.bytes;
    if(!manifest_digest(m))return failure(ContentFailure::internal_hash,id);
    t.manifest=std::move(m);t.mappings[connection]=observed.native_key;t.phase=Phase::validated;return result;
}
ContentDecision ContentRegistry::observation_admission(uint64_t connection,uint64_t id,uint64_t now) {
    auto r=access(connection,id,now,false);if(!r)return r;
    auto phase=transactions_.at(id).phase;
    if(connection==authority_?phase!=Phase::sealed:phase!=Phase::validated)return failure(ContentFailure::wrong_phase,id);
    return r;
}
ContentDecision ContentRegistry::ready(uint64_t connection,uint64_t id,const ContentObservation& observed,uint64_t now) {
    auto result=access(connection,id,now,false);if(!result)return result;auto& t=transactions_.at(id);
    if(connection==authority_)return failure(ContentFailure::wrong_owner,id);
    if(t.phase!=Phase::validated)return failure(ContentFailure::wrong_phase,id);
    if(observed.png!=t.png)return failure(ContentFailure::hash_mismatch,id);
    ContentManifest m;result=observe(observed,m);result.transaction=id;if(!result)return result;
    if(m.native_properties!=t.manifest.native_properties||m.parts!=t.manifest.parts)return failure(ContentFailure::properties_mismatch,id);
    t.mappings[connection]=observed.native_key;return result;
}
ContentDecision ContentRegistry::commit(uint64_t connection,uint64_t id,uint64_t now) {
    auto result=access(connection,id,now,true);if(!result)return result;auto& t=transactions_.at(id);
    if(t.phase!=Phase::validated)return failure(ContentFailure::wrong_phase,id);
    if(current(t.player)!=t.base)return failure(ContentFailure::stale_base,id);
    for(const auto& peer:peers_)if(!t.mappings.count(peer.first))return failure(ContentFailure::not_ready,id);
    if(versions_.size()>=max_versions||(!blobs_.count(t.png)&&t.data.size()>max_cached_bytes-cached_bytes_))return failure(ContentFailure::capacity,id);
    if(blobs_.count(t.png)&&blobs_.at(t.png)!=t.data)return failure(ContentFailure::hash_mismatch,id);
    if(!blobs_.count(t.png)){cached_bytes_+=t.data.size();blobs_.emplace(t.png,std::move(t.data));}
    versions_.emplace(t.manifest.version,t.manifest);current_[t.player]=t.manifest.version;
    for(const auto& m:t.mappings)mappings_[m.first][t.png]=m.second;
    transactions_.erase(id);return result;
}
ContentDecision ContentRegistry::dependency_failure(uint64_t connection,uint64_t id,const ContentKey& missing,uint64_t now) {
    auto result=access(connection,id,now,false);if(!result)return result;auto& t=transactions_.at(id);
    if(connection==authority_)return failure(ContentFailure::wrong_owner,id);
    if(t.phase!=Phase::validated)return failure(ContentFailure::wrong_phase,id);
    if(std::find(t.manifest.parts.begin(),t.manifest.parts.end(),missing)==t.manifest.parts.end())return failure(ContentFailure::malformed,id);
    t.mappings.erase(connection);return result;
}
ContentDecision ContentRegistry::cancel(uint64_t connection,uint64_t id,uint64_t now) {
    auto r=access(connection,id,now,true);if(r)transactions_.erase(id);return r;
}
const std::vector<uint8_t>* ContentRegistry::candidate(uint64_t connection,uint64_t id,uint64_t now) {
    if(!access(connection,id,now,false))return nullptr;const auto& t=transactions_.at(id);
    if(t.phase==Phase::validated||(t.phase==Phase::sealed&&connection==authority_))return &t.data;
    return nullptr;
}
const ContentManifest* ContentRegistry::proposed(uint64_t id) const {
    auto t=transactions_.find(id);return t!=transactions_.end()&&t->second.phase==Phase::validated?&t->second.manifest:nullptr;
}
const ContentManifest* ContentRegistry::published(const Digest& id) const {auto i=versions_.find(id);return i==versions_.end()?nullptr:&i->second;}
const std::vector<uint8_t>* ContentRegistry::blob(const Digest& id) const {auto i=blobs_.find(id);return i==blobs_.end()?nullptr:&i->second;}
Digest ContentRegistry::current(uint64_t player) const {auto i=current_.find(player);return i==current_.end()?Digest{}:i->second;}
bool ContentRegistry::mapping(uint64_t connection,const Digest& png,ContentKey& result) const {
    auto p=mappings_.find(connection);if(p==mappings_.end())return false;auto k=p->second.find(png);if(k==p->second.end())return false;result=k->second;return true;
}
}
