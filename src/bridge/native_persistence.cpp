#include "native_persistence.h"
#include "native_player_transfer_probe.h"
#include "native_quiescence_probe.h"
#include "native_global_observer.h"
#include "native_persistence_abi.h"
#include "native_actors.h"
#include "native_content.h"
#include "native_player_context.h"
#include "native_award_context.h"
#include "native_checkpoint.h"
#include "diagnostics.h"
#include "detour_transaction.h"
#include <Spore/Simulator/SubSystem/GamePersistenceManager.h>
#include <Spore/Simulator/SubSystem/GameNounManager.h>
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/SubSystem/SpacePlayerData.h>
#include <Spore/Simulator/SubSystem/PlanetModel.h>
#include <Spore/Simulator/Serialization.h>
#include <Spore/Simulator/cCreatureAnimal.h>
#include <Spore/Simulator/cHerd.h>
#include <Spore/IO.h>
#include <Spore/App/IMessageManager.h>
#include <Spore/Resource/DatabasePackedFile.h>
#include <Spore/Resource/cResourceManager.h>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <intrin.h>

namespace sporemp {
namespace {
using Manager = Simulator::cGamePersistenceManager;
using Abi = NativePersistenceAbi<Manager>;
using State = NativePersistenceState;
using PackedFile = Resource::DatabasePackedFile;
using OpenRecord = bool(__thiscall*)(PackedFile*, const ResourceKey&, Resource::IRecord**, IO::AccessFlags, IO::CD,
                                     bool, Resource::RecordInfo*);
using SerializerOpen = bool(__thiscall*)(void*, Simulator::ISerializerDatabase*, const ResourceKey&, bool);
using SerializerDatabaseOpen = bool(__thiscall*)(void*, const ResourceKey&, Simulator::ISerializerWriteStream**, bool);
using WriteResource = bool(__thiscall*)(Resource::cResourceManager*, const Resource::ResourceObject*, void*,
                                        Resource::Database*, Resource::IResourceFactory*, const ResourceKey*);
constexpr wchar_t fixture_file[] = L"Satiria.spo";
constexpr char16_t fixture_name[] = u"Satiria";
constexpr wchar_t second_file[] = L"Haagos.spo";
constexpr char16_t second_name[] = u"Haagos";
constexpr uint64_t load_timeout_ms = 90000;
// The original routines this file calls, each pinned by its prefix before it is bound: the full save B29600, the
// filename loader B28020, the reset B7E010 that precedes a load, and the getter B3D460 of the manager that reset
// takes (preferred VAs, as in native_persistence_abi.h).
constexpr uint32_t save_rva = 0x729600, load_rva = 0x728020, reset_rva = 0x77e010, reset_manager_get_rva = 0x73d460;
uintptr_t image_base = 0;
uintptr_t image_end = 0;
DWORD engine_thread = 0;
NativePersistenceEvent event_sink = nullptr;
Abi::Save save_original = nullptr;
Abi::Load load_original = nullptr;
Abi::Reset reset_original = nullptr;
Abi::Get reset_manager_get = nullptr;
OpenRecord open_record_original = nullptr;
SerializerOpen serializer_open_original = nullptr;
SerializerDatabaseOpen serializer_database_open_original = nullptr;
WriteResource write_resource_original = nullptr;
using NeighborValues = decltype(Simulator::cCreatureAnimal::mNeighbors);
using NeighborWriter = bool(__cdecl*)(Simulator::ISerializerWriteStream*, const NeighborValues*);
using HerdWriter = bool(__cdecl*)(Simulator::ISerializerWriteStream*, const void* const*);
NeighborWriter neighbor_writer_original = nullptr;
HerdWriter herd_writer_original = nullptr;
bool graph_projection_attached = false;
thread_local const NeighborValues* graph_neighbors = nullptr;
thread_local const void* graph_herd = nullptr;
thread_local uint32_t graph_neighbor_writes = 0, graph_herd_writes = 0;
bool save_probe_attached = false;
bool record_probe_attached = false;
bool serializer_probe_attached = false;
bool serializer_database_probe_attached = false;
bool write_resource_probe_attached = false;
bool record_probe_attempted = false;
uint64_t save_probe_serial = 0;
std::atomic<uint64_t> cross_thread_save_count{0};
std::atomic<DWORD> cross_thread_save_thread{0};
std::atomic<bool> cross_thread_save_result{false};
constexpr uint32_t save_probe_limit = 2048;
constexpr uint32_t save_probe_stack_depth = 10;
// What a traced write opened: one of the four save archives by file name, or a record through one of the two
// serializer entries. Zero is a write that is counted but not traced.
constexpr uint8_t archive_planet_records_pkp = 1, archive_stars_db = 2, archive_stars_db_tmp = 3,
                  archive_serializer_open = 4, archive_serializer_database_open = 5, archive_planet_records_pkt = 6;

struct ProbeWrite {
    std::atomic<bool> ready{false};
    uint32_t instance = 0, type = 0, group = 0, caller = 0, thread = 0, access = 0, disposition = 0;
    uint32_t resource_caller = 0;
    std::array<uint32_t, save_probe_stack_depth> stack_rvas{};
    uint8_t stack_size = 0;
    uint8_t archive = 0;
    bool success = false;
};

std::array<ProbeWrite, save_probe_limit> probe_writes{};
std::atomic<uint32_t> probe_write_count{0}, probe_all_writes{0};
thread_local uint32_t active_write_resource_caller = 0;
uint32_t probe_flushed = 0;
NativePersistenceSnapshot snapshot;
uint64_t requested_epoch = 0, requested_ai = 0, load_scene_epoch = 0, load_scene_ai = 0;
uint64_t observed_ui_epoch = 0;
const wchar_t* selected_load_file = fixture_file;
const char16_t* selected_load_name = fixture_name;
ULONGLONG operation_started = 0;
bool available = false;
bool diagnostic_save = false;
bool complete_save = false;
uint32_t crash_boundary = 0;

bool on_thread() {
    return engine_thread && GetCurrentThreadId() == engine_thread;
}

bool __cdecl graph_neighbor_writer(Simulator::ISerializerWriteStream* stream, const NeighborValues* values) {
    if (graph_neighbors && values == graph_neighbors && on_thread()) {
        ++graph_neighbor_writes;
        const NeighborValues empty;
        return neighbor_writer_original(stream, &empty);
    }
    return neighbor_writer_original(stream, values);
}

bool __cdecl graph_herd_writer(Simulator::ISerializerWriteStream* stream, const void* const* value) {
    if (graph_herd && value == graph_herd && on_thread()) {
        ++graph_herd_writes;
        const void* empty = nullptr;
        return herd_writer_original(stream, &empty);
    }
    return herd_writer_original(stream, value);
}

const NativeHook graph_projection_hooks[] = {
    {reinterpret_cast<void**>(&neighbor_writer_original), reinterpret_cast<void*>(graph_neighbor_writer)},
    {reinterpret_cast<void**>(&herd_writer_original), reinterpret_cast<void*>(graph_herd_writer)}};

LONG toggle_graph_projection(bool attach) {
    return change_hooks(graph_projection_hooks, _countof(graph_projection_hooks), attach);
}

void event(const char* name, const char* format = "", ...) {
    if (!on_thread() || !event_sink) {
        return;
    }
    char fields[1536]{};
    va_list args;
    va_start(args, format);
    const int count = vsprintf_s(fields, format, args);
    va_end(args);
    if (count >= 0) {
        event_sink(name, fields);
    }
}

bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const auto begin = reinterpret_cast<uintptr_t>(address);
    const auto region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return begin >= region && size <= info.RegionSize && begin - region <= info.RegionSize - size;
}

bool read_pointer(uintptr_t address, uintptr_t& value) {
    if (!readable(reinterpret_cast<const void*>(address), sizeof(value))) {
        return false;
    }
    memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return true;
}

uint32_t image_rva(const void* address) {
    const auto value = reinterpret_cast<uintptr_t>(address);
    return value >= image_base && value < image_end ? static_cast<uint32_t>(value - image_base) : 0;
}

bool packed_file_leaf(PackedFile* database, char (&leaf)[64]) {
    // The pinned concrete GetLocation implementation is MOV EAX,[ECX+20]; RET.
    // Copy only a filename, never an OS profile or private path into the trace.
    const auto path = database->GetLocation();
    if (!path) {
        return false;
    }
    size_t length = 0;
    for (size_t i = 0; i < 1024; ++i) {
        if (!readable(path + i, sizeof(char16_t))) {
            return false;
        }
        const char16_t character = path[i];
        if (!character) {
            leaf[length] = 0;
            return length != 0;
        }
        if (character == u'/' || character == u'\\') {
            length = 0;
            continue;
        }
        if (character < 32 || character > 126 || length + 1 >= sizeof(leaf)) {
            return false;
        }
        leaf[length++] = static_cast<char>(character);
    }
    return false;
}

bool star_serializer_key(const ResourceKey& key) {
    if (key.groupID == 0 && key.instanceID == 0x0179d304) {
        switch (key.typeID) {
        case 0x0179d310:
        case 0x018eb9d2:
        case 0x03a3ac9f:
        case 0x03e4353c:
        case 0x042232a1:
        case 0x0424ea07:
            return true;
        default:
            break;
        }
    }
    return key.groupID == 0x01897c18 && key.instanceID == 0x01897c18 && key.typeID == 0x01897c18;
}

// The traced archive a packed file's name stands for, or zero.
uint8_t traced_archive(const char* leaf) {
    return !strcmp(leaf, "planetRecords.pkp")   ? archive_planet_records_pkp
           : !strcmp(leaf, "planetRecords.pkt") ? archive_planet_records_pkt
           : !strcmp(leaf, "stars.db")          ? archive_stars_db
           : !strcmp(leaf, "stars.db.tmp")      ? archive_stars_db_tmp
                                                : 0;
}

// The file name recorded for a traced archive that is not a serializer entry.
const char* traced_archive_name(uint8_t archive) {
    return archive == archive_planet_records_pkp   ? "planetRecords.pkp"
           : archive == archive_planet_records_pkt ? "planetRecords.pkt"
           : archive == archive_stars_db           ? "stars.db"
                                                   : "stars.db.tmp";
}

// One row for a record opened through either serializer entry. The hook passes the key as it stood before the
// original ran, and its own return address.
void trace_serializer_open(uint8_t archive, uint32_t instance, uint32_t type, uint32_t group, uint32_t caller,
                           bool truncate, bool success) {
    const auto index = probe_write_count.fetch_add(1, std::memory_order_relaxed);
    if (index >= save_probe_limit) {
        return;
    }
    auto& row = probe_writes[index];
    row.instance = instance;
    row.type = type;
    row.group = group;
    row.caller = caller;
    row.thread = GetCurrentThreadId();
    row.disposition = truncate ? 1 : 0;
    row.archive = archive;
    row.success = success;
    row.ready.store(true, std::memory_order_release);
}

bool __fastcall save_probe_hook(Manager* persistence, void*, const wchar_t* file, bool full) {
    if (!on_thread()) {
        const auto thread = GetCurrentThreadId();
        const bool result = save_original(persistence, file, full);
        cross_thread_save_thread.store(thread, std::memory_order_relaxed);
        cross_thread_save_result.store(result, std::memory_order_relaxed);
        cross_thread_save_count.fetch_add(1, std::memory_order_release);
        return result;
    }
    ++save_probe_serial;
    const char* selection = !file                             ? "ui_default"
                            : wcscmp(file, fixture_file) == 0 ? "Satiria"
                            : wcscmp(file, second_file) == 0  ? "Haagos"
                                                              : "other";
    event("native_save_probe_enter", ",\"serial\":%llu,\"selection\":\"%s\",\"full\":%s", save_probe_serial, selection,
          full ? "true" : "false");
    const bool result = save_original(persistence, file, full);
    event("native_save_probe_return", ",\"serial\":%llu,\"native_success\":%s", save_probe_serial,
          result ? "true" : "false");
    return result;
}

bool __fastcall open_record_probe_hook(PackedFile* database, void*, const ResourceKey& name, Resource::IRecord** output,
                                       IO::AccessFlags access, IO::CD disposition, bool arg_10,
                                       Resource::RecordInfo* info) {
    const auto caller = image_rva(_ReturnAddress());
    const bool observe = static_cast<int>(access) & static_cast<int>(IO::AccessFlags::Write);
    char leaf[64]{};
    const bool has_leaf = observe && packed_file_leaf(database, leaf);
    const uint8_t archive = has_leaf ? traced_archive(leaf) : 0;
    const auto instance = name.instanceID, type = name.typeID, group = name.groupID;
    const bool result = open_record_original(database, name, output, access, disposition, arg_10, info);
    if (observe) {
        probe_all_writes.fetch_add(1, std::memory_order_relaxed);
        if (archive) {
            const auto index = probe_write_count.fetch_add(1, std::memory_order_relaxed);
            if (index < save_probe_limit) {
                auto& row = probe_writes[index];
                row.instance = instance;
                row.type = type;
                row.group = group;
                row.caller = caller;
                row.resource_caller = active_write_resource_caller;
                row.thread = GetCurrentThreadId();
                row.access = static_cast<unsigned>(access);
                row.disposition = static_cast<unsigned>(disposition);
                row.archive = archive;
                row.success = result;
                void* frames[save_probe_stack_depth]{};
                const auto frame_count = CaptureStackBackTrace(0, save_probe_stack_depth, frames, nullptr);
                row.stack_size = static_cast<uint8_t>(frame_count);
                for (USHORT i = 0; i < frame_count; ++i) {
                    row.stack_rvas[i] = image_rva(frames[i]);
                }
                row.ready.store(true, std::memory_order_release);
            }
        }
    }
    return result;
}

bool __fastcall serializer_open_probe_hook(void* stream, void*, Simulator::ISerializerDatabase* database,
                                           const ResourceKey& name, bool truncate) {
    const auto caller = image_rva(_ReturnAddress());
    const bool observe = star_serializer_key(name);
    const auto instance = name.instanceID, type = name.typeID, group = name.groupID;
    const bool result = serializer_open_original(stream, database, name, truncate);
    if (observe) {
        trace_serializer_open(archive_serializer_open, instance, type, group, caller, truncate, result);
    }
    return result;
}

bool __fastcall serializer_database_open_probe_hook(void* database, void*, const ResourceKey& name,
                                                    Simulator::ISerializerWriteStream** output, bool truncate) {
    const auto caller = image_rva(_ReturnAddress());
    const bool observe = star_serializer_key(name);
    const auto instance = name.instanceID, type = name.typeID, group = name.groupID;
    const bool result = serializer_database_open_original(database, name, output, truncate);
    if (observe) {
        trace_serializer_open(archive_serializer_database_open, instance, type, group, caller, truncate, result);
    }
    return result;
}

bool __fastcall write_resource_probe_hook(Resource::cResourceManager* manager, void*,
                                          const Resource::ResourceObject* resource, void* factory_data,
                                          Resource::Database* database, Resource::IResourceFactory* factory,
                                          const ResourceKey* name) {
    const auto previous = active_write_resource_caller;
    active_write_resource_caller = image_rva(_ReturnAddress());
    const bool result = write_resource_original(manager, resource, factory_data, database, factory, name);
    active_write_resource_caller = previous;
    return result;
}

const NativeHook save_probe_hook_binding[] = {
    {reinterpret_cast<void**>(&save_original), reinterpret_cast<void*>(save_probe_hook)}};
const NativeHook record_probe_hook_binding[] = {
    {reinterpret_cast<void**>(&open_record_original), reinterpret_cast<void*>(open_record_probe_hook)}};
const NativeHook serializer_probe_hook_binding[] = {
    {reinterpret_cast<void**>(&serializer_open_original), reinterpret_cast<void*>(serializer_open_probe_hook)}};
const NativeHook serializer_database_probe_hook_binding[] = {
    {reinterpret_cast<void**>(&serializer_database_open_original),
     reinterpret_cast<void*>(serializer_database_open_probe_hook)}};
const NativeHook write_resource_probe_hook_binding[] = {
    {reinterpret_cast<void**>(&write_resource_original), reinterpret_cast<void*>(write_resource_probe_hook)}};

LONG toggle_save_probe(bool attach) {
    return change_hooks(save_probe_hook_binding, _countof(save_probe_hook_binding), attach);
}

LONG toggle_record_probe(bool attach) {
    return change_hooks(record_probe_hook_binding, _countof(record_probe_hook_binding), attach);
}

LONG toggle_serializer_probe(bool attach) {
    return change_hooks(serializer_probe_hook_binding, _countof(serializer_probe_hook_binding), attach);
}

LONG toggle_serializer_database_probe(bool attach) {
    return change_hooks(serializer_database_probe_hook_binding, _countof(serializer_database_probe_hook_binding),
                        attach);
}

LONG toggle_write_resource_probe(bool attach) {
    return change_hooks(write_resource_probe_hook_binding, _countof(write_resource_probe_hook_binding), attach);
}

// Attaches one observing hook after the load and records whether it took.
void arm_probe(LONG (*toggle)(bool), bool& attached, const char* armed_event, const char* rejected_event) {
    const auto hook_status = toggle(true);
    if (hook_status == NO_ERROR) {
        attached = true;
    }
    event(attached ? armed_event : rejected_event, ",\"status\":%ld,\"after_native_load\":true", hook_status);
}

// Detaches one hook at disposal, if it is attached. A hook that could not be detached stays marked attached, so its
// original pointer is kept for the detour that still runs.
void detach_probe(LONG (*toggle)(bool), bool& attached, const char* detached_event) {
    if (!attached) {
        return;
    }
    const auto hook_status = toggle(false);
    event(detached_event, ",\"status\":%ld", hook_status);
    if (hook_status == NO_ERROR) {
        attached = false;
    }
}

void serializer_open_event(const ProbeWrite& row) {
    event(row.archive == archive_serializer_open ? "native_save_serializer_open"
                                                 : "native_save_serializer_database_open",
          ",\"instance\":%u,\"type\":%u,\"group\":%u,\"caller_rva\":%u,\"truncate\":%s,\"write_thread_id\":%u,"
          "\"native_success\":%s",
          row.instance, row.type, row.group, row.caller, row.disposition ? "true" : "false", row.thread,
          row.success ? "true" : "false");
}

void record_write_open_event(const ProbeWrite& row) {
    char stack[160]{};
    size_t used = 0;
    for (uint8_t i = 0; i < row.stack_size; ++i) {
        const auto count = snprintf(stack + used, sizeof(stack) - used, "%s%u", i ? "," : "", row.stack_rvas[i]);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(stack) - used) {
            break;
        }
        used += static_cast<size_t>(count);
    }
    event("native_save_record_write_open",
          ",\"archive\":\"%s\",\"instance\":%u,\"type\":%u,\"group\":%u,\"access\":%u,\"disposition\":%u,\"caller_"
          "rva\":%u,\"resource_caller_rva\":%u,\"stack_rvas\":[%s],\"write_thread_id\":%u,\"native_success\":%s",
          traced_archive_name(row.archive), row.instance, row.type, row.group, row.access, row.disposition, row.caller,
          row.resource_caller, stack, row.thread, row.success ? "true" : "false");
}

