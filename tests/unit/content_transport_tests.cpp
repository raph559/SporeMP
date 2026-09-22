#include "../../src/network/coordinator.h"
#include "../../src/network/content_control.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace sporemp::network;
namespace {
unsigned checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
Digest mark(uint8_t byte){Digest d{};d.fill(byte);return d;}
SessionConfig config(){SessionConfig c;c.identity.build=mark(1);c.identity.executable=mark(2);c.identity.content=mark(3);c.identity.fixture=mark(4);
    for(size_t n=0;n<world_file_count;++n)c.identity.world[n]=mark(uint8_t(4+n));c.authority=mark(21);c.player1=mark(22);c.player2=mark(23);return c;}
std::vector<uint8_t> png(){return {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x60,0x00,0x02,0x00,0x00,0x05,0x00,0x01,0x7a,0x5e,0xab,0x3f,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};}
Digest hash(const std::vector<uint8_t>& bytes){Digest d{};check(content_digest(bytes.data(),bytes.size(),d),"test content hash");return d;}
ContentObservation observation(const SessionConfig& c) {
    ContentObservation o;o.png=hash(png());o.native_key={0x40626200,123,0x2b978c46};o.model_type=0x9ea3031a;
    o.installed_profile=c.identity.content;o.world=c.identity.world;
    ContentBlock b;b.part={0x40626000,456,0x00b1b104};b.capability_count=1;o.blocks.push_back(b);o.resolved_parts.push_back(b.part);
    o.capabilities.push_back({{'b','i','t','e'},2});return o;
}
void codec() {
    ContentFrame f;f.op=ContentOp::offer;f.request=0xfedcba9876543210;f.transaction=123;f.identity=mark(9);f.total=68;
    ContentWire bytes{};check(encode_content(f,bytes),"encode content frame");ContentFrame decoded;
    check(decode_content(bytes,decoded)&&decoded.request==f.request&&decoded.transaction==123&&decoded.total==68&&decoded.identity==f.identity,"content codec roundtrip");
    check(bytes[0]==0x53&&bytes[1]==0x4d&&bytes[2]==0x43&&bytes[3]==0x38&&bytes[16]==0x10&&bytes[23]==0xfe,"content envelope has fixed independent little-endian offsets");
    auto bad=bytes;bad[511]=1;check(!decode_content(bad,decoded),"content reserved tail rejected");bad=bytes;bad[88]=0xff;bad[89]=0xff;check(!decode_content(bad,decoded),"oversized content data count rejected");
    bad=bytes;bad[4]=2;check(!decode_content(bad,decoded),"unknown content schema rejected");
    f.data.resize(content_chunk_bytes+1);check(!encode_content(f,bytes),"oversized content frame cannot encode");f.data.clear();
    Packet packet;packet.kind=Kind::content_request;packet.sequence=1;packet.session=9;check(encode_content(f,packet.content),"valid content payload encoded");
    auto wire=encode(packet);Packet restored;std::string error;
    check(decode(wire.data(),wire.size(),restored,error)&&restored.kind==Kind::content_request&&restored.content==packet.content,"authenticated transport retains content frame without interpreting entity scalars");
    wire[599]=1;check(!decode(wire.data(),wire.size(),restored,error),"transport content tail reserved");wire=encode(packet);wire[12]=5;check(!decode(wire.data(),wire.size(),restored,error),"old network schema cannot carry content RPC");
    for(auto offset:{24,28,48,56,64}){wire=encode(packet);wire[offset]=1;check(!decode(wire.data(),wire.size(),restored,error),"content transport header cannot smuggle game/world fields");}
    auto o=observation(config());o.capabilities[0].level=-1;std::vector<uint8_t> encoded;
    check(encode_observation(o,encoded)&&encoded.size()==352,"explicit native observation encoding size");ContentObservation copy;
    check(decode_observation(encoded,copy)&&copy.native_key==o.native_key&&copy.world==o.world&&copy.blocks.size()==1&&copy.capabilities[0].level==-1,"native observation signed levels and copied scalar identity roundtrip");
    for(size_t size=0;size<encoded.size();++size){auto shorter=encoded;shorter.resize(size);check(!decode_observation(shorter,copy),"every truncated native observation rejected");}
    auto larger=encoded;larger.push_back(0);check(!decode_observation(larger,copy),"trailing native observation bytes rejected");
    larger=encoded;larger[280]=0xff;larger[281]=0xff;check(!decode_observation(larger,copy),"oversized rigblock count refused before allocation");
    o.blocks.resize(512);o.capabilities.resize(4096);o.resolved_parts.resize(512);
    check(encode_observation(o,encoded)&&encoded.size()==59684&&decode_observation(encoded,copy),"maximum native value envelope remains bounded below 64KiB");
    o.blocks.push_back({});check(!encode_observation(o,encoded),"oversized native value envelope refuses encode");
    ContentManifest manifest;manifest.png=mark(1);manifest.native_properties=mark(2);manifest.installed_profile=config().identity.content;
    manifest.world=config().identity.world;manifest.png_bytes=68;manifest.parts={{0x40626000,456,0x00b1b104}};
    ContentDependencies deps;check(encode_dependencies(manifest,encoded)&&encoded.size()==316&&decode_dependencies(encoded,deps),"bounded dependency manifest roundtrip");
    check(deps.png==manifest.png&&deps.parts==manifest.parts&&deps.world==manifest.world,"dependency manifest binds original requirements to exact content/world");
    for(size_t n=0;n<encoded.size();++n){auto truncated=encoded;truncated.resize(n);check(!decode_dependencies(truncated,deps),"every truncated dependency manifest refused");}
    auto invalid=encoded;invalid[304+8]=0;check(!decode_dependencies(invalid,deps),"unsupported native dependency type refused");
    manifest.parts.push_back(manifest.parts.front());check(!encode_dependencies(manifest,encoded),"duplicate native dependency refused");
    manifest.parts.clear();for(uint32_t n=1;n<=512;++n)manifest.parts.push_back({0x40626000,n,0x00b1b104});
    check(encode_dependencies(manifest,encoded)&&encoded.size()==max_dependency_bytes&&decode_dependencies(encoded,deps)&&deps.parts.size()==512,"maximum dependency set remains bounded across transport chunks");
    invalid=encoded;invalid[300]=0xff;invalid[301]=0xff;check(!decode_dependencies(invalid,deps),"oversized dependency count refused before allocation");
    manifest.parts.push_back({0x40626000,513,0x00b1b104});check(!encode_dependencies(manifest,encoded),"oversized dependency set refused");
}
struct Client {
    Peer peer;uint64_t next=0;std::vector<ContentFrame> events;
    void start(const PeerConfig& c){std::string error;check(peer.start(c,error),"real TLS content client start");packet(Kind::welcome);}
    Packet packet(Kind kind) {
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);Event e;
        while(std::chrono::steady_clock::now()<end) {
            while(peer.poll(e)) {
                if(e.kind==EventKind::disconnected)throw std::runtime_error("content TLS disconnected: "+e.detail);
                if(e.kind!=EventKind::packet)continue;
                if(e.packet.kind==Kind::reject)throw std::runtime_error("content TLS reject: "+error_detail(e.packet));
                if(e.packet.kind==kind)return e.packet;
                if(e.packet.kind==Kind::content_response){ContentFrame f;check(decode_content(e.packet.content,f),"real TLS event codec");events.push_back(f);}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }throw std::runtime_error("content TLS packet deadline");
    }
    ContentFrame rpc(ContentFrame f,ContentFailure expected=ContentFailure::none) {
        f.request=++next;Packet p;p.kind=Kind::content_request;check(encode_content(f,p.content)&&peer.send(p),"real TLS content request enqueued");
        for(;;) {
            auto reply=packet(Kind::content_response);ContentFrame r;check(decode_content(reply.content,r),"real TLS content response decoded");
            if(!r.request){events.push_back(r);continue;}
            check(r.request==f.request&&r.op==f.op,"content response correlates exact request and operation");
            if(r.failure!=expected)std::cerr<<"RPC op "<<uint32_t(f.op)<<" returned "<<content_failure_name(r.failure)<<'\n';
            check(r.failure==expected,"real TLS content result matches expected policy");return r;
        }
    }
    ContentFrame simple(ContentOp op,uint64_t transaction=0,ContentFailure expected=ContentFailure::none){ContentFrame f;f.op=op;f.transaction=transaction;return rpc(f,expected);}
    ContentFrame observed(uint64_t id,const ContentObservation& o,ContentFailure expected=ContentFailure::none) {
        std::vector<uint8_t> bytes;check(encode_observation(o,bytes),"encode native fixture attestation");ContentFrame f;f.op=ContentOp::observation_begin;f.transaction=id;f.identity=hash(bytes);f.total=static_cast<uint32_t>(bytes.size());rpc(f);
        for(size_t offset=0;offset<bytes.size();offset+=content_chunk_bytes) {
            f={};f.op=ContentOp::observation_chunk;f.transaction=id;f.offset=static_cast<uint32_t>(offset);
            f.data.assign(bytes.begin()+offset,bytes.begin()+std::min(bytes.size(),offset+content_chunk_bytes));rpc(f);
        }
        return simple(ContentOp::observation_end,id,expected);
    }
};
Entity actor(uint64_t id,uint64_t owner){Entity e;e.id=id;e.owner=owner;e.generation=1;e.native_id=uint32_t(id);e.health=10;e.energy=10;e.hunger=10;e.scale=1;return e;}
void real_transport() {
    CoordinatorConfig cfg;cfg.port=0;cfg.session=config();Coordinator server;std::string error;check(server.start(cfg,error),"real TLS content coordinator starts");
    std::atomic<bool> stop{false};std::thread pump([&]{CoordinatorEvent event;while(!stop){while(server.poll(event)){}std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
    try {
        Client authority,a,b;authority.start(server.peer_config(Role::authority));a.start(server.peer_config(Role::player,1));b.start(server.peer_config(Role::player,2));
        Packet scene;scene.kind=Kind::scene_begin;scene.scene=1;scene.baseline=1;scene.count=2;authority.peer.send(scene);
        scene.kind=Kind::entity;scene.entity=actor(101,1);authority.peer.send(scene);scene.entity=actor(102,2);authority.peer.send(scene);
        scene.kind=Kind::scene_end;authority.peer.send(scene);auto baseline_a=a.packet(Kind::scene_end),baseline_b=b.packet(Kind::scene_end);
        Packet ack;ack.kind=Kind::baseline_ack;ack.scene=1;ack.baseline=baseline_a.baseline;a.peer.send(ack);ack.baseline=baseline_b.baseline;b.peer.send(ack);
        auto id=a.simple(ContentOp::begin).transaction;check(id!=0,"coordinator allocates fresh transaction");
        Packet jump=ack;jump.kind=Kind::action;jump.entity=actor(102,2);jump.verb=Verb::jump;jump.request=43;b.peer.send(jump);
        auto action=authority.packet(Kind::action);check(action.player==2&&action.entity.owner==2&&action.request==43,"other player's authenticated gameplay routes while an editor draft is open (HOST fixture)");
        b.simple(ContentOp::cancel,id,ContentFailure::wrong_owner);a.simple(ContentOp::cancel,id);
        a.simple(ContentOp::commit,id,ContentFailure::unknown_transaction);
        check(a.simple(ContentOp::current).identity==Digest{},"cancelled draft remains unpublished over real TLS");
        id=a.simple(ContentOp::begin).transaction;auto bytes=png();ContentFrame offer;offer.op=ContentOp::offer;offer.transaction=id;offer.total=static_cast<uint32_t>(bytes.size());offer.identity=hash(bytes);a.rpc(offer);
        b.simple(ContentOp::fetch,id,ContentFailure::not_ready);ContentFrame part;part.op=ContentOp::chunk;part.transaction=id;part.data=bytes;a.rpc(part);a.simple(ContentOp::seal,id);
        b.simple(ContentOp::fetch,id,ContentFailure::not_ready);auto fetched=authority.simple(ContentOp::fetch,id);check(fetched.data==bytes&&fetched.total==bytes.size(),"only authority receives exact complete quarantined PNG over TLS");
        a.simple(ContentOp::observation_end,id,ContentFailure::wrong_phase);
        auto o=observation(cfg.session);auto wrong=o;wrong.world[3][0]^=1;
        auto refusal=authority.observed(id,wrong,ContentFailure::world_mismatch);check(refusal.world_index==3,"native world mismatch retains precise file index over TLS");
        wrong=o;wrong.blocks[0].part.instance=457;refusal=authority.observed(id,wrong,ContentFailure::missing_part);check(refusal.missing==wrong.blocks[0].part,"missing installed part retains full native key over TLS");
        authority.observed(id,o);check(b.simple(ContentOp::fetch,id).data==bytes,"authority validation opens exact approved bytes to client");
        auto required=b.simple(ContentOp::dependencies,id);ContentDependencies deps;
        check(decode_dependencies(required.data,deps)&&deps.png==o.png&&deps.parts==o.resolved_parts,"real TLS carries authority-derived requirements before import");
        check(required.identity==hash(required.data),"dependency transfer binds complete exact bytes");
        a.simple(ContentOp::commit,id,ContentFailure::not_ready);wrong=o;wrong.capabilities[0].level=99;a.observed(id,wrong,ContentFailure::properties_mismatch);
        o.native_key.instance=777;a.observed(id,o);a.simple(ContentOp::commit,id,ContentFailure::not_ready);o.native_key.instance=999;b.observed(id,o);
        ContentFrame denied;denied.op=ContentOp::dependency_failure;denied.transaction=id;denied.failure=ContentFailure::missing_part;denied.missing=o.blocks.front().part;
        b.rpc(denied);a.simple(ContentOp::commit,id,ContentFailure::not_ready);
        denied.missing.instance++;b.rpc(denied,ContentFailure::malformed);
        b.observed(id,o);
        auto result=a.simple(ContentOp::commit,id);check(result.identity!=Digest{},"native-attested fixture publishes immutable version over TLS");
        auto current=a.simple(ContentOp::current);check(current.identity==result.identity&&current.data==std::vector<uint8_t>(o.png.begin(),o.png.end()),"query returns exactly committed content identity");
        check(b.simple(ContentOp::current).identity==Digest{},"publication cannot overwrite another owner");
        a.simple(ContentOp::commit,id,ContentFailure::unknown_transaction);
        ContentFrame stale;stale.op=ContentOp::begin;a.rpc(stale,ContentFailure::stale_base);
        stale.identity=result.identity;auto draft=a.rpc(stale).transaction;
        scene.kind=Kind::scene_begin;scene.scene=2;scene.baseline=2;scene.count=0;authority.peer.send(scene);
        scene.kind=Kind::scene_end;authority.peer.send(scene);a.packet(Kind::scene_end);b.packet(Kind::scene_end);
        a.simple(ContentOp::cancel,draft,ContentFailure::unknown_transaction);
        check(a.simple(ContentOp::current).identity==result.identity,"scene reset fences drafts while preserving immutable published metadata");
        a.peer.stop();b.peer.stop();authority.peer.stop();
    }catch(...){stop=true;pump.join();server.stop();throw;}
    stop=true;pump.join();server.stop();
}
}
int main(){try{codec();real_transport();std::cout<<"PASS "<<checks<<" HOST content codec/policy and real Windows Schannel TLS checks; SPORE NOT RUN.\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
