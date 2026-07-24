#include "skel.hpp"

#include <cstring>

#include "chunk.hpp"

namespace husk::skel {

namespace {

constexpr size_t kBonesFieldOffset = 0x00;
constexpr size_t kHeaderSize = 0x10;  // through the end of key_bone_lookup

uint32_t readU32(const uint8_t* buf, size_t bufSize, size_t off) {
    if (off + 4 > bufSize) {
        throw ParseError("field at offset " + std::to_string(off) +
                          " needs 4 bytes but the chunk is only " + std::to_string(bufSize) +
                          " bytes");
    }
    uint32_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

m2::Array readArray(const uint8_t* buf, size_t bufSize, size_t off) {
    m2::Array a;
    a.count = readU32(buf, bufSize, off);
    a.offset = readU32(buf, bufSize, off + 4);
    return a;
}

// Finds the SKB1 chunk in `fileBytes`. Throws ParseError, naming every
// chunk tag actually found, if there isn't one.
Chunk findSkb1(const std::vector<uint8_t>& fileBytes) {
    auto chunks = readChunks(fileBytes.data(), fileBytes.size());
    auto skb1 = findChunk(chunks, "SKB1");
    if (!skb1) {
        std::string found;
        for (const auto& c : chunks) {
            if (!found.empty()) found += ", ";
            found += c.tag;
        }
        throw ParseError("no SKB1 chunk in .skel file; chunks found: [" + found + "]");
    }
    return *skb1;
}

}  // namespace

BoneHeader parseBoneHeader(const std::vector<uint8_t>& fileBytes) {
    Chunk skb1 = findSkb1(fileBytes);
    if (skb1.size < kHeaderSize) {
        throw ParseError("SKB1 chunk is " + std::to_string(skb1.size) +
                          " bytes, need at least " + std::to_string(kHeaderSize) +
                          " for its header");
    }
    BoneHeader h;
    h.bones = readArray(skb1.data, skb1.size, kBonesFieldOffset);
    return h;
}

std::vector<m2::Bone> parseBones(const std::vector<uint8_t>& fileBytes) {
    Chunk skb1 = findSkb1(fileBytes);
    if (skb1.size < kHeaderSize) {
        throw ParseError("SKB1 chunk is " + std::to_string(skb1.size) +
                          " bytes, need at least " + std::to_string(kHeaderSize) +
                          " for its header");
    }
    m2::Array bones = readArray(skb1.data, skb1.size, kBonesFieldOffset);
    std::vector<uint8_t> chunkBlob(skb1.data, skb1.data + skb1.size);
    return m2::parseBones(chunkBlob, bones);
}

}  // namespace husk::skel
