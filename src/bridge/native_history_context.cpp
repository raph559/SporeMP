#include "native_history_context.h"
#include "native_history_abi.h"
#include <Spore/IO.h>
#include <Spore/Simulator/Serialization.h>
#include "native_replica.h"
#include "native_scene.h"
#include "native_actors.h"
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <intrin.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

namespace sporemp {
struct NativeHistoryScopeAccess {
    static bool secondary(const NativeHistoryOwnerScope* scope) {
        return scope && scope->secondary_;
    }

    static bool key_valid(const NativeHistoryOwnerScope* scope) {
        return scope && scope->key_valid_;
    }

    static const ResourceKey& key(const NativeHistoryOwnerScope* scope) {
        return scope->key_;
    }

    static uint32_t action(const NativeHistoryOwnerScope* scope) {
        return scope ? scope->action_ : UINT32_MAX;
    }

    static Simulator::cCreatureAnimal* target(const NativeHistoryOwnerScope* scope) {
        return scope ? scope->target_ : nullptr;
    }
};

namespace {
using Animal = Simulator::cCreatureAnimal;
using Abi = NativeHistoryAbi<Animal>;
Abi::Get get_original = nullptr;
Abi::Event event_original = nullptr;
Abi::Allocate allocate_original = nullptr;
Abi::Construct construct_original = nullptr;
Abi::Initialize initialize_original = nullptr;
Abi::Delete delete_original = nullptr;
Abi::CreateEntry create_entry_original = nullptr;
Abi::Serialize<Simulator::ISerializerWriteStream> write_original = nullptr;
Abi::Serialize<Simulator::ISerializerReadStream> read_original = nullptr;
void* checkpoint_root = nullptr;
Abi::ResizePayload resize_payload_original = nullptr;
Abi::ReadCategory read_category_original = nullptr;
uintptr_t image_base = 0;
DWORD engine_thread = 0;
PlayerContextEvent sink = nullptr;
bool (*thread_check)() = nullptr;
void* primary_manager = nullptr;
void* secondary_manager = nullptr;
uint64_t b_id = 0, b_epoch = 0, failure_epoch = 0, next_serial = 0;
const char* failure = nullptr;
unsigned action_depth = 0, event_depth = 0;
bool retiring = false, event_secondary = false;
NativeHistoryOwnerScope* current_scope = nullptr;
NativeHistoryOwnerScope* event_owner_scope = nullptr;
NativeHistoryReceipt last_receipt{};

struct Presentation {
    void* root = nullptr;
    void* primary = nullptr;
    uint64_t epoch = 0, entity = 0, generation = 0;
    network::HistoryState state{};
    bool published = false;
};

Presentation presentation;
// 668C50 publishes borrowed entry pointers into caller-owned UI vectors.
// A timeline close is not proof that every query consumer has released them.
// Keep published original roots alive until process exit; reuse an identical
// admission on reconnect. This bounded experimental retention avoids a UAF.
constexpr size_t presentation_root_capacity = 16;
std::vector<Presentation> retained_presentations;
void* ui_root = nullptr;

bool on_thread() {
    return GetCurrentThreadId() == engine_thread;
}

void event(const char* name, const char* format = "", ...) {
    if (!sink || !on_thread()) {
        return;
    }
    char fields[1536]{};
    va_list args;
    va_start(args, format);
    const int result = vsprintf_s(fields, format, args);
    va_end(args);
    if (result >= 0) {
        sink(name, fields);
    }
}

void fail(const char* reason) {
    if (failure) {
        return;
    }
    failure = reason;
    failure_epoch = b_epoch;
    event("native_history_acceptance_failed",
          ",\"actor\":%llu,\"epoch\":%llu,\"reason\":\"%s\",\"original_event_preserved\":true", b_id, b_epoch, reason);
}

bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto begin = reinterpret_cast<uintptr_t>(address), region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin >= region && size <= info.RegionSize && begin - region <= info.RegionSize - size;
}

template <class T>
T word(const void* address, size_t offset) {
    T result{};
    memcpy(&result, static_cast<const unsigned char*>(address) + offset, sizeof(result));
    return result;
}

bool layout(void* history) {
    return readable(history, 0x40) && word<uintptr_t>(history, 0) == image_base + 0x1014354 &&
           word<uintptr_t>(history, 8) == image_base + 0x1014330;
}

uint64_t hash_bytes(uint64_t hash, const void* data, size_t size) {
    const auto bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool payload(const void* entry, const unsigned char*& bytes, size_t& size) {
    bytes = nullptr;
    size = 0;
    if (!readable(entry, 0x20)) {
        return false;
    }
    const auto begin = word<uintptr_t>(entry, 0xc), end = word<uintptr_t>(entry, 0x10),
               capacity = word<uintptr_t>(entry, 0x14);
    if (end < begin || capacity < end || (end - begin) % 4 || (end - begin) > 1048576 ||
        (end > begin && !readable(reinterpret_cast<void*>(begin), end - begin))) {
        return false;
    }
    bytes = reinterpret_cast<const unsigned char*>(begin);
    size = end - begin;
    return true;
}

// Walks the list of the category node at this address, front to back: every link is checked against the node
// before it, every entry is added to the category's hash and to the event count. When the expected entry is met,
// its category is written out; meeting it twice is a failure.
bool digest_category(uint32_t address, NativeHistoryDigest& result, const void* expected_entry,
                     uint32_t* entry_category, uint64_t& hash) {
    const auto node = reinterpret_cast<const unsigned char*>(address);
    const auto sentinel = address + 4;
    auto previous = sentinel;
    for (auto item = word<uintptr_t>(node, 4); item != sentinel;) {
        if (result.events >= 65536 || !readable(reinterpret_cast<void*>(item), 0x28)) {
            return false;
        }
        const auto list_node = reinterpret_cast<const unsigned char*>(item);
        if (word<uintptr_t>(list_node, 4) != previous) {
            return false;
        }
        const auto entry = list_node + 8;
        const unsigned char* data = nullptr;
        size_t size = 0;
        if (!payload(entry, data, size)) {
            return false;
        }
        if (expected_entry == entry && entry_category) {
            if (*entry_category != UINT32_MAX) {
                return false;
            }
            *entry_category = word<uint32_t>(node, 0);
        }
        hash = hash_bytes(hash, entry, 12);
        hash = hash_bytes(hash, &size, sizeof(size));
        if (size) {
            hash = hash_bytes(hash, data, size);
        }
        ++result.events;
        previous = item;
        item = word<uintptr_t>(list_node, 0);
    }
    return word<uintptr_t>(node, 8) == previous;
}

// Native category hash nodes: key+0, list sentinel+4, next bucket node+10.
// List nodes: next+0, previous+4, original entry+8 (size20). This is a
// nonmutating census, established by 7EBA20/7EC0A0/7EB7F0, not an SDK cast.
bool digest(void* manager, NativeHistoryDigest& result, const void* expected_entry = nullptr,
            uint32_t* entry_category = nullptr) {
    result = {};
    if (!layout(manager)) {
        return false;
    }
    if (entry_category) {
        *entry_category = UINT32_MAX;
    }
    result.hash = hash_bytes(1469598103934665603ull, static_cast<const unsigned char*>(manager) + 0x30, 8);
    const auto buckets = word<uintptr_t>(manager, 0x10);
    const auto count = word<uint32_t>(manager, 0x14), expected = word<uint32_t>(manager, 0x18);
    if (!count || count > 65536 || expected > 65536 ||
        !readable(reinterpret_cast<void*>(buckets), (size_t(count) + 1) * 4)) {
        return false;
    }
    const auto table = reinterpret_cast<const uint32_t*>(buckets);
    for (uint32_t i = 0; i < count; ++i) {
        for (auto address = table[i]; address;) {
            if (result.categories >= expected || !readable(reinterpret_cast<void*>(address), 0x14)) {
                return false;
            }
            const auto node = reinterpret_cast<const unsigned char*>(address);
            uint64_t hash = hash_bytes(1469598103934665603ull, node, 4);
            if (!digest_category(address, result, expected_entry, entry_category, hash)) {
                return false;
            }
            result.hash += hash;
            ++result.categories;
            address = word<uintptr_t>(node, 0x10);
        }
    }
    result.valid = result.categories == expected;
    return result.valid;
}

bool same(const NativeHistoryDigest& a, const NativeHistoryDigest& b) {
    return a.valid && b.valid && a.categories == b.categories && a.events == b.events && a.hash == b.hash;
}

bool snapshot(void* manager, network::HistoryState& output) {
    output = {};
    NativeHistoryDigest observed;
    if (!digest(manager, observed) || observed.events > network::history_capacity) {
        return false;
    }
    network::HistoryState state;
    memcpy(state.date.data(), static_cast<const unsigned char*>(manager) + 0x30, 8);
    state.count = observed.events;
    const auto buckets = word<uintptr_t>(manager, 0x10), count = word<uint32_t>(manager, 0x14);
    const auto table = reinterpret_cast<const uint32_t*>(buckets);
    uint32_t populated = 0;
    for (uint32_t i = 0; i < count; ++i) {
        for (auto address = table[i]; address;) {
            const auto node = reinterpret_cast<const unsigned char*>(address);
            const auto category = word<uint32_t>(node, 0), item = word<uintptr_t>(node, 4);
            // Original Read materializes empty categories 7..12. They contain no
            // events and are not an additional history or a reward to replay.
            if (category < 7 || category > 12) {
                return false;
            }
            if (item != address + 4) {
                // The category's own list, front to back; digest above has checked its links.
                if (category != 8 || populated) {
                    return false;
                }
                for (auto link = item; link != address + 4;
                     link = word<uintptr_t>(reinterpret_cast<const unsigned char*>(link), 0)) {
                    if (populated >= network::history_capacity || !readable(reinterpret_cast<void*>(link), 0x28)) {
                        return false;
                    }
                    const auto entry = reinterpret_cast<const unsigned char*>(link) + 8;
                    const unsigned char* bytes = nullptr;
                    size_t size = 0;
                    if (!payload(entry, bytes, size) || size != 48) {
                        return false;
                    }
                    memcpy(state.entries[populated].header.data(), entry, 12);
                    memcpy(state.entries[populated].data.data(), bytes, 48);
                    ++populated;
                }
            }
            address = word<uintptr_t>(node, 0x10);
        }
    }
    if (populated != state.count || !network::valid_history(state)) {
        return false;
    }
    output = state;
    return true;
}

bool presentation_identity(uint64_t epoch, uint64_t entity, uint64_t generation) {
    if (!on_thread() || !native_replica_is_client() || Simulator::GetGameModeID() != kGameCreature) {
        return false;
    }
    auto manager = Simulator::cGameNounManager::Get();
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    NativeSceneLocalIdentity identity;
    return avatar &&
           native_scene_local_identity(epoch, avatar->mID, reinterpret_cast<uintptr_t>(avatar), identity, true) &&
           identity.owner == 2 && identity.id == entity && identity.generation == generation;
}

// A new original history container, constructed and initialized by the original code, or null. The name labels
// the allocation.
void* create_history(const char* name) {
    const auto allocation = allocate_original(0x40, name, 0, 0, nullptr, 0);
    if (!allocation) {
        return nullptr;
    }
    auto constructed = construct_original(allocation);
    if (constructed != allocation || !layout(constructed) || !initialize_original(constructed)) {
        delete_original(allocation, 1);
        return nullptr;
    }
    return constructed;
}

bool append_snapshot(Presentation& destination, const network::HistoryState& state) {
    if (!network::valid_history(state)) {
        return false;
    }
    if (destination.root) {
        network::HistoryState current;
        if (!snapshot(destination.root, current) || !(current == destination.state)) {
            return false;
        }
        if (current == state) {
            return true;
        }
        // Published pointers and their payload stay stable: a history only
        // gains entries after the ones it had.
        if (!network::history_extends(current, state)) {
            return false;
        }
    } else {
        const auto constructed = create_history("SporeMP history display");
        if (!constructed) {
            return false;
        }
        destination.root = constructed;
        memcpy(static_cast<unsigned char*>(constructed) + 0x30, state.date.data(), 8);
    }
    for (uint32_t index = destination.state.count; index < state.count; ++index) {
        const auto& source = state.entries[index];
        auto entry = create_entry_original(destination.root, 8, source.header[2]);
        if (!entry) {
            return false;
        }
        resize_payload_original(entry, 12);
        const unsigned char* data = nullptr;
        size_t size = 0;
        if (!payload(entry, data, size) || size != 48) {
            return false;
        }
        NativeHistoryDigest census;
        uint32_t category = UINT32_MAX;
        if (!digest(destination.root, census, entry, &category) || category != 8 || census.events != index + 1 ||
            census.categories != 1) {
            return false;
        }
        memcpy(entry, source.header.data(), 12);
        memcpy(const_cast<unsigned char*>(data), source.data.data(), 48);
    }
    network::HistoryState observed;
    if (!snapshot(destination.root, observed) || !(observed == state)) {
        return false;
    }
    destination.state = state;
    return true;
}

struct Projection {
    Presentation* destination;
    const network::HistoryState* state;
};

bool project_snapshot(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    return native_replica_allows(replica::Mutation::apply_state) && append_snapshot(*args.destination, *args.state);
}

void dispose_if_idle() {
    if (!retiring || action_depth || event_depth || !secondary_manager) {
        return;
    }
    auto disposing = secondary_manager;
    secondary_manager = nullptr;
    delete_original(disposing, 1);
    event("native_history_retired", ",\"actor\":%llu,\"epoch\":%llu,\"deleting_destructor\":true", b_id, b_epoch);
    primary_manager = nullptr;
    b_id = b_epoch = 0;
    retiring = false;
    last_receipt = {};
}

void* __cdecl manager_hook() {
    const auto where = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - image_base);
    auto primary = get_original();
    if (on_thread() && ui_root && where == 0x268c5e) {
        return ui_root;
    }
    if (on_thread() && event_secondary && current_scope == event_owner_scope && secondary_manager &&
        native_history_manager_caller(where)) {
        if (primary != primary_manager) {
            fail("primary_history_changed_during_event");
        }
        return secondary_manager;
    }
    return primary;
}

// What the original reader left in the caller's vector while the displayed root was selected: the number of
// sixteen-byte rows (UINT32_MAX when the vector cannot be read), and whether every row's entry is a category-8 entry
// of that root.
struct ReadRows {
    uint32_t rows = UINT32_MAX;
    bool owned = true;
};

ReadRows inspect_rows(const void* output) {
    ReadRows read;
    if (!readable(output, 12)) {
        return read;
    }
    const auto begin = word<uintptr_t>(output, 0), end = word<uintptr_t>(output, 4);
    if (end >= begin && (end - begin) % 16 == 0 && end - begin <= 65536 &&
        (end == begin || readable(reinterpret_cast<void*>(begin), end - begin))) {
        read.rows = static_cast<uint32_t>((end - begin) / 16);
        for (auto item = begin; item < end; item += 16) {
            auto entry = reinterpret_cast<void*>(word<uintptr_t>(reinterpret_cast<void*>(item), 12));
            NativeHistoryDigest observed;
            uint32_t entry_category = UINT32_MAX;
            if (!entry || !digest(ui_root, observed, entry, &entry_category) || entry_category != 8) {
                read.owned = false;
            }
        }
    }
    return read;
}

void __cdecl read_category_hook(void* output, uint32_t category) {
    const auto caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - image_base);
    if (!on_thread()) {
        read_category_original(output, category);
        return;
    }
    auto previous = ui_root;
    ui_root = nullptr;
    const bool selected = native_history_ui_reader(caller, category) && presentation.root &&
                          presentation.primary == get_original() &&
                          presentation_identity(presentation.epoch, presentation.entity, presentation.generation);
    if (selected) {
        ui_root = presentation.root;
        presentation.published = true;
    }
    read_category_original(output, category);
    if (selected) {
        const ReadRows read = inspect_rows(output);
        event("native_history_ui_read",
              ",\"owner\":2,\"entity\":%llu,\"epoch\":%llu,\"category\":8,\"entries\":%u,\"rows\":%u,\"owned_entries\":"
              "%s,\"caller_rva\":%u",
              presentation.entity, presentation.epoch, presentation.state.count, read.rows,
              read.rows != UINT32_MAX && read.owned ? "true" : "false", caller);
    }
    ui_root = previous;
}

