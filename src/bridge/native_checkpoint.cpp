#include "native_checkpoint.h"
#include "native_player_context.h"
#include "native_award_context.h"
#include "native_relationship_context.h"
#include "native_history_context.h"
#include <Spore/ModAPI.h>
#include <Spore/Simulator/SubSystem/cStrategy.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/IO.h>
#include <array>
#include <cstdio>
#include <cstring>

namespace sporemp {
namespace {
constexpr uint32_t strategy_id = 0x53504d39, magic = 0x39435053;
uint32_t schema = 3;
DWORD engine_thread = 0;
uintptr_t image_base = 0, image_end = 0;
NativePersistenceEvent sink = nullptr;
bool enabled = false, armed = false, wrote = false, read_seen = false, read_good = false, preserved = false;
uint64_t request_id = 0;
uint64_t recovery_deadline = 0;
NativeActorCheckpoint actors;
NativeAwardCheckpoint awards;
NativePickupCheckpoint pickups;
Simulator::cPlayer* loaded_player = nullptr;
uint32_t loaded_player_id = UINT32_MAX;

bool on_thread() {
    return enabled && GetCurrentThreadId() == engine_thread;
}

void event(const char* name, bool success, uint32_t native_id = UINT32_MAX) {
    if (!on_thread() || !sink) {
        return;
    }
    char fields[384]{};
    sprintf_s(fields, ",\"request\":%llu,\"success\":%s,\"player_native_id\":%u,\"schema\":%u", request_id,
              success ? "true" : "false", native_id, schema);
    sink(name, fields);
}

bool stream_slot(void* object, size_t slot, uint32_t rva) {
    if (!object) {
        return false;
    }
    const auto table = *reinterpret_cast<const uintptr_t*>(object);
    return table >= image_base && table + slot + sizeof(uintptr_t) <= image_end &&
           *reinterpret_cast<const uintptr_t*>(table + slot) == image_base + rva;
}

// Explicit scalar encoding; native containers and pointers are handled only
// by SPORE's own COM serializer and never copied to a bridge sidecar.
using Header = std::array<uint32_t, 36 + 2048 * 2>;

Header header() {
    Header h{};
    h[0] = magic;
    h[1] = schema;
    h[2] = static_cast<uint32_t>(h.size());
    h[3] = native_second_player(actors.epoch)->mID;
    for (size_t i = 0; i < 2; ++i) {
        const auto& a = actors.actors[i];
        const size_t n = 4 + i * 6;
        h[n] = a.native_id;
        h[n + 1] = a.herd_native_id;
        h[n + 2] = a.species_instance;
        h[n + 3] = a.species_type;
        h[n + 4] = a.species_group;
        h[n + 5] = a.archetype;
    }
    for (size_t i = 0; i < awards.words.size(); ++i) {
        h[16 + i] = awards.words[i];
    }
    for (size_t i = 0; i < pickups.grants.size(); ++i) {
        h[36 + i * 2] = pickups.grants[i][0];
        h[37 + i * 2] = pickups.grants[i][1];
    }
    return h;
}

class Extension final : public Simulator::ISimulatorStrategy {
    int references = 0;

public:
    int AddRef() override {
        return ++references;
    }

    int Release() override {
        return --references;
    } // static object, no native destructor

    void Initialize() override {}

    void Dispose() override {}

    const char* GetName() const override {
        return "SporeMP native checkpoint";
    }

    void OnModeExited(uint32_t, uint32_t) override {}

    void OnModeEntered(uint32_t, uint32_t) override {}

    uint32_t GetLastGameMode() const override {
        return UINT32_MAX;
    }

    uint32_t GetCurrentGameMode() const override {
        return UINT32_MAX;
    }

    bool func24h(uint32_t) override {
        return false;
    }

