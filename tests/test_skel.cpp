// Spec source: https://wowdev.wiki/M2/.skel "SKB1" section (fetched
// 2026-07-24), plus M2#Bones for M2CompBone itself. Offsets below are typed
// out fresh from that page, not copied from src/skel.cpp -- same
// independent-transcription rationale as tests/test_m2.cpp.
//
// A .skel file is chunked exactly like M2 itself (4-byte tag, uint32 size,
// payload, non-reversed -- husk::readChunks handles both). The SKB1 chunk's
// payload:
//   0x00 bones (M2Array<M2CompBone>)          -- offsets relative to this
//   0x08 key_bone_lookup (M2Array<uint16_t>)     chunk's own payload start
// -> 0x10 = 16 bytes, then raw data.
// M2CompBone itself (88 bytes) is the same struct tests/test_m2.cpp
// transcribes -- see that file's header comment for its offsets. This file
// only needs enough of it (parent_bone, pivot) to prove the two are the
// same data read through a different path.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/chunk.hpp"
#include "../src/skel.hpp"

namespace {

void putU32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    if (buf.size() < off + 4) buf.resize(off + 4, 0);
    std::memcpy(buf.data() + off, &v, 4);
}

void putF32(std::vector<uint8_t>& buf, size_t off, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32(buf, off, bits);
}

void putArray(std::vector<uint8_t>& buf, size_t off, uint32_t count, uint32_t arrayOffset) {
    putU32(buf, off, count);
    putU32(buf, off + 4, arrayOffset);
}

// Same 88-byte M2CompBone layout as tests/test_m2.cpp's putBone -- see that
// file's header comment for the full offset table.
void putBone(std::vector<uint8_t>& buf, size_t off, int16_t parentBone, float pivotX,
             float pivotY, float pivotZ) {
    if (buf.size() < off + 0x58) buf.resize(off + 0x58, 0);
    uint16_t parentBoneBits = static_cast<uint16_t>(parentBone);
    std::memcpy(buf.data() + off + 0x08, &parentBoneBits, 2);
    putF32(buf, off + 0x4C, pivotX);
    putF32(buf, off + 0x50, pivotY);
    putF32(buf, off + 0x54, pivotZ);
}

void appendChunk(std::vector<uint8_t>& buf, const char tag[4], const std::vector<uint8_t>& payload) {
    buf.insert(buf.end(), tag, tag + 4);
    uint32_t size = static_cast<uint32_t>(payload.size());
    uint8_t sizeBytes[4];
    std::memcpy(sizeBytes, &size, 4);
    buf.insert(buf.end(), sizeBytes, sizeBytes + 4);
    buf.insert(buf.end(), payload.begin(), payload.end());
}

// Builds an SKB1 chunk payload: 16-byte header (bones array, key_bone_lookup
// left zeroed -- unread) followed by `boneCount` raw M2CompBone records.
std::vector<uint8_t> buildSkb1Payload(uint32_t boneCount) {
    constexpr size_t kHeaderSize = 0x10;
    std::vector<uint8_t> payload(kHeaderSize, 0);
    putArray(payload, 0x00, boneCount, static_cast<uint32_t>(kHeaderSize));
    // key_bone_lookup at 0x08 left as {0, 0} -- unread by this parser.
    for (uint32_t i = 0; i < boneCount; ++i) {
        size_t off = kHeaderSize + static_cast<size_t>(i) * 0x58;
        putBone(payload, off, /*parentBone=*/static_cast<int16_t>(i) - 1, /*pivotX=*/float(i),
                /*pivotY=*/float(i) * 2, /*pivotZ=*/float(i) * 3);
    }
    return payload;
}

}  // namespace

TEST_CASE("parseBoneHeader: reads the bones array field at the right offset") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKB1", buildSkb1Payload(3));

    auto h = husk::skel::parseBoneHeader(file);
    CHECK(h.bones.count == 3);
    CHECK(h.bones.offset == 0x10);
}

TEST_CASE("parseBoneHeader: unrelated chunks before/after SKB1 don't confuse it") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKL1", {1, 2, 3, 4});
    appendChunk(file, "SKB1", buildSkb1Payload(2));
    appendChunk(file, "SKS1", {5, 6});

    auto h = husk::skel::parseBoneHeader(file);
    CHECK(h.bones.count == 2);
}

TEST_CASE("parseBoneHeader: missing SKB1 chunk throws, names what it found") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKL1", {1});
    appendChunk(file, "SKS1", {2});

    CHECK_THROWS_WITH_AS(husk::skel::parseBoneHeader(file), doctest::Contains("SKL1"),
                          husk::skel::ParseError);
}

TEST_CASE("parseBoneHeader: SKB1 payload shorter than its 16-byte header throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKB1", {1, 2, 3});  // way short of 16 bytes
    CHECK_THROWS_AS(husk::skel::parseBoneHeader(file), husk::skel::ParseError);
}

