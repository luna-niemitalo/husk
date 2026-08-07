// Tests for husk::m2's primitives module (src/m2_primitives.hpp/.cpp): parseHeader/expansionForVersion/extractBlob.
// Split out of the former tests/test_m2.cpp -- see FILE_SPLIT_TODO.md Item 5.

#include "test_m2_fixtures.hpp"

TEST_CASE("parseHeader: pre-Legion flat MD20 reads every fixed field correctly") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK(h.magic == 0x3032444D);  // "MD20" little-endian
    CHECK_FALSE(h.chunked);
    checkSentinelHeader(h);
}


TEST_CASE("parseHeader: Legion+ chunked file resolves MD21 regardless of chunk order") {
    auto md20 = buildMd20Blob();

    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1, 2, 3, 4});  // unrelated chunk before MD21
    appendChunk(file, "MD21", md20);
    appendChunk(file, "PFID", {5, 6, 7, 8});  // unrelated chunk after MD21

    auto h = husk::m2::parseHeader(file);
    CHECK(h.chunked);
    checkSentinelHeader(h);
}


TEST_CASE("parseHeader: SKID chunk, when present, is surfaced as skeletonFileId") {
    auto md20 = buildMd20Blob();

    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    uint32_t fileDataId = 0x00ABCDEFu;
    uint8_t skidPayload[4];
    std::memcpy(skidPayload, &fileDataId, 4);
    appendChunk(file, "SKID", std::vector<uint8_t>(skidPayload, skidPayload + 4));

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.skeletonFileId.has_value());
    CHECK(*h.skeletonFileId == 0x00ABCDEFu);
}


TEST_CASE("parseHeader: no SKID chunk leaves skeletonFileId empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);

    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.skeletonFileId.has_value());
}


TEST_CASE("parseHeader: flat (non-chunked) MD20 file never has skeletonFileId") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK_FALSE(h.skeletonFileId.has_value());
}


TEST_CASE("parseHeader: PFID chunk, when present, is surfaced as physFileId") {
    auto md20 = buildMd20Blob();

    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    uint32_t fileDataId = 0x00123456u;
    uint8_t pfidPayload[4];
    std::memcpy(pfidPayload, &fileDataId, 4);
    appendChunk(file, "PFID", std::vector<uint8_t>(pfidPayload, pfidPayload + 4));

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.physFileId.has_value());
    CHECK(*h.physFileId == 0x00123456u);
}


TEST_CASE("parseHeader: no PFID chunk leaves physFileId empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);

    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.physFileId.has_value());
}


TEST_CASE("parseHeader: flat (non-chunked) MD20 file never has physFileId") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK_FALSE(h.physFileId.has_value());
}


TEST_CASE("parseHeader: chunked file with no MD21 chunk throws, names what it found") {
    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1});
    appendChunk(file, "PFID", {2});

    CHECK_THROWS_WITH_AS(husk::m2::parseHeader(file), doctest::Contains("SFID"),
                          husk::m2::ParseError);
}


TEST_CASE("parseHeader: too short to hold a magic value throws") {
    std::vector<uint8_t> file = {'M', 'D'};
    CHECK_THROWS_AS(husk::m2::parseHeader(file), husk::m2::ParseError);
}


TEST_CASE("parseHeader: MD20 blob shorter than the fixed header throws") {
    std::vector<uint8_t> file(0x50, 0);  // way short of 0xD8
    std::memcpy(file.data(), "MD20", 4);
    CHECK_THROWS_AS(husk::m2::parseHeader(file), husk::m2::ParseError);
}


TEST_CASE("parseHeader: wrong magic in a flat (non-chunked) file throws") {
    std::vector<uint8_t> file(kFixedHeaderSize, 0);
    std::memcpy(file.data(), "XXXX", 4);
    CHECK_THROWS_AS(husk::m2::parseHeader(file), husk::m2::ParseError);
}


TEST_CASE("parseHeader: name array pointing past the end of the blob throws") {
    auto blob = buildMd20Blob();
    // Corrupt the name array to claim far more bytes than the blob has.
    putArray(blob, 0x008, 0xFFFFFF, static_cast<uint32_t>(kFixedHeaderSize));
    CHECK_THROWS_AS(husk::m2::parseHeader(blob), husk::m2::ParseError);
}


TEST_CASE("parseHeader: empty name (count 0) is allowed, per the wiki's 9.2.0.41462+ note") {
    auto blob = buildMd20Blob();
    putArray(blob, 0x008, 0, 0);
    auto h = husk::m2::parseHeader(blob);
    CHECK(h.name.empty());
}


