#include "native_network.h"
#include "native_actors.h"
#include "native_scene.h"
#include "native_window.h"
#include "native_scene_ownership.h"
#include "native_scene_cadence.h"
#include "native_replica.h"
#include "native_persistence.h"
#include "native_checkpoint.h"
#include "native_content_network.h"
#include "native_quiescence_probe.h"
#include "native_player_transfer_probe.h"
#include "native_award_context.h"
#include "native_inventory.h"
#include "native_history_context.h"
#include "native_relationship_context.h"
#include "native_social.h"
#include "native_death.h"
#include "native_hatch.h"
#include "native_worker.h"
#include "native_global_preload.h"
#include "diagnostics.h"
#include "../network/peer.h"
#include <Spore/App/IMessageManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sporemp {
namespace {
using namespace network;
static_assert(uint32_t(Verb::move) == uint32_t(ActorVerb::move) && uint32_t(Verb::jump) == uint32_t(ActorVerb::jump) &&
                  uint32_t(Verb::attack) == uint32_t(ActorVerb::attack) &&
                  uint32_t(Verb::stop) == uint32_t(ActorVerb::stop) &&
                  uint32_t(Verb::approach) == uint32_t(ActorVerb::approach) &&
                  uint32_t(Verb::engage) == uint32_t(ActorVerb::engage) &&
                  uint32_t(Verb::pickup) == uint32_t(ActorVerb::pickup) &&
                  uint32_t(Verb::social) == uint32_t(ActorVerb::social) &&
                  uint32_t(Verb::combat) == uint32_t(ActorVerb::combat) &&
                  uint32_t(Verb::steer) == uint32_t(ActorVerb::steer) &&
                  uint32_t(Verb::steer_stop) == uint32_t(ActorVerb::steer_stop),
              "Network/native input verb values must match at the scalar boundary");
std::unique_ptr<Peer> peer;
PeerConfig config;
DWORD engine_thread = 0;
App::IMessageManager* messages = nullptr;
std::filesystem::path output_directory;
bool listening = false, welcomed = false, ready = false, failed = false, load_queued = false, setup_queued = false,
     local_ready = false;
bool owns_persistence = false, baseline_complete = false;
bool connect_started = false;
bool authority_inputs_armed = false;
// Native slots follow original primary/secondary storage. Canonical owners
// follow the durable universe and may be reversed after a location transfer.
std::array<uint32_t, 3> canonical_by_slot{0, 1, 2};
bool authority_has_secondary = false;
uint64_t authority_input_fence_epoch = 0;
bool startup_menu_ready = false;
// Pinned original GGE intro completion/skip message. All four native post
// sites enter GGE UI state 6 and dispose the intro before queued delivery.
constexpr uint32_t startup_menu_ready_message = 0x066f8f25;
uint64_t player = 0, scene = 0, baseline = 0, published_epoch = 0, request_sequence = 0;
uint64_t checkpoint_revision = 0;
uint64_t authenticated_session = 0;
uint64_t scene_worker_generation = 0;
Packet client_tutorial{};
bool client_tutorial_active = false, client_tutorial_complete = false, client_tutorial_ack_due = false;
ULONGLONG next_tutorial_send = 0, next_tutorial_ack = 0;

struct InventoryPublication {
    uint64_t entity = 0, generation = 0, revision = 0;
    InventoryState state;
};

std::array<InventoryPublication, 3> inventories;
InventoryAssembly inventory_assembly;
InventoryState client_inventory;
uint64_t inventory_entity = 0, inventory_generation = 0;
ULONGLONG next_inventory_send = 0;
bool inventory_installed = false;
HistoryFrame history_publication{}, client_history{};
RelationshipFrame relationship_publication{}, client_relationship{};
uint64_t relationship_publication_epoch = 0;
ULONGLONG next_relationship_send = 0;
bool relationship_unpublishable = false;
uint64_t history_publication_epoch = 0;
ULONGLONG next_history_send = 0;
ULONGLONG next_social_send = 0, client_social_deadline = 0, next_social_log = 0;
SocialFrame client_social{};
Packet client_death{};
Packet client_hatch{};
ULONGLONG next_hatch_send = 0;
ULONGLONG next_death_send = 0, next_death_ack = 0;
enum class TransferWork {
    idle,
    startup,
    pausing,
    held,
    remeasuring,
    resuming
};

struct TransferMaintenance {
    TransferWork work = TransferWork::idle;
    Packet command{}, receipt{};
    bool receipt_ready = false, imported = false, finish_after_resume = false;
    uint64_t deadline = 0, pause_token = 0, resume_ai = 0;
    uint32_t selected_player = 0, selected_avatar = 0, selected_herd = 0;
} transfer;

ULONGLONG next_snapshot = 0, next_retry = 0, started = 0, last_summary = 0;
ULONGLONG last_authority_ai_ms = 0;
uint64_t last_authority_ai_entries = 0;
constexpr ULONGLONG authority_ai_timeout_ms = 5000;
size_t baseline_count = 0;
NativeSceneFrame staging;
std::map<uint64_t, Entity> published;

struct Motion {
    Entity previous{}, target{};
    ULONGLONG received = 0;
};

std::map<uint64_t, Motion> motion;
std::map<uint64_t, uint64_t> tombstones;
HWND game_window = nullptr;
WNDPROC original_window_proc = nullptr;
std::array<bool, 5> held{};
std::array<bool, 5> pressed{};
bool keyboard_steering = false;
ULONGLONG next_input = 0;

bool on_thread() {
    return engine_thread && engine_thread == GetCurrentThreadId();
}

void reset_history() {
    client_history = {};
    reset_native_history_presentation();
    // The owner's relationship snapshot follows the same identity and scene lifetime.
    client_relationship = {};
}

// Forgets every movement key this player holds and that the keyboard is steering.
void release_keys() {
    held = {};
    pressed = {};
    keyboard_steering = false;
}

// Clears the authority's social view from this game and forgets the frame it showed.
void clear_social_view() {
    clear_native_social_view();
    client_social_deadline = 0;
    client_social = {};
}

// The latest sample of entity `id` when this player owns it at `generation`; null otherwise.
const Entity* own_replica(uint64_t id, uint64_t generation) {
    const auto found = motion.find(id);
    if (found == motion.end() || found->second.target.owner != player ||
        found->second.target.generation != generation) {
        return nullptr;
    }
    return &found->second.target;
}

uint32_t native_slot(uint32_t canonical) {
    if (canonical < 1 || canonical > 2) {
        return 0;
    }
    return canonical_by_slot[1] == canonical ? 1u : canonical_by_slot[2] == canonical ? 2u : 0u;
}

Entity native_entity(Entity value) {
    value.owner = native_slot(static_cast<uint32_t>(value.owner));
    value.pickup_owner = native_slot(value.pickup_owner);
    return value;
}

bool same_tutorial_identity(const Packet& a, const Packet& b) {
    // Baselines may be refreshed on reconnect. Every native incarnation,
    // canonical owner and location/worker lifetime must still be identical.
    return a.session == b.session && a.scene == b.scene && a.player == b.player && a.location == b.location &&
           a.worker_generation == b.worker_generation && a.native_load_slot == b.native_load_slot &&
           a.tutorial == b.tutorial;
}

bool selected_head_adopted() {
    if (!on_thread() || !local_ready) {
        return false;
    }
    if (native_checkpoint_deadline()) {
        return true;
    }
    if (!config.primary_owner || config.secondary_save == Digest{} ||
        native_persistence_snapshot().state != NativePersistenceState::loaded) {
        return false;
    }
    uint32_t native_id = UINT32_MAX, foreign = UINT32_MAX;
    const auto primary = 1u;
    return native_actor_owner_native_id(primary, native_id) &&
           (config.role == Role::player || !native_actor_owner_native_id(3 - primary, foreign));
}

void event(const char* name, const std::string& fields = "") {
    native_actor_worker_event(name, fields.c_str());
}

std::string quoted(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c >= 32 && c < 127) {
            result += static_cast<char>(c);
        }
    }
    return result + '"';
}

