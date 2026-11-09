// Synthetic-buffer tests for the WDC5 proof-of-concept parser (src/db2.hpp/
// .cpp). Byte layout constructed by hand from documentation/wowdev-wiki/md/
// DB2.md's WDC5 structure -- cross-checked once against a real file
// (chrmodelmaterial.db2, see db2.hpp's module comment) but these fixtures
// are independently built, not copied from that file, so they exercise the
// parser's own offset arithmetic rather than just replaying known-good
// bytes. This file covers fixed-width sections; offset-map ("sparse")
// sections are in tests/test_db2_offsetmap.cpp, relationship data in
// tests/test_db2_relations.cpp.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/db2.hpp"
#include "test_db2_fixtures.hpp"

namespace {

// One section, two plain (storageType None) uint32 fields per record,
// `recordCount` records, each record {field0=id, field1=100*id}. No pallet/
// common data, no offset map, string_table_size 1 (a single trailing zero
// byte, matching DB2.md's "almost always contains at least one zero-byte").
std::vector<uint8_t> buildSimpleFile(uint32_t recordCount) {
    size_t recordSize = 8;
    size_t stringTableSize = 1;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize + 2 * kFieldStorageInfoSize;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);
    std::memcpy(buf.data() + 8, "TEST_SCHEMA", 11);

    size_t p = 8 + 128;
    putU32(buf, p, recordCount);
    p += 4;  // recordCount
    putU32(buf, p, 2);
    p += 4;  // fieldCount
    putU32(buf, p, static_cast<uint32_t>(recordSize));
    p += 4;  // recordSize
    putU32(buf, p, static_cast<uint32_t>(stringTableSize));
    p += 4;  // stringTableSize
    putU32(buf, p, 0);
    p += 4;  // tableHash
    putU32(buf, p, 0);
    p += 4;  // layoutHash
    putU32(buf, p, 1);
    p += 4;  // minId
    putU32(buf, p, recordCount);
    p += 4;  // maxId
    putU32(buf, p, 0);
    p += 4;  // locale
    putU16(buf, p, 0);
    p += 2;  // flags
    putU16(buf, p, 0);
    p += 2;  // idIndex
    putU32(buf, p, 2);
    p += 4;  // totalFieldCount
    putU32(buf, p, 0);
    p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0);
    p += 4;  // lookupColumnCount
    putU32(buf, p, 2 * kFieldStorageInfoSize);
    p += 4;  // fieldStorageInfoSize
    putU32(buf, p, 0);
    p += 4;  // commonDataSize
    putU32(buf, p, 0);
    p += 4;  // palletDataSize
    putU32(buf, p, 1);
    p += 4;  // sectionCount
    CHECK(p == kHeaderSize);

    // section_headers[0]
    putU64(buf, p, 0);
    p += 8;  // tactKeyHash
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset));
    p += 4;  // fileOffset
    putU32(buf, p, recordCount);
    p += 4;  // recordCount
    putU32(buf, p, static_cast<uint32_t>(stringTableSize));
    p += 4;  // stringTableSize
    putU32(buf, p, 0);
    p += 4;  // offsetRecordsEnd
    putU32(buf, p, 0);
    p += 4;  // idListSize
    putU32(buf, p, 0);
    p += 4;  // relationshipDataSize
    putU32(buf, p, 0);
    p += 4;  // offsetMapIdCount
    putU32(buf, p, 0);
    p += 4;  // copyTableCount
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    // field_structure[0..1]: both plain uint32 (size=0 -> 32-bit elements)
    putU16(buf, p, 0);
    p += 2;
    putU16(buf, p, 0);
    p += 2;  // field 0: position 0
    putU16(buf, p, 0);
    p += 2;
    putU16(buf, p, 4);
    p += 2;  // field 1: position 4

    // field_storage_info[0..1]: storageType None, fieldSizeBits=32
    for (int i = 0; i < 2; ++i) {
        putU16(buf, p, static_cast<uint16_t>(i * 32));
        p += 2;  // fieldOffsetBits
        putU16(buf, p, 32);
        p += 2;  // fieldSizeBits
        putU32(buf, p, 0);
        p += 4;  // additionalDataSize
        putU32(buf, p, 0);
        p += 4;  // storageType = None
        putU32(buf, p, 0);
        p += 4;
        putU32(buf, p, 0);
        p += 4;
        putU32(buf, p, 0);
        p += 4;
    }
    CHECK(p == sectionFileOffset);

    for (uint32_t r = 0; r < recordCount; ++r) {
        putU32(buf, p, r + 1);
        p += 4;  // field 0: id
        putU32(buf, p, (r + 1) * 100);
        p += 4;  // field 1
    }
    // string block: single zero byte already present from the zero-init.

    return buf;
}

}  // namespace

