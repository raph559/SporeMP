#pragma once
#include <cstdint>

namespace sporemp {
template <class Animal>
struct NativePickupAbi {
    // D32FD0: opaque strategy ECX, picked object at ESP+4, void RET4.
    using Input = void(__thiscall*)(void*, void*);
    // D30830: opaque picked object at ESP+4, full classification in EAX,
    // plain RET; D32FDE's caller removes its one stack word.
    using Classify = uint32_t(__cdecl*)(void*);
    // C0C070: primary Animal ECX, eleven stack words, RET 2Ch, order in EAX.
    using Order = void*(__thiscall*)(Animal*, uint32_t, uint32_t, uint32_t, uint32_t, float, uint32_t, uint32_t,
                                     uint32_t, uint32_t, void*, bool);
    // BC9477/BC969E: eight stack words, caller cleanup 20h; D70FD0 bool in AL.
    using Tick = bool(__cdecl*)(Animal*, double, uint32_t, uintptr_t, void*, void*, float);
};
} // namespace sporemp
