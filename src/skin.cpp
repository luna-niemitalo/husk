#include "skin.hpp"

#include <cstring>

namespace husk::skin {

namespace {

namespace offset {
constexpr size_t magic = 0x00;
constexpr size_t vertices = 0x04;
constexpr size_t indices = 0x0C;
constexpr size_t bones = 0x14;
constexpr size_t submeshes = 0x1C;
constexpr size_t batches = 0x24;
constexpr size_t minHeaderSize = batches + 8;  // through the end of `batches`
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

uint8_t readU8(const uint8_t* buf, size_t bufSize, size_t off) {
    if (off + 1 > bufSize) {
        throw ParseError("field at offset " + std::to_string(off) +
                          " needs 1 byte but the file is only " + std::to_string(bufSize) +
                          " bytes");
    }
    return buf[off];
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
    h.submeshes = readArray(data, size, offset::submeshes);
    h.batches = readArray(data, size, offset::batches);
    return h;
}

std::vector<uint16_t> parseU16Array(const std::vector<uint8_t>& fileBytes, const m2::Array& array) {
    std::vector<uint16_t> values;
    if (array.count == 0) {
        return values;
    }

    const uint8_t* data = fileBytes.data();
    size_t size = fileBytes.size();

    // Up-front validation before reserve(), same reasoning and same bug
    // class as husk::m2::parseVertices/parseBones.
    // TODO: Remove: FAILURES.md #2.
    constexpr size_t kElementSize = 2;  // uint16
    if (array.offset > size || array.count > (size - array.offset) / kElementSize) {
        throw ParseError("array claims " + std::to_string(array.count) +
                          " uint16 entries at offset " + std::to_string(array.offset) +
                          ", which needs more room than the file's " + std::to_string(size) +
                          " bytes");
    }
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

std::vector<Submesh> parseSubmeshes(const std::vector<uint8_t>& fileBytes, const m2::Array& array) {
    std::vector<Submesh> submeshes;
    if (array.count == 0) {
        return submeshes;
    }

    // M2SkinSection, wowdev.wiki M2/.skin#Submeshes: 0x30 (48) bytes; only
    // vertexStart/vertexCount/indexStart/indexCount (offsets 0x04-0x0B) are
    // read, see skin.hpp's Submesh doc comment for why the rest is skipped.
    constexpr size_t kSubmeshSize = 0x30;
    const uint8_t* data = fileBytes.data();
    size_t size = fileBytes.size();

    if (array.offset > size || array.count > (size - array.offset) / kSubmeshSize) {
        throw ParseError("submeshes array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kSubmeshSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the file's " +
                          std::to_string(size) + " bytes");
    }
    submeshes.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kSubmeshSize;
        Submesh s;
        s.skinSectionId = readU16(data, size, off + 0x00);
        s.vertexStart = readU16(data, size, off + 0x04);
        s.vertexCount = readU16(data, size, off + 0x06);
        s.indexStart = readU16(data, size, off + 0x08);
        s.indexCount = readU16(data, size, off + 0x0A);
        submeshes.push_back(s);
    }

    return submeshes;
}

std::vector<Batch> parseBatches(const std::vector<uint8_t>& fileBytes, const m2::Array& array) {
    std::vector<Batch> batches;
    if (array.count == 0) {
        return batches;
    }

    // M2Batch, wowdev.wiki M2/.skin#Texture_units: 0x18 (24) bytes; see
    // skin.hpp's Batch doc comment for which fields are read and why the
    // rest (priorityPlane/shader_id/geosetIndex/materialLayer) is skipped.
    constexpr size_t kBatchSize = 0x18;
    const uint8_t* data = fileBytes.data();
    size_t size = fileBytes.size();

    if (array.offset > size || array.count > (size - array.offset) / kBatchSize) {
        throw ParseError("batches array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kBatchSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the file's " +
                          std::to_string(size) + " bytes");
    }
    batches.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kBatchSize;
        Batch b;
        b.flags = readU8(data, size, off + 0x00);
        b.skinSectionIndex = readU16(data, size, off + 0x04);
        b.colorIndex = readU16(data, size, off + 0x08);
        b.materialIndex = readU16(data, size, off + 0x0A);
        b.textureCount = readU16(data, size, off + 0x0E);
        b.textureComboIndex = readU16(data, size, off + 0x10);
        b.textureCoordComboIndex = readU16(data, size, off + 0x12);
        b.textureWeightComboIndex = readU16(data, size, off + 0x14);
        b.textureTransformComboIndex = readU16(data, size, off + 0x16);
        batches.push_back(b);
    }

    return batches;
}

}  // namespace husk::skin
