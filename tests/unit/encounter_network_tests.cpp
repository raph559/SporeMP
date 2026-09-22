#include "session.h"
#include <limits>
#include <stdexcept>
#include <string>

using namespace sporemp::network;
namespace {
int assertions=0;
void check(bool value,const char* reason){++assertions;if(!value)throw std::runtime_error(reason);}
Digest digest(uint8_t value){Digest d{};d.fill(value);return d;}
Entity entity(uint64_t id,uint64_t owner){Entity e;e.id=id;e.generation=7;e.owner=owner;e.native_id=uint32_t(id);e.tick=1;e.health=10;e.dna=8.75f;return e;}
SessionConfig config(){SessionConfig c;c.identity={digest(1),digest(2),digest(3),digest(4)};for(auto& value:c.identity.world)value=digest(4);c.authority=digest(5);c.player1=digest(6);c.player2=digest(7);return c;}
struct Fixture {
    SessionConfig settings=config();Session session{settings};uint64_t sequence[8]{};
    std::vector<Dispatch> send(uint64_t id,Packet p){p.sequence=++sequence[id];p.session=p.kind==Kind::hello?0:settings.epoch;return session.receive(id,p);}
    std::vector<Dispatch> join(uint64_t id,Role role,uint64_t owner=1){check(session.connect(id),"encounter connection");Packet p;p.kind=Kind::hello;p.identity=settings.identity;p.role=role;p.credential=role==Role::authority?settings.authority:owner==1?settings.player1:settings.player2;return send(id,p);}
    std::vector<Dispatch> initial(){join(1,Role::authority);join(2,Role::player);Packet p;p.scene=3;p.baseline=1;p.count=3;p.kind=Kind::scene_begin;send(1,p);p.kind=Kind::entity;for(auto e:{entity(101,1),entity(102,2),entity(103,0)}){p.entity=e;send(1,p);}p.kind=Kind::scene_end;return send(1,p);}
};
uint64_t baseline(const std::vector<Dispatch>& out){for(const auto& d:out)if(d.packet.kind==Kind::scene_begin)return d.packet.baseline;return 0;}
bool refused(const std::vector<Dispatch>& out,Error error){return out.size()==1&&out[0].packet.kind==Kind::action_result&&out[0].packet.error==error&&!out[0].close;}
const Entity* find(const std::vector<Dispatch>& out,uint64_t id){for(const auto& d:out)if(d.packet.kind==Kind::entity&&d.packet.entity.id==id)return &d.packet.entity;return nullptr;}
void pickup_codec_tests(){
    Packet p;p.kind=Kind::entity;p.entity=entity(103,0);p.entity.life_state=1;p.entity.health=0;
    p.entity.fed_on=1;p.entity.food=-0.25f;p.entity.pickup_owner=2;
    Packet decoded;std::string error;auto wire=encode(p);
    check(decode(wire.data(),wire.size(),decoded,error)&&decoded.entity.fed_on==1&&
        decoded.entity.food==-0.25f&&decoded.entity.pickup_owner==2,"first-feed beneficiary and signed native food roundtrip");
    check(wire.size()==600&&wire[12]==7&&wire[388]==1&&wire[389]==0&&wire[390]==0&&wire[391]==0&&
        wire[392]==0&&wire[393]==0&&wire[394]==0x80&&wire[395]==0xbe&&
        wire[396]==2&&wire[397]==0&&wire[398]==0&&wire[399]==0,"schema7 preserves first-feed offsets388 through399");
    auto bad=wire;bad[383]=1;
    check(!decode(bad.data(),bad.size(),decoded,error)&&error=="nonzero_reserved","context byte383 remains reserved in schema7");
    bad=wire;bad[399]=1;
    check(!decode(bad.data(),bad.size(),decoded,error)&&error=="invalid_entity","byte399 is beneficiary high byte and rejects oversized owner, not reserved data");
    bad=wire;bad[389]=1;
    check(!decode(bad.data(),bad.size(),decoded,error)&&error=="invalid_entity","first-feed flag cannot truncate a nonzero high byte");
    bad=wire;bad[12]=3;
    check(!decode(bad.data(),bad.size(),decoded,error)&&error=="unsupported_protocol_or_schema","schema3 cannot silently discard first-feed state");
    for(uint32_t invalid:{2u,256u,UINT32_MAX}){
        p.entity.fed_on=invalid;wire=encode(p);
        check(!decode(wire.data(),wire.size(),decoded,error),"first-feed flag outside zero/one rejected");
    }
    p.entity.fed_on=1;
    for(uint32_t invalid:{3u,256u,UINT32_MAX}){
        p.entity.pickup_owner=invalid;wire=encode(p);
        check(!decode(wire.data(),wire.size(),decoded,error),"unknown first-feed beneficiary rejected");
    }
    p.entity.pickup_owner=1;p.entity.fed_on=0;wire=encode(p);
    check(!decode(wire.data(),wire.size(),decoded,error),"beneficiary without observed first-feed flag rejected");
    p.entity.pickup_owner=0;p.entity.fed_on=1;p.entity.food=3.5f;wire=encode(p);
    check(decode(wire.data(),wire.size(),decoded,error)&&decoded.entity.pickup_owner==0&&decoded.entity.food==3.5f,
        "already-fed corpse can retain nutrition with unknown historical beneficiary");
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),1.01e9f,-1.01e9f}){
        p.entity.food=invalid;wire=encode(p);
        check(!decode(wire.data(),wire.size(),decoded,error),"nonfinite or unbounded native food rejected");
    }
    for(float accepted:{-1.0e9f,-0.001f,0.f,1.0e9f}){
        p.entity.food=accepted;wire=encode(p);
        check(decode(wire.data(),wire.size(),decoded,error)&&decoded.entity.food==accepted,"finite bounded food preserves signed native value without clamp");
    }
    p.kind=Kind::action;p.entity=entity(101,1);p.verb=Verb::pickup;p.target=103;p.target_generation=7;wire=encode(p);
    check(uint32_t(Verb::pickup)==7&&targeted(Verb::pickup)&&decode(wire.data(),wire.size(),decoded,error)&&decoded.verb==Verb::pickup,
        "pickup7 is a targeted intention on the wire");
    p.target=0;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"pickup requires a target ID");
    p.target=103;p.target_generation=0;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"pickup requires target incarnation");
    p.target_generation=7;p.direction=1;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"pickup cannot carry movement direction");
    p.direction=0;p.verb=Verb(8);wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"verb beyond pickup remains unsupported");
}
void pickup_session_tests(){
    Fixture f;auto initial=f.initial();Packet ack;ack.kind=Kind::baseline_ack;ack.scene=3;ack.baseline=baseline(initial);f.send(2,ack);
    Packet update;update.kind=Kind::motion;update.scene=3;update.baseline=1;update.entity=entity(103,0);update.entity.tick=2;update.entity.food=4;f.send(1,update);
    Packet action=ack;action.kind=Kind::action;action.entity=entity(101,1);action.verb=Verb::pickup;action.target=103;action.target_generation=7;action.request=10;
    check(refused(f.send(2,action),Error::not_ready),"positive food alone does not admit pickup of a living NPC");
    update.entity.life_state=1;update.entity.health=0;update.entity.tick=3;f.send(1,update);
    action.request++;action.player=2;action.entity.fed_on=1;action.entity.pickup_owner=2;action.entity.food=999;
    auto out=f.send(2,action);
    check(out.size()==1&&out[0].connection==1&&out[0].packet.verb==Verb::pickup&&out[0].packet.player==1&&
        out[0].packet.target==103&&out[0].packet.target_generation==7&&out[0].packet.entity.fed_on==0&&
        out[0].packet.entity.pickup_owner==0&&out[0].packet.entity.food==0,"corpse pickup routes authenticated actor and discards forged actor pickup fields");
    action.request++;action.target_generation=6;
    check(refused(f.send(2,action),Error::stale),"pickup rejects obsolete corpse incarnation");
    action.target_generation=7;action.target=102;
    check(refused(f.send(2,action),Error::ownership),"pickup does not admit another owned actor as a corpse resource");
    action.target=103;action.entity.generation=6;out=f.send(2,action);
    check(out.size()==1&&out[0].packet.kind==Kind::reject&&out[0].packet.error==Error::ownership,"pickup rejects obsolete controlled-actor incarnation");
    action.entity=entity(102,2);out=f.send(2,action);
    check(out.size()==1&&out[0].packet.kind==Kind::reject&&out[0].packet.error==Error::ownership,"player1 cannot issue pickup as player2");
    action.entity=entity(101,1);action.request++;
    update.entity.tick=4;update.entity.fed_on=1;update.entity.pickup_owner=2;update.entity.food=3.5f;out=f.send(1,update);
    check(out.size()==1&&out[0].packet.entity.fed_on==1&&out[0].packet.entity.pickup_owner==2&&out[0].packet.entity.food==3.5f,
        "authoritative first-feed beneficiary publishes independently of remaining food");
    out=f.send(2,action);
    check(out.size()==1&&out[0].connection==1&&out[0].packet.kind==Kind::action,"first-feed bonus already claimed does not imply all corpse nutrition is consumed");
    auto joined=f.join(3,Role::player,2);auto corpse=find(joined,103);
    check(corpse&&corpse->life_state==1&&corpse->fed_on==1&&corpse->pickup_owner==2&&corpse->food==3.5f,
        "late join baseline retains first-feed flag beneficiary and remaining nutrition");
    const Entity fed=update.entity;
    for(uint64_t stale_tick:{uint64_t(3),uint64_t(4)}){
        update.entity=fed;update.entity.tick=stale_tick;update.entity.fed_on=0;update.entity.pickup_owner=0;update.entity.food=4;
        out=f.send(1,update);check(out.size()==1&&out[0].packet.error==Error::stale,"old or duplicate packet cannot erase first-feed state or refill food");
    }
    update.entity=fed;update.entity.generation=6;update.entity.tick=99;update.entity.pickup_owner=1;out=f.send(1,update);
    check(out.size()==1&&out[0].packet.error==Error::stale,"obsolete corpse generation cannot transfer beneficiary with a newer tick");
    joined=f.join(4,Role::player,2);corpse=find(joined,103);
    check(corpse&&corpse->generation==7&&corpse->tick==4&&corpse->fed_on==1&&corpse->pickup_owner==2&&corpse->food==3.5f&&f.session.entity_count()==3,
        "fresh baseline proves rejected updates left the authoritative corpse unchanged");
    update.entity=fed;update.entity.tick=5;update.entity.food=-0.25f;out=f.send(1,update);
    check(out.size()==2&&out[0].packet.entity.food==-0.25f,"native final-bite negative food publishes without clamping");
    action.request++;check(refused(f.send(2,action),Error::not_ready),"depleted negative-food corpse cannot be picked up again");
    joined=f.join(5,Role::player,2);corpse=find(joined,103);
    check(corpse&&corpse->food==-0.25f&&corpse->fed_on==1&&corpse->pickup_owner==2,"late join preserves depletion and the original first-feed beneficiary");
    update.entity.tick=6;update.entity.food=0;f.send(1,update);action.request++;
    check(refused(f.send(2,action),Error::not_ready),"zero-food corpse cannot be picked up");
    update.entity.tick=7;update.entity.food=2;f.send(1,update);
    update.entity=entity(101,1);update.entity.tick=2;update.entity.life_state=1;update.entity.health=0;f.send(1,update);action.request++;
    check(refused(f.send(2,action),Error::not_ready),"dead controlled actor cannot pick up positive-food corpse");
}
void pending_death_tests(){
    Fixture f;auto initial=f.initial();Packet ack;ack.kind=Kind::baseline_ack;ack.scene=3;ack.baseline=baseline(initial);f.send(2,ack);
    Packet update;update.kind=Kind::motion;update.scene=3;update.baseline=1;update.entity=entity(103,0);
    update.entity.tick=2;update.entity.health=0;update.entity.food=100;
    auto wire=encode(update);Packet decoded;std::string error;
    check(decode(wire.data(),wire.size(),decoded,error)&&decoded.entity.health==0&&decoded.entity.life_state==0,
        "native zero-health before dead flag roundtrips without manufacturing death");
    auto out=f.send(1,update);
    check(out.size()==1&&out[0].packet.entity.health==0&&out[0].packet.entity.life_state==0,
        "pending native death publishes the exact observed intermediate state");
    Packet action=ack;action.kind=Kind::action;action.entity=entity(101,1);action.target=103;action.target_generation=7;
    for(auto verb:{Verb::attack,Verb::approach,Verb::engage,Verb::pickup}){
        action.verb=verb;action.request++;
        check(refused(f.send(2,action),Error::not_ready),"zero-health pending death is neither living combat target nor ready corpse");
    }
    auto joined=f.join(3,Role::player,2);auto pending=find(joined,103);
    check(pending&&pending->health==0&&pending->life_state==0&&pending->food==100&&pending->generation==7,
        "fresh baseline preserves pending native death without resurrection or invented dead flag");
    update.entity.life_state=1;update.entity.tick=3;f.send(1,update);
    action.verb=Verb::pickup;action.request++;out=f.send(2,action);
    check(out.size()==1&&out[0].connection==1&&out[0].packet.verb==Verb::pickup,
        "later original dead flag admits the same corpse incarnation for pickup");
    update.entity=entity(101,1);update.entity.health=0;update.entity.tick=2;f.send(1,update);
    for(auto verb:{Verb::jump,Verb::move,Verb::stop,Verb::pickup}){
        action.verb=verb;action.target=targeted(verb)?103:0;action.target_generation=targeted(verb)?7:0;action.request++;
        check(refused(f.send(2,action),Error::not_ready),"zero-health controlled actor cannot act before native dead flag arrives");
    }
    update.entity.health=5;update.entity.tick=3;f.send(1,update);
    action.verb=Verb::jump;action.target=action.target_generation=0;action.request++;out=f.send(2,action);
    check(out.size()==1&&out[0].connection==1,"subsequent positive native health permits an ordinary action");
}
}
int encounter_network_tests(){
    Packet p;p.kind=Kind::action;p.entity=entity(101,1);p.verb=Verb::engage;p.target=103;p.target_generation=7;p.request=41;
    std::string error;Packet decoded;auto wire=encode(p);
    check(decode(wire.data(),wire.size(),decoded,error)&&decoded.target_generation==7&&decoded.target==103,"target incarnation roundtrip");
    wire[12]=1;check(!decode(wire.data(),wire.size(),decoded,error)&&error=="unsupported_protocol_or_schema","schema1 cannot omit target incarnation/life state");
    p.target_generation=0;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"target incarnation required");
    p.target_generation=7;p.verb=Verb::jump;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"untargeted command cannot smuggle target");
    p.kind=Kind::motion;p.entity.life_state=1;p.entity.health=0;wire=encode(p);
    check(decode(wire.data(),wire.size(),decoded,error)&&decoded.entity.life_state==1&&decoded.entity.health==0,"terminal native state roundtrip");
    p.entity.health=1;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"dead state with positive health rejected");
    p.entity.health=0;p.entity.life_state=2;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"unknown native life state rejected");
    p.entity.life_state=0;p.entity.health=5;p.entity.age=0;p.entity.alpha=1;p.entity.combatant_state=2;wire=encode(p);
    check(decode(wire.data(),wire.size(),decoded,error)&&decoded.entity.age==0&&decoded.entity.alpha==1&&decoded.entity.combatant_state==2,"native age alpha and combatant state roundtrip");
    for(size_t offset:{size_t(380),size_t(381),size_t(382)}){auto bad=wire;bad[offset]=3;check(!decode(bad.data(),bad.size(),decoded,error),"unsupported native context byte rejected");}
    wire[12]=2;check(!decode(wire.data(),wire.size(),decoded,error),"schema2 cannot omit native age context");
    p.entity.age=256;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"oversized native context cannot truncate to a valid byte");
    p.entity.age=0;p.entity.combatant_state=1;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"unqualified native combatant state rejected");
    p.entity.combatant_state=0;p.entity.scale=0.375f;wire=encode(p);
    check(decode(wire.data(),wire.size(),decoded,error)&&decoded.entity.scale==0.375f,"native observed scale roundtrip");
    check(wire.size()==600&&wire[384]==0&&wire[385]==0&&wire[386]==0xc0&&wire[387]==0x3e,"native scale retains fixed offset and little endian encoding");
    for(float invalid:{0.f,-1.f,1001.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){p.entity.scale=invalid;wire=encode(p);check(!decode(wire.data(),wire.size(),decoded,error),"invalid native scale rejected");}
    p.entity.scale=1;wire=encode(p);wire[383]=1;check(!decode(wire.data(),wire.size(),decoded,error),"context reserve remains rejected");

    Fixture f;auto initial=f.initial();auto b=baseline(initial);check(b&&f.session.entity_count()==3,"encounter baseline count");
    Packet ack;ack.kind=Kind::baseline_ack;ack.scene=3;ack.baseline=b;f.send(2,ack);
    Packet action=ack;action.kind=Kind::action;action.entity=entity(101,1);action.entity.native_id=999;action.entity.health=999;action.player=2;action.verb=Verb::engage;action.target=103;action.target_generation=7;action.request=1;
    auto out=f.send(2,action);check(out.size()==1&&out[0].connection==1&&out[0].packet.player==1&&out[0].packet.entity.native_id==101&&out[0].packet.entity.health==10,"untrusted client native fields replaced by authoritative values");
    action.target_generation=6;action.request=2;check(refused(f.send(2,action),Error::stale),"old target incarnation refused");
    action.target=102;action.target_generation=7;check(refused(f.send(2,action),Error::ownership),"owned target not silently admitted as PvP");
    action.target=103;
    Packet update;update.kind=Kind::motion;update.scene=3;update.baseline=1;update.entity=entity(103,0);update.entity.tick=2;update.entity.health=0;update.entity.life_state=1;
    out=f.send(1,update);check(out.size()==1&&out[0].packet.entity.life_state==1&&f.session.entity_count()==3,"native NPC death retained without despawn");
    check(refused(f.send(2,action),Error::not_ready),"attack against retained corpse refused");
    auto reconnect=f.join(3,Role::player,2);auto corpse=find(reconnect,103);
    check(corpse&&corpse->life_state==1&&corpse->health==0&&corpse->generation==7,"late join receives corpse not living fixture");
    update.entity=entity(101,1);update.entity.tick=2;update.entity.health=0;update.entity.life_state=1;update.entity.dna=17.5f;f.send(1,update);
    action.verb=Verb::jump;action.target=action.target_generation=0;check(refused(f.send(2,action),Error::not_ready),"dead controlled actor cannot act");
    f.session.disconnect(2);reconnect=f.join(4,Role::player,1);auto player=find(reconnect,101);corpse=find(reconnect,103);
    check(player&&player->life_state==1&&player->dna==17.5f&&corpse&&corpse->life_state==1&&f.session.entity_count()==3,"post-death reconnect preserves balances corpse and actor count");
    ack.baseline=baseline(reconnect);f.send(4,ack);
    update.entity.health=5;update.entity.life_state=0;update.entity.age=0;update.entity.alpha=0;update.entity.combatant_state=0;update.entity.tick=3;out=f.send(1,update);
    check(out.size()==2&&f.session.entity_count()==3&&out[0].packet.entity.age==0&&out[0].packet.entity.combatant_state==0,"same noun native revive preserves global identity and juvenile context");
    action.baseline=ack.baseline;action.request=3;out=f.send(4,action);
    check(out.size()==1&&out[0].connection==1&&out[0].packet.entity.generation==7&&out[0].packet.entity.dna==17.5f,"native revive restores intentions without reward addition");
    update.entity.tick=2;update.entity.life_state=1;update.entity.health=0;out=f.send(1,update);
    check(out.size()==1&&out[0].packet.error==Error::stale,"old terminal sample cannot undo native revive");
    pickup_codec_tests();
    pickup_session_tests();
    pending_death_tests();
    return assertions;
}
