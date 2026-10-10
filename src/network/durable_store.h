#pragma once
#include "world_identity.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sporemp::network {
// One writer, immutable checksummed records, flushed atomic CURRENT publication.
// A successful commit is durable to Windows' file-flush contract. No claim of
// surviving storage hardware that lies about flush completion.
class DurableStore {
public:
    static constexpr size_t max_bytes = 100 * 1024 * 1024;
    using Boundary = std::function<void(const char*)>; // Test-only crash injection.
    DurableStore();
    ~DurableStore();
    DurableStore(const DurableStore&) = delete;
    DurableStore& operator=(const DurableStore&) = delete;
    // Directory must already exist; production caller gives it a private ACL.
    bool open(const std::filesystem::path&, std::vector<uint8_t>&, std::string&);
    bool commit(const std::vector<uint8_t>&, std::string&, const Boundary& = {});
    uint64_t revision() const noexcept;
    Digest identity() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sporemp::network