    bool Write(Simulator::ISerializerStream* raw) override {
        if (!on_thread() || !armed) {
            return false; // zero-length extension for ordinary saves
        }
        auto stream = static_cast<Simulator::ISerializerWriteStream*>(raw);
        auto player = native_second_player(actors.epoch);
        // SDK vtable +2c, pinned GOG x86 WritePointer at 69D980.
        if (!player || !stream_slot(stream, 0x2c, 0x29d980)) {
            return false;
        }
        auto h = header();
        auto file = stream->GetRecord()->GetStream();
        if (!IO::WriteUInt32(file, h.data(), h.size())) {
            return false;
        }
        // Original COM serializer roots the complete original cPlayer graph.
        // IsGood is authoritative: the original WritePointer implementation
        // sets its stream status and does not promise a meaningful AL return.
        stream->WritePointer(player);
        wrote = stream->IsGood() && (schema == 3 || (write_native_relationship_checkpoint(actors.epoch, stream) &&
                                                     write_native_history_checkpoint(actors.epoch, stream)));
        event("native_checkpoint_extension_written", wrote, player->mID);
        return wrote;
    }

    bool Read(Simulator::ISerializerStream* raw) override {
        if (!on_thread() || read_seen || loaded_player) {
            return false;
        }
        read_seen = true;
        auto stream = static_cast<Simulator::ISerializerReadStream*>(raw);
        if (!stream_slot(stream, 0x28, 0x29e170)) {
            return false;
        }
        Header h{};
        auto file = stream->GetRecord()->GetStream();
        if (!IO::ReadUInt32(file, h.data(), h.size()) || h[0] != magic || (h[1] != 3 && h[1] != 4) ||
            h[2] != h.size()) {
            return false;
        }
        schema = h[1];
        for (size_t i = 0; i < 2; ++i) {
            const size_t n = 4 + i * 6;
            actors.actors[i] = {h[n], h[n + 1], h[n + 2], h[n + 3], h[n + 4], h[n + 5]};
        }
        for (size_t i = 0; i < awards.words.size(); ++i) {
            awards.words[i] = h[16 + i];
        }
        for (size_t i = 0; i < pickups.grants.size(); ++i) {
            pickups.grants[i] = {h[36 + i * 2], h[37 + i * 2]};
        }
        if (!valid_native_award_checkpoint(awards)) {
            return false;
        }
        ObjectPtr object;
        if (!stream->ReadObjectPointer(0x02c216ed, object, true) || !object || !stream->IsGood()) {
            return false;
        }
        auto player = reinterpret_cast<Simulator::cPlayer*>(object.get());
        if (!validate_native_player_checkpoint(player, h[3])) {
            return false;
        }
        if (schema == 4 && (!read_native_relationship_checkpoint(stream) || !read_native_history_checkpoint(stream))) {
            discard_native_relationship_checkpoint();
            return false;
        }
        static_cast<Simulator::cGameData*>(player)->AddRef();
        loaded_player = player;
        loaded_player_id = player->mID;
        read_good = true;
        event("native_checkpoint_extension_read", true, player->mID);
        return true;
    }

    void OnLoad(const Simulator::cSavedGameHeader&) override {}

    bool WriteToXML(Simulator::XmlSerializer*) override {
        return false;
    }

    void Update(int, int) override {}

    void PostUpdate(int, int) override {}

    void func40h(uint32_t) override {}

    void func44h(uint32_t) override {}

    void func48h() override {}

