#include "native_player_transfer_probe.h"
#include "native_player_context.h"
#include "native_persistence.h"
#include "native_actors.h"
#include "native_replica.h"
#include "native_award_context.h"
#include "native_quiescence_probe.h"
#include "diagnostics.h"
#include "detour_transaction.h"
#include <Spore/App/IClassManager.h>
#include <Spore/App/cCreatureModeStrategy.h>
#include <Spore/Editors/SpeciesManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/SpacePlayerData.h>
#include <Spore/Simulator/cCreatureGameData.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Spore/Simulator/cCollectableItems.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/Simulator/cIdentityColorable.h>
#include <Spore/Simulator/Serialization.h>
#include <Spore/IO.h>
#include <Spore/Resource/DatabasePackedFile.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <intrin.h>
#include <set>
#include <vector>

namespace sporemp {
namespace {
using Player = Simulator::cPlayer;
using Animal = Simulator::cCreatureAnimal;
using Noun = Simulator::cGameData;
using Herd = Simulator::cHerd;
using Items = Simulator::cCollectableItems;
using Stage = Simulator::cCreatureGameData;
using ReadStream = Simulator::ISerializerReadStream;
using Stream = Simulator::ISerializerStream;
using Manager = Simulator::cGameNounManager;
constexpr uint32_t player_class = 0x02c21781, animal_class = 0x018eb45e, items_class = 0x03a3ac9f;
// The one serialization version an artifact may carry; the stage exports report it as theirs.
constexpr uint32_t accepted_serialization_version = 0x00250003;
uintptr_t base = 0, end = 0;
DWORD thread = 0;
NativePlayerTransferProbeEvent sink = nullptr;
bool initialized = false, used = false, attached = false;

struct RetainedImport {
    uint64_t epoch = 0;
    uint32_t player = 0, avatar = 0, herd = 0;
};

RetainedImport retained_import;
uint64_t request_id = 0;
using Factory = void*(__thiscall*)(App::IClassManager*, uint32_t, uint32_t, ICoreAllocator*, App::ISPClassFactory*);
using Read = bool(__thiscall*)(void*, Stream*);
using ReadWords = bool(__cdecl*)(IO::IStream*, uint32_t*, size_t, int);
using ColorSetter = void(__cdecl*)(float, float, float);
using Get = void*(__cdecl*)();
using Register = void(__thiscall*)(Player*);
using HerdReader = bool(__cdecl*)(ReadStream*, void**);
Factory factory_original = nullptr;
Read noun_read_original = nullptr, player_read_original = nullptr, animal_read_original = nullptr;
ReadWords words_original = nullptr;
ColorSetter color_original = nullptr;
Get theme_original = nullptr;
Register register_original = nullptr;
HerdReader herd_original = nullptr;

struct Scope {
    Player* player = nullptr;
    Animal* animal = nullptr;
    Items* items = nullptr;
    Herd* herd = nullptr;
    uint32_t player_id = 0, animal_id = 0, color_cursor = 0, local_avatar_flag = 0;
    bool player_read = false, failed = false;
    unsigned factories[3]{}, noun_reads = 0, player_reads = 0, animal_reads = 0, herd_reads = 0;
    unsigned color_headers = 0, color_writes = 0, themes = 0, registrations = 0;
};

thread_local Scope* scope = nullptr;

bool on_thread() {
    return initialized && GetCurrentThreadId() == thread;
}

void event(const char* name, const char* reason = nullptr) {
    if (!sink) {
        return;
    }
    char fields[512]{};
    sprintf_s(fields, ",\"request\":%llu,\"reason\":\"%s\",\"experimental\":true,\"transfer_qualified\":false",
              request_id, reason ? reason : "");
    sink(name, fields);
}

[[noreturn]] void partial_failure(const char* reason) {
    event("native_player_import_partial_failure", reason);
    // The caller must be a disposable supervisor-managed, disconnected worker.
    // Continuing after an original reader partially mutated native objects is unsafe.
    TerminateProcess(GetCurrentProcess(), 0x4d10);
    __fastfail(7);
}

uint32_t bits(float value) {
    uint32_t result = 0;
    memcpy(&result, &value, 4);
    return result;
}

float floating(uint32_t value) {
    float result = 0;
    memcpy(&result, &value, 4);
    return result;
}

std::array<uint32_t, 10> persistent_stage(const Stage& value) {
    return {static_cast<uint32_t>(value.mCurrentBrainLevel),
            bits(value.mAvatarNormalizingScale),
            static_cast<uint32_t>(value.mNumFlapsAllowed),
            static_cast<uint32_t>(value.mAbilityMode),
            bits(value.mEvolutionPoints),
            static_cast<uint32_t>(value.DEPRECATED_mLearnedAbilities),
            static_cast<uint32_t>(value.DEPRECATED_mLearnedAbilities >> 32),
            static_cast<uint32_t>(value.mLearnedAbilities[0]),
            static_cast<uint32_t>(value.mLearnedAbilities[1]),
            static_cast<uint32_t>(value.mLearnedAbilities[2])};
}

bool valid_stage(const std::array<uint32_t, 10>& words) {
    return words[0] <= 3 && words[2] <= 1000 && words[3] <= 1 && (words[9] & 0xff000000) == 0 &&
           std::isfinite(floating(words[1])) && floating(words[1]) > 0 && std::isfinite(floating(words[4])) &&
           floating(words[4]) >= 0;
}

std::array<uint32_t, 20> stage_checkpoint(const Stage& value) {
    return {static_cast<uint32_t>(value.mCurrentBrainLevel),
            bits(value.mAvatarNormalizingScale),
            static_cast<uint32_t>(value.mNumFlapsAllowed),
            uint32_t(value.field_C) | (uint32_t(value.field_D) << 1) | (uint32_t(value.field_E) << 2) |
                (uint32_t(value.field_F) << 3),
            uint32_t(value.field_10) | (uint32_t(value.field_11) << 1),
            static_cast<uint32_t>(value.field_14),
            bits(value.field_18.x),
            bits(value.field_18.y),
            bits(value.field_18.z),
            static_cast<uint32_t>(value.mAbilityMode),
            bits(value.mEvolutionPoints),
            0,
            static_cast<uint32_t>(value.DEPRECATED_mLearnedAbilities),
            static_cast<uint32_t>(value.DEPRECATED_mLearnedAbilities >> 32),
            static_cast<uint32_t>(value.mLearnedAbilities[0]),
            static_cast<uint32_t>(value.mLearnedAbilities[1]),
            static_cast<uint32_t>(value.mLearnedAbilities[2]),
            bits(value.mGlideStartPosition.x),
            bits(value.mGlideStartPosition.y),
            bits(value.mGlideStartPosition.z)};
}

bool prefix(uint32_t rva, const unsigned char* expected, size_t length) {
    return end > base && rva <= end - base && length <= end - base - rva &&
           !memcmp(reinterpret_cast<const void*>(base + rva), expected, length);
}

bool slot(void* object, uint32_t table_rva, uint32_t offset, uint32_t function_rva) {
    if (!object || table_rva + offset + 4 > end - base) {
        return false;
    }
    return *reinterpret_cast<uintptr_t*>(object) == base + table_rva &&
           *reinterpret_cast<uintptr_t*>(base + table_rva + offset) == base + function_rva;
}

bool active() {
    return scope && on_thread();
}

void* __fastcall factory_hook(App::IClassManager* self, void*, uint32_t id, uint32_t cast, ICoreAllocator* allocator,
                              App::ISPClassFactory* factory) {
    if (!active() || reinterpret_cast<uintptr_t>(_ReturnAddress()) != base + 0x29ea69) {
        return factory_original(self, id, cast, allocator, factory);
    }
    if (cast != 0x0179c807 || allocator || factory) {
        scope->failed = true;
        return nullptr;
    }
    Simulator::ISimulatorSerializable* object = nullptr;
    unsigned index = 3;
    if (id == player_class) {
        object = scope->player;
        index = 0;
    } else if (id == animal_class) {
        object = scope->animal;
        index = 1;
    } else if (id == items_class) {
        object = scope->items;
        index = 2;
    }
    if (!object || ++scope->factories[index] != 1) {
        scope->failed = true;
        return nullptr;
    }
    // Native 9203A0 returns a borrowed successful cast. COM 69EADB and
    // 69EAE7 subsequently retain its map and returned reference separately.
    return object->Cast(cast);
}

bool __fastcall noun_read_hook(void* self, void*, Stream* stream) {
    if (!active() || (self != scope->player && self != static_cast<Noun*>(scope->animal))) {
        return noun_read_original(self, stream);
    }
    auto noun = static_cast<Noun*>(self);
    const auto id = noun->mID;
    const bool result = noun_read_original(self, stream);
    noun->mID = id; // Preserve the allocated or already registered destination ID.
    ++scope->noun_reads;
    return result;
}

bool __fastcall player_read_hook(void* self, void*, Stream* stream) {
    if (!active() || self != scope->player) {
        return player_read_original(self, stream);
    }
    if (scope->player_read || ++scope->player_reads != 1) {
        partial_failure("duplicate_player_read");
    }
    scope->player_read = true;
    const bool result = player_read_original(self, stream);
    scope->player_read = false;
    return result;
}

bool __fastcall animal_read_hook(void* self, void*, Stream* stream) {
    if (!active() || self != scope->animal) {
        return animal_read_original(self, stream);
    }
    if (++scope->animal_reads != 1) {
        partial_failure("duplicate_avatar_read");
    }
    const bool result = animal_read_original(self, stream);
    scope->animal->mGeneralFlags = (scope->animal->mGeneralFlags & ~0x200) | scope->local_avatar_flag;
    if (scope->animal->mHerd.get() != scope->herd) {
        partial_failure("destination_herd_changed");
    }
    return result;
}

bool __cdecl words_hook(IO::IStream* stream, uint32_t* values, size_t count, int endian) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool result = words_original(stream, values, count, endian);
    if (active() && scope->player_read && caller == base + 0x87c9d0) {
        if (!result || !values || count != 1 || endian != 0) {
            partial_failure("color_header_read");
        }
        *values = scope->color_cursor;
        ++scope->color_headers;
    }
    return result;
}

void __cdecl color_hook(float red, float green, float blue) {
    if (active() && scope->player_read && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x87ca38) {
        ++scope->color_writes;
        return;
    }
    color_original(red, green, blue);
}

void* __cdecl theme_hook() {
    if (active() && scope->player_read && reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x87cae1) {
        ++scope->themes;
        return nullptr;
    } // Original Read explicitly accepts absent singleton.
    return theme_original();
}

void __fastcall register_hook(Player* player, void*) {
    if (active() && scope->player_read && player == scope->player &&
        reinterpret_cast<uintptr_t>(_ReturnAddress()) == base + 0x87cb40) {
        ++scope->registrations;
        return;
    } // Selected existing player already has its ten listeners.
    register_original(player);
}

bool __cdecl herd_hook(ReadStream* stream, void** field) {
    if (!active() || field != reinterpret_cast<void**>(&scope->animal->mHerd)) {
        return herd_original(stream, field);
    }
    void* absent = nullptr;
    const bool result = herd_original(stream, &absent);
    ++scope->herd_reads;
    if (absent || !stream->IsGood()) {
        partial_failure("slice_herd_reference_not_null");
    }
    return result; // Consume the native null without detaching the live native herd.
}

NativeHook hooks[] = {{reinterpret_cast<void**>(&factory_original), reinterpret_cast<void*>(factory_hook)},
                      {reinterpret_cast<void**>(&noun_read_original), reinterpret_cast<void*>(noun_read_hook)},
                      {reinterpret_cast<void**>(&player_read_original), reinterpret_cast<void*>(player_read_hook)},
                      {reinterpret_cast<void**>(&animal_read_original), reinterpret_cast<void*>(animal_read_hook)},
                      {reinterpret_cast<void**>(&words_original), reinterpret_cast<void*>(words_hook)},
                      {reinterpret_cast<void**>(&color_original), reinterpret_cast<void*>(color_hook)},
                      {reinterpret_cast<void**>(&theme_original), reinterpret_cast<void*>(theme_hook)},
                      {reinterpret_cast<void**>(&register_original), reinterpret_cast<void*>(register_hook)},
                      {reinterpret_cast<void**>(&herd_original), reinterpret_cast<void*>(herd_hook)}};

struct Collection {
    std::vector<std::array<uint32_t, 3>> statuses;
    std::vector<std::array<uint32_t, 2>> unlocked;
    uint32_t points = 0;
};

bool collection(Items* items, Collection& out) {
    if (!items || items->mItemStatusInfos.size() > 1024 || items->mUnlockedItems.size() > 1024) {
        return false;
    }
    for (const auto& item : items->mItemStatusInfos) {
        out.statuses.push_back({item.first.instanceID, item.first.groupID, item.second});
    }
    std::sort(out.statuses.begin(), out.statuses.end(),
              [](const auto& a, const auto& b) { return a[1] != b[1] ? a[1] < b[1] : a[0] < b[0]; });
    for (const auto& item : items->mUnlockedItems) {
        out.unlocked.push_back({item.instanceID, item.groupID});
    }
    out.points = static_cast<uint32_t>(items->mUnlockPoints);
    return true;
}

std::array<uint32_t, 4> progress(Player* p) {
    return {bits(p->mCurrentGoalProgress), bits(p->mGoalProgressTotal), bits(p->mSocialTraitProgress),
            bits(p->mCombatTraitProgress)};
}

bool duplicate_ids(Manager* manager, unsigned& players) {
    std::set<uint32_t> ids;
    players = 0;
    for (auto& noun : manager->mNouns) {
        if (!ids.insert(noun.mID).second) {
            return true;
        }
        if (noun.GetNounID() == player_class) {
            ++players;
        }
    }
    return false;
}

bool exact_keys(Simulator::SerializerDatabase* database, uint32_t schema) {
    eastl::vector<ResourceKey> keys;
    const unsigned expected = schema == 3 ? 6u : 5u;
    if (database->GetKeyList(keys) != expected || keys.size() != expected) {
        return false;
    }
    std::set<uint32_t> types;
    unsigned roots = 0, stages = 0;
    for (const auto& key : keys) {
        if (key.groupID == 0x53504d50 && key.typeID == 0x53504d47 && key.instanceID == 1) {
            ++roots;
            continue;
        }
        if (schema == 3 && key.groupID == 0x53504d50 && key.typeID == 0x53504d53 && key.instanceID == 1) {
            ++stages;
            continue;
        }
        if (key.groupID || key.instanceID != 0x0179d304 ||
            (key.typeID != 0x0179d310 && key.typeID != player_class && key.typeID != animal_class &&
             key.typeID != items_class) ||
            !types.insert(key.typeID).second) {
            return false;
        }
    }
    return roots == 1 && types.size() == 4 && stages == (schema == 3 ? 1u : 0u);
}

bool read_stage(Simulator::SerializerDatabase* database, const NativePlayerTransferFixture& expected, Stage& decoded) {
    auto destination = Stage::Get();
    if (reinterpret_cast<uintptr_t>(destination) != base + 0x129e370) {
        return false;
    }
    ReadStream* stream = nullptr;
    bool result = database->OpenReadStream(ResourceKey(1, 0x53504d53, 0x53504d50), &stream) && stream;
    if (result) {
        stream->SetSerializationVersion(expected.serialization_version);
        std::array<unsigned char, sizeof(Stage)> saved{};
        memcpy(saved.data(), destination, saved.size());
        // D2E9B0 deliberately resets the global stage, then reads its seven
        // persistent fields. Capture its real defaults before restoring A.
        result = reinterpret_cast<bool(__thiscall*)(Stage*, Stream*)>(base + 0x92e9b0)(destination, stream);
        memcpy(&decoded, destination, sizeof(decoded));
        memcpy(destination, saved.data(), saved.size());
        if (memcmp(destination, saved.data(), saved.size())) {
            partial_failure("destination_stage_restore");
        }
        result = result && stream->IsGood() && persistent_stage(decoded) == expected.stage_persistent;
    }
    if (stream) {
        result = database->CloseReadStream(stream) && result;
        stream->Release();
    }
    if (result && sink) {
        const auto words = stage_checkpoint(decoded);
        char fields[1000]{};
        sprintf_s(
            fields,
            ",\"request\":%llu,\"serialization_version\":%u,\"checkpoint_words\":["
            "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],"
            "\"persistent_values_match\":true,\"destination_stage_unchanged\":true,\"native_reset_defaults\":true,"
            "\"source_pending_false_by_closed_approval\":true,\"experimental\":true,\"transfer_qualified\":false",
            request_id, expected.serialization_version, words[0], words[1], words[2], words[3], words[4], words[5],
            words[6], words[7], words[8], words[9], words[10], words[11], words[12], words[13], words[14], words[15],
            words[16], words[17], words[18], words[19]);
        sink("native_player_stage_read", fields);
    }
    return result;
}
} // namespace

