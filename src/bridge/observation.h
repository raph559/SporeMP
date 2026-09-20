#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace sporemp {
// Opaque addresses are lookup keys inside this process only. Never serialized or dereferenced here.
struct ObservedEntity { uintptr_t key = 0; uint64_t id = 0; };
class EntityTable {
public:
    static constexpr size_t capacity = 8192;
    ObservedEntity observe(uintptr_t key) noexcept;
    ObservedEntity find(uintptr_t key) const noexcept;
    ObservedEntity invalidate(uintptr_t key) noexcept;
    void finish_destroy(uintptr_t key) noexcept;
    void clear() noexcept;
    bool live(ObservedEntity token) const noexcept;
private:
    enum class State { empty, live, destroying, retired };
    struct Slot { uintptr_t key = 0; uint64_t id = 0; State state = State::empty; };
    std::array<Slot, capacity> slots_{};
    uint64_t next_id_ = 0; // Never reset by scene changes; reused addresses receive a fresh ID.
    size_t index(uintptr_t key) const noexcept;
};

enum class ObservationKind {
    trace_start, trace_stop, hooks_ready, hooks_failed, scene_enter, scene_exit, stage_observed,
    entity_created, entity_observed, entity_invalidated, avatar_assigned, avatar_state,
    jump_enter, jump_return, jump_land, ai_summary, diagnostic_snapshot, overflow
};
struct Observation {
    ObservationKind kind = ObservationKind::diagnostic_snapshot;
    uint64_t entity = 0, action = 0, parent = 0;
    uint32_t native_id = UINT32_MAX, political_id = UINT32_MAX, type_id = UINT32_MAX;
    uint32_t mode_id = UINT32_MAX;
    int result = 0;
    int argument = 0;
    uint64_t count = 0, secondary_count = 0;
    float health = 0, energy = 0, hunger = 0;
    bool has_state = false, dead = false;
};

class ObservationTrace {
public:
    // Initialization, flushing and shutdown run only in SDK callbacks, never DllMain.
    bool open(const wchar_t* directory, bool host_fixture = false) noexcept;
    bool on_engine_thread() noexcept;
    void emit(Observation event) noexcept;
    void flush() noexcept;
    void close() noexcept;
    uint64_t next_action() noexcept { return ++action_; }
    uint64_t sequence() const noexcept { return sequence_; }
    uint64_t epoch() const noexcept { return epoch_; }
    void advance_epoch() noexcept { ++epoch_; }
    bool healthy() const noexcept { return !failed_ && !overflowed_; }
    static const char* name(ObservationKind kind) noexcept;
    static bool engine_event(ObservationKind kind) noexcept;
private:
    HANDLE file_ = INVALID_HANDLE_VALUE;
    DWORD engine_thread_ = 0;
    std::atomic<uint64_t> foreign_callbacks_{0};
    uint64_t sequence_ = 0, action_ = 0, epoch_ = 1, bytes_ = 0, lost_ = 0;
    LARGE_INTEGER started_{}, frequency_{};
    std::array<char, 65536> buffer_{};
    size_t used_ = 0;
    bool failed_ = false, overflowed_ = false, host_fixture_ = false;
};
}
