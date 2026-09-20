#include "observation.h"
#include "detour_transaction.h"
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <string>

namespace {
int failures = 0, intercepted = 0;
sporemp::ObservationTrace trace;
void check(bool value, const char* name) {
    std::printf("%s: %s (HOST/FIXTURE ONLY)\n", value ? "PASS" : "FAIL", name);
    if (!value) ++failures;
}
struct NativeFixture {
    int calls = 0;
    float total = 0;
    __declspec(noinline) bool invoke(int value, float delta) {
        ++calls; total += delta;
        // Force a new function entry: MSVC may otherwise turn tail recursion into an internal loop.
        bool (NativeFixture::*volatile recurse)(int, float) = &NativeFixture::invoke;
        if (value == 3) return (this->*recurse)(1, delta + 0.5f);
        return value > 0;
    }
};
using Original = bool (__thiscall*)(NativeFixture*, int, float);
Original original = nullptr;
bool __fastcall replacement(NativeFixture* self, void*, int value, float delta) {
    ++intercepted;
    const bool record = trace.on_engine_thread();
    sporemp::Observation event{sporemp::ObservationKind::jump_enter};
    if (record) { event.action = trace.next_action(); event.argument = value; trace.emit(event); }
    const bool result = original(self, value, delta);
    if (record) { event.kind = sporemp::ObservationKind::jump_return; event.result = result ? 1 : 0; trace.emit(event); }
    return result;
}
}
int wmain(int argc, wchar_t** argv) {
    sporemp::EntityTable table;
    check(table.observe(0).id == 0, "null entity cannot get an identity");
    auto a = table.observe(0x1000), b = table.observe(0x1100);
    check(a.id != b.id && table.observe(a.key).id == a.id, "live objects have distinct stable IDs");
    check(table.invalidate(a.key).id == a.id && !table.live(a), "invalidation fences outstanding tokens before destruction");
    check(table.observe(a.key).id == 0, "reentrant observation cannot resurrect a destroying object");
    table.finish_destroy(a.key);
    auto recreated = table.observe(a.key);
    check(recreated.id > b.id && !table.live(a) && table.live(recreated), "address reuse gets a fresh ID and cannot revive a stale token");
    table.clear();
    check(!table.live(recreated) && !table.live(b) && table.observe(a.key).id > recreated.id, "scene exit invalidates all handles without resetting the ID sequence");
    table.clear();
    for (size_t i = 0; i < sporemp::EntityTable::capacity; ++i) table.observe((i + 1) * 16);
    check(table.observe(0x100000).id == 0, "entity table has a hard capacity instead of unbounded allocation");
    table.invalidate(16); table.finish_destroy(16);
    check(table.observe(0x100000).id != 0, "retired slots can be reused after saturation");

    wchar_t temp[32768]{}, directory[32768]{};
    bool preserve = argc == 3 && wcscmp(argv[1], L"--output") == 0;
    if (preserve) wcscpy_s(directory, argv[2]);
    else {
        GetTempPathW(_countof(temp), temp);
        swprintf_s(directory, L"%lsSporeMP-M02-host-%lu-%llu", temp, GetCurrentProcessId(), GetTickCount64());
        if (!CreateDirectoryW(directory, nullptr)) return 2;
    }
    if (!trace.open(directory, true)) return 2;
    sporemp::Observation state{sporemp::ObservationKind::avatar_state};
    state.has_state = true; state.health = std::numeric_limits<float>::quiet_NaN();
    state.energy = std::numeric_limits<float>::infinity(); state.hunger = -5.25f;
    trace.emit(state);
    trace.flush();

    auto member = &NativeFixture::invoke;
    static_assert(sizeof(member) == sizeof(original), "Win32 fixture uses the pinned thiscall representation");
    memcpy(&original, &member, sizeof(original));
    sporemp::NativeHook hook{reinterpret_cast<void**>(&original), reinterpret_cast<void*>(replacement)};
    check(sporemp::change_hooks(nullptr, 1, true) == ERROR_INVALID_PARAMETER, "invalid hook collection rejected");
    sporemp::NativeHook invalid[] = {hook, {nullptr, nullptr}};
    check(sporemp::change_hooks(invalid, 2, true) == ERROR_INVALID_PARAMETER, "invalid batch is rejected before any patch is installed");
    NativeFixture baseline;
    check(baseline.invoke(0, 1.25f) == false && baseline.invoke(3, 2.f) == true && baseline.calls == 3,
          "unhooked thiscall baseline includes false, true and nested native invocations");
    check(intercepted == 0, "rejected installation leaves original code untouched");

    HANDLE run_thread = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    NativeFixture foreign;
    std::thread worker([&]() { WaitForSingleObject(run_thread, INFINITE); foreign.invoke(1, 7.5f); });
    LONG status = sporemp::change_hooks(&hook, 1, true);
    check(status == NO_ERROR, "actual Detours transaction attaches with another process thread enrolled");
    if (status == NO_ERROR) {
        NativeFixture observed;
        check(!observed.invoke(0, 1.25f) && observed.invoke(3, 2.f), "detoured false and true return values are preserved");
        check(observed.calls == baseline.calls && observed.total == baseline.total && intercepted == 3,
              "this pointer, integer/float arguments and nested original-call count match the baseline exactly");
    }
    SetEvent(run_thread); worker.join(); CloseHandle(run_thread);
    check(foreign.calls == 1 && foreign.total == 7.5f, "foreign-thread original still executes once with untouched arguments");
    if (status == NO_ERROR) {
        check(sporemp::change_hooks(&hook, 1, false) == NO_ERROR, "actual transaction detaches outside loader lock");
        int previous = intercepted; NativeFixture detached;
        check(detached.invoke(1, 2.f) && intercepted == previous, "detachment restores direct original behavior");
    }
    trace.close(); trace.close();
    sporemp::ObservationTrace duplicate;
    check(!duplicate.open(directory, true), "trace path uses exclusive creation and preserves earlier evidence");
    wchar_t path[32768]{};
    swprintf_s(path, L"%ls\\gameplay-%lu.jsonl", directory, GetCurrentProcessId());
    FILE* file = nullptr; _wfopen_s(&file, path, L"rb");
    std::string content; char bytes[4096]; size_t n;
    if (file) { while ((n = fread(bytes, 1, sizeof(bytes), file)) != 0) content.append(bytes, n); fclose(file); }
    check(content.find("\"evidence_class\":\"HOST_FIXTURE\"") != std::string::npos &&
          content.find("HOST_FIXTURE_NOT_GAME") != std::string::npos, "host-generated records cannot be mistaken for game identity");
    check(content.find("\"health\":null,\"energy\":null,\"hunger\":-5.25") != std::string::npos,
          "non-finite native scalar samples serialize as JSON null");
    check(content.find("\"event\":\"trace_stop\"") != std::string::npos &&
          content.find("\"foreign_callbacks\":1") != std::string::npos, "terminal record reports unobserved foreign-thread callback");
    if (preserve) std::wprintf(L"Host trace retained: %ls\n", path);
    else { DeleteFileW(path); RemoveDirectoryW(directory); }
    std::puts("SPORE and SDK core were not loaded. Native behavior, landing and lifetime coverage: NOT RUN.");
    return failures == 0 ? 0 : 1;
}
