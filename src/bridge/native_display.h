#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

namespace sporemp {
// Original GA readme_en-us.txt, "Command Line Options": -w, -f, -r:<width>x<height>.
// No arbitrary arguments, native bindings or preference-file writes are needed.
inline std::wstring display_arguments(int count, const wchar_t* const* args) {
    if (count == 0) return {};
    if (count != 4 || std::wstring_view(args[0]) != L"--display-mode" ||
        std::wstring_view(args[2]) != L"--resolution")
        throw std::invalid_argument("Expected --display-mode fullscreen|windowed --resolution WIDTHxHEIGHT");
    std::wstring_view mode(args[1]), size(args[3]);
    if (mode != L"fullscreen" && mode != L"windowed")
        throw std::invalid_argument("Invalid display mode");
    auto separator = size.find(L'x');
    if (separator == std::wstring_view::npos) throw std::invalid_argument("Invalid resolution");
    auto number = [](std::wstring_view value, unsigned minimum) {
        if (value.size() < 3 || value.size() > 4 || value.front() == L'0')
            throw std::invalid_argument("Invalid resolution dimensions");
        unsigned result = 0;
        for (wchar_t c : value) {
            if (c < L'0' || c > L'9') throw std::invalid_argument("Invalid resolution character");
            result = result * 10 + (c - L'0');
        }
        if (result < minimum || result > 8192) throw std::invalid_argument("Resolution outside allowed range");
        return result;
    };
    const auto width = number(size.substr(0, separator), 640);
    const auto height = number(size.substr(separator + 1), 480);
    return std::wstring(mode == L"fullscreen" ? L" -f -r:" : L" -w -r:") +
        std::to_wstring(width) + L"x" + std::to_wstring(height);
}
}