TEST_CASE("db2::parse reads a synthetic WDC5 header correctly") {
    auto buf = buildSimpleFile(2);
    auto file = husk::db2::parse(buf);

    CHECK(file.header.versionNum == 5);
    CHECK(file.header.schemaString == "TEST_SCHEMA");
    CHECK(file.header.recordCount == 2);
    CHECK(file.header.fieldCount == 2);
    CHECK(file.header.recordSize == 8);
    CHECK(file.header.sectionCount == 1);
    CHECK(file.sections.size() == 1);
    CHECK(file.sections[0].header.recordCount == 2);
    CHECK(file.sections[0].recordBytes.size() == 16);
    CHECK(file.fieldStorageInfo.size() == 2);
}

TEST_CASE("db2::decodeField reads plain uint32 fields in row order") {
    auto buf = buildSimpleFile(3);
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];

    for (size_t r = 0; r < 3; ++r) {
        auto field0 = husk::db2::decodeField(file, section, r, 0);
        auto field1 = husk::db2::decodeField(file, section, r, 1);
        REQUIRE(field0.size() == 1);
        REQUIRE(field1.size() == 1);
        CHECK(field0[0] == r + 1);
        CHECK(field1[0] == (r + 1) * 100);
    }
}

TEST_CASE("db2::recordId reads the inline id field when header.flags & 0x04 is not set") {
    auto buf = buildSimpleFile(3);
    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    for (size_t r = 0; r < 3; ++r) {
        CHECK(husk::db2::recordId(file, section, r) == r + 1);
    }
}

