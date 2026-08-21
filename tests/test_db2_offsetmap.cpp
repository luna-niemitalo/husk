// Synthetic-buffer tests for the WDC5 proof-of-concept parser's offset-map
// ("sparse") section support (src/db2.hpp/.cpp) -- split out of
// tests/test_db2.cpp (FILE_SPLIT_TODO.md Item 4): the offset_map_id_list/
// relationship_map reordering case (flags & 0x02) and every
// db2::decodeOffsetMapRecord test, distinct from the rest of that file's
// fixed-width-section coverage. Byte layout constructed by hand from
// documentation/wowdev-wiki/md/DB2.md's WDC5 structure, same convention as
// test_db2.cpp's own header comment.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/db2.hpp"

namespace {

void putU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) {
    std::memcpy(buf.data() + offset, &v, 2);
}
void putU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) {
    std::memcpy(buf.data() + offset, &v, 4);
}
void putU64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) {
    std::memcpy(buf.data() + offset, &v, 8);
}

constexpr size_t kHeaderSize = 204;  // magic(4) + version(4) + schema(128) + 17 header fields (68)
constexpr size_t kSectionHeaderSize = 40;
constexpr size_t kFieldStructureSize = 4;
constexpr size_t kFieldStorageInfoSize = 24;

// Builds a single-section offset-map ("sparse", flags & 0x01) file with
// `fieldSizeBits.size()` storage-type-None scalar fields (every field
// eligible for decodeOffsetMapRecord's inline-string heuristic requires
// fieldSizeBits == 32, per its own doc comment -- callers pass 32 for a
// string-candidate field, something else to force the plain-integer path)
// and one record per entry of `records`, each entry's raw bytes placed
// verbatim as that record's offset_map-pointed variable_record_data. No
// relationship map (flags == 0x01 only), but db2::parse always reads
// offset_map_id_list once flags & 0x01 is set regardless of relationship
// data (see its own `if (hasOffsetMap && !hasRelationshipData)` tail call)
// -- included here (arbitrary sequential IDs, unused by these tests) so
// the buffer matches what parse actually expects to find.
std::vector<uint8_t> buildOffsetMapFile(const std::vector<uint16_t>& fieldSizeBits,
                                          const std::vector<std::vector<uint8_t>>& records) {
    size_t fieldCount = fieldSizeBits.size();
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize + fieldCount * kFieldStorageInfoSize;

    size_t variableRecordDataSize = 0;
    for (const auto& r : records) variableRecordDataSize += r.size();
    size_t offsetRecordsEnd = sectionFileOffset + variableRecordDataSize;
    size_t offsetMapSize = records.size() * 6;
    size_t offsetMapIdListSize = records.size() * 4;
    size_t total = offsetRecordsEnd + offsetMapSize + offsetMapIdListSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, 0); p += 4;  // recordCount (header-level, unused for offset-map decode)
    putU32(buf, p, static_cast<uint32_t>(fieldCount)); p += 4;
    putU32(buf, p, 0); p += 4;  // recordSize (unused for offset-map sections)
    putU32(buf, p, 0); p += 4;  // stringTableSize
    putU32(buf, p, 0); p += 4;  // tableHash
    putU32(buf, p, 0); p += 4;  // layoutHash
    putU32(buf, p, 1); p += 4;  // minId
    putU32(buf, p, static_cast<uint32_t>(records.size())); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0x01); p += 2;  // flags: offset map only
    putU16(buf, p, 0); p += 2;  // idIndex
    putU32(buf, p, static_cast<uint32_t>(fieldCount)); p += 4;  // totalFieldCount
    putU32(buf, p, 0); p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0); p += 4;  // lookupColumnCount
    putU32(buf, p, static_cast<uint32_t>(fieldCount * kFieldStorageInfoSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // commonDataSize
    putU32(buf, p, 0); p += 4;  // palletDataSize
    putU32(buf, p, 1); p += 4;  // sectionCount
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;  // tactKeyHash
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;  // fileOffset
    putU32(buf, p, static_cast<uint32_t>(records.size())); p += 4;  // recordCount
    putU32(buf, p, 0); p += 4;  // stringTableSize
    putU32(buf, p, static_cast<uint32_t>(offsetRecordsEnd)); p += 4;
    putU32(buf, p, 0); p += 4;  // idListSize
    putU32(buf, p, 0); p += 4;  // relationshipDataSize
    putU32(buf, p, static_cast<uint32_t>(records.size())); p += 4;  // offsetMapIdCount
    putU32(buf, p, 0); p += 4;  // copyTableCount
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    // field_structure: size=0 -> 32-bit elements for every field (fine even
    // for the narrower fields these tests use -- decodeOffsetMapRecord's
    // arrayLength math only cares that fieldSizeBits is itself a multiple
    // of elementBits, and every field here is scalar, fieldSizeBits ==
    // elementBits).
    for (size_t i = 0; i < fieldCount; ++i) {
        putU16(buf, p, 0); p += 2;
        putU16(buf, p, 0); p += 2;
    }
    for (size_t i = 0; i < fieldCount; ++i) {
        putU16(buf, p, 0); p += 2;  // fieldOffsetBits (unused by decodeOffsetMapRecord)
        putU16(buf, p, fieldSizeBits[i]); p += 2;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;  // storageType = None
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
    }
    CHECK(p == sectionFileOffset);

    std::vector<size_t> recordOffsets;
    for (const auto& r : records) {
        recordOffsets.push_back(p);
        std::memcpy(buf.data() + p, r.data(), r.size());
        p += r.size();
    }
    CHECK(p == offsetRecordsEnd);

    for (size_t i = 0; i < records.size(); ++i) {
        putU32(buf, p, static_cast<uint32_t>(recordOffsets[i])); p += 4;
        putU16(buf, p, static_cast<uint16_t>(records[i].size())); p += 2;
    }
    for (size_t i = 0; i < records.size(); ++i) {
        putU32(buf, p, static_cast<uint32_t>(i + 1)); p += 4;  // offset_map_id_list[i], arbitrary
    }
    CHECK(p == total);

    return buf;
}

}  // namespace

