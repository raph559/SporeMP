#include "actor_commands.h"
#include <cstdio>
using namespace sporemp;

int main() {
    int failures = 0;
    auto check = [&](bool result, const char* name) {
        std::printf("%s: %s (HOST/FIXTURE ONLY)\n", result ? "PASS" : "FAIL", name);
        if (!result) {
            ++failures;
        }
    };
    for (uint64_t owner : {1ULL, 2ULL}) {
        bool allowed = true;
        for (auto verb : {ActorVerb::move, ActorVerb::jump, ActorVerb::stop, ActorVerb::steer, ActorVerb::steer_stop,
                          ActorVerb::approach}) {
            allowed &= foreign_creature_input(owner, verb, 0);
        }
        check(allowed && foreign_creature_input(owner, ActorVerb::combat, 5),
              "each foreign owner admits locomotion, approach and the pinned Bite input at both command boundaries");
        // Trial253: a creature's second attack was refused here before the original could judge it.
        check(foreign_creature_input(owner, ActorVerb::combat, 0) &&
                  foreign_creature_input(owner, ActorVerb::combat, 7) &&
                  foreign_creature_input(owner, ActorVerb::combat, 87),
              "each foreign owner may ask for any ability slot as an attack; the creature's own ability decides "
              "natively");
        check(!foreign_creature_input(owner, ActorVerb::combat, -1) &&
                  !foreign_creature_input(owner, ActorVerb::combat, 88),
              "an attack outside the ability slots is refused");
        bool denied = true;
        for (auto verb :
             {ActorVerb::attack, ActorVerb::retire, ActorVerb::engage, ActorVerb::social, ActorVerb::collect}) {
            denied &= !foreign_creature_input(owner, verb, 5);
        }
        check(denied, "foreign input admission cannot silently enable other interaction families");
    }
    for (uint64_t owner : {1, 2}) {
        check(foreign_creature_input(owner, ActorVerb::forage, 2804) &&
                  !foreign_creature_input(owner, ActorVerb::forage, 0) &&
                  !foreign_creature_input(owner, ActorVerb::forage, -3),
              "either owner may ask to eat a fruit it names; the original decides whether its creature can");
    }
    check(!foreign_creature_input(0, ActorVerb::steer, 0) && !foreign_creature_input(3, ActorVerb::combat, 5) &&
              !foreign_creature_input(0x100000001ULL, ActorVerb::combat, 5),
          "foreign owner admission rejects unassigned and wide IDs before native slot conversion");
    // Trial320: the second player's creature could not regain the health a fight cost it; feeding was refused here.
    check(foreign_creature_input(1, ActorVerb::pickup, 0) && foreign_creature_input(2, ActorVerb::pickup, 0) &&
              !foreign_creature_input(1, ActorVerb::pickup, 5) && !foreign_creature_input(2, ActorVerb::pickup, 5) &&
              !foreign_creature_input(3, ActorVerb::pickup, 0),
          "each foreign owner may ask to feed, with the zero argument only");
    // Pinned cCreatureAbility: categories social 0, attack 1, neutral 2; types as named below.
    check(client_ability_route(37, 0) == AbilityRoute::social && client_ability_route(34, 0) == AbilityRoute::social &&
              client_ability_route(30, 1) == AbilityRoute::combat &&
              client_ability_route(33, 1) == AbilityRoute::combat,
          "Dance to Sing go to the authority as social input, Bite to Strike as attacks");
    // Trial274: the bar's Jump button made the client's own creature jump where only that player saw it.
    check(client_ability_route(17, 2) == AbilityRoute::jump,
          "the bar's Jump goes to the authority as that player's jump");
    check(client_ability_route(16, 2) == AbilityRoute::local && client_ability_route(18, 2) == AbilityRoute::local &&
              client_ability_route(14, 2) == AbilityRoute::local &&
              client_ability_route(29, 1) == AbilityRoute::local &&
              client_ability_route(30, 2) == AbilityRoute::local && client_ability_route(31, 4) == AbilityRoute::local,
          "Mating Call, Glide and every other ability stay with the original; an attack type outside the attack "
          "category is no attack");
    ActorCommands c;
    auto a = c.bind(0x1000, 1), b = c.bind(0x2000, 2), npc = c.bind(0x3000, 0);
    check(a.live && b.live && a.id != b.id && npc.live, "independent owner bindings and native NPC identity");
    check(!c.bind(0x1000, 2).live && c.bind(0x1000, 1).id == a.id,
          "an existing actor cannot be claimed by another owner");
    ActorCommand attack{0, a.id, npc.id, 2, ActorVerb::attack};
    check(c.enqueue(attack) == ActorDecision::wrong_owner, "B cannot command A or spend through its native action");
    attack.owner = 1;
    check(c.enqueue(attack) == ActorDecision::accepted && attack.sequence == 1,
          "authorized command is queued with a unique sequence");
    ActorCommand queued;
    c.invalidate(0x1000);
    auto recreated = c.bind(0x1000, 1);
    check(recreated.id != a.id && c.pop(queued) && c.authorize(queued) == ActorDecision::unknown_actor,
          "destroy and address reuse reject a command already waiting in the queue");
    attack.actor = recreated.id;
    attack.target = b.id;
    check(c.enqueue(attack) == ActorDecision::accepted, "recreated actor uses its fresh identity");
    c.invalidate(0x2000);
    check(c.pop(queued) && c.authorize(queued) == ActorDecision::invalid_target,
          "target destruction between enqueue and dispatch rejects attack");
    attack.verb = ActorVerb::approach;
    check(c.enqueue(attack) == ActorDecision::invalid_target,
          "approach rejects a destroyed target before native path planning");
    attack.verb = ActorVerb::engage;
    check(c.enqueue(attack) == ActorDecision::invalid_target, "held attack rejects a destroyed target");
    attack.target = npc.id;
    attack.owner = 2;
    check(c.enqueue(attack) == ActorDecision::wrong_owner, "held attack cannot cross actor ownership");
    ActorCommand move{0, recreated.id, 0, 1, ActorVerb::move};
    check(c.enqueue(move) == ActorDecision::accepted, "movement has no target dependency");
    c.scene_exit();
    check(!c.pop(queued) && c.authorize(move) == ActorDecision::unknown_actor,
          "scene exit fences bindings and cancels queued native work");
    auto next = c.bind(0x1000, 1);
    check(next.epoch > recreated.epoch && next.id > recreated.id, "scene reload cannot reuse prior authority IDs");
    move.actor = next.id;
    bool filled = true;
    uint64_t last = 0;
    for (size_t i = 0; i < ActorCommands::queue_capacity; ++i) {
        filled &= c.enqueue(move) == ActorDecision::accepted;
    }
    check(filled && c.enqueue(move) == ActorDecision::full, "bounded queue rejects overload");
    bool ordered = true;
    while (c.pop(queued)) {
        ordered &= queued.sequence > last;
        last = queued.sequence;
    }
    check(ordered, "queued inputs preserve submission ordering");
    ActorCommands pickups;
    auto feeder = pickups.bind(0x4000, 1), corpse = pickups.bind(0x5000, 0);
    ActorCommand pickup{0, feeder.id, corpse.id, 2, ActorVerb::pickup};
    check(uint32_t(ActorVerb::pickup) == 7 && pickups.enqueue(pickup) == ActorDecision::wrong_owner,
          "pickup7 cannot command another owner's actor");
    pickup.owner = 1;
    pickup.target = 0;
    check(pickups.enqueue(pickup) == ActorDecision::invalid_target, "pickup requires an admitted local target");
    pickup.target = feeder.id;
    check(pickups.enqueue(pickup) == ActorDecision::invalid_target, "pickup rejects self target");
    pickup.target = corpse.id;
    check(pickups.enqueue(pickup) == ActorDecision::accepted, "pickup with valid owner and target queues once");
    pickups.invalidate(0x5000);
    auto replacement = pickups.bind(0x5000, 0);
    check(replacement.id != corpse.id && pickups.pop(queued) &&
              pickups.authorize(queued) == ActorDecision::invalid_target,
          "corpse destruction and same-address reuse fence an already queued pickup");
    pickup.target = replacement.id;
    check(pickups.enqueue(pickup) == ActorDecision::accepted,
          "fresh target incarnation accepts a new pickup intention");
    pickups.invalidate(0x4000);
    check(pickups.pop(queued) && pickups.authorize(queued) == ActorDecision::unknown_actor,
          "feeder destruction fences pickup before native dispatch");
    ActorCommands fenced;
    const auto owner = fenced.bind(0x6000, 1), target = fenced.bind(0x7000, 0);
    ActorCommand held{0, owner.id, target.id, 1, ActorVerb::engage};
    check(fenced.enqueue(held) == ActorDecision::accepted && held.input_epoch == fenced.input_epoch() &&
              fenced.pop(queued),
          "held native intention retains its input admission epoch");
    const auto old_epoch = held.input_epoch;
    ActorCommand waiting{0, owner.id, 0, 1, ActorVerb::jump};
    check(fenced.enqueue(waiting) == ActorDecision::accepted && fenced.pending_count() == 1,
          "second native command waits behind a previously admitted intention");
    const auto revoked = fenced.fence_inputs();
    check(revoked > old_epoch && !fenced.inputs_ready() && !fenced.pending_count() && !fenced.pop(waiting) &&
              fenced.find(owner.id).live && fenced.find(target.id).live,
          "input fence discards pending work while preserving original actor bindings");
    check(fenced.authorize(held) == ActorDecision::inputs_fenced &&
              fenced.authorize(queued) == ActorDecision::inputs_fenced,
          "input fence rejects commands already popped for dispatch and retained engage intentions");
    ActorCommand blocked{0, owner.id, 0, 1, ActorVerb::move};
    check(fenced.enqueue(blocked) == ActorDecision::inputs_fenced && !blocked.sequence,
          "late network input cannot enter a fenced command queue");
    check(fenced.fence_inputs() == revoked && !fenced.resume_inputs(old_epoch) && !fenced.inputs_ready(),
          "repeated fence is stable and stale resume cannot reactivate authority");
    check(fenced.resume_inputs(revoked) && fenced.inputs_ready() && !fenced.resume_inputs(revoked),
          "only the current explicit fence can resume once");
    check(fenced.authorize(held) == ActorDecision::inputs_fenced &&
              fenced.enqueue(held) == ActorDecision::inputs_fenced,
          "old retained intention cannot be reauthorized or requeued after resume");
    ActorCommand fresh{0, owner.id, 0, 1, ActorVerb::jump};
    check(fenced.enqueue(fresh) == ActorDecision::accepted && fresh.input_epoch == revoked && fenced.pop(waiting) &&
              fenced.authorize(waiting) == ActorDecision::accepted,
          "freshly admitted input resumes against unchanged native actor identity");
    const auto second_fence = fenced.fence_inputs();
    fenced.scene_exit();
    check(!fenced.inputs_ready() && !fenced.resume_inputs(revoked) && second_fence > revoked,
          "scene exit cannot silently reopen a revoked input lease");
    KeyboardSteeringState steering;
    ActorCommand walk{10, owner.id, 0, 1, ActorVerb::steer};
    ActorCommand release{11, owner.id, 0, 1, ActorVerb::steer_stop};
    ActorCommand jump{12, owner.id, 0, 1, ActorVerb::jump};
    steering.observe(release);
    check(!steering.release.sequence, "release without a keyboard walk has no native stop permission");
    steering.observe(walk);
    steering.observe(jump);
    steering.observe(release);
    check(!steering.active && steering.release.sequence == 11,
          "release after a jump retains only its original walk stop request");
    steering.observe(jump);
    check(steering.release.sequence == 11,
          "a pending release survives a later jump until the native adapter observes landing");
    steering.observe(walk);
    check(steering.active && !steering.release.sequence, "new steering supersedes a release waiting for landing");
    for (auto verb : {ActorVerb::combat, ActorVerb::social, ActorVerb::pickup, ActorVerb::approach, ActorVerb::stop,
                      ActorVerb::collect}) {
        steering.observe(walk);
        ActorCommand interaction{13, owner.id, target.id, 1, verb};
        steering.observe(interaction);
        steering.observe(release);
        check(!steering.active && !steering.release.sequence, "late release cannot cancel a newer native interaction");
    }
    steering.observe(walk);
    steering.observe(release);
    steering.clear();
    check(!steering.active && !steering.release.sequence, "input and scene fences discard deferred release work");
    ActorCommands resources;
    auto collector = resources.bind(100, 1);
    ActorCommand collect{0, collector.id, 123, 1, ActorVerb::collect};
    check(resources.enqueue(collect) == ActorDecision::invalid_target,
          "collect requires a resource incarnation, not an Animal binding");
    collect.target_generation = 9;
    collect.target_native_id = 9770;
    check(resources.enqueue(collect) == ActorDecision::accepted && resources.pop(waiting) &&
              waiting.target_generation == 9 && waiting.target_native_id == 9770,
          "resource values survive the owned actor queue without binding a fake Animal");
    resources.fence_inputs();
    check(resources.authorize(waiting) == ActorDecision::inputs_fenced,
          "popped collect obeys the same authority input fence");
    return failures ? 1 : 0;
}
