#pragma once

// Reads the stack pointer at this statement into `variable`, for the stack-balance checks of the ABI tests (MSVC
// Win32 inline assembly). Use this instead of a bare `__asm` statement: a bare one has no terminator, and
// clang-format joins the statement after it onto the same line.
#if !defined(_MSC_VER) || !defined(_M_IX86)
#error Stack-balance checks require the pinned MSVC Win32 build.
#endif

// clang-format off
#define SPOREMP_READ_STACK_POINTER(variable) __asm { mov variable, esp }
// clang-format on