TEST_CASE("expansionForVersion: matches the wiki's version table, overlaps included") {
    // Rows, transcribed from wowdev.wiki M2#Versions:
    //   256       Pre-Release
    //   256-257   Classic
    //   260-263   The Burning Crusade
    //   264       Wrath of the Lich King
    //   265-272   Cataclysm
    //   272       Mists of Pandaria / Warlords of Draenor
    //   272-274   Legion / Battle for Azeroth / Shadowlands
    // The wiki itself calls these "rough estimates" of overlapping ranges,
    // so a version matching multiple rows (e.g. 272) is the documented
    // behavior, not a bug.
    CHECK(husk::m2::expansionForVersion(256) == "Pre-Release or Classic");
    CHECK(husk::m2::expansionForVersion(257) == "Classic");
    CHECK(husk::m2::expansionForVersion(261) == "The Burning Crusade");
    CHECK(husk::m2::expansionForVersion(264) == "Wrath of the Lich King");
    CHECK(husk::m2::expansionForVersion(266) == "Cataclysm");
    CHECK(husk::m2::expansionForVersion(272) ==
          "Cataclysm or Mists of Pandaria / Warlords of Draenor or "
          "Legion / Battle for Azeroth / Shadowlands");
    CHECK(husk::m2::expansionForVersion(274) == "Legion / Battle for Azeroth / Shadowlands");
    CHECK(husk::m2::expansionForVersion(999) == "unknown");
}


TEST_CASE("extractBlob: flat MD20 file returns the file bytes verbatim") {
    auto blob = buildMd20Blob();
    auto extracted = husk::m2::extractBlob(blob);
    CHECK(extracted == blob);
}


TEST_CASE("extractBlob: Legion+ chunked file returns just the MD21 payload") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1, 2, 3, 4});
    appendChunk(file, "MD21", md20);
    auto extracted = husk::m2::extractBlob(file);
    CHECK(extracted == md20);
}


TEST_CASE("parseHeader: TXID chunk, when present, is surfaced as textureFileDataIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // Three FileDataIDs: two real, one 0 (a non-file-based texture type,
    // wowdev.wiki M2#TXID).
    std::vector<uint8_t> txidPayload;
    for (uint32_t id : {1034713u, 0u, 220043u}) {
        uint8_t bytes[4];
        std::memcpy(bytes, &id, 4);
        txidPayload.insert(txidPayload.end(), bytes, bytes + 4);
    }
    appendChunk(file, "TXID", txidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.textureFileDataIds.has_value());
    REQUIRE(h.textureFileDataIds->size() == 3);
    CHECK((*h.textureFileDataIds)[0] == 1034713u);
    CHECK((*h.textureFileDataIds)[1] == 0u);
    CHECK((*h.textureFileDataIds)[2] == 220043u);
}


TEST_CASE("parseHeader: no TXID chunk leaves textureFileDataIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.textureFileDataIds.has_value());
}


TEST_CASE("parseHeader: flat (non-chunked) MD20 file never has textureFileDataIds") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK_FALSE(h.textureFileDataIds.has_value());
}

// A TXID chunk whose byte length isn't a multiple of 4 (one uint32
// FileDataID per entry) must fail loudly, not silently truncate the last
// partial entry with no error and no count mismatch a caller could
// notice -- same class of foreign-data mismatch every other
// fixed-record-array parser in this codebase already fails loudly on.
// TODO: Remove: FAILURES2.md #8/FAILURES.md #2 (the findings this is a
// regression test for).

TEST_CASE("parseHeader: TXID chunk with a byte length not a multiple of 4 throws, rather than "
          "silently dropping the trailing partial entry") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    appendChunk(file, "TXID", {1, 2, 3, 4, 5, 6});  // 6 bytes: 1 whole entry + 2 stray bytes
    CHECK_THROWS_WITH_AS(husk::m2::parseHeader(file), doctest::Contains("TXID"),
                          husk::m2::ParseError);
}

// Forward-compatibility: husk::readChunks (src/chunk.cpp) is tag-agnostic
// by construction -- it doesn't validate chunk tags against any list, so a
// brand-new chunk a future client build ships (this format adds them
// often, see README.md's Design notes) doesn't break parsing on its own.
// `Header::chunkTags` is what turns that "silently fine" default into an
// actual diagnostic opportunity for callers (see cmd_info.cpp's
// documentedM2ChunkTags/isUndocumentedChunkTag) -- these tests lock in
// both halves: chunkTags is populated correctly, and a chunk tag this
// parser has genuinely never heard of doesn't throw.

TEST_CASE("parseHeader: chunkTags lists every top-level chunk tag, in file order") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "SFID", {1, 2, 3, 4});
    appendChunk(file, "MD21", md20);
    appendChunk(file, "TXID", {5, 6, 7, 8});

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.chunkTags.size() == 3);
    CHECK(h.chunkTags[0] == "SFID");
    CHECK(h.chunkTags[1] == "MD21");
    CHECK(h.chunkTags[2] == "TXID");
}


TEST_CASE("parseHeader: flat (non-chunked) MD20 file has an empty chunkTags") {
    auto blob = buildMd20Blob();
    auto h = husk::m2::parseHeader(blob);
    CHECK(h.chunkTags.empty());
}


