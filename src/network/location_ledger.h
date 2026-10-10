#pragma once
#include "location_id.h"
#include "world_identity.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sporemp::network {
// Canonical IDs are coordinator values, never native noun IDs or save paths.
struct LocationLease {
    LocationId id{};
    uint64_t worker = 0, generation = 0, checkpoint = 0;
    Digest content{};
    bool active = false;
    uint32_t native_load_slot = 0;
};

struct AvatarLease {
    uint64_t player = 0, generation = 0;
    LocationId location{};
    bool active = false;
};

struct InterestLease {
    uint64_t player = 0;
    LocationId location{};
    uint64_t avatar_generation = 0, worker = 0, worker_generation = 0;
};
enum class TransferPhase : uint32_t {
    prepared = 1,
    source_fenced = 2,
    destination_restored = 3,
    committed = 4
};

struct Transfer {
    uint64_t id = 0, player = 0, source_generation = 0, destination_generation = 0;
    LocationId source{}, destination{};
    TransferPhase phase = TransferPhase::prepared;
};
enum class GlobalSystem : uint32_t {
    economy = 1,
    diplomacy = 2,
    galaxy_ai = 3,
    disasters = 4
};

struct GlobalLease {
    uint64_t worker = 0, generation = 0;
};

// Pure coordinator policy. Every successful mutation must be published with the
// enclosing DurableStore record before any external acknowledgment. Native
// quiesce, restore and event suppression are separate evidence gates.
class LocationLedger {
public:
    bool add_location(const LocationLease&, std::string&);
    bool place_avatar(uint64_t player, const LocationId&, std::string&);
    bool set_global_owner(GlobalSystem, const GlobalLease&, std::string&);
    bool begin_transfer(uint64_t id, uint64_t player, const LocationId& destination, std::string&);
    bool fence_source(uint64_t id, uint64_t source_generation, std::string&);
    bool destination_restored(uint64_t id, uint64_t destination_worker, uint64_t destination_generation, std::string&);
    bool recover_committed_destination(uint64_t id, uint64_t worker, uint64_t generation, std::string&);
    bool commit_transfer(uint64_t id, std::string&);
    bool finish_transfer(uint64_t id, std::string&);
    bool abort_transfer(uint64_t id, uint64_t recovered_source_generation, std::string&);
    bool worker_lost(const LocationId&, uint64_t worker, uint64_t generation, std::string&);
    bool replace_worker(const LocationId&, uint64_t worker, uint64_t generation, uint64_t checkpoint, std::string&);
    bool rebind_global(GlobalSystem, const GlobalLease& previous, const GlobalLease& replacement, std::string&);
    // Closed-worker whole-head publication. Preserves canonical locations and
    // owners while fencing every active worker/avatar before fresh admission.
    bool rotate_checkpoint(uint64_t previous, uint64_t next, std::string&);
    bool subscribe_interest(uint64_t player, std::string&);
    bool unsubscribe_interest(uint64_t player, std::string&);
    bool accepts_interest(uint64_t player, const LocationId&, uint64_t avatar_generation, uint64_t worker,
                          uint64_t worker_generation) const noexcept;
    bool claim_global_event(GlobalSystem, uint64_t worker, uint64_t generation, uint64_t event_id, std::string&);
    bool accepts_avatar_outcome(uint64_t player, const LocationId&, uint64_t avatar_generation, uint64_t worker,
                                uint64_t worker_generation) const noexcept;
    bool accepts_global_outcome(GlobalSystem, uint64_t worker, uint64_t generation) const noexcept;
    const GlobalLease* global_owner(GlobalSystem) const noexcept;
    const AvatarLease* avatar(uint64_t player) const noexcept;
    const Transfer* transfer(uint64_t id) const noexcept;

    const std::map<LocationId, LocationLease>& location_leases() const noexcept {
        return locations_;
    }

    size_t location_count() const noexcept {
        return locations_.size();
    }

    size_t interest_count() const noexcept {
        return interests_.size();
    }

    size_t global_event_count() const noexcept;
    // Until native per-location projection exists, every location must read
    // the one selected immutable global checkpoint revision.
    bool checkpoints_share(uint64_t revision) const noexcept;
    bool content_matches(const Digest&) const noexcept;
    std::vector<uint8_t> snapshot() const;
    bool restore(const std::vector<uint8_t>&, std::string&);

private:
    std::map<LocationId, LocationLease> locations_;
    std::map<uint64_t, AvatarLease> avatars_;
    std::map<uint64_t, Transfer> transfers_;
    std::map<GlobalSystem, GlobalLease> globals_;
    std::map<uint64_t, InterestLease> interests_;
    std::map<GlobalSystem, std::set<uint64_t>> global_events_;
    uint64_t high_transfer_id_ = 0;
    bool valid() const noexcept;
    bool locations_valid() const noexcept;
    bool avatars_valid() const noexcept;
    bool transfers_valid() const noexcept;
    bool transfer_phase_valid(const Transfer& transfer, const AvatarLease& lease) const noexcept;
    bool globals_valid() const noexcept;
    bool interests_valid() const noexcept;
    bool global_events_valid() const noexcept;
    bool worker_active(uint64_t worker, uint64_t generation) const noexcept;
    bool other_locations_admit(const LocationId& id, uint64_t worker, uint64_t checkpoint, std::string& error) const;
    bool transfer_fenced(uint64_t player) const noexcept;
    bool transfer_uncommitted(uint64_t player) const noexcept;
};
} // namespace sporemp::network
