#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sporemp {
// Pure decoding of already readable copies of the pinned x86 message layouts.
// The caller supplies the relocated, qualified native primary vtable address.
// Decoders never dereference embedded pointers, call native code or infer saves.
constexpr size_t native_editor_request_bytes = 0x9c;
constexpr size_t native_editor_result_bytes = 0x48;
enum class NativeEditorDecodeResult { ok, truncated, unsupported_class, malformed_boolean };
struct NativeEditorRequestObservation {
    uint32_t editor = 0, instance = 0, type = 0, group = 0, calling_mode = 0, routing_id = 0;
    bool show_save = false, show_new = false, show_publish = false, show_cancel = false, show_play = false;
};
struct NativeEditorResultObservation {
    uint32_t routing_id = 0, model_type = 0, instance = 0, type = 0, group = 0;
    bool cancelled = false, play = false;
};
namespace native_editor_layout {
inline uint32_t scalar(const unsigned char* bytes, size_t offset) {
    uint32_t value = 0; std::memcpy(&value, bytes + offset, sizeof(value)); return value;
}
inline NativeEditorDecodeResult header(const void* data, size_t size, size_t required, uint32_t expected_vtable) {
    if (!data || size < required) return NativeEditorDecodeResult::truncated;
    if (!expected_vtable || scalar(static_cast<const unsigned char*>(data), 0) != expected_vtable)
        return NativeEditorDecodeResult::unsupported_class;
    return NativeEditorDecodeResult::ok;
}
}
inline NativeEditorDecodeResult decode_native_editor_request(const void* data, size_t size, uint32_t expected_vtable,
    NativeEditorRequestObservation& output) {
    using namespace native_editor_layout;
    const auto result = header(data, size, native_editor_request_bytes, expected_vtable);
    if (result != NativeEditorDecodeResult::ok) return result;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0x37] > 1 || bytes[0x38] > 1 || bytes[0x3a] > 1 || bytes[0x3b] > 1 || bytes[0x65] > 1)
        return NativeEditorDecodeResult::malformed_boolean;
    output = {scalar(bytes, 0x0c), scalar(bytes, 0x10), scalar(bytes, 0x14), scalar(bytes, 0x18),
        scalar(bytes, 0x1c), scalar(bytes, 0x90), bytes[0x37] != 0, bytes[0x38] != 0,
        bytes[0x3a] != 0, bytes[0x3b] != 0, bytes[0x65] != 0};
    return NativeEditorDecodeResult::ok;
}
inline NativeEditorDecodeResult decode_native_editor_result(const void* data, size_t size, uint32_t expected_vtable,
    NativeEditorResultObservation& output) {
    using namespace native_editor_layout;
    const auto result = header(data, size, native_editor_result_bytes, expected_vtable);
    if (result != NativeEditorDecodeResult::ok) return result;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (bytes[0x44] > 1 || bytes[0x45] > 1) return NativeEditorDecodeResult::malformed_boolean;
    output = {scalar(bytes, 0x0c), scalar(bytes, 0x14), scalar(bytes, 0x18), scalar(bytes, 0x1c),
        scalar(bytes, 0x20), bytes[0x44] != 0, bytes[0x45] != 0};
    return NativeEditorDecodeResult::ok;
}
}