// header.flags & 0x04 ("has non-inline IDs"): the real ID lives in
// section.idList, parallel to record order, not in any field-array slot --
// db2::recordId must prefer idList over header.idIndex whenever it's
// present, per DB2.md's own note (see db2.hpp's recordId doc comment).
TEST_CASE("db2::recordId reads section.idList when header.flags & 0x04 is set") {
    uint32_t recordCount = 3;
    size_t recordSize = 4;
    size_t stringTableSize = 1;
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + kFieldStructureSize + kFieldStorageInfoSize;
    size_t idListSize = recordCount * 4;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize + idListSize;

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
    putU32(buf, p, 100); p += 4;  // minId
    putU32(buf, p, 102); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0x04); p += 2;  // flags: has non-inline IDs
    putU16(buf, p, 0); p += 2;  // idIndex (ignored -- flags & 0x04 is set)
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
    putU32(buf, p, static_cast<uint32_t>(idListSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // relationshipDataSize
    putU32(buf, p, 0); p += 4;  // offsetMapIdCount
    putU32(buf, p, 0); p += 4;  // copyTableCount
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    putU16(buf, p, 0); p += 2;  // field_structure.size
    putU16(buf, p, 0); p += 2;  // field_structure.position
    putU16(buf, p, 0); p += 2;  // fieldOffsetBits
    putU16(buf, p, 32); p += 2;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;  // storageType = None
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    CHECK(p == sectionFileOffset);

    for (uint32_t r = 0; r < recordCount; ++r) { putU32(buf, p, r * 10); p += 4; }
    p += stringTableSize;
    for (uint32_t r = 0; r < recordCount; ++r) { putU32(buf, p, 100 + r); p += 4; }
    CHECK(p == total);

    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    REQUIRE(section.idList.size() == 3);
    CHECK(husk::db2::recordId(file, section, 0) == 100);
    CHECK(husk::db2::recordId(file, section, 1) == 101);
    CHECK(husk::db2::recordId(file, section, 2) == 102);
}

TEST_CASE("db2::decodeField out-of-range record index throws ParseError") {
    auto buf = buildSimpleFile(2);
    auto file = husk::db2::parse(buf);
    CHECK_THROWS_AS(husk::db2::decodeField(file, file.sections[0], 5, 0), husk::db2::ParseError);
}

TEST_CASE("db2::parse rejects a non-WDC5 magic") {
    auto buf = buildSimpleFile(1);
    std::memcpy(buf.data(), "WDC4", 4);
    CHECK_THROWS_AS(husk::db2::parse(buf), husk::db2::ParseError);
}

TEST_CASE("db2::parse rejects a truncated buffer") {
    auto buf = buildSimpleFile(2);
    buf.resize(buf.size() - 10);
    CHECK_THROWS_AS(husk::db2::parse(buf), husk::db2::ParseError);
}

// A single bitpacked, sign-extended 4-bit field packed at bit offset 0 of a
// 1-byte record -- exercises readBits' shift/mask and the sign-extension
// path independently of the "plain uint32 field" fixture above. Built
// directly (not via buildSimpleFile, whose fields are all storageType None)
// since the union layout for Bitpacked differs (word 3 holds the sign-
// extend flag, per DB2.md's field_storage_info tagged union).
TEST_CASE("db2::decodeField sign-extends a bitpacked field") {
    size_t recordSize = 1;
    size_t stringTableSize = 1;
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + kFieldStructureSize + kFieldStorageInfoSize;
    size_t total = sectionFileOffset + recordSize + stringTableSize;
    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, 1);
    p += 4;  // recordCount
    putU32(buf, p, 1);
    p += 4;  // fieldCount
    putU32(buf, p, static_cast<uint32_t>(recordSize));
    p += 4;  // recordSize
    putU32(buf, p, static_cast<uint32_t>(stringTableSize));
    p += 4;  // stringTableSize
    putU32(buf, p, 0);
    p += 4;  // tableHash
    putU32(buf, p, 0);
    p += 4;  // layoutHash
    putU32(buf, p, 1);
    p += 4;  // minId
    putU32(buf, p, 1);
    p += 4;  // maxId
    putU32(buf, p, 0);
    p += 4;  // locale
    putU16(buf, p, 0);
    p += 2;  // flags
    putU16(buf, p, 0);
    p += 2;  // idIndex
    putU32(buf, p, 1);
    p += 4;  // totalFieldCount
    putU32(buf, p, 0);
    p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0);
    p += 4;  // lookupColumnCount
    putU32(buf, p, kFieldStorageInfoSize);
    p += 4;  // fieldStorageInfoSize
    putU32(buf, p, 0);
    p += 4;  // commonDataSize
    putU32(buf, p, 0);
    p += 4;  // palletDataSize
    putU32(buf, p, 1);
    p += 4;  // sectionCount
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0);
    p += 8;
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset));
    p += 4;
    putU32(buf, p, 1);
    p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize));
    p += 4;
    putU32(buf, p, 0);
    p += 4;
    putU32(buf, p, 0);
    p += 4;
    putU32(buf, p, 0);
    p += 4;
    putU32(buf, p, 0);
    p += 4;
    putU32(buf, p, 0);
    p += 4;
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    putU16(buf, p, 0);
    p += 2;  // field_structure.size (unused for Bitpacked decode)
    putU16(buf, p, 0);
    p += 2;  // field_structure.position

    putU16(buf, p, 0);
    p += 2;  // fieldOffsetBits
    putU16(buf, p, 4);
    p += 2;  // fieldSizeBits
    putU32(buf, p, 0);
    p += 4;  // additionalDataSize
    putU32(buf, p, static_cast<uint32_t>(husk::db2::FieldCompression::BitpackedSigned));
    p += 4;
    putU32(buf, p, 0);
    p += 4;  // bitpacking_offset_bits
    putU32(buf, p, 0);
    p += 4;  // bitpacking_size_bits
    putU32(buf, p, 0x01);
    p += 4;  // flags: sign-extend
    CHECK(p == sectionFileOffset);

    buf[p] = 0b00001010;  // low nibble 0b1010 = -6 in 4-bit two's complement

    auto file = husk::db2::parse(buf);
    auto values = husk::db2::decodeField(file, file.sections[0], 0, 0);
    REQUIRE(values.size() == 1);
    CHECK(static_cast<int64_t>(values[0]) == -6);
}

