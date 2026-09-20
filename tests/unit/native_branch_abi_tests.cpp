#include "native_branch.h"
#include "detour_transaction.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
void* branch_original=nullptr;
void* branch_resume=nullptr;
unsigned branch_mutated=0, branch_calls=0;
unsigned allow_branch=1;
unsigned initial_esp=0;
unsigned observed_registers[9]{};
__declspec(align(16)) unsigned char caller_float[512]{};
__declspec(align(16)) unsigned char observed_float[512]{};
__declspec(align(16)) const unsigned vector_words[4]={0x3f800000,0xc0200000,0x40500000,0x80000000};
unsigned test_mxcsr=0x3f80, callback_mxcsr=0x5f80;
unsigned callback_entry_mxcsr=0;
__declspec(naked) bool __cdecl branch_allow() {
    __asm {
        inc branch_calls
        stmxcsr callback_entry_mxcsr
        fninit
        fld1
        fldpi
        ldmxcsr callback_mxcsr
        pxor xmm0, xmm0
        pxor xmm1, xmm1
        pxor xmm2, xmm2
        pxor xmm3, xmm3
        pxor xmm4, xmm4
        pxor xmm5, xmm5
        pxor xmm6, xmm6
        pxor xmm7, xmm7
        mov ecx, 0x87654321
        mov edx, 0x12345678
        mov eax, allow_branch
        ret
    }
}
SPOREMP_NATIVE_BRANCH(branch_hook, branch_allow, branch_original, branch_resume)

// A real x86 function with a live native frame at the detoured instruction.
// The denied continuation shares the original epilogue, as in both SPORE cuts.
__declspec(naked) void __cdecl fixture(unsigned) {
    __asm {
        cmp dword ptr [esp+4], 0
        je run
        mov eax, offset mutation
        mov branch_original, eax
        mov eax, offset presentation
        mov branch_resume, eax
        ret
    run:
        pushfd
        pushad
        fxsave caller_float
        fninit
        fld1
        fldpi
        ldmxcsr test_mxcsr
        movaps xmm0, vector_words
        movaps xmm1, xmm0
        movaps xmm2, xmm0
        movaps xmm3, xmm0
        movaps xmm4, xmm0
        movaps xmm5, xmm0
        movaps xmm6, xmm0
        movaps xmm7, xmm0
        mov initial_esp, esp
        mov eax, 0x13579bdf
        mov ebx, 0x2468ace0
        mov ecx, 0x76543210
        mov edx, 0xfedcba98
        mov esi, 0x11223344
        mov edi, 0xaabbccdd
        mov ebp, 0x55667788
        stc
        std
    mutation:
        mov branch_mutated, 1
    presentation:
        pushfd
        pushad
        fxsave observed_float
        mov esi, esp
        mov edi, offset observed_registers
        mov ecx, 9
        cld
        rep movsd
        popad
        popfd
        fxrstor caller_float
        popad
        popfd
        ret
    }
}
}

int native_branch_abi_checks() {
    using namespace sporemp;
    int checks=0, failures=0;
    auto check=[&](bool value,const char* label) {
        ++checks;if(!value){++failures;std::fprintf(stderr,"FAIL interior branch: %s\n",label);}
    };
    fixture(1);
    fixture(0);
    std::array<unsigned,9> expected_registers{};
    std::memcpy(expected_registers.data(),observed_registers,sizeof(observed_registers));
    std::array<unsigned char,512> expected_float{};
    std::memcpy(expected_float.data(),observed_float,sizeof(observed_float));
    const unsigned reference_esp=observed_registers[3];
    check(branch_mutated==1 && (observed_registers[8]&0x401)==0x401,"unhooked native frame and CF/DF reference");
    const NativeHook hook{&branch_original,reinterpret_cast<void*>(branch_hook)};
    const auto attached=change_hooks(&hook,1,true);
    check(attached==NO_ERROR,"real Detours attaches to interior MOV, outside function entry");
    if(attached!=NO_ERROR)return failures;
    bool registers=true,float_state=true,semantics=true,stack=true,callback_environment=true;
    for(unsigned i=0;i<4096;++i) {
        allow_branch=i&1; branch_mutated=0;
        uintptr_t before=0,after=0;
        __asm mov before,esp
        fixture(0);
        __asm mov after,esp
        stack &= before==after && observed_registers[3]==initial_esp-4;
        // PUSHAD's saved ESP can differ with the caller's stack layout.
        observed_registers[3]=reference_esp;
        registers &= std::memcmp(expected_registers.data(),observed_registers,sizeof(observed_registers))==0;
        // FXSAVE reserved bytes are not architectural state. Compare control,
        // status, tag, MXCSR, the eight 80-bit x87 values and all eight XMMs.
        float_state &= std::memcmp(expected_float.data(),observed_float,5)==0 &&
            std::memcmp(expected_float.data()+24,observed_float+24,8)==0 &&
            std::memcmp(expected_float.data()+160,observed_float+160,128)==0;
        for(unsigned r=0;r<8;++r)
            float_state &= std::memcmp(expected_float.data()+32+r*16,observed_float+32+r*16,10)==0;
        semantics &= branch_mutated==allow_branch;
        callback_environment &= callback_entry_mxcsr==0x1f80;
    }
    check(stack,"4096 allowed/denied entries retain native frame and caller ESP");
    check(registers,"all seven general registers and EFLAGS including CF/DF survive policy callback");
    check(float_state,"live x87 stack/control, XMM0..7 and MXCSR survive deliberately clobbering callback");
    check(callback_environment,"C++ policy callback receives default SSE exception masks and rounding before native state restoration");
    check(semantics && branch_calls==4096,"allowed native mutation and denied native continuation each run once");
    check(change_hooks(&hook,1,false)==NO_ERROR,"interior hook detaches transactionally");
    allow_branch=0;branch_mutated=0;fixture(0);
    check(branch_mutated==1 && branch_calls==4096,"detached native instruction restored");
    std::printf("%d HOST interior x86 branch ABI checks; original SPORE NOT RUN.\n",checks);
    return failures;
}
