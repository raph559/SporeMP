#include "protocol.h"
#include <cmath>
#include <cstring>

namespace sporemp::network {
bool Identity::operator==(const Identity& x) const noexcept { return build==x.build && executable==x.executable && content==x.content && fixture==x.fixture && world==x.world; }
namespace {
void put32(Wire& w,size_t& p,uint32_t v) { for(unsigned i=0;i<4;++i) w[p++]=uint8_t(v>>(8*i)); }
void put64(Wire& w,size_t& p,uint64_t v) { for(unsigned i=0;i<8;++i) w[p++]=uint8_t(v>>(8*i)); }
uint32_t get32(const uint8_t* w,size_t& p) { uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(w[p++])<<(8*i);return v; }
uint64_t get64(const uint8_t* w,size_t& p) { uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(w[p++])<<(8*i);return v; }
void put_float(Wire& w,size_t& p,float v) { uint32_t b=0;std::memcpy(&b,&v,4);put32(w,p,b); }
float get_float(const uint8_t* w,size_t& p) { uint32_t b=get32(w,p);float v=0;std::memcpy(&v,&b,4);return v; }
}
Wire encode(const Packet& m) {
    Wire w{};size_t p=0;
    for(auto v:{uint32_t(0x36504d53),uint32_t(packet_bytes),protocol_version,schema_version,uint32_t(m.kind),uint32_t(m.role),uint32_t(m.error),m.count}) put32(w,p,v);
    for(auto v:{m.sequence,m.session,m.scene,m.baseline,m.player}) put64(w,p,v);
    if(m.kind==Kind::content_request||m.kind==Kind::content_response) {
        for(auto byte:m.content)w[p++]=byte;
        return w;
    }
    for(const auto* d:{&m.identity.build,&m.identity.executable,&m.identity.content,&m.identity.fixture,&m.credential}) for(auto b:*d)w[p++]=b;
    const auto& e=m.entity;
    for(auto v:{e.id,e.generation,e.owner,e.tick}) put64(w,p,v);
    for(auto v:{e.native_id,e.herd_native_id,e.species_instance,e.species_type,e.species_group,e.archetype})put32(w,p,v);
    for(auto v:{e.x,e.y,e.z,e.qx,e.qy,e.qz,e.qw,e.health,e.energy,e.hunger,e.dna})put_float(w,p,v);
    put64(w,p,m.target);put32(w,p,uint32_t(m.verb));put32(w,p,uint32_t(m.direction));
    put64(w,p,m.request);for(auto v:{e.vx,e.vy,e.vz})put_float(w,p,v);
    put64(w,p,m.target_generation);put32(w,p,e.life_state);
    put32(w,p,(e.age<=1?e.age:0xffu)|((e.alpha<=1?e.alpha:0xffu)<<8)|((e.combatant_state==0||e.combatant_state==2?e.combatant_state:0xffu)<<16));
    put_float(w,p,e.scale);
    put32(w,p,e.fed_on);put_float(w,p,e.food);put32(w,p,e.pickup_owner);
    // Schema 5 appends fields; all schema 4 scalar offsets remain unchanged.
    for(const auto& digest:m.identity.world)for(auto byte:digest)w[p++]=byte;
    put32(w,p,m.world_index);
    return w;
}
bool valid_entity(const Entity& e) noexcept {
    if(!e.id||!e.generation||e.owner>2||e.life_state>1||(e.life_state&&e.health!=0)||e.age>1||e.alpha>1||(e.combatant_state!=0&&e.combatant_state!=2)||e.fed_on>1||e.pickup_owner>2||(!e.fed_on&&e.pickup_owner))return false;
    for(auto v:{e.x,e.y,e.z,e.qx,e.qy,e.qz,e.qw,e.health,e.energy,e.hunger,e.dna,e.vx,e.vy,e.vz})if(!std::isfinite(v)||std::abs(v)>1.0e9f)return false;
    return e.health>=0&&e.energy>=0&&e.hunger>=0&&e.dna>=0&&std::isfinite(e.scale)&&e.scale>0&&e.scale<=1000&&std::isfinite(e.food)&&std::abs(e.food)<=1.0e9f;
}
bool targeted(Verb verb) noexcept { return verb==Verb::attack||verb==Verb::approach||verb==Verb::engage||verb==Verb::pickup; }
bool valid_action(const Packet& m) noexcept {
    const auto v=uint32_t(m.verb);
    if(v>7||v==4||m.direction<-1||m.direction>3||!m.entity.id||!m.entity.generation)return false;
    return targeted(m.verb)?m.target&&m.target_generation&&!m.direction:!m.target&&!m.target_generation;
}
bool decode(const uint8_t* w,size_t n,Packet& out,std::string& error) {
    if(!w||n!=packet_bytes){error="invalid_packet_length";return false;}
    size_t p=0;
    if(get32(w,p)!=0x36504d53||get32(w,p)!=packet_bytes){error="invalid_envelope";return false;}
    if(get32(w,p)!=protocol_version||get32(w,p)!=schema_version){error="unsupported_protocol_or_schema";return false;}
    Packet m;auto k=get32(w,p),r=get32(w,p),err=get32(w,p);m.count=get32(w,p);
    if(k<1||k>uint32_t(Kind::content_response)||r<1||r>2||err>uint32_t(Error::world_mismatch)||m.count>max_entities){error="invalid_enum_or_count";return false;}
    m.kind=Kind(k);m.role=Role(r);m.error=Error(err);
    m.sequence=get64(w,p);m.session=get64(w,p);m.scene=get64(w,p);m.baseline=get64(w,p);m.player=get64(w,p);
    if(m.kind==Kind::content_request||m.kind==Kind::content_response) {
        if(m.count||m.scene||m.baseline||m.player||m.error!=Error::none){error="invalid_content_header";return false;}
        for(auto& byte:m.content)byte=w[p++];
        while(p<n)if(w[p++]){error="nonzero_reserved";return false;}
        ContentFrame frame;if(!decode_content(m.content,frame)){error="invalid_content_frame";return false;}
        out=m;error.clear();return true;
    }
    for(auto* d:{&m.identity.build,&m.identity.executable,&m.identity.content,&m.identity.fixture,&m.credential})for(auto& b:*d)b=w[p++];
    auto& e=m.entity;e.id=get64(w,p);e.generation=get64(w,p);e.owner=get64(w,p);e.tick=get64(w,p);
    e.native_id=get32(w,p);e.herd_native_id=get32(w,p);e.species_instance=get32(w,p);e.species_type=get32(w,p);e.species_group=get32(w,p);e.archetype=get32(w,p);
    e.x=get_float(w,p);e.y=get_float(w,p);e.z=get_float(w,p);e.qx=get_float(w,p);e.qy=get_float(w,p);e.qz=get_float(w,p);e.qw=get_float(w,p);e.health=get_float(w,p);e.energy=get_float(w,p);e.hunger=get_float(w,p);e.dna=get_float(w,p);
    m.target=get64(w,p);m.verb=Verb(get32(w,p));m.direction=int32_t(get32(w,p));
    m.request=get64(w,p);e.vx=get_float(w,p);e.vy=get_float(w,p);e.vz=get_float(w,p);
    m.target_generation=get64(w,p);e.life_state=get32(w,p);
    const auto context=get32(w,p);e.age=context&0xffu;e.alpha=(context>>8)&0xffu;e.combatant_state=(context>>16)&0xffu;
    if(context>>24){error="nonzero_reserved";return false;}
    e.scale=get_float(w,p);
    e.fed_on=get32(w,p);e.food=get_float(w,p);e.pickup_owner=get32(w,p);
    for(auto& digest:m.identity.world)for(auto& byte:digest)byte=w[p++];
    m.world_index=get32(w,p);
    if(m.error==Error::world_mismatch ? m.kind!=Kind::reject||m.world_index>=world_file_count : m.world_index!=0){error="invalid_world_mismatch_index";return false;}
    while(p<n)if(w[p++]!=0){error="nonzero_reserved";return false;}
    if(m.kind==Kind::entity||m.kind==Kind::motion){if(!valid_entity(e)){error="invalid_entity";return false;}}
    if(m.kind==Kind::action&&!valid_action(m)){error="invalid_action";return false;}
    out=m;error.clear();return true;
}
std::string hex(const Digest& d){static const char* a="0123456789abcdef";std::string s;for(auto b:d){s+=a[b>>4];s+=a[b&15];}return s;}
bool parse_hex(const std::string& s,Digest& d) noexcept {if(s.size()!=64)return false;Digest x{};auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};for(size_t i=0;i<32;++i){int a=digit(s[i*2]),b=digit(s[i*2+1]);if(a<0||b<0)return false;x[i]=uint8_t(a*16+b);}d=x;return true;}
const char* error_name(Error e) noexcept {switch(e){case Error::none:return "none";case Error::malformed:return "malformed";case Error::incompatible:return "incompatible_build_executable_or_content";case Error::authentication:return "authentication_failed";case Error::capacity:return "capacity_exceeded";case Error::stale:return "stale_epoch_baseline_or_sequence";case Error::ownership:return "wrong_actor_owner";case Error::not_ready:return "baseline_not_ready";case Error::authority_lost:return "authority_disconnected";case Error::timeout:return "timeout";case Error::queue_full:return "queue_full";case Error::world_mismatch:return "canonical_world_mismatch";}return "unknown";}
std::string error_detail(const Packet& packet) {
    std::string detail=error_name(packet.error);
    if(packet.error==Error::world_mismatch&&packet.world_index<world_file_count)detail+=std::string(":")+world_files[packet.world_index].path;
    return detail;
}
}