TEST_CASE("parseHeader: a chunk tag this parser has never heard of is tolerated, not an error") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // "ZZZZ" is deliberately not a real wowdev.wiki-documented M2 chunk tag
    // (see cmd_info.cpp's documentedM2ChunkTags) -- stands in for whatever
    // the next client build actually adds.
    appendChunk(file, "ZZZZ", {0xDE, 0xAD, 0xBE, 0xEF});

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.chunkTags.size() == 2);
    CHECK(h.chunkTags[1] == "ZZZZ");
}


TEST_CASE("parseHeader: SFID chunk, when present, is surfaced as skinFileDataIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    std::vector<uint8_t> sfidPayload;
    for (uint32_t id : {469824u, 469830u}) {
        uint8_t bytes[4];
        std::memcpy(bytes, &id, 4);
        sfidPayload.insert(sfidPayload.end(), bytes, bytes + 4);
    }
    appendChunk(file, "SFID", sfidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.skinFileDataIds.has_value());
    REQUIRE(h.skinFileDataIds->size() == 2);
    CHECK((*h.skinFileDataIds)[0] == 469824u);
    CHECK((*h.skinFileDataIds)[1] == 469830u);
}


TEST_CASE("parseHeader: no SFID chunk leaves skinFileDataIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.skinFileDataIds.has_value());
}


TEST_CASE("parseHeader: LDV1 chunk, when present, is surfaced as lodCount") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // struct LodData { uint16 unk0; uint16 lodCount; ... } -- only the
    // first 4 bytes matter to husk.
    std::vector<uint8_t> ldv1Payload = {0x08, 0x00, 0x03, 0x00};  // unk0=8, lodCount=3
    appendChunk(file, "LDV1", ldv1Payload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.lodCount.has_value());
    CHECK(*h.lodCount == 3);
}


TEST_CASE("parseHeader: no LDV1 chunk leaves lodCount empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.lodCount.has_value());
}


TEST_CASE("parseHeader: BFID chunk, when present, is surfaced as boneFileDataIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    std::vector<uint8_t> bfidPayload;
    for (uint32_t id : {100001u, 100002u, 100003u}) {
        uint8_t bytes[4];
        std::memcpy(bytes, &id, 4);
        bfidPayload.insert(bfidPayload.end(), bytes, bytes + 4);
    }
    appendChunk(file, "BFID", bfidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.boneFileDataIds.has_value());
    REQUIRE(h.boneFileDataIds->size() == 3);
    CHECK((*h.boneFileDataIds)[1] == 100002u);
}


TEST_CASE("parseHeader: no BFID chunk leaves boneFileDataIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.boneFileDataIds.has_value());
}


TEST_CASE("parseHeader: AFID chunk, when present, is surfaced as animFileIds") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // { uint16 anim_id; uint16 sub_anim_id; uint32 file_id; }[]
    std::vector<uint8_t> afidPayload;
    auto putEntry = [&](uint16_t animId, uint16_t subAnimId, uint32_t fileId) {
        uint8_t a[2], s[2], f[4];
        std::memcpy(a, &animId, 2);
        std::memcpy(s, &subAnimId, 2);
        std::memcpy(f, &fileId, 4);
        afidPayload.insert(afidPayload.end(), a, a + 2);
        afidPayload.insert(afidPayload.end(), s, s + 2);
        afidPayload.insert(afidPayload.end(), f, f + 4);
    };
    putEntry(120, 0, 469839);
    putEntry(119, 1, 469836);
    appendChunk(file, "AFID", afidPayload);

    auto h = husk::m2::parseHeader(file);
    REQUIRE(h.animFileIds.has_value());
    REQUIRE(h.animFileIds->size() == 2);
    CHECK((*h.animFileIds)[0].animId == 120);
    CHECK((*h.animFileIds)[0].subAnimId == 0);
    CHECK((*h.animFileIds)[0].fileId == 469839u);
    CHECK((*h.animFileIds)[1].animId == 119);
    CHECK((*h.animFileIds)[1].subAnimId == 1);
    CHECK((*h.animFileIds)[1].fileId == 469836u);
}


TEST_CASE("parseHeader: no AFID chunk leaves animFileIds empty") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    auto h = husk::m2::parseHeader(file);
    CHECK_FALSE(h.animFileIds.has_value());
}

// AFID's 8-byte-record version of the TXID test above.
// TODO: Remove: FAILURES2.md #8 citation.

TEST_CASE("parseHeader: AFID chunk with a byte length not a multiple of 8 throws, rather than "
          "silently dropping the trailing partial entry") {
    auto md20 = buildMd20Blob();
    std::vector<uint8_t> file;
    appendChunk(file, "MD21", md20);
    // 10 bytes: 1 whole {anim_id, sub_anim_id, file_id} entry (8 bytes) + 2
    // stray bytes.
    appendChunk(file, "AFID", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    CHECK_THROWS_WITH_AS(husk::m2::parseHeader(file), doctest::Contains("AFID"),
                          husk::m2::ParseError);
}