namespace {
// The first sixteen bytes of every original this probe hooks or calls.
bool code_prefixes_pinned() {
    // Preferred VAs are pinned to SDK cbf9206 / original GOG executable SHA
    // dc04aee5...; callers first pass the normal executable/content guard.
    const struct {
        uint32_t rva;
        unsigned char bytes[16];
    } checks[] = {
        {0x5203a0, {0x8b, 0x54, 0x24, 0x10, 0x8b, 0x01, 0x8b, 0x40, 0x24, 0x56, 0x52, 0x8b, 0x54, 0x24, 0x14, 0x52}},
        {0x718540, {0x81, 0xec, 0x18, 0x0a, 0, 0, 0x56, 0x8b, 0xb4, 0x24, 0x20, 0x0a, 0, 0, 0x8b, 0x06}},
        {0x87c960, {0x81, 0xec, 0x28, 0x0a, 0, 0, 0x53, 0x55, 0x56, 0x8b, 0xb4, 0x24, 0x38, 0x0a, 0, 0}},
        {0x8030e0, {0x81, 0xec, 0x18, 0x0a, 0, 0, 0x53, 0x56, 0x8b, 0xb4, 0x24, 0x24, 0x0a, 0, 0, 0x8b}},
        {0x53a6f0, {0x83, 0x7c, 0x24, 0x10, 0x01, 0x53, 0x8b, 0x5c, 0x24, 0x08, 0x74, 0x09, 0x8b, 0x03, 0x8b, 0x50}},
        {0x875380, {0x56, 0x8b, 0xf1, 0x57, 0x85, 0xf6, 0x74, 0x05, 0x8d, 0x7e, 0x34, 0xeb, 0x02, 0x33, 0xff, 0xe8}},
        {0x720490, {0x56, 0x8b, 0x74, 0x24, 0x0c, 0x8b, 0x0e, 0x85, 0xc9, 0x74, 0x0d, 0xc7, 0x06, 0, 0, 0}},
        {0x806660, {0x56, 0x57, 0x8b, 0xf9, 0xe8, 0xa7, 0x52, 0xf5, 0xff, 0xc7, 0x87, 0xb0, 0x02, 0, 0, 0}},
        {0x0df5c0, {0x55, 0x8b, 0xec, 0x81, 0xec, 0x8c, 0, 0, 0, 0x89, 0x8d, 0x78, 0xff, 0xff, 0xff, 0x8b}},
        {0x723920, {0x53, 0x8b, 0x5c, 0x24, 0x0c, 0x33, 0xc0, 0x57, 0x8b, 0xf9, 0x85, 0xdb, 0x0f, 0x84, 0xdf, 0}},
        {0x809b40, {0x83, 0xec, 0x48, 0x53, 0x55, 0x56, 0x57, 0xe8, 0xb4, 0x2f, 0xa7, 0xff, 0x8b, 0x5c, 0x24, 0x60}},
        {0x92e9b0, {0x81, 0xec, 0x14, 0x0a, 0, 0, 0x53, 0x56, 0x33, 0xc0, 0x8b, 0xf1, 0x6a, 0x50, 0x89, 0x46}}};

    for (const auto& check : checks) {
        if (!prefix(check.rva, check.bytes, sizeof(check.bytes))) {
            return false;
        }
    }
    return true;
}

// The originals whose first bytes hold an absolute address: each is compared with that address rebased. The
// class manager's factory slot is checked with them.
bool relocated_prefixes_pinned() {
    unsigned char get_bytes[] = {0xa1, 0, 0, 0, 0, 0xc3};
    auto address = static_cast<uint32_t>(base + 0x11fcc74);
    memcpy(get_bytes + 1, &address, 4);
    if (!prefix(0x27cb00, get_bytes, sizeof(get_bytes))) {
        return false;
    }
    unsigned char color_bytes[] = {0x51, 0x83, 0x3d, 0, 0, 0, 0, 0, 0x74, 0x33, 0x83, 0x3d, 0, 0, 0, 0};
    address = static_cast<uint32_t>(base + 0x1287978);
    memcpy(color_bytes + 3, &address, 4);
    address = static_cast<uint32_t>(base + 0x116b0cc);
    memcpy(color_bytes + 12, &address, 4);
    if (!prefix(0x76f240, color_bytes, sizeof(color_bytes)) ||
        !slot(App::IClassManager::Get(), 0x103d9bc, 0x20, 0x5203a0)) {
        return false;
    }
    unsigned char stage_writer[] = {0x81, 0xec, 0x14, 0x0a, 0, 0, 0x68, 0x26, 0x0d, 0xa8, 0x01, 0x68, 0, 0, 0, 0};
    address = static_cast<uint32_t>(base + 0x11829b8);
    memcpy(stage_writer + 12, &address, 4);
    return prefix(0x92e300, stage_writer, sizeof(stage_writer));
}

void bind_originals() {
    factory_original = reinterpret_cast<Factory>(base + 0x5203a0);
    noun_read_original = reinterpret_cast<Read>(base + 0x718540);
    player_read_original = reinterpret_cast<Read>(base + 0x87c960);
    animal_read_original = reinterpret_cast<Read>(base + 0x8030e0);
    words_original = reinterpret_cast<ReadWords>(base + 0x53a6f0);
    color_original = reinterpret_cast<ColorSetter>(base + 0x76f240);
    theme_original = reinterpret_cast<Get>(base + 0x27cb00);
    register_original = reinterpret_cast<Register>(base + 0x875380);
    herd_original = reinterpret_cast<HerdReader>(base + 0x720490);
}
} // namespace