// BitpackedIndexed and BitpackedIndexedArray fields both draw their
// additional data from the same physical pallet_data block (DB2.md's
// field_storage_info doc comment: additional_data_offset sums "any previous
// fields which are stored in the same block"), so an array field preceded
// by a BitpackedIndexed field (the real ItemDisplayInfo.db2 shape: ten
// BitpackedIndexed scalar fields before ModelResourcesID's
// BitpackedIndexedArray) must offset past that field's pallet data. A
// wrong offset lands on a real but unrelated value instead of throwing
// (investigations/CHARACTER_PIPELINE_TEST_FINDINGS.md Finding 1).
TEST_CASE("db2::decodeField: a BitpackedIndexedArray field's pallet offset accounts for "
          "a preceding BitpackedIndexed field's pallet_data usage") {
    // field0: BitpackedIndexed, 4-bit index, 3 pallet entries (12 bytes).
    // field1: BitpackedIndexedArray, 4-bit index, arrayCount=1, 2 pallet
    // entries (8 bytes) -- must start at pallet byte offset 12, right after
    // field0's own 12 bytes, not at offset 0.
    size_t recordSize = 1;
    size_t stringTableSize = 1;
    size_t palletDataSize = 20;  // field0: 3*4=12, field1: 2*4=8
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize +
                                2 * kFieldStorageInfoSize + palletDataSize;
    size_t total = sectionFileOffset + recordSize + stringTableSize;
    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, 1);
    p += 4;  // recordCount
    putU32(buf, p, 2);
    p += 4;  // fieldCount
    putU32(buf, p, static_cast<uint32_t>(recordSize));
    p += 4;  // recordSize
    putU32(buf, p, static_cast<uint32_t>(stringTableSize));
    p += 4;  // stringTableSize
    putU32(buf, p, 0);
    p += 4;  // tableHash
    putU32(buf, p, 0);
    p += 4;  // layoutHash
    putU32(buf, p, 1);
    p += 4;  // minId
    putU32(buf, p, 1);
    p += 4;  // maxId
    putU32(buf, p, 0);
    p += 4;  // locale
    putU16(buf, p, 0);
    p += 2;  // flags
    putU16(buf, p, 0);
    p += 2;  // idIndex
    putU32(buf, p, 2);
    p += 4;  // totalFieldCount
    putU32(buf, p, 0);
    p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0);
    p += 4;  // lookupColumnCount
    putU32(buf, p, 2 * kFieldStorageInfoSize);
    p += 4;  // fieldStorageInfoSize
    putU32(buf, p, 0);
    p += 4;  // commonDataSize
    putU32(buf, p, static_cast<uint32_t>(palletDataSize));
    p += 4;  // palletDataSize
    putU32(buf, p, 1);
    p += 4;  // sectionCount
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0);
    p += 8;  // tactKeyHash
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset));
    p += 4;  // fileOffset
    putU32(buf, p, 1);
    p += 4;  // recordCount
    putU32(buf, p, static_cast<uint32_t>(stringTableSize));
    p += 4;  // stringTableSize
    putU32(buf, p, 0);
    p += 4;  // offsetRecordsEnd
    putU32(buf, p, 0);
    p += 4;  // idListSize
    putU32(buf, p, 0);
    p += 4;  // relationshipDataSize
    putU32(buf, p, 0);
    p += 4;  // offsetMapIdCount
    putU32(buf, p, 0);
    p += 4;  // copyTableCount
    CHECK(p == kHeaderSize + kSectionHeaderSize);

    // field_structure[0..1] (unused for these storage types)
    putU16(buf, p, 0);
    p += 2;
    putU16(buf, p, 0);
    p += 2;
    putU16(buf, p, 0);
    p += 2;
    putU16(buf, p, 0);
    p += 2;

    // field_storage_info[0]: BitpackedIndexed, 4-bit index at bit 0
    putU16(buf, p, 0);
    p += 2;  // fieldOffsetBits
    putU16(buf, p, 4);
    p += 2;  // fieldSizeBits
    putU32(buf, p, 12);
    p += 4;  // additionalDataSize
    putU32(buf, p, static_cast<uint32_t>(husk::db2::FieldCompression::BitpackedIndexed));
    p += 4;
    putU32(buf, p, 0);
    p += 4;
    putU32(buf, p, 0);
    p += 4;
    putU32(buf, p, 0);
    p += 4;

    // field_storage_info[1]: BitpackedIndexedArray, 4-bit index at bit 4, arrayCount=1
    putU16(buf, p, 4);
    p += 2;  // fieldOffsetBits
    putU16(buf, p, 4);
    p += 2;  // fieldSizeBits
    putU32(buf, p, 8);
    p += 4;  // additionalDataSize
    putU32(buf, p, static_cast<uint32_t>(husk::db2::FieldCompression::BitpackedIndexedArray));
    p += 4;
    putU32(buf, p, 0);
    p += 4;  // bitpacking_offset_bits
    putU32(buf, p, 0);
    p += 4;  // bitpacking_size_bits
    putU32(buf, p, 1);
    p += 4;  // arrayCount
    CHECK(p == kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize + 2 * kFieldStorageInfoSize);

    // pallet_data: field0's 3 entries (111, 222, 333), then field1's 2
    // entries (444, 555) starting at byte 12.
    putU32(buf, p + 0, 111);
    putU32(buf, p + 4, 222);
    putU32(buf, p + 8, 333);
    putU32(buf, p + 12, 444);
    putU32(buf, p + 16, 555);
    p += static_cast<uint32_t>(palletDataSize);
    CHECK(p == sectionFileOffset);

    // record: field0 index=1 (low nibble), field1 index=1 (high nibble)
    buf[p] = 0x11;

    auto file = husk::db2::parse(buf);
    auto field0 = husk::db2::decodeField(file, file.sections[0], 0, 0);
    REQUIRE(field0.size() == 1);
    CHECK(field0[0] == 222);  // pallet_data[1] within field0's own 3-entry region

    auto field1 = husk::db2::decodeField(file, file.sections[0], 0, 1);
    REQUIRE(field1.size() == 1);
    CHECK(field1[0] == 555);  // pallet_data[12 + 1*4*1] -- field1's own region, not field0's
}

