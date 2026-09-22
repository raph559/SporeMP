#include "native_content.h"
#include "native_content_abi.h"
#include "content_png.h"
#include "diagnostics.h"
#include <Spore/Editors/cCreatureDataResource.h>
#include <Spore/ResourceKey.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <filesystem>
#include <vector>

namespace sporemp {
namespace {
using Data = Editors::cCreatureDataResource;
using Abi = NativeContentAbi<ResourceKey, Data>;
using LookupAbi = NativeContentLookupAbi<ResourceKey>;
using ImportAbi = NativeContentImportAbi<ResourceKey>;
constexpr uint32_t load_rva = 0x0bb500, release_rva = 0x033020, data_vtable_rva = 0x0feed80;
constexpr uint32_t crt_type = 0x2b978c46, data_type = 0x0f43029a;
constexpr uint32_t prop_type = 0x00b1b104;
constexpr uint32_t app_get_rva = 0x4de4b0, creation_get_rva = 0x27dc90;
constexpr uint32_t find_database_rva = 0x4de620, find_record_rva = 0x4dfc80, location_rva = 0x642700;
constexpr uint32_t import_get_rva = 0x1f79c0, import_rva = 0x1fc3c0, import_vtable_rva = 0xffa0e0;
constexpr uint32_t max_rigblocks = 512, max_capabilities = 4096;
uintptr_t image_base = 0;
size_t image_size = 0;
DWORD engine_thread = 0;
NativeContentEvent event_sink = nullptr;
Abi::Load load_original = nullptr;
Abi::Release release_original = nullptr;
uint64_t last_request = 0;
bool available = false, inspecting = false;
bool lookup_available = false;
bool import_available = false;
uint64_t last_import = 0;

bool on_thread() { return engine_thread && GetCurrentThreadId() == engine_thread; }
void event(const char* name, const char* format = "", ...) {
    if (!on_thread() || !event_sink) return;
    char fields[1024]{};
    va_list args; va_start(args, format);
    const auto written = vsprintf_s(fields, format, args); va_end(args);
    if (written >= 0) event_sink(name, fields);
}
bool readable(const void* address, size_t size) {
    if (!size) return true;
    uintptr_t cursor = reinterpret_cast<uintptr_t>(address);
    if (!cursor || size > std::numeric_limits<uintptr_t>::max() - cursor) return false;
    const uintptr_t end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const DWORD access = info.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t region = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (cursor < region || info.RegionSize > std::numeric_limits<uintptr_t>::max() - region) return false;
        const uintptr_t region_end = region + info.RegionSize;
        if (region_end <= cursor) return false;
        cursor = region_end < end ? region_end : end;
    }
    return true;
}
bool image_span(uint32_t rva, size_t size) {
    return image_base && rva <= image_size && size <= image_size - rva &&
        readable(reinterpret_cast<const void*>(image_base + rva), size);
}
template<size_t N> bool prefix(uint32_t rva, const unsigned char (&bytes)[N]) {
    return image_span(rva, N) && !std::memcmp(reinterpret_cast<const void*>(image_base + rva), bytes, N);
}
bool getter_prefix(uint32_t rva, uint32_t global_rva) {
    unsigned char bytes[] = {0xa1,0,0,0,0,0xc3};
    if (!image_span(global_rva, 4)) return false;
    const auto address = static_cast<uint32_t>(image_base + global_rva);
    std::memcpy(bytes + 1, &address, sizeof(address));
    return prefix(rva, bytes);
}
bool image_pointer(uintptr_t address, size_t size) {
    return address >= image_base && address - image_base <= UINT32_MAX &&
        image_span(static_cast<uint32_t>(address - image_base), size);
}
bool virtual_entry(void* object, size_t offset, uint32_t expected_rva, size_t object_size) {
    if (!readable(object, object_size)) return false;
    uintptr_t table = 0, entry = 0;
    std::memcpy(&table, object, sizeof(table));
    if (!image_pointer(table, offset + sizeof(entry))) return false;
    std::memcpy(&entry, reinterpret_cast<const void*>(table + offset), sizeof(entry));
    return entry == image_base + expected_rva;
}
bool lookup_receiver(void* manager) {
    // Both getters must independently return the audited native class. Do not
    // substitute one singleton for the other or assume identical precedence.
    return virtual_entry(manager, 0x30, find_record_rva, 0x178) &&
        virtual_entry(manager, 0x58, find_database_rva, 0x178);
}
bool database_leaf(void* database, char (&leaf)[128]) {
    if (!virtual_entry(database, 0x28, location_rva, 0x24)) return false;
    const auto path = reinterpret_cast<LookupAbi::GetLocation>(image_base + location_rva)(database);
    size_t length = 0;
    bool component_valid = true;
    for (size_t i = 0; i < 1024; ++i) {
        const auto address = reinterpret_cast<uintptr_t>(path);
        if (address > UINTPTR_MAX - (i + 1) * sizeof(char16_t)) return false;
        const auto character = reinterpret_cast<const void*>(address + i * sizeof(char16_t));
        if (!readable(character, sizeof(char16_t))) return false;
        char16_t c = 0; std::memcpy(&c, character, sizeof(c));
        if (!c) {
            if (!component_valid || !length) return false;
            leaf[length] = 0; return true;
        }
        if (c == u'/' || c == u'\\') { length = 0; component_valid = true; continue; }
        // Log only an ASCII filename, never an account/path or native pointer.
        if (c < 32 || c > 126 || c == u'"' || c == u':') {
            // A drive prefix or non-ASCII parent is legal, but it cannot become
            // a reported leaf. A subsequent separator starts a clean component.
            component_valid = false;
        } else if (length < sizeof(leaf) - 1) leaf[length++] = static_cast<char>(c);
        else component_valid = false;
    }
    return false;
}
bool observe_record(void* manager, const char* manager_name, const char* purpose,
    const ResourceKey& key, uint64_t request) {
    if (!lookup_receiver(manager)) {
        event("content_record_unavailable", ",\"request\":%llu,\"manager\":\"%s\",\"purpose\":\"%s\",\"reason\":\"manager_vtable_unqualified\",\"readiness\":false",
            request, manager_name, purpose);
        return false;
    }
    const auto exact = reinterpret_cast<LookupAbi::FindDatabase>(image_base + find_database_rva)(manager, key);
    ResourceKey resolved(0,0,0);
    const auto mapped = reinterpret_cast<LookupAbi::FindRecord>(image_base + find_record_rva)(manager, key, &resolved, nullptr);
    char exact_leaf[128]{}, mapped_leaf[128]{};
    const bool exact_location = exact && database_leaf(exact, exact_leaf);
    const bool mapped_location = mapped && database_leaf(mapped, mapped_leaf);
    if (!exact_location) exact_leaf[0] = 0;
    if (!mapped_location) mapped_leaf[0] = 0;
    event("content_record_observed", ",\"request\":%llu,\"manager\":\"%s\",\"purpose\":\"%s\",\"instance\":%u,\"type\":%u,\"group\":%u,\"exact_found\":%s,\"mapped_found\":%s,\"resolved_instance\":%u,\"resolved_type\":%u,\"resolved_group\":%u,\"exact_location_qualified\":%s,\"mapped_location_qualified\":%s,\"exact_database\":\"%s\",\"mapped_database\":\"%s\",\"readiness\":false",
        request, manager_name, purpose, key.instanceID, key.typeID, key.groupID,
        exact ? "true" : "false", mapped ? "true" : "false", resolved.instanceID, resolved.typeID, resolved.groupID,
        exact_location ? "true" : "false", mapped_location ? "true" : "false", exact_leaf, mapped_leaf);
    // No resource factory/load call and no owned return: never AddRef/Release
    // these borrowed managers, databases or location strings.
    return exact && mapped && exact_location && mapped_location && resolved.instanceID==key.instanceID &&
        resolved.typeID==key.typeID && resolved.groupID==key.groupID;
}
bool observe_record_pair(const char* purpose, const ResourceKey& key, uint64_t request) {
    if (!lookup_available) {
        event("content_record_unavailable", ",\"request\":%llu,\"purpose\":\"%s\",\"reason\":\"binding_unavailable\",\"readiness\":false", request, purpose);
        return false;
    }
    void* app = reinterpret_cast<LookupAbi::Get>(image_base + app_get_rva)();
    void* creation = reinterpret_cast<LookupAbi::Get>(image_base + creation_get_rva)();
    const bool app_found = observe_record(app, "app", purpose, key, request);
    const bool creation_found = observe_record(creation, "creation", purpose, key, request);
    event("content_record_managers", ",\"request\":%llu,\"purpose\":\"%s\",\"same_manager\":%s", request, purpose, app == creation ? "true" : "false");
    return app_found && creation_found;
}
bool owned_result(Data* value) {
    if (!readable(value, sizeof(Data)) || !image_span(data_vtable_rva, 8)) return false;
    uintptr_t vtable = 0, release = 0;
    std::memcpy(&vtable, value, sizeof(vtable));
    std::memcpy(&release, reinterpret_cast<const void*>(image_base + data_vtable_rva + 4), sizeof(release));
    return vtable == image_base + data_vtable_rva && release == image_base + release_rva;
}
template<class T> bool vector_span(const void* vector, uint32_t limit, NativeContentSpan& span, uint32_t& count) {
    std::memcpy(&span, vector, sizeof(span));
    return native_content_span_count(span, sizeof(T), limit, count) &&
        readable(reinterpret_cast<const void*>(static_cast<uintptr_t>(span.begin)), static_cast<size_t>(count) * sizeof(T));
}
struct BlockCopy {
    uint32_t group = 0, instance = 0;
    int index = 0, parent = 0, symmetric = 0, flags = 0, type = 0, capability_start = 0, capability_count = 0;
};
struct CapabilityCopy { unsigned char tag[4]{}; int level = 0; };
struct InspectionCopy {
    ResourceKey key;
    uint32_t model_type = 0, block_count = 0, capability_count = 0;
    std::array<BlockCopy, max_rigblocks> blocks{};
    std::array<CapabilityCopy, max_capabilities> capabilities{};
};
const char* copy_result(Data* value, uint32_t instance, uint32_t group, InspectionCopy& copy) {
    copy.key = value->GetResourceKey();
    if (copy.key.instanceID != instance || copy.key.typeID != data_type || copy.key.groupID != group)
        return "derived_resource_key_mismatch";
    copy.model_type = value->mProperties.mModelType;
    NativeContentSpan blocks{}, tags{}, levels{}; uint32_t level_count = 0;
    if (!vector_span<Data::RigblockData>(&value->mRigblocks, max_rigblocks, blocks, copy.block_count))
        return "rigblock_vector_invalid_or_over_budget";
    if (!vector_span<Data::CapabilityTag>(&value->mCapabilityIDs, max_capabilities, tags, copy.capability_count))
        return "capability_tag_vector_invalid_or_over_budget";
    if (!vector_span<int8_t>(&value->mCapabilityLevels, max_capabilities, levels, level_count))
        return "capability_level_vector_invalid_or_over_budget";
    if (copy.capability_count != level_count) return "capability_vector_count_mismatch";
    if (!copy.block_count) return "empty_creature_rigblocks";
    for (uint32_t i = 0; i < copy.block_count; ++i) {
        Data::RigblockData block{};
        std::memcpy(&block, reinterpret_cast<const void*>(static_cast<uintptr_t>(blocks.begin) + i * sizeof(block)), sizeof(block));
        if (!native_content_capability_range(block.mCapabilityIndex, block.mNumCapabilities, copy.capability_count))
            return "rigblock_capability_range_invalid";
        auto& dst = copy.blocks[i];
        dst.group = block.mGroupID; dst.instance = block.mInstanceID;
        dst.index = block.mIndex; dst.parent = block.mParentIndex; dst.symmetric = block.mSymmetricIndex;
        dst.flags = block.mFlags; dst.type = static_cast<int>(block.mType);
        dst.capability_start = block.mCapabilityIndex; dst.capability_count = block.mNumCapabilities;
    }
    for (uint32_t i = 0; i < copy.capability_count; ++i) {
        std::memcpy(copy.capabilities[i].tag, reinterpret_cast<const void*>(static_cast<uintptr_t>(tags.begin) + i * 4), 4);
        int8_t level = 0;
        std::memcpy(&level, reinterpret_cast<const void*>(static_cast<uintptr_t>(levels.begin) + i), 1);
        copy.capabilities[i].level = level;
    }
    return nullptr;
}
bool fail(uint64_t request, const char* reason, bool released = false) {
    event("content_inspection_failed", ",\"request\":%llu,\"reason\":\"%s\",\"owned_release_completed\":%s,\"readiness\":false",
        request, reason, released ? "true" : "false");
    return false;
}
}

