#include "native_global_admission.h"
#include <cstdio>
#include <limits>

using namespace sporemp;

namespace {
NativeGlobalAdmissionContext context() {
    NativeGlobalAdmissionContext c;
    c.role = NativeGlobalAdmissionRole::authority;
    c.universe[0] = 11;
    c.checkpoint[0] = 22;
    c.location = {1, 2, 3, 4};
    c.checkpoint_revision = 14;
    c.worker = 100;
    c.generation = 10;
    c.process_id = 123;
    c.process_created = 456;
    return c;
}

NativeGlobalAdmissionGrant grant(const NativeGlobalAdmissionContext& c, bool owner) {
    NativeGlobalAdmissionGrant g;
    g.context = c;
    g.nonce = 1;
    g.sent_at_ms = 1000;
    g.ttl_ms = 10000;
    for (auto& lease : g.owners) {
        lease = {owner ? c.worker : 200, owner ? c.generation : 20};
    }
    g.producer_mask = owner ? 15u : 0u;
    return g;
}
} // namespace

int main() {
    unsigned failed = 0, checked = 0;
    const auto check = [&](bool value, const char* label) {
        ++checked;
        if (!value) {
            ++failed;
            std::printf("FAIL %s\n", label);
        }
    };
    const auto c = context();
    NativeGlobalAdmission off;
    check(off.permit(0, false) == NativeGlobalPermit::off, "unmanaged observation remains passive");
    auto g = grant(c, false);
    check(!off.install(g, 1001, true), "grant cannot arm unmanaged native context");
    check(off.arm(c) && !off.arm(c), "context arms exactly once");
    check(off.permit(1001, true) == NativeGlobalPermit::denied, "armed before authenticated response denies");
    check(!off.install(g, 1001, false), "dead preload transport cannot install grant");
    check(off.install(g, 1001, true), "authenticated nonowner grant installs");
    check(off.permit(1001, true) == NativeGlobalPermit::reader, "nonowner gets reader admission");
    check(off.permit(1001, false) == NativeGlobalPermit::denied, "synchronous callback sees transport loss");
    check(off.permit(999, true) == NativeGlobalPermit::denied, "backward local clock rejected");
    check(off.permit(10999, true) == NativeGlobalPermit::reader &&
              off.permit(11000, true) == NativeGlobalPermit::denied,
          "expiry boundary exact");
    check(!off.install(g, 1002, true), "same nonce cannot renew deadline");
    g.nonce = 2;
    g.sent_at_ms = 2000;
    check(off.install(g, 2001, true) && off.deadline() == 12000, "renewal uses request send time");
    g.nonce = 3;
    g.sent_at_ms = 12000;
    check(!off.install(g, 12000, true), "expired grant cannot be revived in same process");
    off.revoke();
    check(off.permit(2002, true) == NativeGlobalPermit::denied, "revoked grant never permits");
    g.sent_at_ms = 2003;
    check(!off.install(g, 2004, true), "revocation cannot be undone by replay/new nonce");
    NativeGlobalAdmission owner;
    check(owner.arm(c), "owner context valid");
    auto owned = grant(c, true);
    check(owner.install(owned, 1001, true) && owner.permit(1001, true) == NativeGlobalPermit::owner,
          "all four owned systems permit original producer");
    auto moved = owned;
    moved.nonce = 2;
    moved.owners.fill({200, 20});
    moved.producer_mask = 0;
    check(!owner.install(moved, 1002, true), "owner migration needs fresh admission");
    NativeGlobalAdmission following;
    check(following.arm(c), "independent reader context");
    auto replacement = grant(c, false);
    check(following.install(replacement, 1001, true), "initial external owner");
    replacement.nonce = 2;
    replacement.sent_at_ms = 2000;
    replacement.owners.fill({200, 21});
    check(following.install(replacement, 2001, true) && following.permit(2001, true) == NativeGlobalPermit::reader,
          "reader follows higher generation of same owner without gaining production");
    replacement.nonce = 3;
    replacement.owners.fill({200, 20});
    check(!following.install(replacement, 2002, true), "reader rejects rollback to dead owner generation");
    replacement.owners.fill({201, 22});
    check(!following.install(replacement, 2002, true), "different global owner needs new native admission");
    replacement.owners.fill({200, 22});
    replacement.producer_mask = 15;
    check(!following.install(replacement, 2002, true), "replacement never promotes a reader to owner");
    const auto rejected = [&](NativeGlobalAdmissionGrant value, const char* label, uint64_t now = 1001) {
        NativeGlobalAdmission a;
        check(a.arm(c), "fresh fixture context");
        check(!a.install(value, now, true), label);
    };
    g = grant(c, false);
    g.context.checkpoint[1] = 1;
    rejected(g, "wrong selected checkpoint digest");
    g = grant(c, false);
    g.context.universe[1] = 1;
    rejected(g, "wrong universe");
    g = grant(c, false);
    ++g.context.checkpoint_revision;
    rejected(g, "wrong checkpoint revision");
    g = grant(c, false);
    ++g.context.generation;
    rejected(g, "wrong worker generation");
    g = grant(c, false);
    ++g.context.worker;
    rejected(g, "wrong worker");
    g = grant(c, false);
    ++g.context.location[2];
    rejected(g, "wrong location");
    g = grant(c, false);
    ++g.context.process_id;
    rejected(g, "wrong native PID");
    g = grant(c, false);
    ++g.context.process_created;
    rejected(g, "PID reuse creation mismatch");
    g = grant(c, false);
    g.producer_mask = 15;
    rejected(g, "nonowner cannot claim producer mask");
    g = grant(c, true);
    g.producer_mask = 0;
    rejected(g, "owner mask must cover exact four leases");
    g = grant(c, true);
    g.producer_mask = 3;
    rejected(g, "partial producer masks unsupported");
    g = grant(c, false);
    g.owners[3].generation++;
    rejected(g, "split global lease tuples unsupported");
    g = grant(c, false);
    g.owners.fill({c.worker, c.generation + 1});
    rejected(g, "stale generation cannot become a reader of itself");
    g = grant(c, false);
    g.ttl_ms = 0;
    rejected(g, "zero TTL");
    g = grant(c, false);
    g.ttl_ms = 30001;
    rejected(g, "oversized TTL");
    g = grant(c, false);
    g.sent_at_ms = 1002;
    rejected(g, "future request-send clock");
    g = grant(c, false);
    rejected(g, "delayed response cannot extend expired send deadline", 11000);
    g = grant(c, false);
    g.sent_at_ms = std::numeric_limits<uint64_t>::max() - 2;
    rejected(g, "deadline overflow", std::numeric_limits<uint64_t>::max() - 1);
    check(NativeGlobalAdmission::spice_noop(false, {7, 0, 0}, {}), "assigned instance preserves original no-op");
    check(NativeGlobalAdmission::spice_noop(false, {0, 8, 0}, {}), "native compares all three key words");
    check(NativeGlobalAdmission::spice_noop(false, {0, 0, 9}, {}), "assigned group matches original branch");
    check(!NativeGlobalAdmission::spice_noop(false, {}, {}), "unassigned record would generate");
    check(!NativeGlobalAdmission::spice_noop(true, {7, 0, 0}, {}), "force argument would reassign");
    check(!NativeGlobalAdmission::spice_noop(false, {7, 0, 0}, {1, 0, 0}), "changed loaded native default rejected");
    auto reader_context = c;
    reader_context.role = NativeGlobalAdmissionRole::reader;
    auto reader_grant = grant(reader_context, true);
    NativeGlobalAdmission reader;
    check(reader.arm(reader_context), "supervised reader identity arms");
    check(!reader.install(reader_grant, 1001, true), "reader at global owner location cannot get owner mask");
    reader_grant.producer_mask = 0;
    check(reader.install(reader_grant, 1001, true) && reader.permit(1001, true) == NativeGlobalPermit::reader,
          "explicit reader role wins over matching global owner worker tuple");
    reader_grant.context.role = NativeGlobalAdmissionRole::authority;
    reader_grant.nonce = 2;
    check(!reader.install(reader_grant, 1002, true), "authenticated role cannot change in place");
    reader_context.worker = reader_context.generation = 0;
    NativeGlobalAdmission unleased_reader;
    check(unleased_reader.arm(reader_context), "reader without a worker lease cannot claim ownership");
    reader_grant = grant(reader_context, false);
    check(unleased_reader.install(reader_grant, 1001, true),
          "zero worker reader accepts only separate global owner tuple");
    std::printf("%u checks, %u failures (HOST admission policy; original SPORE NOT RUN)\n", checked, failed);
    return failed ? 1 : 0;
}
