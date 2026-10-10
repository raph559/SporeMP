#include "protocol.h"
#include "steering.h"
#include "content_registry.h"
#include <cmath>
#include <cstring>

namespace sporemp::network {
namespace {
// Scalars are little-endian on the wire. `offset` is the next byte to write or read; each call moves it past the
// value.
template <size_t Size>
void put32(std::array<uint8_t, Size>& wire, size_t& offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        wire[offset++] = uint8_t(value >> (8 * i));
    }
}

template <size_t Size>
void put64(std::array<uint8_t, Size>& wire, size_t& offset, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        wire[offset++] = uint8_t(value >> (8 * i));
    }
}

uint32_t get32(const uint8_t* wire, size_t& offset) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= uint32_t(wire[offset++]) << (8 * i);
    }
    return value;
}

uint64_t get64(const uint8_t* wire, size_t& offset) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= uint64_t(wire[offset++]) << (8 * i);
    }
    return value;
}

void put_float(Wire& wire, size_t& offset, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    put32(wire, offset, bits);
}

// Kept out of line. A float returned through the x87 register arrives with a signaling NaN quieted (bit 22 set),
// and that is what decode produced while it was one function calling this one. Inlined into the functions decode was
// split into, the NaN kept its bits: a differential run of the split against the single function, 1.6 million
// packets, differed in that bit alone and no longer differs.
__declspec(noinline) float get_float(const uint8_t* wire, size_t& offset) {
    uint32_t bits = get32(wire, offset);
    float value = 0;
    std::memcpy(&value, &bits, 4);
    return value;
}

// A digest or a nested wire, byte for byte.
template <size_t Size, size_t Count>
void put_bytes(std::array<uint8_t, Size>& wire, size_t& offset, const std::array<uint8_t, Count>& bytes) {
    for (auto byte : bytes) {
        wire[offset++] = byte;
    }
}

template <size_t Count>
void get_bytes(const uint8_t* wire, size_t& offset, std::array<uint8_t, Count>& bytes) {
    for (auto& byte : bytes) {
        byte = wire[offset++];
    }
}

// The bytes a frame does not use are reserved: whether every one from `offset` to `size` is zero.
bool rest_is_zero(const uint8_t* wire, size_t& offset, size_t size) {
    while (offset < size) {
        if (wire[offset++]) {
            return false;
        }
    }
    return true;
}
} // namespace

bool encode_transfer(const TransferFrame& frame, TransferWire& wire) noexcept {
    if (frame.op < TransferOp::source_fence || frame.op > TransferOp::source_cleanup) {
        return false;
    }
    wire = {};
    size_t offset = 0;
    put32(wire, offset, uint32_t(frame.op));
    for (auto value :
         {frame.id, frame.player, frame.revision, frame.avatar_generation, frame.worker, frame.worker_generation}) {
        put64(wire, offset, value);
    }
    for (const auto* digest : {&frame.preparation, &frame.challenge, &frame.artifact, &frame.approval, &frame.state}) {
        put_bytes(wire, offset, *digest);
    }
    for (auto value :
         {frame.artifact_bytes, frame.approval_bytes, frame.unique_game_id, frame.canonical_home, frame.source_player,
          frame.source_avatar, frame.source_slot, frame.native_player, frame.native_avatar, frame.native_slot}) {
        put32(wire, offset, value);
    }
    for (auto value : {frame.pause_request, frame.input_epoch, frame.app_begin, frame.app_end, frame.ai_begin,
                       frame.ai_end, frame.zero_deltas, frame.nonzero_deltas, frame.elapsed_ms}) {
        put64(wire, offset, value);
    }
    for (auto value : {frame.pause_before, frame.pause_after, frame.remaining_imports, frame.process_id}) {
        put32(wire, offset, value);
    }
    put64(wire, offset, frame.process_created);
    return true;
}

bool decode_transfer(const TransferWire& wire, TransferFrame& result) noexcept {
    const uint8_t* bytes = wire.data();
    size_t offset = 0;
    TransferFrame frame;
    frame.op = TransferOp(get32(bytes, offset));
    frame.id = get64(bytes, offset);
    frame.player = get64(bytes, offset);
    frame.revision = get64(bytes, offset);
    frame.avatar_generation = get64(bytes, offset);
    frame.worker = get64(bytes, offset);
    frame.worker_generation = get64(bytes, offset);
    for (auto* digest : {&frame.preparation, &frame.challenge, &frame.artifact, &frame.approval, &frame.state}) {
        get_bytes(bytes, offset, *digest);
    }
    for (auto* value : {&frame.artifact_bytes, &frame.approval_bytes, &frame.unique_game_id, &frame.canonical_home,
                        &frame.source_player, &frame.source_avatar, &frame.source_slot, &frame.native_player,
                        &frame.native_avatar, &frame.native_slot}) {
        *value = get32(bytes, offset);
    }
    for (auto* value : {&frame.pause_request, &frame.input_epoch, &frame.app_begin, &frame.app_end, &frame.ai_begin,
                        &frame.ai_end, &frame.zero_deltas, &frame.nonzero_deltas, &frame.elapsed_ms}) {
        *value = get64(bytes, offset);
    }
    for (auto* value : {&frame.pause_before, &frame.pause_after, &frame.remaining_imports, &frame.process_id}) {
        *value = get32(bytes, offset);
    }
    frame.process_created = get64(bytes, offset);
    if (!rest_is_zero(bytes, offset, wire.size())) {
        return false;
    }
    if (frame.op < TransferOp::source_fence || frame.op > TransferOp::source_cleanup || !frame.id || frame.player < 1 ||
        frame.player > 2 || !frame.revision || !frame.avatar_generation || !frame.worker || !frame.worker_generation ||
        frame.preparation == Digest{} || frame.challenge == Digest{} || !frame.unique_game_id ||
        !frame.canonical_home || (!frame.source_player && frame.op != TransferOp::source_fence) ||
        !frame.source_avatar || frame.source_slot < 1 || frame.source_slot > 2 || frame.native_slot < 1 ||
        frame.native_slot > 2 || frame.artifact_bytes > 1024 * 1024 || frame.approval_bytes > 20680) {
        return false;
    }
    result = frame;
    return true;
}