void status(const char* state, const std::string& detail) {
    event("network_status", ",\"state\":" + quoted(state) + ",\"player_id\":" + std::to_string(player) +
                                ",\"baseline_sequence\":" + std::to_string(baseline) + ",\"detail\":" + quoted(detail));
    // Bounded, nonsecret player status. Atomic replace keeps launcher reads whole.
    const auto temporary = output_directory / L"network-status.tmp";
    const auto destination = output_directory / L"network-status.json";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream << "{\"state\":" << quoted(state) << ",\"player_id\":" << player << ",\"baseline_sequence\":" << baseline
           << ",\"detail\":" << quoted(detail) << ",\"checkpoint_revision\":" << checkpoint_revision
           << ",\"checkpoint_recovered\":" << (checkpoint_revision && selected_head_adopted() ? "true" : "false")
           << "}\n";
    stream.close();
    MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void quarantine(const std::string& reason, bool stop_native = true) {
    if (failed) {
        return;
    }
    reset_history();
    clear_native_social_view(stop_native);
    client_social_deadline = 0;
    reset_native_death();
    client_death = {};
    client_hatch = {};
    failed = true;
    ready = false;
    motion.clear();
    release_keys();
    fence_native_content_network();
    fence_native_global_preload();
    if (config.role == Role::authority && authority_inputs_armed) {
        authority_input_fence_epoch = native_actor_fence_network_inputs("network_quarantine", stop_native);
    }
    native_replica_arm_network();
    reset_native_scene_baseline();
    status("error", reason);
    // Drop the coordinator lease without a blocking network-thread join on the game thread.
    if (peer) {
        peer->request_stop();
    }
}

// The last messages this peer sent. A rejection does not say which one it answers (trial233), so the
// rejection row lists them: kind, player and the frequent motion and pose kinds collapsed to a count.
struct SentNote {
    uint32_t kind = 0;
    uint64_t player = 0;
    ULONGLONG tick = 0;
};

std::array<SentNote, 12> recent_sent{};
size_t recent_sent_next = 0;

bool send(Packet packet) {
    if (!peer || failed || !peer->send(packet)) {
        quarantine("Network output queue unavailable.");
        return false;
    }
    if (packet.kind != Kind::motion) {
        recent_sent[recent_sent_next] = {static_cast<uint32_t>(packet.kind), static_cast<uint64_t>(packet.player),
                                         GetTickCount64()};
        recent_sent_next = (recent_sent_next + 1) % recent_sent.size();
    }
    return true;
}

std::string recent_sent_text() {
    std::string text = "[";
    const auto now = GetTickCount64();
    for (size_t i = 0; i < recent_sent.size(); ++i) {
        const auto& note = recent_sent[(recent_sent_next + i) % recent_sent.size()];
        if (!note.tick) {
            continue;
        }
        if (text.size() > 1) {
            text += ',';
        }
        text += "[" + std::to_string(note.kind) + "," + std::to_string(note.player) + "," +
                std::to_string(now - note.tick) + "]";
    }
    return text + "]";
}

bool authority_death(uint32_t slot, Packet& packet, NativeDeathRequest& request) {
    if (!native_network_death_authority() || !pending_native_death(slot, request) || request.epoch != scene) {
        return false;
    }
    const auto owner = canonical_by_slot[slot];
    const auto actor = std::find_if(published.begin(), published.end(), [&](const auto& entry) {
        const auto& value = entry.second;
        return value.owner == owner && value.native_id == request.native_id && value.life_state && value.health == 0;
    });
    if (actor == published.end()) {
        return false;
    }
    packet = {};
    packet.kind = Kind::death;
    packet.role = Role::authority;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.player = owner;
    packet.death = {request.occurrence, actor->second.id, actor->second.generation, request.reason};
    return true;
}

bool same_death_identity(const Packet& a, const Packet& b) {
    return a.session == b.session && a.scene == b.scene && a.player == b.player && a.location == b.location &&
           a.worker_generation == b.worker_generation && a.native_load_slot == b.native_load_slot && a.death == b.death;
}

void receive_death(const Packet& packet) {
    if (!ready || !local_ready || packet.scene != scene || packet.baseline != baseline ||
        packet.session != authenticated_session || !(packet.location == config.location) ||
        packet.native_load_slot != config.native_load_slot || !valid_death(packet.death)) {
        return;
    }
    if (config.role == Role::authority) {
        if (packet.kind != Kind::death_done || packet.role != Role::player ||
            packet.worker_generation != config.worker_generation) {
            return;
        }
        Packet expected;
        NativeDeathRequest request;
        if (authority_death(native_slot(static_cast<uint32_t>(packet.player)), expected, request) &&
            packet.player == expected.player && packet.death == expected.death) {
            const bool applied = complete_native_death(request);
            event("network_death_completion", ",\"player\":" + std::to_string(packet.player) +
                                                  ",\"occurrence\":" + std::to_string(packet.death.occurrence) +
                                                  ",\"entity\":" + std::to_string(packet.death.entity) +
                                                  ",\"native_applied\":" + (applied ? "true" : "false"));
        }
        return;
    }
    if (packet.kind != Kind::death || packet.role != Role::authority || packet.player != player) {
        return;
    }
    const Entity* actor = own_replica(packet.death.entity, packet.death.generation);
    if (!actor || !actor->life_state || actor->health != 0) {
        return;
    }
    if (client_death.death.occurrence) {
        if (same_death_identity(packet, client_death)) {
            client_death.baseline = baseline;
        }
        return;
    }
    if (!present_native_death(packet.death.occurrence, packet.death.reason)) {
        return;
    }
    client_death = packet;
    next_death_ack = 0;
    release_keys();
    event("network_death_client_started", ",\"player\":" + std::to_string(player) +
                                              ",\"entity\":" + std::to_string(packet.death.entity) +
                                              ",\"occurrence\":" + std::to_string(packet.death.occurrence));
}

void update_death() {
    if (!ready || !welcomed || failed || transfer.work != TransferWork::idle) {
        return;
    }
    const auto now = GetTickCount64();
    if (config.role == Role::authority) {
        if (now < next_death_send) {
            return;
        }
        for (uint32_t slot = 1; slot <= 2; ++slot) {
            Packet packet;
            NativeDeathRequest request;
            if (authority_death(slot, packet, request)) {
                send(packet);
            }
        }
        next_death_send = now + 1000;
        return;
    }
    if (!client_death.death.occurrence) {
        return;
    }
    const Entity* actor = own_replica(client_death.death.entity, client_death.death.generation);
    if (client_death.session != authenticated_session || client_death.scene != scene || client_death.player != player ||
        !actor) {
        reset_native_death();
        client_death = {};
        return;
    }
    if (!actor->life_state && actor->health > 0) {
        if (!restore_native_death_view(client_death.death.occurrence)) {
            return;
        }
        event("network_death_client_released", ",\"player\":" + std::to_string(player) +
                                                   ",\"occurrence\":" + std::to_string(client_death.death.occurrence));
        reset_native_death();
        client_death = {};
        return;
    }
    if (client_death.baseline != baseline || now < next_death_ack ||
        !native_death_confirmed(client_death.death.occurrence)) {
        return;
    }
    Packet receipt = client_death;
    receipt.kind = Kind::death_done;
    receipt.role = Role::player;
    send(receipt);
    next_death_ack = now + 1000;
}

bool authority_hatch(Packet& packet, uint32_t& native_id) {
    network::HatchFrame frame;
    uint64_t current_epoch = 0;
    if (!native_network_death_authority() || !capture_native_hatch(native_id, current_epoch, frame) ||
        current_epoch != scene) {
        return false;
    }
    const auto owner = canonical_by_slot[2];
    const auto actor = std::find_if(published.begin(), published.end(), [&](const auto& entry) {
        return entry.second.owner == owner && entry.second.native_id == native_id && !entry.second.life_state &&
               entry.second.health > 0;
    });
    if (actor == published.end()) {
        return false;
    }
    frame.entity = actor->second.id;
    frame.generation = actor->second.generation;
    packet = {};
    packet.kind = Kind::hatch_view;
    packet.role = Role::authority;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.player = owner;
    packet.hatch = frame;
    return valid_hatch(frame);
}

void receive_hatch(const Packet& packet) {
    if (!ready || !local_ready || packet.scene != scene || packet.baseline != baseline ||
        packet.session != authenticated_session || !(packet.location == config.location) ||
        packet.native_load_slot != config.native_load_slot) {
        return;
    }
    if (config.role == Role::authority) {
        if (packet.kind != Kind::hatch_input || packet.role != Role::player ||
            packet.worker_generation != config.worker_generation || !valid_hatch(packet.hatch, true)) {
            return;
        }
        Packet expected;
        uint32_t native_id = UINT32_MAX;
        if (authority_hatch(expected, native_id) && packet.player == expected.player &&
            same_hatch_offer(packet.hatch, expected.hatch)) {
            const bool applied = apply_native_hatch_input(native_id, scene, packet.hatch);
            event("network_hatch_authority_input", ",\"player\":" + std::to_string(packet.player) +
                                                       ",\"serial\":" + std::to_string(packet.hatch.serial) +
                                                       ",\"choice\":" + std::to_string(packet.hatch.choice) +
                                                       ",\"applied\":" + (applied ? "true" : "false"));
        }
        return;
    }
    if (packet.kind != Kind::hatch_view || packet.role != Role::authority || packet.player != player ||
        packet.worker_generation != scene_worker_generation || !valid_hatch(packet.hatch)) {
        return;
    }
    if (!own_replica(packet.hatch.entity, packet.hatch.generation)) {
        return;
    }
    if (client_hatch.scene == scene && client_hatch.hatch.entity == packet.hatch.entity &&
        client_hatch.hatch.serial > packet.hatch.serial) {
        return;
    }
    client_hatch = packet;
}

void update_hatch() {
    if (!ready || !welcomed || failed || transfer.work != TransferWork::idle) {
        return;
    }
    if (config.role == Role::authority) {
        if (GetTickCount64() < next_hatch_send) {
            return;
        }
        Packet packet;
        uint32_t native_id = UINT32_MAX;
        if (authority_hatch(packet, native_id)) {
            send(packet);
        }
        next_hatch_send = GetTickCount64() + 250;
        return;
    }
    if (!client_hatch.hatch.serial || client_death.death.occurrence) {
        return;
    }
    const Entity* actor = own_replica(client_hatch.hatch.entity, client_hatch.hatch.generation);
    if (client_hatch.scene != scene || client_hatch.baseline != baseline || !actor || actor->life_state ||
        actor->health <= 0) {
        client_hatch = {};
        reset_native_hatch();
        return;
    }
    present_native_hatch(client_hatch.hatch);
}

void reset_inventory() {
    inventory_assembly.reset();
    client_inventory = {};
    inventory_installed = false;
    inventory_entity = inventory_generation = 0;
}

void receive_inventory(const Packet& packet) {
    if (config.role != Role::player || !ready || !local_ready || packet.role != Role::authority ||
        packet.scene != scene || packet.baseline != baseline || packet.session != authenticated_session ||
        packet.player != player || !(packet.location == config.location) ||
        packet.native_load_slot != config.native_load_slot || !valid_inventory_frame(packet.inventory)) {
        return;
    }
    const auto& frame = packet.inventory;
    if (!own_replica(frame.entity, frame.generation)) {
        return;
    }
    if (inventory_entity != frame.entity || inventory_generation != frame.generation) {
        reset_inventory();
        inventory_entity = frame.entity;
        inventory_generation = frame.generation;
    }
    InventoryState completed;
    const auto result = inventory_assembly.receive(frame, completed);
    if (result == InventoryReceipt::invalid) {
        quarantine("Invalid part inventory snapshot.");
        return;
    }
    if (result != InventoryReceipt::complete) {
        return;
    }
    if (!present_native_inventory(scene, native_slot(static_cast<uint32_t>(player)), frame, completed,
                                  inventory_installed ? &client_inventory : nullptr)) {
        quarantine("The original part inventory could not be displayed.");
        return;
    }
    client_inventory = std::move(completed);
    inventory_installed = true;
}

void update_inventory() {
    if (config.role != Role::authority || !ready || !welcomed || failed || !native_network_authority_inputs_ready() ||
        transfer.work != TransferWork::idle || GetTickCount64() < next_inventory_send) {
        return;
    }
    next_inventory_send = GetTickCount64() + 1000;
    for (const auto& entry : published) {
        const auto& actor = entry.second;
        if (!actor.owner || actor.owner > 2) {
            continue;
        }
        InventoryState observed;
        if (!capture_native_inventory(native_slot(static_cast<uint32_t>(actor.owner)), scene, observed)) {
            quarantine("The original part inventory could not be read.");
            return;
        }
        auto& saved = inventories[static_cast<size_t>(actor.owner)];
        if (saved.entity != actor.id || saved.generation != actor.generation) {
            saved = {actor.id, actor.generation, 0, {}};
        }
        if (!saved.revision || !(saved.state == observed)) {
            saved.state = std::move(observed);
            ++saved.revision;
        }
        for (uint32_t offset = 0; offset < saved.state.items.size(); offset += inventory_chunk_items) {
            Packet packet;
            packet.kind = Kind::inventory;
            packet.role = Role::authority;
            packet.player = actor.owner;
            packet.scene = scene;
            packet.baseline = baseline;
            auto& frame = packet.inventory;
            frame.entity = actor.id;
            frame.generation = actor.generation;
            frame.revision = saved.revision;
            frame.total = static_cast<uint32_t>(saved.state.items.size());
            frame.offset = offset;
            frame.count = std::min(inventory_chunk_items, frame.total - offset);
            frame.points = saved.state.points;
            frame.group = saved.state.group;
            std::copy_n(saved.state.items.begin() + offset, frame.count, frame.items.begin());
            send(packet);
        }
    }
}

// A history or relationship snapshot is for the second player only, from the authority of the scene and baseline
// this game has applied.
bool second_player_snapshot_admitted(const Packet& packet) {
    if (config.role != Role::player || player != 2 || !ready || !welcomed || !local_ready || failed ||
        baseline_complete || transfer.work != TransferWork::idle || packet.role != Role::authority ||
        packet.player != player || packet.scene != scene || packet.baseline != baseline ||
        packet.session != authenticated_session || packet.worker_generation != scene_worker_generation ||
        !(packet.location == config.location) || packet.native_load_slot != config.native_load_slot) {
        return false;
    }
    return true;
}

// Whether entity `id` is the second player's creature at `generation`.
bool second_player_creature(uint64_t id, uint64_t generation) {
    const auto actor = motion.find(id);
    if (actor == motion.end() || actor->second.target.owner != 2 || actor->second.target.kind != EntityKind::animal ||
        actor->second.target.generation != generation) {
        return false;
    }
    return true;
}

void receive_history(const Packet& packet) {
    if (!second_player_snapshot_admitted(packet) || !valid_history_frame(packet.history)) {
        return;
    }
    const auto& frame = packet.history;
    if (!second_player_creature(frame.entity, frame.generation)) {
        return;
    }
    auto manager = Simulator::cGameNounManager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    NativeSceneLocalIdentity identity;
    if (!avatar ||
        !native_scene_local_identity(scene, avatar->mID, reinterpret_cast<uintptr_t>(avatar), identity, true) ||
        identity.owner != 2 || identity.id != frame.entity || identity.generation != frame.generation) {
        quarantine("The current creature no longer matches its history snapshot.");
        return;
    }
    if (client_history.entity != frame.entity || client_history.generation != frame.generation) {
        reset_history();
    }
    if (frame.revision <= client_history.revision) {
        return;
    }
    if (!present_native_history(scene, 2, frame)) {
        quarantine("The original creature history could not be displayed.");
        return;
    }
    client_history = frame;
    event("network_history_applied", ",\"player\":2,\"entity\":" + std::to_string(frame.entity) +
                                         ",\"generation\":" + std::to_string(frame.generation) +
                                         ",\"revision\":" + std::to_string(frame.revision) + ",\"entries\":" +
                                         std::to_string(frame.state.count) + ",\"reward_executed\":false");
}

void receive_relationship(const Packet& packet) {
    if (!second_player_snapshot_admitted(packet) || !valid_relationship_frame(packet.relationship)) {
        return;
    }
    const auto& frame = packet.relationship;
    if (!second_player_creature(frame.entity, frame.generation)) {
        return;
    }
    if (client_relationship.entity != frame.entity || client_relationship.generation != frame.generation) {
        client_relationship = {};
    }
    if (frame.revision <= client_relationship.revision) {
        return;
    }
    bool changed = false;
    if (!apply_native_relationships(scene, frame, changed)) {
        quarantine("The original species relationships could not be displayed.");
        return;
    }
    client_relationship = frame;
    event("network_relationship_applied",
          ",\"player\":2,\"entity\":" + std::to_string(frame.entity) +
              ",\"generation\":" + std::to_string(frame.generation) +
              ",\"revision\":" + std::to_string(frame.revision) + ",\"entries\":" + std::to_string(frame.count) +
              ",\"changed\":" + (changed ? "true" : "false") + ",\"reward_executed\":false");
}

// History and relationships are published for the second player's creature only, by the dedicated authority of an
// experimental foreign-species session while it serves a running scene.
bool second_player_publication_open() {
    return !(config.role != Role::authority || !config.experimental_foreign_species || config.primary_owner ||
             canonical_by_slot[2] != 2 || !ready || !welcomed || !local_ready || failed ||
             !native_network_authority_inputs_ready() || transfer.work != TransferWork::idle);
}

// The second player's creature among the published entities. `ambiguous` when that player owns more than one
// entity, or one that is not the creature the original numbers `native_id`.
struct SecondPlayerActor {
    const Entity* actor = nullptr;
    bool ambiguous = false;
};

SecondPlayerActor second_player_actor(uint32_t native_id) {
    const Entity* actor = nullptr;
    for (const auto& entry : published) {
        if (entry.second.owner != 2) {
            continue;
        }
        if (actor || entry.second.kind != EntityKind::animal || entry.second.native_id != native_id) {
            return {nullptr, true};
        }
        actor = &entry.second;
    }
    return {actor, false};
}

// Keeps the published relationships of `actor` equal to `observed`, under a new revision when they changed.
// False after a quarantine.
bool revise_relationship_publication(const Entity& actor, const RelationshipFrame& observed, bool& changed) {
    auto& saved = relationship_publication;
    if (relationship_publication_epoch != scene || saved.entity != actor.id || saved.generation != actor.generation) {
        saved = {};
        saved.entity = actor.id;
        saved.generation = actor.generation;
        relationship_publication_epoch = scene;
    }
    changed = !saved.revision || !same_relationships(saved, observed);
    if (changed) {
        if (saved.revision == UINT64_MAX) {
            quarantine("Creature relationship revision exhausted.");
            return false;
        }
        saved.count = observed.count;
        saved.entries = observed.entries;
        ++saved.revision;
    }
    return true;
}

// Periodic complete state reaches a fresh or reconnected owner without replaying
// any relationship action or reward-producing native callback.
void send_relationship_publication(bool changed) {
    const auto& saved = relationship_publication;
    Packet packet;
    packet.kind = Kind::relationship;
    packet.role = Role::authority;
    packet.player = 2;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.relationship = saved;
    if (send(packet) && changed) {
        event("network_relationship_published",
              ",\"player\":2,\"entity\":" + std::to_string(saved.entity) + ",\"generation\":" +
                  std::to_string(saved.generation) + ",\"revision\":" + std::to_string(saved.revision) +
                  ",\"entries\":" + std::to_string(saved.count) + ",\"full_snapshot\":true");
    }
}

void update_relationships() {
    if (!second_player_publication_open() || GetTickCount64() < next_relationship_send ||
        !native_relationship_replication_ready(scene)) {
        return;
    }
    next_relationship_send = GetTickCount64() + 1000;
    uint32_t native_id = UINT32_MAX;
    if (!native_actor_owner_native_id(2, native_id)) {
        return;
    }
    const SecondPlayerActor found = second_player_actor(native_id);
    if (found.ambiguous || !found.actor ||
        validate_native_scene_action_entity(scene, native_entity(*found.actor)) != NativeSceneResult::accepted) {
        return;
    }
    RelationshipFrame observed;
    if (!capture_native_relationships(scene, observed)) {
        // More species than the bounded snapshot holds, a populated second map or an
        // active callback. Say so once; never send a partial snapshot.
        if (!relationship_unpublishable) {
            event("network_relationship_unpublishable",
                  ",\"player\":2,\"capacity\":" + std::to_string(relationship_capacity));
        }
        relationship_unpublishable = true;
        return;
    }
    relationship_unpublishable = false;
    bool changed = false;
    if (revise_relationship_publication(*found.actor, observed, changed)) {
        send_relationship_publication(changed);
    }
}

// Keeps the published history of `actor` equal to `observed`, under a new revision when it changed.
// False after a quarantine.
bool revise_history_publication(const Entity& actor, const HistoryState& observed, bool& changed) {
    auto& saved = history_publication;
    if (history_publication_epoch != scene || saved.entity != actor.id || saved.generation != actor.generation) {
        saved = {};
        saved.entity = actor.id;
        saved.generation = actor.generation;
        history_publication_epoch = scene;
    }
    changed = !saved.revision || !(saved.state == observed);
    if (changed) {
        if (saved.revision == UINT64_MAX) {
            quarantine("Creature history revision exhausted.");
            return false;
        }
        saved.state = observed;
        ++saved.revision;
    }
    return true;
}

// Periodic complete state reaches fresh/reconnected owners without replaying
// a relationship action, history event or reward-producing native callback.
void send_history_publication(bool changed) {
    const auto& saved = history_publication;
    Packet packet;
    packet.kind = Kind::history;
    packet.role = Role::authority;
    packet.player = 2;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.history = saved;
    if (send(packet) && changed) {
        event("network_history_published", ",\"player\":2,\"entity\":" + std::to_string(saved.entity) +
                                               ",\"generation\":" + std::to_string(saved.generation) +
                                               ",\"revision\":" + std::to_string(saved.revision) + ",\"entries\":" +
                                               std::to_string(saved.state.count) + ",\"full_snapshot\":true");
    }
}

void update_history() {
    if (!second_player_publication_open() || GetTickCount64() < next_history_send ||
        !native_history_replication_ready(scene)) {
        return;
    }
    next_history_send = GetTickCount64() + 1000;
    uint32_t native_id = UINT32_MAX;
    if (!native_actor_owner_native_id(2, native_id)) {
        return;
    }
    const SecondPlayerActor found = second_player_actor(native_id);
    if (found.ambiguous) {
        quarantine("The original creature history owner is ambiguous.");
        return;
    }
    if (!found.actor) {
        return;
    }
    if (validate_native_scene_action_entity(scene, native_entity(*found.actor)) != NativeSceneResult::accepted) {
        quarantine("The original creature history identity changed.");
        return;
    }
    HistoryState observed;
    if (!capture_native_history(2, scene, observed) || !valid_history(observed)) {
        quarantine("The original creature history could not be read.");
        return;
    }
    bool changed = false;
    if (revise_history_publication(*found.actor, observed, changed)) {
        send_history_publication(changed);
    }
}

bool authority_tutorial(Packet& packet, NativeTutorialRequest& request, uint32_t slot) {
    if (!ready || !native_network_authority_inputs_ready() || transfer.work != TransferWork::idle ||
        !pending_native_tutorial(request, slot) || request.epoch != scene) {
        return false;
    }
    uint32_t native_id = UINT32_MAX;
    if (!native_actor_owner_native_id(slot, native_id)) {
        return false;
    }
    const auto owner = canonical_by_slot[slot];
    const auto entity = std::find_if(published.begin(), published.end(), [&](const auto& item) {
        return item.second.owner == owner && item.second.native_id == native_id && !item.second.life_state &&
               item.second.health > 0;
    });
    if (entity == published.end()) {
        return false;
    }
    packet = {};
    packet.kind = Kind::tutorial;
    packet.role = Role::authority;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.player = owner;
    packet.tutorial = {entity->second.id, entity->second.generation, request.token, request.caption};
    return true;
}

void receive_tutorial(const Packet& packet) {
    if (!ready || packet.scene != scene || packet.baseline != baseline || packet.session != authenticated_session ||
        !(packet.location == config.location) || packet.native_load_slot != config.native_load_slot ||
        !valid_tutorial(packet.tutorial)) {
        return;
    }
    if (config.role == Role::authority) {
        if (packet.kind != Kind::tutorial_done || packet.role != Role::player ||
            packet.worker_generation != config.worker_generation) {
            return;
        }
        Packet expected;
        NativeTutorialRequest request;
        const uint32_t slot = packet.player == canonical_by_slot[1]   ? 1u
                              : packet.player == canonical_by_slot[2] ? 2u
                                                                      : 0u;
        if (authority_tutorial(expected, request, slot) && packet.player == expected.player &&
            packet.tutorial == expected.tutorial) {
            const bool released = request.owner == 1 || request.token != 0xea7666af ||
                                  finish_native_network_social_owner(request.actor, request.epoch);
            const bool applied = released && complete_native_tutorial(request);
            event("network_tutorial_completion", ",\"player\":" + std::to_string(packet.player) +
                                                     ",\"entity\":" + std::to_string(packet.tutorial.entity) +
                                                     ",\"native_recorded\":" + (applied ? "true" : "false"));
        }
        return;
    }
    if (packet.kind != Kind::tutorial || packet.role != Role::authority || packet.player != player) {
        return;
    }
    const Entity* actor = own_replica(packet.tutorial.entity, packet.tutorial.generation);
    if (!actor || actor->life_state || actor->health <= 0) {
        return;
    }
    if (client_tutorial_active && same_tutorial_identity(packet, client_tutorial)) {
        client_tutorial.baseline = baseline;
        if (client_tutorial_complete) {
            client_tutorial_ack_due = true;
        }
        return;
    }
    if (native_tutorial_presentation() == NativeTutorialPresentation::active) {
        return;
    }
    reset_native_tutorial_presentation();
    if (!present_native_tutorial(packet.tutorial.token, packet.tutorial.caption)) {
        return;
    }
    client_tutorial = packet;
    client_tutorial_active = true;
    client_tutorial_complete = client_tutorial_ack_due = false;
    next_tutorial_ack = 0;
    release_keys();
    event("network_tutorial_client_started", ",\"player\":" + std::to_string(player) + ",\"entity\":" +
                                                 std::to_string(packet.tutorial.entity) + ",\"completed\":false");
}

void update_tutorial() {
    if (!ready || !welcomed || failed || transfer.work != TransferWork::idle) {
        return;
    }
    const auto now = GetTickCount64();
    if (config.role == Role::authority) {
        if (now < next_tutorial_send) {
            return;
        }
        for (uint32_t slot = 1; slot <= 2; ++slot) {
            Packet packet;
            NativeTutorialRequest request;
            if (authority_tutorial(packet, request, slot)) {
                send(packet);
            }
        }
        next_tutorial_send = now + 1000;
        return;
    }
    if (!client_tutorial_active || client_tutorial.scene != scene || client_tutorial.session != authenticated_session ||
        client_tutorial.baseline != baseline || client_tutorial.player != player) {
        return;
    }
    const Entity* actor = own_replica(client_tutorial.tutorial.entity, client_tutorial.tutorial.generation);
    if (!actor || actor->life_state || actor->health <= 0) {
        return;
    }
    if (!client_tutorial_complete && native_tutorial_presentation() == NativeTutorialPresentation::completed) {
        client_tutorial_complete = client_tutorial_ack_due = true;
    }
    if (client_tutorial_ack_due && now >= next_tutorial_ack) {
        Packet receipt = client_tutorial;
        receipt.kind = Kind::tutorial_done;
        receipt.role = Role::player;
        send(receipt);
        next_tutorial_ack = now + 1000;
        client_tutorial_ack_due = false;
    }
}

bool social_entities_match(const SocialFrame& frame, const Entity& actor, const Entity& target) {
    return actor.id == frame.entity && actor.generation == frame.generation && target.id == frame.target &&
           target.generation == frame.target_generation && !target.owner && !actor.life_state && actor.health > 0 &&
           !target.life_state && target.health > 0;
}

void receive_social_view(const Packet& packet) {
    if (config.role != Role::player || !ready || !local_ready || packet.role != Role::authority ||
        packet.scene != scene || packet.baseline != baseline || packet.session != authenticated_session ||
        packet.player != player || !(packet.location == config.location) ||
        packet.native_load_slot != config.native_load_slot || !valid_social_frame(packet.social)) {
        return;
    }
    const auto actor = motion.find(packet.social.entity), target = motion.find(packet.social.target);
    if (actor == motion.end() || target == motion.end() || actor->second.target.owner != player ||
        !social_entities_match(packet.social, actor->second.target, target->second.target)) {
        return;
    }
    const bool applied = present_native_social_view(packet.social);
    const auto now = GetTickCount64();
    if (now >= next_social_log || packet.social.visible != client_social.visible) {
        char fields[512]{};
        sprintf_s(fields,
                  ",\"player\":%llu,\"entity\":%llu,\"target\":%llu,\"visible\":%u,\"mode\":%u,\"relation\":%u,"
                  "\"segments\":[%u,%u],\"applied\":%s,\"values\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]",
                  player, packet.social.entity, packet.social.target, packet.social.visible, packet.social.mode,
                  packet.social.relation, packet.social.segment_count[0], packet.social.segment_count[1],
                  applied ? "true" : "false", double(packet.social.values[0]), double(packet.social.values[1]),
                  double(packet.social.values[2]), double(packet.social.values[3]), double(packet.social.values[4]),
                  double(packet.social.values[5]), double(packet.social.values[6]), double(packet.social.values[7]));
        event("network_social_view", fields);
        next_social_log = now + 1000;
    }
    if (applied) {
        client_social = packet.social;
        client_social_deadline = now + 500;
    }
}

void update_social_view() {
    const auto now = GetTickCount64();
    if (config.role == Role::player) {
        if (!client_social_deadline) {
            return;
        }
        const auto actor = motion.find(client_social.entity), target = motion.find(client_social.target);
        if (!ready || !welcomed || failed || now >= client_social_deadline || actor == motion.end() ||
            target == motion.end() || actor->second.target.owner != player ||
            !social_entities_match(client_social, actor->second.target, target->second.target)) {
            clear_social_view();
            event("network_social_view_cleared");
        }
        return;
    }
    if (!ready || !welcomed || failed || !native_network_authority_inputs_ready() ||
        transfer.work != TransferWork::idle || now < next_social_send) {
        return;
    }
    next_social_send = now + 100;
    uint32_t slot = 0, actor_native = UINT32_MAX, target_native = UINT32_MAX;
    SocialFrame frame;
    if (!capture_native_social_view(scene, slot, actor_native, target_native, frame) || slot < 1 || slot > 2) {
        return;
    }
    const auto owner = canonical_by_slot[slot];
    const auto actor = std::find_if(published.begin(), published.end(), [&](const auto& entry) {
        return entry.second.owner == owner && entry.second.native_id == actor_native;
    });
    const auto target = std::find_if(published.begin(), published.end(), [&](const auto& entry) {
        return !entry.second.owner && entry.second.native_id == target_native;
    });
    if (actor == published.end() || target == published.end()) {
        return;
    }
    frame.entity = actor->second.id;
    frame.generation = actor->second.generation;
    frame.target = target->second.id;
    frame.target_generation = target->second.generation;
    if (!valid_social_frame(frame) || !social_entities_match(frame, actor->second, target->second)) {
        return;
    }
    Packet packet;
    packet.kind = Kind::social_view;
    packet.role = Role::authority;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.player = owner;
    packet.social = frame;
    send(packet);
}

Entity encode_entity(const NativeSceneEntity& entity, uint64_t) {
    auto value = entity;
    if (config.role == Role::authority) {
        value.owner = value.owner < canonical_by_slot.size() ? canonical_by_slot[static_cast<size_t>(value.owner)] : 0;
        value.pickup_owner = value.pickup_owner < canonical_by_slot.size() ? canonical_by_slot[value.pickup_owner] : 0;
    }
    return value;
}

bool transfer_file(const std::filesystem::path& path, Digest& hash, uint32_t& bytes, uint32_t maximum) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    Sha256 digest{};
    const bool valid = GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 && !info.nFileSizeHigh &&
                       info.nFileSizeLow && info.nFileSizeLow <= maximum &&
                       !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                       sha256_file(path.c_str(), digest) && parse_hex(digest.data(), hash);
    if (valid) {
        bytes = info.nFileSizeLow;
    }
    CloseHandle(file);
    return valid;
}

