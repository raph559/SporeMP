#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <array>
#include "../network/content_registry.h"

namespace sporemp {
using NativeContentEvent = void(*)(const char*, const char*);
// Developer-only original saved-creature inspection. The caller must first
// qualify executable/content/loader identity, isolated worker and fixture key.
// Initialization guards code only. Every operation stays on the app-update
// thread; no native pointers or objects are retained or returned across IPC.
bool initialize_native_content(uintptr_t base, size_t image_size, DWORD thread, NativeContentEvent sink);
// Synchronous, one native load per unique increasing request. LoadCreatureData
// can generate native caches. True means bounded scalar observation and one
// owned Release completed, not gameplay validation or multiplayer readiness.
bool inspect_native_content(uint32_t instance, uint32_t type, uint32_t group, uint64_t request_id,
    network::ContentObservation* copied=nullptr);
// Called only after a qualified native world load, on the same engine thread.
// Resolves both the observed terrain identity and its original property-list
// lookup (instance/group, .prop). Does not load or generate terrain or approve it.
void inspect_native_terrain_records(uint32_t instance, uint32_t type, uint32_t group, uint64_t request_id);
// Explicit isolated developer mutation. Reads only <diagnostics>/m08-import.png,
// locks it, checks its SHA-256 and bounded original PNG envelope, then calls the
// original importer once. Return means imported and inspected, never committed.
bool import_native_content(const std::array<uint64_t, 10>& sha_words, uint64_t request_id);
// Same qualified original import binding, fixed content-addressed quarantine
// name under diagnostics. Caller supplies locally approved bytes and fills the
// verified installed/world identities; returned data contains only native values.
bool import_native_content_blob(const network::Digest&,uint64_t request_id,network::ContentObservation&);
uint64_t next_native_content_request();
// Read-only exact/mapped native record lookup, before any PNG import. A false
// result identifies the first unavailable required property record.
bool check_native_content_parts(const std::vector<network::ContentKey>&,uint64_t request_id,network::ContentKey& missing);
void dispose_native_content();
}
