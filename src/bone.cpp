#include "bone.hpp"

#include <cstring>

#include "chunk.hpp"

namespace husk::bone {

namespace {

constexpr size_t kVersionSize = 4;

uint16_t readU16(const uint8_t* buf, size_t bufSize, size_t off) {
    if (off + 2 > bufSize) {
        throw ParseError("field at offset " + std::to_string(off) +
                          " needs 2 bytes but the chunk is only " + std::to_string(bufSize) +
                          " bytes");
    }
    uint16_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

float readF32(const uint8_t* buf, size_t bufSize, size_t off) {
    if (off + 4 > bufSize) {
        throw ParseError("field at offset " + std::to_string(off) +
                          " needs 4 bytes but the chunk is only " + std::to_string(bufSize) +
                          " bytes");
    }
    float v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

}  // namespace

std::vector<Correction> parse(const std::vector<uint8_t>& fileBytes) {
    if (fileBytes.size() < kVersionSize) {
        throw ParseError(".bone file is " + std::to_string(fileBytes.size()) +
                          " bytes, need at least " + std::to_string(kVersionSize) +
                          " for its leading version field");
    }

    auto chunks = readChunks(fileBytes.data() + kVersionSize, fileBytes.size() - kVersionSize);
    auto bida = findChunk(chunks, "BIDA");
    auto bomt = findChunk(chunks, "BOMT");
    if (!bida || !bomt) {
        std::string found;
        for (const auto& c : chunks) {
            if (!found.empty()) found += ", ";
            found += c.tag;
        }
        throw ParseError("expected BIDA and BOMT chunks in .bone file; chunks found: [" + found +
                          "]");
    }

    constexpr size_t kBoneIndexSize = 2;
    constexpr size_t kMatrixSize = 64;  // 16 floats
    size_t count = bida->size / kBoneIndexSize;
    if (bida->size % kBoneIndexSize != 0 || bomt->size % kMatrixSize != 0 ||
        count != bomt->size / kMatrixSize) {
        throw ParseError("BIDA (" + std::to_string(bida->size) + " bytes, " + std::to_string(count) +
                          " indices) and BOMT (" + std::to_string(bomt->size) + " bytes, " +
                          std::to_string(bomt->size / kMatrixSize) +
                          " matrices) don't agree on entry count");
    }

    std::vector<Correction> corrections;
    corrections.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Correction c;
        c.boneIndex = readU16(bida->data, bida->size, i * kBoneIndexSize);
        for (size_t f = 0; f < 16; ++f) {
            c.matrix[f] = readF32(bomt->data, bomt->size, i * kMatrixSize + f * 4);
        }
        corrections.push_back(c);
    }
    return corrections;
}

}  // namespace husk::bone
