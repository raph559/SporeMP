#pragma once
#include <cstdint>

namespace sporemp {
template <class Creature>
struct NativeSocialAbi {
    // D48AD4/D48AD5 push ability/actor, caller cleans eight bytes at D48ADB.
    // D1E930 reads both slots and returns with plain RET; no return is consumed.
    using Input = void(__cdecl*)(Creature*, int);
    // C1CDB8 pushes the creature; C1CDC6 cleans four bytes after C0D500.
    using Deactivate = void(__cdecl*)(Creature*);
    // C1CDBE/C1CDC9 pass tree in ECX and root on the stack to BCB7D0,
    // whose BCB81F return cleans exactly four bytes.
    using SelectRoot = void(__thiscall*)(void*, const void*);
    // BC9477 invokes the leaf tick with eight caller-cleaned words. Time is
    // a double; D9C100 reads the actor at +4, state at +18 and delta at +20.
    using Tick = uint32_t(__cdecl*)(Creature*, double, uint32_t, uint32_t, void*, void*, float);
    // DA49F7..DA49FB supplies counterpart/creature stack slots and context ECX.
    // D9BE1E returns the native animation ID and cleans eight bytes.
    using Result = uint32_t(__thiscall*)(void*, Creature*, Creature*);
    // B32330/B32360 use ECX as the time manager, one pause-type stack word,
    // return the signed counter in EAX, and clean four bytes at RET.
    using Pause = int(__thiscall*)(void*, uint32_t);
    // E319D0 adjusts ECX to UILayout +0c; 810630/8103B0 return AL and RET 4.
    using MeterVisible = bool(__thiscall*)(void*, bool);
    // E31C70 receives the native relationship enum and tail-calls the icon
    // setter with one stack word; default RET 4.
    using MeterRelation = void(__thiscall*)(void*, uint32_t);
};
} // namespace sporemp
