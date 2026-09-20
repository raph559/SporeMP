#pragma once

// An interior x86 branch is not a C++ call boundary. Keep all registers, flags,
// x87 state and XMM0..7/MXCSR alive while consulting the engine-thread policy.
// ORIGINAL is a Detours trampoline; DENIED is an inspected continuation in the
// same native frame, with no skipped stack adjustment or required local setup.
// Both addresses must be validated before attaching. No native bytes are copied
// into this header. The shared shim is exercised by native_branch_abi_tests.cpp.
#if !defined(_MSC_VER) || !defined(_M_IX86)
#error Native branch guards require the pinned MSVC Win32 build.
#endif

#define SPOREMP_NATIVE_BRANCH(NAME, ALLOW, ORIGINAL, DENIED) \
    __declspec(naked) void NAME() { \
        __asm { pushfd } \
        __asm { pushad } \
        __asm { mov eax, esp } \
        __asm { sub esp, 528 } \
        __asm { and esp, -16 } \
        __asm { mov [esp + 512], eax } \
        __asm { fxsave [esp] } \
        __asm { fninit } \
        __asm { mov dword ptr [esp + 516], 0x1f80 } \
        __asm { ldmxcsr [esp + 516] } \
        __asm { cld } \
        __asm { call ALLOW } \
        __asm { test al, al } \
        __asm { jz branch_denied } \
        __asm { fxrstor [esp] } \
        __asm { mov esp, [esp + 512] } \
        __asm { popad } \
        __asm { popfd } \
        __asm { jmp dword ptr [ORIGINAL] } \
        __asm { branch_denied: } \
        __asm { fxrstor [esp] } \
        __asm { mov esp, [esp + 512] } \
        __asm { popad } \
        __asm { popfd } \
        __asm { jmp dword ptr [DENIED] } \
    }
