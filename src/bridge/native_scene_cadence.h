#pragma once
#include <cstdint>

namespace sporemp {
// Wait after capture/publication completes. A slow first capture must not make
// another capture immediately eligible with the same coarse native clock tick.
// This schedules reads; each entity retains its original native sample tick.
constexpr std::uint64_t next_native_scene_capture(std::uint64_t completed_ms) noexcept {
    return completed_ms + 50;
}
}