    void func4Ch() override {}
} extension;
} // namespace

bool initialize_native_checkpoint(uintptr_t base, uintptr_t end, DWORD thread, NativePersistenceEvent callback) {
    if (enabled || GetCurrentThreadId() != thread || end <= base) {
        return false;
    }
    const unsigned char cleanup_call[] = {0xe8, 0x01, 0xe0, 0xff, 0xff, 0x8b, 0x06, 0x8b, 0x0c, 0x38};
    if (end - base < 0x72455a + sizeof(cleanup_call) ||
        memcmp(reinterpret_cast<const void*>(base + 0x72455a), cleanup_call, sizeof(cleanup_call))) {
        return false;
    }
    image_base = base;
    image_end = end;
    engine_thread = thread;
    sink = callback;
    // Register with ModAPI's persistence map only. There is no new game update
    // strategy and no replacement of any original subsystem.
    enabled = ModAPI::AddSimulatorStrategy(&extension, strategy_id);
    event("native_checkpoint_extension_registered", enabled);
    return enabled;
}

bool arm_native_checkpoint(const NativeActorCheckpoint& capture, uint64_t request) {
    if (!on_thread() || armed || !native_second_player(capture.epoch) ||
        !snapshot_native_awards(awards, capture.epoch, true) || !snapshot_native_pickup_checkpoint(pickups)) {
        return false;
    }
    schema = native_actor_experimental_foreign_ready(capture.epoch) ? 4 : 3;
    actors = capture;
    request_id = request;
    wrote = false;
    armed = true;
    return true;
}

bool finish_native_checkpoint_write() {
    const bool result = on_thread() && armed && wrote;
    armed = false;
    return result;
}

uint32_t native_checkpoint_schema() {
    return on_thread() ? schema : 0;
}

bool native_checkpoint_present() {
    return on_thread() && read_seen;
}

uint64_t native_checkpoint_deadline() {
    return on_thread() ? recovery_deadline : 0;
}

bool native_checkpoint_foreign_pair(uint64_t epoch, NativeActorCheckpoint& pair, uint32_t& player_id) {
    pair = {};
    player_id = UINT32_MAX;
    if (!on_thread() || schema != 4 || !read_good || !preserved || loaded_player || !recovery_deadline || !epoch ||
        actors.epoch != epoch) {
        event("native_checkpoint_pair_not_adopted", false);
        return false;
    }
    auto player = native_second_player(epoch);
    uint32_t a = 0, b = 0;
    if (!validate_native_player_checkpoint(player, loaded_player_id)) {
        event("native_checkpoint_pair_player_invalid", false, loaded_player_id);
        return false;
    }
    if (!native_actor_owner_native_id(1, a) || !native_actor_owner_native_id(2, b) || a != actors.actors[0].native_id ||
        b != actors.actors[1].native_id) {
        event("native_checkpoint_pair_actors_invalid", false);
        return false;
    }
    event("native_checkpoint_pair_admitted", true, loaded_player_id);
    pair = actors;
    player_id = loaded_player_id;
    return true;
}

bool observe_native_checkpoint_destroy(const void* noun, uint32_t caller_rva) {
    if (!on_thread() || !loaded_player || static_cast<const Simulator::cGameData*>(loaded_player) != noun) {
        return false;
    }
    char fields[256]{};
    sprintf_s(fields, ",\"player_native_id\":%u,\"caller_rva\":%u", loaded_player->mID, caller_rva);
    sink("native_checkpoint_player_destroy_requested", fields);
    // Native GameNounManager post-load B24240 enumerates a bounded snapshot of
    // players and destroys every non-primary at the B2455A call. Keep exactly
    // the authenticated extension root through that single-player cleanup.
    // Other destruction calls, primary players and unknown roots pass through.
    if (caller_rva != 0x72455f || !read_good || preserved ||
        native_persistence_snapshot().state != NativePersistenceState::loading ||
        !validate_native_player_checkpoint(loaded_player, loaded_player->mID)) {
        return false;
    }
    if (!restore_native_pickup_flags(pickups)) {
        return false;
    }
    preserved = true;
    event("native_checkpoint_player_preserved", true, loaded_player->mID);
    return true;
}

bool restore_native_checkpoint(uint64_t epoch, uint64_t request) {
    if (!on_thread() || !read_good || !loaded_player) {
        return false;
    }
    request_id = request;
    actors.epoch = epoch;
    const bool result = adopt_native_player_checkpoint(loaded_player, epoch) &&
                        restore_native_actor_checkpoint(actors, request, true) &&
                        restore_native_actor_awards(awards, epoch, schema == 4) &&
                        restore_native_pickup_checkpoint(pickups);
    event("native_checkpoint_extension_restored", result, loaded_player->mID);
    if (result) {
        static_cast<Simulator::cGameData*>(loaded_player)->Release();
        loaded_player = nullptr;
        recovery_deadline = GetTickCount64() + 300000;
    }
    return result;
}

void dispose_native_checkpoint() {
    if (!on_thread()) {
        return;
    }
    if (loaded_player) {
        static_cast<Simulator::cGameData*>(loaded_player)->Release();
        loaded_player = nullptr;
    }
    discard_native_relationship_checkpoint();
    enabled = false;
    armed = false;
    sink = nullptr;
    // ModAPI retains and releases the static strategy during its app shutdown,
    // after bridge dispose. Late callbacks see enabled=false.
}
} // namespace sporemp
