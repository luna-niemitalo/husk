// CLI tier: `husk db2-export` -- exercises the real compiled binary (see
// run_husk.hpp) against a small, synthetic, on-disk WDC5 fixture (same
// hand-built-from-DB2.md shape tests/test_db2.cpp's unit tests use, kept
// local here rather than shared since this is a CLI-boundary test, not a
// db2.hpp unit test). Reads the resulting .sqlite file back via the sqlite3
// C API directly -- a real downstream consumer, not a mocked stand-in.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include <sqlite3.h>

#include "run_husk.hpp"
#include "test_data_paths.hpp"

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

// A synthetic WDC5 file with `fieldCount` plain (storageType None) uint32
// fields and `rows.size()` records, real tableHash/layoutHash of the
// caller's choosing -- used by the --dir/FK-constraint tests below to build
// two files whose real WoWDBDefs relation husk's own dbd.hpp resolves.
std::vector<uint8_t> buildDb2WithFields(uint32_t tableHash, uint32_t layoutHash,
                                         const std::vector<std::vector<uint32_t>>& rows) {
    uint32_t fieldCount = static_cast<uint32_t>(rows.empty() ? 0 : rows[0].size());
    size_t recordSize = fieldCount * 4;
    size_t stringTableSize = 1;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize + fieldCount * kFieldStorageInfoSize;
    size_t total = sectionFileOffset + rows.size() * recordSize + stringTableSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;  // recordCount
    putU32(buf, p, fieldCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, tableHash); p += 4;
    putU32(buf, p, layoutHash); p += 4;
    putU32(buf, p, 1); p += 4;  // minId
    putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0); p += 2;  // flags
    putU16(buf, p, 0); p += 2;  // idIndex
    putU32(buf, p, fieldCount); p += 4;  // totalFieldCount
    putU32(buf, p, 0); p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0); p += 4;  // lookupColumnCount
    putU32(buf, p, fieldCount * kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;  // commonDataSize
    putU32(buf, p, 0); p += 4;  // palletDataSize
    putU32(buf, p, 1); p += 4;  // sectionCount
    REQUIRE(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;  // tactKeyHash
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;  // fileOffset
    putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // offsetRecordsEnd
    putU32(buf, p, 0); p += 4;  // idListSize
    putU32(buf, p, 0); p += 4;  // relationshipDataSize
    putU32(buf, p, 0); p += 4;  // offsetMapIdCount
    putU32(buf, p, 0); p += 4;  // copyTableCount
    REQUIRE(p == kHeaderSize + kSectionHeaderSize);

    for (uint32_t i = 0; i < fieldCount; ++i) {
        putU16(buf, p, 0); p += 2;
        putU16(buf, p, static_cast<uint16_t>(i * 4)); p += 2;
    }
    for (uint32_t i = 0; i < fieldCount; ++i) {
        putU16(buf, p, static_cast<uint16_t>(i * 32)); p += 2;
        putU16(buf, p, 32); p += 2;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;  // storageType = None
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
    }
    REQUIRE(p == sectionFileOffset);

    for (const std::vector<uint32_t>& row : rows) {
        for (uint32_t v : row) {
            putU32(buf, p, v);
            p += 4;
        }
    }
    return buf;
}

// A synthetic WDC5 file with exactly one inline uint32 field (`ids`) plus a
// real relationship_map giving each record's non-inline relation value by
// position (`foreignIds[i]` for record `i`, header.flags & 0x02 clear --
// same "recordIndexOrId is a position, not a real ID" case
// db2::nonInlineRelationValuesByRecord documents and the real
// ChrModelTextureLayer chain actually uses). Mirrors
// tests/test_db2.cpp's own "decodes a relationship map with no offset map
// involved" fixture, but with a real ID column too so the CLI test can
// build a genuinely joinable table.
std::vector<uint8_t> buildDb2WithNonInlineRelation(uint32_t tableHash, uint32_t layoutHash,
                                                    const std::vector<uint32_t>& ids,
                                                    const std::vector<uint32_t>& foreignIds) {
    REQUIRE(ids.size() == foreignIds.size());
    uint32_t recordCount = static_cast<uint32_t>(ids.size());
    size_t recordSize = 4;
    size_t stringTableSize = 1;
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + kFieldStructureSize + kFieldStorageInfoSize;
    size_t relationshipMapSize = 12 + static_cast<size_t>(recordCount) * 8;
    size_t total =
        sectionFileOffset + recordCount * recordSize + stringTableSize + relationshipMapSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, 1); p += 4;  // fieldCount
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, tableHash); p += 4;
    putU32(buf, p, layoutHash); p += 4;
    putU32(buf, p, 1); p += 4;  // minId
    putU32(buf, p, recordCount); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0); p += 2;  // flags: 0x02 clear -- relationship entries hold positions, not IDs
    putU16(buf, p, 0); p += 2;  // idIndex
    putU32(buf, p, 1); p += 4;  // totalFieldCount
    putU32(buf, p, 0); p += 4;  // bitpackedDataOffset
    putU32(buf, p, 0); p += 4;  // lookupColumnCount
    putU32(buf, p, kFieldStorageInfoSize); p += 4;
    putU32(buf, p, 0); p += 4;  // commonDataSize
    putU32(buf, p, 0); p += 4;  // palletDataSize
    putU32(buf, p, 1); p += 4;  // sectionCount
    REQUIRE(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;  // tactKeyHash
    putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;  // fileOffset
    putU32(buf, p, recordCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // offsetRecordsEnd
    putU32(buf, p, 0); p += 4;  // idListSize
    putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;  // relationshipDataSize
    putU32(buf, p, 0); p += 4;  // offsetMapIdCount
    putU32(buf, p, 0); p += 4;  // copyTableCount
    REQUIRE(p == kHeaderSize + kSectionHeaderSize);

    putU16(buf, p, 0); p += 2;
    putU16(buf, p, 0); p += 2;  // field 0 (ID): position 0

    putU16(buf, p, 0); p += 2;  // fieldOffsetBits
    putU16(buf, p, 32); p += 2;  // fieldSizeBits
    putU32(buf, p, 0); p += 4;  // additionalDataSize
    putU32(buf, p, 0); p += 4;  // storageType = None
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    REQUIRE(p == sectionFileOffset);

    for (uint32_t id : ids) {
        putU32(buf, p, id);
        p += 4;
    }
    p += stringTableSize;  // string block: single zero byte, already zero-initialized

    putU32(buf, p, recordCount); p += 4;  // relationship_map.num_entries
    putU32(buf, p, foreignIds.empty() ? 0 : *std::min_element(foreignIds.begin(), foreignIds.end()));
    p += 4;  // relationship_map.min_id
    putU32(buf, p, foreignIds.empty() ? 0 : *std::max_element(foreignIds.begin(), foreignIds.end()));
    p += 4;  // relationship_map.max_id
    for (uint32_t i = 0; i < recordCount; ++i) {
        putU32(buf, p, foreignIds[i]); p += 4;  // entry.foreign_id
        putU32(buf, p, i); p += 4;              // entry.record_index (position, flags & 0x02 clear)
    }
    REQUIRE(p == total);
    return buf;
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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

// `--dir` exports every .db2 file under a directory into one SQLite
// database, and a column with a real
// WoWDBDefs "<Table::Col>" relation target gets a real FOREIGN KEY
// constraint when that target table is also part of the batch -- verified
// here by actually running the resulting join, not just inspecting the
// schema text.
TEST_CASE("husk db2-export --dir: two related synthetic files export with a real FOREIGN KEY, "
          "joinable end to end") {
    fs::path dir = fs::temp_directory_path() / "husk-test-db2export-dir";
    fs::remove_all(dir);
    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::path definitionsDir = dbdDir / "definitions";
    fs::create_directories(db2Dir);
    fs::create_directories(definitionsDir);
    fs::path outPath = dir / "out.sqlite";

    const uint32_t kParentTableHash = 0x11111111;
    const uint32_t kParentLayoutHash = 0x22222222;
    const uint32_t kChildTableHash = 0x33333333;
    const uint32_t kChildLayoutHash = 0x44444444;

    // Parent: one field, ID (inline, $id$) -- rows ID=10,20,30.
    writeFile(db2Dir / "parent.db2",
              buildDb2WithFields(kParentTableHash, kParentLayoutHash, {{10}, {20}, {30}}));
    // Child: two fields, ID (inline, $id$) and ParentID (relates to Parent::ID).
    writeFile(db2Dir / "child.db2", buildDb2WithFields(kChildTableHash, kChildLayoutHash,
                                                         {{1, 10}, {2, 20}, {3, 30}}));

    std::ofstream manifest(dbdDir / "manifest.json");
    manifest << "[\n"
             << "  {\"tableName\": \"Parent\", \"tableHash\": \"" << std::hex << kParentTableHash
             << "\"},\n"
             << "  {\"tableName\": \"Child\", \"tableHash\": \"" << std::hex << kChildTableHash << "\"}\n"
             << "]\n" << std::dec;
    manifest.close();

    std::ofstream parentDbd(definitionsDir / "Parent.dbd");
    parentDbd << "COLUMNS\n"
              << "int ID\n"
              << "\n"
              << "LAYOUT " << std::hex << kParentLayoutHash << "\n"
              << std::dec << "BUILD 1.0.0.1\n"
              << "$id$ID<32>\n";
    parentDbd.close();

    std::ofstream childDbd(definitionsDir / "Child.dbd");
    childDbd << "COLUMNS\n"
             << "int ID\n"
             << "int<Parent::ID> ParentID\n"
             << "\n"
             << "LAYOUT " << std::hex << kChildLayoutHash << "\n"
             << std::dec << "BUILD 1.0.0.1\n"
             << "$id$ID<32>\n"
             << "ParentID<32>\n";
    childDbd.close();

    auto result = runHusk("db2-export --dir " + db2Dir.string() + " " + outPath.string() + " --dbd-dir " +
                           dbdDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 table(s)") != std::string::npos);
    REQUIRE(fs::exists(outPath));

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(outPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);

    sqlite3_stmt* schemaStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT sql FROM sqlite_master WHERE name = 'Child'", -1, &schemaStmt,
                                nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(schemaStmt) == SQLITE_ROW);
    std::string childSchema(reinterpret_cast<const char*>(sqlite3_column_text(schemaStmt, 0)));
    sqlite3_finalize(schemaStmt);
    CHECK(childSchema.find("FOREIGN KEY (\"ParentID\") REFERENCES \"Parent\"(\"ID\")") !=
          std::string::npos);

    sqlite3_stmt* joinStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(
                db,
                "SELECT c.ID, c.ParentID, p.ID FROM Child c JOIN Parent p ON c.ParentID = p.ID "
                "ORDER BY c.ID",
                -1, &joinStmt, nullptr) == SQLITE_OK);
    int rowCount = 0;
    while (sqlite3_step(joinStmt) == SQLITE_ROW) {
        ++rowCount;
        int64_t childId = sqlite3_column_int64(joinStmt, 0);
        int64_t parentId = sqlite3_column_int64(joinStmt, 1);
        int64_t joinedParentId = sqlite3_column_int64(joinStmt, 2);
        CHECK(parentId == joinedParentId);
        CHECK(parentId == childId * 10);
    }
    CHECK(rowCount == 3);
    sqlite3_finalize(joinStmt);
    sqlite3_close(db);

    fs::remove_all(dir);
}

// The gap this test closes: a `$noninline,relation$` DBD field (e.g. real
// ChrModelTextureLayer's CharComponentTextureLayoutsID under some layouts)
// occupies no WDC5 field-array slot at all -- its value lives only in the
// section's own relationship_map (db2::nonInlineRelationValuesByRecord).
// Before this fix, db2-export only ever iterated file.fieldStorageInfo, so
// a column like this got no SQLite column and no FK at all, silently.
// Child here has exactly one inline field (ID) and a fully non-inline
// ParentID -- proves the column, its real per-record value (via the
// relationship_map, not a field slot), and its FOREIGN KEY all come
// through, verified with a real JOIN, not just schema inspection.
TEST_CASE("husk db2-export --dir: a $noninline,relation$ field gets a real column, value, and "
          "FOREIGN KEY, joinable end to end") {
    fs::path dir = fs::temp_directory_path() / "husk-test-db2export-noninline-relation";
    fs::remove_all(dir);
    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::path definitionsDir = dbdDir / "definitions";
    fs::create_directories(db2Dir);
    fs::create_directories(definitionsDir);
    fs::path outPath = dir / "out.sqlite";

    const uint32_t kParentTableHash = 0x55555555;
    const uint32_t kParentLayoutHash = 0x66666666;
    const uint32_t kChildTableHash = 0x77777777;
    const uint32_t kChildLayoutHash = 0x88888888;

    // Parent: one inline field, ID -- rows ID=10,20,30.
    writeFile(db2Dir / "parent.db2",
              buildDb2WithFields(kParentTableHash, kParentLayoutHash, {{10}, {20}, {30}}));
    // Child: one inline field (ID=1,2,3), and ParentID stored ONLY in the
    // relationship_map (record i -> foreign_id, position-based since
    // flags & 0x02 is clear) -- child record 0 (ID=1) relates to parent 10,
    // record 1 (ID=2) to parent 20, record 2 (ID=3) to parent 30.
    writeFile(db2Dir / "child.db2",
              buildDb2WithNonInlineRelation(kChildTableHash, kChildLayoutHash, {1, 2, 3}, {10, 20, 30}));

    std::ofstream manifest(dbdDir / "manifest.json");
    manifest << "[\n"
             << "  {\"tableName\": \"Parent\", \"tableHash\": \"" << std::hex << kParentTableHash
             << "\"},\n"
             << "  {\"tableName\": \"Child\", \"tableHash\": \"" << std::hex << kChildTableHash << "\"}\n"
             << "]\n" << std::dec;
    manifest.close();

    std::ofstream parentDbd(definitionsDir / "Parent.dbd");
    parentDbd << "COLUMNS\n"
              << "int ID\n"
              << "\n"
              << "LAYOUT " << std::hex << kParentLayoutHash << "\n"
              << std::dec << "BUILD 1.0.0.1\n"
              << "$id$ID<32>\n";
    parentDbd.close();

    // ParentID is declared in COLUMNS (for its real relation target) but
    // annotated $noninline,relation$ in the LAYOUT block -- it occupies no
    // field-array slot in the .db2 file itself, matching real
    // ChrModelTextureLayer.dbd's own CharComponentTextureLayoutsID shape
    // under the layout this session verified against real data.
    std::ofstream childDbd(definitionsDir / "Child.dbd");
    childDbd << "COLUMNS\n"
             << "int ID\n"
             << "int<Parent::ID> ParentID\n"
             << "\n"
             << "LAYOUT " << std::hex << kChildLayoutHash << "\n"
             << std::dec << "BUILD 1.0.0.1\n"
             << "$id$ID<32>\n"
             << "$noninline,relation$ParentID<32>\n";
    childDbd.close();

    auto result = runHusk("db2-export --dir " + db2Dir.string() + " " + outPath.string() + " --dbd-dir " +
                           dbdDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 table(s)") != std::string::npos);
    REQUIRE(fs::exists(outPath));

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(outPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);

    sqlite3_stmt* schemaStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT sql FROM sqlite_master WHERE name = 'Child'", -1, &schemaStmt,
                                nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(schemaStmt) == SQLITE_ROW);
    std::string childSchema(reinterpret_cast<const char*>(sqlite3_column_text(schemaStmt, 0)));
    sqlite3_finalize(schemaStmt);
    CHECK(childSchema.find("\"ParentID\" INTEGER") != std::string::npos);
    CHECK(childSchema.find("FOREIGN KEY (\"ParentID\") REFERENCES \"Parent\"(\"ID\")") !=
          std::string::npos);

    sqlite3_stmt* joinStmt2 = nullptr;
    REQUIRE(sqlite3_prepare_v2(
                db,
                "SELECT c.ID, c.ParentID, p.ID FROM Child c JOIN Parent p ON c.ParentID = p.ID "
                "ORDER BY c.ID",
                -1, &joinStmt2, nullptr) == SQLITE_OK);
    int rowCount2 = 0;
    while (sqlite3_step(joinStmt2) == SQLITE_ROW) {
        ++rowCount2;
        int64_t childId = sqlite3_column_int64(joinStmt2, 0);
        int64_t parentId = sqlite3_column_int64(joinStmt2, 1);
        int64_t joinedParentId = sqlite3_column_int64(joinStmt2, 2);
        CHECK(parentId == joinedParentId);
        CHECK(parentId == childId * 10);
    }
    CHECK(rowCount2 == 3);
    sqlite3_finalize(joinStmt2);
    sqlite3_close(db);

    fs::remove_all(dir);
}

// Real-data-gated: the exact real chain
// TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 1 named as unfinished --
// ChrModelTextureLayer's own CharComponentTextureLayoutsID is a real
// $noninline,relation$ field under its real layout, verified against real
// local files rather than only the synthetic fixture above. Skips cleanly
// (not a failure) when either the real WoWDBDefs checkout or the real local
// DB2 export isn't present -- same "local, optional, read-only reference
// data" tier every other real-data test in this repo already uses.
TEST_CASE("husk db2-export --dir: real chrmodeltexturelayer.db2 -> charcomponenttexturelayouts.db2 "
          "non-inline relation joins correctly" *
          doctest::skip(husk::test::testDbdDir().empty() ||
                         !fs::exists("/media/luna/data/wow_export/dbfilesclient/chrmodeltexturelayer.db2") ||
                         !fs::exists(
                             "/media/luna/data/wow_export/dbfilesclient/charcomponenttexturelayouts.db2"))) {
    fs::path dir = fs::temp_directory_path() / "husk-test-db2export-real-noninline-relation";
    fs::remove_all(dir);
    fs::path db2Dir = dir / "db2";
    fs::create_directories(db2Dir);
    fs::path outPath = dir / "out.sqlite";

    fs::copy_file("/media/luna/data/wow_export/dbfilesclient/chrmodeltexturelayer.db2",
                   db2Dir / "chrmodeltexturelayer.db2");
    fs::copy_file("/media/luna/data/wow_export/dbfilesclient/charcomponenttexturelayouts.db2",
                   db2Dir / "charcomponenttexturelayouts.db2");

    auto result = runHusk("db2-export --dir " + db2Dir.string() + " " + outPath.string() + " --dbd-dir " +
                           husk::test::testDbdDir());
    CHECK(result.exitCode == 0);
    REQUIRE(fs::exists(outPath));

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(outPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);

    sqlite3_stmt* schemaStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT sql FROM sqlite_master WHERE name = 'ChrModelTextureLayer'", -1,
                                &schemaStmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(schemaStmt) == SQLITE_ROW);
    std::string schema(reinterpret_cast<const char*>(sqlite3_column_text(schemaStmt, 0)));
    sqlite3_finalize(schemaStmt);
    CHECK(schema.find("\"CharComponentTextureLayoutsID\" INTEGER") != std::string::npos);
    CHECK(schema.find("FOREIGN KEY (\"CharComponentTextureLayoutsID\") REFERENCES "
                       "\"CharComponentTextureLayouts\"(\"ID\")") != std::string::npos);

    // Every real row resolves a value (the relation is real for 100% of
    // this table's records, not a sometimes-present field) -- and at least
    // one real join succeeds against whatever layouts are present in this
    // local, necessarily-partial export.
    sqlite3_stmt* countStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db,
                                "SELECT COUNT(*), COUNT(CharComponentTextureLayoutsID) FROM "
                                "ChrModelTextureLayer",
                                -1, &countStmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(countStmt) == SQLITE_ROW);
    int64_t total = sqlite3_column_int64(countStmt, 0);
    int64_t resolved = sqlite3_column_int64(countStmt, 1);
    sqlite3_finalize(countStmt);
    CHECK(total > 0);
    CHECK(resolved == total);

    sqlite3_stmt* realJoinStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(
                db,
                "SELECT COUNT(*) FROM ChrModelTextureLayer t JOIN CharComponentTextureLayouts l "
                "ON t.CharComponentTextureLayoutsID = l.ID",
                -1, &realJoinStmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(realJoinStmt) == SQLITE_ROW);
    int64_t joined = sqlite3_column_int64(realJoinStmt, 0);
    sqlite3_finalize(realJoinStmt);
    CHECK(joined > 0);
    sqlite3_close(db);

    fs::remove_all(dir);
}