// DB2.md's WDC5 struct pseudocode: "if flag 0x02 is set offset_map_id_list
// will appear before relationship_map instead" -- real for the only local
// fixtures with both bits set (Collectable*Sparse tables, see
// db2.cpp's own comment at the read site). Builds a minimal offset-map
// section (flags 0x01 | 0x02) with the reordered layout and checks each
// piece lands where the reorder puts it, not where the default (no-offset-
// map) order would.
TEST_CASE("db2::parse reorders offset_map_id_list before relationship_map when flags & 0x02 is set") {
    size_t variableRecordSize = 4;
    uint32_t offsetMapIdCount = 1;
    uint32_t numEntries = 1;
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + kFieldStructureSize + kFieldStorageInfoSize;
    size_t offsetRecordsEnd = sectionFileOffset + variableRecordSize;
    size_t offsetMapSize = static_cast<size_t>(offsetMapIdCount) * 6;
    size_t offsetMapIdListSize = static_cast<size_t>(offsetMapIdCount) * 4;
    size_t relationshipMapSize = 12 + static_cast<size_t>(numEntries) * 8;
    size_t total = sectionFileOffset + variableRecordSize + offsetMapSize + offsetMapIdListSize + relationshipMapSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, 0); p += 4;  // recordCount (unused by the offset-map path)
    putU32(buf, p, 1); p += 4;  // fieldCount
    putU32(buf, p, 0); p += 4;  // recordSize (unused for offset-map sections)
    putU32(buf, p, 0); p += 4;  // stringTableSize
    putU32(buf, p, 0); p += 4;  // tableHash
    putU32(buf, p, 0); p += 4;  // layoutHash
    putU32(buf, p, 1); p += 4;  // minId
    putU32(buf, p, 1); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0x03); p += 2;  // flags: offset map + relationship data
    putU16(buf, p, 0); p += 2;  // idIndex
    putU32(buf, p, 1); p += 4;  // totalFieldCount
    putU32(buf, p, 0); p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0); p += 4;  // lookupColumnCount
    putU32(buf, p, kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;  // commonDataSize
    putU32(buf, p, 0); p += 4;  // palletDataSize
    putU32(buf, p, 1); p += 4;  // sectionCount
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;  // tactKeyHash
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;  // fileOffset
    putU32(buf, p, 0); p += 4;  // recordCount
    putU32(buf, p, 0); p += 4;  // stringTableSize
    putU32(buf, p, static_cast<uint32_t>(offsetRecordsEnd)); p += 4;
    putU32(buf, p, 0); p += 4;  // idListSize
    putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;
    putU32(buf, p, offsetMapIdCount); p += 4;
    putU32(buf, p, 0); p += 4;  // copyTableCount
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    putU16(buf, p, 0); p += 2;  // field_structure[0].size (unused for offset-map records)
    putU16(buf, p, 0); p += 2;  // field_structure[0].position
    putU16(buf, p, 0); p += 2;  // fieldOffsetBits
    putU16(buf, p, 32); p += 2;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;  // storageType = None
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    CHECK(p == sectionFileOffset);

    p += variableRecordSize;  // variable_record_data, contents irrelevant here

    putU32(buf, p, 0); p += 4;  // offset_map[0].offset == 0 -> "no entry at this ID"
    putU16(buf, p, 0); p += 2;  // offset_map[0].size
    CHECK(p == offsetRecordsEnd + offsetMapSize);

    // Reordered position: offset_map_id_list comes BEFORE relationship_map.
    putU32(buf, p, 999); p += 4;  // offset_map_id_list[0]

    putU32(buf, p, numEntries); p += 4;
    putU32(buf, p, 100); p += 4;  // relationship_map.min_id
    putU32(buf, p, 100); p += 4;  // relationship_map.max_id
    putU32(buf, p, 100); p += 4;  // entry[0].foreign_id
    putU32(buf, p, 100); p += 4;  // entry[0].record_index_or_id
    CHECK(p == total);

    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    REQUIRE(section.offsetMapIdList.size() == 1);
    CHECK(section.offsetMapIdList[0] == 999);
    CHECK(section.hasRelationshipMap());
    REQUIRE(section.relationshipEntries.size() == 1);
    CHECK(section.relationshipEntries[0].foreignId == 100);
    CHECK(section.relationshipEntries[0].recordIndexOrId == 100);
}