bool initialize_native_content(uintptr_t base, size_t size, DWORD thread, NativeContentEvent sink) {
    image_base = base; image_size = size; engine_thread = thread; event_sink = sink;
    available = false; inspecting = false; last_request = 0; load_original = nullptr; release_original = nullptr;
    lookup_available = false;
    import_available = false; last_import = 0;
    if (!on_thread() || !size || size > std::numeric_limits<uintptr_t>::max() - base) return false;
    static_assert(sizeof(void*) == 4 && sizeof(Data) == 0x128, "Pinned Win32 native layout");
    const unsigned char load_bytes[] = {0x55,0x8b,0xec,0x81,0xec,0xa4,0,0,0,0xe8,0x82,0x27,0x1c,0};
    const unsigned char release_bytes[] = {0x55,0x8b,0xec,0x83,0xec,0x14,0x89,0x4d,0xf0,0xb8,0xfe,0xff,0xff,0xff};
    if (!prefix(load_rva, load_bytes) || !prefix(release_rva, release_bytes) || !image_span(data_vtable_rva, 8)) {
        event("content_binding_rejected", ",\"native_execution_qualified\":false"); return false;
    }
    uintptr_t release = 0;
    std::memcpy(&release, reinterpret_cast<const void*>(base + data_vtable_rva + 4), sizeof(release));
    if (release != base + release_rva) { event("content_binding_rejected", ",\"reason\":\"release_vtable_mismatch\""); return false; }
    load_original = reinterpret_cast<Abi::Load>(base + load_rva);
    release_original = reinterpret_cast<Abi::Release>(base + release_rva);
    available = true;
    event("content_bindings_checked", ",\"code_prefixes\":2,\"native_execution_qualified\":false,\"readiness\":false");
    const unsigned char find_database_bytes[] = {0x8b,0x49,0x2c,0x55,0x57,0x8b,0x39,0x33,0xed,0x85,0xff,0x74,0x39};
    const unsigned char find_record_bytes[] = {0x83,0xec,0x14,0x53,0x56,0x8b,0x74,0x24,0x20,0x57,0x8b,0xd9,0x8d,0x46,0x04};
    const unsigned char location_bytes[] = {0x8b,0x41,0x20,0xc3};
    lookup_available = getter_prefix(app_get_rva, 0x1267aa0) && getter_prefix(creation_get_rva, 0x11fd894) &&
        prefix(find_database_rva, find_database_bytes) && prefix(find_record_rva, find_record_bytes) && prefix(location_rva, location_bytes);
    event(lookup_available ? "content_lookup_bindings_checked" : "content_lookup_binding_rejected",
        ",\"code_prefixes\":5,\"native_execution_qualified\":false,\"readiness\":false");
    const unsigned char import_bytes[] = {0x81,0xec,0x10,0x03,0,0,0x53,0x55,0x56,0x8b,0xb4,0x24,0x24,0x03,0,0};
    unsigned char import_constructor[] = {0x56,0x8b,0xf1,0xc7,0x06,0,0,0,0};
    const auto import_vtable = static_cast<uint32_t>(base + import_vtable_rva);
    std::memcpy(import_constructor + 5, &import_vtable, 4);
    import_available = getter_prefix(import_get_rva, 0x11f1f38) && prefix(import_rva, import_bytes) &&
        prefix(0x1fcfe0, import_constructor) && image_span(import_vtable_rva, 8);
    event(import_available ? "content_import_bindings_checked" : "content_import_binding_rejected",
        ",\"code_prefixes\":3,\"native_execution_qualified\":false,\"readiness\":false");
    return true;
}