// Writes the rows the hooks have completed, in order, from the engine thread. A row still being filled ends the pass.
void flush_probe_records() {
    if (!on_thread()) {
        return;
    }
    const auto available_rows = probe_write_count.load(std::memory_order_acquire);
    while (probe_flushed < available_rows && probe_flushed < save_probe_limit) {
        const auto& row = probe_writes[probe_flushed];
        if (!row.ready.load(std::memory_order_acquire)) {
            break;
        }
        if (row.archive == archive_serializer_open || row.archive == archive_serializer_database_open) {
            serializer_open_event(row);
        } else {
            record_write_open_event(row);
        }
        ++probe_flushed;
    }
}

Manager* manager() {
    auto value = Manager::Get();
    return readable(value, 0x4c) && *reinterpret_cast<const uintptr_t*>(value) == image_base + 0x105f9a0 ? value
                                                                                                         : nullptr;
}

// Whether the worker status and the game itself both say a Creature scene is running.
bool creature_scene(const worker::Message& status) {
    return status.values[0] == static_cast<uint64_t>(worker::Phase::scene) && status.values[5] == kGameCreature &&
           Simulator::GetGameModeID() == kGameCreature;
}

// The count of native AI entries the status carries; the load and save records name it native_ai_*.
uint64_t native_ai_entries(const worker::Message& status) {
    return status.values[2];
}