TEST_CASE("parseBones: resolves SKB1's bones array into real M2CompBone records") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKB1", buildSkb1Payload(3));

    auto bones = husk::skel::parseBones(file);
    REQUIRE(bones.size() == 3);

    CHECK(bones[0].parentBone == -1);
    CHECK(bones[0].pivot.x == doctest::Approx(0));

    CHECK(bones[1].parentBone == 0);
    CHECK(bones[1].pivot.x == doctest::Approx(1));
    CHECK(bones[1].pivot.y == doctest::Approx(2));
    CHECK(bones[1].pivot.z == doctest::Approx(3));

    CHECK(bones[2].parentBone == 1);
    CHECK(bones[2].pivot.x == doctest::Approx(2));
}

TEST_CASE("parseBones: no SKB1 chunk throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKL1", {1});
    CHECK_THROWS_AS(husk::skel::parseBones(file), husk::skel::ParseError);
}

TEST_CASE("boneTrackBlob: returns the SKB1 chunk's own payload bytes verbatim") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKB1", buildSkb1Payload(2));

    auto blob = husk::skel::boneTrackBlob(file);
    CHECK(blob.size() == buildSkb1Payload(2).size());
    CHECK(blob == buildSkb1Payload(2));
}

TEST_CASE("boneTrackBlob: no SKB1 chunk throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKS1", {1});
    CHECK_THROWS_AS(husk::skel::boneTrackBlob(file), husk::skel::ParseError);
}

namespace {

// Builds an SKS1 chunk payload: 32-byte header (global_loops/
// sequence_lookups left zeroed -- unread) followed by `seqCount` raw
// M2Sequence records (0x40 bytes each, same stride husk::m2::parseSequences
// itself uses -- see m2.hpp's Sequence doc comment for why).
std::vector<uint8_t> buildSks1Payload(uint32_t seqCount) {
    constexpr size_t kHeaderSize = 0x20;
    std::vector<uint8_t> payload(kHeaderSize, 0);
    putArray(payload, 0x08, seqCount, static_cast<uint32_t>(kHeaderSize));  // sequences field
    for (uint32_t i = 0; i < seqCount; ++i) {
        size_t off = kHeaderSize + static_cast<size_t>(i) * 0x40;
        payload.resize(off + 0x40, 0);
        uint16_t id = static_cast<uint16_t>(100 + i);
        std::memcpy(payload.data() + off + 0x00, &id, 2);
        uint32_t duration = 1000 + i;
        std::memcpy(payload.data() + off + 0x04, &duration, 4);
    }
    return payload;
}

}  // namespace

TEST_CASE("parseSequences: resolves SKS1's sequences array into real M2Sequence records") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKS1", buildSks1Payload(2));

    auto sequences = husk::skel::parseSequences(file);
    REQUIRE(sequences.size() == 2);
    CHECK(sequences[0].id == 100);
    CHECK(sequences[0].duration == 1000);
    CHECK(sequences[1].id == 101);
    CHECK(sequences[1].duration == 1001);
}

TEST_CASE("parseSequences: no SKS1 chunk throws, names what it found") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKB1", buildSkb1Payload(1));

    CHECK_THROWS_WITH_AS(husk::skel::parseSequences(file), doctest::Contains("SKB1"),
                          husk::skel::ParseError);
}

TEST_CASE("parseSequences: SKS1 payload shorter than its 32-byte header throws") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKS1", {1, 2, 3});
    CHECK_THROWS_AS(husk::skel::parseSequences(file), husk::skel::ParseError);
}

TEST_CASE("findAnimFileIds: reads (anim_id, sub_anim_id, file_id) entries from AFID") {
    auto putU16 = [](std::vector<uint8_t>& b, uint16_t v) {
        b.push_back(static_cast<uint8_t>(v));
        b.push_back(static_cast<uint8_t>(v >> 8));
    };

    std::vector<uint8_t> file;
    std::vector<uint8_t> afid;
    putU16(afid, 60);  // anim_id
    putU16(afid, 1);   // sub_anim_id
    putU32(afid, 4, 1100263);  // file_id, this .skel's own -- not the owning M2's
    appendChunk(file, "AFID", afid);

    auto entries = husk::skel::findAnimFileIds(file);
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 1);
    CHECK((*entries)[0].animId == 60);
    CHECK((*entries)[0].subAnimId == 1);
    CHECK((*entries)[0].fileId == 1100263);
}

TEST_CASE("findAnimFileIds: no AFID chunk returns nullopt, not an error (not every .skel has "
          "one -- e.g. a child skeleton sharing its parent's animations, per SKPD's wiki note)") {
    std::vector<uint8_t> file;
    appendChunk(file, "SKB1", buildSkb1Payload(1));

    CHECK_FALSE(husk::skel::findAnimFileIds(file).has_value());
}
