// Synthetic-buffer tests for the WDC5 parser's relationship data
// (src/db2.hpp/.cpp): the relationship map without an offset map, its
// "$noninline,relation$" per-record values, and an all-zero map region.
// Byte layout built by hand, same convention as tests/test_db2.cpp's header
// comment.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/db2.hpp"
#include "test_db2_fixtures.hpp"

// relationship_mapping (DB2.md's WDC5 struct): num_entries/min_id/max_id
// then num_entries {foreign_id, record_index} pairs, right after the
// (empty, in this fixture) copy table -- no offset map involved, so no
// reordering quirk applies here (test_db2_offsetmap.cpp covers that).
TEST_CASE("db2::parse decodes a relationship map with no offset map involved") {
    uint32_t recordCount = 1;
    uint32_t numEntries = 2;
    size_t recordSize = 8;
    size_t stringTableSize = 1;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize + 2 * kFieldStorageInfoSize;
    size_t relationshipMapSize = 12 + static_cast<size_t>(numEntries) * 8;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize + relationshipMapSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, 2); p += 4;  // fieldCount
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // tableHash
    putU32(buf, p, 0); p += 4;  // layoutHash
    putU32(buf, p, 1); p += 4;  // minId
    putU32(buf, p, recordCount); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0x02); p += 2;  // flags: has relationship data
    putU16(buf, p, 0); p += 2;  // idIndex
    putU32(buf, p, 2); p += 4;  // totalFieldCount
    putU32(buf, p, 0); p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0); p += 4;  // lookupColumnCount
    putU32(buf, p, 2 * kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;  // commonDataSize
    putU32(buf, p, 0); p += 4;  // palletDataSize
    putU32(buf, p, 1); p += 4;  // sectionCount
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;  // tactKeyHash
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;  // fileOffset
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // offsetRecordsEnd
    putU32(buf, p, 0); p += 4;  // idListSize
    putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;  // relationshipDataSize
    putU32(buf, p, 0); p += 4;  // offsetMapIdCount
    putU32(buf, p, 0); p += 4;  // copyTableCount
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    for (int i = 0; i < 2; ++i) { putU16(buf, p, 0); p += 2; putU16(buf, p, static_cast<uint16_t>(i * 4)); p += 2; }
    for (int i = 0; i < 2; ++i) {
        putU16(buf, p, static_cast<uint16_t>(i * 32)); p += 2;
        putU16(buf, p, 32); p += 2;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;  // storageType = None
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
    }
    CHECK(p == sectionFileOffset);

    for (uint32_t r = 0; r < recordCount; ++r) {
        putU32(buf, p, r + 1); p += 4;
        putU32(buf, p, (r + 1) * 100); p += 4;
    }
    p += stringTableSize;  // string block: single zero byte, already zero-initialized

    putU32(buf, p, numEntries); p += 4;
    putU32(buf, p, 500); p += 4;  // relationship_map.min_id
    putU32(buf, p, 600); p += 4;  // relationship_map.max_id
    putU32(buf, p, 500); p += 4;  // entry[0].foreign_id
    putU32(buf, p, 0); p += 4;    // entry[0].record_index
    putU32(buf, p, 600); p += 4;  // entry[1].foreign_id
    putU32(buf, p, 7); p += 4;    // entry[1].record_index
    CHECK(p == total);

    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    CHECK(section.hasRelationshipMap());
    CHECK(section.relationshipMinId == 500);
    CHECK(section.relationshipMaxId == 600);
    REQUIRE(section.relationshipEntries.size() == 2);
    CHECK(section.relationshipEntries[0].foreignId == 500);
    CHECK(section.relationshipEntries[0].recordIndexOrId == 0);
    CHECK(section.relationshipEntries[1].foreignId == 600);
    CHECK(section.relationshipEntries[1].recordIndexOrId == 7);
}

// db2::nonInlineRelationValuesByRecord resolves a "$noninline,relation$"
// column's real per-record value from the relationship map -- verified for
// real against chrmodeltexturelayer.db2, whose
// CharComponentTextureLayoutsID is stored exactly this way under its
// current real layout, header.flags & 0x02 clear (recordIndexOrId is a
// real record *index* in this case, not an ID -- see db2.hpp's own doc
// comment for why the 0x02-set case is deliberately unhandled).
TEST_CASE("db2::nonInlineRelationValuesByRecord resolves per-record foreign IDs when flags & 0x02 is clear") {
    uint32_t recordCount = 3;
    uint32_t numEntries = 3;
    size_t recordSize = 4;
    size_t stringTableSize = 1;
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + kFieldStructureSize + kFieldStorageInfoSize;
    size_t relationshipMapSize = 12 + static_cast<size_t>(numEntries) * 8;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize + relationshipMapSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, 1); p += 4;  // fieldCount
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // tableHash
    putU32(buf, p, 0); p += 4;  // layoutHash
    putU32(buf, p, 1); p += 4;  // minId
    putU32(buf, p, recordCount); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0); p += 2;  // flags: NOT 0x02 -- recordIndexOrId is a real index
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
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // offsetRecordsEnd
    putU32(buf, p, 0); p += 4;  // idListSize
    putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // offsetMapIdCount
    putU32(buf, p, 0); p += 4;  // copyTableCount
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    putU16(buf, p, 0); p += 2;
    putU16(buf, p, 0); p += 2;
    putU16(buf, p, 0); p += 2;
    putU16(buf, p, 32); p += 2;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;  // storageType = None
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    CHECK(p == sectionFileOffset);

    for (uint32_t r = 0; r < recordCount; ++r) { putU32(buf, p, r); p += 4; }
    p += stringTableSize;

    putU32(buf, p, numEntries); p += 4;
    putU32(buf, p, 900); p += 4;  // relationship_map.min_id
    putU32(buf, p, 900); p += 4;  // relationship_map.max_id
    // entries: record index 0 and 1 both belong to foreign_id 900,
    // record index 2 belongs to foreign_id 901.
    putU32(buf, p, 900); p += 4; putU32(buf, p, 0); p += 4;
    putU32(buf, p, 900); p += 4; putU32(buf, p, 1); p += 4;
    putU32(buf, p, 901); p += 4; putU32(buf, p, 2); p += 4;
    CHECK(p == total);

    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    auto values = husk::db2::nonInlineRelationValuesByRecord(file, section);
    REQUIRE(values.size() == 3);
    REQUIRE(values[0].has_value());
    CHECK(*values[0] == 900);
    REQUIRE(values[1].has_value());
    CHECK(*values[1] == 900);
    REQUIRE(values[2].has_value());
    CHECK(*values[2] == 901);
}

