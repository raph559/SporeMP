#pragma once
#include <cstdint>

namespace sporemp {
struct NativeHatchAbi {
    // Pinned D49220 takes one caller-cleaned ability-index word, returns the
    // original eligibility code in EAX and uses plain RET. D49568 calls it;
    // D4956D is the caller return address. No native object crosses this ABI.
    using Eligibility = uint32_t(__cdecl*)(int);
};
} // namespace sporemp
