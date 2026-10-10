#pragma once
#include "protocol.h"
#include "content_control.h"
#include "location_ledger.h"
#include <map>
#include <chrono>
#include <vector>
#include <functional>

namespace sporemp::network {
struct SessionConfig {
    Identity identity{};
    Digest authority{}, player1{}, player2{};
    uint64_t epoch = 1, checkpoint_revision = 0;
    LocationId location{};
    uint64_t worker_generation = 0, maintenance_transfer = 0;
    uint32_t native_load_slot = 0;
    bool content_authority = true;
    uint8_t permitted_owners = 0b110;
};

struct Dispatch {
    uint64_t connection = 0;
    Packet packet{};
    bool close = false;
    bool recipient_scope = false;
};

// Single coordinator thread only. Pure scalar policy; no socket/engine dependency.
class Session {
public:
    explicit Session(SessionConfig config, ContentControl* shared_content = nullptr,
                     const LocationLedger* leases = nullptr);
    // A running location session is bound to specific durable worker/avatar
    // leases. Revocation requires a new session or the explicit disconnected
    // worker replacement path; ordinary packets cannot refresh this binding.
    bool lease_current() const noexcept;
    void fence_lease();
    bool connect(uint64_t connection_id);

    bool authority_connected() const noexcept {
        return authority_ != 0;
    }

    bool authority_ready() const noexcept {
        return authority_ != 0 && complete_ && !building_ && !maintenance_ && lease_current();
    }

    bool authority_control_ready() const noexcept {
        return authority_ != 0 && (complete_ || (config_.maintenance_transfer && scene_)) && !building_ &&
               lease_current();
    }

    uint64_t baseline_id() const noexcept {
        return source_baseline_;
    }

    uint64_t epoch() const noexcept {
        return config_.epoch;
    }

    const Entity* owned_entity(uint64_t player) const noexcept;
    // Maintenance preserves the authenticated control stream while withholding
    // all gameplay and content publication. Only Coordinator can rebind leases.
    bool begin_transfer_maintenance(uint64_t transfer, uint8_t native_owners = 0xff);
    bool rebind_transfer_maintenance(uint64_t transfer, bool release = false);

    uint64_t maintenance_transfer() const noexcept {
        return maintenance_;
    }

    uint64_t maintenance_start() const noexcept {
        return config_.maintenance_transfer;
    }

    std::vector<Dispatch> transfer_command(const TransferFrame& frame);

    void transfer_receipts(std::function<bool(const Packet&)> callback) {
        transfer_receipt_ = std::move(callback);
    }

    bool can_rotate_disconnected_authority(uint64_t worker_generation, const Digest& credential) const noexcept;
    bool rotate_disconnected_authority(uint64_t worker_generation, const Digest& credential);
    bool recipient_scope(uint64_t connection_id, Packet& packet);
    std::vector<Dispatch> receive(uint64_t connection_id, const Packet& packet);
    std::vector<Dispatch> disconnect(uint64_t connection_id);

    size_t entity_count() const noexcept {
        return entities_.size();
    }

    uint64_t scene() const noexcept {
        return scene_;
    }

    std::vector<uint8_t> content_snapshot() const {
        return content_->registry().snapshot();
    }

    bool restore_content(const std::vector<uint8_t>& bytes, std::string& error) {
        return content_->restore(bytes, error);
    }

    void content_persistence(ContentRegistry::Persist persist) {
        content_->persistence(std::move(persist));
    }

    ContentControl& shared_content() noexcept {
        return *content_;
    }

    bool checkpoint_world(const WorldIdentity& world) {
        if (!connections_.empty() || !content_->checkpoint_world(world)) {
            return false;
        }
        config_.identity.world = world;
        config_.identity.fixture = world[0];
        return true;
    }

private:
    struct Connection {
        uint64_t player = 0, sequence = 0, out_sequence = 0, baseline = 0, last_request = 0;
        bool authenticated = false, authority = false, ready = false;
        size_t actions = 0;
        std::chrono::steady_clock::time_point action_window = std::chrono::steady_clock::now();
    };

