#include "native_inventory.h"
#include "native_inventory_abi.h"
#include "native_player_context.h"
#include "native_actors.h"
#include "native_replica.h"
#include "native_scene.h"
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace sporemp {
namespace {
using namespace network;
using Items = Simulator::cCollectableItems;
using Key = Simulator::cCollectableItemID;
uintptr_t image = 0, image_end = 0;
DWORD engine_thread = 0;
NativeInventoryAbi::Manager manager_get = nullptr;
NativeInventoryAbi::Notify notify_part = nullptr;

bool on_thread() {
    return engine_thread && GetCurrentThreadId() == engine_thread;
}

bool readable(const void* pointer, size_t size) {
    MEMORY_BASIC_INFORMATION memory{};
    const auto at = reinterpret_cast<uintptr_t>(pointer);
    return pointer && VirtualQuery(pointer, &memory, sizeof(memory)) == sizeof(memory) && memory.State == MEM_COMMIT &&
           !(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && at >= reinterpret_cast<uintptr_t>(memory.BaseAddress) &&
           size <= memory.RegionSize - (at - reinterpret_cast<uintptr_t>(memory.BaseAddress));
}

bool capture(Simulator::cPlayer* player, InventoryState& output) {
    if (!player || !validate_native_player_checkpoint(player, player->mID)) {
        return false;
    }
    auto items = player->mpCRGItems.get();
    if (items->mItemStatusInfos.empty() || items->mItemStatusInfos.size() > inventory_capacity ||
        items->mUnlockedItems.size() > inventory_capacity || items->mUnlockPoints < 0) {
        return false;
    }
    InventoryState state;
    state.points = uint32_t(items->mUnlockPoints);
    state.group = items->mItemsGroupID;
    std::map<uint64_t, uint32_t> unlocked;
    uint32_t index = 0;
    for (const auto& key : items->mUnlockedItems) {
        if (!unlocked.emplace((uint64_t(key.groupID) << 32) | key.instanceID, index++).second) {
            return false;
        }
    }
    for (const auto& value : items->mItemStatusInfos) {
        InventoryItem row{value.first.instanceID, value.first.groupID, uint32_t(value.second), UINT32_MAX};
        const auto found = unlocked.find(row.key());
        if (found != unlocked.end()) {
            row.index = found->second;
        }
        state.items.push_back(row);
    }
    std::sort(state.items.begin(), state.items.end(), [](const auto& a, const auto& b) { return a.key() < b.key(); });
    if (!valid_inventory(state) ||
        unlocked.size() != static_cast<size_t>(std::count_if(state.items.begin(), state.items.end(),
                                                             [](const auto& item) { return item.status & 1; }))) {
        return false;
    }
    output = std::move(state);
    return true;
}

struct Projection {
    Simulator::cPlayer* player;
    const InventoryState* state;
};

bool apply(void* raw) {
    auto& args = *static_cast<Projection*>(raw);
    auto items = args.player->mpCRGItems.get();
    const auto& state = *args.state;
    if (!native_replica_allows(replica::Mutation::apply_state) || items->mItemsGroupID != state.group ||
        items->mItemStatusInfos.size() != state.items.size()) {
        return false;
    }
    std::map<uint64_t, uint8_t*> statuses;
    for (auto& item : items->mItemStatusInfos) {
        statuses.emplace((uint64_t(item.first.groupID) << 32) | item.first.instanceID, &item.second);
    }
    std::map<uint32_t, Key> ordered;
    for (const auto& row : state.items) {
        if (!statuses.count(row.key())) {
            return false;
        }
        if (row.status & 1) {
            ordered.emplace(row.index, Key{row.instance, row.group});
        }
    }
    // Allocate before changing the displayed inventory. This mirrors observed
    // values only: no UnlockPart, reward callback, picker or save is executed.
    eastl::list<Key> replacement;
    for (const auto& value : ordered) {
        replacement.push_back(value.second);
    }
    items->mUnlockedItems.swap(replacement);
    for (const auto& row : state.items) {
        *statuses.at(row.key()) = uint8_t(row.status);
    }
    items->mUnlockPoints = static_cast<int>(state.points);
    return true;
}

// The keys the previous state already had unlocked; none without a previous state.
std::set<uint64_t> unlocked_keys(const InventoryState* previous) {
    std::set<uint64_t> known;
    if (previous) {
        for (const auto& row : previous->items) {
            if (row.status & 1) {
                known.insert(row.key());
            }
        }
    }
    return known;
}

// Original D3D4A0 -> D37F10 -> E14CE0 passes instance/B1B104/group.
// 593960 only separates the observed item-ID pair; no lookup/picker.
// False, with nothing notified, when the original's manager is not a readable object of this image.
bool notify_new_part(const InventoryItem& row) {
    auto display = manager_get();
    if (!readable(display, 0x40)) {
        return false;
    }
    const auto table = *static_cast<uintptr_t*>(display);
    if (table < image || table >= image_end) {
        return false;
    }
    notify_part(display, {row.instance, 0x00b1b104, row.group});
    return true;
}
} // namespace

bool prepare_native_inventory(uintptr_t base, uintptr_t end, DWORD thread) {
    image = base;
    image_end = end;
    engine_thread = thread;
    manager_get = nullptr;
    notify_part = nullptr;
    // Original dc04aee5 executable, static export m11-part-notify-20260928.
    const unsigned char notify_prefix[] = {0x53, 0x8b, 0xd9, 0x83, 0x7b, 0x3c, 0x00, 0x56,
                                           0x57, 0x75, 0x59, 0x6a, 0x00, 0x6a, 0x00, 0x6a};
    if (end <= base || end - base < 0x127eb64 ||
        std::memcmp(reinterpret_cast<void*>(base + 0xa14ce0), notify_prefix, sizeof(notify_prefix))) {
        return false;
    }
    const auto getter = reinterpret_cast<const unsigned char*>(base + 0x73d500);
    uint32_t address = 0;
    std::memcpy(&address, getter + 1, 4);
    if (getter[0] != 0xa1 || getter[5] != 0xc3 || address != base + 0x127eb60) {
        return false;
    }
    manager_get = reinterpret_cast<NativeInventoryAbi::Manager>(base + 0x73d500);
    notify_part = reinterpret_cast<NativeInventoryAbi::Notify>(base + 0xa14ce0);
    return true;
}

bool capture_native_inventory(uint32_t slot, uint64_t epoch, InventoryState& state) {
    if (!on_thread() || native_replica_is_client() || slot < 1 || slot > 2 ||
        native_actor_worker_status().epoch != epoch || Simulator::GetGameModeID() != kGameCreature) {
        return false;
    }
    auto manager = Simulator::cGameNounManager::Get();
    return manager && capture(slot == 1 ? manager->GetPlayer() : native_second_player(epoch), state);
}

bool present_native_inventory(uint64_t epoch, uint32_t owner, const InventoryFrame& frame, const InventoryState& state,
                              const InventoryState* previous) {
    if (!on_thread() || !native_replica_is_client() || !notify_part || !manager_get || !valid_inventory(state)) {
        return false;
    }
    auto manager = Simulator::cGameNounManager::Get();
    auto player = manager ? manager->GetPlayer() : nullptr;
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    NativeSceneLocalIdentity identity;
    if (!player || !avatar || !validate_native_player_checkpoint(player, player->mID) ||
        !native_scene_local_identity(epoch, avatar->mID, reinterpret_cast<uintptr_t>(avatar), identity, true) ||
        identity.owner != owner || identity.id != frame.entity || identity.generation != frame.generation) {
        return false;
    }
    Projection projection{player, &state};
    if (!native_replica_project(apply, &projection, false)) {
        return false;
    }
    InventoryState observed;
    if (!capture(player, observed) || !(observed == state)) {
        return false;
    }
    char fields[400]{};
    sprintf_s(fields,
              ",\"owner\":%u,\"revision\":%llu,\"entity\":%llu,\"generation\":%llu,\"rows\":%zu,\"unlocked\":%zu,"
              "\"points\":%u,\"exact\":true,\"reward_executed\":false",
              owner, frame.revision, frame.entity, frame.generation, state.items.size(),
              static_cast<size_t>(player->mpCRGItems->mUnlockedItems.size()), state.points);
    native_actor_worker_event("native_inventory_applied", fields);
    const std::set<uint64_t> known = unlocked_keys(previous);
    for (const auto& row : state.items) {
        sprintf_s(fields, ",\"owner\":%u,\"revision\":%llu,\"instance\":%u,\"group\":%u,\"status\":%u,\"index\":%u",
                  owner, frame.revision, row.instance, row.group, row.status, row.index);
        native_actor_worker_event("native_inventory_item", fields);
        if (!previous || !(row.status & 1) || known.count(row.key())) {
            continue;
        }
        if (!notify_new_part(row)) {
            return false;
        }
        native_actor_worker_event("native_inventory_part_notification", fields);
    }
    return true;
}
} // namespace sporemp