bool named_scene(const worker::Message& status, const char16_t* name) {
    if (!creature_scene(status)) {
        return false;
    }
    auto nouns = Simulator::cGameNounManager::Get();
    if (!nouns || !nouns->GetPlayer() || !nouns->GetAvatar() || nouns->GetPlayer()->mbIsDestroyed ||
        nouns->GetAvatar()->mbIsDestroyed) {
        return false;
    }
    auto home = Simulator::GetPlayerHomePlanet();
    return home && home->mName == name;
}

bool selected_scene(const worker::Message& status) {
    return named_scene(status, selected_load_name);
}

bool fixture_menu(const worker::Message& status) {
    return status.values[0] == static_cast<uint64_t>(worker::Phase::menu) && status.values[5] == kGGEMode &&
           Simulator::GetGameModeID() == kGGEMode;
}

// The pinned SDK declares PlanetID::internalValue as the planet index in
// the high byte and StarID in the low 24 bits.
constexpr uint32_t star_of(uint32_t planet_id) {
    return planet_id & 0x00ffffffu;
}

constexpr uint32_t planet_index_of(uint32_t planet_id) {
    return planet_id >> 24;
}

// Pinned SDK cbf9206: SpacePlayerData global VA 0x16E1D0C, active
// cPlanet pointer at +0x04, and its record pointer at +0x13C. These
// are read-only candidates in the original engine callback. A null
// active planet is legitimate in some modes and is not a location ID.
void observe_active_planet(uint64_t request, uint64_t epoch, uint32_t home_planet_id) {
    uintptr_t player_data = 0, active_planet = 0, active_record = 0;
    constexpr uintptr_t player_data_global_rva = 0x16e1d0c - 0x400000;
    constexpr uintptr_t active_planet_offset = 0x04, planet_record_offset = 0x13c;
    if (!read_pointer(image_base + player_data_global_rva, player_data) || !player_data ||
        !read_pointer(player_data + active_planet_offset, active_planet) || !active_planet ||
        !read_pointer(active_planet + planet_record_offset, active_record) ||
        !readable(reinterpret_cast<const void*>(active_record), sizeof(Simulator::cPlanetRecord))) {
        event("native_active_planet_unavailable",
              ",\"request\":%llu,\"observed_epoch\":%llu,\"canonical_location_qualified\":false", request, epoch);
        return;
    }
    const auto active_id = reinterpret_cast<const Simulator::cPlanetRecord*>(active_record)->mKey.internalValue;
    event("native_active_planet_candidate",
          ",\"request\":%llu,\"observed_epoch\":%llu,\"native_planet_id_candidate\":%u,\"native_star_id_candidate\":%"
          "u,\"native_planet_index_candidate\":%u,\"matches_home\":%s,\"canonical_location_qualified\":false",
          request, epoch, active_id, star_of(active_id), planet_index_of(active_id),
          active_id == home_planet_id ? "true" : "false");
}

// cPlanetModel::Get() is already required by the verified load path.
// Its SDK mPlanetKey is a surface-resource candidate, not a PlanetID.
void observe_surface(uint64_t request, uint64_t epoch, const ResourceKey& home_terrain) {
    auto model = Simulator::cPlanetModel::Get();
    if (!readable(model, 0x38)) {
        event("native_surface_unavailable",
              ",\"request\":%llu,\"observed_epoch\":%llu,\"canonical_location_qualified\":false", request, epoch);
        return;
    }
    const ResourceKey surface = model->mPlanetKey;
    event("native_surface_candidate",
          ",\"request\":%llu,\"observed_epoch\":%llu,\"surface_instance\":%u,\"surface_type\":%u,\"surface_group\":"
          "%u,\"matches_home_generated_terrain\":%s,\"canonical_location_qualified\":false",
          request, epoch, surface.instanceID, surface.typeID, surface.groupID,
          surface.instanceID == home_terrain.instanceID && surface.typeID == home_terrain.typeID &&
                  surface.groupID == home_terrain.groupID
              ? "true"
              : "false");
}

// The loaded world as the original describes it: the home planet, the active planet, the surface model, then the
// terrain records of the home planet.
void observe_loaded_world(uint64_t request, uint64_t epoch) {
    // Reuse the home-planet lookup already exercised by fixture_scene. Copy
    // scalar fields during this engine callback; retain no native reference.
    auto home = Simulator::GetPlayerHomePlanet();
    if (!readable(home, sizeof(Simulator::cPlanetRecord))) {
        event("native_world_observation_unavailable", ",\"request\":%llu,\"observed_epoch\":%llu", request, epoch);
        return;
    }
    const ResourceKey terrain = home->mGeneratedTerrainKey;
    // This is a diagnostic candidate until a native transition proves the current-location map.
    const uint32_t planet_id = home->mKey.internalValue;
    event("native_world_observed",
          ",\"request\":%llu,\"observed_epoch\":%llu,\"generated_terrain_instance\":%u,\"generated_terrain_type\":%u,"
          "\"generated_terrain_group\":%u,\"home_world\":%s,\"native_home_planet_id_candidate\":%u,\"native_home_star_"
          "id_candidate\":%u,\"native_planet_index_candidate\":%u,\"canonical_location_qualified\":false,\"canonical_"
          "resource_validation\":false",
          request, epoch, terrain.instanceID, terrain.typeID, terrain.groupID, home->mbHomeWorld ? "true" : "false",
          planet_id, star_of(planet_id), planet_index_of(planet_id));
    observe_active_planet(request, epoch, planet_id);
    observe_surface(request, epoch, terrain);
    inspect_native_terrain_records(terrain.instanceID, terrain.typeID, terrain.groupID, request);
    mark_native_global_observer(request, epoch, NativeGlobalObservationPhase::scene_ready);
}

void fail(const char* reason, uint64_t observed_epoch) {
    snapshot.state = State::failed;
    event("native_persistence_failed",
          ",\"request\":%llu,\"reason\":\"%s\",\"requested_epoch\":%llu,\"observed_epoch\":%llu", snapshot.request,
          reason, requested_epoch, observed_epoch);
}

// Whether `size` bytes at `rva` would run past the image.
bool outside_image(uintptr_t base, uintptr_t end, uint32_t rva, size_t size) {
    return rva > end - base || size > end - base - rva;
}

bool code_differs(uintptr_t base, uint32_t rva, const unsigned char* bytes, size_t size) {
    return memcmp(reinterpret_cast<const void*>(base + rva), bytes, size) != 0;
}

// A fresh start for this process image: nothing requested, nothing observed, an empty write trace.
void reset_persistence_state(uintptr_t base, uintptr_t end, DWORD thread, NativePersistenceEvent sink) {
    image_base = base;
    image_end = end;
    engine_thread = thread;
    event_sink = sink;
    available = false;
    snapshot = {};
    observed_ui_epoch = 0;
    selected_load_file = fixture_file;
    selected_load_name = fixture_name;
    record_probe_attempted = false;
    cross_thread_save_count.store(0);
    cross_thread_save_thread.store(0);
    cross_thread_save_result.store(false);
    probe_write_count.store(0);
    probe_all_writes.store(0);
    probe_flushed = 0;
    for (auto& row : probe_writes) {
        row.ready.store(false, std::memory_order_relaxed);
    }
}

