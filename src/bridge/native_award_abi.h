#pragma once
#include <cstdint>

namespace sporemp {
template <class Actor, class Spatial, class Player, class Manager, class Strategy>
struct NativeAwardAbi {
    using Owned = bool(__thiscall*)(Spatial*);
    using Amount = float(__thiscall*)(Actor*, bool);
    using PlayerGet = Player*(__thiscall*)(Manager*);
    using AvatarGet = Actor*(__thiscall*)(Manager*);
    // 4DF490: species-manager ECX, no stack arguments, full EAX pointer, RET.
    using SpeciesGet = void*(__thiscall*)(void*);
    using DisplayAdd = void(__thiscall*)(void*, float);
    using Action = void(__thiscall*)(Strategy*, uint32_t, void*);
    // D47860: handler receiver ECX, action and three-word payload, RET 8.
    // D475E0: same receiver, action and target noun, pointer result, RET 8.
    // D46BE0: three stack pointers, ECX unused on entry, full EAX, RET 0Ch.
    using PropertyAction = uint32_t(__thiscall*)(void*, uint32_t, void*);
    using PropertyLookup = void*(__thiscall*)(void*, uint32_t, void*);
    using PropertyApply = uint32_t(__stdcall*)(void*, void*, Actor*);
    using Update = void(__thiscall*)(Strategy*, float, float);
    using Goal = float(__cdecl*)();
    // D389D0 reads name/token from the stack, calls the native cinematic
    // scheduler, retains its full EAX result and returns with RET 8.
    using Tutorial = uint32_t(__stdcall*)(const char*, uint32_t);
    using TutorialHas = bool(__thiscall*)(Player*, const uint32_t*);
    using TutorialRecord = void(__thiscall*)(Player*, const uint32_t*);
    // AE08B0: manager ECX, name and five words, full EAX, RET 18h.
    using CinematicPlay = uint32_t(__thiscall*)(void*, const char*, uint32_t, uint32_t, const uint32_t*, uint32_t,
                                                uint32_t);
    // ADDE10 copies all three key words into the selected native request.
    using CinematicCaption = void(__thiscall*)(void*, uint32_t, const uint32_t*, uint32_t);
    // B3D5C0 returns the original singleton; BA3190 is its non-inserting
    // map lookup: map ECX, two-word iterator output, three-word key, RET 8.
    using RelationshipManager = void*(__cdecl*)();
    using RelationshipFind = uint32_t*(__thiscall*)(void*, uint32_t*, const uint32_t*);
};
} // namespace sporemp
