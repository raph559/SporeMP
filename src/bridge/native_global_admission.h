#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace sporemp {
enum class NativeGlobalAdmissionRole : uint32_t {
    reader = 1,
    authority = 2
};

// Values only: the authenticated preload transport is owned by native_network.
// Its TLS admission must validate these against the current durable ledger.
struct NativeGlobalAdmissionContext {
    NativeGlobalAdmissionRole role = NativeGlobalAdmissionRole::reader;
    std::array<uint8_t, 32> universe{}, checkpoint{};
    std::array<uint64_t, 4> location{};
    uint64_t checkpoint_revision = 0, worker = 0, generation = 0, process_created = 0;
    uint32_t process_id = 0;

    bool operator==(const NativeGlobalAdmissionContext& b) const {
        return role == b.role && universe == b.universe && checkpoint == b.checkpoint && location == b.location &&
               checkpoint_revision == b.checkpoint_revision && worker == b.worker && generation == b.generation &&
               process_created == b.process_created && process_id == b.process_id;
    }
};

struct NativeGlobalLease {
    uint64_t worker = 0, generation = 0;

    bool operator==(const NativeGlobalLease& b) const {
        return worker == b.worker && generation == b.generation;
    }
};

struct NativeGlobalAdmissionGrant {
    NativeGlobalAdmissionContext context{};
    std::array<NativeGlobalLease, 4> owners{};
    uint64_t nonce = 0, sent_at_ms = 0;
    uint32_t ttl_ms = 0, producer_mask = 0;
};
enum class NativeGlobalPermit {
    off,
    denied,
    owner,
    reader
};

// Portable admission state, also exercised by HOST tests. Engine-thread caller;
// transport liveness is queried separately for every actual native entry.
class NativeGlobalAdmission {
public:
    static constexpr uint32_t max_ttl_ms = 30000;

    bool arm(const NativeGlobalAdmissionContext& value) {
        if (armed_ || !valid(value)) {
            return false;
        }
        context_ = value;
        armed_ = true;
        return true;
    }

    bool install(const NativeGlobalAdmissionGrant& value, uint64_t now, bool live) {
        // A reader can follow a proved replacement of the same global worker.
        // It never gains producer rights, changes its own context or revives an
        // expired grant. The coordinator retains the old owner's death handle.
        const bool reader_replacement = grant_.nonce && !grant_.producer_mask && !value.producer_mask &&
                                        grant_.owners[0].worker == value.owners[0].worker &&
                                        grant_.owners[0].generation < value.owners[0].generation;
        if (!armed_ || revoked_ || !live || !(context_ == value.context) || !value.nonce ||
            value.nonce <= grant_.nonce || !value.ttl_ms || value.ttl_ms > max_ttl_ms || value.sent_at_ms > now ||
            now - value.sent_at_ms >= value.ttl_ms ||
            value.sent_at_ms > std::numeric_limits<uint64_t>::max() - value.ttl_ms ||
            (grant_.nonce && (now >= deadline_ || (!(grant_.owners[0] == value.owners[0]) && !reader_replacement)))) {
            return false;
        }
        const auto owner = value.owners[0];
        if (!owner.worker || !owner.generation) {
            return false;
        }
        for (const auto& item : value.owners) {
            if (!(item == owner)) {
                return false;
            }
        }
        const bool mine = context_.role == NativeGlobalAdmissionRole::authority && owner.worker == context_.worker &&
                          owner.generation == context_.generation;
        // A different generation of this worker cannot grant this process reader rights.
        if (context_.role == NativeGlobalAdmissionRole::authority && owner.worker == context_.worker && !mine) {
            return false;
        }
        if (value.producer_mask != (mine ? 15u : 0u)) {
            return false;
        }
        grant_ = value;
        deadline_ = value.sent_at_ms + value.ttl_ms;
        return true;
    }

    NativeGlobalPermit permit(uint64_t now, bool live) const {
        if (!armed_) {
            return NativeGlobalPermit::off;
        }
        if (revoked_ || !live || !grant_.nonce || now < grant_.sent_at_ms || now >= deadline_) {
            return NativeGlobalPermit::denied;
        }
        return grant_.producer_mask == 15 ? NativeGlobalPermit::owner : NativeGlobalPermit::reader;
    }

    void revoke() {
        if (armed_) {
            revoked_ = true;
        }
    }

    bool armed() const {
        return armed_;
    }

    const NativeGlobalAdmissionContext& context() const {
        return context_;
    }

    const NativeGlobalAdmissionGrant& grant() const {
        return grant_;
    }

    uint64_t deadline() const {
        return deadline_;
    }

    static bool spice_noop(bool force, const std::array<uint32_t, 3>& key,
                           const std::array<uint32_t, 3>& native_default) {
        return !force && native_default == std::array<uint32_t, 3>{} && key != native_default;
    }

private:
    static bool valid(const NativeGlobalAdmissionContext& v) {
        if ((v.role != NativeGlobalAdmissionRole::reader && v.role != NativeGlobalAdmissionRole::authority) ||
            v.universe == std::array<uint8_t, 32>{} || v.checkpoint == std::array<uint8_t, 32>{} ||
            !v.checkpoint_revision || !v.process_id || !v.process_created ||
            (v.role == NativeGlobalAdmissionRole::authority && (!v.worker || !v.generation)) ||
            ((v.worker == 0) != (v.generation == 0))) {
            return false;
        }
        for (const auto component : v.location) {
            if (!component) {
                return false;
            }
        }
        return true;
    }

    bool armed_ = false, revoked_ = false;
    NativeGlobalAdmissionContext context_{};
    NativeGlobalAdmissionGrant grant_{};
    uint64_t deadline_ = 0;
};
} // namespace sporemp
