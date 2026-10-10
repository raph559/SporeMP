#pragma once
#include <cstdint>

namespace sporemp {
// Pinned GOG GA dc04aee5: D2C058 calls 807CA0 and cleans three words.
// The original returns bool in AL, with plain RET; the key is a const reference.
template <class Window, class Key>
using NativePortraitSetBackground = bool(__cdecl*)(Window*, const Key&, int);
// Original E01280 passes GlobalUI+8 as ECX to 810650, with (control,true).
// 810650 forwards to 8102A0; its null path uses RET 8.
template <class Layout, class Window>
using NativePortraitFindWindow = Window*(__thiscall*)(Layout*, uint32_t, bool);
inline constexpr uint32_t native_portrait_control = 0x03d99151;
inline constexpr uint32_t native_portrait_png = 0x2f7d0004;
inline constexpr uint8_t native_portrait_setter_prefix[] = {0x51, 0x53, 0x56, 0x8b, 0x74, 0x24, 0x10,
                                                            0x33, 0xdb, 0x3b, 0xf3, 0x75, 0x06};
inline constexpr uint8_t native_portrait_setter_return[] = {0x5e, 0x8a, 0xc3, 0x5b, 0x59, 0xc3};
inline constexpr uint8_t native_portrait_lookup_bytes[] = {0x8b, 0x49, 0x14, 0x85, 0xc9, 0x74, 0x05, 0xe9, 0x44,
                                                           0xfc, 0xff, 0xff, 0x33, 0xc0, 0xc2, 0x08, 0x00};
} // namespace sporemp
