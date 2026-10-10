#include "content_registry.h"
#include "state_codec.h"
#include "../bridge/content_png.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <limits>
#include <set>
#include <tuple>

namespace sporemp::network {
bool ContentKey::operator==(const ContentKey& x) const noexcept {
    return group == x.group && instance == x.instance && type == x.type;
}

bool ContentKey::operator<(const ContentKey& x) const noexcept {
    return std::tie(group, instance, type) < std::tie(x.group, x.instance, x.type);
}

namespace {
// The resource types of a creation and of a part's property record: crt_type and prop_type in
// src/bridge/native_content.cpp.
constexpr uint32_t crt_type = 0x2b978c46, prop_type = 0x00b1b104;
// The one model type an observation or a stored manifest may carry.
constexpr uint32_t accepted_model_type = 0x9ea3031a;
// The smallest PNG that creation_png_envelope (src/bridge/content_png.h) accepts.
constexpr uint32_t min_png_bytes = 57;
constexpr size_t max_blocks = 512, max_parts = 512, max_capabilities = 4096;
constexpr uint32_t snapshot_magic = 0x39534d43; // CMS9

ContentDecision failure(ContentFailure reason, uint64_t transaction = 0) {
    ContentDecision decision;
    decision.failure = reason;
    decision.transaction = transaction;
    return decision;
}

void append_word(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) {
        out.push_back(uint8_t(value >> (8 * n)));
    }
}

void append_wide(std::vector<uint8_t>& out, uint64_t value) {
    for (unsigned n = 0; n < 8; ++n) {
        out.push_back(uint8_t(value >> (8 * n)));
    }
}

void append_digest(std::vector<uint8_t>& out, const Digest& digest) {
    out.insert(out.end(), digest.begin(), digest.end());
}

void append_key(std::vector<uint8_t>& out, const ContentKey& key) {
    append_word(out, key.group);
    append_word(out, key.instance);
    append_word(out, key.type);
}

bool native_key(const ContentKey& key, uint32_t type) {
    return key.group && key.group != UINT32_MAX && key.instance && key.instance != UINT32_MAX && key.type == type;
}

bool manifest_digest(ContentManifest& manifest) {
    // Explicit versioned canonical encoding, never a native struct or pointer.
    std::vector<uint8_t> bytes;
    append_word(bytes, 0x38434d53); // SMC8
    append_word(bytes, 1);
    append_wide(bytes, manifest.owner);
    append_wide(bytes, manifest.revision);
    append_digest(bytes, manifest.parent);
    append_digest(bytes, manifest.png);
    append_digest(bytes, manifest.native_properties);
    append_digest(bytes, manifest.installed_profile);
    for (const auto& file : manifest.world) {
        append_digest(bytes, file);
    }
    append_word(bytes, manifest.png_bytes);
    append_word(bytes, manifest.model_type);
    append_word(bytes, manifest.rigblocks);
    append_word(bytes, manifest.capabilities);
    append_word(bytes, static_cast<uint32_t>(manifest.parts.size()));
    for (const auto& part : manifest.parts) {
        append_key(bytes, part);
    }
    return content_digest(bytes.data(), bytes.size(), manifest.version);
}

// Identical approved bytes must never carry different gameplay properties: whether a version already holds
// this PNG under other native properties.
bool properties_conflict(const std::map<Digest, ContentManifest>& versions, const Digest& png,
                         const Digest& native_properties) {
    for (const auto& version : versions) {
        if (version.second.png == png && version.second.native_properties != native_properties) {
            return true;
        }
    }
    return false;
}

// The parts the engine resolved, as a set: false when one is not a part key or is listed twice.
bool distinct_parts(const std::vector<ContentKey>& parts, std::set<ContentKey>& resolved) {
    for (const auto& part : parts) {
        if (!native_key(part, prop_type) || !resolved.insert(part).second) {
            return false;
        }
    }
    return true;
}

// A block names a part, sits at its own index, has a parent before it, a symmetric block other than itself
// and a range inside the observation's capabilities.
bool valid_block(const ContentBlock& block, size_t index, const ContentObservation& observation) {
    if (!native_key(block.part, prop_type) || block.index != static_cast<int32_t>(index) || block.parent < -1 ||
        block.parent >= static_cast<int32_t>(index) || block.symmetric < -1 ||
        block.symmetric >= static_cast<int32_t>(observation.blocks.size()) || block.symmetric == block.index ||
        block.capability_start < 0 || block.capability_count < 0 ||
        static_cast<size_t>(block.capability_start) > observation.capabilities.size() ||
        static_cast<size_t>(block.capability_count) >
            observation.capabilities.size() - static_cast<size_t>(block.capability_start)) {
        return false;
    }
    return true;
}

// Appends every block to the canonical property encoding and collects the parts they need; false at the
// first invalid block.
bool append_blocks(std::vector<uint8_t>& out, const ContentObservation& observation, std::set<ContentKey>& required) {
    for (size_t n = 0; n < observation.blocks.size(); ++n) {
        const auto& block = observation.blocks[n];
        if (!valid_block(block, n, observation)) {
            return false;
        }
        required.insert(block.part);
        append_key(out, block.part);
        for (auto value : {block.index, block.parent, block.symmetric, block.flags, block.block_type,
                           block.capability_start, block.capability_count}) {
            append_word(out, static_cast<uint32_t>(value));
        }
    }
    return true;
}

// Appends every capability to the canonical property encoding; false at the first invalid one.
bool append_capabilities(std::vector<uint8_t>& out, const std::vector<ContentCapability>& capabilities) {
    for (const auto& capability : capabilities) {
        if (capability.level < -128 || capability.level > 127 || capability.tag == std::array<uint8_t, 4>{}) {
            return false;
        }
        out.insert(out.end(), capability.tag.begin(), capability.tag.end());
        append_word(out, static_cast<uint32_t>(capability.level));
    }
    return true;
}

// Reads one stored manifest and checks what can be checked without the rest of the snapshot. Its version
// digest is left as stored, for the caller to compare with the recomputed one.
bool read_manifest(state::Reader& in, const Digest& installed_profile, ContentManifest& manifest, std::string& error) {
    for (auto* digest : {&manifest.version, &manifest.parent, &manifest.png, &manifest.native_properties,
                         &manifest.installed_profile}) {
        *digest = in.digest();
    }
    for (auto& file : manifest.world) {
        file = in.digest();
    }
    manifest.owner = in.wide();
    manifest.revision = in.wide();
    manifest.png_bytes = in.word();
    manifest.model_type = in.word();
    manifest.rigblocks = in.word();
    manifest.capabilities = in.word();
    const auto parts = in.word();
    if (!in.valid || parts < 1 || parts > max_parts) {
        error = "content_snapshot_part_count";
        return false;
    }
    for (uint32_t n = 0; n < parts; ++n) {
        ContentKey part;
        part.group = in.word();
        part.instance = in.word();
        part.type = in.word();
        manifest.parts.push_back(part);
    }
    if (!in.valid || manifest.owner < 1 || manifest.owner > 2 || !manifest.revision ||
        manifest.revision > ContentRegistry::max_versions || manifest.png == Digest{} ||
        manifest.native_properties == Digest{} || manifest.installed_profile != installed_profile ||
        !valid_world_identity(manifest.world) || manifest.png_bytes < min_png_bytes ||
        manifest.png_bytes > ContentRegistry::max_png_bytes || manifest.model_type != accepted_model_type ||
        !manifest.rigblocks || manifest.rigblocks > max_blocks || manifest.capabilities > max_capabilities ||
        !std::is_sorted(manifest.parts.begin(), manifest.parts.end()) ||
        std::adjacent_find(manifest.parts.begin(), manifest.parts.end()) != manifest.parts.end()) {
        error = "content_snapshot_manifest_invalid";
        return false;
    }
    for (const auto& part : manifest.parts) {
        if (!native_key(part, prop_type)) {
            error = "content_snapshot_part_invalid";
            return false;
        }
    }
    return true;
}

// Each owner's versions by revision, to check once all are read that they form one chain.
using Lineage = std::map<uint64_t, std::map<uint64_t, Digest>>;

// Reads the stored manifests, each under the digest recomputed from its own fields, and files them by owner
// and revision.
bool read_versions(state::Reader& in, uint32_t count, const Digest& installed_profile,
                   std::map<Digest, ContentManifest>& versions, Lineage& lineage, std::string& error) {
    for (uint32_t n = 0; n < count; ++n) {
        ContentManifest manifest;
        if (!read_manifest(in, installed_profile, manifest, error)) {
            return false;
        }
        const auto expected = manifest.version;
        if (!manifest_digest(manifest) || manifest.version != expected ||
            !versions.emplace(manifest.version, manifest).second ||
            !lineage[manifest.owner].emplace(manifest.revision, manifest.version).second) {
            error = "content_snapshot_manifest_hash_or_duplicate";
            return false;
        }
    }
    return true;
}

// Reads the stored PNGs, each a valid envelope under its own digest; there are never more than versions.
bool read_blobs(state::Reader& in, uint32_t version_count, std::map<Digest, std::vector<uint8_t>>& blobs,
                size_t& cached_bytes, std::string& error) {
    const auto count = in.word();
    if (!in.valid || count > version_count) {
        error = "content_snapshot_blob_count";
        return false;
    }
    for (uint32_t n = 0; n < count; ++n) {
        const auto expected = in.digest();
        auto data = in.data(ContentRegistry::max_png_bytes);
        Digest actual{};
        if (!in.valid || data.size() > ContentRegistry::max_cached_bytes - cached_bytes ||
            creation_png_envelope(data.data(), data.size()) || !content_digest(data.data(), data.size(), actual) ||
            actual != expected || blobs.count(expected)) {
            error = "content_snapshot_blob_invalid";
            return false;
        }
        cached_bytes += data.size();
        blobs.emplace(expected, std::move(data));
    }
    return true;
}

// One owner's versions, by revision, must be an unbroken chain from revision 1 to the stored head, each with
// its PNG. Collects the PNGs the chain uses.
bool lineage_valid(const std::map<uint64_t, Digest>& revisions, const std::map<Digest, ContentManifest>& versions,
                   const std::map<Digest, std::vector<uint8_t>>& blobs, const Digest& head, std::set<Digest>& used,
                   std::string& error) {
    Digest previous{};
    uint64_t revision = 0;
    for (const auto& item : revisions) {
        const auto& manifest = versions.at(item.second);
        if (item.first != ++revision || manifest.parent != previous) {
            error = "content_snapshot_broken_lineage";
            return false;
        }
        const auto blob = blobs.find(manifest.png);
        if (blob == blobs.end() || blob->second.size() != manifest.png_bytes) {
            error = "content_snapshot_missing_blob";
            return false;
        }
        if (properties_conflict(versions, manifest.png, manifest.native_properties)) {
            error = "content_snapshot_properties_conflict";
            return false;
        }
        used.insert(manifest.png);
        previous = manifest.version;
    }
    if (head != previous) {
        error = "content_snapshot_wrong_head";
        return false;
    }
    return true;
}
} // namespace

