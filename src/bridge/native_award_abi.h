#pragma once
#include <cstdint>
namespace sporemp {
template<class Actor,class Spatial,class Player,class Manager,class Strategy>
struct NativeAwardAbi {
    using Owned=bool(__thiscall*)(Spatial*);
    using Amount=float(__thiscall*)(Actor*,bool);
    using PlayerGet=Player*(__thiscall*)(Manager*);
    using AvatarGet=Actor*(__thiscall*)(Manager*);
    using DisplayAdd=void(__thiscall*)(void*,float);
    using Action=void(__thiscall*)(Strategy*,uint32_t,void*);
    using Update=void(__thiscall*)(Strategy*,float,float);
    using Goal=float(__cdecl*)();
};
}
