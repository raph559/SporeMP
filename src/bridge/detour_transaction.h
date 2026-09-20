#pragma once
#include <windows.h>
#include <cstddef>
namespace sporemp {
struct NativeHook { void** original; void* replacement; };
// Enroll all observed process threads; abort the complete transaction on any failure.
// Called outside loader lock. The host test uses this same code on fixture functions.
LONG change_hooks(const NativeHook* hooks, size_t count, bool attach) noexcept;
}