bool initialize_native_player_transfer_probe(uintptr_t image_base, uintptr_t image_end, DWORD engine_thread,
                                             NativePlayerTransferProbeEvent callback) {
    if (initialized || GetCurrentThreadId() != engine_thread || image_end <= image_base) {
        return false;
    }
    base = image_base;
    end = image_end;
    thread = engine_thread;
    sink = callback;
    if (!code_prefixes_pinned() || !relocated_prefixes_pinned()) {
        return false;
    }
    bind_originals();
    initialized = true;
    event("native_player_import_bindings_checked");
    return true;
}

namespace {
// The original display strategy's counter of rewards and herd evolutions still to be shown.
bool native_progression_pending(App::cCreatureModeStrategy* strategy) {
    const auto counter = reinterpret_cast<const unsigned char*>(strategy->mpDisplayStrategy.get()) + 0x128;
    // Same original display-counter layout used by native_awards_checkpoint_idle.
    // Reject rather than dropping a pending reward or herd-evolution request.
    return strategy->field_48 || *reinterpret_cast<const uint32_t*>(counter) != 0 ||
           *reinterpret_cast<const uintptr_t*>(counter + 0xc) != *reinterpret_cast<const uintptr_t*>(counter + 0x1c);
}

void record_primary_stage_export(uint64_t request, uint64_t epoch, Player* primary, Animal* avatar, Stage* stage,
                                 bool written, Simulator::ISerializerWriteStream* stream) {
    char fields[1000]{};
    sprintf_s(fields,
              ",\"request\":%llu,\"epoch\":%llu,\"player_native_id\":%u,\"avatar_native_id\":%u,"
              "\"unique_game_id\":%u,\"serialization_version\":%u,\"brain\":%d,\"normalizing_scale_bits\":%u,"
              "\"flaps\":%d,\"ability_mode\":%u,\"dna_bits\":%u,\"deprecated_abilities_low\":%u,"
              "\"deprecated_abilities_high\":%u,\"learned_0\":%u,\"learned_1\":%u,\"learned_2\":%u,"
              "\"source_stage_unchanged\":true,\"source_counter_idle\":true,\"source_world_pending\":false,"
              "\"original_write_return\":%s,\"stream_good\":%s,"
              "\"experimental\":true,\"transfer_qualified\":false",
              request, epoch, primary->mID, avatar->mID, primary->mUniqueGameID, accepted_serialization_version,
              stage->mCurrentBrainLevel, bits(stage->mAvatarNormalizingScale), stage->mNumFlapsAllowed,
              static_cast<uint32_t>(stage->mAbilityMode), bits(stage->mEvolutionPoints),
              static_cast<uint32_t>(stage->DEPRECATED_mLearnedAbilities),
              static_cast<uint32_t>(stage->DEPRECATED_mLearnedAbilities >> 32),
              static_cast<uint32_t>(stage->mLearnedAbilities[0]), static_cast<uint32_t>(stage->mLearnedAbilities[1]),
              static_cast<uint32_t>(stage->mLearnedAbilities[2]), written ? "true" : "false",
              stream->IsGood() ? "true" : "false");
    if (sink) {
        sink("native_primary_stage_export", fields);
    }
}

// Writes the primary's persistent stage fields through the original writer. The source must be the loaded
// Creature scene of a supervised worker, with one valid primary and no progression waiting to be shown; a
// transfer under a held pause may also have a second player and a network configuration.
bool write_primary_stage_impl(uint64_t request, Simulator::ISerializerWriteStream* stream, bool held_transfer) {
    request_id = request;
    wchar_t marker[128]{};
    if (!on_thread() || !request || !stream || !stream->IsGood() || scope || attached || native_replica_is_client() ||
        native_persistence_snapshot().state != NativePersistenceState::loaded || Simulator::IsLoadingGameMode() ||
        Simulator::GetGameModeID() != kGameCreature ||
        (!held_transfer && GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", marker, _countof(marker))) ||
        !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", marker, _countof(marker)) ||
        !GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", marker, _countof(marker))) {
        return false;
    }
    const auto epoch = native_actor_worker_status().epoch;
    auto manager = Manager::Get();
    auto primary = manager ? manager->GetPlayer() : nullptr;
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    auto stage = Stage::Get();
    auto strategy = App::cCreatureModeStrategy::Get();
    unsigned players = 0;
    const bool has_secondary = native_second_player(epoch) != nullptr;
    if (!epoch || (!held_transfer && has_secondary) || !primary || !avatar || !stage ||
        reinterpret_cast<uintptr_t>(stage) != base + 0x129e370 || duplicate_ids(manager, players) ||
        players != (has_secondary ? 2u : 1u) || !validate_native_player_checkpoint(primary, primary->mID) ||
        avatar->mbDead || avatar->mbMarkedForDeletion || !valid_stage(persistent_stage(*stage)) || !strategy ||
        !strategy->mpDisplayStrategy) {
        return false;
    }
    if (native_progression_pending(strategy)) {
        event("native_primary_stage_export_rejected", "pending_native_progression");
        return false;
    }
    std::array<unsigned char, sizeof(Stage)> before{};
    memcpy(before.data(), stage, before.size());
    // Original D2E300 constructs ClassSerializer with the pinned seven-field
    // descriptor table 015829B8 and forwards its boolean write result.
    const bool written = reinterpret_cast<bool(__thiscall*)(Stage*, Stream*)>(base + 0x92e300)(stage, stream);
    const bool unchanged = !memcmp(before.data(), stage, before.size());
    if (!unchanged) {
        partial_failure("source_stage_changed_during_write");
    }
    const bool complete = written && stream->IsGood();
    record_primary_stage_export(request, epoch, primary, avatar, stage, written, stream);
    return complete;
}

void record_selected_stage_export(uint64_t request, uint64_t epoch, uint32_t player_id, uint32_t avatar_id,
                                  uint32_t unique_game_id, const std::array<uint32_t, 10>& persistent) {
    char fields[1100]{};
    sprintf_s(
        fields,
        ",\"request\":%llu,\"epoch\":%llu,\"native_slot\":2,\"player_native_id\":%u,\"avatar_native_id\":%u,"
        "\"unique_game_id\":%u,\"serialization_version\":%u,\"brain\":%u,\"normalizing_scale_bits\":%u,"
        "\"flaps\":%u,\"ability_mode\":%u,\"dna_bits\":%u,\"deprecated_abilities_low\":%u,"
        "\"deprecated_abilities_high\":%u,\"learned_0\":%u,\"learned_1\":%u,\"learned_2\":%u,"
        "\"source_stage_unchanged\":true,\"destination_primary_stage_unchanged\":true,\"source_counter_idle\":true,"
        "\"source_world_pending\":false,\"original_write_return\":true,\"stream_good\":true,"
        "\"experimental\":true,\"transfer_qualified\":false",
        request, epoch, player_id, avatar_id, unique_game_id, accepted_serialization_version, persistent[0],
        persistent[1], persistent[2], persistent[3], persistent[4], persistent[5], persistent[6], persistent[7],
        persistent[8], persistent[9]);
    if (sink) {
        sink("native_selected_stage_export", fields);
    }
}
} // namespace

bool write_native_primary_transfer_stage(uint64_t request, Simulator::ISerializerWriteStream* stream) {
    return write_primary_stage_impl(request, stream, false);
}

bool write_native_selected_transfer_stage(uint64_t request, uint64_t epoch, uint64_t pause_request,
                                          uint32_t native_slot, uint32_t player_id, uint32_t avatar_id,
                                          Simulator::ISerializerWriteStream* stream) {
    const auto pause = native_quiescence_probe_status(epoch);
    if (!on_thread() || !pause_request || !pause.ready || !pause.owns_pause || pause.request != pause_request) {
        return false;
    }
    if (native_slot == 1) {
        auto manager = Manager::Get();
        auto player = manager ? manager->GetPlayer() : nullptr;
        auto avatar = manager ? manager->GetAvatar() : nullptr;
        if (!player || !avatar || player->mID != player_id || avatar->mID != avatar_id) {
            return false;
        }
        return write_primary_stage_impl(request, stream, true);
    }
    request_id = request;
    wchar_t marker[128]{};
    if (!on_thread() || !request || native_slot != 2 || !stream || scope || attached || native_replica_is_client() ||
        native_actor_worker_status().epoch != epoch ||
        !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", marker, _countof(marker))) {
        return false;
    }
    auto player = native_second_player(epoch);
    uint32_t selected_player = 0, selected_avatar = 0;
    if (!player || player->mID != player_id ||
        !native_actor_transfer_selection(epoch, native_slot, player->mUniqueGameID, selected_player, selected_avatar) ||
        selected_player != player_id || selected_avatar != avatar_id) {
        return false;
    }
    NativeAwardCheckpoint observed;
    if (!write_native_secondary_award_stage(epoch, player_id, avatar_id, stream, observed)) {
        return false;
    }
    const auto& words = observed.words;
    const std::array<uint32_t, 10> persistent = {words[0],  words[1],  words[2],  words[9],  words[10],
                                                 words[12], words[13], words[14], words[15], words[16]};
    if (!valid_stage(persistent)) {
        return false;
    }
    record_selected_stage_export(request, epoch, player_id, avatar_id, player->mUniqueGameID, persistent);
    return true;
}

namespace {
// The lower-case hexadecimal SHA-256 that eight 32-bit words name, each word four digest bytes, least
// significant first. False for a word over 32 bits or an all-zero digest.
bool digest_from_words(const std::array<uint64_t, 10>& sha_words, Sha256& expected) {
    constexpr char hex[] = "0123456789abcdef";
    bool nonzero = false;
    for (size_t i = 0; i < 8; ++i) {
        if (sha_words[i] > UINT32_MAX) {
            return false;
        }
        nonzero = nonzero || sha_words[i] != 0;
        for (size_t j = 0; j < 4; ++j) {
            const auto byte = static_cast<uint8_t>(sha_words[i] >> (j * 8));
            expected[(i * 4 + j) * 2] = hex[byte >> 4];
            expected[(i * 4 + j) * 2 + 1] = hex[byte & 15];
        }
    }
    return nonzero;
}

// The path of one file in the diagnostics directory. Every component must exist, and none may be relative or a
// reparse point. Returns the reason the path is refused, or null.
const char* diagnostics_file(const wchar_t* leaf, std::filesystem::path& path) {
    wchar_t directory[32768]{};
    const auto length = GetEnvironmentVariableW(L"SPOREMP_DIAGNOSTICS_DIR", directory, _countof(directory));
    if (!length || length >= _countof(directory) || !absolute_local_path(directory)) {
        return "diagnostics_directory";
    }
    path = std::filesystem::path(directory) / leaf;
    std::filesystem::path at;
    for (const auto& part : path) {
        if (part == L"." || part == L"..") {
            return "unsafe_import_path";
        }
        at /= part;
        const auto attributes = GetFileAttributesW(at.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            return "unsafe_import_path";
        }
    }
    return nullptr;
}

// A file opened for reading without write sharing, held until its owner returns.
struct HeldFile {
    HANDLE handle = INVALID_HANDLE_VALUE;

