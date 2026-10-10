#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace sporemp::network {
struct SocialSegment {
    float end = 0, steps = 0;
    uint32_t color = 0;
    float duration = 0, speed = 0;

    bool operator==(const SocialSegment& b) const noexcept {
        return end == b.end && steps == b.steps && color == b.color && duration == b.duration && speed == b.speed;
    }
};

static_assert(sizeof(SocialSegment) == 20);

// Ephemeral native UI values. No gameplay commands, rewards or pointers.
struct SocialFrame {
    uint64_t entity = 0, generation = 0, target = 0, target_generation = 0;
    uint32_t visible = 0, flags = 0, mode = 0;
    std::array<float, 8> values{};
    uint32_t relation = 0;
    std::array<uint32_t, 2> segment_count{};
    // Scalar drawing segments, without native vector layout or addresses.
    std::array<std::array<SocialSegment, 6>, 2> segments{};

    bool operator==(const SocialFrame& b) const noexcept {
        return entity == b.entity && generation == b.generation && target == b.target &&
               target_generation == b.target_generation && visible == b.visible && flags == b.flags && mode == b.mode &&
               values == b.values && relation == b.relation && segment_count == b.segment_count &&
               segments == b.segments;
    }
};

inline bool valid_social_frame(const SocialFrame& f) noexcept {
    if (!f.entity || !f.generation || !f.target || !f.target_generation || f.entity == f.target || f.visible > 1 ||
        f.flags > 7 || f.mode > 2 || f.relation > 6 || (f.visible && !f.relation)) {
        return false;
    }
    for (float v : f.values) {
        if (!std::isfinite(v) || std::abs(v) > 64) {
            return false;
        }
    }
    for (size_t side = 0; side < 2; ++side) {
        if (f.segment_count[side] > 6) {
            return false;
        }
        for (size_t index = 0; index < 6; ++index) {
            const auto& segment = f.segments[side][index];
            for (float v : {segment.end, segment.steps, segment.duration, segment.speed}) {
                if (!std::isfinite(v) || std::abs(v) > 64) {
                    return false;
                }
            }
            if (index >= f.segment_count[side] && !(segment == SocialSegment{})) {
                return false;
            }
        }
    }
    return true;
}
} // namespace sporemp::network
