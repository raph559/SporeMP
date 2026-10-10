#pragma once
#include "location_id.h"
#include "world_identity.h"
#include <array>
#include <cstdint>

namespace sporemp::network {
constexpr uint32_t preload_ttl_ms = 30000;

struct PreloadOwner {
    uint64_t worker = 0, generation = 0;
};

// Authenticated TLS side channel only. No Session authority or native scene is
// admitted by this exchange. A grant is valid only on its original live
// transport and until request-send monotonic time + ttl_ms, never arrival time.
struct PreloadFrame {
    uint64_t nonce = 0;
    LocationId location{};
    uint64_t worker = 0, worker_generation = 0, checkpoint_revision = 0;
    Digest checkpoint{}, universe{};
    std::array<PreloadOwner, 4> owners{}; // economy, diplomacy, galaxy_ai, disasters
    uint32_t ttl_ms = 0, native_load_slot = 0;
    uint64_t maintenance_transfer = 0;
    uint32_t process_id = 0;
    uint64_t process_created = 0;
    uint32_t producer_mask = 0; // qualified all-four owner = 15; all other peers = 0
};
} // namespace sporemp::network