bool transfer_witness_file(const TransferFrame& frame) {
    const auto& command = transfer.command;
    const auto value = transfer_witness_json(frame, command.session, command.scene, command.baseline);
    const auto name = L"transfer-witness-" + std::to_wstring(frame.id) + L"-" +
                      std::to_wstring(static_cast<uint32_t>(frame.op)) + L".json";
    const auto temporary = output_directory / (name + L".tmp"), path = output_directory / name;
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD count = 0;
    const bool saved = WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &count, nullptr) &&
                       count == value.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return saved && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH);
}

bool transfer_receipt(TransferFrame frame) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return false;
    }
    frame.process_id = GetCurrentProcessId();
    frame.process_created = (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    frame.native_player = transfer.selected_player;
    frame.native_avatar = transfer.selected_avatar;
    if (frame.op == TransferOp::destination_discard || frame.op == TransferOp::source_cleanup) {
        frame.native_player = frame.native_avatar = 0;
    }
    const auto pause = native_quiescence_probe_status(transfer.command.scene);
    frame.pause_request = transfer.pause_token;
    frame.input_epoch = pause.input_epoch;
    frame.app_begin = pause.app_begin;
    frame.app_end = pause.app_end;
    frame.ai_begin = pause.ai_begin;
    frame.ai_end = pause.ai_end;
    frame.zero_deltas = pause.zero_native_deltas;
    frame.nonzero_deltas = pause.nonzero_native_deltas;
    frame.elapsed_ms = pause.elapsed_ms;
    frame.pause_before = 0;
    frame.pause_after = static_cast<uint32_t>(pause.gameplay_count);
    if (transfer.work == TransferWork::resuming) {
        frame.pause_before = 1;
        frame.app_end = native_worker_app_updates();
        frame.ai_end = native_actor_worker_status().values[2];
        frame.input_epoch = authority_input_fence_epoch;
    }
    frame.remaining_imports = authority_has_secondary ? 1u : 0u;
    if (!transfer_witness_digest(frame, transfer.command.session, transfer.command.scene, transfer.command.baseline,
                                 frame.state) ||
        !transfer_witness_file(frame)) {
        return false;
    }
    Packet receipt;
    receipt.kind = Kind::transfer_receipt;
    receipt.role = Role::authority;
    receipt.scene = transfer.command.scene;
    receipt.baseline = transfer.command.baseline;
    receipt.transfer = frame;
    transfer.receipt = receipt;
    transfer.receipt_ready = true;
    event("network_native_transfer_receipt",
          ",\"transfer\":" + std::to_string(frame.id) + ",\"operation\":" +
              std::to_string(static_cast<uint32_t>(frame.op)) + ",\"state_sha256\":" + quoted(hex(frame.state)) +
              ",\"artifact_sha256\":" + quoted(hex(frame.artifact)) + ",\"completed_native_work\":true");
    return send(receipt);
}

bool primary_admission(NativePlayerTransferAdmission& admission) {
    auto manager = Simulator::cGameNounManager::Get();
    auto primary = manager ? manager->GetPlayer() : nullptr;
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    if (!primary || !avatar) {
        return false;
    }
    admission.epoch = transfer.command.scene;
    admission.pause_request = transfer.pause_token;
    admission.canonical_player = transfer.command.transfer.player;
    admission.primary_player = primary->mID;
    admission.primary_avatar = avatar->mID;
    admission.primary_unique_game = primary->mUniqueGameID;
    return true;
}

bool release_transfer_pause() {
    if (request_native_quiescence_resume(transfer.pause_token, scene) != worker::Result::accepted) {
        return false;
    }
    // Native AI can advance after release; intentions stay fenced until the
    // coordinator accepts the new scene baseline and owner mapping.
    authority_input_fence_epoch = native_actor_fence_network_inputs("transfer_awaiting_baseline", false);
    return authority_input_fence_epoch != 0;
}

bool exact_transfer_command(const TransferFrame& a, const TransferFrame& b) {
    TransferWire first{}, second{};
    return encode_transfer(a, first) && encode_transfer(b, second) && first == second;
}

// Releases the transfer pause and waits for the original AI to advance again. Quarantines with `failure` when the
// pause cannot be released.
void resume_after_transfer(const char* failure) {
    if (!release_transfer_pause()) {
        quarantine(failure);
        return;
    }
    transfer.resume_ai = native_actor_worker_status().values[2];
    transfer.work = TransferWork::resuming;
}

// Writes to `herd` the herd of the creature the original numbers `avatar_native_id`. Leaves `herd` as it was when
// that creature or its herd is not there.
void read_native_herd(uint32_t avatar_native_id, uint32_t& herd) {
    auto manager = Simulator::cGameNounManager::Get();
    for (auto& noun : manager->mNouns) {
        if (noun.mID == avatar_native_id) {
            auto actor = object_cast<Simulator::cCreatureAnimal>(&noun);
            if (actor && actor->mHerd) {
                herd = actor->mHerd->mID;
            }
        }
    }
}

// Removes the selected secondary player from the original, then restarts the stability observation of the pause.
bool retire_selected_secondary(const TransferFrame& command) {
    return native_actor_retire_transfer_secondary(command.id, scene, transfer.pause_token, command.unique_game_id,
                                                  transfer.selected_player, transfer.selected_avatar,
                                                  transfer.selected_herd) &&
           restart_native_transfer_observation(transfer.pause_token, scene, native_worker_app_updates());
}

// The secondary player is gone: measure what remains, then release the pause.
void remeasure_without_secondary() {
    authority_has_secondary = false;
    transfer.imported = false;
    transfer.work = TransferWork::remeasuring;
    transfer.finish_after_resume = true;
}

// The first command of a recovery worker, which holds its startup pause.
void receive_recovery_command(const Packet& packet) {
    const auto& frame = packet.transfer;
    const bool retired_checkpoint_secondary = frame.op == TransferOp::source_cleanup && frame.source_slot == 2 &&
                                              frame.native_slot == 2 && authority_has_secondary &&
                                              config.resident_owners == 0b110 && baseline == 0;
    if (frame.id != config.maintenance_transfer || frame.native_slot < 1 || frame.native_slot > 2 ||
        canonical_by_slot[frame.native_slot] != frame.player ||
        (frame.op != TransferOp::destination_restore && frame.op != TransferOp::source_restore &&
         !retired_checkpoint_secondary)) {
        quarantine("Recovery command did not match this maintenance registration.");
        return;
    }
    transfer.command = packet;
    transfer.work = TransferWork::pausing;
    transfer.deadline = GetTickCount64() + 120000;
    // The original pause was acquired before this worker connected.
}

