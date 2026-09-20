#include "native_persistence_abi.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
// Compiled HOST receivers verify calling conventions only. They perform no
// persistence, load no SDK/game code and make no claim about native lifetimes.
struct Manager {
    Manager* receiver = nullptr;
    const wchar_t* saved_name = nullptr;
    const wchar_t* loaded_name = nullptr;
    bool silent = false;
    unsigned saves = 0, loads = 0, resets = 0;
    __declspec(noinline) bool save(const wchar_t* name, bool value) {
        receiver = this; saved_name = name; silent = value; ++saves;
        return name != nullptr && !value;
    }
    __declspec(noinline) void load(const wchar_t* name) {
        receiver = this; loaded_name = name; ++loads;
    }
    __declspec(noinline) void reset() { receiver = this; ++resets; }
};
using Abi = sporemp::NativePersistenceAbi<Manager>;
void* getter_value = nullptr;
unsigned getter_calls = 0;
__declspec(noinline) void* __cdecl get_manager() { ++getter_calls; return getter_value; }

template<class Function, class Member>
void address(Function& function, Member member) {
    static_assert(sizeof(function) == sizeof(member), "Win32 member pointer representation");
    std::memcpy(&function, &member, sizeof(function));
}

struct StackResults { bool balanced = true, values = true; };
__declspec(noinline) StackResults repeat_calls(Manager& first, Manager& second,
        const wchar_t* name, Abi::Save save, Abi::Load load, Abi::Reset reset, Abi::Get get) {
    // Volatile function pointers force calls through the aliases under test.
    Abi::Save volatile save_call = save;
    Abi::Load volatile load_call = load;
    Abi::Reset volatile reset_call = reset;
    Abi::Get volatile get_call = get;
    StackResults result;
    for (unsigned i = 0; i < 4096; ++i) {
        Manager* receiver = i & 1 ? &second : &first;
        const bool silent = (i & 2) != 0;
        std::uintptr_t before = 0, after = 0;
        __asm mov before, esp
        const bool saved = save_call(receiver, name, silent);
        __asm mov after, esp
        result.balanced = result.balanced && before == after;
        result.values = result.values && saved == !silent && receiver->receiver == receiver &&
            receiver->saved_name == name && receiver->silent == silent;

        __asm mov before, esp
        load_call(receiver, name);
        __asm mov after, esp
        result.balanced = result.balanced && before == after;
        result.values = result.values && receiver->receiver == receiver && receiver->loaded_name == name;

        __asm mov before, esp
        reset_call(receiver);
        __asm mov after, esp
        result.balanced = result.balanced && before == after;
        result.values = result.values && receiver->receiver == receiver;

        getter_value = i & 4 ? receiver : nullptr;
        __asm mov before, esp
        void* returned = get_call();
        __asm mov after, esp
        result.balanced = result.balanced && before == after;
        result.values = result.values && returned == getter_value;
    }
    return result;
}
}

int native_persistence_abi_checks() {
    static_assert(sizeof(void*) == 4, "Persistence ABI fixture requires Win32");
    static_assert(sizeof(wchar_t) == 2, "Persistence names use UTF-16 code units");
    int failures = 0;
    auto check = [&](bool value, const char* message) {
        std::printf("%s: %s (HOST PERSISTENCE ABI FIXTURE; SPORE NOT LOADED)\n", value ? "PASS" : "FAIL", message);
        if (!value) ++failures;
    };
    Abi::Save save = nullptr; Abi::Load load = nullptr; Abi::Reset reset = nullptr;
    address(save, &Manager::save); address(load, &Manager::load); address(reset, &Manager::reset);
    Abi::Save volatile save_call = save;
    Abi::Load volatile load_call = load;
    Abi::Reset volatile reset_call = reset;
    Abi::Get volatile get_call = &get_manager;
    const wchar_t name[] = L"Satir\u00eda_\u6c34_\U0001f30d";
    const wchar_t expected[] = L"Satir\u00eda_\u6c34_\U0001f30d";
    Manager first, second;
    check(save_call(&first, name, false) && first.receiver == &first && first.saved_name == name &&
        !first.silent && first.saves == 1,
        "save thiscall preserves receiver, const UTF-16 pointer, false input and true AL result");
    check(!save_call(&second, name, true) && second.receiver == &second && second.saved_name == name &&
        second.silent && second.saves == 1 && first.saves == 1,
        "save thiscall preserves a distinct receiver, true input and false AL result");
    load_call(&second, name);
    check(second.receiver == &second && second.loaded_name == name && second.loads == 1 && first.loads == 0,
        "load thiscall retains the exact const UTF-16 filename and one receiver-local call");
    reset_call(&first);
    check(first.receiver == &first && first.resets == 1 && second.resets == 0,
        "reset thiscall passes its opaque receiver in ECX with no stack arguments");
    getter_calls = 0; getter_value = &second;
    const bool nonnull_getter = get_call() == &second;
    getter_value = nullptr;
    check(nonnull_getter && get_call() == nullptr && getter_calls == 2,
        "free cdecl getter returns the exact pointer or null and calls once per request");

    first = {}; second = {}; getter_calls = 0;
    const auto repeated = repeat_calls(first, second, name, save, load, reset, &get_manager);
    check(repeated.balanced, "all four persistence aliases restore ESP after each of 4096 repeated calls");
    check(repeated.values && first.saves == 2048 && second.saves == 2048 &&
        first.loads == 2048 && second.loads == 2048 && first.resets == 2048 && second.resets == 2048 && getter_calls == 4096,
        "repeated calls preserve both boolean paths, alternating receivers and exact call counts");
    check(std::wcscmp(name, expected) == 0 && name[10] == wchar_t(0xd83c) && name[11] == wchar_t(0xdf0d),
        "UTF-16 accented, non-Latin and surrogate-pair filename code units remain unchanged");
    return failures;
}
