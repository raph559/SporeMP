#pragma once
#include <cstdint>

namespace sporemp::network {
// Canonical coordinator identity. This value is distinct from every native
// planet/resource key and carries no meaning outside the universe ledger.
struct LocationId {
    uint64_t galaxy = 0, star = 0, planet = 0, area = 0;

    bool operator<(const LocationId& other) const noexcept {
        if (galaxy != other.galaxy) {
            return galaxy < other.galaxy;
        }
        if (star != other.star) {
            return star < other.star;
        }
        if (planet != other.planet) {
            return planet < other.planet;
        }
        return area < other.area;
    }

    bool operator==(const LocationId& other) const noexcept {
        return galaxy == other.galaxy && star == other.star && planet == other.planet && area == other.area;
    }

    bool valid() const noexcept {
        return galaxy && star && planet && area;
    }
};
} // namespace sporemp::network