bool same_transfer_request(const TransferFrame& a, const TransferFrame& b) noexcept {
    if (a.op != b.op || a.id != b.id || a.player != b.player || a.revision != b.revision ||
        a.avatar_generation != b.avatar_generation || a.preparation != b.preparation || a.challenge != b.challenge ||
        a.unique_game_id != b.unique_game_id || a.worker != b.worker || a.worker_generation != b.worker_generation ||
        a.canonical_home != b.canonical_home ||
        (a.source_player != b.source_player && !(a.op == TransferOp::source_fence && !a.source_player)) ||
        a.source_avatar != b.source_avatar || a.source_slot != b.source_slot || a.native_slot != b.native_slot ||
        a.pause_request != b.pause_request) {
        return false;
    }
    return a.op == TransferOp::source_fence ||
           (a.artifact == b.artifact && a.approval == b.approval && a.artifact_bytes == b.artifact_bytes &&
            a.approval_bytes == b.approval_bytes);
}

std::string transfer_witness_json(const TransferFrame& frame, uint64_t session, uint64_t scene, uint64_t baseline) {
    std::string json = "{\"schema\":1";
    auto number = [&](const char* name, uint64_t value) {
        json += ",\"";
        json += name;
        json += "\":";
        json += std::to_string(value);
    };
    auto digest = [&](const char* name, const Digest& value) {
        json += ",\"";
        json += name;
        json += "\":\"";
        json += hex(value);
        json += '"';
    };
    number("op", uint32_t(frame.op));
    number("id", frame.id);
    number("player", frame.player);
    number("revision", frame.revision);
    number("avatar_generation", frame.avatar_generation);
    number("worker", frame.worker);
    number("worker_generation", frame.worker_generation);
    number("session", session);
    number("scene", scene);
    number("baseline", baseline);
    digest("preparation", frame.preparation);
    digest("challenge", frame.challenge);
    digest("artifact", frame.artifact);
    digest("approval", frame.approval);
    number("artifact_bytes", frame.artifact_bytes);
    number("approval_bytes", frame.approval_bytes);
    number("unique_game_id", frame.unique_game_id);
    number("canonical_home", frame.canonical_home);
    number("source_player", frame.source_player);
    number("source_avatar", frame.source_avatar);
    number("source_slot", frame.source_slot);
    number("native_player", frame.native_player);
    number("native_avatar", frame.native_avatar);
    number("native_slot", frame.native_slot);
    number("pause_request", frame.pause_request);
    number("input_epoch", frame.input_epoch);
    number("app_begin", frame.app_begin);
    number("app_end", frame.app_end);
    number("ai_begin", frame.ai_begin);
    number("ai_end", frame.ai_end);
    number("zero_deltas", frame.zero_deltas);
    number("nonzero_deltas", frame.nonzero_deltas);
    number("elapsed_ms", frame.elapsed_ms);
    number("pause_before", frame.pause_before);
    number("pause_after", frame.pause_after);
    number("remaining_imports", frame.remaining_imports);
    number("process_id", frame.process_id);
    number("process_created", frame.process_created);
    json += "}\n";
    return json;
}

bool transfer_witness_digest(const TransferFrame& frame, uint64_t session, uint64_t scene, uint64_t baseline,
                             Digest& digest) {
    const auto json = transfer_witness_json(frame, session, scene, baseline);
    return content_digest(reinterpret_cast<const uint8_t*>(json.data()), json.size(), digest);
}

bool Identity::operator==(const Identity& other) const noexcept {
    return build == other.build && executable == other.executable && content == other.content &&
           fixture == other.fixture && world == other.world;
}

