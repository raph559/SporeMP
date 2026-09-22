#pragma once
#include <cstdint>

namespace sporemp {
// Win32 types for the pinned model-only presentation leaves. They are shared
// with the HOST ABI fixture; native pointers never leave the bridge process.
template<class Animal, class Animated>
struct NativeLifeAbi {
    // C0C710: three caller-cleaned stack words, bool in AL. ECX is not Animal.
    using Map = bool(__cdecl*)(Animal*, uint32_t, uint32_t*);
    // A048D0: model ECX, one stack word, RET 4, bool in AL.
    using HasCurrentMode = bool(__thiscall*)(Animated*, int);
    // A02B00: model ECX, no stack words; tail-calls the native queue clear.
    using Clear = void(__thiscall*)(Animated*);
    // A05090: model ECX, four optional output pointers, RET 10h. The native
    // implementation has no defined success result despite the SDK's int type.
    using Read = void(__thiscall*)(Animated*, uint32_t*, float*, int*, int*);
};
}
