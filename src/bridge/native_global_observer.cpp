#include "native_global_observer.h"
#include "detour_transaction.h"
#include <Spore/Simulator/SubSystem/GameModeManager.h>
#include <Spore/Simulator/cPlanetRecord.h>
#include <Spore/Simulator/cStarRecord.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <vector>

namespace sporemp {
namespace {
// ABI/prefix witnesses: local/m03-static/m10-global-20260926-01..12,
// documented in global-producers/STATIC-AUDIT.md. Preferred image base 400000.
// SDK cbf9206b9a823f0911cd9be0217104a49d72380b; executable SHA256
// dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37.
enum Producer : size_t {
    universe,
    relationship_tick,
    relationship_apply,
    spice,
    generate_star,
    generate_missing,
    star_update,
    star_post,
    count
};

constexpr uint32_t rvas[count] = {0xc14cd0, 0x906c70, 0x906280, 0xc3ca50, 0x7a8980, 0x7b77a0, 0x7a71a0, 0x7a71d0};
constexpr const char* names[count] = {"universe_update",     "relationship_tick", "relationship_apply",
                                      "assign_planet_spice", "generate_star",     "generate_missing",
                                      "star_update",         "star_postupdate"};
using TickCall = void(__thiscall*)(void*, int, int);
using ApplyCall = float(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, float);
using SpiceCall = void(__thiscall*)(void*, Simulator::cPlanetRecord*, bool);
using GenerateCall = void(__thiscall*)(void*, Simulator::cStarRecord*);
using WalkCall = void(__thiscall*)(void*);
TickCall universe_original = nullptr, relationship_original = nullptr, star_original = nullptr, post_original = nullptr;
ApplyCall apply_original = nullptr;
SpiceCall spice_original = nullptr;
GenerateCall generate_original = nullptr;
WalkCall walk_original = nullptr;
uintptr_t base = 0, image_end = 0, seen_manager = 0;
DWORD engine_thread = 0;
NativeGlobalObserverEvent sink = nullptr;
bool attached = false, reporting = false;
NativeGlobalAdmission admission;
std::atomic<bool> guard_armed{false};
std::atomic<bool> guard_pending{false};
bool (*preload_live)() = nullptr;
HANDLE supervisor_handle = nullptr;
bool guard_bindings = false;
std::array<uint64_t, count> guard_owner_calls{}, guard_reader_calls{};
uint64_t spice_noop_calls = 0;
uint64_t started_ms = 0, last_report_ms = 0, cached_epoch = 0, cached_app = 0, mark_id = 0;

struct Counts {
    std::atomic<uint64_t> entries{0}, returns{0}, foreign{0}, creature{0}, other_modes{0};
};

std::array<Counts, count> counters;
std::array<uint32_t, count> last_mode{};
std::array<bool, count> sampled{};

struct Sample {
    uint64_t ordinal = 0, epoch = 0, app = 0, ms = 0;
    uint32_t producer = 0, thread = 0, mode = 0, caller = 0, vtable = 0, args[4]{};
    uint32_t before[16]{}, after[16]{};
    bool before_valid = false, after_valid = false;
    bool guard_active = false, guard_owner = false, guard_noop = false;
    uint64_t grant_nonce = 0;
};

// Only the verified engine thread accesses this ring. Other threads are counted
// atomically, never dereferenced or queued. A dropped sample invalidates any
// claim of complete mutation detail, but never drops the original native call.
std::array<Sample, 512> samples{}, draining{};
size_t queued = 0;
uint64_t dropped = 0;

bool on_thread() {
    return attached && GetCurrentThreadId() == engine_thread;
}

uint32_t bits(float value) {
    uint32_t out;
    memcpy(&out, &value, 4);
    return out;
}

uint32_t image_rva(uintptr_t address) {
    return address >= base && address < image_end ? static_cast<uint32_t>(address - base) : 0;
}

bool readable(const void* address, size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const auto at = reinterpret_cast<uintptr_t>(address), start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    return at >= start && size <= info.RegionSize && at - start <= info.RegionSize - size;
}

template <class T>
bool read(uintptr_t address, T& out) {
    if (!readable(reinterpret_cast<void*>(address), sizeof(T))) {
        return false;
    }
    memcpy(&out, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
}

uint32_t object_vtable(const void* object) {
    uintptr_t table = 0;
    return read(reinterpret_cast<uintptr_t>(object), table) ? image_rva(table) : 0;
}

[[noreturn]] void deny_producer(Producer producer, const char* reason, uint32_t planet = 0, bool entered = false) {
    // Arming is restricted to a verified disposable authority. A rejected native
    // callback cannot return an invented result or continue until a posted quit.
    if (GetCurrentThreadId() == engine_thread && sink) {
        const auto& context = admission.context();
        const auto& grant = admission.grant();
        char fields[1200]{};
        sprintf_s(fields,
                  ",\"producer\":\"%s\",\"reason\":\"%s\",\"worker\":%llu,\"worker_generation\":%llu,"
                  "\"checkpoint_revision\":%llu,\"grant_nonce\":%llu,\"planet_id\":%u,\"original_body_entered\":%s,"
                  "\"failure_exit\":19732,\"native_global_ownership_qualified\":false",
                  names[producer], reason, context.worker, context.generation, context.checkpoint_revision, grant.nonce,
                  planet, entered ? "true" : "false");
        sink("native_global_producer_denied", fields);
    }
    TerminateProcess(GetCurrentProcess(), 0x4d14);
    __fastfail(7);
}

bool transport_live() {
    return preload_live && supervisor_handle && WaitForSingleObject(supervisor_handle, 0) == WAIT_TIMEOUT &&
           preload_live();
}

NativeGlobalPermit guard_entry(Producer producer, void* self, Sample& sample) {
    if (!guard_armed.load(std::memory_order_acquire)) {
        if (guard_pending.load(std::memory_order_acquire)) {
            deny_producer(producer, "startup_admission_not_completed");
        }
        return NativeGlobalPermit::off;
    }
    // No non-atomic grant data is accessed by an unexpected native thread.
    if (GetCurrentThreadId() != engine_thread) {
        deny_producer(producer, "foreign_engine_thread");
    }
    const auto permission = admission.permit(GetTickCount64(), transport_live());
    if (permission == NativeGlobalPermit::denied || permission == NativeGlobalPermit::off) {
        deny_producer(producer, "no_live_authenticated_grant");
    }
    const auto mode = Simulator::GetGameModeID();
    if (mode != kGameCreature && mode != 0x02ccd1d2) {
        deny_producer(producer, "unsupported_native_mode");
    }
    constexpr uint32_t tables[count] = {0x1095750, 0x1078d9c, 0x1078d9c, 0x1099b38,
                                        0x1065fe8, 0x1065fe8, 0x1065fe8, 0x1065fe8};
    if (object_vtable(self) != tables[producer]) {
        deny_producer(producer, "producer_object_binding");
    }
    sample.guard_active = true;
    sample.guard_owner = permission == NativeGlobalPermit::owner;
    sample.grant_nonce = admission.grant().nonce;
    if (permission == NativeGlobalPermit::owner) {
        ++guard_owner_calls[producer];
    } else {
        if (producer == universe || producer == relationship_tick || producer == relationship_apply ||
            producer == generate_star) {
            deny_producer(producer, "global_producer_not_owned");
        }
        if (producer == star_post) {
            uintptr_t relationship = 0, table = 0, target = 0;
            if (!read(reinterpret_cast<uintptr_t>(self) + 0x204, relationship) || !read(relationship, table) ||
                table != base + 0x1078d9c || !read(table + 0x10, target) || target != base + 0x7267d0) {
                deny_producer(producer, "relationship_postupdate_not_original_noop");
            }
        }
        ++guard_reader_calls[producer];
    }
    return permission;
}

bool begin_sample(Producer producer, void* self, uintptr_t caller, Sample& sample, bool always = false) {
    auto& values = counters[producer];
    const auto ordinal = values.entries.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto thread = GetCurrentThreadId();
    if (thread != engine_thread) {
        values.foreign.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const auto mode = Simulator::GetGameModeID();
    (mode == kGameCreature ? values.creature : values.other_modes).fetch_add(1, std::memory_order_relaxed);
    if (!always && sampled[producer] && last_mode[producer] == mode) {
        return false;
    }
    sampled[producer] = true;
    last_mode[producer] = mode;
    sample.ordinal = ordinal;
    sample.epoch = cached_epoch;
    sample.app = cached_app;
    sample.ms = GetTickCount64() - started_ms;
    sample.producer = static_cast<uint32_t>(producer);
    sample.thread = thread;
    sample.mode = mode;
    sample.caller = image_rva(caller);
    sample.vtable = object_vtable(self);
    return true;
}

void finish(Producer producer, const Sample& sample, bool selected) {
    counters[producer].returns.fetch_add(1, std::memory_order_relaxed);
    if (!selected) {
        return;
    }
    if (queued == samples.size()) {
        ++dropped;
        return;
    }
    samples[queued++] = sample;
}

bool planet_values(Simulator::cPlanetRecord* planet, uint32_t* words) {
    if (!readable(planet, sizeof(*planet)) || !object_vtable(planet)) {
        return false;
    }
    const auto& key = planet->mSpiceGen;
    words[0] = planet->mKey.internalValue;
    words[1] = static_cast<uint32_t>(planet->mType);
    words[2] = static_cast<uint32_t>(planet->mFlags);
    words[3] = static_cast<uint32_t>(planet->mTechLevel);
    words[4] = key.instanceID;
    words[5] = key.typeID;
    words[6] = key.groupID;
    words[7] = planet->mGeneratedTerrainKey.instanceID;
    words[8] = planet->mGeneratedTerrainKey.typeID;
    words[9] = planet->mGeneratedTerrainKey.groupID;
    words[10] = bits(planet->mRotationAxis.x);
    words[11] = bits(planet->mRotationAxis.y);
    words[12] = bits(planet->mRotationAxis.z);
    words[13] = bits(planet->mRotationPeriod);
    words[14] = bits(planet->mAtmosphereScore);
    words[15] = bits(planet->mTemperatureScore);
    return true;
}

bool star_values(Simulator::cStarRecord* star, uint32_t* words) {
    if (!readable(star, sizeof(*star)) || !object_vtable(star)) {
        return false;
    }
    words[0] = star->mKey.internalValue;
    words[1] = static_cast<uint8_t>(star->mPlanetCount);
    words[2] = static_cast<uint32_t>(star->mFlags);
    words[3] = star->mEmpireID;
    words[4] = static_cast<uint32_t>(star->mSavedGameVersionMajor);
    words[5] = static_cast<uint32_t>(star->mSavedGameVersionMinor);
    for (size_t i = 0; i < 9; ++i) {
        words[6 + i] = static_cast<uint32_t>(star->mSavedGameTimeStamp[i]);
    }
    words[15] = static_cast<uint32_t>(star->mTechLevel);
    return true;
}

void remember_manager(void* self) {
    if (GetCurrentThreadId() == engine_thread && object_vtable(self) == 0x1065fe8) {
        seen_manager = reinterpret_cast<uintptr_t>(self);
    }
}

void __fastcall universe_hook(void* self, void*, int a, int b) {
    Sample sample;
    const bool selected = begin_sample(universe, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample);
    sample.args[0] = a;
    sample.args[1] = b;
    if (selected) {
        sample.before_valid = read(reinterpret_cast<uintptr_t>(self) + 0x14, sample.before[0]);
    }
    guard_entry(universe, self, sample);
    universe_original(self, a, b);
    if (selected) {
        sample.after_valid = read(reinterpret_cast<uintptr_t>(self) + 0x14, sample.after[0]);
    }
    finish(universe, sample, selected);
}

void __fastcall relationship_hook(void* self, void*, int a, int b) {
    Sample sample;
    const bool selected = begin_sample(relationship_tick, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample);
    sample.args[0] = a;
    sample.args[1] = b;
    if (selected) {
        sample.before_valid = read(reinterpret_cast<uintptr_t>(self) + 0x44, sample.before[0]);
    }
    guard_entry(relationship_tick, self, sample);
    relationship_original(self, a, b);
    if (selected) {
        sample.after_valid = read(reinterpret_cast<uintptr_t>(self) + 0x44, sample.after[0]);
    }
    finish(relationship_tick, sample, selected);
}

float __fastcall apply_hook(void* self, void*, uint32_t a, uint32_t b, uint32_t event, float scale) {
    Sample sample;
    const bool selected =
        begin_sample(relationship_apply, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample, true);
    sample.args[0] = a;
    sample.args[1] = b;
    sample.args[2] = event;
    sample.args[3] = bits(scale);
    guard_entry(relationship_apply, self, sample);
    const float result = apply_original(self, a, b, event, scale);
    if (selected) {
        sample.after[0] = bits(result);
        sample.after_valid = true;
    }
    finish(relationship_apply, sample, selected);
    return result;
}

void __fastcall spice_hook(void* self, void*, Simulator::cPlanetRecord* planet, bool force) {
    Sample sample;
    const bool selected = begin_sample(spice, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample, true);
    sample.args[0] = force ? 1u : 0u;
    if (selected) {
        sample.before_valid = planet_values(planet, sample.before);
    }
    const auto permission = guard_entry(spice, self, sample);
    if (permission != NativeGlobalPermit::off) {
        std::array<uint32_t, 3> original_default{};
        if (!sample.before_valid || !read(base + 0x12df56c, original_default) ||
            original_default != std::array<uint32_t, 3>{}) {
            deny_producer(spice, "planet_or_default_binding");
        }
        if (permission == NativeGlobalPermit::reader) {
            const std::array<uint32_t, 3> key = {sample.before[4], sample.before[5], sample.before[6]};
            if (!NativeGlobalAdmission::spice_noop(force, key, original_default)) {
                deny_producer(spice, "unowned_spice_would_mutate", sample.before[0]);
            }
            sample.guard_noop = true;
            ++spice_noop_calls;
        }
    }
    spice_original(self, planet, force);
    if (selected) {
        sample.after_valid = planet_values(planet, sample.after);
    }
    if (sample.guard_noop && (!sample.after_valid || memcmp(sample.before, sample.after, sizeof(sample.before)))) {
        deny_producer(spice, "original_noop_changed_record", sample.before[0], true);
    }
    finish(spice, sample, selected);
}

void __fastcall generate_hook(void* self, void*, Simulator::cStarRecord* star) {
    remember_manager(self);
    Sample sample;
    const bool selected =
        begin_sample(generate_star, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample, true);
    if (selected) {
        sample.before_valid = star_values(star, sample.before);
    }
    guard_entry(generate_star, self, sample);
    generate_original(self, star);
    if (selected) {
        sample.after_valid = star_values(star, sample.after);
    }
    finish(generate_star, sample, selected);
}

void __fastcall walk_hook(void* self, void*) {
    remember_manager(self);
    Sample sample;
    const bool selected =
        begin_sample(generate_missing, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample, true);
    guard_entry(generate_missing, self, sample);
    walk_original(self);
    finish(generate_missing, sample, selected);
}

void __fastcall star_hook(void* self, void*, int a, int b) {
    remember_manager(self);
    Sample sample;
    const bool selected = begin_sample(star_update, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample);
    guard_entry(star_update, self, sample);
    sample.args[0] = a;
    sample.args[1] = b;
    star_original(self, a, b);
    finish(star_update, sample, selected);
}

void __fastcall post_hook(void* self, void*, int a, int b) {
    remember_manager(self);
    Sample sample;
    const bool selected = begin_sample(star_post, self, reinterpret_cast<uintptr_t>(_ReturnAddress()), sample);
    guard_entry(star_post, self, sample);
    sample.args[0] = a;
    sample.args[1] = b;
    post_original(self, a, b);
    finish(star_post, sample, selected);
}

NativeHook hooks[] = {{reinterpret_cast<void**>(&universe_original), reinterpret_cast<void*>(universe_hook)},
                      {reinterpret_cast<void**>(&relationship_original), reinterpret_cast<void*>(relationship_hook)},
                      {reinterpret_cast<void**>(&apply_original), reinterpret_cast<void*>(apply_hook)},
                      {reinterpret_cast<void**>(&spice_original), reinterpret_cast<void*>(spice_hook)},
                      {reinterpret_cast<void**>(&generate_original), reinterpret_cast<void*>(generate_hook)},
                      {reinterpret_cast<void**>(&walk_original), reinterpret_cast<void*>(walk_hook)},
                      {reinterpret_cast<void**>(&star_original), reinterpret_cast<void*>(star_hook)},
                      {reinterpret_cast<void**>(&post_original), reinterpret_cast<void*>(post_hook)}};

const char* phase_name(NativeGlobalObservationPhase phase) {
    switch (phase) {
    case NativeGlobalObservationPhase::before_load:
        return "before_load";
    case NativeGlobalObservationPhase::scene_ready:
        return "scene_ready";
    case NativeGlobalObservationPhase::before_save:
        return "before_save";
    case NativeGlobalObservationPhase::after_save:
        return "after_save";
    case NativeGlobalObservationPhase::checkpoint:
        return "checkpoint";
    case NativeGlobalObservationPhase::interval:
        return "interval";
    case NativeGlobalObservationPhase::end:
        return "end";
    }
    return "invalid";
}

void word_array(char* out, size_t capacity, const uint32_t* words, size_t size) {
    size_t offset = 0;
    for (size_t i = 0; i < size; ++i) {
        const int written = sprintf_s(out + offset, capacity - offset, "%s%u", i ? "," : "", words[i]);
        if (written < 0) {
            return;
        }
        offset += static_cast<size_t>(written);
    }
}

void drain() {
    // Called only outside hooks, on the same engine thread. Preserve ring entries
    // raised by a reentrant sink until the next drain rather than overwriting them.
    const auto available = queued;
    queued = 0;
    std::copy_n(samples.begin(), available, draining.begin());
    for (size_t i = 0; i < available; ++i) {
        const auto& sample = draining[i];
        char before[192]{}, after[192]{}, fields[1200]{};
        word_array(before, sizeof(before), sample.before, 16);
        word_array(after, sizeof(after), sample.after, 16);
        sprintf_s(fields,
                  ",\"producer\":\"%s\",\"entry_ordinal\":%llu,\"epoch_at_last_app_update\":%llu,"
                  "\"app_at_last_update\":%llu,\"elapsed_ms\":%llu,\"thread\":%u,\"mode\":%u,\"caller_rva\":%u,"
                  "\"object_vtable_rva\":%u,\"args\":[%u,%u,%u,%u],\"before_valid\":%s,\"before\":[%s],"
                  "\"after_valid\":%s,\"after\":[%s],\"original_returned\":true,\"global_guard_active\":%s,"
                  "\"global_guard_owner\":%s,\"global_guard_original_noop\":%s,\"grant_nonce\":%llu",
                  names[sample.producer], sample.ordinal, sample.epoch, sample.app, sample.ms, sample.thread,
                  sample.mode, sample.caller, sample.vtable, sample.args[0], sample.args[1], sample.args[2],
                  sample.args[3], sample.before_valid ? "true" : "false", before, sample.after_valid ? "true" : "false",
                  after, sample.guard_active ? "true" : "false", sample.guard_owner ? "true" : "false",
                  sample.guard_noop ? "true" : "false", sample.grant_nonce);
        sink("native_global_callback", fields);
    }
}

// What a walk over the ticker's lists has seen so far.
struct TickerWalk {
    std::array<uintptr_t, 512> seen{};
    size_t total = 0;
    unsigned universes = 0, active = 0, pending = 0;
};

struct TickerNode {
    uintptr_t forward = 0, object = 0, table = 0, update = 0;
};

// One list node: new to the walk, linked back to the node before it, and holding an object whose vtable and
// update entry lie inside the image.
bool read_ticker_node(uintptr_t address, uintptr_t previous, const TickerWalk& walk, TickerNode& node) {
    uintptr_t back = 0;
    if (walk.total == walk.seen.size() ||
        std::find(walk.seen.begin(), walk.seen.begin() + walk.total, address) != walk.seen.begin() + walk.total ||
        !read(address, node.forward) || !read(address + 4, back) || back != previous ||
        !read(address + 8, node.object) || !readable(reinterpret_cast<void*>(node.object), 0x10) ||
        !read(node.object, node.table) || !image_rva(node.table) || !read(node.table + 0x10, node.update) ||
        !image_rva(node.update)) {
        return false;
    }
    // Duplicate simulator objects are separately detected even if list nodes differ.
    for (size_t i = 0; i < walk.total; ++i) {
        uintptr_t old_object = 0;
        if (!read(walk.seen[i] + 8, old_object) || old_object == node.object) {
            return false;
        }
    }
    return true;
}

void record_ticker_entry(uint64_t request, uint32_t list, size_t ordinal, const TickerNode& node, bool is_universe) {
    unsigned char flags[3]{};
    memcpy(flags, reinterpret_cast<void*>(node.object + 0xc), sizeof(flags));
    char fields[480]{};
    sprintf_s(fields,
              ",\"mark\":%llu,\"request\":%llu,\"observed_epoch\":%llu,\"list\":%u,"
              "\"ordinal\":%u,\"vtable_rva\":%u,\"update_rva\":%u,\"flags\":[%u,%u,%u],\"universe\":%s",
              mark_id, request, cached_epoch, list, static_cast<unsigned>(ordinal), image_rva(node.table),
              image_rva(node.update), flags[0], flags[1], flags[2], is_universe ? "true" : "false");
    sink("native_global_ticker_entry", fields);
}

// Walks one of the ticker's two lists, the active one (0) or the pending one (1), and records every entry.
// False when the list is not a consistent doubly linked list of distinct image objects.
bool walk_ticker_list(uintptr_t ticker, uint32_t list, uint64_t request, TickerWalk& walk) {
    const auto sentinel = ticker + (list ? 0x28 : 0x1c);
    uintptr_t next = 0, previous = sentinel, tail = 0;
    if (!read(sentinel, next) || !read(sentinel + 4, tail)) {
        return false;
    }
    while (next != sentinel) {
        TickerNode node;
        if (!read_ticker_node(next, previous, walk, node)) {
            return false;
        }
        walk.seen[walk.total++] = next;
        if (list) {
            ++walk.pending;
        } else {
            ++walk.active;
        }
        const bool is_universe = node.table == base + 0x1095750 && node.update == base + rvas[universe];
        if (is_universe) {
            ++walk.universes;
        }
        record_ticker_entry(request, list, walk.total, node, is_universe);
        previous = next;
        next = node.forward;
    }
    return previous == tail;
}

// Every entry of the ticker's two lists. True when the ticker is absent or both lists were walked completely.
bool ticker_census(uint64_t request) {
    uintptr_t ticker = 0;
    bool valid = read(base + 0x127eaf0, ticker);
    const bool present = ticker != 0;
    valid = valid && (!present || (readable(reinterpret_cast<void*>(ticker), 0x44) &&
                                   object_vtable(reinterpret_cast<void*>(ticker)) == 0x1065bd0));
    TickerWalk walk;
    for (uint32_t list = 0; valid && present && list < 2; ++list) {
        valid = walk_ticker_list(ticker, list, request, walk);
    }
    char fields[480]{};
    sprintf_s(fields,
              ",\"mark\":%llu,\"request\":%llu,\"observed_epoch\":%llu,\"present\":%s,"
              "\"complete\":%s,\"active\":%u,\"pending\":%u,\"universe_count\":%u,\"deferred_removals_excluded\":true",
              mark_id, request, cached_epoch, present ? "true" : "false", valid ? "true" : "false", walk.active,
              walk.pending, walk.universes);
    sink("native_global_ticker_census", fields);
    return valid;
}

bool pointer_vector(uintptr_t address, std::vector<uintptr_t>& values, size_t bound) {
    uintptr_t first = 0, last = 0, limit = 0;
    if (!read(address, first) || !read(address + 4, last) || !read(address + 8, limit) || first > last ||
        last > limit || (last - first) % 4 || (last - first) / 4 > bound ||
        (first && !readable(reinterpret_cast<void*>(first), last - first))) {
        return false;
    }
    if (!first) {
        return !last && !limit;
    }
    for (auto at = first; at < last; at += 4) {
        uintptr_t value = 0;
        if (!read(at, value)) {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

// The star records the manager holds, without repeats: its two vectors and three single pointers. False when
// the manager was never seen, one of them cannot be read or there are too many.
bool collect_stars(uintptr_t manager, std::vector<uintptr_t>& stars) {
    bool complete = manager && readable(reinterpret_cast<void*>(manager), 0x22c) &&
                    object_vtable(reinterpret_cast<void*>(manager)) == 0x1065fe8;
    if (complete) {
        complete = pointer_vector(manager + 0xdc, stars, 8192) && pointer_vector(manager + 0xf0, stars, 8192);
    }
    for (const auto offset : {0x148u, 0x14cu, 0x1dcu}) {
        if (complete) {
            uintptr_t value = 0;
            complete = read(manager + offset, value);
            if (value) {
                stars.push_back(value);
            }
        }
    }
    std::sort(stars.begin(), stars.end());
    stars.erase(std::unique(stars.begin(), stars.end()), stars.end());
    if (stars.size() > 8192) {
        complete = false;
    }
    return complete;
}

// The sixteen words of one star or planet record, as star_values or planet_values read them.
void record_state(const char* name, uint64_t request, const uint32_t* words) {
    char serialized[192]{}, fields[400]{};
    word_array(serialized, sizeof(serialized), words, 16);
    sprintf_s(fields, ",\"mark\":%llu,\"request\":%llu,\"observed_epoch\":%llu,\"words\":[%s]", mark_id, request,
              cached_epoch, serialized);
    sink(name, fields);
}

// One row per star, and the planets every star caches. False when a record cannot be read, an identifier
// repeats or there are too many planets.
bool record_stars(uint64_t request, const std::vector<uintptr_t>& stars, std::vector<uintptr_t>& planets,
                  std::vector<uint32_t>& star_ids) {
    for (auto at : stars) {
        uint32_t words[16]{};
        if (!star_values(reinterpret_cast<Simulator::cStarRecord*>(at), words) ||
            !pointer_vector(at + 0x84, planets, 256) || planets.size() > 32768) {
            return false;
        }
        if (std::find(star_ids.begin(), star_ids.end(), words[0]) != star_ids.end()) {
            return false;
        }
        star_ids.push_back(words[0]);
        record_state("native_global_star_state", request, words);
    }
    return true;
}

// One row per planet. False when a record cannot be read or an identifier repeats.
bool record_planets(uint64_t request, const std::vector<uintptr_t>& planets, std::vector<uint32_t>& planet_ids) {
    for (auto at : planets) {
        uint32_t words[16]{};
        if (!planet_values(reinterpret_cast<Simulator::cPlanetRecord*>(at), words) ||
            std::find(planet_ids.begin(), planet_ids.end(), words[0]) != planet_ids.end()) {
            return false;
        }
        planet_ids.push_back(words[0]);
        record_state("native_global_planet_state", request, words);
    }
    return true;
}

// The summary of the census, with the relationship timer and the next political identifier of the manager.
void record_census(uint64_t request, uintptr_t manager, bool complete, size_t stars, size_t planets) {
    uintptr_t relationship = 0;
    uint32_t relationship_timer = 0, next_political = 0;
    const bool relationship_valid = manager && read(manager + 0x204, relationship) && relationship &&
                                    object_vtable(reinterpret_cast<void*>(relationship)) == 0x1078d9c &&
                                    read(relationship + 0x44, relationship_timer);
    const bool next_valid = manager && read(manager + 0x1d4, next_political);
    char fields[640]{};
    sprintf_s(fields,
              ",\"mark\":%llu,\"request\":%llu,\"observed_epoch\":%llu,\"manager_seen\":%s,"
              "\"complete_within_scope\":%s,\"stars\":%u,\"cached_planets\":%u,\"relationship_timer_valid\":%s,"
              "\"relationship_timer\":%u,\"next_political_id_valid\":%s,\"next_political_id\":%u,"
              "\"scope\":\"starter_saved_sol_temp_scenario_stars_cached_planets_only\",\"whole_galaxy\":false",
              mark_id, request, cached_epoch, manager ? "true" : "false", complete ? "true" : "false",
              static_cast<unsigned>(stars), static_cast<unsigned>(planets), relationship_valid ? "true" : "false",
              relationship_timer, next_valid ? "true" : "false", next_political);
    sink("native_global_record_census", fields);
}

// The star and planet records reachable from the last star manager a hook saw. True when every one of them was
// read and recorded.
bool loaded_records(uint64_t request) {
    const auto manager = seen_manager;
    std::vector<uintptr_t> stars;
    bool complete = collect_stars(manager, stars);
    std::vector<uintptr_t> planets;
    std::vector<uint32_t> star_ids, planet_ids;
    if (complete) {
        complete = record_stars(request, stars, planets, star_ids);
    }
    std::sort(planets.begin(), planets.end());
    planets.erase(std::unique(planets.begin(), planets.end()), planets.end());
    if (complete) {
        complete = record_planets(request, planets, planet_ids);
    }
    record_census(request, manager, complete, star_ids.size(), planet_ids.size());
    return complete;
}

void summary(uint64_t request, NativeGlobalObservationPhase phase) {
    for (size_t i = 0; i < count; ++i) {
        char fields[1040]{};
        sprintf_s(
            fields,
            ",\"mark\":%llu,\"request\":%llu,\"observed_epoch\":%llu,\"phase\":\"%s\",\"producer\":\"%s\","
            "\"rva\":%u,\"entries\":%llu,\"returns\":%llu,\"foreign_thread_entries\":%llu,\"creature_entries\":%llu,"
            "\"other_mode_entries\":%llu,\"app_ticks\":%llu,\"elapsed_ms\":%llu,\"dropped_samples\":%llu,"
            "\"attached\":%s,\"suppressed_calls\":0,\"global_ownership_qualified\":false,"
            "\"global_guard_armed\":%s,\"guard_owner_calls\":%llu,\"guard_reader_calls\":%llu,\"guard_spice_noop_"
            "calls\":%llu",
            mark_id, request, cached_epoch, phase_name(phase), names[i], rvas[i], counters[i].entries.load(),
            counters[i].returns.load(), counters[i].foreign.load(), counters[i].creature.load(),
            counters[i].other_modes.load(), cached_app, GetTickCount64() - started_ms, dropped,
            attached ? "true" : "false", guard_armed.load() ? "true" : "false", guard_owner_calls[i],
            guard_reader_calls[i], spice_noop_calls);
        sink("native_global_observer_summary", fields);
    }
    uintptr_t object = 0;
    uint32_t timer = 0, killer = 0, remaining = 0;
    const bool address_valid = read(base + 0x12dc798, object);
    const bool scalar_valid = object && object_vtable(reinterpret_cast<void*>(object)) == 0x1095750 &&
                              read(object + 0x14, timer) && read(object + 0x110, killer) &&
                              read(object + 0x114, remaining);
    char fields[512]{};
    sprintf_s(fields,
              ",\"mark\":%llu,\"request\":%llu,\"observed_epoch\":%llu,\"singleton_readable\":%s,"
              "\"present\":%s,\"scalars_valid\":%s,\"time_since_last_event\":%u,\"killer_political_id\":%u,\"death_"
              "time_bits\":%u,"
              "\"mode\":%u,\"thread\":%u",
              mark_id, request, cached_epoch, address_valid ? "true" : "false", object ? "true" : "false",
              scalar_valid ? "true" : "false", timer, killer, remaining, Simulator::GetGameModeID(),
              GetCurrentThreadId());
    sink("native_global_universe_state", fields);
}

bool prefix(uint32_t rva, const unsigned char* bytes, size_t length) {
    return image_end > base && rva <= image_end - base && length <= image_end - base - rva &&
           readable(reinterpret_cast<void*>(base + rva), length) &&
           !memcmp(reinterpret_cast<void*>(base + rva), bytes, length);
}

bool spice_guard_bindings() {
    // STATIC: B8DC20 exact getter and103CA62..103CA8C exact key/force branch;
    // all three absolute addresses are rebased. Original no-op exits103CCC2.
    const unsigned char getter[] = {0x8d, 0x81, 0x98, 0x01, 0, 0, 0xc3};
    unsigned char branch[] = {0x8b, 0x08, 0x3b, 0x0d, 0,    0,    0,    0,    0x75, 0x16, 0x8b, 0x50, 0x04, 0x3b, 0x15,
                              0,    0,    0,    0,    0x75, 0x0b, 0x8b, 0x40, 0x08, 0x3b, 0x05, 0,    0,    0,    0,
                              0x74, 0x0b, 0x80, 0x7c, 0x24, 0x3c, 0,    0x0f, 0x84, 0x35, 0x02, 0,    0};
    const uint32_t key0 = static_cast<uint32_t>(base + 0x12df56c), key1 = key0 + 4, key2 = key0 + 8;
    memcpy(branch + 4, &key0, 4);
    memcpy(branch + 15, &key1, 4);
    memcpy(branch + 26, &key2, 4);
    const unsigned char tail[] = {0x5f, 0x5b, 0x83, 0xc4, 0x2c, 0xc2, 0x08, 0};
    const unsigned char post[] = {0xc2, 0x08, 0};
    return prefix(0x78dc20, getter, sizeof(getter)) && prefix(0xc3ca62, branch, sizeof(branch)) &&
           prefix(0xc3ccc2, tail, sizeof(tail)) && prefix(0x7267d0, post, sizeof(post));
}

uint64_t filetime_value(const FILETIME& value) {
    return uint64_t(value.dwHighDateTime) << 32 | value.dwLowDateTime;
}

// One of the three worker accounts, with a network configuration and a generation of 32 hexadecimal digits in
// its environment.
bool worker_account_and_session() {
    wchar_t user[256]{}, generation[64]{}, network[2]{};
    DWORD length = _countof(user);
    if (!GetUserNameW(user, &length) ||
        (wcscmp(user, L"SporeMP-M04-01") && wcscmp(user, L"SporeMP-M04-02") && wcscmp(user, L"SporeMP-M04-03")) ||
        !GetEnvironmentVariableW(L"SPOREMP_M06_CONFIG", network, _countof(network)) ||
        GetEnvironmentVariableW(L"SPOREMP_M04_GENERATION", generation, _countof(generation)) != 32) {
        return false;
    }
    for (size_t i = 0; i < 32; ++i) {
        if (!((generation[i] >= L'0' && generation[i] <= L'9') || (generation[i] >= L'a' && generation[i] <= L'f') ||
              (generation[i] >= L'A' && generation[i] <= L'F'))) {
            return false;
        }
    }
    return true;
}

// The process identifier the supervisor left in the environment, decimal digits only. Zero when it is absent
// or malformed.
unsigned long supervisor_process_id() {
    wchar_t parent[32]{};
    const auto length = GetEnvironmentVariableW(L"SPOREMP_M04_SUPERVISOR", parent, _countof(parent));
    if (!length || length >= _countof(parent)) {
        return 0;
    }
    for (DWORD i = 0; i < length; ++i) {
        if (parent[i] < L'0' || parent[i] > L'9') {
            return 0;
        }
    }
    wchar_t* end = nullptr;
    const auto pid = wcstoul(parent, &end, 10);
    if (!pid || !end || *end) {
        return 0;
    }
    return pid;
}

// The admission context must describe this very process: its identifier and its creation time.
bool context_names_another_process(const NativeGlobalAdmissionContext& context) {
    FILETIME created{}, exited{}, kernel{}, used{};
    return context.process_id != GetCurrentProcessId() ||
           !GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &used) ||
           filetime_value(created) != context.process_created;
}

bool token_not_elevated() {
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    const bool standard =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes) && !elevation.TokenIsElevated;
    CloseHandle(token);
    return standard;
}

// A handle on the supervisor, which must be a running SporeMP.NativeHost.exe. Null otherwise. The caller owns
// the handle.
HANDLE open_running_supervisor(unsigned long pid) {
    HANDLE candidate = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!candidate) {
        return nullptr;
    }
    wchar_t path[32768]{};
    DWORD capacity = _countof(path);
    bool valid =
        WaitForSingleObject(candidate, 0) == WAIT_TIMEOUT && QueryFullProcessImageNameW(candidate, 0, path, &capacity);
    const auto leaf = wcsrchr(path, L'\\');
    valid = valid && leaf && !_wcsicmp(leaf + 1, L"SporeMP.NativeHost.exe");
    if (!valid) {
        CloseHandle(candidate);
        return nullptr;
    }
    return candidate;
}

// The guard arms only in a worker account's standard process that a live NativeHost supervises. supervisor
// receives the handle that transport_live then watches.
bool isolated_managed_process(const NativeGlobalAdmissionContext& context, HANDLE& supervisor) {
    if (!worker_account_and_session()) {
        return false;
    }
    const auto pid = supervisor_process_id();
    if (!pid || context_names_another_process(context) || !token_not_elevated()) {
        return false;
    }
    HANDLE candidate = open_running_supervisor(pid);
    if (!candidate) {
        return false;
    }
    supervisor = candidate;
    return true;
}

void grant_event(const char* name) {
    const auto& context = admission.context();
    const auto& grant = admission.grant();
    char fields[1300]{}, head[65]{}, universe_text[65]{};
    for (size_t i = 0; i < 32; ++i) {
        sprintf_s(head + 2 * i, 3, "%02x", context.checkpoint[i]);
        sprintf_s(universe_text + 2 * i, 3, "%02x", context.universe[i]);
    }
    sprintf_s(
        fields,
        ",\"native_role\":%u,\"worker\":%llu,\"worker_generation\":%llu,\"checkpoint_revision\":%llu,\"checkpoint_"
        "sha256\":\"%s\","
        "\"universe\":\"%s\",\"grant_nonce\":%llu,\"producer_mask\":%u,\"global_owner_worker\":%llu,\"global_owner_"
        "generation\":%llu,"
        "\"expires_at_ms\":%llu,\"ttl_ms\":%u,\"global_guard_armed\":true,\"native_global_ownership_qualified\":false",
        static_cast<uint32_t>(context.role), context.worker, context.generation, context.checkpoint_revision, head,
        universe_text, grant.nonce, grant.producer_mask, grant.owners[0].worker, grant.owners[0].generation,
        admission.deadline(), grant.ttl_ms);
    sink(name, fields);
}

// The first bytes of the eight producers, in the order of Producer and rvas.
bool producer_prefixes_pinned() {
    const unsigned char p0[] = {0x83, 0xec, 0x20, 0x56, 0x8b, 0xf1, 0x89, 0x74,
                                0x24, 0x04, 0xe8, 0x71, 0x4f, 0xfc, 0xff};
    const unsigned char p1[] = {0x8b, 0x44, 0x24, 0x08, 0x50, 0xe8, 0x16, 0xfe, 0xff, 0xff, 0xc2, 0x08, 0x00};
    const unsigned char p2[] = {0xd9, 0x44, 0x24, 0x10, 0x8b, 0x44, 0x24, 0x0c, 0x56, 0x51, 0xd9, 0x1c, 0x24, 0x50};
    const unsigned char p3[] = {0x83, 0xec, 0x2c, 0x53, 0x57, 0x8b, 0x7c, 0x24, 0x38,
                                0x8b, 0xd9, 0x8b, 0xcf, 0xe8, 0xbe, 0x11, 0xb5, 0xff};
    const unsigned char p4[] = {0x83, 0xec, 0x1c, 0x53, 0x55, 0x56, 0x8b, 0x74, 0x24, 0x2c, 0x8b, 0xe9, 0x33, 0xdb};
    const unsigned char p5[] = {0x83, 0xec, 0x24, 0x53, 0x55, 0x56, 0x57, 0x8b,
                                0xf9, 0x8b, 0x87, 0xe0, 0x00, 0x00, 0x00};
    const unsigned char p6[] = {0x56, 0x8b, 0xf1, 0xe8, 0x68, 0x47, 0xfb, 0xff, 0x3d, 0x05, 0x4c, 0x65, 0x01};
    const unsigned char p7[] = {0x56, 0x8b, 0xf1, 0xe8, 0xd8, 0x9f, 0x47, 0x00,
                                0x8b, 0xc8, 0xe8, 0xe1, 0x9b, 0x08, 0x00};
    const unsigned char* bytes[] = {p0, p1, p2, p3, p4, p5, p6, p7};
    const size_t sizes[] = {sizeof(p0), sizeof(p1), sizeof(p2), sizeof(p3),
                            sizeof(p4), sizeof(p5), sizeof(p6), sizeof(p7)};
    for (size_t i = 0; i < count; ++i) {
        if (!prefix(rvas[i], bytes[i], sizes[i])) {
            return false;
        }
    }
    return true;
}

// The getter of the ticker singleton that ticker_census reads.
bool ticker_getter_pinned() {
    unsigned char getter[] = {0xa1, 0, 0, 0, 0, 0xc3};
    const auto singleton = static_cast<uint32_t>(base + 0x127eaf0);
    memcpy(getter + 1, &singleton, 4);
    return prefix(0x73d430, getter, sizeof(getter));
}

void bind_originals() {
    universe_original = reinterpret_cast<TickCall>(base + rvas[universe]);
    relationship_original = reinterpret_cast<TickCall>(base + rvas[relationship_tick]);
    apply_original = reinterpret_cast<ApplyCall>(base + rvas[relationship_apply]);
    spice_original = reinterpret_cast<SpiceCall>(base + rvas[spice]);
    generate_original = reinterpret_cast<GenerateCall>(base + rvas[generate_star]);
    walk_original = reinterpret_cast<WalkCall>(base + rvas[generate_missing]);
    star_original = reinterpret_cast<TickCall>(base + rvas[star_update]);
    post_original = reinterpret_cast<TickCall>(base + rvas[star_post]);
}

// The counters, the sample ring and the guard start empty on every attachment.
void reset_observations() {
    for (auto& counter : counters) {
        counter.entries = 0;
        counter.returns = 0;
        counter.foreign = 0;
        counter.creature = 0;
        counter.other_modes = 0;
    }
    sampled = {};
    last_mode = {};
    queued = 0;
    dropped = 0;
    cached_epoch = cached_app = mark_id = 0;
    seen_manager = 0;
    admission = {};
    guard_armed = false;
    preload_live = nullptr;
    guard_owner_calls = {};
    guard_reader_calls = {};
    spice_noop_calls = 0;
    started_ms = last_report_ms = GetTickCount64();
}
} // namespace

bool initialize_native_global_observer(uintptr_t image_base, uintptr_t end, DWORD thread,
                                       NativeGlobalObserverEvent callback) {
    if (attached || !callback || !image_base || end <= image_base || GetCurrentThreadId() != thread) {
        return false;
    }
    base = image_base;
    image_end = end;
    engine_thread = thread;
    sink = callback;
    if (!producer_prefixes_pinned()) {
        return false;
    }
    guard_bindings = spice_guard_bindings();
    if (!ticker_getter_pinned()) {
        return false;
    }
    bind_originals();
    reset_observations();
    if (change_hooks(hooks, _countof(hooks), true) != NO_ERROR) {
        return false;
    }
    attached = true;
    sink("native_global_observer_attached",
         ",\"hooks\":8,\"pass_through\":true,\"native_global_ownership_qualified\":false");
    mark_native_global_observer(0, 0, NativeGlobalObservationPhase::before_load);
    return true;
}

namespace {
bool arm_global_process(const NativeGlobalAdmissionContext& context, bool (*live)()) {
    if (!on_thread() || guard_armed.load() || !live || !guard_bindings || !spice_guard_bindings()) {
        return false;
    }
    HANDLE verified = nullptr;
    if (!isolated_managed_process(context, verified)) {
        return false;
    }
    if (!admission.arm(context)) {
        CloseHandle(verified);
        return false;
    }
    if (supervisor_handle) {
        CloseHandle(supervisor_handle);
    }
    supervisor_handle = verified;
    preload_live = live;
    guard_armed.store(true, std::memory_order_release);
    guard_pending.store(false, std::memory_order_release);
    grant_event("native_global_guard_armed");
    return true;
}
} // namespace

bool arm_native_global_authority(const NativeGlobalAdmissionContext& context, bool (*live)()) {
    return context.role == NativeGlobalAdmissionRole::authority && arm_global_process(context, live);
}

bool prepare_native_global_admission(const NativeGlobalAdmissionContext& context) {
    if (!on_thread() || supervisor_handle || guard_armed.load()) {
        return false;
    }
    HANDLE verified = nullptr;
    if (!isolated_managed_process(context, verified)) {
        return false;
    }
    supervisor_handle = verified;
    guard_pending.store(true, std::memory_order_release);
    return true;
}

bool arm_native_global_reader(const NativeGlobalAdmissionContext& context, bool (*live)()) {
    return context.role == NativeGlobalAdmissionRole::reader && arm_global_process(context, live);
}

bool install_native_global_grant(const NativeGlobalAdmissionGrant& grant) {
    if (!on_thread() || !guard_armed.load() || !admission.install(grant, GetTickCount64(), transport_live())) {
        return false;
    }
    grant_event("native_global_grant_installed");
    return true;
}

void revoke_native_global_grant(const char* reason) {
    if (!on_thread() || !guard_armed.load()) {
        return;
    }
    (void)reason;
    admission.revoke();
    grant_event("native_global_grant_revoked");
}

bool native_global_grant_ready() {
    if (!on_thread() || !guard_armed.load()) {
        return false;
    }
    const auto result = admission.permit(GetTickCount64(), transport_live());
    return result == NativeGlobalPermit::owner || result == NativeGlobalPermit::reader;
}

bool native_global_guard_armed() {
    return guard_armed.load(std::memory_order_acquire);
}

void update_native_global_observer(uint64_t app_ticks, uint64_t epoch) {
    if (!on_thread() || reporting) {
        return;
    }
    cached_app = app_ticks;
    cached_epoch = epoch;
    reporting = true;
    drain();
    reporting = false;
    if (GetTickCount64() - last_report_ms >= 10000) {
        mark_native_global_observer(0, epoch, NativeGlobalObservationPhase::interval);
    }
}

bool mark_native_global_observer(uint64_t request, uint64_t epoch, NativeGlobalObservationPhase phase) {
    if (!on_thread() || reporting) {
        return false;
    }
    reporting = true;
    cached_epoch = epoch;
    ++mark_id;
    last_report_ms = GetTickCount64();
    drain();
    summary(request, phase);
    const bool ticker_ok = ticker_census(request);
    const bool records_ok = phase == NativeGlobalObservationPhase::interval ? true : loaded_records(request);
    reporting = false;
    return ticker_ok && records_ok;
}

bool dispose_native_global_observer() {
    if (!attached) {
        return true;
    }
    if (!on_thread() || reporting) {
        return false;
    }
    mark_native_global_observer(0, cached_epoch, NativeGlobalObservationPhase::end);
    if (change_hooks(hooks, _countof(hooks), false) != NO_ERROR) {
        sink("native_global_observer_detach_failed", ",\"still_attached\":true");
        return false;
    }
    guard_pending.store(false, std::memory_order_release);
    guard_armed.store(false, std::memory_order_release);
    preload_live = nullptr;
    admission = {};
    if (supervisor_handle) {
        CloseHandle(supervisor_handle);
        supervisor_handle = nullptr;
    }
    attached = false;
    drain();
    summary(0, NativeGlobalObservationPhase::end);
    sink("native_global_observer_detached", ",\"detached\":true,\"native_global_ownership_qualified\":false");
    return true;
}
} // namespace sporemp
