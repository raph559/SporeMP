#pragma once
#include <cmath>
#include <cstdint>

namespace sporemp::network {
// Camera input is a direction, never a client position or velocity authority.
struct SteeringVector {
    float x = 0, y = 0, z = 0;
};

inline float steering_dot(SteeringVector a, SteeringVector b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline SteeringVector steering_scale(SteeringVector a, float s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}

inline SteeringVector steering_subtract(SteeringVector a, SteeringVector b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline SteeringVector steering_cross(SteeringVector a, SteeringVector b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline bool steering_unit(SteeringVector& v) noexcept {
    const float length = steering_dot(v, v);
    if (!std::isfinite(length) || length < 0.000001f) {
        return false;
    }
    v = steering_scale(v, 1.0f / std::sqrt(length));
    return true;
}

inline bool steering_tangent(SteeringVector direction, SteeringVector position, SteeringVector& result) noexcept {
    if (!steering_unit(position)) {
        return false;
    }
    result = steering_subtract(direction, steering_scale(position, steering_dot(direction, position)));
    return steering_unit(result);
}

// The viewport point whose camera ray passes through `target`, as an offset in pixels from the viewport
// centre. Inputs are the camera position and three of its rays: through the centre, `step` pixels to the
// right of it and `step` pixels below it. Pinhole model. False for a target that is not in front of the
// camera, a zero step or rays that do not differ.
inline bool screen_point(SteeringVector camera, SteeringVector centre_ray, SteeringVector right_ray,
                         SteeringVector down_ray, float step, SteeringVector target, float& dx, float& dy) noexcept {
    if (!(step > 0) || !steering_unit(centre_ray)) {
        return false;
    }
    // Where a direction meets the plane one unit in front of the camera, relative to the centre ray.
    const auto on_plane = [&](SteeringVector direction, SteeringVector& result) noexcept {
        const float depth = steering_dot(direction, centre_ray);
        if (!std::isfinite(depth) || depth < 0.000001f) {
            return false;
        }
        result = steering_subtract(steering_scale(direction, 1.0f / depth), centre_ray);
        return true;
    };
    SteeringVector right, down, point;
    if (!on_plane(right_ray, right) || !on_plane(down_ray, down) ||
        !on_plane(steering_subtract(target, camera), point)) {
        return false;
    }
    const float right_length = steering_dot(right, right), down_length = steering_dot(down, down);
    if (right_length < 0.000000000001f || down_length < 0.000000000001f) {
        return false;
    }
    dx = step * steering_dot(point, right) / right_length;
    dy = step * steering_dot(point, down) / down_length;
    return std::isfinite(dx) && std::isfinite(dy);
}

inline bool camera_steering(SteeringVector position, SteeringVector center_ray, SteeringVector right_ray,
                            int horizontal, int forward, SteeringVector& result) noexcept {
    if (horizontal < -1 || horizontal > 1 || forward < -1 || forward > 1 || (!horizontal && !forward) ||
        !steering_unit(position) || !steering_unit(center_ray) || !steering_unit(right_ray)) {
        return false;
    }
    auto screen_right = steering_subtract(right_ray, steering_scale(center_ray, steering_dot(right_ray, center_ray)));
    if (!steering_unit(screen_right)) {
        return false;
    }
    SteeringVector front;
    if (!steering_tangent(center_ray, position, front)) {
        // A vertical view still has a defined screen-right direction.
        front = steering_cross(position, screen_right);
        if (!steering_unit(front)) {
            return false;
        }
    }
    auto right = steering_cross(front, position);
    if (!steering_unit(right) || steering_dot(right, screen_right) < 0.2f) {
        return false;
    }
    result = {front.x * forward + right.x * horizontal, front.y * forward + right.y * horizontal,
              front.z * forward + right.z * horizontal};
    return steering_unit(result);
}

// Three signed unit components, each quantized to 1/511 in a 10-bit field.
// Codes 1023 and the top two bits are reserved. Magnitude is checked again at
// decode so a malformed word cannot become a speed or distance request.
// A place in the world as three signed 16-bit values of an eighth of a unit, with a marker bit: plant food is no
// shared entity, and each game re-places part of its fruit on its own (trial378), so an intention to eat a fruit
// says where the player saw it. Zero is no place.
inline uint64_t pack_place(const float (&place)[3]) noexcept {
    uint64_t packed = uint64_t(1) << 48;
    for (size_t i = 0; i < 3; ++i) {
        // Written as a negated range so that a NaN is refused too.
        if (!(place[i] > -4000.0f && place[i] < 4000.0f)) { // NOLINT(readability-simplify-boolean-expr)
            return 0;
        }
        const auto value = static_cast<int32_t>(place[i] * 8.0f + (place[i] < 0 ? -0.5f : 0.5f));
        packed |= uint64_t(static_cast<uint16_t>(static_cast<int16_t>(value))) << (16 * i);
    }
    return packed;
}

inline bool unpack_place(uint64_t packed, float (&place)[3]) noexcept {
    if (packed >> 48 != 1) {
        return false;
    }
    for (size_t i = 0; i < 3; ++i) {
        place[i] = static_cast<float>(static_cast<int16_t>(static_cast<uint16_t>(packed >> (16 * i)))) / 8.0f;
    }
    return true;
}

// Whether a fruit at `held` is the one seen at the packed place: within a unit and a half.
inline bool same_place(uint64_t packed, const float (&held)[3]) noexcept {
    float seen[3]{};
    if (!unpack_place(packed, seen)) {
        return false;
    }
    float square = 0;
    for (size_t i = 0; i < 3; ++i) {
        square += (seen[i] - held[i]) * (seen[i] - held[i]);
    }
    return square <= 1.5f * 1.5f;
}

inline bool unpack_steering(int32_t packed, SteeringVector& result) noexcept {
    const uint32_t bits = static_cast<uint32_t>(packed);
    if (bits >> 30) {
        return false;
    }
    const uint32_t x = bits & 1023, y = (bits >> 10) & 1023, z = (bits >> 20) & 1023;
    if (x == 1023 || y == 1023 || z == 1023) {
        return false;
    }
    result = {float(int(x) - 511) / 511.0f, float(int(y) - 511) / 511.0f, float(int(z) - 511) / 511.0f};
    const float length = steering_dot(result, result);
    return length >= 0.99f && length <= 1.01f && steering_unit(result);
}

inline bool pack_steering(SteeringVector direction, int32_t& result) noexcept {
    if (!steering_unit(direction)) {
        return false;
    }
    const auto component = [](float value) {
        return uint32_t(int(std::lround(value * 511.0f)) + 511);
    };
    result = int32_t(component(direction.x) | (component(direction.y) << 10) | (component(direction.z) << 20));
    SteeringVector checked;
    return unpack_steering(result, checked);
}
} // namespace sporemp::network