// The save, the filename loader and the reset. All three prefixes contain only position-relative operands.
bool persistence_routines_pinned(uintptr_t base, uintptr_t end) {
    struct Prefix {
        uint32_t rva;
        unsigned char bytes[16];
    };

    static const Prefix prefixes[] = {
        {save_rva, {0x83, 0xec, 0x10, 0x55, 0x8b, 0xe9, 0xe8, 0x95, 0x3d, 0x01, 0x00, 0x8b, 0xc8, 0xe8, 0x7e, 0x18}},
        {load_rva, {0x83, 0xec, 0x40, 0x53, 0x55, 0x8b, 0x6c, 0x24, 0x4c, 0x56, 0x57, 0x33, 0xff, 0x8b, 0xf1, 0x55}},
        {reset_rva, {0x55, 0x56, 0x57, 0x8b, 0xf1, 0xe8, 0x36, 0xf4, 0xfb, 0xff, 0x8b, 0xe8, 0x8b, 0x7d, 0x24, 0xe8}}};
    for (const auto& item : prefixes) {
        if (outside_image(base, end, item.rva, sizeof(item.bytes)) ||
            code_differs(base, item.rva, item.bytes, sizeof(item.bytes))) {
            event("persistence_binding_rejected", ",\"rva\":%u", item.rva);
            return false;
        }
    }
    return true;
}

// The two getters that return a global in EAX. Their MOV operands are relocated explicitly before the byte
// comparison.
bool persistence_getters_pinned(uintptr_t base, uintptr_t end) {
    const struct {
        uint32_t rva, global_rva;
    } getters[] = {{0x73d440, 0x127eaf4}, {reset_manager_get_rva, 0x127eafc}};

    for (const auto& item : getters) {
        unsigned char bytes[] = {0xa1, 0, 0, 0, 0, 0xc3};
        const auto address = static_cast<uint32_t>(base + item.global_rva);
        memcpy(bytes + 1, &address, sizeof(address));
        if (outside_image(base, end, item.rva, sizeof(bytes)) || code_differs(base, item.rva, bytes, sizeof(bytes))) {
            event("persistence_binding_rejected", ",\"rva\":%u", item.rva);
            return false;
        }
    }
    return true;
}

// SPOREMP_M10_SAVE_PROBE=observe pins the routines the write probes detour and attaches the observing save hook.
// The write probes themselves are armed later, after a load. False when the option is set but cannot be honoured;
// true when it is unset or the hook is attached.
bool attach_requested_save_probe(uintptr_t base, uintptr_t end) {
    wchar_t probe_option[16]{};
    const auto probe_length = GetEnvironmentVariableW(L"SPOREMP_M10_SAVE_PROBE", probe_option, _countof(probe_option));
    if (!probe_length) {
        return true;
    }
    const unsigned char record_bytes[] = {0x83, 0xec, 0x1c, 0x53, 0x55, 0x56, 0x33, 0xdb,
                                          0xf6, 0x44, 0x24, 0x34, 0x03, 0x57, 0x8b, 0xf1};
    const unsigned char location_bytes[] = {0x8b, 0x41, 0x20, 0xc3};
    const unsigned char write_resource_bytes[] = {0x8b, 0x44, 0x24, 0x04, 0x83, 0xec, 0x0c,
                                                  0x53, 0x32, 0xdb, 0x57, 0x8b, 0xf9};
    unsigned char serializer_bytes[] = {0x64, 0xa1, 0, 0, 0, 0, 0x6a, 0xff, 0x68, 0, 0, 0, 0, 0x50, 0x64, 0x89};
    unsigned char database_bytes[] = {0x6a, 0xff, 0x68, 0, 0, 0, 0, 0x64, 0xa1, 0, 0, 0, 0, 0x50, 0x64, 0x89};
    // The pinned SEH handler operand is an absolute VA and relocates with
    // the original executable. The remaining bytes are position invariant.
    const auto serializer_handler = static_cast<uint32_t>(base + 0xe0d4a8);
    memcpy(serializer_bytes + 9, &serializer_handler, sizeof(serializer_handler));
    const auto database_handler = static_cast<uint32_t>(base + 0xe0e6d8);
    memcpy(database_bytes + 3, &database_handler, sizeof(database_handler));
    constexpr uint32_t record_rva = 0x4da6f0, location_rva = 0x642700, write_resource_rva = 0x4deda0,
                       serializer_rva = 0x29e260, database_rva = 0x29e470;
    if (probe_length >= _countof(probe_option) || wcscmp(probe_option, L"observe") ||
        outside_image(base, end, record_rva, sizeof(record_bytes)) ||
        outside_image(base, end, location_rva, sizeof(location_bytes)) ||
        outside_image(base, end, write_resource_rva, sizeof(write_resource_bytes)) ||
        outside_image(base, end, serializer_rva, sizeof(serializer_bytes)) ||
        outside_image(base, end, database_rva, sizeof(database_bytes)) ||
        code_differs(base, record_rva, record_bytes, sizeof(record_bytes)) ||
        code_differs(base, location_rva, location_bytes, sizeof(location_bytes)) ||
        code_differs(base, write_resource_rva, write_resource_bytes, sizeof(write_resource_bytes)) ||
        code_differs(base, serializer_rva, serializer_bytes, sizeof(serializer_bytes)) ||
        code_differs(base, database_rva, database_bytes, sizeof(database_bytes))) {
        event("native_save_probe_binding_rejected");
        return false;
    }
    open_record_original = reinterpret_cast<OpenRecord>(base + record_rva);
    write_resource_original = reinterpret_cast<WriteResource>(base + write_resource_rva);
    serializer_open_original = reinterpret_cast<SerializerOpen>(base + serializer_rva);
    serializer_database_open_original = reinterpret_cast<SerializerDatabaseOpen>(base + database_rva);
    const auto hook_status = toggle_save_probe(true);
    if (hook_status != NO_ERROR) {
        event("native_save_probe_hook_rejected", ",\"status\":%ld", hook_status);
        return false;
    }
    save_probe_attached = true;
    event("native_save_probe_ready",
          ",\"code_prefixes\":5,\"record_data_modified\":false,\"native_execution_qualified\":false");
    return true;
}

// The one scalar an operation may carry: a load names the fixture (0) or the second file (1); a save fault names
// its crash boundary, 1 before or 2 after the original save, and needs a second player; a save carries none.
bool valid_command_values(const worker::Message& request, const worker::Message& status) {
    using worker::Op;
    const size_t first_zero = request.op == Op::save_fault || request.op == Op::load ? 1 : 0;
    for (size_t i = first_zero; i < request.values.size(); ++i) {
        if (request.values[i]) {
            return false;
        }
    }
    if (request.op == Op::load && request.values[0] > 1) {
        return false;
    }
    if (request.op != Op::load && request.op != Op::save_fault && request.values[0]) {
        return false;
    }
    return request.op != Op::save_fault ||
           (request.values[0] >= 1 && request.values[0] <= 2 && native_second_player(status.epoch));
}
} // namespace

bool initialize_native_persistence(uintptr_t base, uintptr_t end, DWORD thread, NativePersistenceEvent sink) {
    reset_persistence_state(base, end, thread, sink);
    if (!on_thread() || end <= base) {
        return false;
    }
    if (!persistence_routines_pinned(base, end) || !persistence_getters_pinned(base, end)) {
        return false;
    }
    save_original = reinterpret_cast<Abi::Save>(base + save_rva);
    load_original = reinterpret_cast<Abi::Load>(base + load_rva);
    reset_original = reinterpret_cast<Abi::Reset>(base + reset_rva);
    reset_manager_get = reinterpret_cast<Abi::Get>(base + reset_manager_get_rva);
    if (!manager()) {
        event("persistence_manager_rejected");
        return false;
    }
    if (!initialize_native_checkpoint(base, end, thread, sink)) {
        return false;
    }
    if (!attach_requested_save_probe(base, end)) {
        return false;
    }
    available = true;
    snapshot.state = State::idle;
    event("persistence_bindings_checked",
          ",\"code_prefixes\":5,\"fixture\":\"Satiria.spo\",\"native_execution_qualified\":false");
    return true;
}

bool native_persistence_busy() {
    if (!on_thread()) {
        return true;
    }
    return snapshot.state == State::save_pending || snapshot.state == State::saving ||
           snapshot.state == State::load_pending || snapshot.state == State::loading;
}

worker::Result native_persistence_command(const worker::Message& request, const worker::Message& status) {
    using worker::Result;
    using worker::Op;
    if (!on_thread() || !available ||
        (request.op != Op::save && request.op != Op::save_probe && request.op != Op::save_fault &&
         request.op != Op::load)) {
        return Result::unavailable;
    }
    if (native_persistence_busy()) {
        return Result::busy;
    }
    if (!request.sequence) {
        return Result::invalid;
    }
    if (request.epoch != status.epoch) {
        return Result::stale;
    }
    if (!valid_command_values(request, status)) {
        return Result::invalid;
    }
    crash_boundary = request.op == Op::save_fault ? static_cast<uint32_t>(request.values[0]) : 0;
    const bool save = request.op != Op::load;
    if (!manager() || (save ? !selected_scene(status) : !fixture_menu(status))) {
        return Result::unavailable;
    }
    if (!save) {
        selected_load_file = request.values[0] ? second_file : fixture_file;
        selected_load_name = request.values[0] ? second_name : fixture_name;
    }
    diagnostic_save = request.op == Op::save_probe;
    complete_save = save && native_second_player(status.epoch) != nullptr;
    snapshot.request = request.sequence;
    snapshot.native_save_result_valid = false;
    snapshot.native_save_result = false;
    snapshot.state = save ? State::save_pending : State::load_pending;
    requested_epoch = status.epoch;
    requested_ai = native_ai_entries(status);
    load_scene_epoch = load_scene_ai = 0;
    operation_started = GetTickCount64();
    event(save ? "native_save_queued" : "native_load_queued",
          ",\"request\":%llu,\"requested_epoch\":%llu,\"native_ai_before\":%llu", snapshot.request, requested_epoch,
          requested_ai);
    return Result::accepted;
}