bool content_digest(const uint8_t* bytes, size_t size, Digest& out) noexcept {
    if ((size && !bytes) || size > ULONG_MAX) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Digest value{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) {
        ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    if (ok && size) {
        ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(size), 0) >= 0;
    }
    if (ok) {
        ok = BCryptFinishHash(hash, value.data(), static_cast<ULONG>(value.size()), 0) >= 0;
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (ok) {
        out = value;
    }
    return ok;
}

const char* content_failure_name(ContentFailure reason) noexcept {
    switch (reason) {
#define CONTENT_ERROR(name)                                                                                            \
    case ContentFailure::name:                                                                                         \
        return #name
        CONTENT_ERROR(none);
        CONTENT_ERROR(unknown_connection);
        CONTENT_ERROR(wrong_owner);
        CONTENT_ERROR(wrong_authority);
        CONTENT_ERROR(authority_unavailable);
        CONTENT_ERROR(stale_base);
        CONTENT_ERROR(transaction_exists);
        CONTENT_ERROR(unknown_transaction);
        CONTENT_ERROR(expired);
        CONTENT_ERROR(wrong_phase);
        CONTENT_ERROR(capacity);
        CONTENT_ERROR(invalid_size);
        CONTENT_ERROR(invalid_offset);
        CONTENT_ERROR(hash_mismatch);
        CONTENT_ERROR(invalid_png);
        CONTENT_ERROR(invalid_native_observation);
        CONTENT_ERROR(missing_part);
        CONTENT_ERROR(installed_profile_mismatch);
        CONTENT_ERROR(world_mismatch);
        CONTENT_ERROR(properties_mismatch);
        CONTENT_ERROR(not_ready);
        CONTENT_ERROR(unchanged_content);
        CONTENT_ERROR(internal_hash);
        CONTENT_ERROR(stale_request);
        CONTENT_ERROR(malformed);
        CONTENT_ERROR(storage_failed);
#undef CONTENT_ERROR
    }
    return "unknown";
}

ContentRegistry::ContentRegistry(Digest installed_profile, WorldIdentity world)
    : installed_(installed_profile), world_(world) {}

bool ContentRegistry::connect(uint64_t connection, uint64_t player, bool authority) {
    if (!connection || peers_.count(connection) || peers_.size() >= 8 ||
        (authority ? player != 0 || authority_ != 0 : player < 1 || player > 2) || installed_ == Digest{} ||
        !valid_world_identity(world_)) {
        return false;
    }
    if (!authority) {
        std::vector<uint64_t> replaced;
        for (const auto& peer : peers_) {
            if (!peer.second.authority && peer.second.player == player) {
                replaced.push_back(peer.first);
            }
        }
        for (auto previous : replaced) {
            disconnect(previous);
        }
    }
    peers_.emplace(connection, Peer{player, authority});
    if (authority) {
        authority_ = connection;
    }
    return true;
}

void ContentRegistry::disconnect(uint64_t connection) {
    if (!peers_.erase(connection)) {
        return;
    }
    mappings_.erase(connection);
    if (connection == authority_) {
        authority_ = 0;
        fence();
        return;
    }
    // A replacement participant cannot inherit the prior connection's readiness.
    // Retaining the other owner's draft is safe; commit needs every live peer.
    for (auto pending = transactions_.begin(); pending != transactions_.end();) {
        if (pending->second.connection == connection) {
            pending = transactions_.erase(pending);
        } else {
            pending->second.mappings.erase(connection);
            ++pending;
        }
    }
}

void ContentRegistry::fence() {
    transactions_.clear();
    mappings_.clear();
}

void ContentRegistry::expire(uint64_t now_ms) {
    for (auto pending = transactions_.begin(); pending != transactions_.end();) {
        if (now_ms >= pending->second.deadline) {
            pending = transactions_.erase(pending);
        } else {
            ++pending;
        }
    }
}

ContentDecision ContentRegistry::access(uint64_t connection, uint64_t transaction, uint64_t now_ms, bool owner) {
    const auto peer = peers_.find(connection);
    if (peer == peers_.end()) {
        return failure(ContentFailure::unknown_connection, transaction);
    }
    const auto pending = transactions_.find(transaction);
    if (pending == transactions_.end()) {
        return failure(ContentFailure::unknown_transaction, transaction);
    }
    if (owner && pending->second.connection != connection) {
        return failure(ContentFailure::wrong_owner, transaction);
    }
    if (now_ms >= pending->second.deadline) {
        transactions_.erase(pending);
        return failure(ContentFailure::expired, transaction);
    }
    if (!authority_) {
        return failure(ContentFailure::authority_unavailable, transaction);
    }
    return failure(ContentFailure::none, transaction);
}

// Whether the store cannot take another version, or cannot cache this many bytes of a PNG it does not hold.
bool ContentRegistry::storage_full(const Digest& png, size_t bytes) const {
    return versions_.size() >= max_versions || (!blobs_.count(png) && bytes > max_cached_bytes - cached_bytes_);
}

ContentDecision ContentRegistry::begin(uint64_t connection, const Digest& base, uint64_t now_ms) {
    expire(now_ms);
    const auto peer = peers_.find(connection);
    if (peer == peers_.end()) {
        return failure(ContentFailure::unknown_connection);
    }
    if (peer->second.authority) {
        return failure(ContentFailure::wrong_owner);
    }
    if (!authority_) {
        return failure(ContentFailure::authority_unavailable);
    }
    if (current(peer->second.player) != base) {
        return failure(ContentFailure::stale_base);
    }
    for (const auto& pending : transactions_) {
        if (pending.second.player == peer->second.player) {
            return failure(ContentFailure::transaction_exists);
        }
    }
    if (transactions_.size() >= 2 || next_transaction_ == UINT64_MAX || now_ms > UINT64_MAX - transaction_lifetime_ms) {
        return failure(ContentFailure::capacity);
    }
    Transaction draft;
    draft.connection = connection;
    draft.player = peer->second.player;
    draft.base = base;
    draft.deadline = now_ms + transaction_lifetime_ms;
    const auto id = ++next_transaction_;
    transactions_.emplace(id, std::move(draft));
    return failure(ContentFailure::none, id);
}

ContentDecision ContentRegistry::offer(uint64_t connection, uint64_t transaction, const Digest& png, uint32_t size,
                                       uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, true);
    if (!result) {
        return result;
    }
    auto& pending = transactions_.at(transaction);
    if (pending.phase != Phase::draft) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    if (size < min_png_bytes || size > max_png_bytes) {
        return failure(ContentFailure::invalid_size, transaction);
    }
    if (png == Digest{}) {
        return failure(ContentFailure::hash_mismatch, transaction);
    }
    if (storage_full(png, size)) {
        return failure(ContentFailure::capacity, transaction);
    }
    pending.png = png;
    pending.bytes = size;
    pending.data.reserve(size);
    pending.phase = Phase::upload;
    return result;
}

