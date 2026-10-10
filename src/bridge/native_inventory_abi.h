#pragma once
#include <cstdint>

namespace sporemp {
struct NativePartKey {
    uint32_t instance, type, group;
};

static_assert(sizeof(NativePartKey) == 12, "Pinned native ResourceKey stack value");

struct NativeInventoryAbi {
    // B3D500 returns the original presentation singleton (cdecl, EAX).
    // E14CE0 takes that receiver in ECX and three by-value key words; RET 0Ch.
    // Its original queue creates/draws UI only; it never selects/unlocks a part.
    using Manager = void*(__cdecl*)();
    using Notify = void(__thiscall*)(void*, NativePartKey);
};
} // namespace sporemp