// Both histories as they are now: the first player's and the second player's.
void digest_both(NativeHistoryDigest& primary, NativeHistoryDigest& secondary) {
    digest(primary_manager, primary);
    digest(secondary_manager, secondary);
}

// The one event the second player's scope admits is the first kill of its latched target, with a valid species key.
void expect_first_kill(Animal* target, uint32_t kind, uint32_t caller) {
    if (!native_history_first_kill(NativeHistoryScopeAccess::action(current_scope), kind, caller) ||
        target != NativeHistoryScopeAccess::target(current_scope) ||
        !NativeHistoryScopeAccess::key_valid(current_scope)) {
        fail("unexpected_original_history_event");
    }
}

// After an event of the first player: the second player's history must be exactly what it was.
void record_primary_event(uint32_t kind, uint32_t caller, const NativeHistoryDigest& primary_before,
                          const NativeHistoryDigest& secondary_before) {
    NativeHistoryDigest primary_after{}, secondary_after{};
    digest_both(primary_after, secondary_after);
    const bool preserved = same(secondary_before, secondary_after);
    if (!preserved) {
        fail("secondary_history_changed_by_primary_event");
    }
    event("native_primary_history_event_return",
          ",\"owner\":1,\"epoch\":%llu,\"requested_event\":%u,\"caller_rva\":%u,\"primary_entries_before\":%u,"
          "\"primary_entries_after\":%u,\"secondary_entries\":%u,\"secondary_hash\":%llu,\"secondary_unchanged\":%s",
          b_epoch, kind, caller, primary_before.events, primary_after.events, secondary_after.events,
          secondary_after.hash, preserved ? "true" : "false");
}

