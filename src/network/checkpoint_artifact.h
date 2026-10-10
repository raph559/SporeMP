#pragma once
#include "protocol.h"
#include <filesystem>
#include <vector>

namespace sporemp::network {
// Closed original-game bytes, never a coordinator simulation or replay log.
// Paths are fixed profile-relative archive members, never peer-supplied paths.
struct CheckpointArtifact {
    static constexpr size_t max_bytes = 80 * 1024 * 1024;
    uint32_t schema = 0;
    Identity identity{};
    Digest provenance{}, digest{};
    Digest secondary_save{}; // schema 2: original Haagos.spo bytes
    std::array<uint32_t, 4>
        native_ids{}; // schema 1: A/B actors and players; schema 2: two primary game IDs, then zeroes
    uint64_t save_request = 0;
    uint32_t rollback_ms = 0;
};

bool validate_checkpoint_artifact(const std::vector<uint8_t>&, CheckpointArtifact&, std::string&);
bool read_checkpoint_artifact(const std::filesystem::path&, std::vector<uint8_t>&, std::string&);
bool write_checkpoint_artifact(const std::filesystem::path&, const std::vector<uint8_t>&, std::string&);
} // namespace sporemp::network
