// CLI tier: `husk db2-export` -- exercises the real compiled binary (see
// run_husk.hpp) against a small, synthetic, on-disk WDC5 fixture (same
// hand-built-from-DB2.md shape tests/test_db2.cpp's unit tests use, kept
// local here rather than shared since this is a CLI-boundary test, not a
// db2.hpp unit test). Reads the resulting .sqlite file back via the sqlite3
// C API directly -- a real downstream consumer, not a mocked stand-in.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include <sqlite3.h>

#include "run_husk.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

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

constexpr size_t kHeaderSize = 204;
constexpr size_t kSectionHeaderSize = 40;
constexpr size_t kFieldStructureSize = 4;
constexpr size_t kFieldStorageInfoSize = 24;

// Byte-for-byte the same construction as tests/test_db2.cpp's own
// buildSimpleFile (kept as a local copy since this is a CLI-boundary test,
// not a db2.hpp unit test) -- one section, two plain (storageType None)
// uint32 fields per record: field0 = id (1..recordCount), field1 = 100*id.
std::vector<uint8_t> buildSimpleDb2(uint32_t recordCount) {
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
    putU32(buf, p, static_cast<uint32_t>(2 * kFieldStorageInfoSize));
    p += 4;  // fieldStorageInfoSize
    putU32(buf, p, 0);
    p += 4;  // commonDataSize
    putU32(buf, p, 0);
    p += 4;  // palletDataSize
    putU32(buf, p, 1);
    p += 4;  // sectionCount
    REQUIRE(p == kHeaderSize);

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
    REQUIRE(p == kHeaderSize + kSectionHeaderSize);

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
    REQUIRE(p == sectionFileOffset);

    for (uint32_t r = 0; r < recordCount; ++r) {
        putU32(buf, p, r + 1);
        p += 4;  // field 0: id
        putU32(buf, p, (r + 1) * 100);
        p += 4;  // field 1: 100*id
    }
    return buf;
}

}  // namespace

TEST_CASE("husk db2-export: synthetic WDC5 file, no --dbd-dir, writes real SQLite with generic "
          "field_<N> columns and the exact real values") {
    fs::path dir = fs::temp_directory_path() / "husk-test-db2export";
    fs::create_directories(dir);
    fs::path dbPath = dir / "test.db2";
    fs::path outPath = dir / "test.sqlite";
    fs::remove(outPath);

    std::vector<uint8_t> bytes = buildSimpleDb2(5);
    std::ofstream f(dbPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.close();

    auto result = runHusk("db2-export " + dbPath.string() + " " + outPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("wrote 5 row(s)") != std::string::npos);
    CHECK(result.output.find("generic field_<N> column names") != std::string::npos);
    REQUIRE(fs::exists(outPath));

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(outPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT field_0, field_1 FROM test ORDER BY field_0", -1, &stmt,
                                nullptr) == SQLITE_OK);
    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ++rowCount;
        int64_t id = sqlite3_column_int64(stmt, 0);
        int64_t value = sqlite3_column_int64(stmt, 1);
        CHECK(id == rowCount);
        CHECK(value == rowCount * 100);
    }
    CHECK(rowCount == 5);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    fs::remove_all(dir);
}

TEST_CASE("husk db2-export: an unknown --dbd-dir table hash falls back to generic column names, "
          "doesn't fail") {
    fs::path dir = fs::temp_directory_path() / "husk-test-db2export-nodbd";
    fs::create_directories(dir);
    fs::path dbPath = dir / "test.db2";
    fs::path outPath = dir / "test.sqlite";
    fs::path emptyDbdDir = dir / "empty_dbd";
    fs::create_directories(emptyDbdDir);
    // No manifest.json in emptyDbdDir at all -- loadTableForHash must return
    // nullopt cleanly, not throw, and db2-export must still succeed.

    std::vector<uint8_t> bytes = buildSimpleDb2(2);
    std::ofstream f(dbPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.close();

    auto result = runHusk("db2-export " + dbPath.string() + " " + outPath.string() + " --dbd-dir " +
                           emptyDbdDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("generic field_<N> column names") != std::string::npos);

    fs::remove_all(dir);
}