// The receipt of the entry the original event returned: read only from a category-8 entry of a valid history whose
// payload is the expected 48 bytes.
NativeHistoryReceipt read_receipt(void* entry, uint64_t serial, uint32_t category,
                                  const NativeHistoryDigest& secondary_after) {
    const unsigned char* data = nullptr;
    size_t size = 0;
    NativeHistoryReceipt receipt{};
    receipt.serial = serial;
    receipt.category = category;
    if (entry && secondary_after.valid && category == 8 && payload(entry, data, size) && size == 48) {
        receipt.valid = true;
        receipt.time = word<uint32_t>(data, 0);
        receipt.key = ResourceKey(word<uint32_t>(data, 8), word<uint32_t>(data, 4), word<uint32_t>(data, 12));
        receipt.event_type = word<uint32_t>(data, 16);
        receipt.relation = word<uint32_t>(data, 32);
    }
    return receipt;
}

// After an event of the second player: the first player's history must be exactly what it was, and the second
// player's must have gained the one expected entry. The receipt is kept for the caller of the action.
void record_secondary_event(void* entry, uint64_t serial, uint32_t kind, uint32_t caller,
                            const NativeHistoryDigest& primary_before, const NativeHistoryDigest& secondary_before) {
    NativeHistoryDigest primary_after{}, secondary_after{};
    uint32_t category = UINT32_MAX;
    digest(primary_manager, primary_after);
    digest(secondary_manager, secondary_after, entry, &category);
    NativeHistoryReceipt receipt = read_receipt(entry, serial, category, secondary_after);
    if (!same(primary_before, primary_after)) {
        fail("primary_history_changed_by_secondary_event");
    }
    if (!receipt.valid || !secondary_before.valid || !secondary_after.valid ||
        !native_history_one_event_added(secondary_before.events, secondary_before.categories, secondary_after.events,
                                        secondary_after.categories) ||
        secondary_after.events > network::history_capacity || !NativeHistoryScopeAccess::key_valid(current_scope) ||
        receipt.key != NativeHistoryScopeAccess::key(current_scope) || receipt.event_type != 5 ||
        receipt.relation != 1) {
        fail("unexpected_original_history_result");
    }
    receipt.valid = receipt.valid && !failure;
    last_receipt = receipt;
    event("native_history_event_return",
          ",\"serial\":%llu,\"actor\":%llu,\"epoch\":%llu,\"caller_rva\":%u,\"requested_event\":%u,\"valid\":%s,"
          "\"category\":%u,\"species\":%u,\"species_type\":%u,\"species_group\":%u,\"event_type\":%u,\"relation\":%"
          "u,\"time\":%u,\"primary_unchanged\":%s,\"primary_entries\":%u,\"primary_hash\":%llu,\"secondary_entries_"
          "before\":%u,\"secondary_entries_after\":%u,\"secondary_hash\":%llu,\"failure_latched\":%s",
          serial, b_id, b_epoch, caller, kind, receipt.valid ? "true" : "false", category, receipt.key.instanceID,
          uint32_t(receipt.key.typeID), receipt.key.groupID, receipt.event_type, receipt.relation, receipt.time,
          same(primary_before, primary_after) ? "true" : "false", primary_after.events, primary_after.hash,
          secondary_before.events, secondary_after.events, secondary_after.hash, failure ? "true" : "false");
}

