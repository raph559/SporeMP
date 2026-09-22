#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sporemp {
constexpr size_t max_creation_png_bytes = 4 * 1024 * 1024;
// The original-created fixture is RGBA8, noninterlaced, with IHDR/IDAT/IEND only.
// Refuse every other envelope until qualified. This checks the PNG container,
// not its Model-in-Picture payload; the original importer must validate that.
inline const char* creation_png_envelope(const uint8_t* data, size_t size) {
    constexpr uint8_t signature[] = {137,80,78,71,13,10,26,10};
    if (!data || size < 57 || size > max_creation_png_bytes) return "png_size_limit";
    if (std::memcmp(data, signature, 8)) return "png_signature";
    const auto word = [](const uint8_t* p) { return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3]; };
    size_t offset = 8, chunks = 0, pixels = 0;
    while (offset < size) {
        if (++chunks > 128 || size - offset < 12) return "png_chunk_bounds";
        const size_t length = word(data + offset);
        if (length > size - offset - 12) return "png_chunk_bounds";
        const auto type = data + offset + 4;
        const auto body = type + 4;
        uint32_t crc = UINT32_MAX;
        for (size_t i = 0; i < length + 4; ++i) {
            crc ^= type[i];
            for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        if ((crc ^ UINT32_MAX) != word(body + length)) return "png_crc";
        if (chunks == 1) {
            if (std::memcmp(type,"IHDR",4) || length != 13) return "png_header";
            const auto width = word(body), height = word(body + 4);
            if (!width || !height || width > 512 || height > 512 || body[8] != 8 || body[9] != 6 ||
                body[10] || body[11] || body[12]) return "png_format_unqualified";
        } else if (!std::memcmp(type,"IDAT",4)) {
            if (!length) return "png_empty_pixels";
            pixels += length;
        } else if (!std::memcmp(type,"IEND",4)) {
            if (length || !pixels || offset + 12 != size) return "png_end_or_trailing_bytes";
            return nullptr;
        } else return "png_chunk_unqualified";
        offset += length + 12;
    }
    return "png_missing_end";
}
}