// What a command asks of a transfer that is held under its pause.
enum class HeldStep {
    unsupported,
    activate,
    discard,
    cleanup,
    retain_source
};

HeldStep held_step(const TransferFrame& frame, const TransferFrame& previous) {
    if (frame.op == TransferOp::source_activate &&
        (previous.op == TransferOp::source_fence || previous.op == TransferOp::source_restore)) {
        return HeldStep::activate;
    }
    if (frame.op == TransferOp::destination_activate && previous.op == TransferOp::destination_restore) {
        return HeldStep::activate;
    }
    if (frame.op == TransferOp::destination_discard && previous.op == TransferOp::destination_restore) {
        return HeldStep::discard;
    }
    if (frame.op == TransferOp::source_cleanup && previous.op == TransferOp::source_fence &&
        previous.source_slot == 2) {
        return HeldStep::cleanup;
    }
    if (frame.op == TransferOp::source_restore && previous.op == TransferOp::source_fence) {
        return HeldStep::retain_source;
    }
    return HeldStep::unsupported;
}

// The source has remained under the same continuous measured pause.
// Revalidate that existing native state instead of reading over it.
void revalidate_retained_source(const TransferFrame& frame) {
    uint32_t player_id = 0, avatar_id = 0;
    if (!native_quiescence_probe_ready(scene) ||
        !native_actor_transfer_selection(scene, frame.source_slot, frame.unique_game_id, player_id, avatar_id) ||
        player_id != transfer.selected_player || avatar_id != transfer.selected_avatar || !transfer_receipt(frame)) {
        quarantine("The retained original source state could not be revalidated for rollback.");
        return;
    }
    event("network_native_transfer_source_retained",
          ",\"transfer\":" + std::to_string(frame.id) +
              ",\"same_continuous_pause\":true,\"native_state_rewritten\":false");
}

// A destination discards the player it imported, or a source cleans up the secondary that left.
void retire_held_secondary(const TransferFrame& frame, bool discard) {
    if ((discard && !transfer.imported) || !authority_has_secondary || !retire_selected_secondary(frame)) {
        quarantine("The provisional native player could not be discarded completely.");
        return;
    }
    remeasure_without_secondary();
}

// A command for a transfer this authority already works on: the same command again, or the next step of a transfer
// held under its pause.
void receive_followup_command(const Packet& packet) {
    const auto& frame = packet.transfer;
    if (exact_transfer_command(frame, transfer.command.transfer)) {
        if (transfer.receipt_ready) {
            send(transfer.receipt);
        }
        return;
    }
    const auto previous = transfer.command.transfer;
    if (frame.id != previous.id || frame.preparation != previous.preparation || frame.player != previous.player ||
        frame.unique_game_id != previous.unique_game_id || frame.canonical_home != previous.canonical_home ||
        !transfer.receipt_ready || transfer.work != TransferWork::held) {
        quarantine("Transfer phase did not match the held native state.");
        return;
    }
    const HeldStep step = held_step(frame, previous);
    if (step == HeldStep::unsupported || frame.artifact != transfer.receipt.transfer.artifact ||
        frame.artifact_bytes != transfer.receipt.transfer.artifact_bytes ||
        (transfer.receipt.transfer.approval != Digest{} && frame.approval != transfer.receipt.transfer.approval)) {
        quarantine("Unsupported transition from the held native transfer.");
        return;
    }
    transfer.command = packet;
    transfer.receipt_ready = false;
    transfer.deadline = GetTickCount64() + 120000;
    if (step == HeldStep::retain_source) {
        revalidate_retained_source(frame);
    } else if (step == HeldStep::discard || step == HeldStep::cleanup) {
        retire_held_secondary(frame, step == HeldStep::discard);
    } else {
        resume_after_transfer("The transfer pause could not be released exactly once.");
    }
}

// The first command of a transfer, on an authority that serves its scene: stop serving and ask for the pause.
void begin_transfer(const Packet& packet) {
    const auto& frame = packet.transfer;
    if (!ready || (frame.op != TransferOp::source_fence && frame.op != TransferOp::destination_restore &&
                   frame.op != TransferOp::source_restore)) {
        quarantine("This authority is not ready for the requested native transfer phase.");
        return;
    }
    if (frame.op == TransferOp::source_fence &&
        (native_slot(static_cast<uint32_t>(frame.player)) != frame.source_slot ||
         frame.native_slot != frame.source_slot)) {
        quarantine("Transfer source does not own the selected native slot.");
        return;
    }
    if (frame.op != TransferOp::source_fence &&
        (frame.native_slot == 1 ? canonical_by_slot[1] != frame.player
                                : canonical_by_slot[2] != frame.player || authority_has_secondary)) {
        quarantine("Transfer destination slot does not match the canonical player.");
        return;
    }
    transfer = {};
    transfer.command = packet;
    transfer.work = TransferWork::pausing;
    transfer.pause_token = frame.id;
    transfer.deadline = GetTickCount64() + 120000;
    ready = false;
    reset_history();
    fence_native_content_network();
    if (request_native_transfer_quiescence(transfer.pause_token, scene, native_worker_app_updates()) !=
        worker::Result::accepted) {
        quarantine("The original transfer pause could not be acquired.");
    }
}

void receive_transfer(const Packet& packet) {
    const auto& frame = packet.transfer;
    if (config.role != Role::authority || !welcomed || !local_ready || !peer->transport_active() ||
        packet.session != authenticated_session || packet.scene != scene || packet.baseline != baseline ||
        frame.worker_generation != config.worker_generation || !frame.id || !frame.worker || frame.player < 1 ||
        frame.player > 2) {
        quarantine("Transfer command scope did not match this native authority.");
        return;
    }
    if (transfer.receipt_ready && exact_transfer_command(frame, transfer.command.transfer)) {
        send(transfer.receipt);
        return;
    }
    if (transfer.work == TransferWork::startup) {
        receive_recovery_command(packet);
    } else if (transfer.work != TransferWork::idle) {
        receive_followup_command(packet);
    } else {
        begin_transfer(packet);
    }
}

// The original AI advanced after the pause was released: seal the activation receipt and serve the scene again.
void finish_transfer_resume(const TransferFrame& command) {
    if (native_actor_worker_status().values[2] <= transfer.resume_ai) {
        return;
    }
    if (!transfer_receipt(command)) {
        quarantine("Native activation receipt could not be sealed.");
        return;
    }
    transfer.work = TransferWork::idle;
    published_epoch = 0;
    published.clear();
    config.maintenance_transfer = 0;
    config.maintenance_scene = 0;
    last_authority_ai_entries = native_actor_worker_status().values[2];
    last_authority_ai_ms = GetTickCount64();
}

// The paused state was measured again after a player was restored or a secondary retired.
void finish_transfer_remeasure(const TransferFrame& command) {
    if (transfer.finish_after_resume) {
        resume_after_transfer("Native discard pause release failed.");
        return;
    }
    if (!transfer_receipt(command)) {
        quarantine("Native restored-state receipt could not be sealed.");
        return;
    }
    transfer.work = TransferWork::held;
}

// A source under its pause: export the departing player's original state and seal it in a receipt.
void export_transfer_source(const TransferFrame& command) {
    if (!native_actor_transfer_selection(scene, command.source_slot, command.unique_game_id, transfer.selected_player,
                                         transfer.selected_avatar) ||
        transfer.selected_avatar != command.source_avatar ||
        (command.source_player && transfer.selected_player != command.source_player)) {
        quarantine("The selected transfer source native identity changed.");
        return;
    }
    NativePlayerGraphSelection selection{scene,
                                         command.player,
                                         transfer.pause_token,
                                         command.source_slot,
                                         command.unique_game_id,
                                         command.canonical_home};
    if (command.source_slot == 2) {
        read_native_herd(transfer.selected_avatar, transfer.selected_herd);
        if (!transfer.selected_herd) {
            quarantine("The departing player's native herd is unavailable.");
            return;
        }
    }
    if (!export_native_selected_player_state(command.id, selection)) {
        quarantine("Original player state export failed.");
        return;
    }
    auto frame = command;
    frame.source_player = transfer.selected_player;
    const auto path = output_directory / (L"player-state-" + std::to_wstring(command.id) + L".db");
    if (!transfer_file(path, frame.artifact, frame.artifact_bytes, 1024 * 1024) ||
        !native_quiescence_probe_ready(scene) || !transfer_receipt(frame)) {
        quarantine("The exported native state could not be sealed.");
        return;
    }
    transfer.work = TransferWork::held;
}

// A destination under its pause: the player state and its approval must be the files the durable command names,
// and the approval must be for the selected player. False after a quarantine.
bool load_transfer_approval(const TransferFrame& command, NativePlayerTransferFixture& approved,
                            NativePlayerTransferAdmission& admission) {
    Digest artifact{}, approval{};
    uint32_t artifact_bytes = 0, approval_bytes = 0;
    if (!transfer_file(output_directory / L"player-import.db", artifact, artifact_bytes, 1024 * 1024) ||
        !transfer_file(output_directory / L"player-import.expected", approval, approval_bytes, 20680) ||
        artifact != command.artifact || approval != command.approval || artifact_bytes != command.artifact_bytes ||
        approval_bytes != command.approval_bytes) {
        quarantine("Destination transfer files do not match the durable command.");
        return false;
    }
    // Each word holds four little-endian digest bytes (native_player_transfer_probe.h).
    std::array<uint64_t, 10> words{};
    for (size_t i = 0; i < approval.size(); ++i) {
        words[i / 4] |= uint64_t(approval[i]) << ((i % 4) * 8);
    }
    if (!load_native_player_transfer_fixture(words, approved) || !primary_admission(admission) ||
        approved.schema != 3 || approved.source_unique_game != command.unique_game_id ||
        approved.source_home != command.canonical_home || approved.source_player != command.source_player ||
        approved.source_avatar != command.source_avatar) {
        quarantine("Native transfer approval does not match the selected player.");
        return false;
    }
    return true;
}

// A selected paired Save can contain a player who has since left.
// The coordinator authorizes only retirement in this fresh baseline-0
// maintenance session. Identify the actual loaded secondary rather
// than reusing noun IDs from the now-dead exporting process. The
// immutable approved artifact pins its canonical home/identity;
// its older saved progression must never be written over the traveler.
void retire_checkpoint_secondary(const TransferFrame& command) {
    if (!config.maintenance_transfer || baseline || !authority_has_secondary || command.source_slot != 2 ||
        command.native_slot != 2 ||
        !native_actor_transfer_selection(scene, 2, command.unique_game_id, transfer.selected_player,
                                         transfer.selected_avatar)) {
        quarantine("The obsolete saved traveler does not match cleanup admission.");
        return;
    }
    read_native_herd(transfer.selected_avatar, transfer.selected_herd);
    if (!transfer.selected_herd || !retire_selected_secondary(command)) {
        quarantine("The obsolete checkpoint secondary could not be retired completely.");
        return;
    }
    event("network_native_checkpoint_secondary_retired",
          ",\"transfer\":" + std::to_string(command.id) + ",\"canonical_player\":" + std::to_string(command.player) +
              ",\"canonical_home\":" + std::to_string(command.canonical_home) +
              ",\"baseline_before_cleanup\":0,\"player_state_rewritten\":false");
    remeasure_without_secondary();
}

// Restores the approved state over the secondary player the loaded checkpoint already holds. False after a
// quarantine.
bool restore_existing_secondary(const TransferFrame& command, const NativePlayerTransferAdmission& admission,
                                const NativePlayerTransferFixture& approved, NativePlayerTransferProbeResult& result) {
    uint32_t player_id = 0, avatar_id = 0, herd_id = 0;
    if (!native_actor_transfer_selection(scene, 2, command.unique_game_id, player_id, avatar_id)) {
        quarantine("The selected checkpoint player does not match the recovery artifact.");
        return false;
    }
    read_native_herd(avatar_id, herd_id);
    if (!herd_id) {
        quarantine("The restored checkpoint player's native herd is unavailable.");
        return false;
    }
    result = restore_native_secondary_transfer(command.id, admission, approved, player_id, avatar_id, herd_id,
                                               command.canonical_home);
    return true;
}

// Restores or imports the approved player into the slot the command names, then measures the paused state again.
// A restoration that changed only part of the original ends this process.
void restore_transfer_player(const TransferFrame& command, const NativePlayerTransferAdmission& admission,
                             const NativePlayerTransferFixture& approved) {
    const bool existing_secondary = command.native_slot == 2 && authority_has_secondary;
    NativePlayerTransferProbeResult result;
    if (existing_secondary) {
        if (!restore_existing_secondary(command, admission, approved, result)) {
            return;
        }
    } else {
        result = command.native_slot == 1 ? restore_native_primary_transfer(command.id, admission, approved)
                                          : import_native_player_transfer(command.id, admission, approved);
    }
    if (!result.imported || !result.has_stage) {
        quarantine("Original native player restoration failed.");
        return;
    }
    if (command.native_slot == 2) {
        NativeAwardCheckpoint stage;
        stage.words = result.stage_words;
        if (result.restored_primary || result.restored_secondary != existing_secondary ||
            (!existing_secondary && (!native_actor_adopt_transfer_import(
                                         scene, command.id, admission.primary_player, admission.primary_avatar,
                                         admission.primary_unique_game, result.player, result.avatar, result.herd) ||
                                     !restore_native_actor_awards(stage, scene)))) {
            event("network_native_transfer_partial_failure");
            TerminateProcess(GetCurrentProcess(), 0x4d13);
            return;
        }
        authority_has_secondary = true;
        transfer.imported = true;
    } else if (!result.restored_primary) {
        TerminateProcess(GetCurrentProcess(), 0x4d14);
        return;
    }
    transfer.selected_player = result.player;
    transfer.selected_avatar = result.avatar;
    transfer.selected_herd = result.herd;
    if (!restart_native_transfer_observation(transfer.pause_token, scene, native_worker_app_updates())) {
        quarantine("Restored native state could not start its own stability observation.");
        return;
    }
    transfer.work = TransferWork::remeasuring;
}

void update_transfer() {
    if (transfer.work == TransferWork::idle || failed) {
        return;
    }
    if (GetTickCount64() > transfer.deadline) {
        quarantine("Native transfer maintenance timed out.");
        return;
    }
    if (transfer.work == TransferWork::startup || transfer.work == TransferWork::held) {
        return;
    }
    const auto& command = transfer.command.transfer;
    if (transfer.work == TransferWork::resuming) {
        finish_transfer_resume(command);
        return;
    }
    if (!native_quiescence_probe_ready(scene)) {
        return;
    }
    if (transfer.work == TransferWork::remeasuring) {
        finish_transfer_remeasure(command);
        return;
    }
    if (command.op == TransferOp::source_fence) {
        export_transfer_source(command);
        return;
    }
    NativePlayerTransferFixture approved;
    NativePlayerTransferAdmission admission;
    if (!load_transfer_approval(command, approved, admission)) {
        return;
    }
    if (command.op == TransferOp::source_cleanup) {
        retire_checkpoint_secondary(command);
        return;
    }
    restore_transfer_player(command, admission, approved);
}