namespace {
// The observing writer hook is armed only after the selected original
// Creature world has loaded. Attaching it during native Load caused a
// reproducible callback crash in the first disposable probe.
void arm_write_probes_after_load(const worker::Message& status) {
    if (!save_probe_attached || record_probe_attempted || snapshot.state != State::loaded || !selected_scene(status) ||
        !native_ai_entries(status)) {
        return;
    }
    record_probe_attempted = true;
    arm_probe(toggle_record_probe, record_probe_attached, "native_save_record_probe_armed",
              "native_save_record_probe_rejected");
    if (!record_probe_attached) {
        return;
    }
    arm_probe(toggle_write_resource_probe, write_resource_probe_attached, "native_save_resource_probe_armed",
              "native_save_resource_probe_rejected");
    arm_probe(toggle_serializer_probe, serializer_probe_attached, "native_save_serializer_probe_armed",
              "native_save_serializer_probe_rejected");
    if (serializer_probe_attached) {
        arm_probe(toggle_serializer_database_probe, serializer_database_probe_attached,
                  "native_save_serializer_database_probe_armed", "native_save_serializer_database_probe_rejected");
    }
}

// Saves the observing hook saw return on another thread since the last update. They are counted there and recorded
// here, because only the engine thread writes events.
void report_cross_thread_saves() {
    const auto cross_thread_saves = cross_thread_save_count.exchange(0, std::memory_order_acquire);
    if (cross_thread_saves) {
        event("native_save_probe_other_thread_return",
              ",\"count\":%llu,\"save_thread_id\":%u,\"engine_thread_id\":%u,\"last_native_success\":%s",
              cross_thread_saves, cross_thread_save_thread.load(std::memory_order_relaxed), engine_thread,
              cross_thread_save_result.load(std::memory_order_relaxed) ? "true" : "false");
    }
}

// Observe ordinary original-game UI world entry once per Creature scene.
// A controlled Save always targets the exact filename admitted by the
// previous original-game Load. The loaded home planet must still agree.
void observe_ui_world_entry(const worker::Message& status) {
    if (snapshot.state != State::idle || !creature_scene(status) || !native_ai_entries(status) ||
        observed_ui_epoch == status.epoch) {
        return;
    }
    auto nouns = Simulator::cGameNounManager::Get();
    auto home = Simulator::GetPlayerHomePlanet();
    if (nouns && nouns->GetPlayer() && nouns->GetAvatar() && readable(home, sizeof(Simulator::cPlanetRecord))) {
        observed_ui_epoch = status.epoch;
        event("native_ui_world_entry", ",\"observed_epoch\":%llu,\"native_ai_entries\":%llu,\"named_haagos\":%s",
              status.epoch, native_ai_entries(status), home->mName == u"Haagos" ? "true" : "false");
        observe_loaded_world(0, status.epoch);
    }
}

// Snapshot at the actual serialization boundary, after intervening
// native AI and queued console work, rather than at IPC admission.
// False after recording the failure.
bool checkpoint_actors_before_save(const worker::Message& status, NativeActorCheckpoint& checkpoint) {
    if (complete_save || diagnostic_save || status.values[4]) {
        if (!snapshot_native_actor_checkpoint(checkpoint, snapshot.request, complete_save || diagnostic_save)) {
            fail("actor_checkpoint_unavailable", status.epoch);
            return false;
        }
        return true;
    }
    const auto nouns = Simulator::cGameNounManager::Get();
    const auto primary = nouns ? nouns->GetPlayer() : nullptr;
    uint32_t player_id = 0, avatar_id = 0;
    if (!status.values[3] || !primary ||
        !native_actor_transfer_selection(status.epoch, 1, primary->mUniqueGameID, player_id, avatar_id) ||
        !observe_native_player_checkpoint(snapshot.request, "before_single_save")) {
        fail("single_player_checkpoint_unavailable", status.epoch);
        return false;
    }
    return true;
}

// A save of both players: observe them, sample the awards, then arm the checkpoint extension. False after recording
// the failure.
bool arm_checkpoint_barrier(const worker::Message& status, const NativeActorCheckpoint& checkpoint) {
    if (!observe_native_player_checkpoint(snapshot.request, "before_save")) {
        fail("player_probe_unavailable", status.epoch);
        return false;
    }
    sample_native_awards();
    if (!arm_native_checkpoint(checkpoint, snapshot.request)) {
        fail("checkpoint_extension_unavailable", status.epoch);
        return false;
    }
    event(diagnostic_save ? "native_checkpoint_probe_only" : "native_checkpoint_barrier",
          ",\"request\":%llu,\"schema\":%u,\"durable\":false", snapshot.request, native_checkpoint_schema());
    return true;
}

// The state and records that follow the original save's return.
void conclude_save(bool result) {
    const bool extension_ok = !complete_save || finish_native_checkpoint_write();
    const auto observed_epoch = native_actor_worker_status().epoch;
    snapshot.native_save_result_valid = true;
    snapshot.native_save_result = result;
    snapshot.state = result && extension_ok && observed_epoch == requested_epoch ? State::saved : State::failed;
    event("native_save_return",
          ",\"request\":%llu,\"native_success\":%s,\"durable\":false,\"requested_epoch\":%llu,\"observed_"
          "epoch\":%llu,\"elapsed_ms\":%llu",
          snapshot.request, result ? "true" : "false", requested_epoch, observed_epoch,
          GetTickCount64() - operation_started);
    if (complete_save) {
        observe_native_player_checkpoint(snapshot.request, "after_save");
        sample_native_awards();
        event("native_checkpoint_save_complete",
              ",\"request\":%llu,\"success\":%s,\"diagnostic_only\":%s,\"durable\":false", snapshot.request,
              snapshot.state == State::saved ? "true" : "false", diagnostic_save ? "true" : "false");
    } else if (!diagnostic_save) {
        observe_native_player_checkpoint(snapshot.request, "after_single_save");
    }
}

// The queued save: checkpoint, the original full save, then its result. The two crash boundaries end the process
// on purpose, one on each side of the original save.
void run_pending_save(const worker::Message& status, Manager* persistence) {
    NativeActorCheckpoint checkpoint;
    if (!checkpoint_actors_before_save(status, checkpoint)) {
        return;
    }
    if (complete_save && !arm_checkpoint_barrier(status, checkpoint)) {
        return;
    }
    snapshot.state = State::saving;
    event("native_save_enter", ",\"request\":%llu,\"fixture\":\"%ls\"", snapshot.request, selected_load_file);
    mark_native_global_observer(snapshot.request, status.epoch, NativeGlobalObservationPhase::before_save);
    if (crash_boundary == 1) {
        event("native_checkpoint_fault", ",\"boundary\":\"before_original_save\"");
        TerminateProcess(GetCurrentProcess(), 91);
        return;
    }
    // The original Options caller E03417 resumes its save pause, then
    // calls B29600(manager,nullptr,true). Match its native full-save
    // branch with the explicitly verified controlled filename. Original
    // callbacks, serialization and backup marker still execute once.
    const bool result = save_original(persistence, selected_load_file, true);
    if (crash_boundary == 2) {
        event("native_checkpoint_fault", ",\"boundary\":\"after_original_save\",\"native_success\":%s",
              result ? "true" : "false");
        TerminateProcess(GetCurrentProcess(), 92);
        return;
    }
    mark_native_global_observer(snapshot.request, native_actor_worker_status().epoch,
                                NativeGlobalObservationPhase::after_save);
    conclude_save(result);
}

// The queued load: ask the original to load the selected file. Its completion is watched from the next updates.
void request_pending_load(const worker::Message& status, Manager* persistence) {
    auto reset_manager = reset_manager_get();
    if (!readable(reset_manager, 0x38) ||
        *reinterpret_cast<const uintptr_t*>(reset_manager) != image_base + 0x10652b0 ||
        !Simulator::cPlanetModel::Get() || !App::IMessageManager::Get()) {
        fail("load_prerequisite_changed", status.epoch);
        return;
    }
    snapshot.state = State::loading;
    event("native_load_requested",
          ",\"request\":%llu,\"fixture\":\"%ls\",\"requested_epoch\":%llu,\"native_ai_before\":%llu", snapshot.request,
          selected_load_file, requested_epoch, requested_ai);
    mark_native_global_observer(snapshot.request, status.epoch, NativeGlobalObservationPhase::before_load);
    // Ordinary saved-game UI DEBF4B..DEBF72: immediate pre-load message,
    // native reset manager, then filename loader. B28020 copies the name
    // and requests original LoadGame mode. It returns void, not success.
    // No guessed GameLoadParameters, species pointer, or selected star is
    // created: the original checkpoint loader restores native state.
    App::IMessageManager::Get()->MessageSend(0x0680c633, const_cast<wchar_t*>(selected_load_file));
    reset_original(reset_manager);
    load_original(persistence, selected_load_file);
}

// A save or load accepted by native_persistence_command, run on the first update after its acknowledgement. The
// scene it was accepted in must still be the one that is running.
void dispatch_pending(const worker::Message& status) {
    const bool save = snapshot.state == State::save_pending;
    if (requested_epoch != status.epoch || !(save ? selected_scene(status) : fixture_menu(status))) {
        fail("scene_changed_before_dispatch", status.epoch);
        return;
    }
    auto persistence = manager();
    if (!persistence) {
        fail("persistence_manager_changed", status.epoch);
        return;
    }
    if (save) {
        run_pending_save(status, persistence);
        return;
    }
    request_pending_load(status, persistence);
}

// A load in progress. It is complete once the selected world runs in a new scene epoch, that epoch has been seen
// on two updates, and native AI has advanced past both the request and the first sight of the scene.
void watch_load(const worker::Message& status) {
    if (GetTickCount64() - operation_started > load_timeout_ms) {
        fail("load_timeout", status.epoch);
        return;
    }
    if (!selected_scene(status) || status.epoch == requested_epoch) {
        return;
    }
    if (load_scene_epoch != status.epoch) {
        load_scene_epoch = status.epoch;
        load_scene_ai = native_ai_entries(status);
        return;
    }
    if (native_ai_entries(status) <= load_scene_ai || native_ai_entries(status) <= requested_ai) {
        return;
    }
    if (native_checkpoint_present() && !restore_native_checkpoint(status.epoch, snapshot.request)) {
        observe_native_player_checkpoint(snapshot.request, "restore_failed");
        fail("checkpoint_extension_restore_failed", status.epoch);
        return;
    }
    snapshot.state = State::loaded;
    event("native_load_complete",
          ",\"request\":%llu,\"requested_epoch\":%llu,\"observed_epoch\":%llu,\"native_ai_before\":%llu,\"scene_native_"
          "ai_before\":%llu,\"native_ai_after\":%llu,\"elapsed_ms\":%llu,\"identities_restored\":%s",
          snapshot.request, requested_epoch, status.epoch, requested_ai, load_scene_ai, native_ai_entries(status),
          GetTickCount64() - operation_started, native_checkpoint_present() ? "true" : "false");
    observe_loaded_world(snapshot.request, status.epoch);
    observe_native_player_checkpoint(snapshot.request, "loaded");
    if (native_checkpoint_present()) {
        mark_native_global_observer(snapshot.request, status.epoch, NativeGlobalObservationPhase::checkpoint);
    }
}
} // namespace

