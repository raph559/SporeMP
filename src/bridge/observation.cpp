#include "observation.h"
#include "diagnostics.h"
#include "build_identity.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace sporemp {
size_t EntityTable::index(uintptr_t key) const noexcept {
    return ((key >> 4) * uintptr_t(2654435761u)) % capacity;
}
ObservedEntity EntityTable::observe(uintptr_t key) noexcept {
    if (!key) return {};
    Slot* available = nullptr;
    for (size_t n = 0; n < capacity; ++n) {
        auto& slot = slots_[(index(key) + n) % capacity];
        if (slot.key == key && slot.state == State::live) return {key, slot.id};
        if (slot.key == key && slot.state == State::destroying) return {};
        if (slot.state == State::retired && !available) available = &slot;
        if (slot.state == State::empty) { if (!available) available = &slot; break; }
    }
    if (!available || next_id_ == UINT64_MAX) return {};
    *available = {key, ++next_id_, State::live};
    return {key, available->id};
}
ObservedEntity EntityTable::find(uintptr_t key) const noexcept {
    if (!key) return {};
    for (size_t n = 0; n < capacity; ++n) {
        const auto& slot = slots_[(index(key) + n) % capacity];
        if (slot.state == State::empty) break;
        if (slot.key == key && slot.state == State::live) return {key, slot.id};
    }
    return {};
}
ObservedEntity EntityTable::invalidate(uintptr_t key) noexcept {
    if (!key) return {};
    for (size_t n = 0; n < capacity; ++n) {
        auto& slot = slots_[(index(key) + n) % capacity];
        if (slot.state == State::empty) break;
        if (slot.key == key && slot.state == State::live) {
            slot.state = State::destroying;
            return {key, slot.id};
        }
    }
    return {};
}
void EntityTable::finish_destroy(uintptr_t key) noexcept {
    if (!key) return;
    for (size_t n = 0; n < capacity; ++n) {
        auto& slot = slots_[(index(key) + n) % capacity];
        if (slot.state == State::empty) break;
        if (slot.key == key && slot.state == State::destroying) { slot.state = State::retired; return; }
    }
}
void EntityTable::clear() noexcept { for (auto& slot : slots_) slot = {}; }
bool EntityTable::live(ObservedEntity token) const noexcept {
    return token.id != 0 && find(token.key).id == token.id;
}