NativeSceneEntity decode_entity(const Entity& entity) {
    return entity;
}

bool intention(Verb verb, int direction = 0, uint64_t target = 0, uint64_t target_generation = 0,
               uint64_t actor_id = 0) {
    if (!ready || failed || config.role != Role::player || player < 1 || player > 2) {
        return false;
    }
    if (native_tutorial_presentation() == NativeTutorialPresentation::active) {
        return false;
    }
    auto controlled = actor_id ? motion.find(actor_id)
                               : std::find_if(motion.begin(), motion.end(),
                                              [](const auto& item) { return item.second.target.owner == player; });
    if (controlled == motion.end()) {
        return false;
    }
    Packet packet;
    packet.kind = Kind::action;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.player = player;
    packet.entity = controlled->second.target;
    packet.verb = verb;
    packet.direction = direction;
    packet.request = ++request_sequence;
    packet.target = target;
    packet.target_generation = target_generation;
    if (!valid_action(packet)) {
        return false;
    }
    event("network_intention",
          ",\"request\":" + std::to_string(packet.request) + ",\"player\":" + std::to_string(player) + ",\"entity\":" +
              std::to_string(packet.entity.id) + ",\"verb\":" + std::to_string(static_cast<unsigned>(verb)) +
              ",\"target\":" + std::to_string(target) + ",\"target_generation\":" + std::to_string(target_generation));
    return send(packet);
}

int movement_key(WPARAM key) {
    if (key == 'W') {
        return 0;
    }
    if (key == 'D') {
        return 1;
    }
    if (key == 'S') {
        return 2;
    }
    if (key == 'A') {
        return 3;
    }
    if (key == VK_SPACE) {
        return 4;
    }
    return -1;
}

LRESULT CALLBACK input_window(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    const int key = movement_key(wparam);
    if (on_thread() && config.role == Role::player && local_ready && key >= 0 &&
        (message == WM_KEYUP ||
         (message == WM_KEYDOWN && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)))) {
        const size_t index = static_cast<size_t>(key);
        // One trace row per transition, not per auto-repeat.
        if ((message == WM_KEYDOWN) != held[index]) {
            event("network_key", ",\"key\":" + std::to_string(unsigned(wparam)) +
                                     ",\"down\":" + (message == WM_KEYDOWN ? "true" : "false"));
        }
        if (message == WM_KEYDOWN) {
            if (!held[index]) {
                pressed[index] = true;
            }
            held[index] = true;
        } else {
            held[index] = false;
        }
        return 0;
    }
    if (message == WM_KILLFOCUS) {
        held = {};
        pressed = {};
    }
    return CallWindowProcW(original_window_proc, window, message, wparam, lparam);
}

BOOL CALLBACK find_window(HWND window, LPARAM) {
    DWORD process = 0;
    const auto thread = GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && thread == engine_thread && IsWindowVisible(window) &&
        GetWindow(window, GW_OWNER) == nullptr) {
        game_window = window;
        return FALSE;
    }
    return TRUE;
}

void install_input() {
    if (config.role != Role::player || original_window_proc) {
        return;
    }
    EnumWindows(find_window, 0);
    if (!game_window) {
        return;
    }
    SetLastError(0);
    const auto previous = SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(input_window));
    if (!previous && GetLastError()) {
        game_window = nullptr;
        quarantine("Game input window could not be attached.");
        return;
    }
    original_window_proc = reinterpret_cast<WNDPROC>(previous);
    event("network_input_attached", ",\"keys\":\"WASD Space\"");
}

// Asks the original to load the save slot of the shared scene.
void queue_scene_load(const worker::Message& state) {
    worker::Message request;
    request.op = worker::Op::load;
    request.epoch = state.epoch;
    request.sequence = 1;
    request.values[0] = config.native_load_slot;
    if (native_persistence_command(request, state) == worker::Result::accepted) {
        load_queued = true;
        status("connecting", "Loading the shared Creature scene.");
    }
}

// A selected original Save can already contain the transferred
// player. Admit that existing pair only when its checkpoint was
// restored and the coordinator intends both canonical residents.
// This config is admission intent; the authenticated Session still
// validates every baseline owner before accepting publication.
// False after a quarantine.
bool adopt_checkpoint_pair(const worker::Message& state, uint32_t wanted) {
    if (config.role != Role::authority || config.resident_owners != 0b110 || !state.values[wanted + 2] ||
        !native_checkpoint_deadline()) {
        quarantine("The native checkpoint contains an unleased or unverified second player.");
        return false;
    }
    if (!authority_has_secondary) {
        authority_has_secondary = true;
        event("network_native_checkpoint_pair_adopted", ",\"slot1_owner\":" + std::to_string(canonical_by_slot[1]) +
                                                            ",\"slot2_owner\":" + std::to_string(canonical_by_slot[2]) +
                                                            ",\"new_native_actors\":0");
    }
    return true;
}

// The loaded scene is one player's own location: its original avatar is bound as that player's creature.
void bootstrap_primary_owner(const worker::Message& state) {
    if (!native_persistence_matches_home_location(config.location)) {
        quarantine("The loaded Creature planet does not match this shared location.");
        return;
    }
    const auto wanted = 1u;
    const auto other = 3 - wanted;
    if (state.values[other + 2] && !adopt_checkpoint_pair(state, wanted)) {
        return;
    }
    if (!state.values[wanted + 2]) {
        worker::Message request;
        request.op = worker::Op::bind_primary;
        request.epoch = state.epoch;
        request.sequence = 2;
        request.values[0] = wanted;
        if (native_actor_worker_command(request) == worker::Result::accepted) {
            status("connecting", "Binding the original Creature avatar to this location.");
        }
        return;
    }
    if (config.role == Role::authority && !native_actor_network_prepare_rewards()) {
        return;
    }
    if (config.role == Role::authority && !native_actor_network_prepare_home()) {
        quarantine("The original secondary home nest could not be prepared.");
        return;
    }
    local_ready = true;
    install_input();
}

// No primary owner: the worker sets up the creatures of both players in the loaded scene.
void bootstrap_both_actors(const worker::Message& state) {
    if (!setup_queued && (!state.values[3] || !state.values[4])) {
        const auto request = worker::make_setup_command(state.epoch, 2, config.experimental_foreign_species);
        if (native_actor_worker_command(request) == worker::Result::accepted) {
            setup_queued = true;
        }
        return;
    }
    if (!state.values[3] || !state.values[4]) {
        return;
    }
    if (config.experimental_foreign_species && !native_actor_experimental_foreign_ready(state.epoch)) {
        quarantine("The experimental species session requires a fresh original one-player scene.");
        return;
    }
    if (config.role == Role::authority && !native_actor_network_prepare_rewards()) {
        if (config.experimental_foreign_species) {
            quarantine("The original controller for the selected species could not be prepared.");
        }
        return;
    }
    if (config.role == Role::authority) {
        authority_has_secondary = true;
    }
    // Initial foreign-species control qualification does not publish a new
    // B home; foreign nest/death/progression/restore remain separate gates.
    if (config.role == Role::authority && !config.experimental_foreign_species &&
        !native_actor_network_prepare_home()) {
        quarantine("The original secondary home nest could not be prepared.");
        return;
    }
    local_ready = true;
    install_input();
}

void native_bootstrap() {
    auto state = native_actor_worker_status();
    if (owns_persistence) {
        update_native_persistence(state);
    }
    if (!load_queued && startup_menu_ready && native_global_preload_ready() &&
        state.values[0] == static_cast<uint64_t>(worker::Phase::menu)) {
        queue_scene_load(state);
    }
    if (native_persistence_snapshot().state == NativePersistenceState::failed) {
        quarantine("The shared Creature fixture could not be loaded.");
        return;
    }
    if (native_persistence_snapshot().state != NativePersistenceState::loaded) {
        return;
    }
    if (config.primary_owner) {
        bootstrap_primary_owner(state);
    } else {
        bootstrap_both_actors(state);
    }
}

// Starts a new baseline for the scene the frame was captured in. False when the session failed.
bool begin_scene_baseline(const NativeSceneFrame& frame) {
    scene = frame.epoch;
    baseline++;
    published.clear();
    published_epoch = frame.epoch;
    Packet packet;
    packet.kind = Kind::scene_begin;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.count = static_cast<uint32_t>(frame.count);
    return send(packet);
}

// Removes every published entity that is gone from `current` or is there in a later generation. False when the
// session failed.
bool publish_despawns(const std::map<uint64_t, Entity>& current, ULONGLONG now) {
    for (const auto& old : published) {
        if (!current.count(old.first) || current.at(old.first).generation != old.second.generation) {
            Packet packet;
            packet.kind = Kind::despawn;
            packet.scene = scene;
            packet.baseline = baseline;
            packet.entity = old.second;
            packet.entity.tick = now;
            if (!send(packet)) {
                return false;
            }
            event("network_despawn_published", ",\"entity\":" + std::to_string(old.first));
        }
    }
    return true;
}

// The coordinator refuses a movement update whose fixed identity differs from the entity it knows
// (trial238: the session ended at a kill). Say which field moved before it is sent, for the first sixteen.
void report_identity_change(const Entity& was, const Entity& is) {
    static unsigned recorded = 0;
    if ((was.kind != is.kind || was.model_instance != is.model_instance || was.model_type != is.model_type ||
         was.model_group != is.model_group || was.native_id != is.native_id ||
         was.herd_native_id != is.herd_native_id || was.species_instance != is.species_instance ||
         was.species_type != is.species_type || was.species_group != is.species_group ||
         was.archetype != is.archetype) &&
        recorded++ < 16) {
        char fields[640]{};
        sprintf_s(fields,
                  ",\"entity\":%llu,\"generation\":%llu,\"owner\":%llu,\"life_state\":[%u,%u],\"kind\":[%u,%u],"
                  "\"model\":[[%u,%u,%u],[%u,%u,%u]],\"native_id\":[%u,%u],\"herd_native_id\":[%u,%u],"
                  "\"species\":[[%u,%u,%u],[%u,%u,%u]],\"archetype\":[%u,%u]",
                  is.id, is.generation, is.owner, was.life_state, is.life_state, unsigned(was.kind), unsigned(is.kind),
                  was.model_instance, was.model_type, was.model_group, is.model_instance, is.model_type, is.model_group,
                  was.native_id, is.native_id, was.herd_native_id, is.herd_native_id, was.species_instance,
                  was.species_type, was.species_group, is.species_instance, is.species_type, is.species_group,
                  was.archetype, is.archetype);
        event("network_entity_identity_changed", fields);
    }
}

void report_authority_pose(const Entity& entity) {
    char fields[1024]{};
    sprintf_s(fields,
              ",\"entity\":%llu,\"generation\":%llu,\"owner\":%llu,\"tick\":%llu,\"position\":[%.9g,%.9g,%.9g],"
              "\"orientation\":[%.9g,%.9g,%.9g,%.9g],\"velocity\":[%.9g,%.9g,%.9g]",
              entity.id, entity.generation, entity.owner, entity.tick, double(entity.x), double(entity.y),
              double(entity.z), double(entity.qx), double(entity.qy), double(entity.qz), double(entity.qw),
              double(entity.vx), double(entity.vy), double(entity.vz));
    event("network_authority_pose", fields);
}

// Sends every entity of `current`: in full when the baseline begins or the entity is new or in a new generation,
// otherwise as motion, and not at all when nothing of a static entity changed. False when the session failed.
bool publish_entities(const std::map<uint64_t, Entity>& current, bool begin) {
    for (const auto& entry : current) {
        const Entity& entity = entry.second;
        const auto previous = published.find(entry.first);
        if (!begin && previous != published.end() && unchanged_static_entity(previous->second, entity)) {
            continue;
        }
        Packet packet;
        packet.kind = begin || previous == published.end() || previous->second.generation != entity.generation
                          ? Kind::entity
                          : Kind::motion;
        packet.scene = scene;
        packet.baseline = baseline;
        packet.entity = entity;
        if (packet.kind == Kind::motion) {
            report_identity_change(previous->second, entity);
        }
        if (!send(packet)) {
            return false;
        }
        if (entity.owner) {
            report_authority_pose(entity);
        }
    }
    return true;
}

// Closes the baseline and opens the authority to the players' intentions. False when the session failed.
bool end_scene_baseline(size_t count) {
    Packet packet;
    packet.kind = Kind::scene_end;
    packet.scene = scene;
    packet.baseline = baseline;
    packet.count = static_cast<uint32_t>(count);
    if (!send(packet)) {
        return false;
    }
    if (authority_input_fence_epoch && !native_actor_resume_network_inputs(authority_input_fence_epoch)) {
        quarantine("The previous authority input lease could not be resumed.");
        return false;
    }
    authority_input_fence_epoch = 0;
    authority_inputs_armed = true;
    ready = true;
    status("connected", "Dedicated Creature scene is running.");
    return true;
}

void publish_scene() {
    const auto now = GetTickCount64();
    if (!welcomed || !local_ready || now < next_snapshot) {
        return;
    }
    NativeSceneFrame frame;
    const auto result = capture_native_scene(frame, authority_has_secondary ? 0u : config.primary_owner ? 1u : 0u);
    if (result != NativeSceneResult::accepted) {
        quarantine(std::string("Native scene capture failed: ") + native_scene_result_name(result));
        return;
    }
    const bool begin = published_epoch != frame.epoch;
    if (begin && !begin_scene_baseline(frame)) {
        return;
    }
    std::map<uint64_t, Entity> current;
    for (size_t i = 0; i < frame.count; ++i) {
        current.emplace(frame.entities[i].id, encode_entity(frame.entities[i], now));
    }
    if (!publish_despawns(current, now) || !publish_entities(current, begin)) {
        return;
    }
    published = std::move(current);
    if (begin && !end_scene_baseline(frame.count)) {
        return;
    }
    next_snapshot = next_native_scene_capture(GetTickCount64());
}

bool send_baseline_ack(uint32_t count) {
    Packet ack;
    ack.kind = Kind::baseline_ack;
    ack.scene = scene;
    ack.baseline = baseline;
    ack.count = count;
    return send(ack);
}

bool baseline_has_other_owner() {
    for (size_t i = 0; i < staging.count; ++i) {
        if (staging.entities[i].owner && staging.entities[i].owner != player) {
            return true;
        }
    }
    return false;
}

// Names the local creatures that stand for the players of the staged baseline. False when the baseline cannot be
// applied in this update: the session was quarantined, or a local creature is not there yet.
bool map_baseline_owners(NativeSceneMapping& mapping) {
    NativeOwnedSpecies owned_species;
    const bool foreign =
        select_native_owned_species(staging.entities.data(), staging.count, owned_species) && owned_species.distinct;
    if (foreign) {
        if (config.primary_owner) {
            quarantine("Distinct species transfer is not qualified.");
            return false;
        }
        mapping.experimental_foreign_owned = true;
        return true;
    }
    if (!config.primary_owner) {
        return native_actor_owner_native_id(1, mapping.avatar_native_id) &&
               native_actor_owner_native_id(2, mapping.other_native_id);
    }
    if (player != config.primary_owner) {
        quarantine("The player is leased to a different location.");
        return false;
    }
    if (!native_actor_owner_native_id(1, mapping.avatar_native_id)) {
        return false;
    }
    if (baseline_has_other_owner() && !native_actor_owner_native_id(2, mapping.other_native_id)) {
        if (!native_replica_bootstrap_open()) {
            quarantine("This scene now has another player. Rejoin to load its complete native baseline.");
            return false;
        }
        if (!setup_queued) {
            const auto state = native_actor_worker_status();
            const auto request = worker::make_setup_command(state.epoch, 3);
            if (native_actor_worker_command(request) == worker::Result::accepted) {
                setup_queued = true;
            }
        }
        return false;
    }
    return true;
}

