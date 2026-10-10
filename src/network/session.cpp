#include "session.h"
#include <utility>

namespace sporemp::network {
namespace {
bool same_secret(const Digest& a, const Digest& b) {
    uint32_t diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= uint32_t(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool is_content(Kind kind) {
    return kind == Kind::content_request || kind == Kind::content_response;
}

bool is_transfer(Kind kind) {
    return kind == Kind::transfer_command || kind == Kind::transfer_receipt;
}

// Whether a motion leaves an entity what it was: its kind, its native and herd numbers, its species and its
// archetype, and for most kinds its model.
bool same_identity(const Entity& before, const Entity& after) {
    // A nest's model is state: the original swaps it, for example when a player hunts that species
    // (trial239), and clients apply the new model key. For everything else the model is identity.
    // A fruit's too: the original gives a fruit the model of the place it puts it (trial410).
    const bool model_fixed = after.kind != EntityKind::nest && after.kind != EntityKind::fruit;
    return !(before.kind != after.kind ||
             (model_fixed && (before.model_instance != after.model_instance || before.model_type != after.model_type ||
                              before.model_group != after.model_group)) ||
             before.native_id != after.native_id || before.herd_native_id != after.herd_native_id ||
             before.species_instance != after.species_instance || before.species_type != after.species_type ||
             before.species_group != after.species_group || before.archetype != after.archetype);
}
} // namespace

Session::Session(SessionConfig config, ContentControl* shared_content, const LocationLedger* leases)
    : config_(config),
      content_storage_(config_.identity.content, config_.identity.world),
      content_(shared_content ? shared_content : &content_storage_),
      leases_(leases) {
    if (!capture_location_lease()) {
        lease_fenced_ = true;
    }
    if (config_.maintenance_transfer) {
        begin_transfer_maintenance(config_.maintenance_transfer);
    }
}

bool Session::capture_location_lease() noexcept {
    if (!leases_) {
        return true;
    }
    const auto location = leases_->location_leases().find(config_.location);
    if (location == leases_->location_leases().end()) {
        return false;
    }
    lease_worker_ = location->second.worker;
    interested_owners_ = 0;
    avatar_generations_ = {};
    for (size_t player = 1; player <= 2; ++player) {
        const auto* avatar = leases_->avatar(player);
        if (avatar && avatar->active && avatar->location == config_.location) {
            avatar_generations_[player - 1] = avatar->generation;
            if (leases_->accepts_interest(player, config_.location, avatar->generation, lease_worker_,
                                          config_.worker_generation)) {
                interested_owners_ |= static_cast<uint8_t>(1u << player);
            }
        }
    }
    return lease_current();
}

bool Session::lease_current() const noexcept {
    if (lease_fenced_) {
        return false;
    }
    if (!leases_) {
        return true;
    }
    const auto location = leases_->location_leases().find(config_.location);
    if (location == leases_->location_leases().end()) {
        return false;
    }
    const auto& lease = location->second;
    if (!lease.active || lease.worker != lease_worker_ || lease.generation != config_.worker_generation ||
        lease.checkpoint != config_.checkpoint_revision || lease.content != config_.identity.content ||
        lease.native_load_slot != config_.native_load_slot) {
        return false;
    }
    uint8_t owners = 0, interested = 0;
    for (size_t player = 1; player <= 2; ++player) {
        const auto* avatar = leases_->avatar(player);
        uint64_t generation = 0;
        if (avatar && avatar->active && avatar->location == config_.location) {
            generation = avatar->generation;
            owners |= static_cast<uint8_t>(1u << player);
            if (leases_->accepts_interest(player, config_.location, generation, lease_worker_,
                                          config_.worker_generation)) {
                interested |= static_cast<uint8_t>(1u << player);
            }
        }
        if (generation != avatar_generations_[player - 1]) {
            return false;
        }
    }
    const auto* global = leases_->global_owner(GlobalSystem::economy);
    const bool content_authority =
        global && global->worker == lease_worker_ && global->generation == config_.worker_generation;
    return owners == config_.permitted_owners && interested == interested_owners_ &&
           content_authority == config_.content_authority;
}

const Entity* Session::owned_entity(uint64_t player) const noexcept {
    for (const auto& entry : entities_) {
        if (entry.second.owner == player) {
            return &entry.second;
        }
    }
    return nullptr;
}

// No player connection holds a baseline any more: each must be sent a new one and acknowledge it.
void Session::withdraw_baselines() {
    for (auto& entry : connections_) {
        if (!entry.second.authority) {
            entry.second.ready = false;
            entry.second.baseline = 0;
        }
    }
}

bool Session::begin_transfer_maintenance(uint64_t transfer, uint8_t native_owners) {
    if (!transfer || !lease_current() || (maintenance_ && maintenance_ != transfer)) {
        return false;
    }
    if (!maintenance_) {
        maintenance_owners_ = native_owners == 0xff ? config_.permitted_owners : native_owners;
    }
    maintenance_ = transfer;
    withdraw_baselines();
    staging_.clear();
    tombstones_.clear();
    building_ = false;
    return true;
}

bool Session::rebind_transfer_maintenance(uint64_t transfer, bool release) {
    if (!transfer || maintenance_ != transfer || lease_fenced_ || !leases_) {
        return false;
    }
    const auto location = leases_->location_leases().find(config_.location);
    if (location == leases_->location_leases().end() || !location->second.active ||
        location->second.worker != lease_worker_ || location->second.generation != config_.worker_generation) {
        return false;
    }
    config_.permitted_owners = 0;
    for (size_t player = 1; player <= 2; ++player) {
        const auto* avatar = leases_->avatar(player);
        if (avatar && avatar->active && avatar->location == config_.location) {
            config_.permitted_owners |= static_cast<uint8_t>(1u << player);
        }
    }
    if (!capture_location_lease()) {
        return false;
    }
    entities_.clear();
    staging_.clear();
    tombstones_.clear();
    building_ = false;
    withdraw_baselines();
    if (release) {
        maintenance_ = 0;
        maintenance_owners_ = 0;
        config_.maintenance_transfer = 0;
        complete_ = false;
    }
    return true;
}

std::vector<Dispatch> Session::transfer_command(const TransferFrame& frame) {
    std::vector<Dispatch> out;
    if (maintenance_ != frame.id || !authority_control_ready()) {
        return out;
    }
    Packet packet;
    packet.kind = Kind::transfer_command;
    packet.role = Role::authority;
    packet.scene = scene_;
    packet.baseline = source_baseline_;
    packet.transfer = frame;
    emit(out, authority_, packet);
    return out;
}

void Session::clear_scene() noexcept {
    authority_ = 0;
    complete_ = false;
    building_ = false;
    staging_.clear();
    entities_.clear();
    tombstones_.clear();
    tutorials_.clear();
    deaths_.clear();
    hatches_.clear();
    scene_ = 0;
    source_baseline_ = 0;
    staging_scene_ = 0;
    staging_baseline_ = 0;
    expected_ = 0;
}

void Session::fence_lease() {
    if (lease_fenced_) {
        return;
    }
    lease_fenced_ = true;
    for (const auto& connection : connections_) {
        if (!connection.second.authority || config_.content_authority) {
            content_->disconnect(connection.first);
        }
    }
    connections_.clear();
    clear_scene();
    next_baseline_ = 0;
}

bool Session::connect(uint64_t connection_id) {
    if (!lease_current()) {
        fence_lease();
        return false;
    }
    if (!connection_id || connections_.size() >= 8 || connections_.count(connection_id)) {
        return false;
    }
    connections_.emplace(connection_id, Connection{});
    return true;
}

bool Session::can_rotate_disconnected_authority(uint64_t worker_generation, const Digest& credential) const noexcept {
    return !(lease_fenced_ || authority_ || complete_ || building_ || worker_generation <= config_.worker_generation ||
             credential == Digest{} || same_secret(credential, config_.player1) ||
             same_secret(credential, config_.player2) || same_secret(credential, config_.authority));
}

bool Session::rotate_disconnected_authority(uint64_t worker_generation, const Digest& credential) {
    if (!can_rotate_disconnected_authority(worker_generation, credential)) {
        return false;
    }
    config_.worker_generation = worker_generation;
    config_.authority = credential;
    if (!capture_location_lease()) {
        fence_lease();
        return false;
    }
    return true;
}

// What every packet this session sends carries: its epoch, the next sequence of the recipient's stream, and the
// location, worker generation and native load slot it serves.
void Session::stamp(Connection& connection, Packet& packet) const {
    packet.session = config_.epoch;
    packet.sequence = ++connection.out_sequence;
    packet.location = config_.location;
    packet.worker_generation = config_.worker_generation;
    packet.native_load_slot = config_.native_load_slot;
}

bool Session::recipient_scope(uint64_t connection_id, Packet& packet) {
    if (!lease_current()) {
        fence_lease();
        return false;
    }
    auto found = connections_.find(connection_id);
    if (found == connections_.end() || !found->second.authenticated || packet.kind != Kind::content_response) {
        return false;
    }
    stamp(found->second, packet);
    return true;
}

void Session::emit(std::vector<Dispatch>& out, uint64_t connection_id, Packet packet, bool close) {
    auto found = connections_.find(connection_id);
    if (found == connections_.end()) {
        if (packet.kind == Kind::content_response) {
            out.push_back({connection_id, packet, close, true});
        }
        return;
    }
    stamp(found->second, packet);
    out.push_back({connection_id, packet, close});
}

// A rejection carries the kind (target) and the player (target_generation) of the message it refuses;
// both fields are otherwise unused in a rejection.
void Session::reject(std::vector<Dispatch>& out, uint64_t connection_id, Error error, bool close) {
    Packet rejection;
    rejection.kind = Kind::reject;
    rejection.error = error;
    if (handling_) {
        rejection.target = static_cast<uint64_t>(handling_->kind);
        rejection.target_generation = handling_->player;
    }
    emit(out, connection_id, rejection, close);
}

void Session::baseline(std::vector<Dispatch>& out, uint64_t connection_id) {
    auto& connection = connections_.at(connection_id);
    connection.ready = false;
    if (!complete_) {
        return;
    }
    connection.baseline = ++next_baseline_;
    Packet packet;
    packet.kind = Kind::scene_begin;
    packet.scene = scene_;
    packet.baseline = connection.baseline;
    packet.count = uint32_t(entities_.size());
    emit(out, connection_id, packet);
    for (const auto& entry : entities_) {
        packet.kind = Kind::entity;
        packet.entity = entry.second;
        emit(out, connection_id, packet);
    }
    packet.kind = Kind::scene_end;
    packet.entity = {};
    emit(out, connection_id, packet);
}

void Session::publish(std::vector<Dispatch>& out, const Packet& packet) {
    for (const auto& entry : connections_) {
        const auto& connection = entry.second;
        if (connection.authenticated && !connection.authority && connection.baseline) {
            Packet copy = packet;
            copy.baseline = connection.baseline;
            emit(out, entry.first, copy);
        }
    }
}

// An authority's packet about one player goes to that player's connections, each under its own baseline:
// to those that have acknowledged it when `only_ready`, otherwise to all.
void Session::send_to_player(std::vector<Dispatch>& out, const Packet& packet, bool only_ready) {
    for (const auto& entry : connections_) {
        if (entry.second.authenticated && !entry.second.authority && (!only_ready || entry.second.ready) &&
            entry.second.player == packet.player) {
            Packet copy = packet;
            copy.baseline = entry.second.baseline;
            emit(out, entry.first, copy);
        }
    }
}

std::vector<Dispatch> Session::disconnect(uint64_t connection_id) {
    std::vector<Dispatch> out;
    auto found = connections_.find(connection_id);
    if (found != connections_.end() && (!found->second.authority || config_.content_authority)) {
        content_->disconnect(connection_id);
    }
    connections_.erase(connection_id);
    if (authority_ == connection_id) {
        clear_scene();
        // The disconnected transport can no longer publish. A replacement that
        // loads the same selected native save starts its scene/baseline counters
        // again; the old counters must not reject that fresh complete baseline.
        // Keep each player's next_baseline_ monotonic for outstanding ACK fencing.
        for (auto& entry : connections_) {
            entry.second.ready = false;
            entry.second.baseline = 0;
            reject(out, entry.first, Error::authority_lost);
        }
    }
    return out;
}

// One packet from one connection, through the checks every packet passes and then to the part that handles its
// sender and kind. Whatever is answered or forwarded is returned in order.
std::vector<Dispatch> Session::receive(uint64_t connection_id, const Packet& packet) {
    struct Handling {
        const Packet*& slot;

        ~Handling() {
            slot = nullptr;
        }
    } handling{handling_};

    handling_ = &packet;
    std::vector<Dispatch> out;
    if (!lease_current()) {
        fence_lease();
        return out;
    }
    auto found = connections_.find(connection_id);
    if (found == connections_.end()) {
        return out;
    }
    auto& connection = found->second;
    if (!admit_stream(out, connection_id, connection, packet)) {
        return out;
    }
    if (!connection.authenticated) {
        receive_hello(out, connection_id, connection, packet);
        return out;
    }
    if (!admit_epoch(out, connection_id, connection, packet)) {
        return out;
    }
    if (packet.kind == Kind::ping) {
        Packet reply;
        reply.kind = Kind::ping;
        emit(out, connection_id, reply);
    } else if (is_transfer(packet.kind)) {
        receive_transfer(out, connection_id, connection, packet);
    } else if (maintenance_) {
        receive_in_maintenance(out, connection_id, connection, packet);
    } else if (packet.kind == Kind::content_request) {
        receive_content_request(out, connection_id, connection, packet);
    } else if (connection.authority) {
        receive_from_authority(out, connection_id, packet);
    } else {
        receive_from_player(out, connection_id, connection, packet);
    }
    return out;
}

// A packet advances its connection's sequence and, unless it is a content or transfer packet, names this session's
// location and native load slot.
bool Session::admit_stream(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                           const Packet& packet) {
    if (!packet.sequence || packet.sequence <= connection.sequence) {
        reject(out, connection_id, Error::stale, true);
        return false;
    }
    connection.sequence = packet.sequence;
    if (!is_content(packet.kind) && !is_transfer(packet.kind) &&
        (!(packet.location == config_.location) || packet.native_load_slot != config_.native_load_slot)) {
        reject(out, connection_id, Error::stale, true);
        return false;
    }
    return true;
}

// An authenticated packet names this session's epoch and, unless it is a content or transfer packet, the worker
// generation of its sender: the session's for the authority, none for a player.
bool Session::admit_epoch(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                          const Packet& packet) {
    if (!is_content(packet.kind) && !is_transfer(packet.kind) &&
        packet.worker_generation != (connection.authority ? config_.worker_generation : 0)) {
        reject(out, connection_id, Error::stale, true);
        return false;
    }
    if (packet.session != config_.epoch) {
        reject(out, connection_id, Error::stale, true);
        return false;
    }
    return true;
}

// Whether a hello is for this session: its maintenance transfer, its worker generation and its whole identity.
bool Session::hello_matches_session(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    if (packet.kind != Kind::hello || packet.session) {
        reject(out, connection_id, Error::authentication, true);
        return false;
    }
    if (packet.maintenance_transfer != (packet.role == Role::authority ? config_.maintenance_transfer : 0) ||
        (packet.maintenance_transfer && (!packet.scene || packet.baseline))) {
        reject(out, connection_id, Error::stale, true);
        return false;
    }
    if ((packet.role == Role::authority && packet.worker_generation != config_.worker_generation) ||
        (packet.role == Role::player && packet.worker_generation)) {
        reject(out, connection_id, Error::stale, true);
        return false;
    }
    if (packet.identity.build != config_.identity.build || packet.identity.executable != config_.identity.executable ||
        packet.identity.content != config_.identity.content) {
        reject(out, connection_id, Error::incompatible, true);
        return false;
    }
    for (size_t index = 0; index < world_file_count; ++index) {
        if (packet.identity.world[index] == Digest{} || config_.identity.world[index] == Digest{} ||
            packet.identity.world[index] != config_.identity.world[index]) {
            Packet rejection;
            rejection.kind = Kind::reject;
            rejection.error = Error::world_mismatch;
            rejection.world_index = static_cast<uint32_t>(index);
            emit(out, connection_id, rejection, true);
            return false;
        }
    }
    if (packet.identity.fixture != config_.identity.fixture) {
        reject(out, connection_id, Error::incompatible, true);
        return false;
    }
    return true;
}

// Which player a hello's credential belongs to. A player is not admitted during transfer maintenance or where the
// session does not permit it, and an earlier connection of the same player is closed.
bool Session::authenticate_player(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet,
                                  uint64_t& player) {
    if (maintenance_) {
        reject(out, connection_id, Error::not_ready, true);
        return false;
    }
    if (same_secret(packet.credential, config_.player1)) {
        player = 1;
    } else if (same_secret(packet.credential, config_.player2)) {
        player = 2;
    } else {
        reject(out, connection_id, Error::authentication, true);
        return false;
    }
    if (!(config_.permitted_owners & (1u << player))) {
        reject(out, connection_id, Error::ownership, true);
        return false;
    }
    for (auto& old : connections_) {
        if (old.first != connection_id && old.second.authenticated && !old.second.authority &&
            old.second.player == player) {
            reject(out, old.first, Error::stale, true);
            old.second.authenticated = false;
            old.second.ready = false;
        }
    }
    return true;
}

void Session::send_welcome(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection) {
    Packet packet;
    packet.kind = Kind::welcome;
    packet.player = connection.player;
    packet.role = connection.authority ? Role::authority : Role::player;
    packet.identity = config_.identity;
    packet.request = config_.checkpoint_revision;
    packet.maintenance_transfer = connection.authority ? config_.maintenance_transfer : 0;
    packet.scene = packet.maintenance_transfer ? scene_ : 0;
    emit(out, connection_id, packet);
}

void Session::receive_hello(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                            const Packet& packet) {
    if (!hello_matches_session(out, connection_id, packet)) {
        return;
    }
    uint64_t player = 0;
    if (packet.role == Role::authority) {
        if (!same_secret(packet.credential, config_.authority)) {
            reject(out, connection_id, Error::authentication, true);
            return;
        }
        if (authority_) {
            reject(out, connection_id, Error::ownership, true);
            return;
        }
        connection.authority = true;
    } else if (!authenticate_player(out, connection_id, packet, player)) {
        return;
    }
    if ((!connection.authority || config_.content_authority) &&
        !content_->connect(connection_id, player, connection.authority)) {
        reject(out, connection_id, Error::capacity, true);
        return;
    }
    if (connection.authority) {
        authority_ = connection_id;
        if (config_.maintenance_transfer) {
            scene_ = packet.scene;
            source_baseline_ = 0;
        }
    }
    connection.authenticated = true;
    connection.player = player;
    send_welcome(out, connection_id, connection);
    if (!connection.authority) {
        baseline(out, connection_id);
    }
}

// The one transfer packet a session receives: the authority's receipt for the transfer under maintenance.
void Session::receive_transfer(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                               const Packet& packet) {
    if (!connection.authority || packet.role != Role::authority || packet.kind != Kind::transfer_receipt ||
        !maintenance_ || packet.transfer.id != maintenance_) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    if (!authority_control_ready() || packet.scene != scene_ || packet.baseline != source_baseline_) {
        reject(out, connection_id, Error::stale);
        return;
    }
    if (!transfer_receipt_ || !transfer_receipt_(packet)) {
        reject(out, connection_id, Error::not_ready);
    }
}

// The authority's well-formed frames for one player, of this scene and baseline, that maintenance neither delivers
// nor refuses.
bool Session::withheld_in_maintenance(const Connection& connection, const Packet& packet) const {
    if (connection.authority && packet.role == Role::authority && packet.kind == Kind::relationship &&
        packet.scene == scene_ && packet.baseline == source_baseline_ &&
        valid_relationship_frame(packet.relationship) && packet.player == 2 && !packet.count &&
        packet.error == Error::none) {
        return true;
    }
    if (connection.authority && packet.role == Role::authority && packet.kind == Kind::history &&
        packet.scene == scene_ && packet.baseline == source_baseline_ && valid_history_frame(packet.history) &&
        packet.player == 2 && !packet.count && packet.error == Error::none) {
        return true;
    }
    if (connection.authority && packet.role == Role::authority && packet.kind == Kind::hatch_view &&
        packet.scene == scene_ && packet.baseline == source_baseline_ && valid_hatch(packet.hatch) &&
        packet.player >= 1 && packet.player <= 2) {
        return true;
    }
    if (connection.authority && packet.role == Role::authority && packet.kind == Kind::inventory &&
        packet.scene == scene_ && packet.baseline == source_baseline_ && valid_inventory_frame(packet.inventory) &&
        packet.player >= 1 && packet.player <= 2) {
        return true;
    }
    if (connection.authority && packet.kind == Kind::death && packet.role == Role::authority &&
        packet.scene == scene_ && packet.baseline == source_baseline_ && valid_death(packet.death) &&
        packet.player >= 1 && packet.player <= 2) {
        return true;
    }
    // An owner notification already in flight is retained by its native
    // producer. Do not deliver it while transfer maintenance fences play.
    if (connection.authority && packet.kind == Kind::tutorial && packet.scene == scene_ &&
        packet.baseline == source_baseline_ && valid_tutorial(packet.tutorial) && packet.player >= 1 &&
        packet.player <= 2) {
        return true;
    }
    return connection.authority && packet.role == Role::authority && packet.kind == Kind::social_view &&
           packet.scene == scene_ && packet.baseline == source_baseline_ && valid_social_frame(packet.social) &&
           !packet.count && packet.error == Error::none && packet.player >= 1 && packet.player <= 2;
}

// An authority capture already queued on the transport can arrive
// after the coordinator sends its fence command. Drain only ordinary
// output from that exact completed baseline. Rejecting it would make
// the native peer quarantine its valid, newly acquired pause. None of
// these values may update caches, readiness, interest or subscribers.
void Session::drain_fenced_output(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    if (packet.scene != scene_ || packet.baseline != source_baseline_) {
        reject(out, connection_id, Error::stale);
        return;
    }
    if (packet.count || !valid_entity(packet.entity)) {
        reject(out, connection_id, Error::malformed);
        return;
    }
    if ((packet.entity.owner && !(maintenance_owners_ & (1u << packet.entity.owner))) ||
        (packet.kind == Kind::action_result && (packet.player < 1 || packet.player > 2 ||
                                                !(maintenance_owners_ & (1u << packet.player)) || !packet.request))) {
        reject(out, connection_id, Error::ownership);
    }
}

void Session::receive_in_maintenance(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                                     const Packet& packet) {
    if (withheld_in_maintenance(connection, packet)) {
        return;
    }
    const bool ordinary = packet.kind == Kind::motion || packet.kind == Kind::entity || packet.kind == Kind::despawn ||
                          packet.kind == Kind::action_result;
    if (connection.authority && !config_.maintenance_transfer && complete_ && ordinary) {
        drain_fenced_output(out, connection_id, packet);
        return;
    }
    reject(out, connection_id, Error::not_ready);
}

void Session::receive_content_request(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                                      const Packet& packet) {
    if (connection.authority && !config_.content_authority) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    ContentFrame frame;
    if (!decode_content(packet.content, frame)) {
        reject(out, connection_id, Error::malformed, true);
        return;
    }
    const auto now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    for (const auto& response : content_->receive(connection_id, frame, static_cast<uint64_t>(now))) {
        Packet reply;
        reply.kind = Kind::content_response;
        if (!encode_content(response.frame, reply.content)) {
            reject(out, connection_id, Error::malformed, true);
            return;
        }
        emit(out, response.connection, reply);
    }
}

void Session::receive_from_authority(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    if (packet.kind == Kind::scene_begin) {
        receive_scene_begin(out, connection_id, packet);
        return;
    }
    if (building_) {
        receive_while_building(out, connection_id, packet);
        return;
    }
    if (!complete_) {
        reject(out, connection_id, Error::not_ready);
        return;
    }
    if (packet.scene != scene_ || packet.baseline != source_baseline_) {
        reject(out, connection_id, Error::stale);
        return;
    }
    switch (packet.kind) {
    case Kind::hatch_view:
        return receive_hatch_view(out, connection_id, packet);
    case Kind::death:
        return receive_death(out, connection_id, packet);
    case Kind::relationship:
        return receive_relationship(out, connection_id, packet);
    case Kind::history:
        return receive_history(out, connection_id, packet);
    case Kind::inventory:
        return receive_inventory(out, connection_id, packet);
    case Kind::social_view:
        return receive_social_view(out, connection_id, packet);
    case Kind::tutorial:
        return receive_tutorial(out, connection_id, packet);
    case Kind::entity:
        return receive_entity(out, connection_id, packet);
    case Kind::motion:
    case Kind::despawn:
        return receive_motion_or_despawn(out, connection_id, packet);
    case Kind::action_result:
        return receive_action_result(out, connection_id, packet);
    default:
        return reject(out, connection_id, Error::ownership);
    }
}

// A new baseline replaces the scene: it is staged apart, and until it completes no player holds a baseline.
void Session::receive_scene_begin(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    if (!packet.scene || !packet.baseline || packet.count > max_entities || packet.scene < scene_ ||
        (packet.scene == scene_ && packet.baseline <= source_baseline_)) {
        reject(out, connection_id, Error::stale, true);
        return;
    }
    content_->fence();
    building_ = true;
    complete_ = false;
    staging_.clear();
    tutorials_.clear();
    deaths_.clear();
    hatches_.clear();
    staging_scene_ = packet.scene;
    staging_baseline_ = packet.baseline;
    expected_ = packet.count;
    withdraw_baselines();
}

// Between scene_begin and scene_end only the entities of that baseline are admitted.
void Session::receive_while_building(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    if (packet.scene != staging_scene_ || packet.baseline != staging_baseline_) {
        reject(out, connection_id, Error::stale, true);
        return;
    }
    const auto owners = maintenance_ ? maintenance_owners_ : config_.permitted_owners;
    if (packet.kind == Kind::entity) {
        stage_entity(out, connection_id, packet, owners);
        return;
    }
    if (packet.kind == Kind::scene_end) {
        complete_scene(out, connection_id, packet, owners);
        return;
    }
    reject(out, connection_id, Error::not_ready, true);
}

void Session::stage_entity(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet, uint8_t owners) {
    if (!valid_entity(packet.entity) || staging_.size() >= expected_ || staging_.count(packet.entity.id)) {
        reject(out, connection_id, Error::malformed, true);
        return;
    }
    if (packet.entity.owner && !(owners & (1u << packet.entity.owner))) {
        reject(out, connection_id, Error::ownership, true);
        return;
    }
    staging_.emplace(packet.entity.id, packet.entity);
}

// The staged baseline becomes the scene when it holds the announced count and, unless it is empty, one entity for
// each owner in `owners` and none for any other. Outside maintenance every authenticated player is then sent it.
void Session::complete_scene(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet, uint8_t owners) {
    if (staging_.size() != expected_ || packet.count != expected_) {
        reject(out, connection_id, Error::malformed, true);
        return;
    }
    size_t owner1 = 0, owner2 = 0;
    for (const auto& entry : staging_) {
        owner1 += entry.second.owner == 1;
        owner2 += entry.second.owner == 2;
    }
    if (expected_ && (owner1 != size_t((owners >> 1) & 1) || owner2 != size_t((owners >> 2) & 1))) {
        reject(out, connection_id, Error::ownership, true);
        return;
    }
    entities_ = std::move(staging_);
    tombstones_.clear();
    scene_ = staging_scene_;
    source_baseline_ = staging_baseline_;
    building_ = false;
    complete_ = true;
    if (!maintenance_) {
        for (const auto& entry : connections_) {
            if (entry.second.authenticated && !entry.second.authority) {
                baseline(out, entry.first);
            }
        }
    }
}

void Session::receive_hatch_view(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto actor = entities_.find(packet.hatch.entity);
    if (packet.role != Role::authority || packet.player < 1 || packet.player > 2 || !valid_hatch(packet.hatch) ||
        actor == entities_.end() || actor->second.owner != packet.player ||
        actor->second.generation != packet.hatch.generation || actor->second.life_state || actor->second.health <= 0) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    const auto prior = hatches_.find(packet.player);
    if (prior != hatches_.end() && packet.hatch.serial < prior->second.serial) {
        return;
    }
    if (prior != hatches_.end() && packet.hatch.serial == prior->second.serial && !(packet.hatch == prior->second)) {
        reject(out, connection_id, Error::malformed);
        return;
    }
    hatches_[packet.player] = packet.hatch;
    send_to_player(out, packet, true);
}

void Session::receive_death(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto actor = entities_.find(packet.death.entity);
    if (packet.role != Role::authority || packet.player < 1 || packet.player > 2 || !valid_death(packet.death) ||
        actor == entities_.end() || actor->second.owner != packet.player ||
        actor->second.generation != packet.death.generation || !actor->second.life_state || actor->second.health != 0) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    const auto previous = deaths_.find(packet.player);
    if (previous != deaths_.end() && !(previous->second == packet.death) &&
        packet.death.occurrence <= previous->second.occurrence) {
        reject(out, connection_id, Error::stale);
        return;
    }
    deaths_[packet.player] = packet.death;
    send_to_player(out, packet, true);
}

void Session::receive_relationship(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto actor = entities_.find(packet.relationship.entity);
    if (packet.role != Role::authority || packet.player != 2 || !valid_relationship_frame(packet.relationship) ||
        packet.count || packet.error != Error::none || actor == entities_.end() ||
        actor->second.owner != packet.player || actor->second.generation != packet.relationship.generation) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    // Complete state, republished by the authority. Only the owner's ready
    // connection receives it; the other player never sees B's relationships.
    send_to_player(out, packet, true);
}

void Session::receive_history(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto actor = entities_.find(packet.history.entity);
    if (packet.role != Role::authority || packet.player != 2 || !valid_history_frame(packet.history) || packet.count ||
        packet.error != Error::none || actor == entities_.end() || actor->second.owner != packet.player ||
        actor->second.generation != packet.history.generation) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    // Full snapshots are republished by the authority. A fresh subscriber
    // receives them only after its new entity baseline is ready; there is
    // no gameplay-event replay or retained native target requirement.
    send_to_player(out, packet, true);
}

void Session::receive_inventory(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto actor = entities_.find(packet.inventory.entity);
    if (packet.role != Role::authority || packet.player < 1 || packet.player > 2 ||
        !valid_inventory_frame(packet.inventory) || actor == entities_.end() || actor->second.owner != packet.player ||
        actor->second.generation != packet.inventory.generation) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    send_to_player(out, packet, true);
}

void Session::receive_social_view(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto actor = entities_.find(packet.social.entity), target = entities_.find(packet.social.target);
    if (packet.role != Role::authority || packet.player < 1 || packet.player > 2 ||
        !valid_social_frame(packet.social) || actor == entities_.end() || target == entities_.end() ||
        actor->second.owner != packet.player || actor->second.generation != packet.social.generation ||
        target->second.generation != packet.social.target_generation || target->second.owner ||
        actor->second.life_state || actor->second.health <= 0 || target->second.life_state ||
        target->second.health <= 0) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    send_to_player(out, packet, true);
}

void Session::receive_tutorial(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto actor = entities_.find(packet.tutorial.entity);
    if (packet.role != Role::authority || packet.player < 1 || packet.player > 2 || !valid_tutorial(packet.tutorial) ||
        actor == entities_.end() || actor->second.owner != packet.player ||
        actor->second.generation != packet.tutorial.generation || actor->second.life_state ||
        actor->second.health <= 0) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    const auto key = tutorial_key(packet.player, packet.tutorial.token);
    const auto previous = tutorials_.find(key);
    if (previous != tutorials_.end() && !(previous->second == packet.tutorial)) {
        reject(out, connection_id, Error::stale);
        return;
    }
    tutorials_[key] = packet.tutorial;
    send_to_player(out, packet, true);
}

// An entity that joins the complete scene: unknown, newer than its tombstone, and no second creature for an owner.
void Session::receive_entity(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto existing = entities_.find(packet.entity.id);
    const auto tombstone = tombstones_.find(packet.entity.id);
    if (!valid_entity(packet.entity) || existing != entities_.end() ||
        (tombstone != tombstones_.end() && packet.entity.generation <= tombstone->second) ||
        entities_.size() >= max_entities) {
        reject(out, connection_id, Error::stale);
        return;
    }
    if (packet.entity.owner) {
        if (!(config_.permitted_owners & (1u << packet.entity.owner))) {
            reject(out, connection_id, Error::ownership, true);
            return;
        }
        for (const auto& live : entities_) {
            if (live.second.owner == packet.entity.owner) {
                reject(out, connection_id, Error::ownership);
                return;
            }
        }
    }
    entities_.emplace(packet.entity.id, packet.entity);
    publish(out, packet);
}

// A motion replaces the entity's values and may not change what it is. False once it has been refused.
bool Session::apply_motion(std::vector<Dispatch>& out, uint64_t connection_id, Entity& entity, const Packet& packet) {
    if (!valid_entity(packet.entity)) {
        reject(out, connection_id, Error::malformed);
        return false;
    }
    if (!same_identity(entity, packet.entity)) {
        reject(out, connection_id, Error::ownership);
        return false;
    }
    entity = packet.entity;
    return true;
}

// Both name a live entity by its generation and owner at a later tick. A despawn leaves a tombstone holding the
// generation it ended.
void Session::receive_motion_or_despawn(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    const auto existing = entities_.find(packet.entity.id);
    if (existing == entities_.end() || existing->second.generation != packet.entity.generation ||
        existing->second.owner != packet.entity.owner || packet.entity.tick <= existing->second.tick) {
        reject(out, connection_id, Error::stale);
        return;
    }
    if (packet.kind == Kind::motion) {
        if (!apply_motion(out, connection_id, existing->second, packet)) {
            return;
        }
    } else {
        if (tombstones_.size() >= max_entities && !tombstones_.count(packet.entity.id)) {
            reject(out, connection_id, Error::capacity, true);
            return;
        }
        tombstones_[packet.entity.id] = packet.entity.generation;
        entities_.erase(existing);
    }
    publish(out, packet);
}

void Session::receive_action_result(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet) {
    if (packet.player < 1 || packet.player > 2 || !(config_.permitted_owners & (1u << packet.player))) {
        reject(out, connection_id, Error::ownership, true);
        return;
    }
    send_to_player(out, packet, false);
}

void Session::receive_from_player(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                                  const Packet& packet) {
    switch (packet.kind) {
    case Kind::baseline_ack:
        return receive_baseline_ack(out, connection_id, connection, packet);
    case Kind::hatch_input:
        return receive_hatch_input(out, connection_id, connection, packet);
    case Kind::death_done:
        return receive_death_done(out, connection_id, connection, packet);
    case Kind::tutorial_done:
        return receive_tutorial_done(out, connection_id, connection, packet);
    case Kind::action:
        return receive_action(out, connection_id, connection, packet);
    default:
        return reject(out, connection_id, Error::ownership);
    }
}

void Session::receive_baseline_ack(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                                   const Packet& packet) {
    // ACKs already in flight when an authority replaces a baseline are
    // obsolete, not a connection failure. They cannot restore readiness.
    if (!complete_ || !connection.baseline || packet.scene < scene_ ||
        (packet.scene == scene_ && packet.baseline < connection.baseline)) {
        return;
    }
    if (packet.scene != scene_ || packet.baseline != connection.baseline) {
        reject(out, connection_id, Error::stale);
        return;
    }
    connection.ready = true;
}

// A player's actions and hatch inputs share one count for each second; past 128 the connection is closed.
bool Session::within_action_rate(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection) {
    const auto now = std::chrono::steady_clock::now();
    if (now - connection.action_window >= std::chrono::seconds(1)) {
        connection.action_window = now;
        connection.actions = 0;
    }
    if (++connection.actions > 128) {
        reject(out, connection_id, Error::capacity, true);
        return false;
    }
    return true;
}

// Whether a player's packet names the scene and the baseline its connection has acknowledged, with an authority
// to receive it.
bool Session::on_ready_baseline(const Connection& connection, const Packet& packet) const {
    return !(!authority_ || !complete_ || !connection.ready || packet.scene != scene_ ||
             packet.baseline != connection.baseline);
}

// A player's confirmation reaches the authority under the authenticated player and the authority's own baseline.
void Session::confirm_to_authority(std::vector<Dispatch>& out, const Connection& connection, const Packet& packet) {
    Packet receipt = packet;
    receipt.player = connection.player;
    receipt.baseline = source_baseline_;
    emit(out, authority_, receipt);
}

void Session::receive_hatch_input(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                                  const Packet& packet) {
    if (!within_action_rate(out, connection_id, connection)) {
        return;
    }
    if (!on_ready_baseline(connection, packet)) {
        return;
    }
    const auto offered = hatches_.find(connection.player);
    const auto actor = entities_.find(packet.hatch.entity);
    if (packet.role != Role::player || packet.player != connection.player || !valid_hatch(packet.hatch, true) ||
        actor == entities_.end() || actor->second.owner != connection.player ||
        actor->second.generation != packet.hatch.generation) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    if (offered == hatches_.end() || !same_hatch_offer(offered->second, packet.hatch) || actor->second.life_state ||
        actor->second.health <= 0) {
        return;
    }
    Packet input = packet;
    input.baseline = source_baseline_;
    emit(out, authority_, input);
}

void Session::receive_death_done(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                                 const Packet& packet) {
    if (!on_ready_baseline(connection, packet)) {
        return;
    }
    const auto offered = deaths_.find(connection.player);
    const auto actor = entities_.find(packet.death.entity);
    if (packet.role != Role::player || packet.player != connection.player || !valid_death(packet.death) ||
        offered == deaths_.end() || !(offered->second == packet.death) || actor == entities_.end() ||
        actor->second.owner != connection.player || actor->second.generation != packet.death.generation) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    // A confirmation already in flight after native revival cannot revive again.
    if (!actor->second.life_state || actor->second.health != 0) {
        return;
    }
    confirm_to_authority(out, connection, packet);
}

void Session::receive_tutorial_done(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                                    const Packet& packet) {
    if (!on_ready_baseline(connection, packet)) {
        return;
    }
    const auto offered = tutorials_.find(tutorial_key(connection.player, packet.tutorial.token));
    const auto actor = entities_.find(packet.tutorial.entity);
    if (packet.role != Role::player || packet.player != connection.player || !valid_tutorial(packet.tutorial) ||
        offered == tutorials_.end() || !(offered->second == packet.tutorial) || actor == entities_.end() ||
        actor->second.owner != connection.player || actor->second.generation != packet.tutorial.generation ||
        actor->second.life_state || actor->second.health <= 0) {
        reject(out, connection_id, Error::ownership);
        return;
    }
    confirm_to_authority(out, connection, packet);
}

// Answers a player's action that does not reach the authority, naming that action. A quarantine is a rejection;
// any other refusal is an action result.
void Session::refuse_action(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                            const Packet& packet, Error reason, bool quarantine) {
    Packet response;
    response.kind = quarantine ? Kind::reject : Kind::action_result;
    response.error = reason;
    response.player = connection.player;
    response.scene = packet.scene;
    response.baseline = packet.baseline;
    response.entity = packet.entity;
    response.target = packet.target;
    response.target_generation = packet.target_generation;
    response.request = packet.request ? packet.request : packet.sequence;
    emit(out, connection_id, response);
}

// Why the entity a targeted action names cannot be its target, or Error::none.
Error Session::action_target_refusal(const Packet& packet) const {
    const auto target = entities_.find(packet.target);
    if (target == entities_.end() || target->second.generation != packet.target_generation) {
        return Error::stale;
    }
    if (target->second.owner) {
        return Error::ownership;
    }
    if (!eligible_target(packet.verb, target->second)) {
        return Error::not_ready;
    }
    return Error::none;
}

void Session::receive_action(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                             const Packet& packet) {
    if (!within_action_rate(out, connection_id, connection)) {
        return;
    }
    if (!authority_ || !complete_ || !connection.ready) {
        refuse_action(out, connection_id, connection, packet, Error::not_ready);
        return;
    }
    if (packet.scene != scene_ || packet.baseline != connection.baseline) {
        refuse_action(out, connection_id, connection, packet, Error::stale);
        return;
    }
    if (!valid_action(packet)) {
        refuse_action(out, connection_id, connection, packet, Error::malformed);
        return;
    }
    const auto actor = entities_.find(packet.entity.id);
    if (actor == entities_.end() || actor->second.owner != connection.player ||
        actor->second.generation != packet.entity.generation) {
        refuse_action(out, connection_id, connection, packet, Error::ownership, true);
        return;
    }
    if (actor->second.life_state || actor->second.health <= 0) {
        refuse_action(out, connection_id, connection, packet, Error::not_ready);
        return;
    }
    if (targeted(packet.verb)) {
        const Error refusal = action_target_refusal(packet);
        if (refusal != Error::none) {
            refuse_action(out, connection_id, connection, packet, refusal);
            return;
        }
    }
    // Native IDs/vitals in a client action are untrusted. Forward the current
    // authoritative actor value after authenticating its owner/incarnation.
    Packet action = packet;
    action.player = connection.player;
    action.entity = actor->second;
    action.baseline = source_baseline_;
    if (!action.request) {
        action.request = packet.sequence;
    }
    if (action.request <= connection.last_request) {
        reject(out, connection_id, Error::stale);
        return;
    }
    connection.last_request = action.request;
    emit(out, authority_, action);
}
} // namespace sporemp::network
