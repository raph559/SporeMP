#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include "native_window.h"

namespace sporemp {
// Original GA readme_en-us.txt, "Command Line Options": -w, -f, -r:<width>x<height>.
// No arbitrary arguments, native bindings or preference-file writes are needed.
// Worker-only options ahead of the display arguments: [--window-position X,Y] [--no-sound].
// The position goes to the bridge, never to the game's command line. --no-sound becomes the
// original game's own noSound switch (pinned audio start-up 0xA421F0; docs/research-log.md R09).
struct WorkerPresentation {
    std::wstring window_position;
    bool no_sound = false;
    int consumed = 0;
};

inline WorkerPresentation worker_presentation(int count, const wchar_t* const* args) {
    WorkerPresentation result;
    if (result.consumed < count && std::wstring_view(args[result.consumed]) == L"--window-position") {
        if (result.consumed + 1 >= count || !parse_window_placement(args[result.consumed + 1])) {
            throw std::invalid_argument("Expected --window-position X,Y");
        }
        result.window_position = args[result.consumed + 1];
        result.consumed += 2;
    }
    if (result.consumed < count && std::wstring_view(args[result.consumed]) == L"--no-sound") {
        result.no_sound = true;
        ++result.consumed;
    }
    return result;
}

inline std::wstring display_arguments(int count, const wchar_t* const* args) {
    if (count == 0) {
        return {};
    }
    if (count != 4 || std::wstring_view(args[0]) != L"--display-mode" ||
        std::wstring_view(args[2]) != L"--resolution") {
        throw std::invalid_argument("Expected --display-mode fullscreen|windowed --resolution WIDTHxHEIGHT");
    }
    std::wstring_view mode(args[1]), size(args[3]);
    if (mode != L"fullscreen" && mode != L"windowed") {
        throw std::invalid_argument("Invalid display mode");
    }
    auto separator = size.find(L'x');
    if (separator == std::wstring_view::npos) {
        throw std::invalid_argument("Invalid resolution");
    }
    auto number = [](std::wstring_view value, unsigned minimum) {
        if (value.size() < 3 || value.size() > 4 || value.front() == L'0') {
            throw std::invalid_argument("Invalid resolution dimensions");
        }
        unsigned result = 0;
        for (wchar_t c : value) {
            if (c < L'0' || c > L'9') {
                throw std::invalid_argument("Invalid resolution character");
            }
            result = result * 10 + (c - L'0');
        }
        if (result < minimum || result > 8192) {
            throw std::invalid_argument("Resolution outside allowed range");
        }
        return result;
    };
    const auto width = number(size.substr(0, separator), 640);
    const auto height = number(size.substr(separator + 1), 480);
    return std::wstring(mode == L"fullscreen" ? L" -f -r:" : L" -w -r:") + std::to_wstring(width) + L"x" +
           std::to_wstring(height);
}
} // namespace sporemp
