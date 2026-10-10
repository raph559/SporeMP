#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <cmath>

namespace sporemp {
// Pinned D9BD4F..D9BD57 initializes exactly three 32-bit words before
// D9BD5F calls ExecuteAction. The larger provisional SDK structure must not
// be copied here. Addresses remain local and are never serialized.
inline constexpr uint32_t native_social_action = 0x060b4123;
inline constexpr uint32_t native_social_action_caller_rva = 0x99bd64;

struct NativeSocialPayload {
    uint32_t actor = 0, target = 0, reserved = 0;
};

static_assert(sizeof(NativeSocialPayload) == 12);

inline bool decode_native_social_payload(uint32_t action, uint32_t caller, const void* input, size_t readable_bytes,
                                         NativeSocialPayload& output) noexcept {
    output = {};
    if (action != native_social_action || caller != native_social_action_caller_rva || !input ||
        readable_bytes < sizeof(NativeSocialPayload)) {
        return false;
    }
    NativeSocialPayload value;
    std::memcpy(&value, input, sizeof(value));
    if (!value.actor || !value.target || value.actor == value.target || value.reserved) {
        return false;
    }
    output = value;
    return true;
}

// Original E33080 installs this vtable and initializes these fields. E31990
// writes the interpolated side values, E319F0 reads them. Keep raw offsets:
// this observation does not assign winners, score abilities or apply rewards.
inline constexpr uint32_t native_social_ui_vtable_rva = 0x1081900;
inline constexpr size_t native_social_ui_read_size = 0x1e4;

struct NativeSocialMeter {
    std::array<uint8_t, 3> flags{}; // +24, +25, +26
    uint32_t field_44 = 0;
    std::array<float, 8> values{}; // +3c,+40,+134,+138,+13c,+1d8,+1dc,+1e0

    bool operator==(const NativeSocialMeter& other) const noexcept {
        return flags == other.flags && field_44 == other.field_44 && values == other.values;
    }
};

inline bool decode_native_social_meter(const void* input, size_t readable_bytes, uint32_t expected_vtable,
                                       NativeSocialMeter& output) noexcept {
    output = {};
    if (!input || readable_bytes < native_social_ui_read_size || !expected_vtable) {
        return false;
    }
    const auto bytes = static_cast<const unsigned char*>(input);
    uint32_t vtable = 0;
    std::memcpy(&vtable, bytes, sizeof(vtable));
    if (vtable != expected_vtable) {
        return false;
    }
    NativeSocialMeter value;
    for (size_t i = 0; i < value.flags.size(); ++i) {
        value.flags[i] = bytes[0x24 + i];
        if (value.flags[i] > 1) {
            return false;
        }
    }
    std::memcpy(&value.field_44, bytes + 0x44, sizeof(value.field_44));
    constexpr size_t offsets[] = {0x3c, 0x40, 0x134, 0x138, 0x13c, 0x1d8, 0x1dc, 0x1e0};
    for (size_t i = 0; i < value.values.size(); ++i) {
        std::memcpy(&value.values[i], bytes + offsets[i], sizeof(float));
        if (!std::isfinite(value.values[i])) {
            return false;
        }
    }
    output = value;
    return true;
}
} // namespace sporemp
