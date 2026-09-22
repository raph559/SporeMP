#include "native_editor_observer.h"
#include "native_editor_observer_abi.h"
#include <Spore/App/IMessageManager.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace sporemp {
namespace {
// SDK cbf9206b9a823f0911cd9be0217104a49d72380b; GOG GA executable
// dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37.
// EditorRequest::Submit posts its primary pointer. Editor result producers
// 0x57E8A0/0x57C1E0/0x587A20 use the 0x579C80 class. Message queue 0x8847F0
// dispatches the borrowed pointer synchronously, then releases its owned ref.
// Read only while the callback runs. Never retain or AddRef/Release its data.
constexpr uint32_t request_message = 0xb03bc30c, result_message = 0x030c11c7;
constexpr uint32_t request_vtable_rva = 0x00ff6fec, result_vtable_rva = 0x00ff56b4;
uintptr_t image_base = 0;
size_t image_size = 0;
DWORD engine_thread = 0;
NativeEditorObserverEvent event_sink = nullptr;
App::IMessageManager* messages = nullptr;
bool request_listening = false, result_listening = false, active = false;
volatile LONG foreign_thread_callbacks = 0;

bool on_thread() { return engine_thread && GetCurrentThreadId() == engine_thread; }
void event(const char* name, const char* format = "", ...) {
    if (!on_thread() || !event_sink) return;
    char fields[1024]{};
    va_list args; va_start(args, format);
    const auto written = vsprintf_s(fields, format, args); va_end(args);
    if (written >= 0) event_sink(name, fields);
}
bool readable(const void* address, size_t size) {
    uintptr_t cursor = reinterpret_cast<uintptr_t>(address);
    if (!cursor || size > std::numeric_limits<uintptr_t>::max() - cursor) return false;
    const auto end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const DWORD access = info.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto region = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (cursor < region || info.RegionSize > std::numeric_limits<uintptr_t>::max() - region) return false;
        const auto region_end = region + info.RegionSize;
        if (region_end <= cursor) return false;
        cursor = region_end < end ? region_end : end;
    }
    return true;
}
bool image_span(uint32_t rva, size_t size) {
    return image_base && rva <= image_size && size <= image_size - rva &&
        readable(reinterpret_cast<const void*>(image_base + rva), size);
}
template<size_t N> void relocated(std::array<unsigned char, N>& bytes, size_t offset, uint32_t rva) {
    const auto address = static_cast<uint32_t>(image_base + rva);
    std::memcpy(bytes.data() + offset, &address, sizeof(address));
}
bool producer_guards() {
    // Constructor prefixes include relocated primary/secondary vtable writes.
    // No constructor, Submit, save, Post or editor-control leaf is invoked here.
    std::array<unsigned char, 29> request = {0x53,0x33,0xdb,0x56,0x8b,0xf1,
        0xc7,0x46,0x04,0,0,0,0,0x89,0x5e,0x08,0xc7,0x06,0,0,0,0,
        0xc7,0x46,0x04,0,0,0,0};
    std::array<unsigned char, 27> result = {0x8b,0xc1,0xc7,0x40,0x04,0,0,0,0,
        0x33,0xc9,0x89,0x48,0x08,0xc7,0x00,0,0,0,0,0xc7,0x40,0x04,0,0,0,0};
    relocated(request, 9, 0x00feb8b8); relocated(request, 18, request_vtable_rva);
    relocated(request, 25, request_vtable_rva - 4);
    relocated(result, 5, 0x00feb8b8); relocated(result, 16, result_vtable_rva);
    relocated(result, 23, result_vtable_rva - 4);
    return image_span(0x001a9140, request.size()) && image_span(0x00179c80, result.size()) &&
        image_span(request_vtable_rva, 12) && image_span(result_vtable_rva, 12) &&
        !std::memcmp(reinterpret_cast<const void*>(image_base + 0x001a9140), request.data(), request.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(image_base + 0x00179c80), result.data(), result.size());
}
template<size_t N> bool copy_message(const void* data, std::array<unsigned char, N>& copy) {
    if (!readable(data, N)) return false;
    std::memcpy(copy.data(), data, N);
    return true;
}
void rejected(uint32_t id, const char* reason) {
    event("editor_observation_rejected", ",\"message\":%u,\"reason\":\"%s\",\"observation_only\":true,\"readiness\":false", id, reason);
}
class Listener final : public App::IUnmanagedMessageListener {
public:
    bool HandleMessage(uint32_t id, void* data) override {
        if (id != request_message && id != result_message) return false;
        if (!on_thread()) { InterlockedIncrement(&foreign_thread_callbacks); return false; }
        if (!active) return false;
        if (id == request_message) {
            std::array<unsigned char, native_editor_request_bytes> copy{};
            if (!copy_message(data, copy)) { rejected(id, "unreadable_request"); return false; }
            NativeEditorRequestObservation value;
            const auto decoded = decode_native_editor_request(copy.data(), copy.size(),
                static_cast<uint32_t>(image_base + request_vtable_rva), value);
            if (decoded != NativeEditorDecodeResult::ok) {
                rejected(id, decoded == NativeEditorDecodeResult::malformed_boolean ? "invalid_request_boolean" : "unknown_request_class"); return false;
            }
            event("editor_request_observed", ",\"message\":%u,\"editor\":%u,\"instance\":%u,\"type\":%u,\"group\":%u,\"calling_mode\":%u,\"routing_id\":%u,\"show_save\":%u,\"show_new\":%u,\"show_publish\":%u,\"show_cancel\":%u,\"show_play\":%u,\"observation_only\":true,\"editor_landing_verified\":false,\"readiness\":false",
                id, value.editor, value.instance, value.type, value.group, value.calling_mode, value.routing_id,
                unsigned(value.show_save), unsigned(value.show_new), unsigned(value.show_publish), unsigned(value.show_cancel), unsigned(value.show_play));
        } else {
            std::array<unsigned char, native_editor_result_bytes> copy{};
            if (!copy_message(data, copy)) { rejected(id, "unreadable_result"); return false; }
            NativeEditorResultObservation value;
            const auto decoded = decode_native_editor_result(copy.data(), copy.size(),
                static_cast<uint32_t>(image_base + result_vtable_rva), value);
            if (decoded != NativeEditorDecodeResult::ok) {
                rejected(id, decoded == NativeEditorDecodeResult::malformed_boolean ? "invalid_result_boolean" : "unknown_result_class"); return false;
            }
            event("editor_result_observed", ",\"message\":%u,\"routing_id\":%u,\"model_type\":%u,\"instance\":%u,\"type\":%u,\"group\":%u,\"outcome\":\"%s\",\"play\":%u,\"observation_only\":true,\"native_commit_validation\":false,\"readiness\":false",
                id, value.routing_id, value.model_type, value.instance, value.type,
                value.group, value.cancelled ? "cancelled" : "accepted", unsigned(value.play));
        }
        return false; // Never consume or alter the original game's message.
    }
} listener;
}