ContentDecision ContentRegistry::append(uint64_t connection, uint64_t transaction, uint32_t offset,
                                        const std::vector<uint8_t>& bytes, uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, true);
    if (!result) {
        return result;
    }
    auto& pending = transactions_.at(transaction);
    if (pending.phase != Phase::upload) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    if (bytes.empty() || bytes.size() > max_chunk_bytes) {
        return failure(ContentFailure::invalid_size, transaction);
    }
    if (offset != pending.data.size() || bytes.size() > pending.bytes - pending.data.size()) {
        return failure(ContentFailure::invalid_offset, transaction);
    }
    pending.data.insert(pending.data.end(), bytes.begin(), bytes.end());
    return result;
}

ContentDecision ContentRegistry::seal(uint64_t connection, uint64_t transaction, uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, true);
    if (!result) {
        return result;
    }
    auto& pending = transactions_.at(transaction);
    if (pending.phase != Phase::upload) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    if (pending.data.size() != pending.bytes) {
        return failure(ContentFailure::invalid_size, transaction);
    }
    Digest actual{};
    if (!content_digest(pending.data.data(), pending.data.size(), actual)) {
        return failure(ContentFailure::internal_hash, transaction);
    }
    if (actual != pending.png) {
        transactions_.erase(transaction);
        return failure(ContentFailure::hash_mismatch, transaction);
    }
    if (auto error = creation_png_envelope(pending.data.data(), pending.data.size())) {
        result = failure(ContentFailure::invalid_png, transaction);
        result.detail = error;
        transactions_.erase(transaction);
        return result;
    }
    pending.phase = Phase::sealed;
    return result;
}

