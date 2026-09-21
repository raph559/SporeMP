// SPDX-License-Identifier: GPL-3.0-or-later
// HOST fixtures for the pinned SDK wrappers, without loading SPORE or its SDK DLL.
#include <Spore/CppRevEng.h>
#include <cstdio>
#include <cstring>

static_assert(sizeof(void*) == 4, "The pinned SDK wrappers require Win32.");
static_assert(DETOURS_VERSION == 0x4c0c1, "SDK wrappers must compile against Detours 4.0.1.");

namespace {
int failures = 0;
int static_original_calls = 0;
int static_hook_calls = 0;
int member_hook_calls = 0;

void check(bool passed, const char* description) {
    std::printf("%s: %s (HOST/FIXTURE ONLY)\n", passed ? "PASS" : "FAIL", description);
    if (!passed) ++failures;
}

__declspec(noinline) int static_fixture(int first, int second) {
    ++static_original_calls;
    return first * 17 - second * 3;
}

static_detour(StaticHook, int(int, int)) {
    int detoured(int first, int second) {
        ++static_hook_calls;
        return original_function(first, second) + 101;
    }
};

struct MemberFixture {
    int calls = 0;
    int last_value = 0;
    float last_delta = 0;

    __declspec(noinline) float invoke(int value, float delta) {
        ++calls;
        last_value = value;
        last_delta = delta;
        return static_cast<float>(value) + delta;
    }
};

member_detour(MemberHook, MemberFixture, float(int, float)) {
    float detoured(int value, float delta) {
        ++member_hook_calls;
        return original_function(this, value, delta) + 0.5f;
    }
};

bool begin_transaction() {
    const LONG begin = DetourTransactionBegin();
    check(begin == NO_ERROR, "transaction starts for real SDK wrapper hooks");
    if (begin != NO_ERROR) return false;
    const LONG update = DetourUpdateThread(GetCurrentThread());
    check(update == NO_ERROR, "current HOST thread is enrolled");
    if (update == NO_ERROR) return true;
    DetourTransactionAbort();
    return false;
}
}

int main() {
    using StaticCall = int (*)(int, int);
    StaticCall volatile static_call = &static_fixture;
    auto member = &MemberFixture::invoke;
    MemberHook::detour_pointer member_address = nullptr;
    static_assert(sizeof(member) == sizeof(member_address), "Win32 member-pointer representation is required.");
    std::memcpy(&member_address, &member, sizeof(member_address));
    // Indirect volatile calls keep Release optimization from bypassing the hook entry.
    MemberHook::detour_pointer volatile member_call = member_address;
    MemberFixture first;
    MemberFixture second;

    check(static_call(5, -7) == 106 && static_original_calls == 1 && static_hook_calls == 0,
          "static HOST baseline runs unchanged");
    check(member_call(&first, 9, 0.25f) == 9.25f && first.calls == 1 &&
              first.last_value == 9 && first.last_delta == 0.25f && member_hook_calls == 0,
          "member HOST baseline preserves receiver, integer, float and x87 return");

    if (!begin_transaction()) return 1;
    const LONG attach_static = StaticHook::attach(reinterpret_cast<UINT>(&static_fixture));
    const LONG attach_member = MemberHook::attach(reinterpret_cast<UINT>(member_address));
    check(attach_static == NO_ERROR && attach_member == NO_ERROR,
          "actual pinned SDK static_detour and member_detour wrappers attach");
    if (attach_static != NO_ERROR || attach_member != NO_ERROR) {
        DetourTransactionAbort();
        return 1;
    }
    const LONG committed = CommitDetours();
    check(committed == NO_ERROR, "actual SDK CommitDetours commits both replacements");
    if (committed != NO_ERROR) return 1;

    check(static_call(5, -7) == 207 && static_original_calls == 2 && static_hook_calls == 1,
          "SDK static trampoline preserves arguments and calls the original exactly once");
    check(static_call(-2, 4) == 55 && static_original_calls == 3 && static_hook_calls == 2,
          "SDK static trampoline preserves signed arguments and return values");
    check(member_call(&first, -3, -0.75f) == -3.25f && first.calls == 2 &&
              first.last_value == -3 && first.last_delta == -0.75f && member_hook_calls == 1,
          "SDK thiscall/fastcall trampoline preserves receiver, arguments and x87 return");
    check(member_call(&second, 6, 1.25f) == 7.75f && second.calls == 1 && first.calls == 2 &&
              second.last_value == 6 && second.last_delta == 1.25f && member_hook_calls == 2,
          "SDK member trampoline keeps two distinct receivers independent");

    if (!begin_transaction()) return 1;
    const LONG detach_static = StaticHook::detach();
    const LONG detach_member = MemberHook::detach();
    check(detach_static == NO_ERROR && detach_member == NO_ERROR,
          "actual pinned SDK wrappers detach their original trampolines");
    if (detach_static != NO_ERROR || detach_member != NO_ERROR) {
        DetourTransactionAbort();
        return 1;
    }
    const LONG detached = CommitDetours();
    check(detached == NO_ERROR, "SDK CommitDetours commits both detachments");
    if (detached != NO_ERROR) return 1;

    check(static_call(5, -7) == 106 && static_original_calls == 4 && static_hook_calls == 2,
          "static detachment restores unhooked original behavior");
    check(member_call(&second, -4, 0.25f) == -3.75f && second.calls == 2 &&
              second.last_value == -4 && second.last_delta == 0.25f && member_hook_calls == 2,
          "member detachment restores unhooked original behavior");
    std::puts("SPORE and SporeModAPI.dll were not loaded. Native gameplay acceptance: NOT RUN.");
    return failures == 0 ? 0 : 1;
}