    ~HeldFile() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

// Opens the sidecar into the caller's hold and reads it whole: a plain single-link file of a bounded size and
// of the expected SHA-256.
bool read_fixture_file(const std::filesystem::path& path, const Sha256& expected, HeldFile& file,
                       std::vector<uint8_t>& bytes) {
    file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    Sha256 actual{};
    if (file.handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.handle, &info) ||
        info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) || info.nNumberOfLinks != 1 ||
        info.nFileSizeHigh || info.nFileSizeLow < 160 || info.nFileSizeLow > 200 + 1024 * 20 ||
        !sha256_file(path.c_str(), actual) || actual != expected) {
        return false;
    }
    bytes.resize(info.nFileSizeLow);
    DWORD amount = 0;
    return ReadFile(file.handle, bytes.data(), info.nFileSizeLow, &amount, nullptr) && amount == info.nFileSizeLow;
}

// Reads the sidecar's 32-bit words in order.
struct WordReader {
    const std::vector<uint8_t>& bytes;
    size_t position = 0;

    uint32_t word() {
        uint32_t value = 0;
        memcpy(&value, bytes.data() + position, 4);
        position += 4;
        return value;
    }
};

// The fixed fields that follow the schema word, and the stage words of schema 3.
bool decode_fixture_fields(WordReader& reader, NativePlayerTransferFixture& decoded) {
    memcpy(decoded.sha256.data(), reader.bytes.data() + reader.position, 64);
    reader.position += 64;
    decoded.bytes = reader.word();
    decoded.serialization_version = reader.word();
    decoded.source_player = reader.word();
    decoded.source_avatar = reader.word();
    decoded.source_unique_game = reader.word();
    decoded.source_home = reader.word();
    decoded.species_instance = reader.word();
    decoded.species_type = reader.word();
    decoded.species_group = reader.word();
    decoded.age = reader.word();
    decoded.animal_archetype = reader.word();
    decoded.herd_archetype = reader.word();
    decoded.herd_archetype_group = reader.word();
    const auto generation = reader.word();
    memcpy(&decoded.herd_generation, &generation, 4);
    decoded.herd_personality = reader.word();
    for (auto& value : decoded.progression_bits) {
        value = reader.word();
    }
    decoded.status_count = reader.word();
    decoded.unlocked_count = reader.word();
    decoded.unlock_points = reader.word();
    if (decoded.schema == 3) {
        for (auto& value : decoded.stage_persistent) {
            value = reader.word();
        }
        if (!valid_stage(decoded.stage_persistent)) {
            return false;
        }
    }
    return true;
}

// The item statuses, which must be sorted by group then instance without repeats, and the unlocked items. The
// file must end exactly after them.
bool decode_fixture_collection(WordReader& reader, NativePlayerTransferFixture& decoded) {
    if (decoded.status_count > 1024 || decoded.unlocked_count > 1024 ||
        reader.bytes.size() !=
            reader.position + size_t(decoded.status_count) * 12 + size_t(decoded.unlocked_count) * 8) {
        return false;
    }
    for (uint32_t i = 0; i < decoded.status_count; ++i) {
        for (auto& value : decoded.statuses[i]) {
            value = reader.word();
        }
        if (decoded.statuses[i][2] > 255) {
            return false;
        }
        if (i) {
            const auto& before = decoded.statuses[i - 1];
            const auto& current = decoded.statuses[i];
            if (current[1] < before[1] || (current[1] == before[1] && current[0] <= before[0])) {
                return false;
            }
        }
    }
    for (uint32_t i = 0; i < decoded.unlocked_count; ++i) {
        for (auto& value : decoded.unlocked[i]) {
            value = reader.word();
        }
    }
    return true;
}

bool decode_fixture(const std::vector<uint8_t>& bytes, NativePlayerTransferFixture& decoded) {
    WordReader reader{bytes};
    if (reader.word() != 0x4630314d) {
        return false;
    }
    const auto schema = reader.word();
    if ((schema != 2 && schema != 3) || (schema == 3 && bytes.size() < 200)) {
        return false;
    }
    decoded.schema = schema;
    return decode_fixture_fields(reader, decoded) && decode_fixture_collection(reader, decoded);
}
} // namespace