bool initialize_native_editor_observer(uintptr_t base, size_t size, DWORD thread, NativeEditorObserverEvent sink) {
    static_assert(sizeof(void*) == 4, "Pinned original editor ABI is x86");
    if (request_listening || result_listening || !thread || thread != GetCurrentThreadId() || !sink ||
        !base || size > std::numeric_limits<uintptr_t>::max() - base) return false;
    image_base = base; image_size = size; engine_thread = thread; event_sink = sink;
    if (!producer_guards()) {
        event("editor_observer_unavailable", ",\"reason\":\"producer_or_vtable_guard\",\"readiness\":false"); return false;
    }
    messages = App::IMessageManager::Get();
    if (!messages) {
        event("editor_observer_unavailable", ",\"reason\":\"message_manager_missing\",\"readiness\":false"); return false;
    }
    InterlockedExchange(&foreign_thread_callbacks, 0);
    messages->AddUnmanagedListener(&listener, request_message); request_listening = true;
    messages->AddUnmanagedListener(&listener, result_message); result_listening = true;
    active = true;
    event("editor_observer_registered", ",\"producer_prefixes\":2,\"request_message\":%u,\"result_message\":%u,\"observation_only\":true,\"native_execution_qualified\":false,\"readiness\":false", request_message, result_message);
    return true;
}
void dispose_native_editor_observer() {
    if (!on_thread()) return;
    active = false;
    if (messages && request_listening && messages->RemoveListener(&listener, request_message)) request_listening = false;
    if (messages && result_listening && messages->RemoveListener(&listener, result_message)) result_listening = false;
    if (messages) event("editor_observer_disposed", ",\"listeners_removed\":%s,\"foreign_thread_callbacks\":%ld,\"readiness\":false",
        !request_listening && !result_listening ? "true" : "false", InterlockedCompareExchange(&foreign_thread_callbacks, 0, 0));
    if (!request_listening && !result_listening) { messages = nullptr; event_sink = nullptr; }
}
}
