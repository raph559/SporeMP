#include "native_window.h"
#include "native_display.h"
#include <cstddef>
#include <cstdio>
#include <string>

// HOST ONLY. Exercises the placement hooks on a fixture with the statically read ABI of the
// original Canvas::SetClientArea and on real off-screen windows; SPORE is not loaded and no
// native binding is verified here.
namespace {
struct Call {
    void* canvas;
    int x, y, width, height, flag;
};

Call received{};
unsigned fixture_calls = 0;

__declspec(noinline) void __fastcall set_client_area_fixture(void* canvas, void*, int x, int y, int width, int height,
                                                             int flag) {
    ++fixture_calls;
    received = {canvas, x, y, width, height, flag};
}

void call(sporemp::NativeWindowAbi::SetClientArea method, void* canvas, int x, int y, int width, int height, int flag) {
    method(canvas, x, y, width, height, flag);
}

// A window procedure standing in for the game's: it counts what it is told.
unsigned told_deactivated = 0, told_activated = 0, told_other = 0;

LRESULT CALLBACK game_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if ((message == WM_ACTIVATEAPP && !wparam) || (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) ||
        message == WM_KILLFOCUS) {
        ++told_deactivated;
    } else if (message == WM_ACTIVATEAPP || message == WM_ACTIVATE || message == WM_SETFOCUS) {
        ++told_activated;
    } else if (message == WM_APP) {
        ++told_other;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// Tiny tool windows far outside every display: nothing appears and the user's foreground is untouched.
HWND off_screen_window() {
    return CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, -32000, -32000, 1, 1, nullptr, nullptr, nullptr,
                           nullptr);
}
} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool value, const char* name) {
        std::printf("%s: %s (HOST ONLY; SPORE NOT LOADED)\n", value ? "PASS" : "FAIL", name);
        if (!value) {
            ++failures;
        }
    };
    using sporemp::parse_window_placement;
    const auto parsed = parse_window_placement(L"2563,32");
    check(parsed && parsed->x == 2563 && parsed->y == 32, "position parsed");
    const auto negative = parse_window_placement(L"-1920,270");
    check(negative && negative->x == -1920 && negative->y == 270, "position left of the primary display parsed");
    for (const wchar_t* invalid : {L"", L"2563", L"2563,", L",32", L"2563,32,1", L"2563;32", L" 2563,32", L"2563,32 ",
                                   L"+5,5", L"1e3,5", L"1234567,5", L"5,--5", L"0x10,5", L"2563,32 -safe"}) {
        check(!parse_window_placement(invalid), "malformed position rejected");
    }

    const sporemp::WindowPlacement target{2563, 32};
    const auto centered = sporemp::resolve_client_origin(target, sporemp::NativeWindowAbi::center, 0);
    check(centered.x == 2563 && centered.y == 32, "centering request takes the configured position");
    const auto explicit_origin = sporemp::resolve_client_origin(target, 100, 200);
    check(explicit_origin.x == 100 && explicit_origin.y == 200, "explicit position is left alone");
    check(sporemp::activating_show(SW_SHOWNORMAL) && sporemp::activating_show(SW_SHOW) &&
              sporemp::activating_show(SW_RESTORE) && sporemp::activating_show(SW_SHOWDEFAULT),
          "activating show commands recognised");
    check(!sporemp::activating_show(SW_HIDE) && !sporemp::activating_show(SW_SHOWNOACTIVATE) &&
              !sporemp::activating_show(SW_SHOWNA) && !sporemp::activating_show(SW_MINIMIZE),
          "hide and non-activating commands left alone");

    const auto fixture = reinterpret_cast<sporemp::NativeWindowAbi::SetClientArea>(&set_client_area_fixture);
    void* const address = reinterpret_cast<void*>(&set_client_area_fixture);
    int canvas = 0;
    const HWND game = off_screen_window(), other = off_screen_window();
    check(game && other && !IsWindowVisible(game), "hidden fixture windows created");
    call(fixture, &canvas, sporemp::NativeWindowAbi::center, 0, 1280, 720, 1);
    check(fixture_calls == 1 && received.x == sporemp::NativeWindowAbi::center,
          "fixture reached directly before attach");
    check(!sporemp::window_placement_status().attached, "nothing attached by default");
    check(!sporemp::attach_window_placement(address, nullptr, target), "attach without a window refused");
    check(sporemp::attach_window_placement(address, game, target), "placement hooks attach");
    check(!sporemp::attach_window_placement(address, game, target), "second attach refused");

    call(fixture, &canvas, sporemp::NativeWindowAbi::center, 0, 1280, 720, 1);
    check(fixture_calls == 2 && received.canvas == &canvas && received.x == 2563 && received.y == 32 &&
              received.width == 1280 && received.height == 720 && received.flag == 1,
          "centering request reaches the original with the configured position and unchanged size");
    call(fixture, &canvas, 640, 336, 800, 600, 0);
    check(fixture_calls == 3 && received.x == 640 && received.y == 336 && received.width == 800 &&
              received.height == 600,
          "explicit request passes through unchanged");

    ShowWindow(game, SW_SHOWNORMAL);
    check(IsWindowVisible(game) && GetActiveWindow() != game && GetForegroundWindow() != game,
          "an activating show makes the placed window visible without activating it");
    SetActiveWindow(game);
    SetForegroundWindow(game);
    BringWindowToTop(game);
    SetFocus(game);
    check(GetActiveWindow() != game && GetForegroundWindow() != game && GetFocus() != game,
          "activation and focus requests for the placed window do nothing");
    SetWindowPos(game, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    SetWindowPos(game, nullptr, -32000, -31000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT moved{};
    GetWindowRect(game, &moved);
    check(GetActiveWindow() != game && GetForegroundWindow() != game && moved.top == -31000,
          "repositioning still works and never activates the placed window");
    ShowWindow(game, SW_HIDE);
    check(!IsWindowVisible(game), "hiding the placed window still works");
    ShowWindow(other, SW_SHOWNOACTIVATE);
    SetActiveWindow(other);
    check(GetActiveWindow() == other, "another window of the process is activated normally");

    const auto status = sporemp::window_placement_status();
    check(status.attached && status.calls == 2 && status.redirected == 1 && status.last_width == 800 &&
              status.shown_without_activation == 1 && status.activations_suppressed == 4 &&
              status.repositioned_without_activation == 1 && status.foreground_after == sporemp::WindowCall::none &&
              !status.foreground_now,
          "status counts redirects, converted shows, repositions and suppressed activations");
    sporemp::detach_window_placement();
    call(fixture, &canvas, sporemp::NativeWindowAbi::center, 0, 1280, 720, 1);
    check(fixture_calls == 4 && received.x == sporemp::NativeWindowAbi::center &&
              !sporemp::window_placement_status().attached,
          "detach restores the original behaviour");
    SetActiveWindow(game);
    check(GetActiveWindow() == game, "after detach the window can be activated again");
    DestroyWindow(game);
    DestroyWindow(other);

    // A worker's game is never told that it lost activation (57 observed starts: the one told so 3.7 seconds
    // after opening froze on its intro movie; none told so within 3.1 seconds, or never, did).
    HWND shielded = off_screen_window();
    SetWindowLongPtrW(shielded, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(game_procedure));
    SendMessageW(shielded, WM_ACTIVATEAPP, FALSE, 0);
    check(told_deactivated == 1, "before the shield a deactivation reaches the game");
    check(sporemp::attach_activation_shield(shielded) && !sporemp::attach_activation_shield(shielded),
          "shield attaches once");
    SendMessageW(shielded, WM_ACTIVATEAPP, FALSE, 0);
    SendMessageW(shielded, WM_ACTIVATE, WA_INACTIVE, 0);
    SendMessageW(shielded, WM_KILLFOCUS, 0, 0);
    check(told_deactivated == 1 && sporemp::deactivations_withheld() == 3,
          "shielded: no loss of activation or focus reaches the game");
    SendMessageW(shielded, WM_ACTIVATEAPP, TRUE, 0);
    SendMessageW(shielded, WM_ACTIVATE, WA_ACTIVE, 0);
    SendMessageW(shielded, WM_APP, 1, 2);
    // The default handling of an activation may itself set the focus, which the procedure is also told.
    check(told_activated >= 2 && told_other == 1, "activation and every other message still reach it");
    sporemp::detach_activation_shield();
    SendMessageW(shielded, WM_KILLFOCUS, 0, 0);
    check(told_deactivated == 2 &&
              GetWindowLongPtrW(shielded, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(game_procedure),
          "detach gives the window back its own procedure");
    DestroyWindow(shielded);

    // A pointer position only this process sees; the real cursor is never moved.
    auto real_cursor = [] {
        CURSORINFO info{sizeof(info)};
        GetCursorInfo(&info);
        return info.ptScreenPos;
    };
    const POINT before_pointer = real_cursor();
    POINT seen{};
    check(sporemp::attach_virtual_pointer() && !sporemp::attach_virtual_pointer(), "virtual pointer attaches once");
    GetCursorPos(&seen);
    const POINT around = real_cursor();
    check((seen.x == before_pointer.x && seen.y == before_pointer.y) || (seen.x == around.x && seen.y == around.y),
          "with no virtual pointer set the real cursor position is reported");
    sporemp::set_virtual_pointer({-31000, 12345}, 60000);
    check(GetCursorPos(&seen) && seen.x == -31000 && seen.y == 12345,
          "while set, this process sees the virtual position");
    sporemp::set_virtual_pointer({-31000, 12345}, 0);
    GetCursorPos(&seen);
    check(!(seen.x == -31000 && seen.y == 12345), "an expired virtual pointer gives way to the real cursor");
    sporemp::set_virtual_pointer({-31000, 12345}, 60000);
    sporemp::detach_virtual_pointer();
    GetCursorPos(&seen);
    const POINT after_pointer = real_cursor();
    check(!(seen.x == -31000 && seen.y == 12345), "detach restores the real cursor position");
    check(after_pointer.x != -31000 && after_pointer.y != 12345, "the real cursor was not moved by any of it");

    // A key only this process sees as held. F24 is used here so that no real key is involved.
    BYTE keyboard[256]{};
    check(sporemp::attach_virtual_keys() && !sporemp::attach_virtual_keys(), "virtual keys attach once");
    check(!(GetKeyState(VK_F24) & 0x8000) && !(GetAsyncKeyState(VK_F24) & 0x8000), "a key nobody holds reads as up");
    sporemp::set_virtual_key(VK_F24, true);
    check((GetKeyState(VK_F24) & 0x8000) && (GetAsyncKeyState(VK_F24) & 0x8000) && GetKeyboardState(keyboard) &&
              (keyboard[VK_F24] & 0x80),
          "while set, this process sees the key held through all three state functions");
    check(!(GetKeyState(VK_F23) & 0x8000) && !(keyboard[VK_F23] & 0x80), "other keys are untouched");
    sporemp::set_virtual_key(VK_F24, false);
    GetKeyboardState(keyboard);
    check(!(GetKeyState(VK_F24) & 0x8000) && !(GetAsyncKeyState(VK_F24) & 0x8000) && !(keyboard[VK_F24] & 0x80),
          "released, it reads as up again");
    sporemp::set_virtual_key(VK_F24, true);
    sporemp::detach_virtual_keys();
    check(!(GetKeyState(VK_F24) & 0x8000), "detach restores the real key state");

    // What a window is told during startup, without the content of any key.
    HWND watched = off_screen_window(), bystander = off_screen_window();
    auto noted = [](unsigned message, unsigned wparam) {
        const sporemp::WindowMessageNote* notes = nullptr;
        const size_t count = sporemp::window_notes(notes);
        unsigned found = 0;
        for (size_t i = 0; i < count; ++i) {
            found += notes[i].message == message && notes[i].wparam == wparam;
        }
        return found;
    };
    auto pump = [] {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
    };
    check(sporemp::attach_window_observer(watched) && !sporemp::attach_window_observer(watched),
          "observer attaches once");
    SendMessageW(watched, WM_ACTIVATEAPP, FALSE, 0);
    SendMessageW(bystander, WM_ACTIVATEAPP, FALSE, 0);
    check(noted(WM_ACTIVATEAPP, 0) == 1, "a deactivation sent to the watched window is noted, another window's is not");
    SendMessageW(watched, WM_SYSCOMMAND, SC_MINIMIZE | 2, 0);
    check(noted(WM_SYSCOMMAND, SC_MINIMIZE) == 1, "a system command is noted by its kind");
    SendMessageW(watched, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L""));
    const sporemp::WindowMessageNote* all = nullptr;
    const size_t before_keys = sporemp::window_notes(all);
    PostMessageW(watched, WM_KEYDOWN, 'P', 0);
    PostMessageW(watched, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(3, 4));
    pump();
    check(sporemp::window_notes(all) == before_keys + 2 && noted(WM_KEYDOWN, 0) == 1 && noted(WM_LBUTTONDOWN, 0) == 1 &&
              noted(WM_KEYDOWN, 'P') == 0,
          "a key or button reaching the window is noted without which key it was");
    sporemp::detach_window_observer();
    SendMessageW(watched, WM_ACTIVATEAPP, TRUE, 0);
    check(sporemp::window_notes(all) == before_keys + 2, "after detach nothing more is noted");
    DestroyWindow(watched);
    DestroyWindow(bystander);

    const wchar_t* none[]{L"--display-mode", L"windowed", L"--resolution", L"1280x720"};
    auto plain = sporemp::worker_presentation(4, none);
    check(plain.consumed == 0 && plain.window_position.empty() && !plain.no_sound,
          "no presentation option leaves arguments alone");
    const wchar_t* both[]{L"--window-position", L"2563,32",      L"--no-sound", L"--display-mode",
                          L"windowed",          L"--resolution", L"1280x720"};
    auto parked = sporemp::worker_presentation(7, both);
    check(parked.consumed == 3 && parked.window_position == L"2563,32" && parked.no_sound,
          "position and silence accepted before display options");
    const wchar_t* silent[]{L"--no-sound"};
    auto quiet = sporemp::worker_presentation(1, silent);
    check(quiet.consumed == 1 && quiet.no_sound && quiet.window_position.empty(), "silence alone accepted");
    for (const wchar_t* invalid : {L"2563", L"2563,32 -safe", L"x,y"}) {
        const wchar_t* args[]{L"--window-position", invalid};
        bool rejected = false;
        try {
            sporemp::worker_presentation(2, args);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "malformed window position rejected by the host");
    }
    const wchar_t* dangling[]{L"--window-position"};
    bool rejected = false;
    try {
        sporemp::worker_presentation(1, dangling);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "window position without a value rejected");
    return failures;
}
