#pragma once
#include "world_identity.h"
#include <algorithm>
#include <vector>

namespace sporemp::network::state {
// Explicit bounded little-endian persistence encoding. Never serialize objects,
// pointers, padding, connection readiness, or native containers.
struct Writer {
    std::vector<uint8_t> bytes;

    void word(uint32_t v) {
        for (unsigned n = 0; n < 4; ++n) {
            bytes.push_back(uint8_t(v >> (n * 8)));
        }
    }

    void wide(uint64_t v) {
        for (unsigned n = 0; n < 8; ++n) {
            bytes.push_back(uint8_t(v >> (n * 8)));
        }
    }

    void digest(const Digest& v) {
        bytes.insert(bytes.end(), v.begin(), v.end());
    }

    void data(const std::vector<uint8_t>& v) {
        word(static_cast<uint32_t>(v.size()));
        bytes.insert(bytes.end(), v.begin(), v.end());
    }
};

struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool valid = true;

    bool take(size_t n) {
        if (!valid || n > bytes.size() - at) {
            valid = false;
            return false;
        }
        return true;
    }

    uint32_t word() {
        if (!take(4)) {
            return 0;
        }
        uint32_t v = 0;
        for (unsigned n = 0; n < 4; ++n) {
            v |= uint32_t(bytes[at++]) << (n * 8);
        }
        return v;
    }

    uint64_t wide() {
        if (!take(8)) {
            return 0;
        }
        uint64_t v = 0;
        for (unsigned n = 0; n < 8; ++n) {
            v |= uint64_t(bytes[at++]) << (n * 8);
        }
        return v;
    }

    Digest digest() {
        Digest v{};
        if (take(v.size())) {
            std::copy_n(bytes.begin() + at, v.size(), v.begin());
            at += v.size();
        }
        return v;
    }

    std::vector<uint8_t> data(size_t maximum) {
        auto n = word();
        if (n > maximum) {
            valid = false;
            return {};
        }
        if (!take(n)) {
            return {};
        }
        std::vector<uint8_t> v(bytes.begin() + at, bytes.begin() + at + n);
        at += n;
        return v;
    }

    bool done() const {
        return valid && at == bytes.size();
    }
};
} // namespace sporemp::network::state