// A fossil or fruit of the baseline that this client could not bind is not tracked from here on, like a fossil
// created after the baseline that this client cannot mirror.
void forget_unmirrored_baseline_entities() {
    for (size_t i = 0; i < staging.count; ++i) {
        const auto& fossil = staging.entities[i];
        if ((fossil.kind != network::EntityKind::fossil && fossil.kind != network::EntityKind::fruit) ||
            native_scene_entity_bound(fossil.id)) {
            continue;
        }
        char fields[384]{};
        sprintf_s(
            fields,
            ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
            "\"position\":[%.9g,%.9g,%.9g],\"in_baseline\":true,\"visible_to_owner\":false,\"collectable_here\":false",
            fossil.id, fossil.generation, fossil.native_id, double(fossil.x), double(fossil.y), double(fossil.z));
        motion.erase(fossil.id);
        event("network_scene_fossil_unmirrored", fields);
    }
}

void apply_baseline() {
    if (!baseline_complete || !local_ready || failed) {
        return;
    }
    NativeSceneMapping mapping;
    if (!map_baseline_owners(mapping)) {
        return;
    }
    mapping.controlled_owner = static_cast<uint32_t>(player);
    native_replica_arm_network();
    const auto result = apply_native_scene_baseline(staging, mapping);
    baseline_complete = false;
    if (result != NativeSceneResult::accepted) {
        quarantine(std::string("Shared scene could not be applied: ") + native_scene_result_name(result));
        return;
    }
    forget_unmirrored_baseline_entities();
    if (send_baseline_ack(static_cast<uint32_t>(staging.count))) {
        ready = true;
        status("connected", "Joined the shared Creature scene. Use WASD to move and Space to jump.");
    }
}

// Why the authority refuses an action before it looks at the action's target; Error::none when it does not.
// `actor` is the published entity the action names, or null.
Error action_refusal(const Packet& packet, uint64_t native_epoch, const Entity* actor) {
    if (!native_network_authority_inputs_ready() || packet.worker_generation != config.worker_generation ||
        !(packet.location == config.location)) {
        return Error::stale;
    }
    if (config.experimental_foreign_species &&
        (!foreign_creature_input(packet.player, static_cast<ActorVerb>(packet.verb), packet.direction) ||
         native_slot(static_cast<uint32_t>(packet.player)) != packet.player)) {
        return Error::not_ready;
    }
    if (!ready || packet.scene != scene || !actor || actor->generation != packet.entity.generation ||
        actor->owner != packet.player || packet.player < 1 || packet.player > 2) {
        return Error::ownership;
    }
    if (!valid_action(packet) || packet.baseline != baseline || native_epoch != scene) {
        return Error::stale;
    }
    if (actor->life_state || actor->health <= 0) {
        return Error::not_ready;
    }
    if (validate_native_scene_action_entity(scene, native_entity(*actor)) != NativeSceneResult::accepted) {
        return Error::stale;
    }
    return Error::none;
}

// The original's number for the target of an action, UINT32_MAX for a verb without one. Refuses a target that is
// gone, owned by a player, not eligible for the verb or no longer the noun that was published.
Error resolve_action_target(const Packet& packet, uint32_t& target_native_id) {
    target_native_id = UINT32_MAX;
    if (!targeted(packet.verb)) {
        return Error::none;
    }
    const auto target = published.find(packet.target);
    if (target == published.end() || target->second.generation != packet.target_generation) {
        return Error::stale;
    }
    if (target->second.owner) {
        return Error::ownership;
    }
    if (!eligible_target(packet.verb, target->second)) {
        return Error::not_ready;
    }
    if (validate_native_scene_action_entity(scene, native_entity(target->second)) != NativeSceneResult::accepted) {
        return Error::stale;
    }
    target_native_id = target->second.native_id;
    return Error::none;
}

// Hands an admitted action to the original as the actor's intention.
Error queue_native_action(const Packet& packet, uint64_t native_epoch, const Entity& actor, uint32_t target_native_id) {
    const auto result = native_actor_network_command(
        native_epoch, native_slot(static_cast<uint32_t>(packet.player)), actor.native_id, target_native_id,
        static_cast<ActorVerb>(packet.verb), packet.direction, packet.request,
        packet.verb == Verb::collect || packet.verb == Verb::forage ? packet.target : 0,
        packet.verb == Verb::collect ? packet.target_generation : 0);
    event("network_native_intention",
          ",\"request\":" + std::to_string(packet.request) + ",\"player\":" + std::to_string(packet.player) +
              ",\"entity\":" + std::to_string(packet.entity.id) + ",\"target\":" + std::to_string(packet.target) +
              ",\"target_generation\":" + std::to_string(packet.target_generation) +
              ",\"queued\":" + (result == worker::Result::accepted ? "true" : "false"));
    return result == worker::Result::accepted ? Error::none : Error::not_ready;
}

void authority_action(const Packet& packet) {
    Packet reply;
    reply.kind = Kind::action_result;
    reply.scene = scene;
    reply.baseline = baseline;
    reply.player = packet.player;
    reply.request = packet.request;
    reply.entity = packet.entity;
    const auto state = native_actor_worker_status();
    const auto found = published.find(packet.entity.id);
    const Entity* actor = found == published.end() ? nullptr : &found->second;
    reply.error = action_refusal(packet, state.epoch, actor);
    if (reply.error == Error::none) {
        uint32_t target_native_id = UINT32_MAX;
        reply.error = resolve_action_target(packet, target_native_id);
        if (reply.error == Error::none) {
            reply.error = queue_native_action(packet, state.epoch, *actor, target_native_id);
        }
    }
    send(reply);
}

void receive_welcome(const Packet& packet) {
    if (packet.request && !selected_head_adopted()) {
        quarantine("The server requires the selected original save. Restore its checkpoint before rejoining.");
        return;
    }
    if (config.maintenance_transfer &&
        (packet.maintenance_transfer != config.maintenance_transfer || packet.scene != scene)) {
        quarantine("The coordinator did not admit the exact recovery registration.");
        return;
    }
    authenticated_session = packet.session;
    checkpoint_revision = packet.request;
    welcomed = true;
    player = packet.player;
    status("connecting", checkpoint_revision ? "Save restored. Waiting for the shared scene."
                                             : "Authenticated. Waiting for the shared scene.");
}

// A player whose authority was lost keeps its connection and waits for a fresh scene. Any other refusal ends the
// session.
void receive_reject(const Packet& packet) {
    event("network_rejected",
          ",\"request\":" + std::to_string(packet.request) + ",\"player\":" + std::to_string(packet.player) +
              ",\"entity\":" + std::to_string(packet.entity.id) + ",\"error\":" + quoted(error_detail(packet)) +
              ",\"refused_kind\":" + std::to_string(packet.target) + ",\"refused_player\":" +
              std::to_string(packet.target_generation) + ",\"recent_sent_kind_player_ms_ago\":" + recent_sent_text());
    if (config.role == Role::player && packet.error == Error::authority_lost) {
        reset_history();
        clear_social_view();
        fence_native_content_network();
        ready = false;
        baseline_complete = false;
        motion.clear();
        release_keys();
        reset_native_scene_baseline();
        native_replica_arm_network();
        status("disconnected", "The dedicated worker disconnected. Waiting for a fresh shared scene.");
        return;
    }
    quarantine(std::string("Server rejected the session: ") + error_detail(packet));
}

void receive_scene_begin(const Packet& packet) {
    reset_history();
    reset_inventory();
    clear_social_view();
    fence_native_content_network();
    if (packet.baseline <= baseline || !packet.scene || packet.count > NativeSceneFrame::capacity) {
        quarantine("Invalid or oversized scene baseline.");
        return;
    }
    scene = packet.scene;
    baseline = packet.baseline;
    scene_worker_generation = packet.worker_generation;
    baseline_count = packet.count;
    staging = {};
    staging.epoch = scene;
    motion.clear();
    tombstones.clear();
    ready = false;
    baseline_complete = false;
}

void report_unmirrored_spawn(const Entity& fossil) {
    char fields[384]{};
    sprintf_s(fields,
              ",\"remote_entity\":%llu,\"entity_generation\":%llu,\"source_native_id\":%u,"
              "\"model\":[%u,%u,%u],\"position\":[%.9g,%.9g,%.9g],\"visible_to_owner\":false,\"collectable_"
              "here\":false",
              fossil.id, fossil.generation, fossil.native_id, fossil.model_instance, fossil.model_type,
              fossil.model_group, double(fossil.x), double(fossil.y), double(fossil.z));
    event("network_scene_fossil_unmirrored", fields);
}

void report_spawn_failure(const Packet& packet, NativeSceneResult result) {
    const auto& entity = packet.entity;
    char fields[1024]{};
    sprintf_s(fields,
              ",\"result\":\"%s\",\"result_code\":%u,\"scene\":%llu,\"baseline\":%llu,"
              "\"remote_entity\":%llu,\"entity_generation\":%llu,\"owner\":%llu,\"source_native_id\":%u,"
              "\"source_tick\":%llu,"
              "\"source_species\":[%u,%u,%u],\"source_archetype\":%u,\"source_herd\":%u,"
              "\"source_age\":%u,\"source_health\":%.9g,\"source_life_state\":%u,\"source_alpha\":%u,"
              "\"source_combatant_state\":%u,\"source_scale\":%.9g",
              native_scene_result_name(result), static_cast<unsigned>(result), packet.scene, packet.baseline, entity.id,
              entity.generation, entity.owner, entity.native_id, entity.tick, entity.species_instance,
              entity.species_type, entity.species_group, entity.archetype, entity.herd_native_id, entity.age,
              double(entity.health), entity.life_state, entity.alpha, entity.combatant_state, double(entity.scale));
    event("network_scene_spawn_failed", fields);
}

// Creates the original's counterpart of an entity that appeared after the baseline. False when the entity is not
// tracked: a fossil or fruit this client cannot mirror, or a failed spawn that ended the session.
bool spawn_entity(const Packet& packet) {
    const auto result = spawn_native_scene_entity(decode_entity(packet.entity));
    if (native_scene_fossil_unmirrored(packet.entity.kind, result == NativeSceneResult::missing_template)) {
        // Not tracked: later motion and removal for this identity are ignored like any unknown one.
        report_unmirrored_spawn(packet.entity);
        return false;
    }
    if (result != NativeSceneResult::accepted) {
        report_spawn_failure(packet, result);
        quarantine("Native entity spawn failed.");
        return false;
    }
    return true;
}

// An entity of the baseline being staged, or one that appeared in the running scene.
void receive_entity(const Packet& packet) {
    if (!valid_entity(packet.entity) || motion.count(packet.entity.id) ||
        (tombstones.count(packet.entity.id) && packet.entity.generation <= tombstones.at(packet.entity.id))) {
        quarantine("Invalid entity lifecycle.");
        return;
    }
    if (!ready) {
        if (staging.count >= baseline_count || staging.count >= NativeSceneFrame::capacity) {
            quarantine("Scene baseline entity overflow.");
            return;
        }
        staging.entities[staging.count++] = decode_entity(packet.entity);
    } else if (!spawn_entity(packet)) {
        return;
    }
    motion.emplace(packet.entity.id, Motion{packet.entity, packet.entity, GetTickCount64()});
}

void receive_scene_end(const Packet& packet) {
    if (staging.count != baseline_count || packet.count != baseline_count) {
        quarantine("Incomplete scene baseline.");
        return;
    }
    if (!baseline_count) {
        reset_native_scene_baseline();
        native_replica_arm_network();
        release_keys();
        if (send_baseline_ack(0)) {
            status("connecting", "Waiting for the dedicated worker to load the shared scene.");
        }
        return;
    }
    baseline_complete = true;
    apply_baseline();
}

void receive_motion(const Packet& packet) {
    auto found = motion.find(packet.entity.id);
    if (found == motion.end() || found->second.target.generation != packet.entity.generation ||
        packet.entity.tick <= found->second.target.tick || found->second.target.owner != packet.entity.owner) {
        return;
    }
    found->second = {found->second.target, packet.entity, GetTickCount64()};
}

void receive_despawn(const Packet& packet) {
    auto found = motion.find(packet.entity.id);
    if (found == motion.end() || found->second.target.generation != packet.entity.generation) {
        return;
    }
    if (client_history.entity == packet.entity.id && client_history.generation == packet.entity.generation) {
        reset_history();
    }
    if (ready &&
        despawn_native_scene_entity(packet.entity.id, packet.entity.generation) != NativeSceneResult::accepted) {
        quarantine("Native entity removal failed.");
        return;
    }
    tombstones[packet.entity.id] = packet.entity.generation;
    motion.erase(found);
}

void receive_action_result(const Packet& packet) {
    event("network_action_result", ",\"request\":" + std::to_string(packet.request) +
                                       ",\"error\":" + quoted(error_detail(packet)) + ",\"accepted_is_queued\":true");
}

// A player's packets of the shared scene. All but the start of a baseline must name the current scene and
// baseline.
void receive_scene_packet(const Packet& packet) {
    if (packet.kind == Kind::scene_begin) {
        receive_scene_begin(packet);
        return;
    }
    if (packet.scene != scene || packet.baseline != baseline) {
        return;
    }
    switch (packet.kind) {
    case Kind::entity:
        receive_entity(packet);
        break;
    case Kind::scene_end:
        receive_scene_end(packet);
        break;
    case Kind::motion:
        receive_motion(packet);
        break;
    case Kind::despawn:
        receive_despawn(packet);
        break;
    case Kind::action_result:
        receive_action_result(packet);
        break;
    default:
        break;
    }
}

void receive_packet(const Packet& packet) {
    switch (packet.kind) {
    case Kind::history:
        receive_history(packet);
        return;
    case Kind::relationship:
        receive_relationship(packet);
        return;
    case Kind::hatch_view:
    case Kind::hatch_input:
        receive_hatch(packet);
        return;
    case Kind::inventory:
        receive_inventory(packet);
        return;
    case Kind::death:
    case Kind::death_done:
        receive_death(packet);
        return;
    case Kind::social_view:
        receive_social_view(packet);
        return;
    case Kind::tutorial:
    case Kind::tutorial_done:
        receive_tutorial(packet);
        return;
    case Kind::transfer_command:
        receive_transfer(packet);
        return;
    case Kind::content_response:
        receive_native_content_network(packet);
        return;
    case Kind::welcome:
        receive_welcome(packet);
        return;
    case Kind::reject:
        receive_reject(packet);
        return;
    default:
        break;
    }
    if (config.role == Role::authority) {
        if (packet.kind == Kind::action) {
            authority_action(packet);
        }
        return;
    }
    receive_scene_packet(packet);
}

// Where a replica is between its last two samples: the place blended linearly, the turn blended from the nearer
// sign of the previous quaternion and normalised.
NativeSceneEntity blend_sample(const Motion& sample, ULONGLONG now) {
    const float alpha = native_scene_sample_blend(sample.target.kind, now - sample.received);
    auto entity = decode_entity(sample.target);
    entity.x = sample.previous.x + (sample.target.x - sample.previous.x) * alpha;
    entity.y = sample.previous.y + (sample.target.y - sample.previous.y) * alpha;
    entity.z = sample.previous.z + (sample.target.z - sample.previous.z) * alpha;
    float previous_turn[4] = {sample.previous.qx, sample.previous.qy, sample.previous.qz, sample.previous.qw};
    float turn[4] = {entity.qx, entity.qy, entity.qz, entity.qw};
    float dot = 0;
    for (size_t i = 0; i < 4; ++i) {
        dot += previous_turn[i] * turn[i];
    }
    float length = 0;
    for (size_t i = 0; i < 4; ++i) {
        turn[i] = (dot < 0 ? -previous_turn[i] : previous_turn[i]) * (1 - alpha) + turn[i] * alpha;
        length += turn[i] * turn[i];
    }
    if (length > 0) {
        for (auto& component : turn) {
            component /= std::sqrt(length);
        }
    }
    entity.qx = turn[0];
    entity.qy = turn[1];
    entity.qz = turn[2];
    entity.qw = turn[3];
    return entity;
}

