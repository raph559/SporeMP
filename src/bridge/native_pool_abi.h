#pragma once

namespace sporemp {
template<class PoolManager,class Noun> struct NativePoolAbi {
    // Pinned original ACCEC0: manager in ECX, one noun stack word, bool in AL,
    // RET 4 on both null/unmatched false and matched true paths. The signature
    // does not imply that native pool success changes any observed noun counter.
    using Return = bool(__thiscall*)(PoolManager*,Noun*);
};
}
