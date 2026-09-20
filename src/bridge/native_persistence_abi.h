#pragma once

namespace sporemp {
// Win32 aliases shared with executable ABI fixtures. Original preferred VAs:
// B29600 saves ECX, consumes two stack slots (RET 8), returns bool in AL.
// B28020 saves ECX, consumes a filename pointer (RET 4), has no bool result.
// B7E010 saves ECX and returns with RET; B3D440/B3D460 return globals in EAX.
// These native pointers never cross the worker protocol.
template<class Manager>
struct NativePersistenceAbi {
    using Save = bool(__thiscall*)(Manager*, const wchar_t*, bool);
    using Load = void(__thiscall*)(Manager*, const wchar_t*);
    using Reset = void(__thiscall*)(void*);
    using Get = void*(__cdecl*)();
};
}
