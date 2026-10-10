#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sporemp {
// Value-only admission evidence. A resident corpse remains an original herd
// member; native death is not retirement. No health or population is changed.
struct NativeRespawnMember {
    uint32_t native_id = UINT32_MAX, herd_native_id = UINT32_MAX;
    bool current = false;
};

inline bool native_respawn_herd_admissible(uint32_t owner, uint32_t peer, uint32_t herd,
                                           const std::vector<NativeRespawnMember>& members) {
    if (owner == UINT32_MAX || peer == UINT32_MAX || owner == peer || herd == UINT32_MAX || members.empty()) {
        return false;
    }
    unsigned owners = 0;
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto& member = members[i];
        if (!member.current || member.native_id == UINT32_MAX || member.native_id == peer ||
            member.herd_native_id != herd) {
            return false;
        }
        if (member.native_id == owner) {
            ++owners;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (members[j].native_id == member.native_id) {
                return false;
            }
        }
    }
    return owners == 1;
}
} // namespace sporemp