// db2::decodeOffsetMapRecord -- real per-field decode of offset-map
// ("sparse") records, added this session. Verified against 5 real local
// files (conversationline.db2, scenescripttext.db2, the three
// collectablesource*sparse.db2 tables -- see CLAUDE_HISTORY.md); these
// synthetic fixtures cover the same three shapes by hand: a plain numeric
// record, a record with a real inline string, and a genuinely corrupt
// offset_map entry.

TEST_CASE("db2::decodeOffsetMapRecord decodes a plain numeric field") {
    // Field 0: a 32-bit scalar -- eligible for the inline-string heuristic
    // by width, but its raw bytes (value 5) start with 0x05, which the
    // heuristic's control-byte rejection (c < 0x09) rules out immediately,
    // so it correctly falls back to a plain integer.
    std::vector<uint8_t> record0 = {5, 0, 0, 0};
    auto buf = buildOffsetMapFile({32}, {record0});
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    REQUIRE(section.hasOffsetMap());

    auto fields = husk::db2::decodeOffsetMapRecord(file, section, 0);
    REQUIRE(fields.size() == 1);
    CHECK_FALSE(fields[0].str.has_value());
    REQUIRE(fields[0].raw.size() == 1);
    CHECK(fields[0].raw[0] == 5);
}

TEST_CASE("db2::decodeOffsetMapRecord resolves a real inline string field, shifting the next field") {
    // Field 0: numeric (value 5, same non-string-shaped bytes as above).
    // Field 1: a real inline string "Hello!" (6 chars, >= the 4-byte
    // minimum) -- decodeOffsetMapRecord must walk field 1 starting right
    // after field 0's own 4 bytes, not at field 1's nominal
    // field_storage_info.field_offset_bits (unset/0 here on purpose, to
    // prove the sequential bit-cursor is what actually drives decoding).
    std::vector<uint8_t> record0 = {5, 0, 0, 0, 'H', 'e', 'l', 'l', 'o', '!', 0};
    auto buf = buildOffsetMapFile({32, 32}, {record0});
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];

    auto fields = husk::db2::decodeOffsetMapRecord(file, section, 0);
    REQUIRE(fields.size() == 2);
    CHECK_FALSE(fields[0].str.has_value());
    REQUIRE(fields[0].raw.size() == 1);
    CHECK(fields[0].raw[0] == 5);
    REQUIRE(fields[1].str.has_value());
    CHECK(*fields[1].str == "Hello!");
}