void* __cdecl event_hook(Animal* target, uint32_t kind) {
    if (!on_thread()) {
        return event_original(target, kind);
    }
    const auto caller = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - image_base);
    const bool previous = event_secondary;
    auto previous_owner_scope = event_owner_scope;
    // The owner is latched at original action entry. A failed predicate never
    // redirects this in-flight B event into the primary history container.
    const bool secondary = secondary_manager && NativeHistoryScopeAccess::secondary(current_scope);
    event_secondary = secondary;
    event_owner_scope = current_scope;
    ++event_depth;
    NativeHistoryDigest primary_before{}, secondary_before{};
    uint64_t serial = 0;
    const bool primary_observed = secondary_manager && !secondary && !native_replica_is_client();
    if (primary_observed) {
        digest_both(primary_before, secondary_before);
    }
    if (secondary) {
        serial = ++next_serial;
        digest_both(primary_before, secondary_before);
        expect_first_kill(target, kind, caller);
    }
    auto result = event_original(target, kind);
    if (primary_observed) {
        record_primary_event(kind, caller, primary_before, secondary_before);
    }
    if (secondary) {
        record_secondary_event(result, serial, kind, caller, primary_before, secondary_before);
    }
    event_secondary = previous;
    event_owner_scope = previous_owner_scope;
    --event_depth;
    dispose_if_idle();
    return result;
}
} // namespace

