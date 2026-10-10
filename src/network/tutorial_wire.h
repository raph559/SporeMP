#pragma once
#include <cstdint>

namespace sporemp::network {
// Native First DNA / First Part Unlock presentation. No text, paths or native
// pointers are accepted. The common packet supplies the scene and owner.
struct TutorialFrame {
    uint64_t entity = 0, generation = 0;
    uint32_t token = 0, caption = 0;

    bool operator==(const TutorialFrame& other) const noexcept {
        return entity == other.entity && generation == other.generation && token == other.token &&
               caption == other.caption;
    }
};

inline bool valid_tutorial(const TutorialFrame& value) noexcept {
    return value.entity && value.generation &&
           ((value.token == 0xea7666af &&
             (value.caption == 0x04599f0e || value.caption == 0x061d8186 || value.caption == 0x061d8045)) ||
            (value.token == 0xb721f66f && !value.caption));
}

inline uint64_t tutorial_key(uint64_t player, uint32_t token) noexcept {
    return (player << 32) | token;
}
} // namespace sporemp::network
