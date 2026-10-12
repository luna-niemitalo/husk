#include "text_encoding.hpp"

#include <array>
#include <cstdint>

namespace husk::text {

namespace {

// Windows-1252's 0x80..0x9F; 0 marks its five undefined bytes, which pass
// through as the same code points (what Windows' own converter does).
constexpr std::array<char16_t, 32> kHighControls = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

bool isUtf8(std::string_view bytes) {
    size_t i = 0;
    while (i < bytes.size()) {
        auto lead = static_cast<unsigned char>(bytes[i]);
        size_t extra = 0;
        if (lead < 0x80) {
            extra = 0;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
        } else {
            return false;
        }
        if (extra > bytes.size() - i - 1) return false;
        for (size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(bytes[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

std::string fromGameText(std::string_view bytes) {
    if (isUtf8(bytes)) return std::string(bytes);
    std::string out;
    out.reserve(bytes.size() + 8);
    for (char c : bytes) {
        auto b = static_cast<unsigned char>(c);
        uint32_t cp = b;
        if (b >= 0x80 && b < 0xA0 && kHighControls[b - 0x80] != 0) cp = kHighControls[b - 0x80];
        appendUtf8(out, cp);
    }
    return out;
}

}  // namespace husk::text
