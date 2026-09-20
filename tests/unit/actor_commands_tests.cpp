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
    return failures?1:0;
}
