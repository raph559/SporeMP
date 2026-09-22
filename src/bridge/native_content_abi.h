#pragma once
#include <cstddef>
#include <cstdint>

namespace sporemp {
template<class Key, class Data> struct NativeContentAbi {
    // 4BB500: two caller-cleaned stack words, bool in AL, plain RET.
    using Load = bool(__cdecl*)(Key*, Data**);
    // 433020: native ResourceObject receiver in ECX, no stack words, int EAX.
    using Release = int(__thiscall*)(Data*);
};

template<class Key> struct NativeContentLookupAbi {
    // Pinned singleton getters: no arguments, pointer in EAX, plain RET.
    using Get = void*(__cdecl*)();
    // 8DE620: receiver ECX, key reference, RET 4. Borrowed database in EAX.
    using FindDatabase = void*(__thiscall*)(void*, const Key&);
    // 8DFC80: receiver ECX, key/output/database, RET 0xC. Borrowed result.
    using FindRecord = void*(__thiscall*)(void*, const Key&, Key*, void*);
    // A42700: receiver ECX, no stack arguments; borrowed UTF-16 at +0x20.
    using GetLocation = const char16_t*(__thiscall*)(void*);
};
template<class Key> struct NativeContentImportAbi {
    // 5FC3C0: singleton receiver in ECX, UTF-16 path and output Key&, RET 8,
    // bool in AL. Output scalars are borrowed values, not a native resource.
    using Import = bool(__thiscall*)(void*, const char16_t*, Key&);
};

// The first three words of the pinned Win32 EASTL vector. Validate integer
// arithmetic before using native iterators or touching an element.
struct NativeContentSpan { uint32_t begin, end, capacity; };
inline bool native_content_span_count(const NativeContentSpan& span, uint32_t stride,
    uint32_t limit, uint32_t& count) {
    count = 0;
    if (!stride || span.begin > span.end || span.end > span.capacity) return false;
    if (!span.begin) return span.end == 0 && span.capacity == 0;
    const auto bytes = span.end - span.begin;
    if (bytes % stride || (span.capacity - span.begin) % stride || bytes / stride > limit) return false;
    count = bytes / stride;
    return true;
}
inline bool native_content_capability_range(int start, int count, uint32_t size) {
    // No unknown negative sentinel is silently interpreted as an empty range.
    return start >= 0 && count >= 0 && static_cast<uint32_t>(start) <= size &&
        static_cast<uint32_t>(count) <= size - static_cast<uint32_t>(start);
}
}