ContentDecision ContentRegistry::observe(const ContentObservation& observation, ContentManifest& manifest) const {
    if (observation.png == Digest{} || !native_key(observation.native_key, crt_type) ||
        observation.model_type != accepted_model_type || observation.blocks.empty() ||
        observation.blocks.size() > max_blocks || observation.capabilities.size() > max_capabilities ||
        observation.resolved_parts.empty() || observation.resolved_parts.size() > max_parts) {
        return failure(ContentFailure::invalid_native_observation);
    }
    if (observation.installed_profile != installed_) {
        return failure(ContentFailure::installed_profile_mismatch);
    }
    for (size_t n = 0; n < world_file_count; ++n) {
        if (observation.world[n] != world_[n]) {
            auto mismatch = failure(ContentFailure::world_mismatch);
            mismatch.world_index = static_cast<uint32_t>(n);
            return mismatch;
        }
    }
    std::set<ContentKey> required, resolved;
    if (!distinct_parts(observation.resolved_parts, resolved)) {
        return failure(ContentFailure::invalid_native_observation);
    }
    // Explicit versioned canonical encoding of the gameplay properties; its digest identifies them.
    std::vector<uint8_t> properties;
    append_word(properties, 0x38504d53); // SMP8
    append_word(properties, 1);
    append_word(properties, observation.model_type);
    append_word(properties, static_cast<uint32_t>(observation.blocks.size()));
    append_word(properties, static_cast<uint32_t>(observation.capabilities.size()));
    if (!append_blocks(properties, observation, required)) {
        return failure(ContentFailure::invalid_native_observation);
    }
    for (const auto& part : required) {
        if (!resolved.count(part)) {
            auto missing = failure(ContentFailure::missing_part);
            missing.missing = part;
            return missing;
        }
    }
    if (required != resolved) {
        return failure(ContentFailure::invalid_native_observation);
    }
    if (!append_capabilities(properties, observation.capabilities)) {
        return failure(ContentFailure::invalid_native_observation);
    }
    if (!content_digest(properties.data(), properties.size(), manifest.native_properties)) {
        return failure(ContentFailure::internal_hash);
    }
    manifest.png = observation.png;
    manifest.installed_profile = installed_;
    manifest.world = world_;
    manifest.model_type = observation.model_type;
    manifest.rigblocks = static_cast<uint32_t>(observation.blocks.size());
    manifest.capabilities = static_cast<uint32_t>(observation.capabilities.size());
    manifest.parts.assign(required.begin(), required.end());
    return {};
}

