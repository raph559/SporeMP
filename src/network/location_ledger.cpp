#include "location_ledger.h"
#include "state_codec.h"
#include <utility>

namespace sporemp::network {
namespace {
constexpr uint32_t magic = 0x314c4d53; // SML1
constexpr size_t max_locations = 64, max_avatars = 256, max_transfers = 256, max_global_events = 4096;
// GlobalSystem has four values, economy to disasters: a map keyed by it holds at most one entry for each.
constexpr size_t global_system_count = 4;

bool known(GlobalSystem system) {
    return system >= GlobalSystem::economy && system <= GlobalSystem::disasters;
}

bool known(TransferPhase phase) {
    return phase >= TransferPhase::prepared && phase <= TransferPhase::committed;
}

// The snapshot's sections, in the order they are stored. Each writer sits beside the reader of the same section;
// a reader fills the map of the ledger being restored and names the reason it refuses the bytes.

void write_id(state::Writer& writer, const LocationId& id) {
    writer.wide(id.galaxy);
    writer.wide(id.star);
    writer.wide(id.planet);
    writer.wide(id.area);
}

LocationId read_id(state::Reader& reader) {
    return {reader.wide(), reader.wide(), reader.wide(), reader.wide()};
}

void write_location(state::Writer& writer, const LocationLease& lease) {
    write_id(writer, lease.id);
    writer.wide(lease.worker);
    writer.wide(lease.generation);
    writer.wide(lease.checkpoint);
    writer.digest(lease.content);
    writer.word(lease.active ? 1 : 0);
    writer.word(lease.native_load_slot);
}

bool read_locations(state::Reader& reader, uint32_t schema, std::map<LocationId, LocationLease>& locations,
                    std::string& error) {
    const auto count = reader.word();
    if (count > max_locations) {
        error = "location_ledger_size";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        LocationLease lease;
        lease.id = read_id(reader);
        lease.worker = reader.wide();
        lease.generation = reader.wide();
        lease.checkpoint = reader.wide();
        lease.content = reader.digest();
        const auto active = reader.word();
        lease.active = active != 0;
        if (schema >= 3) {
            lease.native_load_slot = reader.word();
        }
        if (active > 1 || lease.native_load_slot > 1 || !locations.emplace(lease.id, lease).second) {
            error = "location_ledger_duplicate";
            return false;
        }
    }
    return true;
}

void write_avatar(state::Writer& writer, const AvatarLease& avatar) {
    writer.wide(avatar.player);
    writer.wide(avatar.generation);
    write_id(writer, avatar.location);
    writer.word(avatar.active ? 1 : 0);
}

bool read_avatars(state::Reader& reader, std::map<uint64_t, AvatarLease>& avatars, std::string& error) {
    const auto count = reader.word();
    if (count > max_avatars) {
        error = "location_ledger_size";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        AvatarLease avatar;
        avatar.player = reader.wide();
        avatar.generation = reader.wide();
        avatar.location = read_id(reader);
        const auto active = reader.word();
        avatar.active = active != 0;
        if (active > 1 || !avatars.emplace(avatar.player, avatar).second) {
            error = "location_ledger_duplicate";
            return false;
        }
    }
    return true;
}

void write_transfer(state::Writer& writer, const Transfer& transfer) {
    writer.wide(transfer.id);
    writer.wide(transfer.player);
    writer.wide(transfer.source_generation);
    writer.wide(transfer.destination_generation);
    write_id(writer, transfer.source);
    write_id(writer, transfer.destination);
    writer.word(static_cast<uint32_t>(transfer.phase));
}

bool read_transfers(state::Reader& reader, std::map<uint64_t, Transfer>& transfers, std::string& error) {
    const auto count = reader.word();
    if (count > max_transfers) {
        error = "location_ledger_size";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        Transfer transfer;
        transfer.id = reader.wide();
        transfer.player = reader.wide();
        transfer.source_generation = reader.wide();
        transfer.destination_generation = reader.wide();
        transfer.source = read_id(reader);
        transfer.destination = read_id(reader);
        transfer.phase = static_cast<TransferPhase>(reader.word());
        if (!transfers.emplace(transfer.id, transfer).second) {
            error = "location_ledger_duplicate";
            return false;
        }
    }
    return true;
}

void write_global(state::Writer& writer, GlobalSystem system, const GlobalLease& owner) {
    writer.word(static_cast<uint32_t>(system));
    writer.wide(owner.worker);
    writer.wide(owner.generation);
}

bool read_globals(state::Reader& reader, std::map<GlobalSystem, GlobalLease>& globals, std::string& error) {
    const auto count = reader.word();
    if (count > global_system_count) {
        error = "location_ledger_size";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        const auto system = static_cast<GlobalSystem>(reader.word());
        const GlobalLease owner{reader.wide(), reader.wide()};
        if (!globals.emplace(system, owner).second) {
            error = "location_ledger_duplicate";
            return false;
        }
    }
    return true;
}

void write_interest(state::Writer& writer, const InterestLease& interest) {
    writer.wide(interest.player);
    write_id(writer, interest.location);
    writer.wide(interest.avatar_generation);
    writer.wide(interest.worker);
    writer.wide(interest.worker_generation);
}

bool read_interests(state::Reader& reader, std::map<uint64_t, InterestLease>& interests, std::string& error) {
    const auto count = reader.word();
    if (count > max_avatars) {
        error = "location_ledger_size";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        InterestLease interest;
        interest.player = reader.wide();
        interest.location = read_id(reader);
        interest.avatar_generation = reader.wide();
        interest.worker = reader.wide();
        interest.worker_generation = reader.wide();
        if (!interests.emplace(interest.player, interest).second) {
            error = "location_ledger_duplicate";
            return false;
        }
    }
    return true;
}

void write_global_event(state::Writer& writer, GlobalSystem system, uint64_t event_id) {
    writer.word(static_cast<uint32_t>(system));
    writer.wide(event_id);
}

bool read_global_events(state::Reader& reader, std::map<GlobalSystem, std::set<uint64_t>>& global_events,
                        std::string& error) {
    const auto count = reader.word();
    if (count > max_global_events) {
        error = "location_ledger_size";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        const auto system = static_cast<GlobalSystem>(reader.word());
        const auto event_id = reader.wide();
        if (!known(system) || !event_id || !global_events[system].insert(event_id).second) {
            error = "location_ledger_duplicate";
            return false;
        }
    }
    return true;
}
} // namespace

const AvatarLease* LocationLedger::avatar(uint64_t player) const noexcept {
    const auto found = avatars_.find(player);
    return found == avatars_.end() ? nullptr : &found->second;
}

const Transfer* LocationLedger::transfer(uint64_t id) const noexcept {
    const auto found = transfers_.find(id);
    return found == transfers_.end() ? nullptr : &found->second;
}

size_t LocationLedger::global_event_count() const noexcept {
    size_t count = 0;
    for (const auto& entry : global_events_) {
        count += entry.second.size();
    }
    return count;
}

// Whether a location is active under exactly this worker and generation.
bool LocationLedger::worker_active(uint64_t worker, uint64_t generation) const noexcept {
    for (const auto& entry : locations_) {
        if (entry.second.active && entry.second.worker == worker && entry.second.generation == generation) {
            return true;
        }
    }
    return false;
}

// A worker owns one location and every location reads the same checkpoint: a lease for `id` is refused when
// another location has its worker or a different checkpoint.
bool LocationLedger::other_locations_admit(const LocationId& id, uint64_t worker, uint64_t checkpoint,
                                           std::string& error) const {
    for (const auto& entry : locations_) {
        if (entry.first == id) {
            continue;
        }
        if (entry.second.worker == worker) {
            error = "worker_already_owns_location";
            return false;
        }
        if (entry.second.checkpoint != checkpoint) {
            error = "location_global_checkpoint_mismatch";
            return false;
        }
    }
    return true;
}

// Whether a transfer of this player has fenced its source and not yet committed.
bool LocationLedger::transfer_fenced(uint64_t player) const noexcept {
    for (const auto& entry : transfers_) {
        if (entry.second.player == player && (entry.second.phase == TransferPhase::source_fenced ||
                                              entry.second.phase == TransferPhase::destination_restored)) {
            return true;
        }
    }
    return false;
}

// Whether this player has a transfer in any phase before committed.
bool LocationLedger::transfer_uncommitted(uint64_t player) const noexcept {
    for (const auto& entry : transfers_) {
        if (entry.second.player == player && entry.second.phase != TransferPhase::committed) {
            return true;
        }
    }
    return false;
}

bool LocationLedger::add_location(const LocationLease& lease, std::string& error) {
    if (!lease.id.valid() || !lease.worker || !lease.generation || !lease.checkpoint || lease.content == Digest{} ||
        !lease.active || lease.native_load_slot > 1 || locations_.size() >= max_locations ||
        locations_.count(lease.id)) {
        error = "location_invalid_or_duplicate";
        return false;
    }
    if (!other_locations_admit(lease.id, lease.worker, lease.checkpoint, error)) {
        return false;
    }
    locations_.emplace(lease.id, lease);
    return true;
}

bool LocationLedger::place_avatar(uint64_t player, const LocationId& id, std::string& error) {
    const auto location = locations_.find(id);
    if (!player || avatars_.size() >= max_avatars || avatars_.count(player) || location == locations_.end() ||
        !location->second.active) {
        error = "avatar_placement_invalid";
        return false;
    }
    avatars_.emplace(player, AvatarLease{player, 1, id, true});
    interests_.emplace(player, InterestLease{player, id, 1, location->second.worker, location->second.generation});
    return true;
}

bool LocationLedger::set_global_owner(GlobalSystem system, const GlobalLease& owner, std::string& error) {
    if (!known(system) || !owner.worker || !owner.generation || globals_.count(system)) {
        error = "global_owner_already_set_or_invalid";
        return false;
    }
    if (!worker_active(owner.worker, owner.generation)) {
        error = "global_owner_not_active_worker";
        return false;
    }
    globals_.emplace(system, owner);
    return true;
}

bool LocationLedger::begin_transfer(uint64_t id, uint64_t player, const LocationId& destination, std::string& error) {
    const auto* current = avatar(player);
    const auto target = locations_.find(destination);
    if (!id || id <= high_transfer_id_ || transfers_.size() >= max_transfers || !current || !current->active ||
        target == locations_.end() || !target->second.active || current->location == destination) {
        error = "transfer_prepare_invalid";
        return false;
    }
    // A transfer consumes one generation at the source fence and another at
    // commit or recovery. Refuse before revoking the last usable source lease.
    if (current->generation >= UINT64_MAX - 1) {
        error = "avatar_generation_exhausted";
        return false;
    }
    for (const auto& entry : transfers_) {
        if (entry.second.player == player) {
            error = "avatar_transfer_in_progress";
            return false;
        }
    }
    const auto source = locations_.find(current->location);
    if (source == locations_.end() || !source->second.active || source->second.content != target->second.content) {
        error = "transfer_content_or_source_mismatch";
        return false;
    }
    transfers_.emplace(
        id, Transfer{id, player, current->generation, 0, current->location, destination, TransferPhase::prepared});
    high_transfer_id_ = id;
    return true;
}

bool LocationLedger::fence_source(uint64_t id, uint64_t source_generation, std::string& error) {
    const auto found = transfers_.find(id);
    if (found == transfers_.end() || found->second.phase != TransferPhase::prepared) {
        error = "transfer_not_prepared";
        return false;
    }
    auto& transfer = found->second;
    auto& avatar = avatars_.at(transfer.player);
    if (!locations_.at(transfer.source).active || !locations_.at(transfer.destination).active) {
        error = "transfer_location_inactive";
        return false;
    }
    if (avatar.generation != source_generation || avatar.generation != transfer.source_generation || !avatar.active) {
        error = "transfer_source_generation_mismatch";
        return false;
    }
    // Also protect prepared records written before the prepare-time check.
    if (avatar.generation >= UINT64_MAX - 1) {
        error = "avatar_generation_exhausted";
        return false;
    }
    ++avatar.generation;
    avatar.active = false;
    interests_.erase(avatar.player);
    transfer.phase = TransferPhase::source_fenced;
    return true;
}

bool LocationLedger::destination_restored(uint64_t id, uint64_t destination_worker, uint64_t destination_generation,
                                          std::string& error) {
    const auto found = transfers_.find(id);
    if (found == transfers_.end() || (found->second.phase != TransferPhase::source_fenced &&
                                      found->second.phase != TransferPhase::destination_restored)) {
        error = "transfer_source_not_fenced";
        return false;
    }
    auto& transfer = found->second;
    const auto target = locations_.find(transfer.destination);
    if (target == locations_.end() || !target->second.active || target->second.worker != destination_worker ||
        target->second.generation != destination_generation) {
        error = "transfer_destination_lease_mismatch";
        return false;
    }
    transfer.destination_generation = destination_generation;
    transfer.phase = TransferPhase::destination_restored;
    return true;
}

bool LocationLedger::commit_transfer(uint64_t id, std::string& error) {
    const auto found = transfers_.find(id);
    if (found == transfers_.end() || found->second.phase != TransferPhase::destination_restored) {
        error = "transfer_destination_not_restored";
        return false;
    }
    auto& transfer = found->second;
    auto& avatar = avatars_.at(transfer.player);
    const auto& target = locations_.at(transfer.destination);
    if (avatar.active || !(avatar.location == transfer.source) || !target.active ||
        target.generation != transfer.destination_generation || avatar.generation == UINT64_MAX) {
        error = "transfer_commit_fence_mismatch";
        return false;
    }
    avatar.location = transfer.destination;
    ++avatar.generation;
    avatar.active = true;
    interests_[avatar.player] =
        InterestLease{avatar.player, avatar.location, avatar.generation, target.worker, target.generation};
    transfer.phase = TransferPhase::committed;
    return true;
}

bool LocationLedger::recover_committed_destination(uint64_t id, uint64_t worker, uint64_t generation,
                                                   std::string& error) {
    const auto found = transfers_.find(id);
    if (found == transfers_.end() || found->second.phase != TransferPhase::committed) {
        error = "transfer_recovery_not_committed";
        return false;
    }
    auto& transfer = found->second;
    const auto& target = locations_.at(transfer.destination);
    const auto& avatar = avatars_.at(transfer.player);
    if (!target.active || target.worker != worker || target.generation != generation ||
        generation <= transfer.destination_generation || !avatar.active || !(avatar.location == target.id)) {
        error = "transfer_recovery_destination_lease_mismatch";
        return false;
    }
    transfer.destination_generation = generation;
    return true;
}

bool LocationLedger::finish_transfer(uint64_t id, std::string& error) {
    const auto found = transfers_.find(id);
    if (found == transfers_.end() || found->second.phase != TransferPhase::committed) {
        error = "transfer_not_committed";
        return false;
    }
    transfers_.erase(found);
    return true;
}

bool LocationLedger::rotate_checkpoint(uint64_t previous, uint64_t next, std::string& error) {
    if (!previous || next <= previous || locations_.empty() || !transfers_.empty() || !checkpoints_share(previous)) {
        error = "location_checkpoint_rotation_stale_or_transferring";
        return false;
    }
    for (const auto& entry : locations_) {
        if (entry.second.active && entry.second.generation == UINT64_MAX) {
            error = "worker_generation_exhausted";
            return false;
        }
    }
    for (const auto& entry : avatars_) {
        if (entry.second.active && entry.second.generation == UINT64_MAX) {
            error = "avatar_generation_exhausted";
            return false;
        }
    }
    for (auto& entry : locations_) {
        auto& location = entry.second;
        location.checkpoint = next;
        if (!location.active) {
            continue;
        }
        for (auto& owner : globals_) {
            if (owner.second.worker == location.worker && owner.second.generation == location.generation) {
                ++owner.second.generation;
            }
        }
        ++location.generation;
    }
    for (auto& entry : avatars_) {
        auto& avatar = entry.second;
        if (!avatar.active) {
            continue;
        }
        ++avatar.generation;
        const auto& location = locations_.at(avatar.location);
        interests_[avatar.player] = {avatar.player, avatar.location, avatar.generation, location.worker,
                                     location.generation};
    }
    error.clear();
    return true;
}

bool LocationLedger::abort_transfer(uint64_t id, uint64_t recovered_source_generation, std::string& error) {
    const auto found = transfers_.find(id);
    if (found == transfers_.end() || found->second.phase == TransferPhase::committed) {
        error = "committed_transfer_cannot_abort";
        return false;
    }
    const auto& transfer = found->second;
    auto& avatar = avatars_.at(transfer.player);
    if (transfer.phase != TransferPhase::prepared || !avatar.active) {
        if (!recovered_source_generation || recovered_source_generation <= avatar.generation ||
            locations_.find(transfer.source) == locations_.end() || !locations_.at(transfer.source).active) {
            error = "source_recovery_generation_required";
            return false;
        }
        avatar.generation = recovered_source_generation;
        avatar.active = true;
        const auto& source = locations_.at(transfer.source);
        interests_[avatar.player] =
            InterestLease{avatar.player, avatar.location, avatar.generation, source.worker, source.generation};
    }
    transfers_.erase(found);
    return true;
}

bool LocationLedger::worker_lost(const LocationId& id, uint64_t worker, uint64_t generation, std::string& error) {
    const auto found = locations_.find(id);
    if (found == locations_.end() || !found->second.active || found->second.worker != worker ||
        found->second.generation != generation) {
        error = "worker_loss_stale_lease";
        return false;
    }
    found->second.active = false;
    for (auto& entry : avatars_) {
        if (entry.second.location == id) {
            entry.second.active = false;
            interests_.erase(entry.first);
        }
    }
    return true;
}

bool LocationLedger::replace_worker(const LocationId& id, uint64_t worker, uint64_t generation, uint64_t checkpoint,
                                    std::string& error) {
    const auto found = locations_.find(id);
    if (found == locations_.end() || found->second.active || !worker || generation <= found->second.generation ||
        !checkpoint || checkpoint < found->second.checkpoint) {
        error = "worker_replacement_invalid";
        return false;
    }
    if (!other_locations_admit(id, worker, checkpoint, error)) {
        return false;
    }
    for (const auto& entry : avatars_) {
        if (entry.second.location == id && entry.second.generation == UINT64_MAX) {
            error = "avatar_generation_exhausted";
            return false;
        }
    }
    // A prepared transfer has not fenced its source; replacing that source
    // revokes the old preparation and resumes only its checkpointed avatar.
    for (auto transfer = transfers_.begin(); transfer != transfers_.end();) {
        if (transfer->second.source == id && transfer->second.phase == TransferPhase::prepared) {
            transfer = transfers_.erase(transfer);
        } else {
            ++transfer;
        }
    }
    auto& location = found->second;
    location.worker = worker;
    location.generation = generation;
    location.checkpoint = checkpoint;
    location.active = true;
    for (auto& entry : avatars_) {
        auto& avatar = entry.second;
        if (avatar.location == id && !transfer_uncommitted(entry.first)) {
            ++avatar.generation;
            avatar.active = true;
            interests_[entry.first] = InterestLease{entry.first, id, avatar.generation, worker, generation};
        }
    }
    return true;
}

bool LocationLedger::rebind_global(GlobalSystem system, const GlobalLease& previous, const GlobalLease& replacement,
                                   std::string& error) {
    const auto found = globals_.find(system);
    if (found == globals_.end() || found->second.worker != previous.worker ||
        found->second.generation != previous.generation ||
        accepts_global_outcome(system, previous.worker, previous.generation) || !replacement.worker ||
        replacement.generation <= previous.generation) {
        error = "global_rebind_stale";
        return false;
    }
    if (!worker_active(replacement.worker, replacement.generation)) {
        error = "global_rebind_worker_inactive";
        return false;
    }
    found->second = replacement;
    return true;
}

bool LocationLedger::subscribe_interest(uint64_t player, std::string& error) {
    const auto* current = avatar(player);
    if (!current || !current->active) {
        error = "interest_avatar_inactive";
        return false;
    }
    const auto location = locations_.find(current->location);
    if (location == locations_.end() || !location->second.active) {
        error = "interest_location_inactive";
        return false;
    }
    const InterestLease lease{player, current->location, current->generation, location->second.worker,
                              location->second.generation};
    const auto existing = interests_.find(player);
    if (existing != interests_.end()) {
        if (existing->second.location == lease.location &&
            existing->second.avatar_generation == lease.avatar_generation && existing->second.worker == lease.worker &&
            existing->second.worker_generation == lease.worker_generation) {
            return true;
        }
        error = "interest_already_bound";
        return false;
    }
    if (interests_.size() >= max_avatars) {
        error = "interest_capacity";
        return false;
    }
    interests_.emplace(player, lease);
    return true;
}

bool LocationLedger::unsubscribe_interest(uint64_t player, std::string& error) {
    if (!interests_.erase(player)) {
        error = "interest_not_subscribed";
        return false;
    }
    return true;
}

bool LocationLedger::accepts_interest(uint64_t player, const LocationId& id, uint64_t avatar_generation,
                                      uint64_t worker, uint64_t worker_generation) const noexcept {
    const auto found = interests_.find(player);
    return found != interests_.end() && found->second.location == id &&
           found->second.avatar_generation == avatar_generation && found->second.worker == worker &&
           found->second.worker_generation == worker_generation &&
           accepts_avatar_outcome(player, id, avatar_generation, worker, worker_generation);
}

bool LocationLedger::claim_global_event(GlobalSystem system, uint64_t worker, uint64_t generation, uint64_t event_id,
                                        std::string& error) {
    if (!event_id || !accepts_global_outcome(system, worker, generation)) {
        error = "global_event_owner_stale";
        return false;
    }
    const auto existing = global_events_.find(system);
    if (existing != global_events_.end() && existing->second.count(event_id)) {
        error = "global_event_duplicate";
        return false;
    }
    if (global_event_count() >= max_global_events) {
        error = "global_event_capacity";
        return false;
    }
    global_events_[system].insert(event_id);
    return true;
}

bool LocationLedger::accepts_avatar_outcome(uint64_t player, const LocationId& id, uint64_t avatar_generation,
                                            uint64_t worker, uint64_t worker_generation) const noexcept {
    const auto* current = avatar(player);
    const auto location = locations_.find(id);
    return current && current->active && current->location == id && current->generation == avatar_generation &&
           location != locations_.end() && location->second.active && location->second.worker == worker &&
           location->second.generation == worker_generation;
}

bool LocationLedger::accepts_global_outcome(GlobalSystem system, uint64_t worker, uint64_t generation) const noexcept {
    const auto found = globals_.find(system);
    if (found == globals_.end() || found->second.worker != worker || found->second.generation != generation) {
        return false;
    }
    return worker_active(worker, generation);
}

const GlobalLease* LocationLedger::global_owner(GlobalSystem system) const noexcept {
    const auto found = globals_.find(system);
    return found == globals_.end() ? nullptr : &found->second;
}

bool LocationLedger::checkpoints_share(uint64_t revision) const noexcept {
    for (const auto& entry : locations_) {
        if (entry.second.checkpoint != revision) {
            return false;
        }
    }
    return true;
}

bool LocationLedger::content_matches(const Digest& digest) const noexcept {
    for (const auto& entry : locations_) {
        if (entry.second.content != digest) {
            return false;
        }
    }
    return true;
}

bool LocationLedger::locations_valid() const noexcept {
    for (const auto& entry : locations_) {
        const auto& lease = entry.second;
        if (!lease.id.valid() || !(entry.first == lease.id) || !lease.worker || !lease.generation ||
            !lease.checkpoint || lease.content == Digest{} || lease.native_load_slot > 1) {
            return false;
        }
        for (const auto& other : locations_) {
            if (&entry != &other &&
                (lease.worker == other.second.worker || lease.checkpoint != other.second.checkpoint)) {
                return false;
            }
        }
    }
    return true;
}

bool LocationLedger::avatars_valid() const noexcept {
    for (const auto& entry : avatars_) {
        const auto& lease = entry.second;
        const auto location = locations_.find(lease.location);
        if (!entry.first || entry.first != lease.player || !lease.generation || location == locations_.end() ||
            (lease.active && !location->second.active)) {
            return false;
        }
        if (!lease.active && location->second.active && !transfer_fenced(entry.first)) {
            return false;
        }
    }
    return true;
}

// What each phase of a transfer requires of its avatar and of the generations the transfer has recorded.
bool LocationLedger::transfer_phase_valid(const Transfer& transfer, const AvatarLease& lease) const noexcept {
    const auto phase = transfer.phase;
    if (phase == TransferPhase::prepared &&
        (!(lease.location == transfer.source) || lease.generation != transfer.source_generation ||
         transfer.destination_generation || (!lease.active && locations_.at(transfer.source).active))) {
        return false;
    }
    if ((phase == TransferPhase::source_fenced || phase == TransferPhase::destination_restored) &&
        (lease.active || !(lease.location == transfer.source) || lease.generation <= transfer.source_generation)) {
        return false;
    }
    if (phase == TransferPhase::committed &&
        (!(lease.location == transfer.destination) || !transfer.destination_generation ||
         (!lease.active && locations_.at(transfer.destination).active))) {
        return false;
    }
    if ((phase == TransferPhase::prepared || phase == TransferPhase::source_fenced) &&
        transfer.destination_generation) {
        return false;
    }
    if ((phase == TransferPhase::destination_restored || phase == TransferPhase::committed) &&
        !transfer.destination_generation) {
        return false;
    }
    return true;
}

bool LocationLedger::transfers_valid() const noexcept {
    for (const auto& entry : transfers_) {
        const auto& transfer = entry.second;
        const auto lease = avatars_.find(transfer.player);
        if (!entry.first || entry.first != transfer.id || entry.first > high_transfer_id_ || !known(transfer.phase) ||
            lease == avatars_.end() || !locations_.count(transfer.source) || !locations_.count(transfer.destination) ||
            transfer.source == transfer.destination || !transfer.source_generation) {
            return false;
        }
        if (!transfer_phase_valid(transfer, lease->second)) {
            return false;
        }
        for (const auto& other : transfers_) {
            if (&entry != &other && transfer.player == other.second.player) {
                return false;
            }
        }
    }
    return true;
}

bool LocationLedger::globals_valid() const noexcept {
    for (const auto& entry : globals_) {
        if (!known(entry.first) || !entry.second.worker || !entry.second.generation) {
            return false;
        }
    }
    return true;
}

bool LocationLedger::interests_valid() const noexcept {
    for (const auto& entry : interests_) {
        const auto& interest = entry.second;
        const auto lease = avatars_.find(entry.first);
        const auto location = locations_.find(interest.location);
        if (!entry.first || interest.player != entry.first || !interest.avatar_generation || !interest.worker ||
            !interest.worker_generation || lease == avatars_.end() || !lease->second.active ||
            !(lease->second.location == interest.location) || lease->second.generation != interest.avatar_generation ||
            location == locations_.end() || !location->second.active || location->second.worker != interest.worker ||
            location->second.generation != interest.worker_generation) {
            return false;
        }
    }
    return true;
}

bool LocationLedger::global_events_valid() const noexcept {
    for (const auto& entry : global_events_) {
        if (!known(entry.first) || entry.second.empty()) {
            return false;
        }
        for (const auto event_id : entry.second) {
            if (!event_id) {
                return false;
            }
        }
    }
    return true;
}

bool LocationLedger::valid() const noexcept {
    if (locations_.size() > max_locations || avatars_.size() > max_avatars || transfers_.size() > max_transfers ||
        globals_.size() > global_system_count || interests_.size() > max_avatars ||
        global_events_.size() > global_system_count || global_event_count() > max_global_events) {
        return false;
    }
    return locations_valid() && avatars_valid() && transfers_valid() && globals_valid() && interests_valid() &&
           global_events_valid();
}

std::vector<uint8_t> LocationLedger::snapshot() const {
    state::Writer writer;
    writer.word(magic);
    writer.word(3);
    writer.wide(high_transfer_id_);
    writer.word(static_cast<uint32_t>(locations_.size()));
    for (const auto& entry : locations_) {
        write_location(writer, entry.second);
    }
    writer.word(static_cast<uint32_t>(avatars_.size()));
    for (const auto& entry : avatars_) {
        write_avatar(writer, entry.second);
    }
    writer.word(static_cast<uint32_t>(transfers_.size()));
    for (const auto& entry : transfers_) {
        write_transfer(writer, entry.second);
    }
    writer.word(static_cast<uint32_t>(globals_.size()));
    for (const auto& entry : globals_) {
        write_global(writer, entry.first, entry.second);
    }
    writer.word(static_cast<uint32_t>(interests_.size()));
    for (const auto& entry : interests_) {
        write_interest(writer, entry.second);
    }
    writer.word(static_cast<uint32_t>(global_event_count()));
    for (const auto& entry : global_events_) {
        for (const auto event_id : entry.second) {
            write_global_event(writer, entry.first, event_id);
        }
    }
    return std::move(writer.bytes);
}

bool LocationLedger::restore(const std::vector<uint8_t>& bytes, std::string& error) {
    state::Reader reader{bytes};
    LocationLedger next;
    if (reader.word() != magic) {
        error = "location_ledger_schema";
        return false;
    }
    const auto schema = reader.word();
    if (schema != 1 && schema != 2 && schema != 3) {
        error = "location_ledger_schema";
        return false;
    }
    next.high_transfer_id_ = reader.wide();
    if (!read_locations(reader, schema, next.locations_, error) || !read_avatars(reader, next.avatars_, error) ||
        !read_transfers(reader, next.transfers_, error) || !read_globals(reader, next.globals_, error)) {
        return false;
    }
    if (schema >= 2 &&
        (!read_interests(reader, next.interests_, error) || !read_global_events(reader, next.global_events_, error))) {
        return false;
    }
    if (!reader.done() || !next.valid()) {
        error = "location_ledger_invalid";
        return false;
    }
    *this = std::move(next);
    return true;
}
} // namespace sporemp::network