TEST_CASE("db2::resolveFieldString finds a plausible in-bounds C string") {
    std::vector<uint8_t> buf(64, 0);
    const char* text = "hello";
    std::memcpy(buf.data() + 40, text, 5);
    buf[45] = 0;
    // Field stored at position 10, value 30 -> string at 10 + 30 = 40.
    auto result = husk::db2::resolveFieldString(buf, 10, 30);
    REQUIRE(result.has_value());
    CHECK(*result == "hello");
}

TEST_CASE("db2::resolveFieldString rejects an out-of-bounds offset") {
    std::vector<uint8_t> buf(64, 0);
    CHECK_FALSE(husk::db2::resolveFieldString(buf, 10, 1000).has_value());
}

// Section::recordsAvailable()'s real-world case, confirmed against a real
// CASC re-extraction (see db2.hpp's own comment on that method):
// `tact_key_hash != 0` means the section *would* be TACT-key-gated if the key were missing at extraction time, not that husk's own
// bytes are still ciphertext -- a real extraction pipeline (CascLib, given
// the key) already decrypts these before the file lands on disk in the
// overwhelming majority of real cases. One section, tactKeyHash set to a
// real-looking nonzero value, but record bytes filled with real
// (non-zero) data -- husk must read it exactly like an ordinary section,
// not skip it. Includes the encrypted_status block (DB2.md's WDC4+
// section) db2::parse expects immediately before section bodies whenever
// any section has a nonzero tactKeyHash, encryptedIdCount=0 (simplest
// valid case, no encrypted IDs listed).
TEST_CASE("db2::parse reads a TACT-key-gated section normally when its bytes are already decrypted") {
    uint32_t recordCount = 1;
    size_t recordSize = 8;
    size_t stringTableSize = 1;
    size_t encryptedStatusSize = 4;  // encryptedIdCount(u32)=0, no ids
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize +
                                2 * kFieldStorageInfoSize + encryptedStatusSize;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize;

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
    putU16(buf, p, 0); p += 2;  // flags
    putU16(buf, p, 0); p += 2;  // idIndex
    putU32(buf, p, 2); p += 4;  // totalFieldCount
    putU32(buf, p, 0); p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0); p += 4;  // lookupColumnCount
    putU32(buf, p, 2 * kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;  // commonDataSize
    putU32(buf, p, 0); p += 4;  // palletDataSize
    putU32(buf, p, 1); p += 4;  // sectionCount
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0x2555AE20C2538D36ULL); p += 8;  // tactKeyHash -- real-looking, nonzero
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // offsetRecordsEnd
    putU32(buf, p, 0); p += 4;  // idListSize
    putU32(buf, p, 0); p += 4;  // relationshipDataSize
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
    CHECK(p == kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize + 2 * kFieldStorageInfoSize);

    putU32(buf, p, 0); p += 4;  // encrypted_status.encrypted_id_count -- 0 ids
    CHECK(p == sectionFileOffset);

    putU32(buf, p, 42); p += 4;   // field 0: real, non-zero data
    putU32(buf, p, 4200); p += 4;  // field 1
    CHECK(p == total - stringTableSize);

    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    CHECK(section.header.tactKeyHash != 0);
    CHECK(section.recordsAvailable());
    REQUIRE(section.recordBytes.size() == recordSize);
    auto field0 = husk::db2::decodeField(file, section, 0, 0);
    auto field1 = husk::db2::decodeField(file, section, 0, 1);
    REQUIRE(field0.size() == 1);
    REQUIRE(field1.size() == 1);
    CHECK(field0[0] == 42);
    CHECK(field1[0] == 4200);
}

