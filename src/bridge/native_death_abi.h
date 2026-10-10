#pragma once
#include <cstdint>

namespace sporemp {
struct NativeDeathAbi {
    // D40284..D4028F: handler ECX, reason stack word; D38E8B RET 4.
    using Begin = void(__thiscall*)(void*, uint32_t);
    // D42E50: IMessageListener subobject ECX, message and payload, RET 8.
    using Message = bool(__thiscall*)(void*, uint32_t, void*);
    // Original handler D38EC0 RET 8, D401E0 RET 4, D405F0 RET,
    // D38F00 RET 4. The destructor flag is zero for bridge-owned storage.
    using Action = bool(__thiscall*)(void*, uint32_t, void*);
    using Tick = bool(__thiscall*)(void*, float);
    using Reset = bool(__thiscall*)(void*);
    using Destroy = void*(__thiscall*)(void*, uint32_t);
    using Restart = void(__cdecl*)();                                // D40FA0; original listener uses no arguments.
    using ClearInteraction = void(__cdecl*)();                       // D3C360 RET
    using UiDeactivate = void(__thiscall*)(void*);                   // D4D670 RET
    using UiActivate = void(__thiscall*)(void*, uint32_t, uint32_t); // D4D740 RET 8
    using CinematicParameter = void(__thiscall*)(void*, uint32_t, const void*, uint32_t); // AE0930 RET 12
};
} // namespace sporemp