NativeHook native_history_manager_binding() {
    return {reinterpret_cast<void**>(&get_original), reinterpret_cast<void*>(manager_hook)};
}

NativeHook native_history_event_binding() {
    return {reinterpret_cast<void**>(&event_original), reinterpret_cast<void*>(event_hook)};
}

NativeHook native_history_ui_binding() {
    return {reinterpret_cast<void**>(&read_category_original), reinterpret_cast<void*>(read_category_hook)};
}

bool prepare_native_history(uintptr_t base, uintptr_t end, DWORD thread, PlayerContextEvent event_sink,
                            bool (*check)()) {
    image_base = base;
    engine_thread = thread;
    sink = event_sink;
    thread_check = check;

    struct Prefix {
        uint32_t rva;
        unsigned char bytes[20];
        uint32_t relocations;
        unsigned length;
    };

    static const Prefix prefixes[] = {
#include "native_history_prefixes.inc"
    };
    for (const auto& prefix : prefixes) {
        unsigned char bytes[20];
        memcpy(bytes, prefix.bytes, 20);
        for (unsigned i = 0; i + 4 <= prefix.length; ++i) {
            if (prefix.relocations & (1u << i)) {
                uint32_t value;
                memcpy(&value, bytes + i, 4);
                value += static_cast<uint32_t>(base) - 0x400000;
                memcpy(bytes + i, &value, 4);
            }
        }
        if (prefix.rva > end - base || prefix.length > end - base - prefix.rva ||
            memcmp(reinterpret_cast<void*>(base + prefix.rva), bytes, prefix.length)) {
            event("history_binding_rejected", ",\"rva\":%u", prefix.rva);
            return false;
        }
    }
    get_original = reinterpret_cast<Abi::Get>(base + 0x27de50);
    event_original = reinterpret_cast<Abi::Event>(base + 0x7a5f10);
    allocate_original = reinterpret_cast<Abi::Allocate>(base + 0xb47240);
    construct_original = reinterpret_cast<Abi::Construct>(base + 0x3ec170);
    initialize_original = reinterpret_cast<Abi::Initialize>(base + 0x3ec010);
    delete_original = reinterpret_cast<Abi::Delete>(base + 0x3ec210);
    create_entry_original = reinterpret_cast<Abi::CreateEntry>(base + 0x3ec230);
    resize_payload_original = reinterpret_cast<Abi::ResizePayload>(base + 0x3eb8f0);
    read_category_original = reinterpret_cast<Abi::ReadCategory>(base + 0x268c50);
    write_original = reinterpret_cast<Abi::Serialize<Simulator::ISerializerWriteStream>>(base + 0x3eb210);
    read_original = reinterpret_cast<Abi::Serialize<Simulator::ISerializerReadStream>>(base + 0x3ec2f0);
    event("history_bindings_checked", ",\"code_prefixes\":%u", unsigned(std::size(prefixes)));
    return true;
}

