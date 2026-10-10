#pragma once
#include "world_identity.h"
#include <array>
#include <cstdint>
#include <string>

namespace sporemp::network {
// Commands are sent only by the coordinator over an authenticated authority
// Session. A receipt reports completed native work, never IPC acceptance.
enum class TransferOp : uint32_t {
    source_fence = 1,
    destination_restore = 2,
    destination_discard = 3,
    source_restore = 4,
    destination_activate = 5,
    source_activate = 6,
    source_cleanup = 7
};

struct TransferFrame {
    TransferOp op = TransferOp::source_fence;
    uint64_t id = 0, player = 0, revision = 0, avatar_generation = 0, worker = 0, worker_generation = 0;
    Digest preparation{}, challenge{}, artifact{}, approval{}, state{};
    uint32_t artifact_bytes = 0, approval_bytes = 0;
    uint32_t unique_game_id = 0, canonical_home = 0;
    uint32_t source_player = 0, source_avatar = 0, source_slot = 0;
    // Original slot1/slot2 and native noun IDs are independent of canonical
    // player1/player2. A destination can import canonical player1 into slot2.
    uint32_t native_player = 0, native_avatar = 0, native_slot = 0;
    uint64_t pause_request = 0, input_epoch = 0;
    uint64_t app_begin = 0, app_end = 0, ai_begin = 0, ai_end = 0;
    uint64_t zero_deltas = 0, nonzero_deltas = 0, elapsed_ms = 0;
    uint32_t pause_before = 0, pause_after = 0, remaining_imports = 0;
    // Read-only local process identity. Never authorizes process termination.
    uint32_t process_id = 0;
    uint64_t process_created = 0;
};

constexpr size_t transfer_wire_bytes = 576;
using TransferWire = std::array<uint8_t, transfer_wire_bytes>;
bool encode_transfer(const TransferFrame&, TransferWire&) noexcept;
bool decode_transfer(const TransferWire&, TransferFrame&) noexcept;
bool same_transfer_request(const TransferFrame&, const TransferFrame&) noexcept;
// Exact canonical one-line JSON (LF terminated). 'state' is deliberately not
// included: Packet.transfer.state is SHA256 of these exact UTF-8 bytes, avoiding
// a self-referential digest. File name: transfer-witness-<id>-<numeric op>.json.
std::string transfer_witness_json(const TransferFrame&, uint64_t session, uint64_t scene, uint64_t baseline);
bool transfer_witness_digest(const TransferFrame&, uint64_t session, uint64_t scene, uint64_t baseline, Digest&);
} // namespace sporemp::network
