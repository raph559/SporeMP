#include "native_window.h"
#include "detour_transaction.h"

namespace sporemp {
namespace {
NativeWindowAbi::SetClientArea original = nullptr;
WindowPlacement target{};
HWND placed = nullptr;
WindowPlacementStatus status{};

// user32 entry points, hooked only inside a placed worker process and only for its own game window.
BOOL(WINAPI* real_show_window)(HWND, int) = ShowWindow;
BOOL(WINAPI* real_set_window_pos)(HWND, HWND, int, int, int, int, UINT) = SetWindowPos;
HWND(WINAPI* real_set_active_window)(HWND) = SetActiveWindow;
BOOL(WINAPI* real_set_foreground_window)(HWND) = SetForegroundWindow;
BOOL(WINAPI* real_bring_window_to_top)(HWND) = BringWindowToTop;
HWND(WINAPI* real_set_focus)(HWND) = SetFocus;

// Diagnostic: the first hooked call after which the placed window was the foreground window.
void note(WindowCall call) {
    if (status.foreground_after == WindowCall::none && GetForegroundWindow() == placed) {
        status.foreground_after = call;
    }
}

void __fastcall replacement(void* canvas, void*, int x, int y, int width, int height, int flag) {
    const auto origin = resolve_client_origin(target, x, y);
    ++status.calls;
    if (x == NativeWindowAbi::center) {
        ++status.redirected;
    }
    status.last_x = origin.x;
    status.last_y = origin.y;
    status.last_width = width;
    status.last_height = height;
    original(canvas, origin.x, origin.y, width, height, flag);
    note(WindowCall::set_client_area);
}

BOOL WINAPI show_window(HWND window, int command) {
    if (window != placed) {
        return real_show_window(window, command);
    }
    if (activating_show(command)) {
        ++status.shown_without_activation;
        command = SW_SHOWNOACTIVATE;
    }
    const BOOL result = real_show_window(window, command);
    note(WindowCall::show_window);
    return result;
}

BOOL WINAPI set_window_pos(HWND window, HWND after, int x, int y, int width, int height, UINT flags) {
    if (window != placed) {
        return real_set_window_pos(window, after, x, y, width, height, flags);
    }
    // Without SWP_NOACTIVATE a position or frame change also activates the window.
    if (!(flags & SWP_NOACTIVATE)) {
        ++status.repositioned_without_activation;
    }
    const BOOL result = real_set_window_pos(window, after, x, y, width, height, flags | SWP_NOACTIVATE);
    note(WindowCall::set_window_pos);
    return result;
}

HWND WINAPI set_active_window(HWND window) {
    if (window != placed) {
        return real_set_active_window(window);
    }
    ++status.activations_suppressed;
    return GetActiveWindow();
}

BOOL WINAPI set_foreground_window(HWND window) {
    if (window != placed) {
        return real_set_foreground_window(window);
    }
    ++status.activations_suppressed;
    return TRUE;
}

BOOL WINAPI bring_window_to_top(HWND window) {
    if (window != placed) {
        return real_bring_window_to_top(window);
    }
    ++status.activations_suppressed;
    return TRUE;
}

HWND WINAPI set_focus(HWND window) {
    if (window != placed) {
        return real_set_focus(window);
    }
    ++status.activations_suppressed;
    return GetFocus();
}

constexpr size_t hook_count = 7;

void hooks(NativeHook (&all)[hook_count]) {
    all[0] = {reinterpret_cast<void**>(&original), &replacement};
    all[1] = {reinterpret_cast<void**>(&real_show_window), &show_window};
    all[2] = {reinterpret_cast<void**>(&real_set_window_pos), &set_window_pos};
    all[3] = {reinterpret_cast<void**>(&real_set_active_window), &set_active_window};
    all[4] = {reinterpret_cast<void**>(&real_set_foreground_window), &set_foreground_window};
    all[5] = {reinterpret_cast<void**>(&real_bring_window_to_top), &bring_window_to_top};
    all[6] = {reinterpret_cast<void**>(&real_set_focus), &set_focus};
}
} // namespace

bool attach_window_placement(void* set_client_area, HWND window, WindowPlacement placement) noexcept {
    if (status.attached || !set_client_area || !window) {
        return false;
    }
    original = reinterpret_cast<NativeWindowAbi::SetClientArea>(set_client_area);
    target = placement;
    placed = window;
    NativeHook all[hook_count];
    hooks(all);
    if (change_hooks(all, hook_count, true) != NO_ERROR) {
        original = nullptr;
        placed = nullptr;
        return false;
    }
    status = {};
    status.attached = true;
    return true;
}

void detach_window_placement() noexcept {
    if (!status.attached) {
        return;
    }
    NativeHook all[hook_count];
    hooks(all);
    if (change_hooks(all, hook_count, false) == NO_ERROR) {
        status.attached = false;
        original = nullptr;
        placed = nullptr;
    }
}

namespace {
WindowMessageNote notes[96]{};
size_t note_count = 0;
HWND observed = nullptr;
HHOOK sent_hook = nullptr, posted_hook = nullptr;
ULONGLONG observed_since = 0;

void note_message(UINT message, WPARAM wparam) {
    if (note_count < _countof(notes)) {
        notes[note_count++] = {static_cast<unsigned>(GetTickCount64() - observed_since), message,
                               static_cast<unsigned>(wparam)};
    }
}

LRESULT CALLBACK sent_messages(int code, WPARAM wparam, LPARAM lparam) {
    const auto* call = reinterpret_cast<const CWPSTRUCT*>(lparam);
    if (code == HC_ACTION && call->hwnd == observed) {
        switch (call->message) {
        case WM_ACTIVATEAPP:
        case WM_ENABLE:
        case WM_SHOWWINDOW:
            note_message(call->message, call->wParam != 0);
            break;
        case WM_ACTIVATE:
            note_message(call->message, LOWORD(call->wParam));
            break;
        case WM_SIZE:
            note_message(call->message, call->wParam);
            break;
        case WM_SYSCOMMAND:
            note_message(call->message, call->wParam & 0xfff0);
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_CLOSE:
        case WM_CAPTURECHANGED:
            note_message(call->message, 0);
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK posted_messages(int code, WPARAM wparam, LPARAM lparam) {
    const auto* message = reinterpret_cast<const MSG*>(lparam);
    if (code == HC_ACTION && wparam == PM_REMOVE && message->hwnd == observed) {
        switch (message->message) {
        // The key itself is never kept, and a repeat of a held key is not a new arrival.
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!(message->lParam & 0x40000000)) {
                note_message(message->message, 0);
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_CLOSE:
            note_message(message->message, 0);
            break;
        case WM_SYSCOMMAND:
            note_message(message->message, message->wParam & 0xfff0);
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}
} // namespace

bool attach_window_observer(HWND window) noexcept {
    if (observed || !window) {
        return false;
    }
    observed = window;
    observed_since = GetTickCount64();
    sent_hook = SetWindowsHookExW(WH_CALLWNDPROC, sent_messages, nullptr, GetCurrentThreadId());
    posted_hook = SetWindowsHookExW(WH_GETMESSAGE, posted_messages, nullptr, GetCurrentThreadId());
    if (sent_hook && posted_hook) {
        return true;
    }
    detach_window_observer();
    return false;
}

void detach_window_observer() noexcept {
    if (sent_hook) {
        UnhookWindowsHookEx(sent_hook);
    }
    if (posted_hook) {
        UnhookWindowsHookEx(posted_hook);
    }
    sent_hook = posted_hook = nullptr;
    observed = nullptr;
}

size_t window_notes(const WindowMessageNote*& first) noexcept {
    first = notes;
    return note_count;
}

namespace {
BOOL(WINAPI* real_get_cursor_pos)(LPPOINT) = GetCursorPos;
bool pointer_attached = false;
POINT pointer{};
ULONGLONG pointer_until = 0;

BOOL WINAPI get_cursor_pos(LPPOINT point) {
    if (point && GetTickCount64() < pointer_until) {
        *point = pointer;
        return TRUE;
    }
    return real_get_cursor_pos(point);
}
} // namespace

bool attach_virtual_pointer() noexcept {
    if (pointer_attached) {
        return false;
    }
    NativeHook hook{reinterpret_cast<void**>(&real_get_cursor_pos), &get_cursor_pos};
    return pointer_attached = change_hooks(&hook, 1, true) == NO_ERROR;
}

void set_virtual_pointer(POINT screen, unsigned milliseconds) noexcept {
    pointer = screen;
    pointer_until = pointer_attached && milliseconds ? GetTickCount64() + milliseconds : 0;
}

void detach_virtual_pointer() noexcept {
    if (!pointer_attached) {
        return;
    }
    pointer_until = 0;
    NativeHook hook{reinterpret_cast<void**>(&real_get_cursor_pos), &get_cursor_pos};
    if (change_hooks(&hook, 1, false) == NO_ERROR) {
        pointer_attached = false;
    }
}

namespace {
HWND shielded = nullptr;
WNDPROC shielded_procedure = nullptr;
unsigned withheld = 0;

LRESULT CALLBACK shield(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    const auto next = shielded_procedure;
    if (!next) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    if (window == shielded && ((message == WM_ACTIVATEAPP && !wparam) ||
                               (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) || message == WM_KILLFOCUS)) {
        ++withheld;
        return 0;
    }
    return CallWindowProcW(next, window, message, wparam, lparam);
}
} // namespace

bool attach_activation_shield(HWND window) noexcept {
    if (shielded || !window) {
        return false;
    }
    SetLastError(0);
    const auto previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(shield));
    if (!previous) {
        return false;
    }
    shielded_procedure = reinterpret_cast<WNDPROC>(previous);
    shielded = window;
    return true;
}

// Only when the shield is still the window's procedure; under another layer it stays and keeps forwarding.
void detach_activation_shield() noexcept {
    if (!shielded) {
        return;
    }
    if (IsWindow(shielded) && GetWindowLongPtrW(shielded, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(shield)) {
        SetWindowLongPtrW(shielded, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(shielded_procedure));
        shielded_procedure = nullptr;
    }
    shielded = nullptr;
}

unsigned deactivations_withheld() noexcept {
    return withheld;
}

namespace {
SHORT(WINAPI* real_get_key_state)(int) = GetKeyState;
SHORT(WINAPI* real_get_async_key_state)(int) = GetAsyncKeyState;
BOOL(WINAPI* real_get_keyboard_state)(PBYTE) = GetKeyboardState;
bool keys_attached = false;
bool virtual_keys[256]{};

bool virtually_down(int key) {
    return keys_attached && key >= 0 && key < 256 && virtual_keys[key];
}

SHORT WINAPI get_key_state(int key) {
    return virtually_down(key) ? static_cast<SHORT>(0x8000) : real_get_key_state(key);
}

SHORT WINAPI get_async_key_state(int key) {
    return virtually_down(key) ? static_cast<SHORT>(0x8000) : real_get_async_key_state(key);
}

BOOL WINAPI get_keyboard_state(PBYTE state) {
    const BOOL read = real_get_keyboard_state(state);
    if (read && state) {
        for (int key = 0; key < 256; ++key) {
            if (virtually_down(key)) {
                state[key] |= 0x80;
            }
        }
    }
    return read;
}

void key_hooks(NativeHook (&all)[3]) {
    all[0] = {reinterpret_cast<void**>(&real_get_key_state), &get_key_state};
    all[1] = {reinterpret_cast<void**>(&real_get_async_key_state), &get_async_key_state};
    all[2] = {reinterpret_cast<void**>(&real_get_keyboard_state), &get_keyboard_state};
}
} // namespace

bool attach_virtual_keys() noexcept {
    if (keys_attached) {
        return false;
    }
    NativeHook all[3];
    key_hooks(all);
    return keys_attached = change_hooks(all, 3, true) == NO_ERROR;
}

void set_virtual_key(unsigned key, bool down) noexcept {
    if (key < 256) {
        virtual_keys[key] = down;
    }
}

void detach_virtual_keys() noexcept {
    if (!keys_attached) {
        return;
    }
    for (auto& key : virtual_keys) {
        key = false;
    }
    NativeHook all[3];
    key_hooks(all);
    if (change_hooks(all, 3, false) == NO_ERROR) {
        keys_attached = false;
    }
}

WindowPlacementStatus window_placement_status() noexcept {
    auto current = status;
    current.foreground_now = placed && GetForegroundWindow() == placed;
    return current;
}
} // namespace sporemp
