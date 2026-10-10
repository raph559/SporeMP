#pragma once
#include <cstdint>

namespace sporemp {
// Exact BA57A0 returns. The first two also contain unexpected event kinds 7/8
// in B's storage after acceptance fails; only event5 is admitted for the bounded experiment.
constexpr bool native_history_manager_caller(uint32_t caller) {
    return caller == 0x7a57c4 || caller == 0x7a57db || caller == 0x7a57ff || caller == 0x7a5813;
}

constexpr bool native_history_relationship_caller(uint32_t caller) {
    return caller == 0x7a58b3;
}

constexpr bool native_history_ui_reader(uint32_t caller, uint32_t category) {
    return caller == 0x269318 && category == 8;
}

constexpr bool native_history_first_kill(uint32_t action, uint32_t event, uint32_t caller) {
    return action == 0 && event == 5 && caller == 0x7a4d4d;
}

// What the original's first-kill event leaves in the owner's history: one event more, in a category that was
// empty or already held events. Up to 0.0.225 only the very first event of a history was accepted (trial315).
constexpr bool native_history_one_event_added(uint32_t events_before, uint32_t categories_before, uint32_t events_after,
                                              uint32_t categories_after) {
    return events_after == events_before + 1 && categories_after >= 1 &&
           (categories_after == categories_before || categories_after == categories_before + 1);
}

template <class Animal>
struct NativeHistoryAbi {
    using Get = void*(__cdecl*)();
    using Allocate = void*(__cdecl*)(uint32_t, const char*, int, unsigned, const char*, int);
    using Construct = void*(__thiscall*)(void*);
    using Initialize = bool(__thiscall*)(void*);
    using Delete = void*(__thiscall*)(void*, uint32_t);
    using Event = void*(__cdecl*)(Animal*, uint32_t);
    // 7EC230 returns a borrowed entry in this root's original category list.
    // 7EB8F0 resizes that entry's native uint32 payload; neither grants rewards.
    using CreateEntry = void*(__thiscall*)(void*, uint32_t, uint32_t);
    using ResizePayload = void(__thiscall*)(void*, uint32_t);
    using ReadCategory = void(__cdecl*)(void*, uint32_t);
    // 7EB210/7EC2F0 receive the original serializable subobject at root+8.
    template <class Stream>
    using Serialize = bool(__thiscall*)(void*, Stream*);
};
} // namespace sporemp
