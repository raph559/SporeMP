#include "replica_policy.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace sporemp::replica;
int native_replica_abi_checks();
int native_branch_abi_checks();
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
const Fence fence{{11, 22}, 3};
const Entity entity{100, 2};
Update update(uint64_t sequence = 1) { return {fence, entity, 1, sequence, {7, 8, 9, 10}}; }
struct Sink {
    Policy* policy;
    Registry* registry;
    unsigned calls = 0;
    uint64_t local = 0;
    Vitals state{};
    int behavior = 0;
};
bool receive(void* context, uint64_t local, const Vitals& value) {
    auto& sink = *static_cast<Sink*>(context);
    ++sink.calls; sink.local = local; sink.state = value;
    check(sink.policy->applying(), "application scope entered");
    check(sink.policy->allow(Mutation::apply_state), "absolute-state writes admitted inside scope");
    check(sink.policy->allow(Mutation::presentation), "presentation retained during apply");
    for (size_t i = 0; i < static_cast<size_t>(Mutation::presentation); ++i)
        check(!sink.policy->allow(static_cast<Mutation>(i)), "native gameplay cannot run as an apply side effect");
    check(!sink.policy->may_publish(), "replica apply cannot emit authority outcomes");
    if (sink.behavior == 1) {
        check(sink.registry->apply(update(2), receive, context) == Decision::busy, "nested replica application denied");
        check(sink.registry->begin(fence, 2) == Decision::busy, "callback cannot replace baseline");
        check(sink.registry->bind({9, 1}, 44) == Decision::busy, "callback cannot replace registry slots");
    } else if (sink.behavior == 2) return false;
    else if (sink.behavior == 3) throw std::runtime_error("partial native adapter failure fixture");
    else if (sink.behavior == 4) sink.registry->invalidate(local);
    else if (sink.behavior == 5) sink.registry->disconnect();
    return true;
}
void roles() {
    for (Role role : {Role::single_player, Role::authority, Role::replica}) {
        Policy policy(role);
        for (size_t i = 0; i < static_cast<size_t>(Mutation::count); ++i) {
            const auto domain = static_cast<Mutation>(i);
            check(policy.allow(domain) == (domain == Mutation::presentation ||
                (role != Role::replica && domain != Mutation::apply_state)), "role mutation boundary");
        }
        check(policy.may_publish() == (role == Role::authority), "only authority publishes native outcomes");
        check(!policy.allow(Mutation::count), "unknown domain denied");
        Registry registry(policy);
        if (role != Role::replica) {
            check(registry.begin(fence, 1) == Decision::wrong_role, "immutable process role");
            check(registry.apply(update(), receive, nullptr) == Decision::wrong_role, "no application outside replica role");
        }
    }
}
void lifecycle() {
    Policy policy(Role::replica); Registry registry(policy); Sink sink{&policy, &registry};
    check(registry.apply(update(), receive, &sink) == Decision::disconnected, "fresh baseline required");
    check(registry.begin({{}, 1}, 1) == Decision::invalid, "zero worker rejected");
    check(registry.begin({{1, 0}, 0}, 1) == Decision::invalid, "zero scene rejected");
    check(registry.begin(fence, 0) == Decision::invalid, "zero admission token rejected");
    check(registry.begin(fence, 1) == Decision::accepted, "source fence admitted");
    check(registry.bind(entity, 123) == Decision::accepted, "existing native diagnostic identity adopted");
    check(registry.bind(entity, 124) == Decision::stale, "one local object per remote identity");
    check(registry.bind({101, 1}, 123) == Decision::stale, "one remote identity per local object");
    check(registry.bind({0, 1}, 125) == Decision::invalid, "zero entity denied");
    check(registry.bind({101, 0}, 125) == Decision::invalid, "zero entity generation denied");
    check(registry.bind({101, 1}, 0) == Decision::invalid, "zero local identity denied");
    check(registry.apply(update(), receive, &sink) == Decision::accepted, "worker state applied");
    check(sink.calls == 1 && sink.local == 123 && sink.state.dna == 10, "values and correct local identity delivered");
    check(!policy.applying() && !policy.allow(Mutation::apply_state), "application permission ends with callback");
    check(registry.apply(update(), receive, &sink) == Decision::stale, "duplicate reward-state update denied");
    auto next = update(2); next.fence.worker[1]++;
    check(registry.apply(next, receive, &sink) == Decision::stale, "different worker cannot update baseline");
    next = update(2); next.fence.scene++;
    check(registry.apply(next, receive, &sink) == Decision::stale, "different scene cannot update baseline");
    next = update(2); next.entity.generation++;
    check(registry.apply(next, receive, &sink) == Decision::missing, "wrong object incarnation rejected");
    next = update(2); next.baseline++;
    check(registry.apply(next, receive, &sink) == Decision::stale, "old admission token cannot cross reconnect");
    check(sink.calls == 1, "rejected envelopes cannot enter native adapter");
    check(registry.apply(update(3), receive, &sink) == Decision::accepted, "snapshot gaps allowed");
    check(registry.apply(update(2), receive, &sink) == Decision::stale, "out of order snapshot denied");
    check(registry.invalidate(123) == Decision::accepted, "destruction invalidates mapping before native release");
    check(registry.apply(update(4), receive, &sink) == Decision::missing, "destroyed object not resurrected");
    check(registry.bind({100, 3}, 124) == Decision::stale, "incarnation replacement needs fresh baseline");
    check(registry.bind({101, 1}, 123) == Decision::stale, "local ID reuse cannot escape tombstone");
    registry.disconnect();
    check(policy.role() == Role::replica && !policy.may_publish(), "disconnect never promotes role");
    check(!policy.allow(Mutation::reward) && !policy.allow(Mutation::save), "disconnect retains irreversible-mutation denial");
    check(registry.bind({101, 1}, 124) == Decision::disconnected, "no objects admitted while disconnected");
    check(registry.apply(update(4), receive, &sink) == Decision::disconnected, "no offline update queue");
    check(registry.begin(fence, 1) == Decision::stale, "previous baseline cannot be reused");
    check(registry.begin(fence, 2) == Decision::accepted, "fresh baseline reconnects");
    check(registry.bind(entity, 126) == Decision::accepted, "fresh native ID must be adopted again");
    check(registry.apply(update(4), receive, &sink) == Decision::stale, "pre-disconnect data stays rejected after reconnect");
    next = update(4); next.baseline = 2;
    check(registry.apply(next, receive, &sink) == Decision::accepted, "fresh baseline update applies");
    check(policy.counters().applied == 3 && policy.counters().invalidated == 1, "audit separates applies and object invalidations");
}
void malformed() {
    Policy policy(Role::replica); Registry registry(policy); Sink sink{&policy, &registry};
    registry.begin(fence, 1); registry.bind(entity, 123);
    for (float bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(), -1.0f}) {
        for (int i = 0; i < 4; ++i) {
            auto next = update();
            switch (i) { case 0: next.state.health=bad; break; case 1: next.state.energy=bad; break;
                case 2: next.state.hunger=bad; break; default: next.state.dna=bad; }
            check(registry.apply(next, receive, &sink) == Decision::invalid, "nonfinite or negative native state denied");
        }
    }
    auto next=update(); next.state.health=0;
    check(registry.apply(next, receive, &sink) == Decision::invalid, "death cannot be synthesized with a health setter");
    check(registry.apply(update(0), receive, &sink) == Decision::invalid, "zero sequence denied");
    check(registry.apply(update(), nullptr, &sink) == Decision::invalid, "missing native callback rejected");
    check(sink.calls==0 && policy.counters().applied==0, "malformed updates have no partial native writes");
    check(registry.apply(update(), receive, &sink)==Decision::accepted, "invalid frames do not consume a valid sequence");
}
void callbacks() {
    for(int behavior=1; behavior<=5; ++behavior) {
        Policy policy(Role::replica); Registry registry(policy); Sink sink{&policy, &registry};
        registry.begin(fence, 1); registry.bind(entity, 123); sink.behavior=behavior;
        check(registry.apply(update(),receive,&sink)==(behavior==1?Decision::accepted:Decision::invalid), "callback boundary result");
        check(!policy.applying(), "guard unwinds after exceptions, failures and reentrancy");
        check(sink.calls==1, "native callback runs at most once");
        if(behavior!=1) {
            check(registry.link()==Link::disconnected, "partial application quarantines baseline");
            check(registry.apply(update(),receive,&sink)==Decision::disconnected, "partial apply never retried automatically");
        }
    }
}
void bounded() {
    Policy policy(Role::replica); Registry registry(policy);
    registry.begin(fence, 1);
    for(size_t i=0;i<Registry::capacity;++i)
        check(registry.bind({i+1,1},i+100)==Decision::accepted,"bounded registry slot");
    check(registry.bind({1000,1},1000)==Decision::full,"registry overflow denied");
    registry.disconnect();
    check(policy.counters().invalidated==Registry::capacity,"disconnect invalidates every live object");
}
void scene_projection() {
    const auto callback = [](void* context) -> bool {
        auto& policy = *static_cast<Policy*>(context);
        check(policy.applying() && policy.allow(Mutation::apply_state), "scene projection enters write scope");
        check(!policy.allow(Mutation::spawn) && !policy.allow(Mutation::death), "generic scope grants no lifecycle privilege");
        check(!policy.allow(Mutation::reward) && !policy.allow(Mutation::save), "scene writes cannot grant rewards or save");
        check(!policy.project(nullptr, nullptr), "recursive projection denied");
        return true;
    };
    Policy authority(Role::authority), replica(Role::replica);
    check(!authority.project(callback, &authority), "authority cannot apply client projection");
    check(replica.project(callback, &replica), "valid scoped scene projection succeeds");
    check(!replica.applying() && !replica.allow(Mutation::apply_state), "projection privilege expires");
    check(!replica.project([](void*) -> bool { throw std::runtime_error("native callback failed"); }, nullptr), "projection failure reported");
    check(!replica.applying(), "exception cannot retain projection privilege");
}
}
int main() {
    try { roles(); lifecycle(); malformed(); callbacks(); bounded(); scene_projection(); if(native_replica_abi_checks() || native_branch_abi_checks()) return 1; }
    catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what());return 1; }
    std::printf("%d HOST/FIXTURE replica assertions passed; original SPORE NOT RUN.\n",checks);
    return 0;
}
