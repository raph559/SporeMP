#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace sporemp {
// Original App::Canvas::SetClientArea: virtual slot +0x40, pinned at image+0x448FB0.
// Static reading only (docs/research-log.md R09): thiscall(x, y, width, height, flag), RET 0x14.
// x == center asks the game to center the client area on the primary display's work area;
// cAppSystem::Configure does that and shows the window immediately afterwards.
struct NativeWindowAbi {
    using SetClientArea = void(__thiscall*)(void* canvas, int x, int y, int width, int height, int flag);
    static constexpr int center = 0x7fffffff;
    static constexpr size_t slot = 0x40 / sizeof(void*);
    static constexpr uintptr_t rva = 0x448fb0;
    static constexpr unsigned char prologue[8]{0x8b, 0x54, 0x24, 0x04, 0x83, 0xec, 0x30, 0x56};
    static constexpr size_t window_offset = 0x74; // Canvas::mWindow, already created when init callbacks run
};

struct WindowPlacement {
    int x;
    int y;
};

// "X,Y" in virtual-screen pixels: optional minus, at most six digits each, nothing else.
inline std::optional<WindowPlacement> parse_window_placement(std::wstring_view text) {
    int values[2]{};
    size_t at = 0;
    for (int index = 0; index < 2; ++index) {
        const bool negative = at < text.size() && text[at] == L'-';
        if (negative) {
            ++at;
        }
        const size_t first = at;
        int value = 0;
        while (at < text.size() && text[at] >= L'0' && text[at] <= L'9' && at - first < 6) {
            value = value * 10 + (text[at++] - L'0');
        }
        if (at == first) {
            return std::nullopt;
        }
        values[index] = negative ? -value : value;
        if (index == 0) {
            if (at >= text.size() || text[at] != L',') {
                return std::nullopt;
            }
            ++at;
        }
    }
    if (at != text.size()) {
        return std::nullopt;
    }
    return WindowPlacement{values[0], values[1]};
}

// The origin handed to the original method: only a centering request is replaced.
inline WindowPlacement resolve_client_origin(const WindowPlacement& configured, int x, int y) {
    return x == NativeWindowAbi::center ? configured : WindowPlacement{x, y};
}

// Show commands that would also activate the window; a placed worker gets SW_SHOWNOACTIVATE instead.
constexpr bool activating_show(int command) {
    return command == SW_SHOWNORMAL || command == SW_SHOW || command == SW_RESTORE || command == SW_SHOWDEFAULT;
}

enum class WindowCall {
    none,
    set_client_area,
    show_window,
    set_window_pos
};

struct WindowPlacementStatus {
    bool attached, foreground_now;
    unsigned calls, redirected, shown_without_activation, repositioned_without_activation, activations_suppressed;
    WindowCall foreground_after; // first hooked call after which the window was the foreground window
    int last_x, last_y, last_width, last_height;
};

// Attach to a function with the ABI above and, for `window` only, to this process's ShowWindow,
// SetWindowPos, SetActiveWindow, SetForegroundWindow, BringWindowToTop and SetFocus, all or none.
// Trials 154 and 155: a non-activating show is not enough, and the game does not call the three
// explicit activation functions before post-init, yet its window becomes the foreground window.
// Outside loader lock. Host tests use a fixture function and a real off-screen window.
bool attach_window_placement(void* set_client_area, HWND window, WindowPlacement placement) noexcept;
void detach_window_placement() noexcept;
WindowPlacementStatus window_placement_status() noexcept;

// Observation only. What a window is told from now on: activation, focus, visibility, size and system
// commands with their kind, and that a key or mouse button reached it, never which key. Kept in a fixed
// buffer; once full, later messages are dropped. Watches the calling thread's messages for `window`.
struct WindowMessageNote {
    unsigned milliseconds, message, wparam;
};

bool attach_window_observer(HWND window) noexcept;
void detach_window_observer() noexcept;
size_t window_notes(const WindowMessageNote*& first) noexcept;

// Workers only. The window is not told that it lost activation or focus: a game told so during its
// intro movie stays on that movie (trial184; docs/research-log.md R10). Everything else passes through.
// Installed before the network module puts its own procedure on the window, removed after it.
bool attach_activation_shield(HWND window) noexcept;
void detach_activation_shield() noexcept;
unsigned deactivations_withheld() noexcept;

// A pointer position only this process sees. The original reads the cursor through GetCursorPos from a
// dozen places besides its mouse messages (docs/research-log.md R10), so a posted click alone lands
// where the physical mouse is not. While a position is set and not expired, GetCursorPos in this
// process reports it; the real cursor is never moved and no other process is affected.
bool attach_virtual_pointer() noexcept;
void set_virtual_pointer(POINT screen, unsigned milliseconds) noexcept;
void detach_virtual_pointer() noexcept;

// Keys only this process sees as held. A key message posted to the window does not change what
// GetKeyState, GetAsyncKeyState and GetKeyboardState report, and the original consults them (static
// reading, docs/research-log.md R10); trial204: a posted ability key did nothing. No real key is pressed.
bool attach_virtual_keys() noexcept;
void set_virtual_key(unsigned key, bool down) noexcept;
void detach_virtual_keys() noexcept;

// Bridge only. SDK init callback: runs after cAppSystem::Init, before the window is centered and shown.
// Installs nothing unless the launch host supplied SPOREMP_WINDOW_POSITION.
void initialize_native_window();
// SDK post-init: one trace row with what happened.
void report_native_window();
void dispose_native_window();
} // namespace sporemp