// A packet is an envelope and then what its kind carries. Each part is written by a `put_` function and read back by
// the `get_` function below it, field for field in the same order; a `get_` function also refuses what its part
// does not admit and names the reason in `error`.
namespace {
// The first word of every packet; on the wire its bytes read "SMP6".
constexpr uint32_t envelope_signature = 0x36504d53;

bool reserved_is_zero(const uint8_t* wire, size_t& offset, std::string& error) {
    if (!rest_is_zero(wire, offset, packet_bytes)) {
        error = "nonzero_reserved";
        return false;
    }
    return true;
}

void put_envelope(Wire& wire, size_t& offset, const Packet& packet) {
    for (auto value : {envelope_signature, uint32_t(packet_bytes), protocol_version, schema_version,
                       uint32_t(packet.kind), uint32_t(packet.role), uint32_t(packet.error), packet.count}) {
        put32(wire, offset, value);
    }
    for (auto value : {packet.sequence, packet.session, packet.scene, packet.baseline, packet.player}) {
        put64(wire, offset, value);
    }
}

bool get_envelope(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    // Each read advances the offset: the two sides of an `||` read different words.
    // NOLINTNEXTLINE(misc-redundant-expression)
    if (get32(wire, offset) != envelope_signature || get32(wire, offset) != packet_bytes) {
        error = "invalid_envelope";
        return false;
    }
    // NOLINTNEXTLINE(misc-redundant-expression)
    if (get32(wire, offset) != protocol_version || get32(wire, offset) != schema_version) {
        error = "unsupported_protocol_or_schema";
        return false;
    }
    auto kind = get32(wire, offset), role = get32(wire, offset), code = get32(wire, offset);
    packet.count = get32(wire, offset);
    if (kind < 1 || kind > uint32_t(Kind::relationship) || role < 1 || role > 2 ||
        code > uint32_t(Error::world_mismatch) || packet.count > max_entities) {
        error = "invalid_enum_or_count";
        return false;
    }
    packet.kind = Kind(kind);
    packet.role = Role(role);
    packet.error = Error(code);
    packet.sequence = get64(wire, offset);
    packet.session = get64(wire, offset);
    packet.scene = get64(wire, offset);
    packet.baseline = get64(wire, offset);
    packet.player = get64(wire, offset);
    return true;
}

// Where a packet belongs: its location, worker generation and native load slot.
void put_location_stamp(Wire& wire, size_t& offset, const Packet& packet) {
    for (auto value : {packet.location.galaxy, packet.location.star, packet.location.planet, packet.location.area,
                       packet.worker_generation}) {
        put64(wire, offset, value);
    }
    put32(wire, offset, packet.native_load_slot);
}

void get_location_stamp(const uint8_t* wire, size_t& offset, Packet& packet) {
    packet.location.galaxy = get64(wire, offset);
    packet.location.star = get64(wire, offset);
    packet.location.planet = get64(wire, offset);
    packet.location.area = get64(wire, offset);
    packet.worker_generation = get64(wire, offset);
    packet.native_load_slot = get32(wire, offset);
}

// What the envelope of every frame about one scene holds: no count and no error, and a session, a scene and a
// baseline.
bool scene_frame_envelope(const Packet& packet) {
    return !(packet.count || packet.error != Error::none || !packet.session || !packet.scene || !packet.baseline);
}

void put_relationship(Wire& wire, size_t& offset, const Packet& packet) {
    const auto& frame = packet.relationship;
    for (auto value : {frame.entity, frame.generation, frame.revision}) {
        put64(wire, offset, value);
    }
    put32(wire, offset, frame.count);
    for (const auto& entry : frame.entries) {
        for (auto value : entry.key) {
            put32(wire, offset, value);
        }
        for (auto value : entry.words) {
            put32(wire, offset, value);
        }
    }
    put_location_stamp(wire, offset, packet);
}

bool get_relationship(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    auto& frame = packet.relationship;
    frame.entity = get64(wire, offset);
    frame.generation = get64(wire, offset);
    frame.revision = get64(wire, offset);
    frame.count = get32(wire, offset);
    for (auto& entry : frame.entries) {
        for (auto& value : entry.key) {
            value = get32(wire, offset);
        }
        for (auto& value : entry.words) {
            value = get32(wire, offset);
        }
    }
    get_location_stamp(wire, offset, packet);
    if (!scene_frame_envelope(packet) || packet.player != 2 || packet.role != Role::authority ||
        packet.native_load_slot > 1 || !valid_relationship_frame(frame)) {
        error = "invalid_relationship_frame";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

void put_history(Wire& wire, size_t& offset, const Packet& packet) {
    const auto& frame = packet.history;
    for (auto value : {frame.entity, frame.generation, frame.revision}) {
        put64(wire, offset, value);
    }
    for (auto value : frame.state.date) {
        put32(wire, offset, value);
    }
    put32(wire, offset, frame.state.count);
    for (const auto& entry : frame.state.entries) {
        for (auto value : entry.header) {
            put32(wire, offset, value);
        }
        for (auto value : entry.data) {
            put32(wire, offset, value);
        }
    }
    put_location_stamp(wire, offset, packet);
}

bool get_history(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    auto& frame = packet.history;
    frame.entity = get64(wire, offset);
    frame.generation = get64(wire, offset);
    frame.revision = get64(wire, offset);
    for (auto& value : frame.state.date) {
        value = get32(wire, offset);
    }
    frame.state.count = get32(wire, offset);
    for (auto& entry : frame.state.entries) {
        for (auto& value : entry.header) {
            value = get32(wire, offset);
        }
        for (auto& value : entry.data) {
            value = get32(wire, offset);
        }
    }
    get_location_stamp(wire, offset, packet);
    if (!scene_frame_envelope(packet) || packet.player != 2 || packet.role != Role::authority ||
        packet.native_load_slot > 1 || !valid_history_frame(frame)) {
        error = "invalid_history_frame";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

// hatch_view from the authority and hatch_input from a player share one frame.
void put_hatch(Wire& wire, size_t& offset, const Packet& packet) {
    const auto& frame = packet.hatch;
    for (auto value : {frame.entity, frame.generation, frame.serial}) {
        put64(wire, offset, value);
    }
    for (auto value : {frame.active, frame.hint, frame.ability, frame.mode, frame.choice}) {
        put32(wire, offset, value);
    }
    put_location_stamp(wire, offset, packet);
}

bool get_hatch(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    auto& frame = packet.hatch;
    frame.entity = get64(wire, offset);
    frame.generation = get64(wire, offset);
    frame.serial = get64(wire, offset);
    frame.active = get32(wire, offset);
    frame.hint = get32(wire, offset);
    frame.ability = get32(wire, offset);
    frame.mode = get32(wire, offset);
    frame.choice = get32(wire, offset);
    get_location_stamp(wire, offset, packet);
    const bool input = packet.kind == Kind::hatch_input;
    if (!scene_frame_envelope(packet) || packet.player < 1 || packet.player > 2 ||
        packet.role != (input ? Role::player : Role::authority) || packet.native_load_slot > 1 ||
        !valid_hatch(frame, input)) {
        error = "invalid_hatch_frame";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

void put_inventory(Wire& wire, size_t& offset, const Packet& packet) {
    const auto& frame = packet.inventory;
    for (auto value : {frame.entity, frame.generation, frame.revision}) {
        put64(wire, offset, value);
    }
    for (auto value : {frame.total, frame.offset, frame.count, frame.points, frame.group}) {
        put32(wire, offset, value);
    }
    for (const auto& item : frame.items) {
        for (auto value : {item.instance, item.group, item.status, item.index}) {
            put32(wire, offset, value);
        }
    }
    put_location_stamp(wire, offset, packet);
}

bool get_inventory(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    auto& frame = packet.inventory;
    frame.entity = get64(wire, offset);
    frame.generation = get64(wire, offset);
    frame.revision = get64(wire, offset);
    frame.total = get32(wire, offset);
    frame.offset = get32(wire, offset);
    frame.count = get32(wire, offset);
    frame.points = get32(wire, offset);
    frame.group = get32(wire, offset);
    for (auto& item : frame.items) {
        item.instance = get32(wire, offset);
        item.group = get32(wire, offset);
        item.status = get32(wire, offset);
        item.index = get32(wire, offset);
    }
    get_location_stamp(wire, offset, packet);
    if (!scene_frame_envelope(packet) || packet.player < 1 || packet.player > 2 || packet.role != Role::authority ||
        packet.native_load_slot > 1 || !valid_inventory_frame(frame)) {
        error = "invalid_inventory_frame";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

void put_social(Wire& wire, size_t& offset, const Packet& packet) {
    const auto& frame = packet.social;
    for (auto value : {frame.entity, frame.generation, frame.target, frame.target_generation}) {
        put64(wire, offset, value);
    }
    for (auto value : {frame.visible, frame.flags, frame.mode}) {
        put32(wire, offset, value);
    }
    for (auto value : frame.values) {
        put_float(wire, offset, value);
    }
    put32(wire, offset, frame.relation);
    for (auto value : frame.segment_count) {
        put32(wire, offset, value);
    }
    for (const auto& side : frame.segments) {
        for (const auto& segment : side) {
            put_float(wire, offset, segment.end);
            put_float(wire, offset, segment.steps);
            put32(wire, offset, segment.color);
            put_float(wire, offset, segment.duration);
            put_float(wire, offset, segment.speed);
        }
    }
    put_location_stamp(wire, offset, packet);
}

bool get_social(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    auto& frame = packet.social;
    frame.entity = get64(wire, offset);
    frame.generation = get64(wire, offset);
    frame.target = get64(wire, offset);
    frame.target_generation = get64(wire, offset);
    frame.visible = get32(wire, offset);
    frame.flags = get32(wire, offset);
    frame.mode = get32(wire, offset);
    for (auto& value : frame.values) {
        value = get_float(wire, offset);
    }
    frame.relation = get32(wire, offset);
    for (auto& value : frame.segment_count) {
        value = get32(wire, offset);
    }
    for (auto& side : frame.segments) {
        for (auto& segment : side) {
            segment.end = get_float(wire, offset);
            segment.steps = get_float(wire, offset);
            segment.color = get32(wire, offset);
            segment.duration = get_float(wire, offset);
            segment.speed = get_float(wire, offset);
        }
    }
    get_location_stamp(wire, offset, packet);
    if (!scene_frame_envelope(packet) || packet.player < 1 || packet.player > 2 || packet.role != Role::authority ||
        packet.native_load_slot > 1 || !valid_social_frame(frame)) {
        error = "invalid_social_view";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

// death from the authority and death_done from a player share one frame.
void put_death(Wire& wire, size_t& offset, const Packet& packet) {
    put64(wire, offset, packet.death.occurrence);
    put64(wire, offset, packet.death.entity);
    put64(wire, offset, packet.death.generation);
    put32(wire, offset, packet.death.reason);
    put_location_stamp(wire, offset, packet);
}

bool get_death(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    packet.death.occurrence = get64(wire, offset);
    packet.death.entity = get64(wire, offset);
    packet.death.generation = get64(wire, offset);
    packet.death.reason = get32(wire, offset);
    get_location_stamp(wire, offset, packet);
    if (!scene_frame_envelope(packet) || packet.player < 1 || packet.player > 2 ||
        packet.role != (packet.kind == Kind::death ? Role::authority : Role::player) || !valid_death(packet.death) ||
        packet.native_load_slot > 1) {
        error = "invalid_death_frame";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

// tutorial from the authority and tutorial_done from a player share one frame.
void put_tutorial(Wire& wire, size_t& offset, const Packet& packet) {
    put64(wire, offset, packet.tutorial.entity);
    put64(wire, offset, packet.tutorial.generation);
    put32(wire, offset, packet.tutorial.token);
    put32(wire, offset, packet.tutorial.caption);
    put_location_stamp(wire, offset, packet);
}

bool get_tutorial(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    packet.tutorial.entity = get64(wire, offset);
    packet.tutorial.generation = get64(wire, offset);
    packet.tutorial.token = get32(wire, offset);
    packet.tutorial.caption = get32(wire, offset);
    get_location_stamp(wire, offset, packet);
    if (!scene_frame_envelope(packet) || packet.player < 1 || packet.player > 2 ||
        packet.role != (packet.kind == Kind::tutorial ? Role::authority : Role::player) ||
        !valid_tutorial(packet.tutorial) || packet.native_load_slot > 1) {
        error = "invalid_tutorial_frame";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

// An identity's build, executable, content and fixture digests; its world digests are written apart from them.
void put_build_identity(Wire& wire, size_t& offset, const Identity& identity) {
    for (const auto* digest : {&identity.build, &identity.executable, &identity.content, &identity.fixture}) {
        put_bytes(wire, offset, *digest);
    }
}

void get_build_identity(const uint8_t* wire, size_t& offset, Identity& identity) {
    for (auto* digest : {&identity.build, &identity.executable, &identity.content, &identity.fixture}) {
        get_bytes(wire, offset, *digest);
    }
}

void put_world_identity(Wire& wire, size_t& offset, const WorldIdentity& world) {
    for (const auto& digest : world) {
        put_bytes(wire, offset, digest);
    }
}

void get_world_identity(const uint8_t* wire, size_t& offset, WorldIdentity& world) {
    for (auto& digest : world) {
        get_bytes(wire, offset, digest);
    }
}

void put_preload(Wire& wire, size_t& offset, const Packet& packet) {
    const auto& frame = packet.preload;
    put64(wire, offset, frame.nonce);
    for (auto value : {frame.location.galaxy, frame.location.star, frame.location.planet, frame.location.area,
                       frame.worker, frame.worker_generation, frame.checkpoint_revision}) {
        put64(wire, offset, value);
    }
    for (const auto* digest : {&frame.checkpoint, &frame.universe}) {
        put_bytes(wire, offset, *digest);
    }
    for (const auto& owner : frame.owners) {
        put64(wire, offset, owner.worker);
        put64(wire, offset, owner.generation);
    }
    put32(wire, offset, frame.ttl_ms);
    put32(wire, offset, frame.native_load_slot);
    put64(wire, offset, frame.maintenance_transfer);
    put32(wire, offset, frame.process_id);
    put64(wire, offset, frame.process_created);
    put_build_identity(wire, offset, packet.identity);
    put_world_identity(wire, offset, packet.identity.world);
    put_bytes(wire, offset, packet.credential);
    put32(wire, offset, frame.producer_mask);
}

// A request names who asks and nothing a grant decides; a grant names all of it and carries no identity or
// credential.
bool valid_preload(const Packet& packet) {
    const auto& frame = packet.preload;
    bool valid = frame.nonce && frame.location.valid() && frame.native_load_slot <= 1 && frame.process_id &&
                 frame.process_created;
    if (packet.kind == Kind::preload_request) {
        valid = valid && !frame.worker && !frame.checkpoint_revision && frame.checkpoint == Digest{} &&
                frame.universe == Digest{} && !frame.ttl_ms && !frame.producer_mask &&
                (packet.role == Role::authority ? frame.worker_generation != 0
                                                : !frame.worker_generation && !frame.maintenance_transfer) &&
                packet.credential != Digest{} && packet.identity.build != Digest{} &&
                packet.identity.executable != Digest{} && packet.identity.content != Digest{} &&
                packet.identity.fixture != Digest{} && valid_world_identity(packet.identity.world);
        for (const auto& owner : frame.owners) {
            valid = valid && !owner.worker && !owner.generation;
        }
    } else {
        valid = valid && frame.worker && frame.worker_generation && frame.checkpoint_revision &&
                frame.checkpoint != Digest{} && frame.universe != Digest{} && frame.ttl_ms &&
                frame.ttl_ms <= preload_ttl_ms && (frame.producer_mask == 0 || frame.producer_mask == 15) &&
                (packet.role == Role::authority || !frame.producer_mask) && packet.credential == Digest{} &&
                packet.identity == Identity{};
        for (const auto& owner : frame.owners) {
            valid = valid && owner.worker && owner.generation && owner.worker == frame.owners[0].worker &&
                    owner.generation == frame.owners[0].generation;
        }
        if (frame.producer_mask) {
            valid = valid && frame.worker == frame.owners[0].worker &&
                    frame.worker_generation == frame.owners[0].generation;
        }
    }
    return valid;
}

bool get_preload(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    if (packet.count || packet.session || packet.scene || packet.baseline || packet.player ||
        packet.error != Error::none) {
        error = "invalid_preload_header";
        return false;
    }
    auto& frame = packet.preload;
    frame.nonce = get64(wire, offset);
    frame.location.galaxy = get64(wire, offset);
    frame.location.star = get64(wire, offset);
    frame.location.planet = get64(wire, offset);
    frame.location.area = get64(wire, offset);
    frame.worker = get64(wire, offset);
    frame.worker_generation = get64(wire, offset);
    frame.checkpoint_revision = get64(wire, offset);
    for (auto* digest : {&frame.checkpoint, &frame.universe}) {
        get_bytes(wire, offset, *digest);
    }
    for (auto& owner : frame.owners) {
        owner.worker = get64(wire, offset);
        owner.generation = get64(wire, offset);
    }
    frame.ttl_ms = get32(wire, offset);
    frame.native_load_slot = get32(wire, offset);
    frame.maintenance_transfer = get64(wire, offset);
    frame.process_id = get32(wire, offset);
    frame.process_created = get64(wire, offset);
    get_build_identity(wire, offset, packet.identity);
    get_world_identity(wire, offset, packet.identity.world);
    get_bytes(wire, offset, packet.credential);
    frame.producer_mask = get32(wire, offset);
    if (!valid_preload(packet)) {
        error = "invalid_preload_frame";
        return false;
    }
    return reserved_is_zero(wire, offset, error);
}

void put_transfer(Wire& wire, size_t& offset, const Packet& packet) {
    TransferWire transfer{};
    encode_transfer(packet.transfer, transfer);
    put_bytes(wire, offset, transfer);
}

bool get_transfer(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    if (packet.count || !packet.scene || packet.player || packet.role != Role::authority ||
        packet.error != Error::none) {
        error = "invalid_transfer_header";
        return false;
    }
    TransferWire transfer{};
    get_bytes(wire, offset, transfer);
    if (!rest_is_zero(wire, offset, packet_bytes) || !decode_transfer(transfer, packet.transfer)) {
        error = "invalid_transfer_frame";
        return false;
    }
    return true;
}

void put_content(Wire& wire, size_t& offset, const Packet& packet) {
    put_bytes(wire, offset, packet.content);
}

bool get_content(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    if (packet.count || packet.scene || packet.baseline || packet.player || packet.error != Error::none) {
        error = "invalid_content_header";
        return false;
    }
    get_bytes(wire, offset, packet.content);
    if (!reserved_is_zero(wire, offset, error)) {
        return false;
    }
    ContentFrame frame;
    if (!decode_content(packet.content, frame)) {
        error = "invalid_content_frame";
        return false;
    }
    return true;
}

// An entity is not contiguous in the common encoding: an action's fields and what later schemas appended sit between
// its parts. First its identity, its species, its pose and its vitals.
void put_entity_body(Wire& wire, size_t& offset, const Entity& entity) {
    for (auto value : {entity.id, entity.generation, entity.owner, entity.tick}) {
        put64(wire, offset, value);
    }
    for (auto value : {entity.native_id, entity.herd_native_id, entity.species_instance, entity.species_type,
                       entity.species_group, entity.archetype}) {
        put32(wire, offset, value);
    }
    for (auto value : {entity.x, entity.y, entity.z, entity.qx, entity.qy, entity.qz, entity.qw, entity.health,
                       entity.energy, entity.hunger, entity.dna}) {
        put_float(wire, offset, value);
    }
}

void get_entity_body(const uint8_t* wire, size_t& offset, Entity& entity) {
    entity.id = get64(wire, offset);
    entity.generation = get64(wire, offset);
    entity.owner = get64(wire, offset);
    entity.tick = get64(wire, offset);
    entity.native_id = get32(wire, offset);
    entity.herd_native_id = get32(wire, offset);
    entity.species_instance = get32(wire, offset);
    entity.species_type = get32(wire, offset);
    entity.species_group = get32(wire, offset);
    entity.archetype = get32(wire, offset);
    entity.x = get_float(wire, offset);
    entity.y = get_float(wire, offset);
    entity.z = get_float(wire, offset);
    entity.qx = get_float(wire, offset);
    entity.qy = get_float(wire, offset);
    entity.qz = get_float(wire, offset);
    entity.qw = get_float(wire, offset);
    entity.health = get_float(wire, offset);
    entity.energy = get_float(wire, offset);
    entity.hunger = get_float(wire, offset);
    entity.dna = get_float(wire, offset);
}

// An entity's age, alpha flag and combatant state share one word, a byte each. A value the wire does not admit is
// written as 0xff; the fourth byte is reserved.
uint32_t pack_entity_context(const Entity& entity) {
    return (entity.age <= 1 ? entity.age : 0xffu) | ((entity.alpha <= 1 ? entity.alpha : 0xffu) << 8) |
           ((entity.combatant_state == 0 || entity.combatant_state == 2 ? entity.combatant_state : 0xffu) << 16);
}

bool unpack_entity_context(uint32_t context, Entity& entity) {
    entity.age = context & 0xffu;
    entity.alpha = (context >> 8) & 0xffu;
    entity.combatant_state = (context >> 16) & 0xffu;
    return !(context >> 24);
}

// Whether an entity is alive, its packed context, its scale and what feeding has left of it.
void put_entity_life(Wire& wire, size_t& offset, const Entity& entity) {
    put32(wire, offset, entity.life_state);
    put32(wire, offset, pack_entity_context(entity));
    put_float(wire, offset, entity.scale);
    put32(wire, offset, entity.fed_on);
    put_float(wire, offset, entity.food);
    put32(wire, offset, entity.pickup_owner);
}

// False when the reserved byte of the context word is set.
bool get_entity_life(const uint8_t* wire, size_t& offset, Entity& entity) {
    entity.life_state = get32(wire, offset);
    if (!unpack_entity_context(get32(wire, offset), entity)) {
        return false;
    }
    entity.scale = get_float(wire, offset);
    entity.fed_on = get32(wire, offset);
    entity.food = get_float(wire, offset);
    entity.pickup_owner = get32(wire, offset);
    return true;
}

// An entity's kind and model, then its recharging words (schema 28) and its fruit state (schema 32).
void put_entity_kind(Wire& wire, size_t& offset, const Entity& entity) {
    put32(wire, offset, uint32_t(entity.kind));
    for (auto value : {entity.model_instance, entity.model_type, entity.model_group}) {
        put32(wire, offset, value);
    }
    for (auto value : entity.recharging) {
        put32(wire, offset, value);
    }
    put32(wire, offset, entity.fruit_state);
}

void get_entity_kind(const uint8_t* wire, size_t& offset, Entity& entity) {
    entity.kind = EntityKind(get32(wire, offset));
    entity.model_instance = get32(wire, offset);
    entity.model_type = get32(wire, offset);
    entity.model_group = get32(wire, offset);
    for (auto& value : entity.recharging) {
        value = get32(wire, offset);
    }
    entity.fruit_state = get32(wire, offset);
}

// What a packet in the common encoding must satisfy once it is read. The reserved bytes are checked after the header
// and before the entity and the action, so that each refusal keeps its reason.
bool admit_common(const uint8_t* wire, size_t& offset, const Packet& packet, std::string& error) {
    if (packet.maintenance_transfer && packet.kind != Kind::hello && packet.kind != Kind::welcome) {
        error = "invalid_maintenance_header";
        return false;
    }
    if (packet.native_load_slot > 1) {
        error = "invalid_native_load_slot";
        return false;
    }
    if (packet.error == Error::world_mismatch ? packet.kind != Kind::reject || packet.world_index >= world_file_count
                                              : packet.world_index != 0) {
        error = "invalid_world_mismatch_index";
        return false;
    }
    if (!reserved_is_zero(wire, offset, error)) {
        return false;
    }
    if (packet.kind == Kind::entity || packet.kind == Kind::motion) {
        if (!valid_entity(packet.entity)) {
            error = "invalid_entity";
            return false;
        }
    }
    if (packet.kind == Kind::action && !valid_action(packet)) {
        error = "invalid_action";
        return false;
    }
    return true;
}

// The common encoding: every kind that has no frame of its own carries an identity, a credential, an entity and an
// action, whichever of them it uses.
void put_common(Wire& wire, size_t& offset, const Packet& packet) {
    const auto& entity = packet.entity;
    put_build_identity(wire, offset, packet.identity);
    put_bytes(wire, offset, packet.credential);
    put_entity_body(wire, offset, entity);
    put64(wire, offset, packet.target);
    put32(wire, offset, uint32_t(packet.verb));
    put32(wire, offset, uint32_t(packet.direction));
    put64(wire, offset, packet.request);
    for (auto value : {entity.vx, entity.vy, entity.vz}) {
        put_float(wire, offset, value);
    }
    put64(wire, offset, packet.target_generation);
    put_entity_life(wire, offset, entity);
    // Schema 5 appends fields; all schema 4 scalar offsets remain unchanged.
    put_world_identity(wire, offset, packet.identity.world);
    put32(wire, offset, packet.world_index);
    put_location_stamp(wire, offset, packet);
    put64(wire, offset, packet.maintenance_transfer);
    put_entity_kind(wire, offset, entity);
}

bool get_common(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    auto& entity = packet.entity;
    get_build_identity(wire, offset, packet.identity);
    get_bytes(wire, offset, packet.credential);
    get_entity_body(wire, offset, entity);
    packet.target = get64(wire, offset);
    packet.verb = Verb(get32(wire, offset));
    packet.direction = int32_t(get32(wire, offset));
    packet.request = get64(wire, offset);
    entity.vx = get_float(wire, offset);
    entity.vy = get_float(wire, offset);
    entity.vz = get_float(wire, offset);
    packet.target_generation = get64(wire, offset);
    if (!get_entity_life(wire, offset, entity)) {
        error = "nonzero_reserved";
        return false;
    }
    get_world_identity(wire, offset, packet.identity.world);
    packet.world_index = get32(wire, offset);
    get_location_stamp(wire, offset, packet);
    packet.maintenance_transfer = get64(wire, offset);
    get_entity_kind(wire, offset, entity);
    return admit_common(wire, offset, packet, error);
}

// What follows the envelope, by kind.
void put_payload(Wire& wire, size_t& offset, const Packet& packet) {
    switch (packet.kind) {
    case Kind::relationship:
        return put_relationship(wire, offset, packet);
    case Kind::history:
        return put_history(wire, offset, packet);
    case Kind::hatch_view:
    case Kind::hatch_input:
        return put_hatch(wire, offset, packet);
    case Kind::inventory:
        return put_inventory(wire, offset, packet);
    case Kind::social_view:
        return put_social(wire, offset, packet);
    case Kind::death:
    case Kind::death_done:
        return put_death(wire, offset, packet);
    case Kind::tutorial:
    case Kind::tutorial_done:
        return put_tutorial(wire, offset, packet);
    case Kind::preload_request:
    case Kind::preload_grant:
        return put_preload(wire, offset, packet);
    case Kind::transfer_command:
    case Kind::transfer_receipt:
        return put_transfer(wire, offset, packet);
    case Kind::content_request:
    case Kind::content_response:
        return put_content(wire, offset, packet);
    default:
        return put_common(wire, offset, packet);
    }
}

bool get_payload(const uint8_t* wire, size_t& offset, Packet& packet, std::string& error) {
    switch (packet.kind) {
    case Kind::relationship:
        return get_relationship(wire, offset, packet, error);
    case Kind::history:
        return get_history(wire, offset, packet, error);
    case Kind::hatch_view:
    case Kind::hatch_input:
        return get_hatch(wire, offset, packet, error);
    case Kind::inventory:
        return get_inventory(wire, offset, packet, error);
    case Kind::social_view:
        return get_social(wire, offset, packet, error);
    case Kind::death:
    case Kind::death_done:
        return get_death(wire, offset, packet, error);
    case Kind::tutorial:
    case Kind::tutorial_done:
        return get_tutorial(wire, offset, packet, error);
    case Kind::preload_request:
    case Kind::preload_grant:
        return get_preload(wire, offset, packet, error);
    case Kind::transfer_command:
    case Kind::transfer_receipt:
        return get_transfer(wire, offset, packet, error);
    case Kind::content_request:
    case Kind::content_response:
        return get_content(wire, offset, packet, error);
    default:
        return get_common(wire, offset, packet, error);
    }
}
} // namespace

Wire encode(const Packet& packet) {
    Wire wire{};
    size_t offset = 0;
    put_envelope(wire, offset, packet);
    put_payload(wire, offset, packet);
    return wire;
}

bool decode(const uint8_t* wire, size_t size, Packet& packet, std::string& error) {
    if (!wire || size != packet_bytes) {
        error = "invalid_packet_length";
        return false;
    }
    size_t offset = 0;
    Packet decoded;
    if (!get_envelope(wire, offset, decoded, error) || !get_payload(wire, offset, decoded, error)) {
        return false;
    }
    packet = decoded;
    error.clear();
    return true;
}

bool valid_entity(const Entity& entity) noexcept {
    if (entity.kind == EntityKind::fossil) {
        // Original Creature ornaments expose an instance-only model key;
        // trial73 records zero type/group. Preserve those native values.
        if (entity.owner || entity.native_id == UINT32_MAX || !entity.model_instance || entity.herd_native_id ||
            entity.species_instance || entity.species_type || entity.species_group || entity.archetype ||
            entity.health || entity.energy || entity.hunger || entity.dna || entity.vx || entity.vy || entity.vz ||
            entity.life_state || entity.age != 1 || entity.alpha || entity.combatant_state || entity.fed_on ||
            entity.pickup_owner || entity.food) {
            return false;
        }
    } else if (entity.kind == EntityKind::nest) {
        if (entity.owner || entity.native_id == UINT32_MAX || !entity.herd_native_id ||
            entity.herd_native_id == UINT32_MAX || !entity.species_instance || !entity.species_type ||
            !entity.species_group || entity.health || entity.energy || entity.hunger || entity.dna || entity.vx ||
            entity.vy || entity.vz || entity.life_state || entity.age != 1 || entity.alpha || entity.combatant_state ||
            entity.fed_on || entity.pickup_owner || entity.food) {
            return false;
        }
    } else if (entity.kind == EntityKind::fruit) {
        // A fruit is one of the original's own nouns, the same number in every game that loaded the save.
        FruitState state;
        if (entity.owner || entity.native_id == UINT32_MAX || !entity.model_instance || entity.herd_native_id ||
            entity.species_instance || entity.species_type || entity.species_group || entity.archetype ||
            entity.health || entity.energy || entity.hunger || entity.dna || entity.vx || entity.vy || entity.vz ||
            entity.life_state || entity.age != 1 || entity.alpha || entity.combatant_state || entity.fed_on ||
            entity.pickup_owner || entity.food || !unpack_fruit_state(entity.fruit_state, state)) {
            return false;
        }
    } else if (entity.kind != EntityKind::animal || entity.model_instance || entity.model_type || entity.model_group) {
        return false;
    }
    if ((entity.recharging[0] || entity.recharging[1] || entity.recharging[2]) &&
        (entity.kind != EntityKind::animal || !entity.owner)) {
        return false;
    }
    if (entity.recharging[2] >> 24) {
        return false;
    }
    if (entity.kind != EntityKind::fruit && entity.fruit_state) {
        return false;
    }
    if (!entity.id || !entity.generation || entity.owner > 2 || entity.life_state > 1 ||
        (entity.life_state && entity.owner && entity.health != 0) || entity.age > 1 || entity.alpha > 1 ||
        (entity.combatant_state != 0 && entity.combatant_state != 2) || entity.fed_on > 1 || entity.pickup_owner > 2 ||
        (!entity.fed_on && entity.pickup_owner)) {
        return false;
    }
    for (auto value : {entity.x, entity.y, entity.z, entity.qx, entity.qy, entity.qz, entity.qw, entity.health,
                       entity.energy, entity.hunger, entity.dna, entity.vx, entity.vy, entity.vz}) {
        if (!std::isfinite(value) || std::abs(value) > 1.0e9f) {
            return false;
        }
    }
    return entity.health >= 0 && entity.energy >= 0 && entity.hunger >= 0 && entity.dna >= 0 &&
           std::isfinite(entity.scale) && entity.scale > 0 && entity.scale <= 1000 && std::isfinite(entity.food) &&
           std::abs(entity.food) <= 1.0e9f;
}

bool unchanged_static_entity(const Entity& a, const Entity& b) noexcept {
    // Tick alone is not a change to an original static resource. Never hide an
    // invalid state or an identity/affiliation change from ordinary admission.
    return (a.kind == EntityKind::fossil || a.kind == EntityKind::nest || a.kind == EntityKind::fruit) &&
           a.kind == b.kind && a.fruit_state == b.fruit_state && valid_entity(a) && valid_entity(b) && a.id == b.id &&
           a.generation == b.generation && a.native_id == b.native_id && a.herd_native_id == b.herd_native_id &&
           a.species_instance == b.species_instance && a.species_type == b.species_type &&
           a.species_group == b.species_group && a.archetype == b.archetype && a.model_instance == b.model_instance &&
           a.model_type == b.model_type && a.model_group == b.model_group && a.x == b.x && a.y == b.y && a.z == b.z &&
           a.qx == b.qx && a.qy == b.qy && a.qz == b.qz && a.qw == b.qw && a.scale == b.scale;
}

bool targeted(Verb verb) noexcept {
    return verb == Verb::attack || verb == Verb::approach || verb == Verb::engage || verb == Verb::pickup ||
           verb == Verb::social || verb == Verb::combat || verb == Verb::collect;
}

bool eligible_target(Verb verb, const Entity& entity) noexcept {
    if (entity.owner) {
        return false;
    }
    if (verb == Verb::collect) {
        return entity.kind == EntityKind::fossil;
    }
    if (entity.kind != EntityKind::animal) {
        return false;
    }
    return verb == Verb::pickup ? entity.life_state != 0 && entity.food > 0
                                : entity.life_state == 0 && entity.health > 0;
}

bool valid_action(const Packet& packet) noexcept {
    const auto verb = uint32_t(packet.verb);
    if (verb > 13 || verb == 4 || !packet.entity.id || !packet.entity.generation) {
        return false;
    }
    // Schema 29: plant food is no shared entity; the fruit is named by the original's own number for it.
    // Schema 31: with the place the player saw it at, packed in the target word (no entity is meant by it).
    if (packet.verb == Verb::forage) {
        float place[3]{};
        return unpack_place(packet.target, place) && !packet.target_generation && packet.direction > 0;
    }
    if (packet.verb == Verb::steer_stop) {
        return !packet.target && !packet.target_generation && !packet.direction;
    }
    if (packet.verb == Verb::steer) {
        SteeringVector vector;
        return !packet.target && !packet.target_generation && unpack_steering(packet.direction, vector);
    }
    if (packet.verb == Verb::social) {
        return packet.direction >= -1 && packet.direction < 88 && packet.target && packet.target_generation;
    }
    if (packet.verb == Verb::combat) {
        return packet.direction >= 0 && packet.direction < 88 && packet.target && packet.target_generation;
    }
    if (packet.direction < -1 || packet.direction > 3) {
        return false;
    }
    return targeted(packet.verb) ? packet.target && packet.target_generation && !packet.direction
                                 : !packet.target && !packet.target_generation;
}

std::string hex(const Digest& digest) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (auto byte : digest) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

bool parse_hex(const std::string& text, Digest& digest) noexcept {
    if (text.size() != 64) {
        return false;
    }
    Digest parsed{};
    auto digit = [](char character) -> int {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    };
    for (size_t i = 0; i < 32; ++i) {
        int high = digit(text[i * 2]), low = digit(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        parsed[i] = uint8_t(high * 16 + low);
    }
    digest = parsed;
    return true;
}

const char* error_name(Error error) noexcept {
    switch (error) {
    case Error::none:
        return "none";
    case Error::malformed:
        return "malformed";
    case Error::incompatible:
        return "incompatible_build_executable_or_content";
    case Error::authentication:
        return "authentication_failed";
    case Error::capacity:
        return "capacity_exceeded";
    case Error::stale:
        return "stale_epoch_baseline_or_sequence";
    case Error::ownership:
        return "wrong_actor_owner";
    case Error::not_ready:
        return "baseline_not_ready";
    case Error::authority_lost:
        return "authority_disconnected";
    case Error::timeout:
        return "timeout";
    case Error::queue_full:
        return "queue_full";
    case Error::world_mismatch:
        return "canonical_world_mismatch";
    }
    return "unknown";
}

std::string error_detail(const Packet& packet) {
    std::string detail = error_name(packet.error);
    if (packet.error == Error::world_mismatch && packet.world_index < world_file_count) {
        detail += std::string(":") + world_files[packet.world_index].path;
    }
    return detail;
}
} // namespace sporemp::network
