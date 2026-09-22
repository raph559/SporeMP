#pragma once
#include <cstdint>
namespace sporemp {
template<class Animal> struct NativePickupAbi {
    // C0C070: primary Animal ECX, eleven stack words, RET 2Ch, order in EAX.
    using Order = void*(__thiscall*)(Animal*,uint32_t,uint32_t,uint32_t,uint32_t,
        float,uint32_t,uint32_t,uint32_t,uint32_t,Animal*,bool);
    // BC9477/BC969E: eight stack words, caller cleanup 20h; D70FD0 bool in AL.
    using Tick = bool(__cdecl*)(Animal*,double,uint32_t,uintptr_t,void*,void*,float);
};
}