bool enable_native_history(uint64_t actor_id, uint64_t epoch) {
    if (!on_thread() || !thread_check || !thread_check() || !get_original || !actor_id || !epoch ||
        epoch == failure_epoch || secondary_manager || action_depth || event_depth) {
        return false;
    }
    auto primary = get_original();
    NativeHistoryDigest before{}, after{};
    if (!digest(primary, before)) {
        return false;
    }
    const auto constructed = create_history("SporeMP history context");
    if (!constructed) {
        return false;
    }
    NativeHistoryDigest empty{};
    if (!digest(constructed, empty) || empty.events || empty.categories || !digest(primary, after) ||
        !same(before, after)) {
        delete_original(constructed, 1);
        return false;
    }
    secondary_manager = constructed;
    primary_manager = primary;
    b_id = actor_id;
    b_epoch = epoch;
    retiring = false;
    failure = nullptr;
    last_receipt = {};
    event("native_history_enabled",
          ",\"owner\":2,\"actor\":%llu,\"epoch\":%llu,\"allocation_bytes\":64,\"native_init\":true,\"published\":false,"
          "\"listener_registered\":false,\"primary_entries\":%u,\"primary_hash\":%llu",
          b_id, b_epoch, before.events, before.hash);
    return true;
}

void retire_native_history() {
    if (on_thread() && secondary_manager) {
        retiring = true;
        dispose_if_idle();
    }
}

bool native_history_read(uint32_t owner, NativeHistoryDigest& result) {
    result = {};
    if (!on_thread() || !get_original || (owner != 1 && owner != 2)) {
        return false;
    }
    return digest(owner == 2 ? secondary_manager : get_original(), result);
}

bool native_history_last_receipt(NativeHistoryReceipt& result) {
    result = last_receipt;
    return on_thread() && result.valid;
}

bool native_history_relationship_query_active() {
    return on_thread() && event_secondary && current_scope == event_owner_scope && secondary_manager;
}

bool native_history_failure_ready(uint64_t epoch, const char*& reason) {
    reason = nullptr;
    if (!on_thread() || epoch != failure_epoch || action_depth || event_depth || !failure) {
        return false;
    }
    reason = failure;
    return true;
}

bool native_history_replication_ready(uint64_t epoch) {
    return on_thread() && !native_replica_is_client() && secondary_manager && b_epoch == epoch && !retiring;
}

