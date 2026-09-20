#pragma once
#include <cstdint>

namespace sporemp {
template<class Listener, class Noun>
struct NativePlayerAbi {
    // C77ED0: ECX is the +34 IMessageListener subobject; two stack words,
    // ret 8, bool in AL. C7C3C0: primary noun ECX, no stack arguments.
    using Message = bool(__thiscall*)(Listener*, uint32_t, void*);
    using RemoveOwner = void(__thiscall*)(Noun*);
};
// Win32 signatures shared by the engine observers and executable ABI fixtures.
// Native arguments stay inside the bridge process. These are not wire types.
template<class Creature, class Animal, class Combatant, class Vector>
struct NativeActorAbi {
    using SelectAbility = int(__thiscall*)(Creature*, int, bool, bool);
    using Strike = bool(__thiscall*)(Animal*, Combatant*, int, Vector*);
    using Damage = int(__thiscall*)(Combatant*, float, uint32_t, int, const Vector&, Combatant*);
    using SetAttack = void(__thiscall*)(Creature*, int);
    using AnimationDone = bool(__thiscall*)(Creature*, uint32_t);
    // Pinned D67D6E..D67D78 pushes five words and caller-cleans 0x14 bytes.
    // BehaviorState stays opaque; no native container or pointer is a wire type.
    using AttackStop = bool(__cdecl*)(void* behaviorState, Creature*, uint32_t, Combatant*, Animal*);
    // BC8B27..BC8B44: eight stack words, including the two-word native clock;
    // caller cleans 0x20, score returns through x87. SDK's separate float clock
    // arguments describe the same slots but are not suitable C++ clock types.
    using Decide = float(__cdecl*)(Creature*, double, uint32_t, void*, bool*, void*, void*);
    // D7D9A4..D7D9AA: one cdecl word; D99930 returns cCreatureBase TYPE.
    using MemoryCreature = Creature*(__cdecl*)(void*);
};
}