TEST_CASE("db2::nonInlineRelationValuesByRecord returns empty when header.flags & 0x02 is set") {
    // Reuses the earlier "no offset map" relationship-map fixture, whose
    // header.flags is 0x02 -- per DB2.md's own note, relationship_entry
    // holds a real record ID in that case, not an index this function
    // knows how to invert, so it must return {} rather than guess.
    uint32_t recordCount = 1;
    uint32_t numEntries = 2;
    size_t recordSize = 8;
    size_t stringTableSize = 1;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize + 2 * kFieldStorageInfoSize;
    size_t relationshipMapSize = 12 + static_cast<size_t>(numEntries) * 8;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize + relationshipMapSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, 2); p += 4;
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 1); p += 4;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, 0); p += 4;
    putU16(buf, p, 0x02); p += 2;  // flags: has relationship data
    putU16(buf, p, 0); p += 2;
    putU32(buf, p, 2); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 2 * kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 1); p += 4;
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    for (int i = 0; i < 2; ++i) { putU16(buf, p, 0); p += 2; putU16(buf, p, static_cast<uint16_t>(i * 4)); p += 2; }
    for (int i = 0; i < 2; ++i) {
        putU16(buf, p, static_cast<uint16_t>(i * 32)); p += 2;
        putU16(buf, p, 32); p += 2;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
    }
    CHECK(p == sectionFileOffset);

    for (uint32_t r = 0; r < recordCount; ++r) { putU32(buf, p, r + 1); p += 4; putU32(buf, p, (r + 1) * 100); p += 4; }
    p += stringTableSize;

    putU32(buf, p, numEntries); p += 4;
    putU32(buf, p, 500); p += 4;
    putU32(buf, p, 600); p += 4;
    putU32(buf, p, 500); p += 4; putU32(buf, p, 0); p += 4;
    putU32(buf, p, 600); p += 4; putU32(buf, p, 7); p += 4;
    CHECK(p == total);

    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    CHECK(husk::db2::nonInlineRelationValuesByRecord(file, section).empty());
}

// A relationshipMap region that reads as all-zero (a genuinely missing/
// truncated chunk, the same real-world shape recordsAvailable() handles
// for record bytes -- see db2.cpp's own comment at the read site) must
// degrade to "no relationship data" rather than throwing ParseError and
// losing the rest of the file over one missing region.
TEST_CASE("db2::parse treats an all-zero relationshipMap region as unavailable, not corrupt") {
    uint32_t recordCount = 1;
    size_t recordSize = 8;
    size_t stringTableSize = 1;
    uint32_t numEntries = 2;
    size_t relationshipMapSize = 12 + static_cast<size_t>(numEntries) * 8;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize + 2 * kFieldStorageInfoSize;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize + relationshipMapSize;

    std::vector<uint8_t> buf(total, 0);  // relationshipMap region stays all-zero
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, 2); p += 4;
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 1); p += 4;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, 0); p += 4;
    putU16(buf, p, 0x02); p += 2;  // flags: has relationship data
    putU16(buf, p, 0); p += 2;
    putU32(buf, p, 2); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 2 * kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 1); p += 4;
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;  // tactKeyHash -- not the cause here, distinct from recordsAvailable
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;  // relationshipDataSize
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    for (int i = 0; i < 2; ++i) { putU16(buf, p, 0); p += 2; putU16(buf, p, static_cast<uint16_t>(i * 4)); p += 2; }
    for (int i = 0; i < 2; ++i) {
        putU16(buf, p, static_cast<uint16_t>(i * 32)); p += 2;
        putU16(buf, p, 32); p += 2;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
    }
    CHECK(p == sectionFileOffset);

    for (uint32_t r = 0; r < recordCount; ++r) {
        putU32(buf, p, r + 1); p += 4;
        putU32(buf, p, (r + 1) * 100); p += 4;
    }
    p += stringTableSize;
    p += relationshipMapSize;  // left all-zero: numEntries/minId/maxId/entries all read as 0
    CHECK(p == total);

    auto file = husk::db2::parse(buf);  // must not throw
    const husk::db2::Section& section = file.sections[0];
    CHECK(section.relationshipEntries.empty());
    CHECK(section.relationshipMinId == 0);
    CHECK(section.relationshipMaxId == 0);
}