bool load_native_player_transfer_fixture(const std::array<uint64_t, 10>& sha_words,
                                         NativePlayerTransferFixture& approved) {
    if (!on_thread() || sha_words[8] || sha_words[9]) {
        return false;
    }
    Sha256 expected{};
    if (!digest_from_words(sha_words, expected)) {
        return false;
    }
    std::filesystem::path path;
    if (diagnostics_file(L"player-import.expected", path)) {
        return false;
    }
    HeldFile file;
    std::vector<uint8_t> bytes;
    if (!read_fixture_file(path, expected, file, bytes)) {
        return false;
    }
    NativePlayerTransferFixture decoded;
    if (!decode_fixture(bytes, decoded)) {
        return false;
    }
    approved = decoded;
    return true;
}

namespace {
// What one import was asked to do. restore_primary overlays the existing primary, restore_secondary an existing
// second player; with neither, a second player is created.
struct TransferOrder {
    uint64_t request;
    uint64_t epoch;
    const NativePlayerTransferFixture& approved;
    const NativePlayerTransferAdmission* admission;
    bool restore_primary;
    const RetainedImport* restore_secondary;
    uint32_t canonical_home;
    bool restore_existing;
};

// An authenticated transfer must hold the measured pause it names, in this epoch, and the actors must be the
// selected ones.
bool admission_refused(const TransferOrder& order) {
    const auto& approved = order.approved;
    const auto admission = order.admission;
    const auto restore_secondary = order.restore_secondary;
    const auto epoch = order.epoch;
    const auto canonical_home = order.canonical_home;
    const auto pause = native_quiescence_probe_status(epoch);
    uint32_t selected_player = 0, selected_avatar = 0, selected_second = 0, selected_animal = 0;
    const bool actors_ready =
        restore_secondary
            ? (restore_secondary->epoch == epoch && canonical_home == approved.source_home && canonical_home &&
               native_actor_transfer_selection(epoch, 1, admission->primary_unique_game, selected_player,
                                               selected_avatar) &&
               selected_player == admission->primary_player && selected_avatar == admission->primary_avatar &&
               native_actor_transfer_selection(epoch, 2, approved.source_unique_game, selected_second,
                                               selected_animal) &&
               selected_second == restore_secondary->player && selected_animal == restore_secondary->avatar)
            : native_actor_transfer_import_ready(epoch, admission->primary_player, admission->primary_avatar,
                                                 admission->primary_unique_game);
    return approved.schema != 3 || !admission->canonical_player || admission->epoch != epoch ||
           !admission->pause_request || pause.request != admission->pause_request || !pause.ready ||
           !pause.owns_pause || !actors_ready;
}

// The values of an approved fixture that the import relies on. Returns the reason they are refused, or null.
const char* fixture_refusal(const NativePlayerTransferFixture& approved) {
    if ((approved.schema != 2 && approved.schema != 3) ||
        (approved.schema == 3 && !valid_stage(approved.stage_persistent)) || approved.sha256[64] ||
        strlen(approved.sha256.data()) != 64 || !approved.bytes || approved.bytes > 1024u * 1024u ||
        approved.serialization_version != accepted_serialization_version || !approved.source_unique_game ||
        !approved.species_instance || approved.age > 4 || !approved.herd_archetype || approved.herd_generation < 0 ||
        approved.herd_generation > 32 ||
        approved.herd_personality > static_cast<uint32_t>(Simulator::CreaturePersonality::Scenario) ||
        approved.status_count > 1024 || approved.unlocked_count > 1024) {
        return "unapproved_fixture_metadata";
    }
    for (size_t i = 0; i < 64; ++i) {
        if (!((approved.sha256[i] >= '0' && approved.sha256[i] <= '9') ||
              (approved.sha256[i] >= 'a' && approved.sha256[i] <= 'f'))) {
            return "invalid_artifact_digest";
        }
    }
    return nullptr;
}

// Everything that is checked before a file is opened: the worker, the admission, the shape of the destination
// scene and the fixture. Returns the reason the import is refused, or null.
const char* order_refusal(const TransferOrder& order) {
    wchar_t marker[128]{};
    if (!on_thread() || used || attached || !order.request || !order.epoch || native_replica_is_client() ||
        (!order.admission && GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", marker, _countof(marker))) ||
        !GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", marker, _countof(marker)) ||
        !GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", marker, _countof(marker))) {
        return "not_disposable_disconnected_worker";
    }
    if (order.admission) {
        if (admission_refused(order)) {
            return "transfer_admission_not_live_and_quiescent";
        }
    } else if (order.restore_existing) {
        return "existing_restore_requires_held_pause";
    }
    if (native_persistence_snapshot().state != NativePersistenceState::loaded || Simulator::IsLoadingGameMode() ||
        Simulator::GetGameModeID() != kGameCreature || native_actor_worker_status().epoch != order.epoch ||
        (bool(native_second_player(order.epoch)) != bool(order.restore_secondary))) {
        return "destination_native_player_shape";
    }
    return fixture_refusal(order.approved);
}

// Opens the artifact into the caller's hold: a plain single-link file of exactly the approved size and digest.
// Returns the reason it is refused, or null.
const char* open_artifact(const std::filesystem::path& path, const NativePlayerTransferFixture& approved,
                          HeldFile& file) {
    file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (file.handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.handle, &info) ||
        info.nNumberOfLinks != 1 || info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY) ||
        info.nFileSizeHigh || info.nFileSizeLow != approved.bytes) {
        return "artifact_not_closed_exact_file";
    }
    Sha256 digest{};
    if (!sha256_file(path.c_str(), digest) || digest != approved.sha256) {
        return "artifact_digest_mismatch";
    }
    return nullptr;
}

// The artifact is player-import.db of the diagnostics directory and no other file. path and file receive it.
// Returns the reason it is refused, or null.
const char* artifact_refusal(const NativePlayerTransferFixture& approved, std::filesystem::path& path, HeldFile& file) {
    if (const auto reason = diagnostics_file(L"player-import.db", path)) {
        return reason;
    }
    return open_artifact(path, approved, file);
}

// The destination scene as it stands before any original reader runs. The values of the primary are kept to
// prove afterwards that the import left it, the stage and the identity colour alone.
struct Destination {
    Manager* manager = nullptr;
    Player* first = nullptr;
    Animal* avatar = nullptr;
    // The player and the avatar that the readers fill when existing ones are restored.
    Player* existing_player = nullptr;
    Animal* existing_avatar = nullptr;
    std::set<uint32_t> expected_native_ids;
    Collection primary_collection;
    std::array<uint32_t, 4> primary_progress{};
    uint32_t primary_id = 0, primary_game = 0, avatar_id = 0, dna_bits = 0, cursor = 0;
    std::array<unsigned char, sizeof(Stage)> primary_stage{};
    Math::ColorRGB color{};
};

// The second player that restore_secondary names must be the retained one: alive, alone in its own herd, and
// carrying the source identity.
bool existing_secondary_refused(const TransferOrder& order, const Destination& destination) {
    const auto& approved = order.approved;
    const auto restore_secondary = order.restore_secondary;
    const auto first = destination.first;
    const auto avatar = destination.avatar;
    const auto existing_player = destination.existing_player;
    const auto existing_avatar = destination.existing_avatar;
    return !existing_player || existing_player->mID != restore_secondary->player || existing_player == first ||
           existing_player->mUniqueGameID != approved.source_unique_game || !existing_avatar ||
           existing_avatar == avatar || existing_avatar->mbDead || existing_avatar->mbMarkedForDeletion ||
           existing_avatar->mbIsDestroyed || (existing_avatar->mGeneralFlags & 0x200) || !existing_avatar->mHerd ||
           existing_avatar->mHerd->mID != restore_secondary->herd || existing_avatar->mHerd == avatar->mHerd ||
           existing_avatar->mHerd->mHerd.size() != 1 ||
           existing_avatar->mHerd->mHerd.front().get() != existing_avatar || existing_avatar->mHerd->mpNest ||
           !existing_avatar->mHerd->mEggs.empty() || existing_avatar->mHerd->mOwnedByAvatar ||
           existing_avatar->mHerd->mpHerdMom || existing_avatar->mHerd->mpEggLayer ||
           !validate_native_player_checkpoint(existing_player, existing_player->mID);
}

