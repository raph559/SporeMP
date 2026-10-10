#pragma once
#include <cstddef>
#include <string>

namespace sporemp::worker {
// Interface text for a trace row: printable ASCII as it is, quotes and backslashes escaped, line breaks and
// tabs as spaces, every other character as a \uXXXX escape, cut after `limit` characters of the original.
// The result goes between the quotes of a JSON string.
inline std::string ui_text_json(const char16_t* text, size_t limit) {
    std::string result;
    if (!text) {
        return result;
    }
    for (size_t i = 0; i < limit && text[i]; ++i) {
        const char16_t c = text[i];
        if (c == u'"' || c == u'\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c == u'\n' || c == u'\r' || c == u'\t') {
            result += ' ';
        } else if (c >= 32 && c < 127) {
            result += static_cast<char>(c);
        } else {
            constexpr char digits[] = "0123456789abcdef";
            result += "\\u";
            for (int shift = 12; shift >= 0; shift -= 4) {
                result += digits[(c >> shift) & 15];
            }
        }
    }
    return result;
}
} // namespace sporemp::worker