    SessionConfig config_;
    ContentControl content_storage_;
    ContentControl* content_;
    const LocationLedger* leases_;
    uint64_t lease_worker_ = 0;
    std::array<uint64_t, 2> avatar_generations_{};
    uint8_t interested_owners_ = 0;
    bool lease_fenced_ = false;
    uint64_t maintenance_ = 0;
    uint8_t maintenance_owners_ = 0;
    std::function<bool(const Packet&)> transfer_receipt_;
    std::map<uint64_t, Connection> connections_;
    std::map<uint64_t, Entity> entities_, staging_;
    std::map<uint64_t, uint64_t> tombstones_;
    std::map<uint64_t, TutorialFrame> tutorials_;
    std::map<uint64_t, DeathFrame> deaths_;
    std::map<uint64_t, HatchFrame> hatches_;
    uint64_t authority_ = 0, scene_ = 0, source_baseline_ = 0, next_baseline_ = 0, staging_scene_ = 0,
             staging_baseline_ = 0;
    uint32_t expected_ = 0;
    bool building_ = false, complete_ = false;
    bool capture_location_lease() noexcept;
    void clear_scene() noexcept;
    void withdraw_baselines();
    void stamp(Connection& connection, Packet& packet) const;
    void emit(std::vector<Dispatch>& out, uint64_t connection_id, Packet packet, bool close = false);
    void reject(std::vector<Dispatch>& out, uint64_t connection_id, Error error, bool close = false);
    // The message being handled, so that a rejection can name what it refuses.
    const Packet* handling_ = nullptr;
    void baseline(std::vector<Dispatch>& out, uint64_t connection_id);
    void publish(std::vector<Dispatch>& out, const Packet& packet);
    void send_to_player(std::vector<Dispatch>& out, const Packet& packet, bool only_ready);

    // receive() in parts. Each answers into `out`; one that returns bool returns false once it has refused the
    // packet. `connection_id` is the sender and `connection` its record.
    bool admit_stream(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection, const Packet& packet);
    bool admit_epoch(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                     const Packet& packet);
    // An unauthenticated connection: only a hello that matches this session and holds a credential is admitted.
    void receive_hello(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                       const Packet& packet);
    bool hello_matches_session(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    bool authenticate_player(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet,
                             uint64_t& player);
    void send_welcome(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection);
    void receive_transfer(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                          const Packet& packet);
    // Transfer maintenance: nothing is delivered or cached.
    void receive_in_maintenance(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                                const Packet& packet);
    bool withheld_in_maintenance(const Connection& connection, const Packet& packet) const;
    void drain_fenced_output(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_content_request(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                                 const Packet& packet);
    // From the authority: a scene baseline in three steps, then changes to the complete scene and frames for one
    // player.
    void receive_from_authority(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_scene_begin(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_while_building(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void stage_entity(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet, uint8_t owners);
    void complete_scene(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet, uint8_t owners);
    void receive_hatch_view(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_death(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_relationship(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_history(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_inventory(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_social_view(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_tutorial(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_entity(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    void receive_motion_or_despawn(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    bool apply_motion(std::vector<Dispatch>& out, uint64_t connection_id, Entity& entity, const Packet& packet);
    void receive_action_result(std::vector<Dispatch>& out, uint64_t connection_id, const Packet& packet);
    // From a player: the acknowledgement of its baseline, then what it asks of the authority.
    void receive_from_player(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                             const Packet& packet);
    void receive_baseline_ack(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                              const Packet& packet);
    bool within_action_rate(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection);
    bool on_ready_baseline(const Connection& connection, const Packet& packet) const;
    void confirm_to_authority(std::vector<Dispatch>& out, const Connection& connection, const Packet& packet);
    void receive_hatch_input(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                             const Packet& packet);
    void receive_death_done(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                            const Packet& packet);
    void receive_tutorial_done(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                               const Packet& packet);
    void receive_action(std::vector<Dispatch>& out, uint64_t connection_id, Connection& connection,
                        const Packet& packet);
    void refuse_action(std::vector<Dispatch>& out, uint64_t connection_id, const Connection& connection,
                       const Packet& packet, Error reason, bool quarantine = false);
    Error action_target_refusal(const Packet& packet) const;
};
} // namespace sporemp::network