bool capture_native_history(uint32_t slot, uint64_t epoch, network::HistoryState& state) {
    state = {};
    return native_history_replication_ready(epoch) && slot == 2 && !failure && !action_depth && !event_depth &&
           native_actor_worker_status().epoch == epoch && Simulator::GetGameModeID() == kGameCreature &&
           snapshot(secondary_manager, state);
}

namespace {
// Date and count, then fifteen words per entry and never fewer than fifteen: the eighteen words a history of
// no or one entry has always had, so checkpoints sealed before 0.0.226 read unchanged.
constexpr size_t checkpoint_word_capacity = 3 + 15 * network::history_capacity;

size_t checkpoint_word_count(uint32_t entries) {
    return 3 + 15 * size_t(entries ? entries : 1);
}

std::array<uint32_t, checkpoint_word_capacity> checkpoint_words(const network::HistoryState& state) {
    std::array<uint32_t, checkpoint_word_capacity> words{};
    words[0] = state.date[0];
    words[1] = state.date[1];
    words[2] = state.count;
    for (size_t entry = 0; entry < network::history_capacity; ++entry) {
        for (size_t i = 0; i < 3; ++i) {
            words[3 + 15 * entry + i] = state.entries[entry].header[i];
        }
        for (size_t i = 0; i < 12; ++i) {
            words[6 + 15 * entry + i] = state.entries[entry].data[i];
        }
    }
    return words;
}

// The inverse of checkpoint_words.
network::HistoryState checkpoint_state(const std::array<uint32_t, checkpoint_word_capacity>& words) {
    network::HistoryState state;
    state.date = {words[0], words[1]};
    state.count = words[2];
    for (size_t entry = 0; entry < network::history_capacity; ++entry) {
        for (size_t i = 0; i < 3; ++i) {
            state.entries[entry].header[i] = words[3 + 15 * entry + i];
        }
        for (size_t i = 0; i < 12; ++i) {
            state.entries[entry].data[i] = words[6 + 15 * entry + i];
        }
    }
    return state;
}
} // namespace

bool write_native_history_checkpoint(uint64_t epoch, Simulator::ISerializerWriteStream* stream) {
    network::HistoryState before, after;
    NativeHistoryDigest primary_before, primary_after;
    if (!stream || !stream->IsGood() || !capture_native_history(2, epoch, before) ||
        !native_history_read(1, primary_before)) {
        return false;
    }
    const auto words = checkpoint_words(before);
    if (!IO::WriteUInt32(stream->GetRecord()->GetStream(), words.data(), checkpoint_word_count(before.count))) {
        return false;
    }
    const bool result = write_original(static_cast<unsigned char*>(secondary_manager) + 8, stream);
    const bool unchanged = capture_native_history(2, epoch, after) && before == after &&
                           native_history_read(1, primary_after) && same(primary_before, primary_after);
    const bool valid = result && stream->IsGood() && unchanged;
    event("native_history_checkpoint_written",
          ",\"owner\":2,\"success\":%s,\"entries\":%u,\"primary_unchanged\":%s,\"primary_categories\":%u,\"primary_"
          "entries\":%u,\"primary_hash\":%llu,\"replayed_events\":0",
          valid ? "true" : "false", before.count, unchanged ? "true" : "false", primary_before.categories,
          primary_before.events, primary_before.hash);
    return valid;
}

bool read_native_history_checkpoint(Simulator::ISerializerReadStream* stream) {
    if (!on_thread() || !stream || !stream->IsGood() || checkpoint_root || secondary_manager || action_depth ||
        event_depth || stream->GetSerializationVersion() < 0x230002) {
        return false;
    }
    std::array<uint32_t, checkpoint_word_capacity> words{};
    if (!IO::ReadUInt32(stream->GetRecord()->GetStream(), words.data(), 18) || words[2] > network::history_capacity ||
        (words[2] > 1 &&
         !IO::ReadUInt32(stream->GetRecord()->GetStream(), words.data() + 18, checkpoint_word_count(words[2]) - 18))) {
        return false;
    }
    const network::HistoryState expected = checkpoint_state(words);
    if (!network::valid_history(expected)) {
        return false;
    }
    auto root = allocate_original(0x40, "SporeMP history checkpoint", 0, 0, nullptr, 0);
    if (!root) {
        return false;
    }
    const bool constructed = construct_original(root) == root && layout(root) && initialize_original(root);
    network::HistoryState observed;
    const bool valid = constructed && read_original(static_cast<unsigned char*>(root) + 8, stream) &&
                       stream->IsGood() && snapshot(root, observed) && observed == expected;
    if (valid) {
        checkpoint_root = root;
    } else {
        delete_original(root, 1);
    }
    event("native_history_checkpoint_read",
          ",\"owner\":2,\"success\":%s,\"entries\":%u,\"exact_state\":%s,\"replayed_events\":0",
          valid ? "true" : "false", expected.count, valid ? "true" : "false");
    return valid;
}

