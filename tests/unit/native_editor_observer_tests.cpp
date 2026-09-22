#include "bridge/native_editor_observer_abi.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
int checks = 0, failures = 0;
void check(bool condition, const char* description) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", description); }
}
template<size_t N> void field(std::array<unsigned char, N>& bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
}
int main() {
    using namespace sporemp;
    using Result = NativeEditorDecodeResult;
    // Synthetic bytes exercise the STATIC-derived decoder; no SDK or game load.
    constexpr uint32_t request_class = 0x013f6fec, result_class = 0x013f56b4;
    std::array<unsigned char, native_editor_request_bytes> request{};
    field(request, 0, request_class); field(request, 0x0c, 11); field(request, 0x10, 22);
    field(request, 0x14, 0x2b978c46); field(request, 0x18, 33); field(request, 0x1c, 44); field(request, 0x90, 55);
    request[0x37] = request[0x3b] = 1;
    // Embedded strings, containers and pointers are deliberately invalid and
    // must remain opaque to the observer.
    field(request, 0x40, 0xffffffff); field(request, 0x70, 1); field(request, 0x94, 0xffffffff);
    NativeEditorRequestObservation begin;
    check(decode_native_editor_request(request.data(), request.size(), request_class, begin) == Result::ok &&
        begin.editor == 11 && begin.instance == 22 && begin.type == 0x2b978c46 && begin.group == 33 &&
        begin.calling_mode == 44 && begin.routing_id == 55 && begin.show_save && begin.show_cancel &&
        !begin.show_new && !begin.show_publish && !begin.show_play, "request scalars decoded without following embedded pointers");
    check(decode_native_editor_request(nullptr, request.size(), request_class, begin) == Result::truncated, "null request refused");
    for (size_t size : {size_t(0), size_t(3), size_t(0x90), request.size()-1})
        check(decode_native_editor_request(request.data(), size, request_class, begin) == Result::truncated, "partial request refused");
    check(decode_native_editor_request(request.data(), request.size(), result_class, begin) == Result::unsupported_class, "different request class refused");
    check(decode_native_editor_request(request.data(), request.size(), 0, begin) == Result::unsupported_class, "unqualified request class refused");
    for (size_t offset : {size_t(0x37), size_t(0x38), size_t(0x3a), size_t(0x3b), size_t(0x65)}) {
        const auto old = request[offset]; request[offset] = 2;
        check(decode_native_editor_request(request.data(), request.size(), request_class, begin) == Result::malformed_boolean &&
            begin.instance == 22 && begin.routing_id == 55, "malformed request flag refused without output mutation");
        request[offset] = old;
    }
    std::array<unsigned char, native_editor_result_bytes> result{};
    field(result, 0, result_class); field(result, 0x0c, 55); field(result, 0x14, 66);
    field(result, 0x18, 77); field(result, 0x1c, 0x2b978c46); field(result, 0x20, 33);
    field(result, 0x10, 0xffffffff); field(result, 0x24, 0xffffffff);
    NativeEditorResultObservation completed;
    check(decode_native_editor_result(result.data(), result.size(), result_class, completed) == Result::ok &&
        completed.routing_id == 55 && completed.model_type == 66 && completed.instance == 77 &&
        completed.type == 0x2b978c46 && completed.group == 33 && !completed.cancelled && !completed.play,
        "accepted result preserves original scalar key without dereferencing data");
    result[0x44] = result[0x45] = 1;
    field(result, 0x18, 22);
    check(decode_native_editor_result(result.data(), result.size(), result_class, completed) == Result::ok &&
        completed.cancelled && completed.play && completed.instance == 22, "cancel result and old resource key remain observable");
    check(decode_native_editor_result(nullptr, result.size(), result_class, completed) == Result::truncated, "null result refused");
    for (size_t size : {size_t(0), size_t(3), size_t(0x44), result.size()-1})
        check(decode_native_editor_result(result.data(), size, result_class, completed) == Result::truncated, "partial result refused");
    check(decode_native_editor_result(result.data(), result.size(), request_class, completed) == Result::unsupported_class, "different result class refused");
    check(decode_native_editor_result(result.data(), result.size(), 0, completed) == Result::unsupported_class, "unqualified result class refused");
    for (size_t offset : {size_t(0x44), size_t(0x45)}) {
        result[offset] = 255;
        check(decode_native_editor_result(result.data(), result.size(), result_class, completed) == Result::malformed_boolean &&
            completed.cancelled && completed.instance == 22, "malformed result flag refused without output mutation");
        result[offset] = 1;
    }
    // Relocated class pointers must be compared with the qualified live address.
    field(result, 0, result_class + 0x100000);
    check(decode_native_editor_result(result.data(), result.size(), result_class, completed) == Result::unsupported_class,
        "unrelocated expected class fails closed");
    check(decode_native_editor_result(result.data(), result.size(), result_class + 0x100000, completed) == Result::ok,
        "matching relocated class decodes");
    std::printf("HOST editor message decoder: %d checks, %d failures; native execution NOT RUN\n", checks, failures);
    return failures ? 1 : 0;
}
