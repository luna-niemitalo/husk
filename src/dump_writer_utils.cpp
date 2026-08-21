#include "dump_writer_utils.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace husk::commands {

uint32_t readU32(const uint8_t* d, size_t n, size_t off) {
    if (off + 4 > n) {
        throw std::runtime_error("chunk field at offset " + std::to_string(off) +
                                  " needs 4 bytes but the chunk is only " + std::to_string(n) +
                                  " bytes");
    }
    uint32_t v;
    std::memcpy(&v, d + off, 4);
    return v;
}

uint16_t readU16(const uint8_t* d, size_t n, size_t off) {
    if (off + 2 > n) {
        throw std::runtime_error("chunk field at offset " + std::to_string(off) +
                                  " needs 2 bytes but the chunk is only " + std::to_string(n) +
                                  " bytes");
    }
    uint16_t v;
    std::memcpy(&v, d + off, 2);
    return v;
}

float readF32(const uint8_t* d, size_t n, size_t off) {
    uint32_t bits = readU32(d, n, off);
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

ChunkArray readChunkArray(const uint8_t* d, size_t n, size_t off) {
    return {readU32(d, n, off), readU32(d, n, off + 4)};
}

std::string hexDump(const uint8_t* d, size_t n) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < n; ++i) {
        os << std::setw(2) << static_cast<int>(d[i]);
    }
    return os.str();
}

void writeVec3(json::Writer& w, const m2::Vec3& v) {
    w.beginObject();
    w.key("x");
    w.value(static_cast<double>(v.x));
    w.key("y");
    w.value(static_cast<double>(v.y));
    w.key("z");
    w.value(static_cast<double>(v.z));
    w.endObject();
}

void writeFixed16AsFloat(json::Writer& w, uint32_t raw) {
    int16_t signedRaw;
    auto bits = static_cast<uint16_t>(raw);
    std::memcpy(&signedRaw, &bits, sizeof(signedRaw));
    float v = std::clamp(static_cast<float>(signedRaw) / 32767.0f, 0.0f, 1.0f);
    w.value(static_cast<double>(v));
}

void writeRawIntAsIs(json::Writer& w, uint32_t raw) { w.value(static_cast<int64_t>(raw)); }

float readHalfFloat(uint16_t bits) {
    uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
    uint32_t exponent = (bits >> 10) & 0x1Fu;
    uint32_t mantissa = bits & 0x3FFu;
    uint32_t out;
    if (exponent == 0) {
        if (mantissa == 0) {
            out = sign;  // +-0
        } else {
            // Subnormal half -> normalized float (not observed in any real
            // DETL record so far, but a foreign file could still carry one).
            uint32_t e = 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --e;
            }
            mantissa &= 0x3FFu;
            out = sign | ((e + (127 - 15)) << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1Fu) {
        out = sign | 0x7F800000u | (mantissa << 13);  // Inf/NaN
    } else {
        out = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
    }
    float v;
    std::memcpy(&v, &out, sizeof(v));
    return v;
}

size_t physPayloadRealLength(const uint8_t* d, size_t n) {
    size_t pos = 0;
    while (n - pos >= 8) {
        uint32_t chunkSize;
        std::memcpy(&chunkSize, d + pos + 4, sizeof(chunkSize));
        size_t payloadStart = pos + 8;
        if (chunkSize > n - payloadStart) break;  // trailing PADDING, not a real next chunk
        pos = payloadStart + chunkSize;
    }
    return pos;
}

}  // namespace husk::commands