const char* ObservationTrace::name(ObservationKind kind) noexcept {
    switch (kind) {
#define EVENT_NAME(value) case ObservationKind::value: return #value
        EVENT_NAME(trace_start); EVENT_NAME(trace_stop); EVENT_NAME(hooks_ready); EVENT_NAME(hooks_failed);
        EVENT_NAME(scene_enter); EVENT_NAME(scene_exit); EVENT_NAME(stage_observed);
        EVENT_NAME(entity_created); EVENT_NAME(entity_observed); EVENT_NAME(entity_invalidated);
        EVENT_NAME(avatar_assigned); EVENT_NAME(avatar_state); EVENT_NAME(jump_enter);
        EVENT_NAME(jump_return); EVENT_NAME(jump_land); EVENT_NAME(ai_summary);
        EVENT_NAME(diagnostic_snapshot); EVENT_NAME(overflow);
#undef EVENT_NAME
    }
    return "invalid";
}
bool ObservationTrace::engine_event(ObservationKind kind) noexcept {
    switch (kind) {
    case ObservationKind::scene_enter: case ObservationKind::scene_exit:
    case ObservationKind::entity_created: case ObservationKind::entity_invalidated:
    case ObservationKind::avatar_assigned: case ObservationKind::jump_enter:
    case ObservationKind::jump_return: case ObservationKind::jump_land: return true;
    default: return false; // Polls, sampled counters and derived IDs are mod observations.
    }
}
bool ObservationTrace::open(const wchar_t* directory, bool host_fixture) noexcept {
    if (file_ != INVALID_HANDLE_VALUE || !absolute_local_path(directory)) return false;
    DWORD attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    wchar_t path[32768]{};
    if (swprintf_s(path, L"%ls\\gameplay-%lu.jsonl", directory, GetCurrentProcessId()) < 0) return false;
    file_ = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) return false;
    engine_thread_ = GetCurrentThreadId();
    host_fixture_ = host_fixture;
    QueryPerformanceCounter(&started_);
    QueryPerformanceFrequency(&frequency_);
    emit({ObservationKind::trace_start});
    flush();
    return !failed_;
}
bool ObservationTrace::on_engine_thread() noexcept {
    // Foreign callbacks never inspect the mutable handle/buffer, including during shutdown.
    if (GetCurrentThreadId() != engine_thread_) { ++foreign_callbacks_; return false; }
    return file_ != INVALID_HANDLE_VALUE;
}
void ObservationTrace::emit(Observation event) noexcept {
    if (!on_engine_thread() || failed_) return;
    if (overflowed_ && event.kind != ObservationKind::trace_stop) { ++lost_; return; }
    // Reserve room for a terminal record. A capped/incomplete trace cannot pass evidence validation.
    if (event.kind == ObservationKind::overflow) { overflowed_ = true; ++lost_; }
    if (!overflowed_ && event.kind != ObservationKind::trace_stop && bytes_ + used_ > 64u * 1024u * 1024u) {
        overflowed_ = true;
        event = {ObservationKind::overflow};
        ++lost_;
    }
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    auto number = [](float value, char (&out)[32]) {
        if (std::isfinite(value)) sprintf_s(out, "%.9g", static_cast<double>(value));
        else strcpy_s(out, "null");
    };
    char health[32], energy[32], hunger[32];
    number(event.health, health); number(event.energy, energy); number(event.hunger, hunger);
    char line[2048]{};
    const int length = sprintf_s(line,
        "{\"schema_version\":1,\"evidence_class\":\"%s\",\"source\":\"%s\",\"event\":\"%s\","
        "\"sequence\":%llu,\"qpc\":%lld,\"qpc_frequency\":%lld,\"pid\":%lu,\"thread_id\":%lu,"
        "\"scene_epoch\":%llu,\"entity_id\":%llu,\"action_id\":%llu,\"parent_action_id\":%llu,"
        "\"native_id\":%u,\"political_id\":%u,\"type_id\":%u,\"mode_id\":%u,"
        "\"result\":%d,\"argument\":%d,\"count\":%llu,\"secondary_count\":%llu,"
        "\"has_state\":%s,\"health\":%s,\"energy\":%s,\"hunger\":%s,\"dead\":%s,"
        "\"foreign_callbacks\":%llu,\"lost_records\":%llu,\"multiplayer_mutations\":false,"
        "\"executable_sha256\":\"%s\",\"sdk_commit\":\"%s\",\"bridge_version\":\"%s\"}\n",
        host_fixture_ ? "HOST_FIXTURE" : "NATIVE_PROBE", engine_event(event.kind) ? "engine" : "mod",
        name(event.kind), ++sequence_, now.QuadPart - started_.QuadPart, frequency_.QuadPart,
        GetCurrentProcessId(), engine_thread_, epoch_, event.entity, event.action, event.parent,
        event.native_id, event.political_id, event.type_id, event.mode_id, event.result, event.argument,
        event.count, event.secondary_count, event.has_state ? "true" : "false",
        event.has_state ? health : "null", event.has_state ? energy : "null", event.has_state ? hunger : "null",
        event.dead ? "true" : "false", foreign_callbacks_.load(), lost_,
        host_fixture_ ? "HOST_FIXTURE_NOT_GAME" : candidate_exe_sha256, sdk_revision, bridge_version);
    if (length <= 0) { failed_ = true; return; }
    if (used_ + static_cast<size_t>(length) > buffer_.size()) flush();
    if (failed_) return;
    memcpy(buffer_.data() + used_, line, static_cast<size_t>(length));
    used_ += static_cast<size_t>(length);
}
void ObservationTrace::flush() noexcept {
    if (!on_engine_thread() || failed_ || used_ == 0) return;
    DWORD written = 0;
    if (!WriteFile(file_, buffer_.data(), static_cast<DWORD>(used_), &written, nullptr) || written != used_) {
        failed_ = true;
        OutputDebugStringW(L"SporeMP M02: trace write failed; native evidence is incomplete.\n");
    }
    bytes_ += written;
    used_ = 0;
}
void ObservationTrace::close() noexcept {
    if (!on_engine_thread()) return;
    emit({ObservationKind::trace_stop});
    flush();
    if (!FlushFileBuffers(file_)) failed_ = true;
    CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
}
}