ContentDecision ContentRegistry::attest(uint64_t authority_connection, uint64_t transaction,
                                        const ContentObservation& observed, uint64_t now_ms) {
    auto result = access(authority_connection, transaction, now_ms, false);
    if (!result) {
        return result;
    }
    if (authority_connection != authority_) {
        return failure(ContentFailure::wrong_authority, transaction);
    }
    auto& pending = transactions_.at(transaction);
    if (pending.phase != Phase::sealed) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    if (observed.png != pending.png) {
        return failure(ContentFailure::hash_mismatch, transaction);
    }
    ContentManifest manifest;
    result = observe(observed, manifest);
    result.transaction = transaction;
    if (!result) {
        return result;
    }
    if (pending.base != Digest{} && versions_.at(pending.base).png == pending.png) {
        return failure(ContentFailure::unchanged_content, transaction);
    }
    // Identical approved bytes under the same complete profile must never gain
    // different gameplay properties through a later attestation or cache entry.
    if (properties_conflict(versions_, pending.png, manifest.native_properties)) {
        return failure(ContentFailure::properties_mismatch, transaction);
    }
    manifest.parent = pending.base;
    manifest.owner = pending.player;
    manifest.revision = pending.base == Digest{} ? 1 : versions_.at(pending.base).revision + 1;
    manifest.png_bytes = pending.bytes;
    if (!manifest_digest(manifest)) {
        return failure(ContentFailure::internal_hash, transaction);
    }
    pending.manifest = std::move(manifest);
    pending.mappings[authority_connection] = observed.native_key;
    pending.phase = Phase::validated;
    return result;
}

