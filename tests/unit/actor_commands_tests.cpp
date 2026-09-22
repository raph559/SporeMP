#include "actor_commands.h"
#include <cstdio>
using namespace sporemp;
int main() {
    int failures=0;
    auto check=[&](bool result,const char* name) {std::printf("%s: %s (HOST/FIXTURE ONLY)\n",result?"PASS":"FAIL",name);if(!result)++failures;};
    ActorCommands c;
    auto a=c.bind(0x1000,1),b=c.bind(0x2000,2),npc=c.bind(0x3000,0);
    check(a.live && b.live && a.id!=b.id && npc.live,"independent owner bindings and native NPC identity");
    check(!c.bind(0x1000,2).live && c.bind(0x1000,1).id==a.id,"an existing actor cannot be claimed by another owner");
    ActorCommand attack{0,a.id,npc.id,2,ActorVerb::attack};
    check(c.enqueue(attack)==ActorDecision::wrong_owner,"B cannot command A or spend through its native action");
    attack.owner=1;
    check(c.enqueue(attack)==ActorDecision::accepted && attack.sequence==1,"authorized command is queued with a unique sequence");
    ActorCommand queued;
    c.invalidate(0x1000);
    auto recreated=c.bind(0x1000,1);
    check(recreated.id!=a.id && c.pop(queued) && c.authorize(queued)==ActorDecision::unknown_actor,"destroy and address reuse reject a command already waiting in the queue");
    attack.actor=recreated.id;attack.target=b.id;
    check(c.enqueue(attack)==ActorDecision::accepted,"recreated actor uses its fresh identity");
    c.invalidate(0x2000);
    check(c.pop(queued) && c.authorize(queued)==ActorDecision::invalid_target,"target destruction between enqueue and dispatch rejects attack");
    attack.verb=ActorVerb::approach;
    check(c.enqueue(attack)==ActorDecision::invalid_target,"approach rejects a destroyed target before native path planning");
    attack.verb=ActorVerb::engage;
    check(c.enqueue(attack)==ActorDecision::invalid_target,"held attack rejects a destroyed target");
    attack.target=npc.id;attack.owner=2;
    check(c.enqueue(attack)==ActorDecision::wrong_owner,"held attack cannot cross actor ownership");
    ActorCommand move{0,recreated.id,0,1,ActorVerb::move};
    check(c.enqueue(move)==ActorDecision::accepted,"movement has no target dependency");
    c.scene_exit();
    check(!c.pop(queued) && c.authorize(move)==ActorDecision::unknown_actor,"scene exit fences bindings and cancels queued native work");
    auto next=c.bind(0x1000,1);
    check(next.epoch>recreated.epoch && next.id>recreated.id,"scene reload cannot reuse prior authority IDs");
    move.actor=next.id;
    bool filled=true;uint64_t last=0;
    for(size_t i=0;i<ActorCommands::queue_capacity;++i) filled &= c.enqueue(move)==ActorDecision::accepted;
    check(filled && c.enqueue(move)==ActorDecision::full,"bounded queue rejects overload");
    bool ordered=true;
    while(c.pop(queued)) {ordered &= queued.sequence>last;last=queued.sequence;}
    check(ordered,"queued inputs preserve submission ordering");
    ActorCommands pickups;
    auto feeder=pickups.bind(0x4000,1),corpse=pickups.bind(0x5000,0);
    ActorCommand pickup{0,feeder.id,corpse.id,2,ActorVerb::pickup};
    check(uint32_t(ActorVerb::pickup)==7 && pickups.enqueue(pickup)==ActorDecision::wrong_owner,
        "pickup7 cannot command another owner's actor");
    pickup.owner=1;pickup.target=0;
    check(pickups.enqueue(pickup)==ActorDecision::invalid_target,"pickup requires an admitted local target");
    pickup.target=feeder.id;
    check(pickups.enqueue(pickup)==ActorDecision::invalid_target,"pickup rejects self target");
    pickup.target=corpse.id;
    check(pickups.enqueue(pickup)==ActorDecision::accepted,"pickup with valid owner and target queues once");
    pickups.invalidate(0x5000);
    auto replacement=pickups.bind(0x5000,0);
    check(replacement.id!=corpse.id && pickups.pop(queued) && pickups.authorize(queued)==ActorDecision::invalid_target,
        "corpse destruction and same-address reuse fence an already queued pickup");
    pickup.target=replacement.id;
    check(pickups.enqueue(pickup)==ActorDecision::accepted,"fresh target incarnation accepts a new pickup intention");
    pickups.invalidate(0x4000);
    check(pickups.pop(queued) && pickups.authorize(queued)==ActorDecision::unknown_actor,
        "feeder destruction fences pickup before native dispatch");
    return failures?1:0;
}
