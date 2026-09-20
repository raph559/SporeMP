#pragma once
namespace sporemp {
template<class Animal> struct NativeReplicaAbi {
    // Primary Animal vtable +88; original C02E10: ECX + one float, RET 4.
    using Hunger = void(__thiscall*)(Animal*, float);
    // D2E480: MOVSS [ESP+4] to the stage DNA scalar; C3 (cdecl).
    using DnaSet = void(__cdecl*)(float);
    // C08210 / C02D00 / C223A0: ECX receiver, no stack arguments.
    using Lifecycle = void(__thiscall*)(Animal*);
    // C02AB0 / C03280: RET 4. bool occupies a full stack word.
    using Brain = void(__thiscall*)(Animal*, int);
    using Remove = void(__thiscall*)(Animal*, bool);
    // Native AL result matters, despite the SDK's void declaration.
    using Unlock = bool(__thiscall*)(void*, unsigned, unsigned, int);
    using Lock = bool(__thiscall*)(void*, unsigned, unsigned);
    // C6D960: ECX is cHerd, one millisecond word, RET 4.
    using HerdUpdate = void(__thiscall*)(void*, unsigned);
    // D4F290 / D4EDD0: ECX context, no stack arguments; C3.
    using Population = void(__thiscall*)(void*);
    // D52270: uint64 clock (two words), RET 8; integer-to-float in body.
    using SceneEvents = void(__thiscall*)(void*, unsigned long long);
    // C19900: ECX animal, unsigned ability index, RET 4; no presentation calls.
    using AbilityUse = void(__thiscall*)(Animal*, unsigned);
    // B29960: primary IMessageListener ECX, message ID + local payload, RET 8, AL.
    using PersistenceMessage = bool(__thiscall*)(void*, unsigned, void*);
    // C0AE30: primary Animal receiver, uint milliseconds, RET 4.
    using AnimalUpdate = void(__thiscall*)(Animal*, unsigned);
    // C2EE80: native relationship UI context, two uint32 output pointers, RET 8.
    using SocialResult = void(__thiscall*)(void*, unsigned*, unsigned*);
};
template<class Animal, class Vector, class Species, class Herd> struct NativeReplicaCreateAbi {
    // C09B40: six stack words, caller pops 0x18, EAX pointer or null.
    using Create = Animal*(__cdecl*)(const Vector&, Species*, int, Herd*, bool, bool);
};
}
