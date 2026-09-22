#include "native_pool_abi.h"
#include "detour_transaction.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {
// HOST argument/return fixture only. This does not model SPORE's pool or nouns.
struct Noun {
    uint32_t sentinel=0xfedcba98;
};
struct PoolManager {
    Noun* accepted=nullptr;
    Noun* received=nullptr;
    unsigned calls=0;
    __declspec(noinline) bool release(Noun* noun) {
        ++calls;received=noun;
        return noun && noun==accepted;
    }
};
using Abi=sporemp::NativePoolAbi<PoolManager,Noun>;
Abi::Return return_original=nullptr;
PoolManager* intercepted_manager=nullptr;
Noun* intercepted_noun=nullptr;
unsigned hook_calls=0,true_returns=0,false_returns=0;
bool __fastcall return_hook(PoolManager* self,void*,Noun* noun) {
    ++hook_calls;intercepted_manager=self;intercepted_noun=noun;
    const bool result=return_original(self,noun);
    if(result)++true_returns;else ++false_returns;
    return result;
}
}

int native_pool_abi_checks() {
    int failures=0;
    const auto check=[&](bool result,const char* description) {
        std::printf("%s: %s (HOST ABI FIXTURE; SPORE NOT LOADED)\n",result?"PASS":"FAIL",description);
        if(!result)++failures;
    };
    auto member=&PoolManager::release;
    static_assert(sizeof(void*)==4,"Pool ABI fixture requires Win32");
    static_assert(sizeof(member)==sizeof(return_original),"Win32 thiscall member representation");
    std::memcpy(&return_original,&member,sizeof(member));
    // Keep the real entry address before Detours changes original to a trampoline.
    Abi::Return volatile call=return_original;
    hook_calls=true_returns=false_returns=0;
    const sporemp::NativeHook hooks[]={
        {reinterpret_cast<void**>(&return_original),reinterpret_cast<void*>(return_hook)}
    };
    const auto attached=sporemp::change_hooks(hooks,std::size(hooks),true);
    check(attached==NO_ERROR,"pool one-word thiscall hook attaches using shared signature");
    if(attached!=NO_ERROR)return failures;
    Noun matched,unmatched;
    PoolManager manager,other;
    manager.accepted=&matched;
    check(!call(&manager,nullptr) && manager.calls==1 && hook_calls==1 &&
        intercepted_manager==&manager && !intercepted_noun && !manager.received && false_returns==1,
        "pool null noun preserves ECX, one original call and AL false");
    check(!call(&manager,&unmatched) && manager.calls==2 && hook_calls==2 &&
        intercepted_manager==&manager && intercepted_noun==&unmatched && manager.received==&unmatched && false_returns==2,
        "pool unmatched noun preserves the exact pointer and AL false");
    check(call(&manager,&matched) && manager.calls==3 && hook_calls==3 &&
        intercepted_manager==&manager && intercepted_noun==&matched && manager.received==&matched && true_returns==1 &&
        matched.sentinel==0xfedcba98 && unmatched.sentinel==0xfedcba98,
        "pool AL true survives with unchanged fixture sentinels; result is not inferred from a counter delta");
    check(!call(&other,&matched) && other.calls==1 && manager.calls==3 && hook_calls==4 &&
        intercepted_manager==&other && intercepted_noun==&matched && other.received==&matched && false_returns==3,
        "pool thiscall forwards the selected manager independently of the noun");
    uintptr_t before=0,after=0;
    unsigned mismatches=0,expected_true=0;
    __asm mov before,esp
    for(unsigned i=0;i<4096;++i) {
        auto noun=i%3==0?&matched:i%3==1?&unmatched:nullptr;
        const bool expected=noun==&matched;
        if(expected)++expected_true;
        const bool result=call(&manager,noun);
        if(result!=expected || intercepted_manager!=&manager || intercepted_noun!=noun || manager.received!=noun)
            ++mismatches;
    }
    __asm mov after,esp
    check(before==after && !mismatches && manager.calls==4099 && other.calls==1 && hook_calls==4100 &&
        true_returns==1+expected_true && false_returns==3+4096-expected_true &&
        matched.sentinel==0xfedcba98 && unmatched.sentinel==0xfedcba98,
        "4096 mixed pool return paths preserve ESP, all arguments, AL results and exact original-call counts");
    const auto detached=sporemp::change_hooks(hooks,std::size(hooks),false);
    check(detached==NO_ERROR,"pool ABI hook detaches");
    if(detached==NO_ERROR) {
        const auto previous_hooks=hook_calls;
        check(call(&manager,&matched) && !call(&manager,nullptr) && hook_calls==previous_hooks && manager.calls==4101,
            "detached pool entry retains both original return paths without interception");
    }
    return failures;
}
#ifdef SPOREMP_POOL_ABI_STANDALONE
int main() {return native_pool_abi_checks()?1:0;}
#endif
