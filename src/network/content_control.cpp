#include "content_control.h"
#include <algorithm>

namespace sporemp::network {
namespace {
// ContentControl keeps its Peer and Upload types private. The helpers that need one take it as a template parameter,
// so that they stay local to this file instead of becoming members declared in the header.

// A refusal made here rather than by the registry: a decision that carries nothing but its failure.
ContentDecision refused(ContentFailure reason) {
    ContentDecision decision;
    decision.failure = reason;
    return decision;
}

// Counts one request against the peer's one-second window. False once the window is full.
template <typename Peer>
bool within_request_rate(Peer& peer, uint64_t now_ms) {
    if (now_ms - peer.rate_window >= 1000) {
        peer.rate_window = now_ms;
        peer.requests = 0;
    }
    return ++peer.requests <= 2048;
}

// Forgets every partial observation upload whose transaction the registry no longer admits.
template <typename Peers>
void drop_unadmitted_observations(Peers& peers, ContentRegistry& registry, uint64_t now_ms) {
    for (auto& existing : peers) {
        if (existing.second.observation.transaction &&
            !registry.observation_admission(existing.first, existing.second.observation.transaction, now_ms)) {
            existing.second.observation = {};
        }
    }
}

// A request must carry exactly the fields its operation uses.
bool request_malformed(const ContentFrame& frame) {
    const bool dependency_failure = frame.op == ContentOp::dependency_failure;
    if ((dependency_failure ? (frame.failure != ContentFailure::missing_part || !frame.missing.group ||
                               !frame.missing.instance || frame.missing.type != 0x00b1b104)
                            : (frame.failure != ContentFailure::none || frame.missing.group || frame.missing.instance ||
                               frame.missing.type)) ||
        frame.world_index || !content_request_op(frame.op) || frame.data.size() > content_chunk_bytes) {
        return true;
    }
    const bool data_op = frame.op == ContentOp::chunk || frame.op == ContentOp::observation_chunk;
    const bool identity_op =
        frame.op == ContentOp::begin || frame.op == ContentOp::offer || frame.op == ContentOp::observation_begin;
    const bool total_op = frame.op == ContentOp::offer || frame.op == ContentOp::observation_begin;
    const bool offset_op = data_op || frame.op == ContentOp::fetch || frame.op == ContentOp::dependencies;
    return (!data_op && !frame.data.empty()) || (!identity_op && frame.identity != Digest{}) ||
           (!total_op && frame.total) || (!offset_op && frame.offset) ||
           ((frame.op == ContentOp::begin || frame.op == ContentOp::current) ? frame.transaction != 0
                                                                             : frame.transaction == 0);
}

// Puts the chunk of `bytes` that starts at the requested offset into the reply. False for an offset past the end.
bool reply_with_chunk(const std::vector<uint8_t>& bytes, const ContentFrame& frame, ContentFrame& reply) {
    if (frame.offset >= bytes.size()) {
        return false;
    }
    reply.total = static_cast<uint32_t>(bytes.size());
    reply.offset = frame.offset;
    const auto end = std::min(bytes.size(), size_t(frame.offset) + content_chunk_bytes);
    reply.data.assign(bytes.begin() + frame.offset, bytes.begin() + end);
    return true;
}

// One chunk of the proposed manifest's dependency list, with the hash of the whole list.
ContentDecision serve_dependencies(ContentRegistry& registry, uint64_t connection, const ContentFrame& frame,
                                   uint64_t now_ms, ContentFrame& reply) {
    ContentDecision admission = registry.observation_admission(connection, frame.transaction, now_ms);
    if (!admission) {
        return admission;
    }
    const auto manifest = registry.proposed(frame.transaction);
    std::vector<uint8_t> bytes;
    if (!manifest) {
        return refused(ContentFailure::not_ready);
    }
    if (!encode_dependencies(*manifest, bytes) || !content_digest(bytes.data(), bytes.size(), reply.identity)) {
        return refused(ContentFailure::internal_hash);
    }
    if (!reply_with_chunk(bytes, frame, reply)) {
        return refused(ContentFailure::invalid_offset);
    }
    return admission;
}

// One chunk of the transaction's candidate bytes.
ContentDecision serve_candidate(ContentRegistry& registry, uint64_t connection, const ContentFrame& frame,
                                uint64_t now_ms, ContentFrame& reply) {
    const auto bytes = registry.candidate(connection, frame.transaction, now_ms);
    if (!bytes) {
        return refused(ContentFailure::not_ready);
    }
    if (!reply_with_chunk(*bytes, frame, reply)) {
        return refused(ContentFailure::invalid_offset);
    }
    return {};
}

template <typename Upload>
ContentDecision begin_observation(ContentRegistry& registry, uint64_t connection, const ContentFrame& frame,
                                  uint64_t now_ms, Upload& upload) {
    ContentDecision admission = registry.observation_admission(connection, frame.transaction, now_ms);
    if (!admission) {
        return admission;
    }
    if (upload.transaction) {
        return refused(ContentFailure::transaction_exists);
    }
    if (frame.total < 292 || frame.total > max_observation_bytes || frame.identity == Digest{}) {
        return refused(ContentFailure::invalid_size);
    }
    upload.transaction = frame.transaction;
    upload.total = frame.total;
    upload.hash = frame.identity;
    upload.bytes.reserve(frame.total);
    return admission;
}

template <typename Upload>
ContentDecision append_observation(ContentRegistry& registry, uint64_t connection, const ContentFrame& frame,
                                   uint64_t now_ms, Upload& upload) {
    ContentDecision admission = registry.observation_admission(connection, frame.transaction, now_ms);
    if (!admission) {
        upload = {};
        return admission;
    }
    if (upload.transaction != frame.transaction) {
        return refused(ContentFailure::unknown_transaction);
    }
    if (frame.data.empty() || frame.offset != upload.bytes.size() ||
        frame.data.size() > upload.total - upload.bytes.size()) {
        return refused(ContentFailure::invalid_offset);
    }
    upload.bytes.insert(upload.bytes.end(), frame.data.begin(), frame.data.end());
    return admission;
}

// The complete upload is checked against its announced hash, decoded, and handed to the registry: as the
// authority's attestation, or as a player's readiness.
template <typename Upload>
ContentDecision end_observation(ContentRegistry& registry, uint64_t connection, const ContentFrame& frame,
                                uint64_t now_ms, bool authority, Upload& upload) {
    ContentDecision admission = registry.observation_admission(connection, frame.transaction, now_ms);
    if (!admission) {
        upload = {};
        return admission;
    }
    if (upload.transaction != frame.transaction) {
        return refused(ContentFailure::unknown_transaction);
    }
    if (upload.bytes.size() != upload.total) {
        return refused(ContentFailure::invalid_size);
    }
    Digest hash{};
    ContentObservation observed;
    if (!content_digest(upload.bytes.data(), upload.bytes.size(), hash) || hash != upload.hash) {
        upload = {};
        return refused(ContentFailure::hash_mismatch);
    }
    if (!decode_observation(upload.bytes, observed)) {
        upload = {};
        return refused(ContentFailure::invalid_native_observation);
    }
    upload = {};
    return authority ? registry.attest(connection, frame.transaction, observed, now_ms)
                     : registry.ready(connection, frame.transaction, observed, now_ms);
}

// The reply names the version a successful commit publishes.
ContentDecision commit_transaction(ContentRegistry& registry, uint64_t connection, const ContentFrame& frame,
                                   uint64_t now_ms, ContentFrame& reply) {
    const auto manifest = registry.proposed(frame.transaction);
    if (manifest) {
        reply.identity = manifest->version;
    }
    ContentDecision decision = registry.commit(connection, frame.transaction, now_ms);
    if (!decision) {
        reply.identity = {};
    }
    return decision;
}

// A player's current version and, when it is published, that version's PNG hash.
ContentDecision serve_current(const ContentRegistry& registry, bool authority, uint64_t player, ContentFrame& reply) {
    if (authority) {
        return refused(ContentFailure::wrong_owner);
    }
    reply.identity = registry.current(player);
    if (auto manifest = registry.published(reply.identity)) {
        reply.data.assign(manifest->png.begin(), manifest->png.end());
    }
    return {};
}

// Carries out one well-formed request and fills the reply fields that are specific to its operation.
template <typename Peer>
ContentDecision apply_request(ContentRegistry& registry, uint64_t connection, const ContentFrame& frame,
                              uint64_t now_ms, Peer& peer, ContentFrame& reply) {
    switch (frame.op) {
    case ContentOp::begin:
        return registry.begin(connection, frame.identity, now_ms);
    case ContentOp::offer:
        return registry.offer(connection, frame.transaction, frame.identity, frame.total, now_ms);
    case ContentOp::chunk:
        return registry.append(connection, frame.transaction, frame.offset, frame.data, now_ms);
    case ContentOp::seal:
        return registry.seal(connection, frame.transaction, now_ms);
    case ContentOp::cancel:
        return registry.cancel(connection, frame.transaction, now_ms);
    case ContentOp::dependency_failure:
        return registry.dependency_failure(connection, frame.transaction, frame.missing, now_ms);
    case ContentOp::dependencies:
        return serve_dependencies(registry, connection, frame, now_ms, reply);
    case ContentOp::fetch:
        return serve_candidate(registry, connection, frame, now_ms, reply);
    case ContentOp::observation_begin:
        return begin_observation(registry, connection, frame, now_ms, peer.observation);
    case ContentOp::observation_chunk:
        return append_observation(registry, connection, frame, now_ms, peer.observation);
    case ContentOp::observation_end:
        return end_observation(registry, connection, frame, now_ms, peer.authority, peer.observation);
    case ContentOp::commit:
        return commit_transaction(registry, connection, frame, now_ms, reply);
    case ContentOp::current:
        return serve_current(registry, peer.authority, peer.player, reply);
    default:
        return refused(ContentFailure::malformed);
    }
}

// The owner of the proposed content learns which part the reporting peer is missing.
template <typename Peers>
void tell_owner_of_missing_part(const Peers& peers, const ContentRegistry& registry, const ContentFrame& frame,
                                std::vector<ContentDispatch>& out) {
    const auto manifest = registry.proposed(frame.transaction);
    if (!manifest) {
        return;
    }
    for (const auto& target : peers) {
        if (!target.second.authority && target.second.player == manifest->owner) {
            ContentFrame event;
            event.op = ContentOp::dependency_event;
            event.transaction = frame.transaction;
            event.failure = ContentFailure::missing_part;
            event.missing = frame.missing;
            out.push_back({target.first, event});
        }
    }
}

// A sealed upload goes to every authority for validation.
template <typename Peers>
void ask_authorities_to_validate(const Peers& peers, const ContentFrame& frame, std::vector<ContentDispatch>& out) {
    ContentFrame event;
    event.op = ContentOp::validate_event;
    event.transaction = frame.transaction;
    for (const auto& target : peers) {
        if (target.second.authority) {
            out.push_back({target.first, event});
        }
    }
}

// Once the authority has attested the content, every player is told to load it.
template <typename Peers>
void tell_players_to_load(const Peers& peers, const ContentRegistry& registry, const ContentFrame& frame,
                          std::vector<ContentDispatch>& out) {
    auto manifest = registry.proposed(frame.transaction);
    if (!manifest) {
        return;
    }
    for (const auto& target : peers) {
        if (!target.second.authority) {
            ContentFrame event;
            event.op = ContentOp::load_event;
            event.transaction = frame.transaction;
            event.identity = manifest->png;
            event.total = manifest->png_bytes;
            event.data.assign(manifest->native_properties.begin(), manifest->native_properties.end());
            out.push_back({target.first, event});
        }
    }
}

// A committed or cancelled transaction ends for every peer, with any observation upload still open for it.
template <typename Peers>
void announce_transaction_end(Peers& peers, const ContentFrame& frame, const Digest& identity,
                              std::vector<ContentDispatch>& out) {
    for (auto& target : peers) {
        if (target.second.observation.transaction == frame.transaction) {
            target.second.observation = {};
        }
        ContentFrame event;
        event.op = frame.op == ContentOp::commit ? ContentOp::published_event : ContentOp::cancelled_event;
        event.transaction = frame.transaction;
        event.identity = identity;
        out.push_back({target.first, event});
    }
}
} // namespace

bool ContentControl::connect(uint64_t connection, uint64_t player, bool authority) {
    if (!registry_.connect(connection, player, authority)) {
        return false;
    }
    for (auto existing = peers_.begin(); existing != peers_.end();) {
        if (!authority && !existing->second.authority && existing->second.player == player) {
            existing = peers_.erase(existing);
        } else {
            ++existing;
        }
    }
    Peer peer;
    peer.player = player;
    peer.authority = authority;
    peers_.emplace(connection, std::move(peer));
    return true;
}

void ContentControl::disconnect(uint64_t connection) {
    auto found = peers_.find(connection);
    const bool authority = found != peers_.end() && found->second.authority;
    registry_.disconnect(connection);
    peers_.erase(connection);
    if (authority) {
        for (auto& remaining : peers_) {
            remaining.second.observation = {};
        }
    }
}

void ContentControl::fence() {
    registry_.fence();
    for (auto& entry : peers_) {
        entry.second.observation = {};
    }
}

std::vector<ContentDispatch> ContentControl::receive(uint64_t connection, const ContentFrame& frame, uint64_t now_ms) {
    std::vector<ContentDispatch> out;
    ContentFrame reply;
    reply.op = frame.op;
    reply.request = frame.request;
    reply.transaction = frame.transaction;
    auto reject = [&](ContentFailure reason) {
        reply.failure = reason;
        out.push_back({connection, reply});
        return out;
    };
    auto found = peers_.find(connection);
    if (found == peers_.end()) {
        return reject(ContentFailure::unknown_connection);
    }
    auto& peer = found->second;
    if (!within_request_rate(peer, now_ms)) {
        return reject(ContentFailure::capacity);
    }
    drop_unadmitted_observations(peers_, registry_, now_ms);
    if (!frame.request || frame.request <= peer.last_request) {
        return reject(ContentFailure::stale_request);
    }
    peer.last_request = frame.request;
    if (request_malformed(frame)) {
        return reject(ContentFailure::malformed);
    }
    const ContentDecision decision = apply_request(registry_, connection, frame, now_ms, peer, reply);
    reply.failure = decision.failure;
    reply.missing = decision.missing;
    reply.world_index = decision.world_index;
    if (frame.op == ContentOp::begin) {
        reply.transaction = decision.transaction;
    }
    if (!decision.detail.empty()) {
        reply.data.assign(decision.detail.begin(), decision.detail.end());
    }
    out.push_back({connection, reply});
    if (!decision) {
        return out;
    }
    if (frame.op == ContentOp::dependency_failure) {
        tell_owner_of_missing_part(peers_, registry_, frame, out);
    }
    if (frame.op == ContentOp::seal) {
        ask_authorities_to_validate(peers_, frame, out);
    }
    if (frame.op == ContentOp::observation_end && peer.authority) {
        tell_players_to_load(peers_, registry_, frame, out);
    }
    if (frame.op == ContentOp::commit || frame.op == ContentOp::cancel) {
        announce_transaction_end(peers_, frame, reply.identity, out);
    }
    return out;
}
} // namespace sporemp::network
