#pragma once
#include "world_identity.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sporemp::network {
// Value-only coordinator policy. Authentication and native calls remain in
// their existing transport/bridge adapters. No client number is an attestation.
struct ContentKey {
    uint32_t group=0, instance=0, type=0;
    bool operator==(const ContentKey& x) const noexcept;
    bool operator<(const ContentKey& x) const noexcept;
};
struct ContentBlock {
    ContentKey part{};
    int32_t index=0, parent=-1, symmetric=-1, flags=0, block_type=0, capability_start=0, capability_count=0;
};
struct ContentCapability { std::array<uint8_t,4> tag{}; int32_t level=0; };
struct ContentObservation {
    Digest png{};
    ContentKey native_key{};
    uint32_t model_type=0;
    std::vector<ContentBlock> blocks;
    std::vector<ContentCapability> capabilities;
    // Exact installed property records actually resolved by the original
    // engine. The pinned complete installed-file identity covers their assets.
    std::vector<ContentKey> resolved_parts;
    Digest installed_profile{};
    WorldIdentity world{};
};
struct ContentManifest {
    Digest version{}, parent{}, png{}, native_properties{}, installed_profile{};
    WorldIdentity world{};
    uint64_t owner=0, revision=0;
    uint32_t png_bytes=0, model_type=0, rigblocks=0, capabilities=0;
    std::vector<ContentKey> parts;
};
enum class ContentFailure {
    none, unknown_connection, wrong_owner, wrong_authority, authority_unavailable,
    stale_base, transaction_exists, unknown_transaction, expired, wrong_phase,
    capacity, invalid_size, invalid_offset, hash_mismatch, invalid_png,
    invalid_native_observation, missing_part, installed_profile_mismatch,
    world_mismatch, properties_mismatch, not_ready, unchanged_content, internal_hash, stale_request, malformed
};
struct ContentDecision {
    ContentFailure failure=ContentFailure::none;
    uint64_t transaction=0;
    ContentKey missing{};
    uint32_t world_index=0;
    std::string detail;
    explicit operator bool() const noexcept { return failure==ContentFailure::none; }
};
const char* content_failure_name(ContentFailure) noexcept;
bool content_digest(const uint8_t*,size_t,Digest&) noexcept;

class ContentRegistry {
public:
    static constexpr uint32_t max_png_bytes=4*1024*1024, max_chunk_bytes=16*1024;
    static constexpr size_t max_versions=32, max_cached_bytes=16*1024*1024;
    static constexpr uint64_t transaction_lifetime_ms=5*60*1000;
    ContentRegistry(Digest installed_profile,WorldIdentity world);
    // Call only after the coordinator authenticates this connection. A fresh
    // connection replacing an owner revokes the old connection's transactions.
    bool connect(uint64_t connection,uint64_t player,bool authority);
    void disconnect(uint64_t connection);
    void fence(); // Scene/authority reset invalidates pending work and readiness.
    ContentDecision begin(uint64_t connection,const Digest& base,uint64_t now_ms);
    ContentDecision offer(uint64_t connection,uint64_t transaction,const Digest& png,uint32_t size,uint64_t now_ms);
    ContentDecision append(uint64_t connection,uint64_t transaction,uint32_t offset,const std::vector<uint8_t>&,uint64_t now_ms);
    ContentDecision seal(uint64_t connection,uint64_t transaction,uint64_t now_ms);
    ContentDecision attest(uint64_t authority_connection,uint64_t transaction,const ContentObservation&,uint64_t now_ms);
    ContentDecision ready(uint64_t connection,uint64_t transaction,const ContentObservation&,uint64_t now_ms);
    ContentDecision dependency_failure(uint64_t connection,uint64_t transaction,const ContentKey&,uint64_t now_ms);
    ContentDecision commit(uint64_t connection,uint64_t transaction,uint64_t now_ms);
    ContentDecision cancel(uint64_t connection,uint64_t transaction,uint64_t now_ms);
    ContentDecision observation_admission(uint64_t connection,uint64_t transaction,uint64_t now_ms);
    // Unvalidated uploads are available only to authority. After attestation,
    // authenticated participants can obtain the candidate for native loading.
    const std::vector<uint8_t>* candidate(uint64_t connection,uint64_t transaction,uint64_t now_ms);
    const ContentManifest* proposed(uint64_t transaction) const;
    const ContentManifest* published(const Digest& version) const;
    const std::vector<uint8_t>* blob(const Digest& png) const;
    Digest current(uint64_t player) const;
    bool mapping(uint64_t connection,const Digest& png,ContentKey&) const;
    size_t version_count() const noexcept { return versions_.size(); }
    size_t pending_count() const noexcept { return transactions_.size(); }
private:
    enum class Phase { draft,upload,sealed,validated };
    struct Peer { uint64_t player=0;bool authority=false; };
    struct Transaction {
        uint64_t connection=0,player=0,deadline=0;
        Phase phase=Phase::draft;
        Digest base{},png{};
        uint32_t bytes=0;
        std::vector<uint8_t> data;
        ContentManifest manifest{};
        std::map<uint64_t,ContentKey> mappings;
    };
    Digest installed_{};
    WorldIdentity world_{};
    uint64_t authority_=0,next_transaction_=0;
    size_t cached_bytes_=0;
    std::map<uint64_t,Peer> peers_;
    std::map<uint64_t,Transaction> transactions_;
    std::map<Digest,ContentManifest> versions_;
    std::map<Digest,std::vector<uint8_t>> blobs_;
    std::map<uint64_t,Digest> current_;
    std::map<uint64_t,std::map<Digest,ContentKey>> mappings_;
    ContentDecision access(uint64_t,uint64_t,uint64_t,bool owner);
    ContentDecision observe(const ContentObservation&,ContentManifest&) const;
    void expire(uint64_t now_ms);
};
}
