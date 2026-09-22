#include "content_wire.h"
#include <algorithm>

namespace sporemp::network {
namespace {
void put(std::vector<uint8_t>& out,uint32_t n){for(unsigned b=0;b<4;++b)out.push_back(uint8_t(n>>(b*8)));}
void put64(std::vector<uint8_t>& out,uint64_t n){put(out,uint32_t(n));put(out,uint32_t(n>>32));}
void put_digest(std::vector<uint8_t>& out,const Digest& d){out.insert(out.end(),d.begin(),d.end());}
void put_key(std::vector<uint8_t>& out,const ContentKey& k){put(out,k.group);put(out,k.instance);put(out,k.type);}
struct Reader {
    const uint8_t* p;size_t size,at=0;bool valid=true;
    uint32_t word(){if(at>size||size-at<4){valid=false;return 0;}uint32_t n=0;for(unsigned b=0;b<4;++b)n|=uint32_t(p[at++])<<(b*8);return n;}
    uint64_t wide(){uint64_t n=word();return n|(uint64_t(word())<<32);}
    Digest digest(){Digest d{};if(at>size||size-at<32){valid=false;return d;}std::copy(p+at,p+at+32,d.begin());at+=32;return d;}
    ContentKey key(){ContentKey k;k.group=word();k.instance=word();k.type=word();return k;}
};
}
bool encode_content(const ContentFrame& f,ContentWire& out) {
    if(f.data.size()>content_chunk_bytes||uint32_t(f.op)<1||uint32_t(f.op)>uint32_t(ContentOp::dependency_event)||
       uint32_t(f.failure)>uint32_t(ContentFailure::malformed)||f.world_index>=world_file_count)return false;
    std::vector<uint8_t> v;v.reserve(content_wire_bytes);put(v,0x38434d53);put(v,1);put(v,uint32_t(f.op));put(v,uint32_t(f.failure));
    put64(v,f.request);put64(v,f.transaction);put(v,f.offset);put(v,f.total);put_digest(v,f.identity);put_key(v,f.missing);
    put(v,f.world_index);put(v,static_cast<uint32_t>(f.data.size()));v.insert(v.end(),f.data.begin(),f.data.end());
    out={};std::copy(v.begin(),v.end(),out.begin());return true;
}
bool decode_content(const ContentWire& bytes,ContentFrame& out) {
    Reader r{bytes.data(),bytes.size()};if(r.word()!=0x38434d53||r.word()!=1)return false;
    auto op=r.word(),error=r.word();if(op<1||op>uint32_t(ContentOp::dependency_event)||error>uint32_t(ContentFailure::malformed))return false;
    ContentFrame f;f.op=ContentOp(op);f.failure=ContentFailure(error);f.request=r.wide();f.transaction=r.wide();
    f.offset=r.word();f.total=r.word();f.identity=r.digest();f.missing=r.key();f.world_index=r.word();auto count=r.word();
    if(!r.valid||count>content_chunk_bytes||f.world_index>=world_file_count)return false;
    f.data.assign(bytes.begin()+r.at,bytes.begin()+r.at+count);r.at+=count;
    for(;r.at<bytes.size();++r.at)if(bytes[r.at])return false;
    out=std::move(f);return true;
}
bool encode_observation(const ContentObservation& o,std::vector<uint8_t>& out) {
    if(o.blocks.size()>512||o.capabilities.size()>4096||o.resolved_parts.size()>512)return false;
    std::vector<uint8_t> v;put(v,0x384f4d53);put(v,1);put_digest(v,o.png);put_key(v,o.native_key);put(v,o.model_type);
    put_digest(v,o.installed_profile);for(const auto& d:o.world)put_digest(v,d);
    put(v,static_cast<uint32_t>(o.blocks.size()));put(v,static_cast<uint32_t>(o.capabilities.size()));put(v,static_cast<uint32_t>(o.resolved_parts.size()));
    for(const auto& b:o.blocks){put_key(v,b.part);for(auto n:{b.index,b.parent,b.symmetric,b.flags,b.block_type,b.capability_start,b.capability_count})put(v,uint32_t(n));}
    for(const auto& c:o.capabilities){v.insert(v.end(),c.tag.begin(),c.tag.end());put(v,uint32_t(c.level));}
    for(const auto& k:o.resolved_parts)put_key(v,k);
    if(v.size()>max_observation_bytes)return false;out=std::move(v);return true;
}
bool decode_observation(const std::vector<uint8_t>& bytes,ContentObservation& out) {
    if(bytes.size()<292||bytes.size()>max_observation_bytes)return false;Reader r{bytes.data(),bytes.size()};
    if(r.word()!=0x384f4d53||r.word()!=1)return false;
    ContentObservation o;o.png=r.digest();o.native_key=r.key();o.model_type=r.word();o.installed_profile=r.digest();
    for(auto& d:o.world)d=r.digest();auto blocks=r.word(),caps=r.word(),parts=r.word();
    if(!r.valid||blocks>512||caps>4096||parts>512||size_t(292)+blocks*40+caps*8+parts*12!=bytes.size())return false;
    for(uint32_t n=0;n<blocks;++n) {
        ContentBlock b;b.part=r.key();b.index=int32_t(r.word());b.parent=int32_t(r.word());b.symmetric=int32_t(r.word());
        b.flags=int32_t(r.word());b.block_type=int32_t(r.word());b.capability_start=int32_t(r.word());b.capability_count=int32_t(r.word());o.blocks.push_back(b);
    }
    for(uint32_t n=0;n<caps;++n){ContentCapability c;for(auto& ch:c.tag)ch=bytes[r.at++];c.level=int32_t(r.word());o.capabilities.push_back(c);}
    for(uint32_t n=0;n<parts;++n)o.resolved_parts.push_back(r.key());
    if(!r.valid||r.at!=bytes.size())return false;out=std::move(o);return true;
}
bool encode_dependencies(const ContentManifest& m,std::vector<uint8_t>& out) {
    if(m.parts.empty()||m.parts.size()>512)return false;
    std::vector<uint8_t> v;put(v,0x38444d53);put(v,1);put_digest(v,m.png);put_digest(v,m.native_properties);put_digest(v,m.installed_profile);
    for(const auto& d:m.world)put_digest(v,d);put(v,m.png_bytes);put(v,static_cast<uint32_t>(m.parts.size()));
    for(const auto& key:m.parts)put_key(v,key);
    ContentDependencies checked;if(!decode_dependencies(v,checked))return false;out=std::move(v);return true;
}
bool decode_dependencies(const std::vector<uint8_t>& bytes,ContentDependencies& out) {
    if(bytes.size()<316||bytes.size()>max_dependency_bytes)return false;Reader r{bytes.data(),bytes.size()};
    if(r.word()!=0x38444d53||r.word()!=1)return false;
    ContentDependencies d;d.png=r.digest();d.native_properties=r.digest();d.installed_profile=r.digest();
    for(auto& item:d.world)item=r.digest();d.png_bytes=r.word();const auto count=r.word();
    if(!r.valid||!count||count>512||size_t(304)+count*12!=bytes.size()||d.png==Digest{}||d.native_properties==Digest{}||
       d.installed_profile==Digest{}||!valid_world_identity(d.world)||d.png_bytes<57||d.png_bytes>ContentRegistry::max_png_bytes)return false;
    for(uint32_t n=0;n<count;++n) {
        const auto key=r.key();if(!key.group||!key.instance||key.type!=0x00b1b104||(!d.parts.empty()&&!(d.parts.back()<key)))return false;
        d.parts.push_back(key);
    }
    if(!r.valid||r.at!=bytes.size())return false;out=std::move(d);return true;
}
}
