#include "native_hatch_abi.h"
#include "stack_pointer.h"
#include "native_respawn_herd.h"
#include "detour_transaction.h"
#include <cstdio>
#include <iterator>

namespace {
using Eligibility = sporemp::NativeHatchAbi::Eligibility;
Eligibility original = nullptr;
unsigned original_calls = 0, hook_calls = 0;
int received = 0;

__declspec(noinline) uint32_t __cdecl eligibility_fixture(int index) {
    ++original_calls;
    received = index;
    return static_cast<uint32_t>(index) ^ 0xa5f00123u;
}

uint32_t __cdecl eligibility_hook(int index) {
    ++hook_calls;
    return original(index);
}
} // namespace

int native_hatch_abi_checks() {
    int failures = 0;
    const auto admission = [&](bool result, const char* message) {
        std::printf("%s: %s (HOST ADMISSION; SPORE NOT LOADED)\n", result ? "PASS" : "FAIL", message);
        if (!result) {
            ++failures;
        }
    };
    using Member = sporemp::NativeRespawnMember;
    const auto admitted = [](const std::vector<Member>& members) {
        return sporemp::native_respawn_herd_admissible(12559, 232, 12558, members);
    };
    const Member owner{12559, 12558, true}, mother{71, 12558, true}, companion{72, 12558, true};
    admission(admitted({owner}), "initial single-owner herd remains admitted");
    admission(admitted({mother, owner, companion}) && admitted({companion, mother, owner}),
              "populated original herd admits B in any position without dropping members");
    admission(!admitted({}) && !admitted({mother, companion}), "empty herd or absent owner is rejected");
    admission(!admitted({owner, owner}) && !admitted({mother, owner, mother}),
              "duplicate owner and duplicate NPC identities are rejected");
    admission(!admitted({owner, {232, 12558, true}}), "peer player is rejected even with matching herd ID");
    admission(!admitted({owner, {71, 9, true}}), "member from another herd is rejected");
    admission(!admitted({owner, {71, 12558, false}}) && !admitted({owner, {}}),
              "retired and unresolved native members are rejected");
    admission(!sporemp::native_respawn_herd_admissible(12559, 12559, 12558, {owner}) &&
                  !sporemp::native_respawn_herd_admissible(12559, UINT32_MAX, 12558, {owner}) &&
                  !sporemp::native_respawn_herd_admissible(12559, 232, UINT32_MAX, {owner}),
              "overlapping or unresolved owner and herd identities are rejected");
    const auto check = [&](bool result, const char* message) {
        std::printf("%s: %s (HOST ABI FIXTURE; SPORE NOT LOADED)\n", result ? "PASS" : "FAIL", message);
        if (!result) {
            ++failures;
        }
    };
    original = &eligibility_fixture;
    sporemp::NativeHook hooks[] = {{reinterpret_cast<void**>(&original), reinterpret_cast<void*>(eligibility_hook)}};
    const auto attached = sporemp::change_hooks(hooks, std::size(hooks), true);
    check(attached == NO_ERROR, "hatch eligibility cdecl hook attaches");
    if (attached != NO_ERROR) {
        return failures;
    }
    Eligibility volatile call = &eligibility_fixture;
    check(call(-1) == (UINT32_MAX ^ 0xa5f00123u) && received == -1,
          "hatch eligibility retains signed sentinel and full EAX result");
    uintptr_t before = 0, after = 0;
    bool exact = true;
    SPOREMP_READ_STACK_POINTER(before);
    for (int i = 0; i < 4096; ++i) {
        exact = exact && call(i) == (static_cast<uint32_t>(i) ^ 0xa5f00123u);
    }
    SPOREMP_READ_STACK_POINTER(after);
    check(exact && before == after && received == 4095 && original_calls == 4097 && hook_calls == 4097,
          "4096 hatch eligibility calls preserve argument, caller stack, result and exact counts");
    check(sporemp::change_hooks(hooks, std::size(hooks), false) == NO_ERROR, "hatch eligibility hook detaches");
    const auto previous_hooks = hook_calls;
    check(call(88) == (88u ^ 0xa5f00123u) && received == 88 && hook_calls == previous_hooks,
          "detached eligibility restores original out-of-range path");
    return failures;
}