// Real-data-driven regression: scenescripttext.db2 has trailing scalar
// string fields with no bytes left over for a raw 32-bit reread (a
// genuinely empty string, and a genuinely 1-character string) -- both
// shorter than the normal 4-byte minimum, both only accepted because no
// other reading of the remaining bytes is even possible. See db2.cpp's
// tryReadInlineString call site for the real record bytes this was found
// against.
TEST_CASE("db2::decodeOffsetMapRecord accepts a short trailing string when no raw fallback fits") {
    // Field 0: a long real string, consuming all but the last byte.
    // Field 1: a single trailing NUL -- an empty string, with zero bytes of
    // room left for a raw 32-bit fallback.
    std::vector<uint8_t> record0 = {'H', 'i', '!', '!', 0, 0};
    auto buf = buildOffsetMapFile({32, 32}, {record0});
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];

    auto fields = husk::db2::decodeOffsetMapRecord(file, section, 0);
    REQUIRE(fields.size() == 2);
    REQUIRE(fields[0].str.has_value());
    CHECK(*fields[0].str == "Hi!!");
    REQUIRE(fields[1].str.has_value());
    CHECK(*fields[1].str == "");
}

// A field whose raw bytes contain a high byte (>= 0x80) must never be
// misread as a string, even though resolveFieldString's own (more
// permissive) character class would accept it -- see tryReadInlineString's
// doc comment for the real conversationline.db2 case this guards against
// (a large/negative 32-bit value silently "reading through" into the next
// field's bytes before finding a zero terminator).
TEST_CASE("db2::decodeOffsetMapRecord never treats a high-byte value as a string") {
    // Field 0: 0xfffff63c (-2500 signed) -- byte0 is printable ('<' =
    // 0x3c), but byte1 (0xf6) is a high byte that must reject the match.
    std::vector<uint8_t> record0 = {0x3c, 0xf6, 0xff, 0xff};
    auto buf = buildOffsetMapFile({32}, {record0});
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];

    auto fields = husk::db2::decodeOffsetMapRecord(file, section, 0);
    REQUIRE(fields.size() == 1);
    CHECK_FALSE(fields[0].str.has_value());
    REQUIRE(fields[0].raw.size() == 1);
    CHECK(fields[0].raw[0] == 0xfffff63c);
}

TEST_CASE("db2::decodeOffsetMapRecord throws on a record too short for a declared field") {
    // Field 0 declares 32 bits, but this record's own offset_map-declared
    // length is only 2 bytes -- neither a valid inline string (no NUL
    // terminator within those 2 bytes) nor enough room for a raw 32-bit
    // read. A real, deliberately corrupt/truncated case, not a guess.
    std::vector<uint8_t> record0 = {1, 2};
    auto buf = buildOffsetMapFile({32}, {record0});
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];

    CHECK_THROWS_AS(husk::db2::decodeOffsetMapRecord(file, section, 0), husk::db2::ParseError);
}

TEST_CASE("db2::decodeOffsetMapRecord throws on an out-of-range record index") {
    std::vector<uint8_t> record0 = {1, 0, 0, 0};
    auto buf = buildOffsetMapFile({32}, {record0});
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];

    CHECK_THROWS_AS(husk::db2::decodeOffsetMapRecord(file, section, 5), husk::db2::ParseError);
}

// A corrupt offset_map entry (offset+size running past the file itself) is
// already caught at db2::parse time, not deferred to decode time -- the
// bounds check lives in db2::parse's own section-building loop
// (requireBytes on section.variableRecordBytes entry), covering the "lies
// about its own size" case decodeOffsetMapRecord's per-record length can
// never see (parse would have already thrown before a Section even exists
// to decode from).
TEST_CASE("db2::parse throws on an offset_map entry claiming bytes past the end of the file") {
    std::vector<uint8_t> record0 = {1, 0, 0, 0};
    auto buf = buildOffsetMapFile({32}, {record0});
    // Corrupt the sole offset_map entry's declared size field -- at
    // sectionFileOffset + record0.size() + 4 (past its own offset field),
    // NOT buf.size() - 2, which lands inside the trailing offset_map_id_list
    // block instead (see buildOffsetMapFile's own doc comment for why that
    // block exists).
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + kFieldStructureSize + kFieldStorageInfoSize;
    size_t sizeFieldPos = sectionFileOffset + record0.size() + 4;
    putU16(buf, sizeFieldPos, 9000);
    CHECK_THROWS_AS(husk::db2::parse(buf), husk::db2::ParseError);
}