// Moves every replica to where it is now. False after a quarantine.
bool project_replicas(ULONGLONG now) {
    for (const auto& item : motion) {
        const auto entity = blend_sample(item.second, now);
        const auto result = project_native_scene_entity(entity);
        if (result != NativeSceneResult::accepted) {
            event("network_projection_failed", ",\"entity\":" + std::to_string(entity.id) +
                                                   ",\"generation\":" + std::to_string(entity.generation) +
                                                   ",\"native_id\":" + std::to_string(entity.native_id) +
                                                   ",\"kind\":" + std::to_string(uint32_t(entity.kind)) +
                                                   ",\"result\":" + quoted(native_scene_result_name(result)));
            quarantine("Native motion projection failed.");
            return false;
        }
    }
    return true;
}

// Sends what the movement keys ask of this player's creature: a direction from the camera while a key is down, a
// stop once they are released, and a jump. Keys by index as `movement_key` numbers them: W, D, S, A, Space.
void send_keyboard_intentions() {
    const int forward = int(held[0] || pressed[0]) - int(held[2] || pressed[2]);
    const int horizontal = int(held[1] || pressed[1]) - int(held[3] || pressed[3]);
    if (forward || horizontal) {
        int32_t direction = 0;
        if (native_scene_camera_input(scene, horizontal, forward, direction)) {
            if (intention(Verb::steer, direction)) {
                keyboard_steering = true;
            }
        } else {
            event("network_camera_input_rejected");
        }
    } else if (keyboard_steering && intention(Verb::steer_stop)) {
        keyboard_steering = false;
        event("network_keyboard_release");
    }
    if (pressed[4]) {
        intention(Verb::jump);
    }
    pressed = {};
}

void project_motion() {
    if (!ready || failed) {
        return;
    }
    const auto now = GetTickCount64();
    if (!project_replicas(now)) {
        return;
    }
    if (now >= next_input) {
        next_input = now + 67;
        send_keyboard_intentions();
    }
}

// A recovery worker holds the original scene before it connects.
// Config selects only a reversible startup pause. No import,
// receipt or baseline is admitted before authenticated welcome.
// False after a quarantine.
bool hold_scene_for_recovery() {
    scene = native_actor_worker_status().epoch;
    baseline = 0;
    config.maintenance_scene = scene;
    transfer = {};
    transfer.work = TransferWork::startup;
    transfer.pause_token = config.maintenance_transfer;
    transfer.command.scene = scene;
    transfer.deadline = GetTickCount64() + 120000;
    if (request_native_transfer_quiescence(transfer.pause_token, scene, native_worker_app_updates()) !=
        worker::Result::accepted) {
        quarantine("Recovery startup could not hold the original scene.");
        return false;
    }
    return true;
}

// Starts the network thread once the local scene is ready. False after a quarantine.
bool start_connection() {
    if (config.maintenance_transfer && transfer.work == TransferWork::idle && !hold_scene_for_recovery()) {
        return false;
    }
    std::string error;
    if (!peer->start(config, error)) {
        quarantine(error);
        return false;
    }
    connect_started = true;
    return true;
}

// The connection closed. An authority in transfer maintenance cannot go on; anyone else forgets the shared scene
// and retries.
void connection_lost(const std::string& detail) {
    reset_history();
    reset_inventory();
    clear_social_view();
    if (config.role == Role::authority && transfer.work != TransferWork::idle) {
        quarantine("The transfer authority disconnected. Recover its durable phase with a new worker generation.");
        return;
    }
    fence_native_content_network();
    ready = welcomed = false;
    motion.clear();
    reset_native_scene_baseline();
    native_replica_arm_network();
    release_keys();
    if (config.role == Role::authority) {
        if (authority_inputs_armed) {
            authority_input_fence_epoch = native_actor_fence_network_inputs("network_disconnected");
        }
        published_epoch = 0;
        published.clear();
    }
    status("disconnected", detail);
    next_retry = GetTickCount64() + 1000;
}

// Handles what the network thread queued. One legal full scene plus a following full motion frame can be drained
// per update without starving the bounded transport queue at 20 Hz. False when the session has failed.
bool drain_peer_events() {
    Event incoming;
    size_t handled = 0;
    while (handled++ < 2 * NativeSceneFrame::capacity + 2 && peer->poll(incoming)) {
        if (incoming.kind == EventKind::packet) {
            receive_packet(incoming.packet);
        } else if (incoming.kind == EventKind::disconnected) {
            connection_lost(incoming.detail);
        }
        if (failed) {
            break;
        }
    }
    return !failed;
}

// An authority serves only a running original scene whose simulation advances. False after a quarantine.
bool authority_simulation_advancing() {
    const auto state = native_actor_worker_status();
    const auto now = GetTickCount64();
    if (state.values[0] != static_cast<uint64_t>(worker::Phase::scene)) {
        quarantine("The original Creature scene is no longer active.");
        return false;
    }
    if (!last_authority_ai_ms || state.values[2] != last_authority_ai_entries) {
        last_authority_ai_entries = state.values[2];
        last_authority_ai_ms = now;
    } else if (now - last_authority_ai_ms >= authority_ai_timeout_ms) {
        quarantine("The original Creature simulation stopped advancing.");
        return false;
    }
    return true;
}

// Restarts the network thread under the same player identity. False when it is still exiting, to try again
// shortly, or after a quarantine.
bool retry_connection() {
    std::string error;
    if (!peer->try_restart(config, error)) {
        if (error == "network_thread_still_exiting") {
            next_retry = GetTickCount64() + 100;
            return false;
        }
        quarantine(error);
        return false;
    }
    next_retry = 0;
    status("connecting", "Reconnecting to the same player identity.");
    return true;
}

// One app update of the session: bring the local scene up, connect, handle what arrived, then publish or apply
// the scene and each of its views.
void update_session() {
    std::string preload_error;
    if (!update_native_global_preload(preload_error)) {
        quarantine(preload_error);
        return;
    }
    if (!local_ready) {
        native_bootstrap();
    }
    if (local_ready && !connect_started && !failed && !start_connection()) {
        return;
    }
    if (!drain_peer_events()) {
        return;
    }
    if (config.role == Role::authority && local_ready && welcomed && transfer.work == TransferWork::idle &&
        !authority_simulation_advancing()) {
        return;
    }
    if (transfer.work != TransferWork::idle) {
        update_transfer();
    } else {
        update_native_content_network();
    }
    if (!welcomed && next_retry && GetTickCount64() >= next_retry && !retry_connection()) {
        return;
    }
    if (config.role == Role::authority) {
        if (transfer.work == TransferWork::idle) {
            publish_scene();
        }
    } else {
        apply_baseline();
        project_motion();
    }
    update_inventory();
    update_history();
    update_relationships();
    update_tutorial();
    update_death();
    update_hatch();
    update_social_view();
    const auto now = GetTickCount64();
    if (now - last_summary >= 1000) {
        last_summary = now;
        event("network_scene_sample",
              ",\"ready\":" + std::string(ready ? "true" : "false") + ",\"player\":" + std::to_string(player) +
                  ",\"scene\":" + std::to_string(scene) + ",\"baseline\":" + std::to_string(baseline) +
                  ",\"entities\":" + std::to_string(config.role == Role::authority ? published.size() : motion.size()));
    }
    if (!local_ready && now - started > 120000) {
        quarantine("Shared Creature scene was not ready within two minutes.");
    }
}

class Listener final : public App::IUnmanagedMessageListener {
    bool HandleMessage(uint32_t id, void*) override {
        if (id == startup_menu_ready_message && on_thread() && peer && !failed) {
            startup_menu_ready = true;
            event("network_startup_menu_ready");
            return false;
        }
        if (id != App::kMsgAppUpdate || !on_thread() || !peer || failed) {
            return false;
        }
        update_session();
        return false;
    }
} listener;
} // namespace

void initialize_native_network(const wchar_t* directory) {
    wchar_t path[32768]{};
    const auto length = GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", path, _countof(path));
    if (!length) {
        return;
    }
    engine_thread = GetCurrentThreadId();
    output_directory = directory;
    started = GetTickCount64();
    reset_history();
    history_publication = {};
    history_publication_epoch = 0;
    next_history_send = 0;
    relationship_publication = {};
    relationship_publication_epoch = 0;
    next_relationship_send = 0;
    relationship_unpublishable = false;
    last_authority_ai_ms = 0;
    last_authority_ai_entries = 0;
    std::string error;
    if (length >= _countof(path) || !read_peer_config(path, config, error) || !initialize_native_scene()) {
        quarantine("Network/native scene initialization failed: " + error);
        return;
    }
    if (config.role == Role::authority && config.primary_owner) {
        canonical_by_slot = {0, config.primary_owner, 3 - config.primary_owner};
        event("network_native_owner_mapping", ",\"slot1_owner\":" + std::to_string(canonical_by_slot[1]) +
                                                  ",\"slot2_owner\":" + std::to_string(canonical_by_slot[2]));
    }
    wchar_t supervisor[32]{};
    owns_persistence = !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", supervisor, _countof(supervisor));
    if (owns_persistence) {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (!initialize_native_persistence(base, base + pe->OptionalHeader.SizeOfImage, engine_thread,
                                           native_actor_worker_event)) {
            quarantine("Native fixture loader is unavailable.");
            return;
        }
    }
    peer = std::make_unique<Peer>();
    if (!initialize_native_global_preload(config, error)) {
        quarantine(error);
        return;
    }
    initialize_native_content_network(config, output_directory, send);
    messages = App::IMessageManager::Get();
    if (!messages) {
        quarantine("Native app updates are unavailable.");
        return;
    }
    messages->AddUnmanagedListener(&listener, startup_menu_ready_message);
    messages->AddUnmanagedListener(&listener, App::kMsgAppUpdate);
    listening = true;
    status("connecting", "Connecting to the shared universe.");
}

void native_network_scene_exit() {
    if (!on_thread() || !peer || !local_ready) {
        return;
    }
    // Mode-exit is a lifetime boundary, so revoke bridge inputs without issuing
    // new movement/target calls into a scene already being disposed.
    quarantine("The native scene was closed. Rejoin from the launcher.", false);
}

NativeNetworkProjection native_network_projection() {
    NativeNetworkProjection result;
    if (!on_thread() || !peer) {
        return result;
    }
    if (config.role == Role::authority) {
        if (const auto deadline = native_checkpoint_deadline()) {
            result.state = worker::ProjectionState::checkpoint_lease;
            result.baseline = deadline;
        }
        return result;
    }
    result.state = worker::ProjectionState::waiting;
    // ready is cleared on a replacement baseline, disconnect or quarantine,
    // and is set only after native application and the baseline acknowledgement.
    // This snapshot must not depend on successful publication of a status file.
    if (listening && connect_started && welcomed && ready && local_ready && !failed && !baseline_complete && scene &&
        baseline && baseline_count && player >= 1 && player <= 2) {
        result.state = worker::ProjectionState::connected;
        result.baseline = baseline;
    }
    return result;
}

bool native_network_foreign_selection(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                      uintptr_t target_address) {
    if (!on_thread() || !peer || !peer->transport_active() || !listening || !connect_started || !ready || !welcomed ||
        !local_ready || failed || baseline_complete || config.role != Role::player || player != 2 || !scene ||
        !baseline || !baseline_count || transfer.work != TransferWork::idle || !native_replica_is_client() ||
        native_replica_bootstrap_open()) {
        return false;
    }
    auto manager = Simulator::cGameNounManager::Get();
    if (!manager || !actor_address || reinterpret_cast<uintptr_t>(manager->GetAvatar()) != actor_address) {
        return false;
    }
    NativeSceneLocalIdentity actor{};
    if (!native_scene_local_identity(scene, actor_native_id, actor_address, actor) || actor.owner != player) {
        return false;
    }
    const auto owned = motion.find(actor.id);
    if (owned == motion.end() || owned->second.target.owner != player ||
        owned->second.target.generation != actor.generation || owned->second.target.life_state ||
        owned->second.target.health <= 0) {
        return false;
    }
    const Entity* primary = nullptr;
    for (const auto& item : motion) {
        if (item.second.target.owner == 1) {
            if (primary) {
                return false;
            }
            primary = &item.second.target;
        }
    }
    const auto same_species = [](const Entity& a, const Entity& b) {
        return a.species_instance == b.species_instance && a.species_type == b.species_type &&
               a.species_group == b.species_group;
    };
    if (!primary || primary->life_state || primary->health <= 0 || same_species(*primary, owned->second.target)) {
        return false;
    }
    // Experimental configuration belongs only to authority. Clients prove the
    // distinct owned pair from their committed, authenticated native baseline.
    if (!target_address) {
        return target_native_id == UINT32_MAX;
    }
    NativeSceneLocalIdentity target{};
    if (!native_scene_local_identity(scene, target_native_id, target_address, target) || target.owner ||
        target.id == actor.id) {
        return false;
    }
    const auto npc = motion.find(target.id);
    return npc != motion.end() && npc->second.target.generation == target.generation &&
           eligible_target(Verb::combat, npc->second.target) && !same_species(npc->second.target, *primary) &&
           !same_species(npc->second.target, owned->second.target);
}

namespace {
// Resolves a local noun to this player's own living creature of the shared scene.
bool own_living_actor(uint32_t actor_native_id, uintptr_t actor_address, NativeSceneLocalIdentity& actor) {
    if (!native_scene_local_identity(scene, actor_native_id, actor_address, actor) || actor.owner != player) {
        return false;
    }
    const auto owner = motion.find(actor.id);
    if (owner == motion.end() || owner->second.target.generation != actor.generation ||
        owner->second.target.owner != player || owner->second.target.life_state || owner->second.target.health <= 0) {
        return false;
    }
    return true;
}

bool native_network_target_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, int ability, uint32_t caller_rva, Verb verb) {
    const bool pickup = verb == Verb::pickup, collect = verb == Verb::collect;
    if (!on_thread() || !ready || !local_ready || failed || config.role != Role::player ||
        caller_rva != ((pickup || collect) ? 0x9356abu : 0x948adbu) || ability < 0 || ability >= 88 ||
        ((pickup || collect) && ability)) {
        return false;
    }
    NativeSceneLocalIdentity actor{}, target{};
    if (!native_scene_local_identity(scene, actor_native_id, actor_address, actor) ||
        !(collect ? native_scene_local_fossil_identity(scene, target_native_id, target_address, target)
                  : native_scene_local_identity(scene, target_native_id, target_address, target, pickup)) ||
        actor.owner != player || target.owner || actor.id == target.id) {
        return false;
    }
    const auto owner = motion.find(actor.id), npc = motion.find(target.id);
    if (owner == motion.end() || npc == motion.end() || owner->second.target.generation != actor.generation ||
        npc->second.target.generation != target.generation || owner->second.target.owner != player ||
        npc->second.target.owner || owner->second.target.life_state || owner->second.target.health <= 0) {
        return false;
    }
    if (!eligible_target(verb, npc->second.target)) {
        return false;
    }
    const bool sent = intention(verb, ability, target.id, target.generation, actor.id);
    event(collect                ? "network_fossil_ui"
          : pickup               ? "network_pickup_ui"
          : verb == Verb::combat ? "network_combat_ui"
                                 : "network_social_ui",
          ",\"player\":" + std::to_string(player) + ",\"entity\":" + std::to_string(actor.id) + ",\"target\":" +
              std::to_string(target.id) + ",\"target_generation\":" + std::to_string(target.generation) +
              ",\"ability\":" + std::to_string(ability) + ",\"caller_rva\":" + std::to_string(caller_rva) +
              ",\"sent\":" + (sent ? "true" : "false"));
    return sent;
}
} // namespace

