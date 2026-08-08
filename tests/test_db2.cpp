// Synthetic-buffer tests for the WDC5 proof-of-concept parser (src/db2.hpp/
// .cpp). Byte layout constructed by hand from documentation/wowdev-wiki/md/
// DB2.md's WDC5 structure -- cross-checked once against a real file
// (chrmodelmaterial.db2, see db2.hpp's module comment) but these fixtures
// are independently built, not copied from that file, so they exercise the
// parser's own offset arithmetic rather than just replaying known-good
// bytes.

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