bool adopt_native_history_checkpoint(uint64_t actor_id, uint64_t epoch) {
    if (!on_thread() || !actor_id || !epoch || !checkpoint_root || secondary_manager || action_depth || event_depth ||
        epoch == failure_epoch || !thread_check || !thread_check()) {
        return false;
    }
    NativeHistoryDigest primary;
    network::HistoryState state;
    if (!digest(get_original(), primary) || !snapshot(checkpoint_root, state)) {
        return false;
    }
    secondary_manager = checkpoint_root;
    checkpoint_root = nullptr;
    primary_manager = get_original();
    b_id = actor_id;
    b_epoch = epoch;
    retiring = false;
    failure = nullptr;
    last_receipt = {};
    event("native_history_checkpoint_adopted",
          ",\"owner\":2,\"actor\":%llu,\"entries\":%u,\"primary_categories\":%u,\"primary_entries\":%u,\"primary_"
          "hash\":%llu,\"replayed_events\":0",
          actor_id, state.count, primary.categories, primary.events, primary.hash);
    return true;
}

void discard_native_history_checkpoint() {
    if (on_thread() && checkpoint_root) {
        delete_original(checkpoint_root, 1);
        checkpoint_root = nullptr;
    }
}

void reset_native_history_presentation() {
    if (!on_thread() || !presentation.root) {
        return;
    }
    if (presentation.published) {
        retained_presentations.push_back(presentation);
        event("native_history_display_retained",
              ",\"entity\":%llu,\"epoch\":%llu,\"roots\":%zu,\"reason\":\"borrowed_original_ui_entries\"",
              presentation.entity, presentation.epoch, retained_presentations.size());
    } else {
        delete_original(presentation.root, 1);
    }
    presentation = {};
}

bool present_native_history(uint64_t epoch, uint32_t owner, const network::HistoryFrame& frame) {
    if (!on_thread() || owner != 2 || !network::valid_history_frame(frame) ||
        !presentation_identity(epoch, frame.entity, frame.generation) || !get_original || !create_entry_original) {
        return false;
    }
    auto primary = get_original();
    NativeHistoryDigest before, after;
    if (!digest(primary, before)) {
        return false;
    }
    if (presentation.root && (presentation.epoch != epoch || presentation.entity != frame.entity ||
                              presentation.generation != frame.generation || presentation.primary != primary)) {
        reset_native_history_presentation();
    }
    if (!presentation.root) {
        const auto found =
            std::find_if(retained_presentations.begin(), retained_presentations.end(), [&](const auto& value) {
                // Remote fixture IDs/epochs can repeat in a different authenticated
                // session. Reuse only an identical snapshot or its safe empty ->
                // first-entry append, never let an old count1 block a fresh count0.
                const bool compatible = network::history_extends(value.state, frame.state);
                return value.epoch == epoch && value.entity == frame.entity && value.generation == frame.generation &&
                       value.primary == primary && compatible;
            });
        if (found != retained_presentations.end()) {
            presentation = *found;
            retained_presentations.erase(found);
        } else {
            if (retained_presentations.size() >= presentation_root_capacity) {
                return false;
            }
            presentation.epoch = epoch;
            presentation.entity = frame.entity;
            presentation.generation = frame.generation;
            presentation.primary = primary;
        }
    }
    Projection projection{&presentation, &frame.state};
    const bool applied = native_replica_project(project_snapshot, &projection, false);
    if (!applied || !digest(primary, after) || !same(before, after)) {
        return false;
    }
    event("native_history_display_applied",
          ",\"owner\":2,\"entity\":%llu,\"epoch\":%llu,\"revision\":%llu,\"entries\":%u,\"exact\":true,\"primary_"
          "unchanged\":true,\"reward_executed\":false",
          frame.entity, epoch, frame.revision, frame.state.count);
    return true;
}

NativeHistoryOwnerScope::NativeHistoryOwnerScope(uint32_t owner, uint64_t actor_id, uint64_t epoch, uint32_t action,
                                                 Animal* target, const uint32_t* key) {
    if (!on_thread()) {
        return;
    }
    entered_ = true;
    previous_ = current_scope;
    current_scope = this;
    ++action_depth;
    secondary_ = secondary_manager && owner == 2 && actor_id == b_id && epoch == b_epoch;
    action_ = action;
    target_ = target;
    key_valid_ = readable(key, sizeof(ResourceKey));
    if (key_valid_) {
        memcpy(&key_, key, sizeof(key_));
    }
}

NativeHistoryOwnerScope::~NativeHistoryOwnerScope() {
    if (!entered_) {
        return;
    }
    current_scope = previous_;
    --action_depth;
    dispose_if_idle();
}
} // namespace sporemp