ContentDecision ContentRegistry::observation_admission(uint64_t connection, uint64_t transaction, uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, false);
    if (!result) {
        return result;
    }
    const auto phase = transactions_.at(transaction).phase;
    if (connection == authority_ ? phase != Phase::sealed : phase != Phase::validated) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    return result;
}

ContentDecision ContentRegistry::ready(uint64_t connection, uint64_t transaction, const ContentObservation& observed,
                                       uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, false);
    if (!result) {
        return result;
    }
    auto& pending = transactions_.at(transaction);
    if (connection == authority_) {
        return failure(ContentFailure::wrong_owner, transaction);
    }
    if (pending.phase != Phase::validated) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    if (observed.png != pending.png) {
        return failure(ContentFailure::hash_mismatch, transaction);
    }
    ContentManifest manifest;
    result = observe(observed, manifest);
    result.transaction = transaction;
    if (!result) {
        return result;
    }
    if (manifest.native_properties != pending.manifest.native_properties || manifest.parts != pending.manifest.parts) {
        return failure(ContentFailure::properties_mismatch, transaction);
    }
    pending.mappings[connection] = observed.native_key;
    return result;
}

ContentDecision ContentRegistry::commit(uint64_t connection, uint64_t transaction, uint64_t now_ms) {
    if (!persist_) {
        return commit_unpersisted(connection, transaction, now_ms);
    }
    // Build the candidate without changing the live publication. No success
    // response or publication event can escape before the store flushes it.
    ContentRegistry candidate = *this;
    candidate.persist_ = {};
    auto result = candidate.commit_unpersisted(connection, transaction, now_ms);
    if (!result) {
        return result;
    }
    std::string error;
    if (!persist_(candidate.snapshot(), error)) {
        return failure(ContentFailure::storage_failed, transaction);
    }
    candidate.persist_ = persist_;
    *this = std::move(candidate);
    return result;
}