// Real-data-gated: offset-map ("sparse") per-field decode, added this
// session -- previously these sections were skipped entirely by db2-export
// (LoadedFile::skippedOffsetMap). scenescripttext.db2 is the real local
// fixture that found two real bugs before this passed (a false positive
// from a small integer field mid-decode, and a false positive from a
// high-byte value chaining across a field boundary -- see db2.cpp's
// tryReadInlineString and decodeOffsetMapRecord for the fixes). Skips
// cleanly, not a failure, when the real local file isn't present.
TEST_CASE(
    "husk db2-export: real scenescripttext.db2 offset-map section exports real Name/Script text" *
    doctest::skip(!fs::exists("/media/luna/data/wow_export/dbfilesclient/scenescripttext.db2"))) {
    fs::path dir = fs::temp_directory_path() / "husk-test-db2export-real-offsetmap";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::path outPath = dir / "out.sqlite";

    auto result = runHusk("db2-export /media/luna/data/wow_export/dbfilesclient/scenescripttext.db2 " +
                           outPath.string());
    CHECK(result.exitCode == 0);
    REQUIRE(fs::exists(outPath));
    // No more "skipped ... offset-map/sparse section(s)" -- every real
    // section here is offset-map-shaped, and all now export.
    CHECK(result.output.find("offset-map") == std::string::npos);

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(outPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);

    sqlite3_stmt* countStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM scenescripttext", -1, &countStmt, nullptr) ==
            SQLITE_OK);
    REQUIRE(sqlite3_step(countStmt) == SQLITE_ROW);
    int64_t total = sqlite3_column_int64(countStmt, 0);
    sqlite3_finalize(countStmt);
    CHECK(total > 30000);  // real file has 36498 records in its one decodable section

    // A real, specific row: a scene named "Global Functions - Scene" whose
    // Script field is real Lua source text -- exact string, not a
    // substring/LIKE guess, so a decode that silently truncated or
    // misaligned this field would fail the check.
    sqlite3_stmt* rowStmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(
                db, "SELECT field_1 FROM scenescripttext WHERE field_0 = 'Global Functions - Scene'", -1,
                &rowStmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(rowStmt) == SQLITE_ROW);
    std::string script(reinterpret_cast<const char*>(sqlite3_column_text(rowStmt, 0)));
    sqlite3_finalize(rowStmt);
    CHECK(script.find("function Scene:WaitTimer(waitTime)") != std::string::npos);
    sqlite3_close(db);

    fs::remove_all(dir);
}