bool inspect_native_content(uint32_t instance, uint32_t type, uint32_t group, uint64_t request, network::ContentObservation* copied) {
    if (!on_thread()) return false; // The trace sink is also engine-thread-only.
    if (!available || !load_original || !release_original) return fail(request, "binding_unavailable");
    if (inspecting) return fail(request, "inspection_reentrant");
    if (!request || request <= last_request) return fail(request, "request_not_fresh");
    if (type != crt_type || !instance || instance == UINT32_MAX || group == UINT32_MAX)
        return fail(request, "unsupported_creation_key");
    last_request = request; inspecting = true;
    event("content_inspection_begin", ",\"request\":%llu,\"instance\":%u,\"type\":%u,\"group\":%u,\"may_generate_native_cache\":true",
        request, instance, type, group);
    ResourceKey key(instance, type, group);
    observe_record_pair("creation", key, request);
    Data* raw = nullptr;
    const bool loaded = load_original(&key, &raw);
    if (!raw) { inspecting = false; return fail(request, loaded ? "native_success_without_result" : "native_load_failed"); }
    if (!owned_result(raw)) {
        // An unexpected result cannot safely be released through guessed code.
        // Disable further probes, disclose the unreleased result, retain no pointer.
        available = false; inspecting = false; return fail(request, "unexpected_result_release_unqualified");
    }
    InspectionCopy copy;
    const char* error = loaded ? copy_result(raw, instance, group, copy) : "native_failure_with_result";
    // LoadCreatureData transfers one reference on successful cache/new/variant
    // branches. 433020 is its actual vtable Release: no extra AddRef or delete.
    release_original(raw);
    raw = nullptr;
    inspecting = false;
    if (error) return fail(request, error, true);
    if (copied) {
        copied->native_key={group,instance,type};copied->model_type=copy.model_type;
        copied->blocks.clear();copied->capabilities.clear();copied->resolved_parts.clear();
        for(uint32_t i=0;i<copy.block_count;++i) {
            const auto& b=copy.blocks[i];network::ContentBlock out;
            out.part={b.group,b.instance,prop_type};out.index=b.index;out.parent=b.parent;out.symmetric=b.symmetric;
            out.flags=b.flags;out.block_type=b.type;out.capability_start=b.capability_start;out.capability_count=b.capability_count;
            copied->blocks.push_back(out);
        }
        for(uint32_t i=0;i<copy.capability_count;++i) {
            network::ContentCapability out;std::memcpy(out.tag.data(),copy.capabilities[i].tag,4);out.level=copy.capabilities[i].level;
            copied->capabilities.push_back(out);
        }
    }
    uint32_t unique_parts = 0, exact_parts = 0;
    for (uint32_t i = 0; i < copy.block_count; ++i) {
        const auto& block = copy.blocks[i];
        bool duplicate = false;
        for (uint32_t j = 0; j < i; ++j)
            if (copy.blocks[j].instance == block.instance && copy.blocks[j].group == block.group) { duplicate = true; break; }
        if (duplicate) continue;
        ++unique_parts;
        if (observe_record_pair("rigblock", ResourceKey(block.instance, prop_type, block.group), request)) {
            ++exact_parts;if(copied)copied->resolved_parts.push_back({block.group,block.instance,prop_type});
        }
    }
    event("content_part_records_complete", ",\"request\":%llu,\"unique_parts\":%u,\"exact_parts_in_both_managers\":%u,\"dependency_closure_verified\":false,\"readiness\":false",
        request, unique_parts, exact_parts);
    for (uint32_t i = 0; i < copy.block_count; ++i) {
        const auto& block = copy.blocks[i];
        event("content_rigblock", ",\"request\":%llu,\"ordinal\":%u,\"group\":%u,\"instance\":%u,\"index\":%d,\"parent\":%d,\"symmetric\":%d,\"flags\":%d,\"block_type\":%d,\"capability_start\":%d,\"capability_count\":%d",
            request, i, block.group, block.instance, block.index, block.parent, block.symmetric, block.flags, block.type, block.capability_start, block.capability_count);
    }
    for (uint32_t i = 0; i < copy.capability_count; ++i) {
        const auto& cap = copy.capabilities[i];
        event("content_capability", ",\"request\":%llu,\"ordinal\":%u,\"tag_hex\":\"%02x%02x%02x%02x\",\"native_level\":%d",
            request, i, static_cast<unsigned>(cap.tag[0]), static_cast<unsigned>(cap.tag[1]), static_cast<unsigned>(cap.tag[2]), static_cast<unsigned>(cap.tag[3]), cap.level);
    }
    event("content_inspection_complete", ",\"request\":%llu,\"instance\":%u,\"type\":%u,\"group\":%u,\"model_type\":%u,\"rigblocks\":%u,\"capabilities\":%u,\"owned_release_completed\":true,\"gameplay_validation\":false,\"readiness\":false",
        request, copy.key.instanceID, copy.key.typeID, copy.key.groupID, copy.model_type, copy.block_count, copy.capability_count);
    return true;
}