// Publishes a validated transaction in this registry alone; `commit` decides whether that is the live
// registry or a candidate to store first.
ContentDecision ContentRegistry::commit_unpersisted(uint64_t connection, uint64_t transaction, uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, true);
    if (!result) {
        return result;
    }
    auto& pending = transactions_.at(transaction);
    if (pending.phase != Phase::validated) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    if (current(pending.player) != pending.base) {
        return failure(ContentFailure::stale_base, transaction);
    }
    for (const auto& peer : peers_) {
        if (!pending.mappings.count(peer.first)) {
            return failure(ContentFailure::not_ready, transaction);
        }
    }
    if (storage_full(pending.png, pending.data.size())) {
        return failure(ContentFailure::capacity, transaction);
    }
    if (blobs_.count(pending.png) && blobs_.at(pending.png) != pending.data) {
        return failure(ContentFailure::hash_mismatch, transaction);
    }
    if (!blobs_.count(pending.png)) {
        cached_bytes_ += pending.data.size();
        blobs_.emplace(pending.png, std::move(pending.data));
    }
    versions_.emplace(pending.manifest.version, pending.manifest);
    current_[pending.player] = pending.manifest.version;
    for (const auto& mapping : pending.mappings) {
        mappings_[mapping.first][pending.png] = mapping.second;
    }
    transactions_.erase(transaction);
    return result;
}

ContentDecision ContentRegistry::dependency_failure(uint64_t connection, uint64_t transaction,
                                                    const ContentKey& missing, uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, false);
    if (!result) {
        return result;
    }
    auto& pending = transactions_.at(transaction);
    if (connection == authority_) {
        return failure(ContentFailure::wrong_owner, transaction);
    }
    if (pending.phase != Phase::validated) {
        return failure(ContentFailure::wrong_phase, transaction);
    }
    if (std::find(pending.manifest.parts.begin(), pending.manifest.parts.end(), missing) ==
        pending.manifest.parts.end()) {
        return failure(ContentFailure::malformed, transaction);
    }
    pending.mappings.erase(connection);
    return result;
}

ContentDecision ContentRegistry::cancel(uint64_t connection, uint64_t transaction, uint64_t now_ms) {
    auto result = access(connection, transaction, now_ms, true);
    if (result) {
        transactions_.erase(transaction);
    }
    return result;
}

const std::vector<uint8_t>* ContentRegistry::candidate(uint64_t connection, uint64_t transaction, uint64_t now_ms) {
    if (!access(connection, transaction, now_ms, false)) {
        return nullptr;
    }
    const auto& pending = transactions_.at(transaction);
    if (pending.phase == Phase::validated || (pending.phase == Phase::sealed && connection == authority_)) {
        return &pending.data;
    }
    return nullptr;
}

const ContentManifest* ContentRegistry::proposed(uint64_t transaction) const {
    const auto pending = transactions_.find(transaction);
    return pending != transactions_.end() && pending->second.phase == Phase::validated ? &pending->second.manifest
                                                                                       : nullptr;
}

const ContentManifest* ContentRegistry::published(const Digest& version) const {
    const auto found = versions_.find(version);
    return found == versions_.end() ? nullptr : &found->second;
}