// One engine update: the probes and observations first, then the one step the current state calls for.
void update_native_persistence(const worker::Message& status) {
    if (!on_thread() || !available) {
        return;
    }
    arm_write_probes_after_load(status);
    flush_probe_records();
    report_cross_thread_saves();
    observe_ui_world_entry(status);
    if (snapshot.state == State::loaded && (status.epoch != load_scene_epoch || !selected_scene(status))) {
        fail("loaded_scene_changed", status.epoch);
        return;
    }
    if (snapshot.state == State::save_pending || snapshot.state == State::load_pending) {
        dispatch_pending(status);
        return;
    }
    if (snapshot.state == State::loading) {
        watch_load(status);
    }
}

void annotate_native_persistence(worker::Message& status) {
    if (!on_thread()) {
        return;
    }
    status.values[8] = snapshot.request;
    status.values[9] = static_cast<uint64_t>(snapshot.state);
}

NativePersistenceSnapshot native_persistence_snapshot() {
    return on_thread() ? snapshot : NativePersistenceSnapshot{};
}

bool native_persistence_matches_home_location(const network::LocationId& expected) {
    if (!on_thread() || snapshot.state != State::loaded || Simulator::GetGameModeID() != kGameCreature ||
        !expected.valid()) {
        return false;
    }
    const auto home = Simulator::GetPlayerHomePlanet();
    const auto model = Simulator::cPlanetModel::Get();
    if (!readable(home, sizeof(Simulator::cPlanetRecord)) || !readable(model, 0x38)) {
        event("native_location_admission", ",\"available\":false,\"matched\":false");
        return false;
    }
    const uint32_t planet = home->mKey.internalValue;
    const auto terrain = home->mGeneratedTerrainKey;
    const auto surface = model->mPlanetKey;
    const bool resource_matches = terrain.instanceID && terrain.instanceID == surface.instanceID &&
                                  terrain.typeID == surface.typeID && terrain.groupID == surface.groupID;
    const bool matched = home->mbHomeWorld && planet && resource_matches && expected.star == star_of(planet) &&
                         expected.planet == planet && expected.area == terrain.instanceID;
    event("native_location_admission",
          ",\"available\":true,\"matched\":%s,\"home_world\":%s,\"surface_matches_home\":%s,\"expected_star\":%llu,"
          "\"expected_planet\":%llu,\"expected_area\":%llu,\"native_star\":%u,\"native_planet\":%u,\"native_area\":%u",
          matched ? "true" : "false", home->mbHomeWorld ? "true" : "false", resource_matches ? "true" : "false",
          expected.star, expected.planet, expected.area, star_of(planet), planet, terrain.instanceID);
    return matched;
}