// Same shape, but the section's actual bytes are genuinely all-zero (the
// real signal a real CASC extraction uses to mean "key was unavailable" --
// db2.hpp's recordsAvailable() comment) -- must stay unavailable.
TEST_CASE("db2::Section::recordsAvailable is false for a genuinely all-zero encrypted section") {
    uint32_t recordCount = 1;
    size_t recordSize = 8;
    size_t stringTableSize = 1;
    size_t encryptedStatusSize = 4;
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize +
                                2 * kFieldStorageInfoSize + encryptedStatusSize;
    size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize;

    std::vector<uint8_t> buf(total, 0);  // stays all-zero: record bytes never written
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
    putU16(buf, p, 0); p += 2;
    putU16(buf, p, 0); p += 2;
    putU32(buf, p, 2); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 2 * kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 1); p += 4;
    CHECK(p == kHeaderSize);

    putU64(buf, p, 0x2555AE20C2538D36ULL); p += 8;
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
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
    putU32(buf, p, 0); p += 4;  // encrypted_status.encrypted_id_count
    CHECK(p == sectionFileOffset);
    // record bytes + string block left all-zero, matching the real
    // "key was unavailable at extraction time" signal.

    auto file = husk::db2::parse(buf);
    const husk::db2::Section& section = file.sections[0];
    CHECK(section.header.tactKeyHash != 0);
    CHECK_FALSE(section.recordsAvailable());
}