const std::vector<uint8_t>* ContentRegistry::blob(const Digest& png) const {
    const auto found = blobs_.find(png);
    return found == blobs_.end() ? nullptr : &found->second;
}

Digest ContentRegistry::current(uint64_t player) const {
    const auto found = current_.find(player);
    return found == current_.end() ? Digest{} : found->second;
}

bool ContentRegistry::mapping(uint64_t connection, const Digest& png, ContentKey& result) const {
    const auto peer = mappings_.find(connection);
    if (peer == mappings_.end()) {
        return false;
    }
    const auto key = peer->second.find(png);
    if (key == peer->second.end()) {
        return false;
    }
    result = key->second;
    return true;
}

std::vector<uint8_t> ContentRegistry::snapshot() const {
    state::Writer out;
    out.word(snapshot_magic);
    out.word(1);
    out.digest(installed_);
    for (const auto& file : world_) {
        out.digest(file);
    }
    out.wide(next_transaction_);
    out.word(static_cast<uint32_t>(versions_.size()));
    for (const auto& entry : versions_) {
        const auto& manifest = entry.second;
        for (const auto* digest : {&manifest.version, &manifest.parent, &manifest.png, &manifest.native_properties,
                                   &manifest.installed_profile}) {
            out.digest(*digest);
        }
        for (const auto& file : manifest.world) {
            out.digest(file);
        }
        out.wide(manifest.owner);
        out.wide(manifest.revision);
        out.word(manifest.png_bytes);
        out.word(manifest.model_type);
        out.word(manifest.rigblocks);
        out.word(manifest.capabilities);
        out.word(static_cast<uint32_t>(manifest.parts.size()));
        for (const auto& part : manifest.parts) {
            out.word(part.group);
            out.word(part.instance);
            out.word(part.type);
        }
    }
    out.word(static_cast<uint32_t>(blobs_.size()));
    for (const auto& entry : blobs_) {
        out.digest(entry.first);
        out.data(entry.second);
    }
    out.digest(current(1));
    out.digest(current(2));
    return std::move(out.bytes);
}

bool ContentRegistry::restore(const std::vector<uint8_t>& bytes, std::string& error) {
    auto fail = [&](const char* message) {
        error = message;
        return false;
    };
    if (!peers_.empty() || !transactions_.empty() || !versions_.empty()) {
        return fail("content_restore_requires_empty_registry");
    }
    if (bytes.size() > max_cached_bytes + 256 * 1024) {
        return fail("content_snapshot_oversized");
    }
    state::Reader in{bytes};
    ContentRegistry candidate(installed_, world_);
    if (in.word() != snapshot_magic || in.word() != 1) {
        return fail("content_snapshot_schema");
    }
    if (in.digest() != installed_) {
        return fail("content_snapshot_installed_profile");
    }
    for (const auto& file : world_) {
        if (in.digest() != file) {
            return fail("content_snapshot_world");
        }
    }
    candidate.next_transaction_ = in.wide();
    const auto count = in.word();
    if (!in.valid || count > max_versions) {
        return fail("content_snapshot_version_count");
    }
    Lineage lineage;
    if (!read_versions(in, count, installed_, candidate.versions_, lineage, error) ||
        !read_blobs(in, count, candidate.blobs_, candidate.cached_bytes_, error)) {
        return false;
    }
    candidate.current_[1] = in.digest();
    candidate.current_[2] = in.digest();
    if (!in.done()) {
        return fail("content_snapshot_truncated_or_trailing");
    }
    std::set<Digest> used;
    for (uint64_t owner = 1; owner <= 2; ++owner) {
        if (!lineage_valid(lineage[owner], candidate.versions_, candidate.blobs_, candidate.current(owner), used,
                           error)) {
            return false;
        }
    }
    if (used.size() != candidate.blobs_.size() || candidate.next_transaction_ < candidate.versions_.size()) {
        return fail("content_snapshot_unreferenced_blob_or_counter");
    }
    candidate.persist_ = persist_;
    *this = std::move(candidate);
    return true;
}
} // namespace sporemp::network