namespace {
// Original descriptor binders for locomotive mNeighbors (02415693)
// and animal mHerd (72ae9fe0), pinned against GOG 3.1.0.29.
constexpr uint32_t neighbor_writer_rva = 0x8438f0, herd_writer_rva = 0x7caad0;

// One export: what was asked, the game's own player and avatar, and the pair that is written with the identity it
// had before the first write. The pointers live only on the engine-thread stack of this export.
struct GraphExport {
    uint64_t request = 0;
    bool project_scene_references = false, include_stage = false;
    const NativePlayerGraphSelection* selection = nullptr;
    Simulator::cGameNounManager* nouns = nullptr;
    Simulator::cPlayer* primary_player = nullptr;
    Simulator::cCreatureAnimal* primary_avatar = nullptr;
    Simulator::cPlayer* player = nullptr;
    Simulator::cCreatureAnimal* avatar = nullptr;
    uint32_t player_id = 0, game_id = 0, avatar_id = 0, home = 0;
};

// The scene objects the avatar refers to, kept as addresses only to compare them after the export.
struct SceneReferences {
    std::array<const void*, 128> neighbors{};
    NeighborValues::size_type neighbor_count = 0;
    Simulator::cHerd* herd = nullptr;
};

// What the serializer accepted, step by step.
struct GraphWrite {
    bool initialized = false, opened = false, root_good = false, stage_good = false, classes_good = false,
         closed = false;
    size_t keys = 0;
};

// The pair a transfer selection names, in place of the game's own: the world must be paused by the request the
// selection carries, and the selected slot must resolve to exactly one player and one avatar. On entry
// `graph.player` is the primary player.
bool select_graph_actors(const NativePlayerGraphSelection& selection, const Simulator::cPlanetRecord* home,
                         GraphExport& graph) {
    const auto pause = native_quiescence_probe_status(selection.epoch);
    uint32_t player_id = 0, avatar_id = 0;
    if (!selection.canonical_player || !selection.canonical_home || !selection.pause_request || !pause.ready ||
        !pause.owns_pause || pause.request != selection.pause_request ||
        !native_actor_transfer_selection(selection.epoch, selection.native_slot, selection.expected_unique_game,
                                         player_id, avatar_id)) {
        return false;
    }
    graph.player = selection.native_slot == 1 ? graph.primary_player : native_second_player(selection.epoch);
    graph.avatar = nullptr;
    for (auto& noun : graph.nouns->mNouns) {
        if (noun.mID == avatar_id) {
            if (graph.avatar) {
                return false;
            }
            graph.avatar = object_cast<Simulator::cCreatureAnimal>(&noun);
        }
    }
    return graph.player && graph.player->mID == player_id && graph.avatar &&
           (selection.native_slot != 1 || (home && selection.canonical_home == home->mKey.internalValue));
}

// The player and avatar to export and the home recorded with them. False unless they are one living, validated
// player with its avatar; without a selection the game must also hold no second player.
bool resolve_graph_actors(GraphExport& graph) {
    const auto selection = graph.selection;
    graph.nouns = Simulator::cGameNounManager::Get();
    graph.player = graph.nouns ? graph.nouns->GetPlayer() : nullptr;
    graph.avatar = graph.nouns ? graph.nouns->GetAvatar() : nullptr;
    const auto home = Simulator::GetPlayerHomePlanet();
    graph.primary_player = graph.player;
    graph.primary_avatar = graph.avatar;
    if (selection && !select_graph_actors(*selection, home, graph)) {
        return false;
    }
    const auto player = graph.player;
    const auto avatar = graph.avatar;
    if (!player || !avatar || !home || (!selection && native_second_player(native_actor_worker_status().epoch)) ||
        player->mbIsDestroyed || avatar->mbIsDestroyed || avatar->mbMarkedForDeletion ||
        !validate_native_player_checkpoint(player, player->mID)) {
        return false;
    }
    graph.home = selection ? selection->canonical_home : home->mKey.internalValue;
    if (selection) {
        event("native_player_graph_selection",
              ",\"request\":%llu,\"epoch\":%llu,\"canonical_player\":%llu,"
              "\"native_slot\":%u,\"player_native_id\":%u,\"avatar_native_id\":%u,\"unique_game_id\":%u,\"canonical_"
              "home\":%u",
              graph.request, selection->epoch, selection->canonical_player, selection->native_slot, player->mID,
              avatar->mID, selection->expected_unique_game, selection->canonical_home);
    }
    return true;
}

// These position-independent prefixes pin the reset/open/root-write ABI.
bool graph_serializer_pinned() {
    const struct {
        uint32_t rva;
        unsigned char bytes[16];
    } prefixes[] = {
        {0x29f160, {0x53, 0x56, 0x8b, 0xf1, 0x57, 0x33, 0xdb, 0x8d, 0x7e, 0x10, 0x88, 0x5e, 0x0c, 0x8b, 0x47, 0x0c}},
        {0x29df20, {0x0f, 0x57, 0xc0, 0x33, 0xc0, 0x38, 0x44, 0x24, 0x04, 0x56, 0x0f, 0x94, 0xc0, 0x57, 0x8b, 0xf1}},
        {0x29d980, {0x57, 0x8b, 0xf9, 0x80, 0x7f, 0x0c, 0x00, 0x74, 0x34, 0x8b, 0x4f, 0x08, 0x8b, 0x01, 0x8b, 0x50}}};

    for (const auto& prefix : prefixes) {
        if (prefix.rva + sizeof(prefix.bytes) > image_end - image_base ||
            memcmp(reinterpret_cast<void*>(image_base + prefix.rva), prefix.bytes, sizeof(prefix.bytes))) {
            return false;
        }
    }
    return true;
}

// The private diagnostics directory of this run: an absolute local path whose every component is an existing
// directory and none a reparse point.
bool diagnostics_root(std::filesystem::path& root) {
    wchar_t directory[32768]{};
    const auto length = GetEnvironmentVariableW(L"SPOREMP_DIAGNOSTICS_DIR", directory, _countof(directory));
    if (!length || length >= _countof(directory) || !absolute_local_path(directory)) {
        return false;
    }
    root = directory;
    std::filesystem::path at;
    for (const auto& part : root) {
        if (part == L"." || part == L"..") {
            return false;
        }
        at /= part;
        const auto attributes = GetFileAttributesW(at.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return false;
        }
    }
    return true;
}

// The two writers the projection detours. Substitution applies only to
// these two fields on this avatar during this export.
bool scene_reference_writers_pinned() {
    const unsigned char neighbor_prefix[] = {0x53, 0x8b, 0x5c, 0x24, 0x08, 0x8b, 0x03, 0x8b,
                                             0x50, 0x20, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x14};
    const unsigned char herd_prefix[] = {0x8b, 0x4c, 0x24, 0x08, 0x8b, 0x09, 0x33, 0xc0,
                                         0x85, 0xc9, 0x74, 0x0c, 0x8b, 0x11, 0x8b, 0x42};
    return !(
        neighbor_writer_rva + sizeof(neighbor_prefix) > image_end - image_base ||
        herd_writer_rva + sizeof(herd_prefix) > image_end - image_base ||
        memcmp(reinterpret_cast<void*>(image_base + neighbor_writer_rva), neighbor_prefix, sizeof(neighbor_prefix)) ||
        memcmp(reinterpret_cast<void*>(image_base + herd_writer_rva), herd_prefix, sizeof(herd_prefix)));
}

std::wstring graph_artifact_name(const GraphExport& graph) {
    return (graph.include_stage              ? L"player-state-"
            : graph.project_scene_references ? L"player-slice-"
                                             : L"player-graph-") +
           std::to_wstring(graph.request) + L".db";
}

// An export never replaces an artifact: the path must be reported as not found, and any other answer refuses the
// export.
bool file_absent(const std::filesystem::path& path) {
    return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool read_scene_references(Simulator::cCreatureAnimal* avatar, SceneReferences& references) {
    references.neighbor_count = avatar->mNeighbors.size();
    if (references.neighbor_count > references.neighbors.size()) {
        return false;
    }
    for (size_t i = 0; i < references.neighbor_count; ++i) {
        references.neighbors[i] = avatar->mNeighbors[i].get();
    }
    references.herd = avatar->mHerd.get();
    return true;
}

bool scene_references_unchanged(Simulator::cCreatureAnimal* avatar, const SceneReferences& references) {
    bool unchanged = avatar->mNeighbors.size() == references.neighbor_count && avatar->mHerd.get() == references.herd;
    for (size_t i = 0; unchanged && i < references.neighbor_count; ++i) {
        unchanged = avatar->mNeighbors[i].get() == references.neighbors[i];
    }
    return unchanged;
}

void graph_export_enter_event(const GraphExport& graph) {
    event("native_player_graph_export_enter",
          ",\"request\":%llu,\"scene_references_projected\":%s,\"player_native_id\":%u,\"avatar_native_id\":%u,"
          "\"unique_game_id\":%u,\"home_planet\":%u",
          graph.request, graph.project_scene_references ? "true" : "false", graph.player_id, graph.avatar_id,
          graph.game_id, graph.home);
}

void graph_herd_event(uint64_t request, const Simulator::cHerd* herd) {
    event("native_player_graph_herd",
          ",\"request\":%llu,\"native_id\":%u,\"archetype\":%u,\"archetype_group\":%u,\"generation\":%d,"
          "\"personality\":%u,\"species_instance\":%u,\"species_type\":%u,\"species_group\":%u",
          request, herd->mID, herd->mArchetype, herd->mArchetypeGroup, herd->mGeneration,
          static_cast<uint32_t>(herd->mCreaturePersonality), herd->mOwnerSpeciesKey.instanceID,
          herd->mOwnerSpeciesKey.typeID, herd->mOwnerSpeciesKey.groupID);
}

// OpenWriteStream returns one caller-owned reference; native
// CloseWriteStream only invokes Close and does not Release.
bool close_write_stream(Simulator::SerializerDatabase* database, Simulator::ISerializerWriteStream* stream) {
    const bool closed = database->CloseWriteStream(stream);
    stream->Release();
    return closed;
}

// The header words, then the player and the avatar as the two roots of the graph.
bool write_graph_roots(Simulator::SerializerDatabase* database, const GraphExport& graph) {
    bool root_good = false;
    Simulator::ISerializerWriteStream* stream = nullptr;
    const ResourceKey key(1, 0x53504d47, 0x53504d50);
    if (database->OpenWriteStream(key, &stream, true) && stream) {
        // Schema 3 adds the original native stage record; schema 2 has
        // the same absent source-scene references without that record.
        const uint32_t words[] = {0x5030314d,      graph.include_stage ? 3u : graph.project_scene_references ? 2u : 1u,
                                  graph.player_id, graph.game_id,
                                  graph.avatar_id, graph.home};
        if (IO::WriteUInt32(stream->GetRecord()->GetStream(), words, _countof(words))) {
            // Use the original IsGood stream status after both writes,
            // as in the verified M09 checkpoint path.
            stream->WritePointer(graph.player);
            stream->WritePointer(graph.avatar);
            root_good = stream->IsGood();
        }
    }
    if (stream) {
        root_good = close_write_stream(database, stream) && root_good;
    }
    return root_good;
}

// The original native stage record of the exported player, in its own stream.
bool write_graph_stage(Simulator::SerializerDatabase* database, const GraphExport& graph) {
    bool stage_good = false;
    Simulator::ISerializerWriteStream* stream = nullptr;
    if (database->OpenWriteStream(ResourceKey(1, 0x53504d53, 0x53504d50), &stream, true) && stream) {
        const auto selection = graph.selection;
        stage_good = selection ? write_native_selected_transfer_stage(graph.request, selection->epoch,
                                                                      selection->pause_request, selection->native_slot,
                                                                      graph.player_id, graph.avatar_id, stream)
                               : write_native_primary_transfer_stage(graph.request, stream);
    }
    if (stream) {
        stage_good = close_write_stream(database, stream) && stage_good;
    }
    return stage_good;
}

// Detours the two writers so that this avatar's neighbors and herd are written empty. True when attached.
bool attach_graph_projection(uint64_t request, Simulator::cCreatureAnimal* avatar) {
    neighbor_writer_original = reinterpret_cast<NeighborWriter>(image_base + neighbor_writer_rva);
    herd_writer_original = reinterpret_cast<HerdWriter>(image_base + herd_writer_rva);
    const auto hook_status = toggle_graph_projection(true);
    graph_projection_attached = hook_status == NO_ERROR;
    if (graph_projection_attached) {
        graph_neighbors = &avatar->mNeighbors;
        graph_herd = &avatar->mHerd;
    }
    event("native_player_graph_projection_attach", ",\"request\":%llu,\"status\":%ld", request, hook_status);
    return graph_projection_attached;
}

// Removes the two detours. True when they are gone and each field was substituted exactly once.
bool detach_graph_projection(uint64_t request) {
    const auto hook_status = toggle_graph_projection(false);
    if (hook_status == NO_ERROR) {
        graph_projection_attached = false;
    }
    event("native_player_graph_projection_detach",
          ",\"request\":%llu,\"status\":%ld,\"neighbor_fields\":%u,\"herd_fields\":%u", request, hook_status,
          graph_neighbor_writes, graph_herd_writes);
    return !graph_projection_attached && graph_neighbor_writes == 1 && graph_herd_writes == 1;
}

// One record per key the serializer wrote. False when the list disagrees with its own count or exceeds 4096 keys.
bool record_graph_keys(Simulator::SerializerDatabase* database, uint64_t request, size_t& keys) {
    eastl::vector<ResourceKey> list;
    keys = database->GetKeyList(list);
    if (keys != list.size() || keys > 4096) {
        return false;
    }
    for (const auto& entry : list) {
        event("native_player_graph_key", ",\"request\":%llu,\"instance\":%u,\"type\":%u,\"group\":%u", request,
              entry.instanceID, entry.typeID, entry.groupID);
    }
    return true;
}

// Into an opened serializer database: the roots, the stage record if asked, then every object the roots reach.
// The projection is attached only around the original SaveClassObjects.
void write_player_graph(Simulator::SerializerDatabase* database, const GraphExport& graph, GraphWrite& written) {
    written.root_good = write_graph_roots(database, graph);
    if (written.root_good && graph.include_stage) {
        written.stage_good = write_graph_stage(database, graph);
    }
    bool projection_ready = !graph.project_scene_references;
    graph_neighbor_writes = 0;
    graph_herd_writes = 0;
    if (written.root_good && graph.project_scene_references) {
        projection_ready = attach_graph_projection(graph.request, graph.avatar);
    }
    if (written.root_good && written.stage_good && projection_ready) {
        written.classes_good = database->SaveClassObjects();
    }
    graph_neighbors = nullptr;
    graph_herd = nullptr;
    if (graph_projection_attached) {
        const bool projected_once = detach_graph_projection(graph.request);
        written.classes_good = written.classes_good && projected_once;
    }
    if (written.classes_good) {
        written.classes_good = record_graph_keys(database, graph.request, written.keys);
    }
}

// Pinned SDK cbf9206 and GOG 3.1.0.29 native stars-save path BB4D40:
// construct and initialize packed/serializer DB, OpenAsSerializer(false,true), write and
// close roots, SaveClassObjects, Close, Release. This observes the native
// transitive graph; it does not assume that graph is portable to a scene.
void write_graph_artifact(const std::filesystem::path& path, const GraphExport& graph, GraphWrite& written) {
    DatabasePackedFilePtr packed = new Resource::DatabasePackedFile(reinterpret_cast<const char16_t*>(path.c_str()));
    SerializerDatabasePtr database = new Simulator::SerializerDatabase(packed.get());
    written.initialized = database->Initialize();
    written.opened = written.initialized && database->OpenAsSerializer(false, true);
    written.stage_good = !graph.include_stage;
    if (written.opened) {
        write_player_graph(database.get(), graph, written);
    }
    // COM retains the serializer DB; explicit Close breaks that cycle even
    // after a failed write, while this external reference is still held.
    written.closed = database->Close();
    database = nullptr;
    packed = nullptr;
}

// Where a new artifact of this export goes, once the serializer the export drives is the pinned one. False when
// the directory is not admissible, a projection is still attached, or the file already exists.
bool admit_graph_artifact(const GraphExport& graph, std::filesystem::path& path) {
    std::filesystem::path root;
    if (!graph_serializer_pinned() || !diagnostics_root(root) || graph_projection_attached ||
        (graph.project_scene_references && !scene_reference_writers_pinned())) {
        return false;
    }
    path = root / graph_artifact_name(graph);
    return file_absent(path);
}

// The identity and scene references of the pair as they stand before the first write, observed and recorded.
bool snapshot_graph_source(GraphExport& graph, SceneReferences& references) {
    graph.player_id = graph.player->mID;
    graph.game_id = graph.player->mUniqueGameID;
    graph.avatar_id = graph.avatar->mID;
    if (!read_scene_references(graph.avatar, references) ||
        !observe_native_player_checkpoint(graph.request, "graph_before")) {
        return false;
    }
    graph_export_enter_event(graph);
    if (references.herd) {
        graph_herd_event(graph.request, references.herd);
    }
    return true;
}

// Whether the game still holds the same primary pair, the selection still names the exported pair, and the
// exported pair kept its identity and was not destroyed.
bool graph_identity_unchanged(const GraphExport& graph) {
    const auto selection = graph.selection;
    uint32_t selected_player_after = 0, selected_avatar_after = 0;
    const bool selection_unchanged =
        !selection ||
        (native_actor_transfer_selection(selection->epoch, selection->native_slot, selection->expected_unique_game,
                                         selected_player_after, selected_avatar_after) &&
         selected_player_after == graph.player_id && selected_avatar_after == graph.avatar_id);
    return graph.nouns->GetPlayer() == graph.primary_player && graph.nouns->GetAvatar() == graph.primary_avatar &&
           selection_unchanged && graph.player->mID == graph.player_id &&
           graph.player->mUniqueGameID == graph.game_id && graph.avatar->mID == graph.avatar_id &&
           !graph.player->mbIsDestroyed && !graph.avatar->mbIsDestroyed;
}

// Writes the player and avatar, with everything the original serializer reaches from them, to a new private
// artifact. The export succeeds only if every step did and the source is found unchanged afterwards.
bool export_player_graph(uint64_t request, bool project_scene_references, bool include_stage,
                         const NativePlayerGraphSelection* selection) {
    if (include_stage && !project_scene_references) {
        return false;
    }
    if (!on_thread() || !request || snapshot.state != State::loaded || Simulator::GetGameModeID() != kGameCreature ||
        Simulator::IsLoadingGameMode()) {
        return false;
    }
    GraphExport graph;
    graph.request = request;
    graph.project_scene_references = project_scene_references;
    graph.include_stage = include_stage;
    graph.selection = selection;
    std::filesystem::path path;
    SceneReferences references;
    if (!resolve_graph_actors(graph) || !admit_graph_artifact(graph, path) ||
        !snapshot_graph_source(graph, references)) {
        return false;
    }
    GraphWrite written;
    write_graph_artifact(path, graph, written);
    std::error_code size_error;
    const auto bytes = std::filesystem::file_size(path, size_error);
    const bool unchanged = graph_identity_unchanged(graph);
    const bool census = observe_native_player_checkpoint(request, "graph_after");
    const bool references_unchanged = scene_references_unchanged(graph.avatar, references);
    event("native_player_graph_scene_references",
          ",\"request\":%llu,\"source_neighbors\":%zu,\"source_herd_present\":%s,\"unchanged\":%s", request,
          references.neighbor_count, references.herd ? "true" : "false", references_unchanged ? "true" : "false");
    const bool result = written.opened && written.root_good && written.stage_good && written.classes_good &&
                        written.closed && unchanged && census && references_unchanged && !size_error && bytes > 0 &&
                        bytes <= (selection ? 1u : 80u) * 1024u * 1024u;
    event("native_player_graph_export_return",
          ",\"request\":%llu,\"initialized\":%s,\"opened\":%s,\"root_good\":%s,\"classes_good\":%s,\"closed\":%s,"
          "\"source_identity_unchanged\":%s,\"keys\":%zu,\"bytes\":%llu,\"success\":%s,\"transfer_qualified\":false",
          request, written.initialized ? "true" : "false", written.opened ? "true" : "false",
          written.root_good ? "true" : "false", written.classes_good ? "true" : "false",
          written.closed ? "true" : "false", unchanged && census ? "true" : "false", written.keys,
          size_error ? 0 : static_cast<uint64_t>(bytes), result ? "true" : "false");
    mark_native_global_observer(request, native_actor_worker_status().epoch, NativeGlobalObservationPhase::checkpoint);
    return result;
}
} // namespace

bool export_native_player_graph(uint64_t request, bool project_scene_references, bool include_stage) {
    return export_player_graph(request, project_scene_references, include_stage, nullptr);
}

bool export_native_selected_player_state(uint64_t request, const NativePlayerGraphSelection& selection) {
    return export_player_graph(request, true, true, &selection);
}

namespace {
// Forgets the original routines. One that a hook could not be detached from stays bound, because that detour still
// calls it.
void forget_originals() {
    if (!save_probe_attached) {
        save_original = nullptr;
    }
    if (!record_probe_attached) {
        open_record_original = nullptr;
    }
    if (!write_resource_probe_attached) {
        write_resource_original = nullptr;
    }
    if (!serializer_probe_attached) {
        serializer_open_original = nullptr;
    }
    if (!serializer_database_probe_attached) {
        serializer_database_open_original = nullptr;
    }
    load_original = nullptr;
    reset_original = nullptr;
    reset_manager_get = nullptr;
}
} // namespace

// Detaches every hook, the save probe last, after the write trace is flushed and summarised; records a request
// that was still pending; then closes the interface.
void dispose_native_persistence() {
    if (!on_thread()) {
        return;
    }
    graph_neighbors = nullptr;
    graph_herd = nullptr;
    detach_probe(toggle_graph_projection, graph_projection_attached,
                 "native_player_graph_projection_detach_on_dispose");
    detach_probe(toggle_write_resource_probe, write_resource_probe_attached,
                 "native_save_resource_probe_detached_on_dispose");
    detach_probe(toggle_serializer_database_probe, serializer_database_probe_attached,
                 "native_save_serializer_database_probe_detached_on_dispose");
    detach_probe(toggle_serializer_probe, serializer_probe_attached,
                 "native_save_serializer_probe_detached_on_dispose");
    detach_probe(toggle_record_probe, record_probe_attached, "native_save_record_probe_detached_on_dispose");
    flush_probe_records();
    if (save_probe_attached) {
        event("native_save_record_probe_summary",
              ",\"write_open_count\":%u,\"selected_write_open_count\":%u,\"flushed\":%u,\"trace_limit\":%u",
              probe_all_writes.load(), probe_write_count.load(), probe_flushed, save_probe_limit);
    }
    detach_probe(toggle_save_probe, save_probe_attached, "native_save_probe_detach");
    dispose_native_checkpoint();
    if (native_persistence_busy()) {
        event("native_persistence_disposed_pending", ",\"request\":%llu,\"state\":%llu", snapshot.request,
              static_cast<uint64_t>(snapshot.state));
    }
    available = false;
    forget_originals();
    snapshot.state = State::unavailable;
    event_sink = nullptr;
}
} // namespace sporemp
