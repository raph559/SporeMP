#pragma once
#include "native_player_context.h"
#include "../network/history_wire.h"
#include <Spore/ResourceKey.h>
#include <Spore/Simulator/cCreatureAnimal.h>

namespace sporemp {
struct NativeHistoryDigest {
    bool valid = false;
    uint32_t categories = 0, events = 0;
    uint64_t hash = 0;
};

struct NativeHistoryReceipt {
    bool valid = false;
    uint64_t serial = 0;
    ResourceKey key{};
    uint32_t category = 0, event_type = 0, relation = 0, time = 0;
};

bool prepare_native_history(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event_sink,
                            bool (*check)());
NativeHook native_history_manager_binding();
NativeHook native_history_event_binding();
NativeHook native_history_ui_binding();
// Caller has authenticated the foreign actor and scene through the existing
// relationship context. This creates original storage; it never publishes it.
bool enable_native_history(uint64_t actor_id, uint64_t epoch);
void retire_native_history();
bool native_history_read(uint32_t owner, NativeHistoryDigest&);
bool native_history_last_receipt(NativeHistoryReceipt&);
// Consult only at the separately pinned relationship getter return 7A58B3.
bool native_history_relationship_query_active();
bool native_history_failure_ready(uint64_t epoch, const char*& reason);
bool native_history_replication_ready(uint64_t epoch);
bool capture_native_history(uint32_t slot, uint64_t epoch, network::HistoryState& state);
// Original serialization of the bounded Creature history. The read root stays
// private until the checkpoint authenticates and adopts the restored B graph.
bool write_native_history_checkpoint(uint64_t epoch, Simulator::ISerializerWriteStream*);
bool read_native_history_checkpoint(Simulator::ISerializerReadStream*);
bool adopt_native_history_checkpoint(uint64_t actor_id, uint64_t epoch);
void discard_native_history_checkpoint();
bool present_native_history(uint64_t epoch, uint32_t owner, const network::HistoryFrame& frame);
void reset_native_history_presentation();

// Required around every original relationship action, including neutral,
// unowned and primary actions. Nested scopes always mask their parent.
class NativeHistoryOwnerScope {
public:
    NativeHistoryOwnerScope(uint32_t owner, uint64_t actor_id, uint64_t epoch, uint32_t action,
                            Simulator::cCreatureAnimal* target, const uint32_t* key);
    ~NativeHistoryOwnerScope();
    NativeHistoryOwnerScope(const NativeHistoryOwnerScope&) = delete;
    NativeHistoryOwnerScope& operator=(const NativeHistoryOwnerScope&) = delete;

private:
    NativeHistoryOwnerScope* previous_ = nullptr;
    Simulator::cCreatureAnimal* target_ = nullptr;
    ResourceKey key_{};
    uint32_t action_ = UINT32_MAX;
    bool entered_ = false, secondary_ = false, key_valid_ = false;
    friend struct NativeHistoryScopeAccess;
};
} // namespace sporemp