void inspect_native_terrain_records(uint32_t instance, uint32_t type, uint32_t group, uint64_t request) {
    if (!on_thread() || !available || inspecting || !request || !instance || instance == UINT32_MAX || group == UINT32_MAX) return;
    observe_record_pair("terrain", ResourceKey(instance, type, group), request);
    // Original cTerrainResourceManager::GetPropertyList (F93500) passes the
    // terrain instance/group to IPropManager; the serialized record is .prop.
    // Observe existence only. Do not call its generate/save fallback.
    observe_record_pair("terrain_properties", ResourceKey(instance, prop_type, group), request);
}

namespace {
bool import_native_content_file(const std::array<uint64_t, 10>& sha_words, uint64_t request,
    network::ContentObservation* copied,bool addressed) {
    if (!on_thread()) return false;
    const auto reject = [&](const char* reason) {
        event("content_import_rejected", ",\"request\":%llu,\"reason\":\"%s\",\"native_call_made\":false,\"readiness\":false", request, reason);
        return false;
    };
    if (!available || !import_available || !lookup_available) return reject("binding_unavailable");
    if (inspecting) return reject("inspection_reentrant");
    if (!request || request <= last_import || request <= last_request) return reject("request_not_fresh");
    last_import = request;
    if (sha_words[8] || sha_words[9]) return reject("invalid_hash_words");
    Sha256 expected{};
    constexpr char hex[] = "0123456789abcdef";
    bool nonzero = false;
    for (size_t i = 0; i < 8; ++i) {
        if (sha_words[i] > UINT32_MAX) return reject("invalid_hash_words");
        nonzero = nonzero || sha_words[i] != 0;
        for (size_t j = 0; j < 4; ++j) {
            const auto byte = static_cast<uint8_t>(sha_words[i] >> (j * 8));
            expected[(i * 4 + j) * 2] = hex[byte >> 4];
            expected[(i * 4 + j) * 2 + 1] = hex[byte & 15];
        }
    }
    if (!nonzero) return reject("empty_hash");
    wchar_t directory[32768]{};
    const auto length = GetEnvironmentVariableW(L"SPOREMP_DIAGNOSTICS_DIR", directory, _countof(directory));
    if (!length || length >= _countof(directory) || !absolute_local_path(directory)) return reject("quarantine_directory_unavailable");
    std::wstring name=L"m08-import.png";
    if(addressed) {name=L"content-";for(size_t i=0;i<64;++i)name+=wchar_t(expected[i]);name+=L".png";}
    const auto path = std::filesystem::path(directory) / name;
    std::filesystem::path at;
    for (const auto& part : path) {
        if (part == L"." || part == L".." || (part != path.root_name() && part.native().find(L':') != std::wstring::npos))
            return reject("quarantine_unsafe_path");
        at /= part;
        const auto attributes = GetFileAttributesW(at.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return reject("quarantine_missing_or_unsafe_path");
    }
    struct HeldFile {
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~HeldFile() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } file;
    file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.handle == INVALID_HANDLE_VALUE) return reject("quarantine_not_closed_or_readable");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.handle, &info) || info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY) || info.nNumberOfLinks != 1)
        return reject("quarantine_unsafe_file");
    if (info.nFileSizeHigh || info.nFileSizeLow > max_creation_png_bytes || info.nFileSizeLow < 57) return reject("png_size_limit");
    Sha256 actual{};
    if (!sha256_file(path.c_str(), actual) || actual != expected) return reject("quarantine_sha256_mismatch");
    std::vector<uint8_t> bytes(info.nFileSizeLow);
    DWORD read = 0;
    if (!ReadFile(file.handle, bytes.data(), info.nFileSizeLow, &read, nullptr) || read != info.nFileSizeLow) return reject("quarantine_read_failed");
    if (const auto reason = creation_png_envelope(bytes.data(), bytes.size())) return reject(reason);
    void* receiver = reinterpret_cast<LookupAbi::Get>(image_base + import_get_rva)();
    uintptr_t table = 0;
    if (!readable(receiver, 0x188)) return reject("import_manager_unavailable");
    std::memcpy(&table, receiver, sizeof(table));
    if (table != image_base + import_vtable_rva) return reject("import_manager_vtable_unqualified");
    // Single engine-thread call. Keep the read lock until both the original
    // importer and the subsequent loader have returned; no paths cross IPC.
    inspecting = true;
    event("content_import_begin", ",\"request\":%llu,\"png_sha256\":\"%s\",\"png_bytes\":%lu,\"readiness\":false", request, actual.data(), read);
    ResourceKey key(UINT32_MAX, UINT32_MAX, UINT32_MAX);
    const bool imported = reinterpret_cast<ImportAbi::Import>(image_base + import_rva)(receiver,
        reinterpret_cast<const char16_t*>(path.c_str()), key);
    event("content_import_returned", ",\"request\":%llu,\"native_result\":%s,\"instance\":%u,\"type\":%u,\"group\":%u,\"native_commit_validation\":false,\"readiness\":false",
        request, imported ? "true" : "false", key.instanceID, key.typeID, key.groupID);
    inspecting = false;
    if (!imported) return false; // False can return an existing key; never call it a new import.
    if (!key.instanceID || key.instanceID == UINT32_MAX || key.typeID != crt_type || key.groupID == UINT32_MAX) {
        event("content_import_unqualified", ",\"request\":%llu,\"reason\":\"imported_type_or_key_unqualified\",\"readiness\":false", request);
        return false;
    }
    return inspect_native_content(key.instanceID, key.typeID, key.groupID, request,copied);
}
}
bool import_native_content(const std::array<uint64_t, 10>& sha_words, uint64_t request) {
    return import_native_content_file(sha_words,request,nullptr,false);
}
bool import_native_content_blob(const network::Digest& hash,uint64_t request,network::ContentObservation& copied) {
    std::array<uint64_t,10> words{};
    for(size_t i=0;i<8;++i)for(size_t j=0;j<4;++j)words[i]|=uint64_t(hash[i*4+j])<<(j*8);
    const bool imported=import_native_content_file(words,request,&copied,true);
    if(imported)copied.png=hash;
    return imported;
}
uint64_t next_native_content_request() {
    if(!on_thread()||last_request==UINT64_MAX||last_import==UINT64_MAX)return 0;
    return (last_request>last_import?last_request:last_import)+1;
}
bool check_native_content_parts(const std::vector<network::ContentKey>& parts,uint64_t request,network::ContentKey& missing) {
    if(!on_thread()||!lookup_available||inspecting||!request||request<=last_request||parts.empty()||parts.size()>max_rigblocks)return false;
    last_request=request;
    for(const auto& part:parts) {
        if(!part.group||!part.instance||part.type!=prop_type||
           !observe_record_pair("required_part",ResourceKey(part.instance,part.type,part.group),request)) {
            missing=part;return false;
        }
    }
    event("content_required_parts_complete",",\"request\":%llu,\"parts\":%u,\"before_import\":true",request,static_cast<unsigned>(parts.size()));
    return true;
}

void dispose_native_content() {
    if (!on_thread() || inspecting) return;
    available = false; load_original = nullptr; release_original = nullptr; last_request = 0;
    lookup_available = false;
    import_available = false; last_import = 0;
    event_sink = nullptr; image_base = 0; image_size = 0; engine_thread = 0;
}
}