// Finds the primary and, for a restore, the existing player and avatar, and checks the identities the order
// names. Returns the reason the destination is refused, or null.
const char* find_destination(const TransferOrder& order, Destination& destination) {
    const auto& approved = order.approved;
    const auto restore_secondary = order.restore_secondary;
    destination.manager = Manager::Get();
    const auto manager = destination.manager;
    destination.first = manager ? manager->GetPlayer() : nullptr;
    destination.avatar = manager ? manager->GetAvatar() : nullptr;
    const auto first = destination.first;
    const auto avatar = destination.avatar;
    unsigned players = 0;
    if (!first || !avatar || duplicate_ids(manager, players) || players != (restore_secondary ? 2u : 1u) ||
        !avatar->mHerd || !avatar->field_E84 || avatar->mbDead || avatar->mbMarkedForDeletion ||
        !validate_native_player_checkpoint(first, first->mID)) {
        return "primary_census";
    }
    destination.existing_player = restore_secondary ? native_second_player(order.epoch) : first;
    destination.existing_avatar = avatar;
    if (restore_secondary) {
        destination.existing_avatar = nullptr;
        for (auto& noun : manager->mNouns) {
            if (noun.mID == restore_secondary->avatar) {
                destination.existing_avatar = object_cast<Animal>(&noun);
            }
        }
        if (existing_secondary_refused(order, destination)) {
            return "existing_secondary_census";
        }
    }
    for (auto& noun : manager->mNouns) {
        destination.expected_native_ids.insert(noun.mID);
    }
    if (order.restore_primary) {
        auto home = Simulator::GetPlayerHomePlanet();
        if (approved.schema != 3 || first->mUniqueGameID != approved.source_unique_game || !home ||
            home->mKey.internalValue != approved.source_home || avatar->mAge != static_cast<int>(approved.age) ||
            avatar->mArchetype != static_cast<int>(approved.animal_archetype)) {
            return "return_primary_identity_or_native_shape_mismatch";
        }
    } else if (first->mUniqueGameID == approved.source_unique_game) {
        return "source_identity_matches_destination";
    }
    return nullptr;
}

// Keeps the primary's collection, progress, identifiers, stage, and the identity colour and its cursor, as they
// are before the import. Returns the reason they cannot be read, or null.
const char* remember_primary(Destination& destination) {
    const auto first = destination.first;
    if (!collection(first->mpCRGItems.get(), destination.primary_collection)) {
        return "primary_collection";
    }
    destination.primary_progress = progress(first);
    destination.primary_id = first->mID;
    destination.primary_game = first->mUniqueGameID;
    destination.avatar_id = destination.avatar->mID;
    destination.dna_bits = bits(Simulator::cCreatureGameData::GetEvolutionPoints());
    memcpy(destination.primary_stage.data(), Stage::Get(), destination.primary_stage.size());
    destination.color = Simulator::GetCachedColorFromId(0x053dbcf1);
    destination.cursor = *reinterpret_cast<const uint32_t*>(base + 0x1287968);
    return nullptr;
}

const char* destination_refusal(const TransferOrder& order, Destination& destination) {
    if (const auto reason = find_destination(order, destination)) {
        return reason;
    }
    return remember_primary(destination);
}

// The six words that open the root record: the format mark, the schema and the source identities.
bool root_words_match(const uint32_t (&words)[6], const NativePlayerTransferFixture& approved) {
    return words[0] == 0x5030314d && words[1] == approved.schema && words[2] == approved.source_player &&
           words[3] == approved.source_unique_game && words[4] == approved.source_avatar &&
           words[5] == approved.source_home;
}

bool root_header_matches(Simulator::SerializerDatabase* database, const NativePlayerTransferFixture& approved) {
    ReadStream* header_stream = nullptr;
    uint32_t header_words[6]{};
    bool header_ok = database->OpenReadStream(ResourceKey(1, 0x53504d47, 0x53504d50), &header_stream) && header_stream;
    if (header_ok) {
        header_ok = IO::ReadUInt32(header_stream->GetRecord()->GetStream(), header_words, 6) &&
                    root_words_match(header_words, approved) && header_stream->IsGood();
    }
    if (header_stream) {
        header_ok = database->CloseReadStream(header_stream) && header_ok;
        header_stream->Release();
    }
    return header_ok;
}

// What the artifact holds apart from the three objects: the source stage of schema 3, and the species.
struct Source {
    Stage restored_stage{};
    ResourceKey species_key;
    Simulator::cSpeciesProfile* species = nullptr;
};

// An existing avatar is overlaid only when it already has the source's species, stage and shape, and an
// existing second player only when its herd already has the source's profile: neither is rebuilt here. Returns
// the reason the existing objects are refused, or null.
const char* existing_refusal(const TransferOrder& order, const Destination& destination, const Source& source) {
    const auto& approved = order.approved;
    const auto existing_avatar = destination.existing_avatar;
    if (order.restore_existing &&
        (existing_avatar->mpSpeciesProfile != source.species || existing_avatar->mSpeciesKey != source.species_key ||
         existing_avatar->GetCurrentBrainLevel() != static_cast<int>(approved.stage_persistent[0]) ||
         existing_avatar->mHerd->mOwnerSpeciesKey != source.species_key ||
         existing_avatar->mAge != static_cast<int>(approved.age) ||
         existing_avatar->mArchetype != static_cast<int>(approved.animal_archetype))) {
        return "existing_species_or_stage_requires_native_rebuild";
    }
    if (order.restore_secondary &&
        (existing_avatar->mpSpeciesProfile != destination.avatar->mpSpeciesProfile ||
         static_cast<uint32_t>(existing_avatar->mHerd->mArchetype) != approved.herd_archetype ||
         static_cast<uint32_t>(existing_avatar->mHerd->mArchetypeGroup) != approved.herd_archetype_group ||
         existing_avatar->mHerd->mGeneration != approved.herd_generation ||
         static_cast<uint32_t>(existing_avatar->mHerd->mCreaturePersonality) != approved.herd_personality)) {
        return "existing_secondary_herd_or_award_profile";
    }
    return nullptr;
}

// Opens the artifact as an original serializer database and reads everything but the objects: the exact key
// list, the root header, the stage record of schema 3 and the species. No original object reader runs here.
// Returns the reason the source is refused, or null; the caller closes the database either way.
const char* read_source(const TransferOrder& order, const Destination& destination,
                        Simulator::SerializerDatabase* database, Source& source) {
    const auto& approved = order.approved;
    if (!database->Initialize() || !database->OpenAsSerializer(true, false) || !exact_keys(database, approved.schema)) {
        return "native_database_envelope";
    }
    if (!root_header_matches(database, approved)) {
        return "native_root_header";
    }
    if (approved.schema == 3 && !read_stage(database, approved, source.restored_stage)) {
        return "native_source_stage_record";
    }
    // All graph classes/fields were independently validated for this digest.
    // No original object reader has run before this point.
    source.species_key = ResourceKey(approved.species_instance, approved.species_type, approved.species_group);
    source.species = Editors::cSpeciesManager::Get()->GetSpeciesProfile(source.species_key);
    if (!source.species) {
        return "source_species_unavailable";
    }
    return existing_refusal(order, destination, source);
}

// The native objects the original readers fill, borrowed from the pointers import_player_transfer_impl holds.
struct Filled {
    Player* second = nullptr;
    Items* items = nullptr;
    Herd* herd = nullptr;
    Animal* animal = nullptr;
    size_t retained_herd_size = 0;
};

// The player the readers fill: the existing one of a restore, or a second player created here. From this point
// a failure is partial and ends the process.
Player* destination_player(const TransferOrder& order, const Destination& destination) {
    used = true;
    if (!order.restore_existing && !create_native_player_context(order.epoch)) {
        partial_failure("native_player_factory");
    }
    auto second = order.restore_existing ? destination.existing_player : native_second_player(order.epoch);
    if (!second || !second->mpCRGItems) {
        partial_failure("native_second_context");
    }
    return second;
}

// A herd created for the import must be empty and of the approved personality; it then takes the source's
// archetype, generation and species.
void prepare_herd(const TransferOrder& order, Herd* herd, const ResourceKey& species_key) {
    const auto& approved = order.approved;
    if (!herd ||
        (!order.restore_existing &&
         (!herd->mHerd.empty() || static_cast<uint32_t>(herd->mCreaturePersonality) != approved.herd_personality))) {
        partial_failure("native_herd_factory");
    }
    if (!order.restore_existing) {
        herd->mArchetype = approved.herd_archetype;
        herd->mArchetypeGroup = approved.herd_archetype_group;
        herd->mGeneration = approved.herd_generation;
        herd->mOwnerSpeciesKey = species_key;
    }
}

void require_animal(const TransferOrder& order, Animal* animal, Herd* herd) {
    if (!animal || !animal->field_E84 || animal->mHerd.get() != herd ||
        (!order.restore_existing && (herd->mHerd.size() != 1 || herd->mHerd.front().get() != animal)) ||
        !slot(animal, 0x106a080, 0x14, 0x8030e0)) {
        partial_failure("native_animal_factory");
    }
}

