#include "../../src/network/content_registry.h"
#include "../../src/network/protocol.h"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sporemp::network;
namespace {
unsigned checks=0;
void check(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
void ok(const ContentDecision& d,const char* message){if(!d)std::cerr<<message<<": "<<content_failure_name(d.failure)<<'\n';check(bool(d),message);}
void denied(const ContentDecision& d,ContentFailure reason,const char* message){check(d.failure==reason,message);}
Digest mark(uint8_t byte){Digest d{};d.fill(byte);return d;}
WorldIdentity world(){WorldIdentity w{};for(size_t n=0;n<w.size();++n)w[n]=mark(static_cast<uint8_t>(n+2));return w;}
std::vector<uint8_t> png(){return {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x60,0x00,0x02,0x00,0x00,0x05,0x00,0x01,0x7a,0x5e,0xab,0x3f,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};}
Digest hash(const std::vector<uint8_t>& bytes){Digest d{};check(content_digest(bytes.data(),bytes.size(),d),"fixture SHA256");return d;}
void be(std::vector<uint8_t>& out,uint32_t n){for(int shift=24;shift>=0;shift-=8)out.push_back(uint8_t(n>>shift));}
void chunk(std::vector<uint8_t>& out,const std::vector<uint8_t>& body) {
    be(out,static_cast<uint32_t>(body.size()-4));out.insert(out.end(),body.begin(),body.end());uint32_t crc=UINT32_MAX;
    for(auto b:body){crc^=b;for(int n=0;n<8;++n)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    be(out,crc^UINT32_MAX);
}
std::vector<uint8_t> variant() {
    auto p=png();std::vector<uint8_t> out(p.begin(),p.begin()+33);
    std::vector<uint8_t> first{'I','D','A','T'},second=first;
    first.insert(first.end(),p.begin()+41,p.begin()+46);second.insert(second.end(),p.begin()+46,p.begin()+52);
    chunk(out,first);chunk(out,second);out.insert(out.end(),p.end()-12,p.end());return out;
}
struct Fixture {
    ContentRegistry registry{mark(1),world()};
    std::vector<uint8_t> bytes=png();
    ContentObservation observation;
    Fixture() {
        check(registry.connect(11,0,true)&&registry.connect(21,1,false)&&registry.connect(22,2,false),"authenticated fixture participants");
        observation.png=hash(bytes);observation.native_key={0x40626200,123,0x2b978c46};observation.model_type=0x9ea3031a;
        ContentBlock block;block.part={0x40626000,456,0x00b1b104};block.capability_count=1;observation.blocks.push_back(block);
        observation.capabilities.push_back({{'b','i','t','e'},2});observation.resolved_parts.push_back(block.part);
        observation.installed_profile=mark(1);observation.world=world();
    }
    uint64_t begin(uint64_t connection=21) {
        auto b=registry.begin(connection,registry.current(connection==21?1:2),100);ok(b,"begin fixture draft");return b.transaction;
    }
    void upload(uint64_t id,uint64_t connection=21) {
        ok(registry.offer(connection,id,observation.png,static_cast<uint32_t>(bytes.size()),101),"offer fixture PNG");
        ok(registry.append(connection,id,0,bytes,102),"append fixture PNG");ok(registry.seal(connection,id,103),"seal fixture PNG");
    }
    Digest finish(uint64_t id,uint64_t connection=21) {
        ok(registry.attest(11,id,observation,104),"fixture authority attestation");
        auto p=observation;p.native_key.instance=789;ok(registry.ready(21,id,p,105),"player one native fixture ready");
        p.native_key.instance=987;ok(registry.ready(22,id,p,106),"player two native fixture ready");
        auto proposed=registry.proposed(id);check(proposed!=nullptr,"candidate manifest available");auto id_out=proposed->version;
        ok(registry.commit(connection,id,107),"atomic fixture publication");return id_out;
    }
};
void validation_tests() {
    Digest d{};check(content_digest(nullptr,0,d)&&hex(d)=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","independent empty SHA256 vector");
    const uint8_t abc[]{'a','b','c'};check(content_digest(abc,3,d)&&hex(d)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","independent abc SHA256 vector");
    check(!content_digest(nullptr,1,d),"null hash input refuses nonzero length");
    Fixture f;auto& r=f.registry;auto id=f.begin();
    check(!r.connect(11,0,true)&&!r.connect(0,1,false)&&!r.connect(99,3,false)&&!r.connect(98,1,true),"bad and duplicate connection identities rejected");
    denied(r.begin(21,{},100),ContentFailure::transaction_exists,"one active draft per owner");
    denied(r.begin(22,mark(3),100),ContentFailure::stale_base,"unpublished parent rejected");
    denied(r.begin(11,{},100),ContentFailure::wrong_owner,"authority cannot author player draft");
    denied(r.begin(98,{},100),ContentFailure::unknown_connection,"unknown participant cannot author");
    check(!r.candidate(11,id,100)&&!r.candidate(22,id,100)&&r.version_count()==0,"draft remains private and unpublished");
    denied(r.offer(22,id,f.observation.png,68,101),ContentFailure::wrong_owner,"another owner cannot upload into draft");
    denied(r.offer(21,id,f.observation.png,ContentRegistry::max_png_bytes+1,101),ContentFailure::invalid_size,"oversized offer rejected before allocation");
    denied(r.offer(21,id,f.observation.png,56,101),ContentFailure::invalid_size,"undersized offer rejected");
    denied(r.offer(21,id,{},68,101),ContentFailure::hash_mismatch,"empty identity rejected");
    ok(r.offer(21,id,f.observation.png,68,101),"valid bounded offer");
    denied(r.append(21,id,0,{},102),ContentFailure::invalid_size,"empty chunk rejected");
    denied(r.append(21,id,0,std::vector<uint8_t>(ContentRegistry::max_chunk_bytes+1),102),ContentFailure::invalid_size,"oversized chunk rejected");
    denied(r.append(21,id,1,f.bytes,102),ContentFailure::invalid_offset,"gap in upload rejected");
    denied(r.append(21,id,UINT32_MAX,f.bytes,102),ContentFailure::invalid_offset,"overflowing upload offset rejected");
    std::vector<uint8_t> partial(f.bytes.begin(),f.bytes.begin()+10),rest(f.bytes.begin()+10,f.bytes.end());
    ok(r.append(21,id,0,partial,102),"first contiguous chunk");
    denied(r.append(21,id,0,partial,102),ContentFailure::invalid_offset,"duplicate chunk rejected");
    denied(r.seal(21,id,103),ContentFailure::invalid_size,"incomplete upload never reaches authority");
    check(!r.candidate(11,id,103),"partial upload hidden from authority");
    ok(r.append(21,id,10,rest,103),"second contiguous chunk");ok(r.seal(21,id,104),"complete verified upload");
    check(r.candidate(11,id,104)&&*r.candidate(11,id,104)==f.bytes&&!r.candidate(22,id,104),"only authority sees sealed unvalidated content");
    denied(r.attest(21,id,f.observation,104),ContentFailure::wrong_authority,"owner cannot claim authoritative properties");
    denied(r.attest(22,id,f.observation,104),ContentFailure::wrong_authority,"other player cannot claim authoritative properties");
    denied(r.commit(21,id,104),ContentFailure::wrong_phase,"unvalidated content cannot publish");
    auto bad=f.observation;bad.png[0]^=1;denied(r.attest(11,id,bad,104),ContentFailure::hash_mismatch,"authority response tied to exact pending bytes");
    bad=f.observation;bad.installed_profile[0]^=1;denied(r.attest(11,id,bad,104),ContentFailure::installed_profile_mismatch,"entire installed dependency profile pinned");
    for(size_t n=0;n<world_file_count;++n) {
        bad=f.observation;bad.world[n][0]^=1;auto result=r.attest(11,id,bad,104);
        check(result.failure==ContentFailure::world_mismatch&&result.world_index==n,"changed terrain/world file identifies precise allowlist index");
    }
    bad=f.observation;bad.blocks[0].part.instance=457;auto missing=r.attest(11,id,bad,104);
    check(missing.failure==ContentFailure::missing_part&&missing.missing==bad.blocks[0].part,"missing dependency reports full native key");
    bad=f.observation;bad.resolved_parts.push_back(bad.resolved_parts[0]);denied(r.attest(11,id,bad,104),ContentFailure::invalid_native_observation,"duplicate resolution evidence rejected");
    bad=f.observation;bad.native_key.type=0x0f43029a;denied(r.attest(11,id,bad,104),ContentFailure::invalid_native_observation,"derived data type is not a native creation key");
    for(int kind=0;kind<8;++kind) {
        bad=f.observation;
        if(kind==0)bad.blocks[0].parent=0;if(kind==1)bad.blocks[0].symmetric=0;
        if(kind==2)bad.blocks[0].index=1;if(kind==3)bad.blocks[0].capability_start=-1;
        if(kind==4)bad.blocks[0].capability_count=2;if(kind==5)bad.capabilities[0].level=128;
        if(kind==6)bad.blocks.resize(513);if(kind==7)bad.capabilities.resize(4097);
        denied(r.attest(11,id,bad,104),ContentFailure::invalid_native_observation,"malformed or excessive native scalar observation rejected");
    }
    ok(r.attest(11,id,f.observation,104),"qualified authority scalar fixture");
    check(r.candidate(21,id,104)&&r.candidate(22,id,104)&&r.version_count()==0&&!r.blob(f.observation.png),"validation exposes loading candidate without publishing");
    denied(r.commit(21,id,105),ContentFailure::not_ready,"every peer must finish native loading");
    bad=f.observation;bad.capabilities[0].level=3;denied(r.ready(21,id,bad,105),ContentFailure::properties_mismatch,"client ability change cannot override authority");
    bad=f.observation;bad.native_key.instance=4321;ok(r.ready(21,id,bad,105),"different local resource key maps to same native properties");
    denied(r.commit(21,id,105),ContentFailure::not_ready,"one ready peer cannot publish for the other");
    bad.native_key.instance=8765;ok(r.ready(22,id,bad,105),"second distinct original resource mapping");
    denied(r.commit(22,id,105),ContentFailure::wrong_owner,"only draft owner commits version");
    Digest version=r.proposed(id)->version;ok(r.commit(21,id,106),"complete native-attested fixture publication");
    auto m=r.published(version);check(m&&m->png==f.observation.png&&m->owner==1&&m->revision==1&&m->parent==Digest{}&&m->parts.size()==1,"immutable manifest identifies owner parent bytes and required parts");
    check(r.current(1)==version&&r.current(2)==Digest{}&&r.version_count()==1&&r.pending_count()==0,"publication is one atomic owner version change");
    check(r.blob(m->png)&&*r.blob(m->png)==f.bytes,"approved bytes remain content addressed");
    ContentKey k{};check(r.mapping(11,m->png,k)&&k.instance==123&&r.mapping(21,m->png,k)&&k.instance==4321&&r.mapping(22,m->png,k)&&k.instance==8765,"global content maps independently to each local key");
    denied(r.commit(21,id,107),ContentFailure::unknown_transaction,"replayed commit cannot create another version");
    denied(r.begin(21,{},108),ContentFailure::stale_base,"old base cannot overwrite published version");
    auto same=f.begin();f.upload(same);denied(r.attest(11,same,f.observation,104),ContentFailure::unchanged_content,"unchanged bytes do not create a new version");ok(r.cancel(21,same,105),"discard no-op draft");
    f.bytes=variant();f.observation.png=hash(f.bytes);auto next=f.begin();f.upload(next);auto next_version=f.finish(next);
    check(next_version!=version&&r.published(version)->version==version&&r.published(next_version)->parent==version&&r.published(next_version)->revision==2,"new bytes produce a new immutable linked version without rewriting old content");
}
void cancellation_tests() {
    for(int phase=0;phase<4;++phase) {
        Fixture f;auto id=f.begin();
        if(phase>=1)ok(f.registry.offer(21,id,f.observation.png,static_cast<uint32_t>(f.bytes.size()),101),"cancel fixture offer");
        if(phase>=2){ok(f.registry.append(21,id,0,f.bytes,102),"cancel fixture append");ok(f.registry.seal(21,id,103),"cancel fixture seal");}
        if(phase>=3)ok(f.registry.attest(11,id,f.observation,104),"cancel fixture attestation");
        denied(f.registry.cancel(22,id,105),ContentFailure::wrong_owner,"foreign cancel cannot discard owner work");
        ok(f.registry.cancel(21,id,105),"owner cancel at every stage");
        check(f.registry.pending_count()==0&&f.registry.version_count()==0&&!f.registry.blob(f.observation.png),"cancel discards all unpublished bytes");
    }
    Fixture f;auto id=f.begin();f.upload(id);f.registry.disconnect(21);
    check(f.registry.pending_count()==0&&!f.registry.candidate(11,id,105),"owner disconnect revokes pending native response");
    check(f.registry.connect(31,1,false),"replacement owner authenticates");denied(f.registry.commit(31,id,105),ContentFailure::unknown_transaction,"replacement cannot adopt old owner transaction");
    auto b=f.registry.begin(31,{},106);ok(b,"replacement new draft");f.registry.disconnect(11);
    check(f.registry.pending_count()==0,"authority loss clears pending work");denied(f.registry.begin(31,{},107),ContentFailure::authority_unavailable,"disconnected authority never permits publication");
    check(f.registry.connect(12,0,true),"new authority has new connection");auto t=f.registry.begin(31,{},108);ok(t,"new authority permits fresh draft");
    f.registry.fence();denied(f.registry.cancel(31,t.transaction,109),ContentFailure::unknown_transaction,"scene fence revokes transactions");
    Fixture expiry;auto e=expiry.begin();denied(expiry.registry.offer(21,e,expiry.observation.png,68,100+ContentRegistry::transaction_lifetime_ms),ContentFailure::expired,"transaction deadline does not extend on upload");
    check(expiry.registry.pending_count()==0,"expired draft removed");
    Fixture concurrent;auto a=concurrent.begin();auto other=concurrent.begin(22);concurrent.upload(a);
    ok(concurrent.registry.cancel(22,other,104),"other owner can cancel while first validates");auto ver=concurrent.finish(a);check(concurrent.registry.published(ver)!=nullptr,"independent editor drafts do not pause other owner commit");
}
void corruption_and_fence_tests() {
    Fixture f;auto id=f.begin();ok(f.registry.offer(21,id,f.observation.png,68,101),"bad hash fixture offer");
    auto corrupt=f.bytes;corrupt[45]^=1;ok(f.registry.append(21,id,0,corrupt,102),"quarantined corrupt bytes");
    denied(f.registry.seal(21,id,103),ContentFailure::hash_mismatch,"corrupt bytes rejected before native exposure");check(!f.registry.pending_count(),"bad hash discarded");
    f.observation.png=hash(corrupt);id=f.begin();ok(f.registry.offer(21,id,f.observation.png,68,101),"rehashed corrupt PNG offered");
    ok(f.registry.append(21,id,0,corrupt,102),"rehashed corrupt bytes quarantined");auto bad=f.registry.seal(21,id,103);
    check(bad.failure==ContentFailure::invalid_png&&bad.detail=="png_crc"&&!f.registry.pending_count(),"PNG CRC rejects content with its own matching hash");
    Fixture peers;id=peers.begin();peers.upload(id);auto ver=peers.finish(id);auto blob=peers.observation.png;
    peers.registry.fence();ContentKey k{};check(!peers.registry.mapping(21,blob,k)&&peers.registry.published(ver),"scene fence retains immutable data but removes native readiness");
    auto second=peers.begin(22);peers.upload(second,22);auto wrong=peers.observation;wrong.capabilities[0].level=4;
    denied(peers.registry.attest(11,second,wrong,104),ContentFailure::properties_mismatch,"same bytes cannot be reinterpreted into new native powers");
    Fixture late;id=late.begin();late.upload(id);ok(late.registry.attest(11,id,late.observation,104),"late peer fixture attested");
    ok(late.registry.ready(21,id,late.observation,105),"old owner ready");ok(late.registry.ready(22,id,late.observation,105),"old peer ready");
    check(late.registry.connect(32,2,false),"replace ready peer");denied(late.registry.commit(21,id,106),ContentFailure::not_ready,"replacement peer must perform fresh native readiness");
    ok(late.registry.ready(32,id,late.observation,106),"fresh peer reports native readiness");ok(late.registry.commit(21,id,107),"new peer readiness unlocks owner commit");
}
}
int main() {
    try {validation_tests();cancellation_tests();corruption_and_fence_tests();
        std::cout<<"PASS "<<checks<<" HOST content registry/transaction checks; native observations are fixtures, SPORE NOT RUN.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
