#include <Spore/ModAPI.h>
#include <Spore/App/Canvas.h>
#include "native_window.h"
#include "native_actors.h"
#include <cstdio>
#include <cstring>

namespace sporemp {
namespace {
const char* state = "not_requested";
WindowPlacement requested{};
bool observing = false, shielding = false;
size_t notes_reported = 0;
} // namespace

void initialize_native_window() {
    wchar_t text[32]{};
    const DWORD length = GetEnvironmentVariableW(L"SPOREMP_WINDOW_POSITION", text, _countof(text));
    if (length == 0) {
        return; // Normal Play and unparked workers: nothing is read or installed.
    }
    state = "malformed";
    const auto placement = length < _countof(text) ? parse_window_placement({text, length}) : std::nullopt;
    if (!placement) {
        return;
    }
    requested = *placement;
    state = "canvas_unavailable";
    auto* canvas = App::Canvas::Get();
    if (!canvas) {
        return;
    }
    // Pinned executable only: the slot must hold the audited function with its audited entry bytes.
    void** table = *reinterpret_cast<void***>(canvas);
    auto* expected = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)) + NativeWindowAbi::rva;
    state = "binding_mismatch";
    if (table[NativeWindowAbi::slot] != expected ||
        std::memcmp(expected, NativeWindowAbi::prologue, sizeof(NativeWindowAbi::prologue)) != 0) {
        return;
    }
    state = "window_unavailable";
    const HWND window =
        *reinterpret_cast<HWND*>(reinterpret_cast<unsigned char*>(canvas) + NativeWindowAbi::window_offset);
    DWORD owner = 0;
    if (!IsWindow(window) || !GetWindowThreadProcessId(window, &owner) || owner != GetCurrentProcessId()) {
        return;
    }
    state = attach_window_placement(expected, window, *placement) ? "attached" : "attach_failed";
    observing = attach_window_observer(window);
    attach_virtual_pointer();
    attach_virtual_keys();
    shielding = attach_activation_shield(window);
}

namespace {
void report(const char* event) {
    const auto status = window_placement_status();
    char fields[384]{};
    std::snprintf(fields, sizeof(fields),
                  ",\"state\":\"%s\",\"x\":%d,\"y\":%d,\"calls\":%u,\"redirected\":%u,\"shown_without_activation\":%u,"
                  "\"repositioned_without_activation\":%u,\"activations_suppressed\":%u,"
                  "\"foreground_after\":%d,\"foreground_now\":%s,\"last\":[%d,%d,%d,%d]",
                  state, requested.x, requested.y, status.calls, status.redirected, status.shown_without_activation,
                  status.repositioned_without_activation, status.activations_suppressed,
                  static_cast<int>(status.foreground_after), status.foreground_now ? "true" : "false", status.last_x,
                  status.last_y, status.last_width, status.last_height);
    native_actor_worker_event(event, fields);
}
} // namespace

namespace {
// Rows of at most 24 notes, each [milliseconds since attach, message, kind]; only notes not yet reported.
void report_messages() {
    const WindowMessageNote* notes = nullptr;
    const size_t count = window_notes(notes);
    while (observing && notes_reported < count) {
        char fields[900]{};
        int length =
            std::snprintf(fields, sizeof(fields), ",\"first\":%zu,\"total\":%zu,\"notes\":[", notes_reported, count);
        for (size_t row = 0; row < 24 && notes_reported < count; ++row, ++notes_reported) {
            const auto& note = notes[notes_reported];
            length += std::snprintf(fields + length, sizeof(fields) - length, "%s[%u,%u,%u]", row ? "," : "",
                                    note.milliseconds, note.message, note.wparam);
        }
        std::snprintf(fields + length, sizeof(fields) - length, "],\"shielded\":%s,\"deactivations_withheld\":%u",
                      shielding ? "true" : "false", deactivations_withheld());
        native_actor_worker_event("native_window_messages", fields);
    }
}
} // namespace

void report_native_window() {
    report("native_window_placement");
    report_messages();
}

// The start-up report is taken at post-init; calls made later in the session only show up here.
void dispose_native_window() {
    if (window_placement_status().attached) {
        report("native_window_placement_final");
    }
    report_messages();
    detach_window_observer();
    detach_activation_shield();
    detach_virtual_keys();
    detach_virtual_pointer();
    detach_window_placement();
}
} // namespace sporemp
