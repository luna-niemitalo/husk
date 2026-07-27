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

m2::Array readArray(const uint8_t* buf, size_t bufSize, size_t off) {
    m2::Array a;
    a.count = readU32(buf, bufSize, off);
    a.offset = readU32(buf, bufSize, off + 4);
    return a;
}

// Finds the chunk tagged `tag` in `fileBytes`. Throws ParseError, naming
// every chunk tag actually found, if there isn't one.
Chunk findRequiredChunk(const std::vector<uint8_t>& fileBytes, const char* tag) {
    auto chunks = readChunks(fileBytes.data(), fileBytes.size());
    auto found = findChunk(chunks, tag);
    if (!found) {
        std::string names;
        for (const auto& c : chunks) {
            if (!names.empty()) names += ", ";
            names += c.tag;
        }
        throw ParseError(std::string("no ") + tag + " chunk in .skel file; chunks found: [" +
                          names + "]");
    }
    return *found;
}

Chunk findSkb1(const std::vector<uint8_t>& fileBytes) { return findRequiredChunk(fileBytes, "SKB1"); }

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

std::vector<uint8_t> boneTrackBlob(const std::vector<uint8_t>& fileBytes) {
    Chunk skb1 = findSkb1(fileBytes);
    if (skb1.size < kHeaderSize) {
        throw ParseError("SKB1 chunk is " + std::to_string(skb1.size) +
                          " bytes, need at least " + std::to_string(kHeaderSize) +
                          " for its header");
    }
    return std::vector<uint8_t>(skb1.data, skb1.data + skb1.size);
}

namespace {
// SKS1 payload: M2Array global_loops (0x00), M2Array sequences (0x08),
// M2Array sequence_lookups (0x10), 8 bytes of trailing padding -- 0x20 = 32
// bytes total (wowdev.wiki M2/.skel#SKS1).
constexpr size_t kSequencesFieldOffset = 0x08;
constexpr size_t kSks1HeaderSize = 0x20;
}  // namespace

std::vector<m2::Sequence> parseSequences(const std::vector<uint8_t>& fileBytes) {
    Chunk sks1 = findRequiredChunk(fileBytes, "SKS1");
    if (sks1.size < kSks1HeaderSize) {
        throw ParseError("SKS1 chunk is " + std::to_string(sks1.size) +
                          " bytes, need at least " + std::to_string(kSks1HeaderSize) +
                          " for its header");
    }
    m2::Array sequences = readArray(sks1.data, sks1.size, kSequencesFieldOffset);
    std::vector<uint8_t> chunkBlob(sks1.data, sks1.data + sks1.size);
    return m2::parseSequences(chunkBlob, sequences);
}

std::optional<std::vector<m2::Header::AnimFileEntry>> findAnimFileIds(
    const std::vector<uint8_t>& fileBytes) {
    auto chunks = readChunks(fileBytes.data(), fileBytes.size());
    auto afid = findChunk(chunks, "AFID");
    if (!afid) {
        return std::nullopt;
    }
    constexpr size_t kEntrySize = 8;
    // Same up-front check as m2.cpp's findAnimFileIdChunk (FAILURES2.md #8)
    // -- a byte length that isn't a multiple of the record size means a
    // truncated/corrupted chunk, which must fail loudly rather than
    // silently drop the last partial entry.
    if (afid->size % kEntrySize != 0) {
        throw ParseError("AFID chunk is " + std::to_string(afid->size) +
                          " bytes, not a multiple of " + std::to_string(kEntrySize) +
                          " (one {anim_id, sub_anim_id, file_id} entry per record) -- truncated "
                          "or corrupted file?");
    }
    size_t count = afid->size / kEntrySize;
    std::vector<m2::Header::AnimFileEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        size_t off = i * kEntrySize;
        m2::Header::AnimFileEntry e;
        e.animId = readU16(afid->data, afid->size, off + 0x00);
        e.subAnimId = readU16(afid->data, afid->size, off + 0x02);
        e.fileId = readU32(afid->data, afid->size, off + 0x04);
        entries.push_back(e);
    }
    return entries;
}

std::optional<std::vector<uint32_t>> findBoneFileDataIds(const std::vector<uint8_t>& fileBytes) {
    auto chunks = readChunks(fileBytes.data(), fileBytes.size());
    auto bfid = findChunk(chunks, "BFID");
    if (!bfid) {
        return std::nullopt;
    }
    constexpr size_t kEntrySize = 4;
    // Same up-front check as m2.cpp's findFileDataIdArrayChunk/this file's
    // own findAnimFileIds (FAILURES2.md #8) -- a byte length that isn't a
    // multiple of the record size means a truncated/corrupted chunk.
    if (bfid->size % kEntrySize != 0) {
        throw ParseError("BFID chunk is " + std::to_string(bfid->size) +
                          " bytes, not a multiple of " + std::to_string(kEntrySize) +
                          " (one uint32 FileDataID per entry) -- truncated or corrupted file?");
    }
    size_t count = bfid->size / kEntrySize;
    std::vector<uint32_t> ids;
    ids.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        ids.push_back(readU32(bfid->data, bfid->size, i * kEntrySize));
    }
    return ids;
}

}  // namespace husk::skel
