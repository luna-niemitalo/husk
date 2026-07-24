#include "skin.hpp"

#include <cstring>

namespace husk::skin {

namespace {

namespace offset {
constexpr size_t magic = 0x00;
constexpr size_t vertices = 0x04;
constexpr size_t indices = 0x0C;
constexpr size_t minHeaderSize = indices + 8;  // through the end of `indices`
}  // namespace offset

uint32_t readU32(const uint8_t* buf, size_t bufSize, size_t off) {
    if (off + 4 > bufSize) {
        throw ParseError("field at offset " + std::to_string(off) +
                          " needs 4 bytes but the file is only " + std::to_string(bufSize) +
                          " bytes");
    }
    uint32_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

uint16_t readU16(const uint8_t* buf, size_t bufSize, size_t off) {
    if (off + 2 > bufSize) {
        throw ParseError("field at offset " + std::to_string(off) +
                          " needs 2 bytes but the file is only " + std::to_string(bufSize) +
                          " bytes");
    }
    uint16_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

m2::Array readArray(const uint8_t* buf, size_t bufSize, size_t off) {
    m2::Array a;
    a.count = readU32(buf, bufSize, off);
    a.offset = readU32(buf, bufSize, off + 4);
    return a;
}

}  // namespace

Header parseHeader(const std::vector<uint8_t>& fileBytes) {
    if (fileBytes.size() < offset::minHeaderSize) {
        throw ParseError(".skin header is " + std::to_string(fileBytes.size()) +
                          " bytes, need at least " + std::to_string(offset::minHeaderSize) +
                          " for the fields this parser reads");
    }

    const uint8_t* data = fileBytes.data();
    size_t size = fileBytes.size();

    Header h;
    h.magic = readU32(data, size, offset::magic);
    if (h.magic != 0x4E494B53 /* "SKIN" little-endian */) {
        char bytes[5] = {};
        std::memcpy(bytes, &h.magic, 4);
        throw ParseError(std::string("expected SKIN magic in the .skin file, got '") + bytes +
                          "'");
    }
    h.vertices = readArray(data, size, offset::vertices);
    h.indices = readArray(data, size, offset::indices);
    return h;
}

std::vector<uint16_t> parseU16Array(const std::vector<uint8_t>& fileBytes, const m2::Array& array) {
    std::vector<uint16_t> values;
    if (array.count == 0) {
        return values;
    }

    const uint8_t* data = fileBytes.data();
    size_t size = fileBytes.size();
    values.reserve(array.count);
    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * 2;
        values.push_back(readU16(data, size, off));
    }
    return values;
}

std::vector<uint32_t> resolveTriangleIndices(const std::vector<uint8_t>& fileBytes,
                                              const Header& header) {
    auto localVertices = parseU16Array(fileBytes, header.vertices);
    auto localIndices = parseU16Array(fileBytes, header.indices);

    std::vector<uint32_t> globalIndices;
    globalIndices.reserve(localIndices.size());
    for (uint16_t slot : localIndices) {
        if (slot >= localVertices.size()) {
            throw ParseError("skin index " + std::to_string(slot) +
                              " is out of range for the skin's vertices array (" +
                              std::to_string(localVertices.size()) + " entries)");
        }
        globalIndices.push_back(localVertices[slot]);
    }
    return globalIndices;
}

}  // namespace husk::skin