// Runs the original object readers over the database with the import hooks attached, so that they fill the
// objects of local instead of creating their own. Every hook must have run exactly as many times as one
// player, one animal and one collection call for.
void run_original_readers(Simulator::SerializerDatabase* database, uint32_t serialization_version, Scope& local) {
    const auto status = change_hooks(hooks, _countof(hooks), true);
    if (status != NO_ERROR) {
        partial_failure("import_hook_attach");
    }
    attached = true;
    scope = &local;
    const bool loaded = database->LoadClassObjects(serialization_version);
    scope = nullptr;
    const auto detached = change_hooks(hooks, _countof(hooks), false);
    if (detached != NO_ERROR) {
        partial_failure("import_hook_detach");
    }
    attached = false;
    if (!loaded || local.failed || local.factories[0] != 1 || local.factories[1] != 1 || local.factories[2] != 1 ||
        local.noun_reads != 2 || local.player_reads != 1 || local.animal_reads != 1 || local.herd_reads != 1 ||
        local.color_headers != 1 || local.color_writes != 1 || local.themes != 1 || local.registrations != 1) {
        partial_failure("native_graph_reader_census");
    }
}

// Reads the root record again, now with its two object pointers, which the caller compares with the objects
// that were filled. The caller owns the two references.
bool read_roots(Simulator::SerializerDatabase* database, const NativePlayerTransferFixture& approved,
                ObjectPtr& player_root, ObjectPtr& animal_root) {
    ReadStream* stream = nullptr;
    uint32_t words[6]{};
    bool roots = database->OpenReadStream(ResourceKey(1, 0x53504d47, 0x53504d50), &stream) && stream;
    if (roots) {
        stream->SetSerializationVersion(approved.serialization_version);
        roots = IO::ReadUInt32(stream->GetRecord()->GetStream(), words, 6) && root_words_match(words, approved) &&
                stream->ReadObjectPointer(0x02c216ed, player_root, false) &&
                stream->ReadObjectPointer(Animal::TYPE, animal_root, false) && stream->IsGood();
    }
    if (stream) {
        roots = database->CloseReadStream(stream) && roots;
        stream->Release();
    }
    return roots;
}

// After the readers: the filled objects must carry the source's values in the destination's own identifiers,
// no noun may have appeared or gone, the collection must be the approved one, and the primary, the stage and
// the identity colour must be what they were. imported receives the collection that was read.
void verify_import(const TransferOrder& order, const Destination& destination, const Source& source,
                   const Filled& filled, const Scope& local, Collection& imported) {
    const auto& approved = order.approved;
    const bool restore_primary = order.restore_primary;
    const auto manager = destination.manager;
    const auto first = destination.first;
    const auto avatar = destination.avatar;
    const auto second = filled.second;
    const auto animal = filled.animal;
    const auto herd = filled.herd;
    unsigned players = 0;
    if (animal->mAge != static_cast<int>(approved.age) || animal->mSpeciesKey != source.species_key ||
        animal->mArchetype != static_cast<int>(approved.animal_archetype) ||
        animal->mpSpeciesProfile != source.species || !animal->field_E84 || !animal->GetModel() ||
        animal->mHerd.get() != herd ||
        (!restore_primary && static_cast<uint32_t>(herd->mCreaturePersonality) != approved.herd_personality) ||
        (order.restore_existing && (herd->mHerd.size() != filled.retained_herd_size ||
                                    static_cast<uint32_t>(animal->mGeneralFlags & 0x200) != local.local_avatar_flag)) ||
        animal->mID != local.animal_id || second->mID != local.player_id ||
        second->mUniqueGameID != approved.source_unique_game || second->mpCRGItems.get() != filled.items ||
        progress(second) != approved.progression_bits || duplicate_ids(manager, players) ||
        players != (restore_primary ? 1u : 2u)) {
        partial_failure("imported_native_context");
    }
    std::set<uint32_t> native_ids_after;
    for (auto& noun : manager->mNouns) {
        native_ids_after.insert(noun.mID);
    }
    if (native_ids_after != destination.expected_native_ids) {
        partial_failure("native_noun_census_changed_unexpectedly");
    }
    Collection primary_after;
    if (!collection(filled.items, imported) || imported.statuses.size() != approved.status_count ||
        imported.unlocked.size() != approved.unlocked_count || imported.points != approved.unlock_points ||
        !std::equal(imported.statuses.begin(), imported.statuses.end(), approved.statuses.begin()) ||
        !std::equal(imported.unlocked.begin(), imported.unlocked.end(), approved.unlocked.begin()) ||
        (!restore_primary && (!collection(first->mpCRGItems.get(), primary_after) ||
                              primary_after.statuses != destination.primary_collection.statuses ||
                              primary_after.unlocked != destination.primary_collection.unlocked ||
                              primary_after.points != destination.primary_collection.points))) {
        partial_failure("collection_identity");
    }
    const auto color_after = Simulator::GetCachedColorFromId(0x053dbcf1);
    if (manager->GetPlayer() != first || manager->GetAvatar() != avatar || first->mID != destination.primary_id ||
        first->mUniqueGameID != destination.primary_game || avatar->mID != destination.avatar_id ||
        (!restore_primary && progress(first) != destination.primary_progress) ||
        bits(Simulator::cCreatureGameData::GetEvolutionPoints()) != destination.dna_bits ||
        memcmp(destination.primary_stage.data(), Stage::Get(), destination.primary_stage.size()) ||
        *reinterpret_cast<const uint32_t*>(base + 0x1287968) != destination.cursor ||
        memcmp(&destination.color, &color_after, sizeof(destination.color))) {
        partial_failure("destination_primary_or_color_changed");
    }
}

// An existing second player keeps its model, herd and identifiers; only its idle reward context is replaced,
// through the original lifecycle, with the stage that was read.
void finish_secondary_restore(const TransferOrder& order, const Filled& filled,
                              NativePlayerTransferProbeResult& result) {
    const auto& approved = order.approved;
    const auto epoch = order.epoch;
    const auto second = filled.second;
    const auto animal = filled.animal;
    const auto herd = filled.herd;
    uint32_t selected_player = 0, selected_avatar = 0;
    if (!native_actor_transfer_selection(epoch, 2, approved.source_unique_game, selected_player, selected_avatar) ||
        selected_player != second->mID || selected_avatar != animal->mID) {
        partial_failure("existing_secondary_binding");
    }
    // Retain the verified model/herd/IDs. Replace only the idle secondary
    // reward context through its already qualified original lifecycle.
    retire_native_awards();
    NativeAwardCheckpoint stage;
    stage.words = result.stage_words;
    if (!restore_native_actor_awards(stage, epoch) ||
        !native_actor_transfer_selection(epoch, 2, approved.source_unique_game, selected_player, selected_avatar) ||
        selected_player != second->mID || selected_avatar != animal->mID) {
        partial_failure("existing_secondary_stage");
    }
    result.restored_secondary = true;
    retained_import = {epoch, second->mID, animal->mID, herd->mID};
    char fields[800]{};
    sprintf_s(fields,
              ",\"request\":%llu,\"epoch\":%llu,\"canonical_player\":%llu,\"canonical_home\":%u,"
              "\"player_native_id\":%u,\"avatar_native_id\":%u,\"herd_native_id\":%u,\"unique_game_id\":%u,"
              "\"created_nouns\":0,\"reused_factory_classes\":3,\"noun_ids_preserved\":2,\"native_players\":2,"
              "\"stage_applied\":true,\"destination_primary_unchanged\":true,\"global_color_unchanged\":true,"
              "\"bindings_unchanged\":true,\"experimental\":true,\"transfer_qualified\":false",
              order.request, epoch, order.admission->canonical_player, order.canonical_home, second->mID, animal->mID,
              herd->mID, second->mUniqueGameID);
    if (sink) {
        sink("native_player_secondary_restore_return", fields);
    }
}

// The returning primary keeps its model, profile, herd and identifiers and takes the stage that was read.
void finish_primary_restore(const TransferOrder& order, const Filled& filled, const Stage& restored_stage,
                            NativePlayerTransferProbeResult& result) {
    const auto& approved = order.approved;
    const auto admission = order.admission;
    const auto second = filled.second;
    const auto animal = filled.animal;
    const auto herd = filled.herd;
    // Apply exactly the original reader's stage, including its own reset
    // defaults, only after the three native object reads succeeded. The
    // existing primary's native model/profile/herd and identities survive.
    memcpy(Stage::Get(), &restored_stage, sizeof(restored_stage));
    if (persistent_stage(*Stage::Get()) != approved.stage_persistent ||
        !native_actor_transfer_import_ready(order.epoch, admission->primary_player, admission->primary_avatar,
                                            admission->primary_unique_game)) {
        partial_failure("return_primary_stage_or_binding");
    }
    result.restored_primary = true;
    char fields[800]{};
    sprintf_s(fields,
              ",\"request\":%llu,\"epoch\":%llu,\"canonical_player\":%llu,\"player_native_id\":%u,"
              "\"avatar_native_id\":%u,\"herd_native_id\":%u,\"unique_game_id\":%u,\"created_nouns\":0,"
              "\"reused_factory_classes\":3,\"noun_ids_preserved\":2,\"native_players\":1,\"stage_applied\":true,"
              "\"global_color_unchanged\":true,\"primary_binding_unchanged\":true,\"experimental\":true,\"transfer_"
              "qualified\":false",
              order.request, order.epoch, admission->canonical_player, second->mID, animal->mID, herd->mID,
              second->mUniqueGameID);
    if (sink) {
        sink("native_player_primary_restore_return", fields);
    }
}