bool native_network_social_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, int ability, uint32_t caller_rva) {
    return native_network_target_input(actor_native_id, actor_address, target_native_id, target_address, ability,
                                       caller_rva, Verb::social);
}

bool native_network_hatch_active() {
    if (!on_thread() || !peer || !peer->transport_active() || !ready || !welcomed || !local_ready || failed ||
        config.role != Role::player || transfer.work != TransferWork::idle ||
        client_hatch.session != authenticated_session || client_hatch.player != player ||
        client_hatch.kind != Kind::hatch_view || client_hatch.role != Role::authority || client_hatch.scene != scene ||
        client_hatch.baseline != baseline || !(client_hatch.location == config.location) ||
        client_hatch.native_load_slot != config.native_load_slot ||
        client_hatch.worker_generation != scene_worker_generation || !valid_hatch(client_hatch.hatch) ||
        !client_hatch.hatch.active || client_death.death.occurrence) {
        return false;
    }
    const Entity* actor = own_replica(client_hatch.hatch.entity, client_hatch.hatch.generation);
    return actor && !actor->life_state && actor->health > 0 && actor->age == 0;
}

bool native_network_hatch_offer(uint32_t actor_native_id, uintptr_t actor_address, network::HatchFrame& frame) {
    if (!native_network_hatch_active()) {
        return false;
    }
    NativeSceneLocalIdentity actor{};
    if (!native_scene_local_identity(scene, actor_native_id, actor_address, actor) || actor.owner != player ||
        actor.id != client_hatch.hatch.entity || actor.generation != client_hatch.hatch.generation) {
        return false;
    }
    frame = client_hatch.hatch;
    return true;
}

bool native_network_hatch_input(uint32_t actor_native_id, uintptr_t actor_address, int ability, uint32_t caller_rva) {
    if (ability < 0 || ability >= 88 || caller_rva != 0x948adb) {
        return false;
    }
    network::HatchFrame frame;
    if (!native_network_hatch_offer(actor_native_id, actor_address, frame)) {
        return false;
    }
    Packet input = client_hatch;
    input.kind = Kind::hatch_input;
    input.role = Role::player;
    input.hatch.choice = static_cast<uint32_t>(ability);
    return send(input);
}

bool native_network_combat_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, int ability, uint32_t caller_rva) {
    return native_network_target_input(actor_native_id, actor_address, target_native_id, target_address, ability,
                                       caller_rva, Verb::combat);
}

bool native_network_jump_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t caller_rva) {
    if (!on_thread() || !ready || !local_ready || failed || config.role != Role::player || caller_rva != 0x948adbu) {
        return false;
    }
    NativeSceneLocalIdentity actor{};
    if (!own_living_actor(actor_native_id, actor_address, actor)) {
        return false;
    }
    const bool sent = intention(Verb::jump, 0, 0, 0, actor.id);
    event("network_jump_ui", ",\"player\":" + std::to_string(player) + ",\"entity\":" + std::to_string(actor.id) +
                                 ",\"caller_rva\":" + std::to_string(caller_rva) +
                                 ",\"sent\":" + (sent ? "true" : "false"));
    return sent;
}

bool native_network_plant_food_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t fruit_native_id,
                                     uint64_t seen_place, uint32_t caller_rva) {
    if (!on_thread() || !ready || !local_ready || failed || config.role != Role::player || caller_rva != 0x9356abu ||
        !fruit_native_id || fruit_native_id > uint32_t(INT32_MAX) || !seen_place) {
        return false;
    }
    NativeSceneLocalIdentity actor{};
    if (!own_living_actor(actor_native_id, actor_address, actor)) {
        return false;
    }
    const bool sent = intention(Verb::forage, static_cast<int>(fruit_native_id), seen_place, 0, actor.id);
    event("network_plant_food_ui", ",\"player\":" + std::to_string(player) + ",\"entity\":" + std::to_string(actor.id) +
                                       ",\"fruit_native_id\":" + std::to_string(fruit_native_id) + ",\"caller_rva\":" +
                                       std::to_string(caller_rva) + ",\"sent\":" + (sent ? "true" : "false"));
    return sent;
}

bool native_network_pickup_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, uint32_t caller_rva) {
    return native_network_target_input(actor_native_id, actor_address, target_native_id, target_address, 0, caller_rva,
                                       Verb::pickup);
}

bool native_network_fossil_input(uint32_t actor_native_id, uintptr_t actor_address, uint32_t target_native_id,
                                 uintptr_t target_address, uint32_t caller_rva) {
    return native_network_target_input(actor_native_id, actor_address, target_native_id, target_address, 0, caller_rva,
                                       Verb::collect);
}

namespace {
// Whether this player's game window can take a posted input for the scene `epoch`.
bool input_window_available(uint64_t epoch) {
    if (config.role != Role::player || !ready || !local_ready || failed || epoch != scene || !game_window ||
        !original_window_proc || !IsWindow(game_window)) {
        return false;
    }
    return true;
}

// The messages a mouse produces, in client pixels: a move to the point, then for action 2 the left button down and
// up there, for action 3 the right one. Posted, so the game's own message loop delivers them at its usual point in
// the frame.
bool post_pointer(int x, int y, uint64_t action) {
    const LPARAM point = MAKELPARAM(x, y);
    // The original also polls the cursor; for the next seconds this process alone sees it at the same point.
    POINT screen{x, y};
    if (ClientToScreen(game_window, &screen)) {
        set_virtual_pointer(screen, 3000);
    }
    bool posted = PostMessageW(game_window, WM_MOUSEMOVE, 0, point) != 0;
    if (action == 2) {
        posted = posted && PostMessageW(game_window, WM_LBUTTONDOWN, MK_LBUTTON, point) &&
                 PostMessageW(game_window, WM_LBUTTONUP, 0, point);
    }
    if (action == 3) {
        posted = posted && PostMessageW(game_window, WM_RBUTTONDOWN, MK_RBUTTON, point) &&
                 PostMessageW(game_window, WM_RBUTTONUP, 0, point);
    }
    return posted;
}
} // namespace

worker::Result native_network_input(const worker::Message& request) {
    if (!on_thread() || !peer || !worker::valid_input(request)) {
        return worker::Result::invalid;
    }
    if (!input_window_available(request.epoch)) {
        return worker::Result::unavailable;
    }
    const bool down = request.values[0] == 1;
    const auto key = static_cast<WPARAM>(request.values[1]);
    // The same message a keyboard produces: repeat count 1 and the key's scan code; a release
    // also carries the previous-state and transition bits. Posted, so the game's own message
    // loop delivers it to the window procedure at its usual point in the frame.
    LPARAM detail = 1 | (static_cast<LPARAM>(MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC)) << 16);
    if (!down) {
        detail |= static_cast<LPARAM>(0xC0000000u);
    }
    // The bridge itself consumes the movement and jump keys from the message. An ability key is the
    // original's to handle, and it also asks Windows whether the key is down.
    if (key >= '1' && key <= '9') {
        set_virtual_key(static_cast<unsigned>(key), down);
    }
    const bool posted = PostMessageW(game_window, down ? WM_KEYDOWN : WM_KEYUP, key, detail) != 0;
    event("network_input_posted",
          ",\"key\":" + std::to_string(unsigned(key)) + ",\"down\":" + (down ? "true" : "false") +
              ",\"posted\":" + (posted ? "true" : "false") + ",\"input_class\":\"INPUT_IN_PROCESS\"");
    return posted ? worker::Result::accepted : worker::Result::failed;
}

worker::Result native_network_click(const worker::Message& request) {
    if (!on_thread() || !peer || !worker::valid_click(request)) {
        return worker::Result::invalid;
    }
    if (!input_window_available(request.epoch)) {
        return worker::Result::unavailable;
    }
    int x = 0, y = 0;
    if (!native_scene_entity_screen_point(scene, request.values[1], static_cast<unsigned>(request.values[2]), x, y)) {
        return worker::Result::unavailable;
    }
    const auto action = request.values[0];
    const bool posted = post_pointer(x, y, action);
    event("network_pointer_posted", ",\"entity\":" + std::to_string(request.values[1]) +
                                        ",\"action\":" + std::to_string(action) + ",\"point\":[" + std::to_string(x) +
                                        "," + std::to_string(y) + "],\"posted\":" + (posted ? "true" : "false") +
                                        ",\"input_class\":\"INPUT_IN_PROCESS\"");
    return posted ? worker::Result::accepted : worker::Result::failed;
}

worker::Result native_network_point(const worker::Message& request) {
    if (!on_thread() || !peer || !worker::valid_point(request)) {
        return worker::Result::invalid;
    }
    if (!input_window_available(request.epoch)) {
        return worker::Result::unavailable;
    }
    const int x = static_cast<int>(request.values[1]), y = static_cast<int>(request.values[2]);
    RECT client{};
    if (!GetClientRect(game_window, &client) || x >= client.right || y >= client.bottom) {
        return worker::Result::unavailable;
    }
    const auto action = request.values[0];
    const bool posted = post_pointer(x, y, action);
    event("network_point_posted", ",\"action\":" + std::to_string(action) + ",\"point\":[" + std::to_string(x) + "," +
                                      std::to_string(y) + "],\"client\":[" + std::to_string(client.right) + "," +
                                      std::to_string(client.bottom) + "],\"posted\":" + (posted ? "true" : "false") +
                                      ",\"input_class\":\"INPUT_IN_PROCESS\"");
    return posted ? worker::Result::accepted : worker::Result::failed;
}

worker::Result native_network_command(const worker::Message& request) {
    if (!on_thread() || !peer) {
        return worker::Result::unavailable;
    }
    if (request.op == worker::Op::jump) {
        return intention(Verb::jump) ? worker::Result::accepted : worker::Result::unavailable;
    }
    if (request.op == worker::Op::move && request.values[2] <= 3) {
        return intention(Verb::move, static_cast<int>(request.values[2])) ? worker::Result::accepted
                                                                          : worker::Result::unavailable;
    }
    if (request.op == worker::Op::stop) {
        return intention(Verb::stop) ? worker::Result::accepted : worker::Result::unavailable;
    }
    if (request.op == worker::Op::network_action) {
        // Developer acceptance request through this real client's authenticated
        // connection. Explicit actor IDs permit a deliberate ownership challenge;
        // only the authority may accept/reject an actor intention.
        for (size_t i = 6; i < request.values.size(); ++i) {
            if (request.values[i]) {
                return worker::Result::invalid;
            }
        }
        const bool social = request.values[2] == static_cast<uint32_t>(Verb::social);
        const bool combat = request.values[2] == static_cast<uint32_t>(Verb::combat);
        const bool steering = request.values[2] == static_cast<uint32_t>(Verb::steer);
        const bool release = request.values[2] == static_cast<uint32_t>(Verb::steer_stop);
        // The same authenticated packet validation used by physical input
        // admits diagnostic steering. Check wide IPC values before narrowing.
        if (request.values[0] != player || request.values[2] > 11 || request.values[2] == 4 ||
            (social     ? request.values[3] >= 88 && request.values[3] != UINT32_MAX
             : combat   ? request.values[3] >= 88
             : steering ? request.values[3] >= uint64_t(1) << 30
             : release  ? request.values[3] != 0
                        : request.values[3] > 3)) {
            return worker::Result::invalid;
        }
        const int argument = social && request.values[3] == UINT32_MAX ? -1 : static_cast<int>(request.values[3]);
        return intention(static_cast<Verb>(request.values[2]), argument, request.values[4], request.values[5],
                         request.values[1])
                   ? worker::Result::accepted
                   : worker::Result::unavailable;
    }
    return worker::Result::unavailable;
}

bool native_network_authority_inputs_ready() {
    if (!authority_inputs_armed) {
        return true;
    }
    return on_thread() && peer && config.role == Role::authority && !failed && ready && welcomed && local_ready &&
           transfer.work == TransferWork::idle && peer->transport_active();
}

bool native_network_death_authority() {
    return on_thread() && peer && config.role == Role::authority && !failed && ready && welcomed && local_ready &&
           transfer.work == TransferWork::idle && peer->transport_active();
}

bool native_network_transfer_context(uint64_t request, uint64_t epoch) {
    if (!on_thread() || !peer || config.role != Role::authority || failed || !local_ready ||
        transfer.work == TransferWork::idle || transfer.pause_token != request || transfer.command.scene != epoch ||
        native_actor_worker_status().epoch != epoch) {
        return false;
    }
    if (transfer.work == TransferWork::startup && config.maintenance_transfer == request) {
        return !connect_started || !welcomed || peer->transport_active();
    }
    return welcomed && peer->transport_active();
}

worker::Result native_network_fence_authority_inputs(uint64_t expected_scene, uint64_t request) {
    using Result = worker::Result;
    if (!on_thread() || !peer || config.role != Role::authority || !authority_inputs_armed || !connect_started) {
        return Result::unavailable;
    }
    if (!request) {
        return Result::invalid;
    }
    const auto state = native_actor_worker_status();
    if (expected_scene != state.epoch || expected_scene != scene) {
        return Result::stale;
    }
    quarantine("The authority input lease was fenced by developer control.");
    if (!authority_input_fence_epoch) {
        authority_input_fence_epoch = native_actor_fence_network_inputs("developer_input_fence");
    }
    event("network_authority_input_fence_receipt",
          ",\"request\":" + std::to_string(request) + ",\"scene\":" + std::to_string(scene) +
              ",\"input_epoch\":" + std::to_string(authority_input_fence_epoch) + ",\"inputs_fenced\":" +
              (authority_input_fence_epoch ? "true" : "false") + ",\"native_world_idle_qualified\":false");
    return authority_input_fence_epoch ? Result::accepted : Result::failed;
}

void dispose_native_network() {
    if (!on_thread()) {
        return;
    }
    reset_history();
    history_publication = {};
    history_publication_epoch = 0;
    next_history_send = 0;
    relationship_publication = {};
    relationship_publication_epoch = 0;
    next_relationship_send = 0;
    relationship_unpublishable = false;
    reset_native_death();
    client_death = {};
    client_hatch = {};
    clear_native_social_view(false);
    client_social_deadline = 0;
    client_social = {};
    dispose_native_global_preload();
    if (config.role == Role::authority && authority_inputs_armed) {
        authority_input_fence_epoch = native_actor_fence_network_inputs("network_dispose", false);
    }
    dispose_native_content_network();
    if (listening) {
        messages->RemoveListener(&listener, App::kMsgAppUpdate);
        messages->RemoveListener(&listener, startup_menu_ready_message);
        listening = false;
    }
    if (original_window_proc && IsWindow(game_window)) {
        SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_window_proc));
    }
    original_window_proc = nullptr;
    game_window = nullptr;
    if (peer) {
        peer->stop();
        peer.reset();
        status("disconnected", "The game session has closed.");
    }
    dispose_native_scene();
    if (owns_persistence) {
        dispose_native_persistence();
    }
}
} // namespace sporemp