// A second player was created: its record, and the identifiers a later retirement must name.
void finish_new_import(const TransferOrder& order, const Filled& filled, const Collection& imported,
                       const NativePlayerTransferProbeResult& result) {
    const auto second = filled.second;
    const auto animal = filled.animal;
    const auto herd = filled.herd;
    char fields[800]{};
    sprintf_s(fields,
              ",\"request\":%llu,\"player_native_id\":%u,\"avatar_native_id\":%u,\"herd_native_id\":%u,"
              "\"unique_game_id\":%u,\"item_statuses\":%zu,\"unlocked_items\":%zu,\"factory_classes\":3,\"noun_ids_"
              "remapped\":2,"
              "\"destination_primary_unchanged\":true,\"global_color_unchanged\":true,\"theme_updates_suppressed\":1,"
              "\"duplicate_listener_registration_suppressed\":1,\"schema\":%u,\"stage_restored\":%s,"
              "\"destination_stage_unchanged\":true,\"experimental\":true,\"transfer_qualified\":false",
              order.request, second->mID, animal->mID, herd->mID, second->mUniqueGameID, imported.statuses.size(),
              imported.unlocked.size(), order.approved.schema, result.has_stage ? "true" : "false");
    if (sink) {
        sink("native_player_import_return", fields);
    }
    retained_import = {order.epoch, second->mID, animal->mID, herd->mID};
}

// The result of a verified import, after the last step its kind calls for.
NativePlayerTransferProbeResult conclude_import(const TransferOrder& order, const Filled& filled, const Source& source,
                                                const Collection& imported) {
    NativePlayerTransferProbeResult result{true, filled.second->mID, filled.animal->mID, filled.herd->mID};
    if (order.approved.schema == 3) {
        result.has_stage = true;
        result.stage_words = stage_checkpoint(source.restored_stage);
    }
    if (order.restore_secondary) {
        finish_secondary_restore(order, filled, result);
    } else if (order.restore_primary) {
        finish_primary_restore(order, filled, source.restored_stage, result);
    } else {
        finish_new_import(order, filled, imported, result);
    }
    return result;
}

// What the import hooks need to recognise the objects being filled and to keep their destination values.
Scope reader_scope(const Filled& filled, uint32_t color_cursor) {
    Scope local;
    local.player = filled.second;
    local.animal = filled.animal;
    local.items = filled.items;
    local.herd = filled.herd;
    local.player_id = filled.second->mID;
    local.animal_id = filled.animal->mID;
    local.color_cursor = color_cursor;
    local.local_avatar_flag = filled.animal->mGeneralFlags & 0x200;
    return local;
}

// The three nouns created for the import are the only ones the census may gain.
void expect_created_nouns(const Filled& filled, Destination& destination) {
    if (!destination.expected_native_ids.insert(filled.second->mID).second ||
        !destination.expected_native_ids.insert(filled.animal->mID).second ||
        !destination.expected_native_ids.insert(filled.herd->mID).second) {
        partial_failure("factory_noun_id_collision");
    }
}

// One import. Everything up to the species lookup only reads and can be refused; from destination_player on, the
// original objects are being changed and a failure ends the process. The file, the database and the references
// to the filled objects are declared and released here, exactly where the original calls and their reference
// counts need them: none of them moves into a helper.
NativePlayerTransferProbeResult
import_player_transfer_impl(uint64_t request, uint64_t epoch, const NativePlayerTransferFixture& approved,
                            const NativePlayerTransferAdmission* admission, bool restore_primary,
                            const RetainedImport* restore_secondary = nullptr, uint32_t canonical_home = 0) {
    NativePlayerTransferProbeResult result{};
    request_id = request;
    const bool restore_existing = restore_primary || restore_secondary;
    const TransferOrder order{request,           epoch,          approved,        admission, restore_primary,
                              restore_secondary, canonical_home, restore_existing};
    const auto reject = [&](const char* reason) {
        event("native_player_import_rejected", reason);
        return result;
    };
    if (const auto reason = order_refusal(order)) {
        return reject(reason);
    }
    std::filesystem::path path;
    HeldFile file;
    if (const auto reason = artifact_refusal(approved, path, file)) {
        return reject(reason);
    }
    Destination destination;
    if (const auto reason = destination_refusal(order, destination)) {
        return reject(reason);
    }
    DatabasePackedFilePtr packed = new Resource::DatabasePackedFile(reinterpret_cast<const char16_t*>(path.c_str()));
    SerializerDatabasePtr database = new Simulator::SerializerDatabase(packed.get());
    Source source;
    if (const auto reason = read_source(order, destination, database.get(), source)) {
        database->Close();
        return reject(reason);
    }
    const auto arrival = destination.existing_avatar->GetPosition();
    const auto orientation = destination.existing_avatar->GetOrientation();
    auto second = destination_player(order, destination);
    cCollectableItemsPtr held_items = second->mpCRGItems;
    cHerdPtr herd = restore_existing
                        ? destination.existing_avatar->mHerd
                        : destination.manager->CreateHerd(arrival, source.species, 1, false,
                                                          static_cast<int>(approved.herd_personality), false);
    prepare_herd(order, herd.get(), source.species_key);
    const auto retained_herd_size = herd->mHerd.size();
    cCreatureAnimalPtr animal =
        restore_existing
            ? destination.existing_avatar
            : Animal::Create(arrival, source.species, static_cast<int>(approved.age), herd.get(), false, false);
    require_animal(order, animal.get(), herd.get());
    const Filled filled{second, held_items.get(), herd.get(), animal.get(), retained_herd_size};
    Scope local = reader_scope(filled, destination.cursor);
    if (!restore_existing) {
        expect_created_nouns(filled, destination);
    }
    run_original_readers(database.get(), approved.serialization_version, local);
    ObjectPtr player_root, animal_root;
    const bool roots = read_roots(database.get(), approved, player_root, animal_root);
    const bool closed = database->Close();
    database = nullptr;
    packed = nullptr;
    if (!roots || !closed || player_root.get() != static_cast<Object*>(second) ||
        animal_root.get() != static_cast<Object*>(static_cast<Noun*>(animal.get()))) {
        partial_failure("native_root_identity");
    }
    // C43D70 installed a generic locomotion strategy during deserialization.
    // Restore the original animal strategy after preserving its destination
    // herd and non-primary flag; model/profile were constructed natively above.
    reinterpret_cast<void(__thiscall*)(Animal*)>(base + 0x806660)(animal.get());
    animal->Teleport(arrival, orientation);
    Collection imported;
    verify_import(order, destination, source, filled, local, imported);
    return conclude_import(order, filled, source, imported);
}
} // namespace

NativePlayerTransferProbeResult import_native_player_transfer_probe(uint64_t request, uint64_t epoch,
                                                                    const NativePlayerTransferFixture& approved) {
    return import_player_transfer_impl(request, epoch, approved, nullptr, false);
}

NativePlayerTransferProbeResult import_native_player_transfer(uint64_t request,
                                                              const NativePlayerTransferAdmission& admission,
                                                              const NativePlayerTransferFixture& approved) {
    return import_player_transfer_impl(request, admission.epoch, approved, &admission, false);
}

NativePlayerTransferProbeResult restore_native_primary_transfer(uint64_t request,
                                                                const NativePlayerTransferAdmission& admission,
                                                                const NativePlayerTransferFixture& approved) {
    return import_player_transfer_impl(request, admission.epoch, approved, &admission, true);
}

NativePlayerTransferProbeResult restore_native_secondary_transfer(uint64_t request,
                                                                  const NativePlayerTransferAdmission& admission,
                                                                  const NativePlayerTransferFixture& approved,
                                                                  uint32_t player_id, uint32_t avatar_id,
                                                                  uint32_t herd_id, uint32_t canonical_home) {
    const RetainedImport existing{admission.epoch, player_id, avatar_id, herd_id};
    return import_player_transfer_impl(request, admission.epoch, approved, &admission, false, &existing,
                                       canonical_home);
}

bool reset_native_player_transfer_after_retirement(uint64_t epoch, uint32_t player_id, uint32_t avatar_id,
                                                   uint32_t herd_id) {
    if (!on_thread() || attached || scope || !epoch || native_second_player(epoch)) {
        return false;
    }
    auto manager = Manager::Get();
    auto first = manager ? manager->GetPlayer() : nullptr;
    auto avatar = manager ? manager->GetAvatar() : nullptr;
    if (!first || !avatar ||
        !native_actor_transfer_import_ready(epoch, first->mID, avatar->mID, first->mUniqueGameID)) {
        return false;
    }
    for (auto& noun : manager->mNouns) {
        if (noun.mID == player_id || noun.mID == avatar_id || noun.mID == herd_id) {
            return false;
        }
    }
    if (!used) {
        return true; // A secondary restored by the original checkpoint used no import slot.
    }
    if (retained_import.epoch != epoch || retained_import.player != player_id || retained_import.avatar != avatar_id ||
        retained_import.herd != herd_id) {
        return false;
    }
    retained_import = {};
    used = false;
    event("native_player_import_use_released", "exact_native_secondary_retirement");
    return true;
}
} // namespace sporemp
