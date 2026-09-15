// Tests for src/db2table.hpp/.cpp (generic named-column reader) and
// src/chrmodel_db2.hpp/.cpp (real typed structs built on top of it).
// Synthetic WDC5 bytes + a synthetic WoWDBDefs checkout on disk -- same
// discipline as tests/test_db2.cpp/test_dbd.cpp's own hand-built fixtures,
// kept local rather than shared since each of this project's CLI/unit test
// files builds its own minimal repro.

#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

#include "../src/chrcustomization_db2.hpp"
#include "../src/chrmodel_db2.hpp"
#include "../src/db2table.hpp"
#include "test_data_paths.hpp"

namespace fs = std::filesystem;

namespace {

void putU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) { std::memcpy(buf.data() + offset, &v, 2); }
void putU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) { std::memcpy(buf.data() + offset, &v, 4); }
void putU64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) { std::memcpy(buf.data() + offset, &v, 8); }

constexpr size_t kHeaderSize = 204;
constexpr size_t kSectionHeaderSize = 40;
constexpr size_t kFieldStructureSize = 4;
constexpr size_t kFieldStorageInfoSize = 24;

// One field ("Width"), header.flags = 0x04 (non-inline ID), a non-inline
// relation column ("OtherID") resolved via the relationship map (index-
// based, header.flags & 0x02 clear) -- exercises all three of
// db2table::readNamedColumns' resolution paths in one fixture, the same
// real shape chrmodeltexturelayer.db2 itself has (see CLAUDE_HISTORY.md).
std::vector<uint8_t> buildTestFile(uint32_t tableHash, uint32_t layoutHash) {
    uint32_t recordCount = 2;
    size_t recordSize = 4;
    size_t stringTableSize = 1;
    size_t idListSize = recordCount * 4;
    size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + kFieldStructureSize + kFieldStorageInfoSize;
    size_t relationshipMapSize = 12 + 2 * 8;
    size_t total =
        sectionFileOffset + recordCount * recordSize + stringTableSize + idListSize + relationshipMapSize;

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
    putU32(buf, p, 500); p += 4;  // minId
    putU32(buf, p, 501); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0x04); p += 2;  // flags: has non-inline IDs
    putU16(buf, p, 0); p += 2;  // idIndex (ignored, flags & 0x04 set)
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
    putU32(buf, p, static_cast<uint32_t>(idListSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;
    putU32(buf, p, 0); p += 4;  // offsetMapIdCount
    putU32(buf, p, 0); p += 4;  // copyTableCount
    REQUIRE(p == kHeaderSize + kSectionHeaderSize);

    putU16(buf, p, 0); p += 2;  // field_structure.size
    putU16(buf, p, 0); p += 2;  // field_structure.position
    putU16(buf, p, 0); p += 2;  // fieldOffsetBits
    putU16(buf, p, 32); p += 2;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;  // storageType = None
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    REQUIRE(p == sectionFileOffset);

    putU32(buf, p, 111); p += 4;  // record 0: Width
    putU32(buf, p, 222); p += 4;  // record 1: Width
    p += stringTableSize;

    putU32(buf, p, 500); p += 4;  // idList[0]
    putU32(buf, p, 501); p += 4;  // idList[1]

    putU32(buf, p, 2); p += 4;  // relationship_map.num_entries
    putU32(buf, p, 900); p += 4;  // min_id
    putU32(buf, p, 901); p += 4;  // max_id
    putU32(buf, p, 900); p += 4; putU32(buf, p, 0); p += 4;  // record 0 -> OtherID 900
    putU32(buf, p, 901); p += 4; putU32(buf, p, 1); p += 4;  // record 1 -> OtherID 901
    REQUIRE(p == total);

    return buf;
}

// Two inline fields only, no id/relation column at all: "Scalar" (a plain
// 32-bit scalar, field_size_bits 32 -- arrayLength 1) and "PairField" (a
// genuine 2-element 32-bit array, field_size_bits 64), the same real
// "None"-storage array shape ChrCustomizationChoice.SwatchColor<32>[2] has.
// Exercises readNamedArrayColumns' inline-field path without dragging in
// the id/relation machinery buildTestFile above already covers.
std::vector<uint8_t> buildArrayTestFile(uint32_t tableHash, uint32_t layoutHash,
                                         const std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>>& rows) {
    uint32_t fieldCount = 2;
    size_t recordSize = 4 + 8;  // Scalar (4 bytes) + PairField (2x4 bytes)
    size_t stringTableSize = 1;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize + fieldCount * kFieldStorageInfoSize;
    size_t total = sectionFileOffset + rows.size() * recordSize + stringTableSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32(buf, 4, 5);

    size_t p = 8 + 128;
    putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
    putU32(buf, p, fieldCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, tableHash); p += 4;
    putU32(buf, p, layoutHash); p += 4;
    putU32(buf, p, 1); p += 4;  // minId
    putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;  // maxId
    putU32(buf, p, 0); p += 4;  // locale
    putU16(buf, p, 0); p += 2;  // flags: 0 -- no non-inline id in this fixture
    putU16(buf, p, 0); p += 2;  // idIndex: unused, not requested by these tests
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

    // field_structure: Scalar at byte 0, PairField at byte 4.
    putU16(buf, p, 0); p += 2; putU16(buf, p, 0); p += 2;
    putU16(buf, p, 0); p += 2; putU16(buf, p, 4); p += 2;

    // field_storage_info: Scalar -- offset bit 0, size 32 bits (arrayLength 1).
    putU16(buf, p, 0); p += 2; putU16(buf, p, 32); p += 2;
    putU32(buf, p, 0); p += 4;  // additionalDataSize
    putU32(buf, p, 0); p += 4;  // storageType = None
    putU32(buf, p, 0); p += 4;  // defaultValue
    putU32(buf, p, 0); p += 4;  // arrayCount (unused for None)
    putU32(buf, p, 0); p += 4;  // signExtend
    // PairField -- offset bit 32, size 64 bits (2 elements x 32 bits).
    putU16(buf, p, 32); p += 2; putU16(buf, p, 64); p += 2;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;  // storageType = None
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    putU32(buf, p, 0); p += 4;
    REQUIRE(p == sectionFileOffset);

    for (const auto& [scalar, pair] : rows) {
        putU32(buf, p, scalar); p += 4;
        putU32(buf, p, pair.first); p += 4;
        putU32(buf, p, pair.second); p += 4;
    }
    p += stringTableSize;
    REQUIRE(p == total);
    return buf;
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

struct TestDbdDir {
    fs::path dir;
    explicit TestDbdDir(const std::string& name) : dir(fs::temp_directory_path() / name) {
        fs::remove_all(dir);
        fs::create_directories(dir / "definitions");
    }
    ~TestDbdDir() { fs::remove_all(dir); }

    void writeManifest(const std::vector<std::pair<std::string, uint32_t>>& tables) {
        std::ofstream m(dir / "manifest.json");
        m << "[\n";
        for (size_t i = 0; i < tables.size(); ++i) {
            m << "  {\"tableName\": \"" << tables[i].first << "\", \"tableHash\": \"" << std::hex
              << tables[i].second << "\"}" << (i + 1 < tables.size() ? "," : "") << "\n" << std::dec;
        }
        m << "]\n";
    }

    void writeDbd(const std::string& tableName, const std::string& text) {
        std::ofstream f(dir / "definitions" / (tableName + ".dbd"));
        f << text;
    }
};

}  // namespace

TEST_CASE("db2table::readNamedColumns resolves inline, non-inline-id, and non-inline-relation columns together") {
    const uint32_t kTableHash = 0xABCDEF01;
    const uint32_t kLayoutHash = 0x12345678;

    TestDbdDir dbd("husk-test-db2table-dbd");
    dbd.writeManifest({{"Test", kTableHash}});
    dbd.writeDbd("Test",
                 "COLUMNS\n"
                 "int ID\n"
                 "int<Other::ID> OtherID\n"
                 "int Width\n"
                 "\n"
                 "LAYOUT 12345678\n"
                 "BUILD 1.0.0.1\n"
                 "$noninline,id$ID<32>\n"
                 "$noninline,relation$OtherID<32>\n"
                 "Width<32>\n");

    fs::path db2Path = dbd.dir / "test.db2";
    writeFile(db2Path, buildTestFile(kTableHash, kLayoutHash));

    std::ostringstream err;
    auto rows = husk::db2table::readNamedColumns(db2Path.string(), dbd.dir.string(),
                                                  {"ID", "OtherID", "Width"}, err);
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 2);

    REQUIRE((*rows)[0][0].has_value());
    CHECK(*(*rows)[0][0] == 500);
    REQUIRE((*rows)[0][1].has_value());
    CHECK(*(*rows)[0][1] == 900);
    REQUIRE((*rows)[0][2].has_value());
    CHECK(*(*rows)[0][2] == 111);

    REQUIRE((*rows)[1][0].has_value());
    CHECK(*(*rows)[1][0] == 501);
    REQUIRE((*rows)[1][1].has_value());
    CHECK(*(*rows)[1][1] == 901);
    REQUIRE((*rows)[1][2].has_value());
    CHECK(*(*rows)[1][2] == 222);
}

TEST_CASE("db2table::readNamedColumns returns nullopt without --dbd-dir") {
    std::ostringstream err;
    auto rows = husk::db2table::readNamedColumns("/nonexistent.db2", "", {"ID"}, err);
    CHECK_FALSE(rows.has_value());
}

TEST_CASE("db2table::readNamedColumns reports an unresolved column, still returns rows for the resolved ones") {
    const uint32_t kTableHash = 0x22334455;
    const uint32_t kLayoutHash = 0x66778899;

    TestDbdDir dbd("husk-test-db2table-partial-dbd");
    dbd.writeManifest({{"Test", kTableHash}});
    dbd.writeDbd("Test",
                 "COLUMNS\n"
                 "int ID\n"
                 "int<Other::ID> OtherID\n"
                 "int Width\n"
                 "\n"
                 "LAYOUT 66778899\n"
                 "BUILD 1.0.0.1\n"
                 "$noninline,id$ID<32>\n"
                 "$noninline,relation$OtherID<32>\n"
                 "Width<32>\n");

    fs::path db2Path = dbd.dir / "test.db2";
    writeFile(db2Path, buildTestFile(kTableHash, kLayoutHash));

    std::ostringstream err;
    auto rows = husk::db2table::readNamedColumns(db2Path.string(), dbd.dir.string(),
                                                  {"Width", "NoSuchColumn"}, err);
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 2);
    REQUIRE((*rows)[0][0].has_value());
    CHECK(*(*rows)[0][0] == 111);
    CHECK_FALSE((*rows)[0][1].has_value());
    CHECK(err.str().find("NoSuchColumn") != std::string::npos);
}

TEST_CASE("db2table::readNamedArrayColumns reads a real 2-element array field, "
          "and still handles a scalar (arrayLength 1) field correctly") {
    const uint32_t kTableHash = 0xAABBCCDD;
    const uint32_t kLayoutHash = 0x87654321;

    TestDbdDir dbd("husk-test-db2table-array-dbd");
    dbd.writeManifest({{"Test", kTableHash}});
    dbd.writeDbd("Test",
                 "COLUMNS\n"
                 "int Scalar\n"
                 "int PairField\n"
                 "\n"
                 "LAYOUT 87654321\n"
                 "BUILD 1.0.0.1\n"
                 "Scalar<32>\n"
                 "PairField<32>[2]\n");

    fs::path db2Path = dbd.dir / "test.db2";
    writeFile(db2Path, buildArrayTestFile(kTableHash, kLayoutHash, {{42, {100, 200}}, {7, {0, 0}}}));

    std::ostringstream err;
    auto rows = husk::db2table::readNamedArrayColumns(db2Path.string(), dbd.dir.string(),
                                                       {"Scalar", "PairField"}, err);
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 2);

    // Row 0: Scalar comes back as a genuine 1-element vector (not unwrapped
    // to a bare scalar) even though its own declared arrayLength is 1 --
    // the exact "still works correctly" case this function's own doc
    // comment promises.
    REQUIRE((*rows)[0][0].has_value());
    REQUIRE((*rows)[0][0]->size() == 1);
    CHECK((*(*rows)[0][0])[0] == 42);

    REQUIRE((*rows)[0][1].has_value());
    REQUIRE((*rows)[0][1]->size() == 2);
    CHECK((*(*rows)[0][1])[0] == 100);
    CHECK((*(*rows)[0][1])[1] == 200);

    REQUIRE((*rows)[1][0].has_value());
    CHECK((*(*rows)[1][0])[0] == 7);
    REQUIRE((*rows)[1][1].has_value());
    CHECK((*(*rows)[1][1])[0] == 0);
    CHECK((*(*rows)[1][1])[1] == 0);
}

TEST_CASE("db2table::readNamedArrayColumns reports an unresolved column as nullopt for every row, "
          "not a crash") {
    const uint32_t kTableHash = 0x11223344;
    const uint32_t kLayoutHash = 0x99887766;

    TestDbdDir dbd("husk-test-db2table-array-unresolved-dbd");
    dbd.writeManifest({{"Test", kTableHash}});
    dbd.writeDbd("Test",
                 "COLUMNS\nint Scalar\nint PairField\n\n"
                 "LAYOUT 99887766\nBUILD 1.0.0.1\nScalar<32>\nPairField<32>[2]\n");

    fs::path db2Path = dbd.dir / "test.db2";
    writeFile(db2Path, buildArrayTestFile(kTableHash, kLayoutHash, {{1, {2, 3}}}));

    std::ostringstream err;
    auto rows = husk::db2table::readNamedArrayColumns(db2Path.string(), dbd.dir.string(),
                                                       {"PairField", "NoSuchColumn"}, err);
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 1);
    REQUIRE((*rows)[0][0].has_value());
    CHECK((*(*rows)[0][0])[0] == 2);
    CHECK_FALSE((*rows)[0][1].has_value());
    CHECK(err.str().find("NoSuchColumn") != std::string::npos);
}

TEST_CASE("db2table::readNamedArrayColumns returns nullopt when the file itself can't be read") {
    std::ostringstream err;
    auto rows = husk::db2table::readNamedArrayColumns("/nonexistent.db2", "/nonexistent-dbd", {"Whatever"}, err);
    CHECK_FALSE(rows.has_value());
}

TEST_CASE("db2table::readNamedArrayColumns returns nullopt without --dbd-dir") {
    std::ostringstream err;
    auto rows = husk::db2table::readNamedArrayColumns("/nonexistent.db2", "", {"Whatever"}, err);
    CHECK_FALSE(rows.has_value());
}

TEST_CASE("chrcustomization::Choice::swatchColor resolves real, plausible values against real local "
          "ChrCustomizationChoice.db2 data (test_data/db2, reference/WoWDBDefs)" *
          doctest::skip(husk::test::testDbdDir().empty())) {
    // Cross-checked independently before trusting this test: `husk
    // db2-export test_data/db2/chrcustomizationchoice.db2 <out>.sqlite
    // --dbd-dir reference/WoWDBDefs` (real CLI, real files) already
    // resolves a real "SwatchColor<32>[2]" array column (db2-export's own
    // array support, a separate code path from db2table's) into
    // SwatchColor_0/SwatchColor_1 -- real row ID 1 reads SwatchColor_0 =
    // 4293312670, SwatchColor_1 = 0 there. This test confirms
    // chrcustomization::Choice::swatchColor (built on THIS session's new
    // db2table::readNamedArrayColumns, a genuinely different code path)
    // agrees with that independently-obtained real value, not just that
    // it returns *something*.
    std::ostringstream err;
    auto data = husk::chrcustomization::load("test_data/db2", husk::test::testDbdDir(), err);
    REQUIRE(data.has_value());
    REQUIRE_FALSE(data->choices.empty());

    const husk::chrcustomization::Choice* choice1 = nullptr;
    for (const auto& c : data->choices) {
        if (c.id == 1) {
            choice1 = &c;
            break;
        }
    }
    REQUIRE(choice1 != nullptr);
    REQUIRE(choice1->swatchColor.has_value());
    CHECK((*choice1->swatchColor)[0] == 4293312670u);
    CHECK((*choice1->swatchColor)[1] == 0u);

    // Real choice 1 has no real display name (this struct's own doc
    // comment: "the client draws those from SwatchColor instead") --
    // confirms swatchColor is genuinely populated for exactly the kind of
    // row that needs it, not just present everywhere by construction.
    CHECK(choice1->name.empty());
}

TEST_CASE("chrmodel::load builds real typed structs from four synthetic tables, joined by layout ID") {
    const uint32_t kLayoutsHash = 0x10000001, kLayoutsLayoutHash = 0x10000002;
    const uint32_t kMaterialHash = 0x20000001, kMaterialLayoutHash = 0x20000002;
    const uint32_t kSectionHash = 0x30000001, kSectionLayoutHash = 0x30000002;
    const uint32_t kLayerHash = 0x40000001, kLayerLayoutHash = 0x40000002;

    TestDbdDir dbd("husk-test-chrmodel-dbd");
    dbd.writeManifest({{"CharComponentTextureLayouts", kLayoutsHash},
                        {"ChrModelMaterial", kMaterialHash},
                        {"CharComponentTextureSections", kSectionHash},
                        {"ChrModelTextureLayer", kLayerHash}});

    // CharComponentTextureLayouts: ID (noninline id), Width, Height.
    dbd.writeDbd("CharComponentTextureLayouts",
                 "COLUMNS\nint ID\nint Width\nint Height\n\n"
                 "LAYOUT 10000002\nBUILD 1.0.0.1\n$noninline,id$ID<32>\nWidth<32>\nHeight<32>\n");
    // ChrModelMaterial: ID (inline id), CharComponentTextureLayoutsID (inline relation), TextureType, Width, Height, Flags.
    dbd.writeDbd("ChrModelMaterial",
                 "COLUMNS\nint ID\nint<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID\n"
                 "int TextureType\nint Width\nint Height\nint Flags\n\n"
                 "LAYOUT 20000002\nBUILD 1.0.0.1\n$id$ID<32>\n"
                 "CharComponentTextureLayoutsID<32>\nTextureType<32>\nWidth<32>\nHeight<32>\nFlags<32>\n");
    // CharComponentTextureSections: ID (inline id -- buildFlatRecords below
    // has no idList support, so this fixture uses an inline id, unlike the
    // real modern layout's $noninline,id$; the non-inline-id path is
    // already covered by test_db2.cpp/test_dbd.cpp's own unit tests),
    // CharComponentTextureLayoutID (inline relation), SectionType, X, Y,
    // Width, Height, OverlapSectionMask -- 8 inline fields total.
    dbd.writeDbd("CharComponentTextureSections",
                 "COLUMNS\nint ID\nint<CharComponentTextureLayouts::ID> CharComponentTextureLayoutID\n"
                 "int SectionType\nint X\nint Y\nint Width\nint Height\nint OverlapSectionMask\n\n"
                 "LAYOUT 30000002\nBUILD 1.0.0.1\n$id$ID<32>\n"
                 "CharComponentTextureLayoutID<32>\nSectionType<32>\nX<32>\nY<32>\nWidth<32>\nHeight<32>\n"
                 "OverlapSectionMask<32>\n");
    // ChrModelTextureLayer: ID (inline id), TextureType, Layer, Flags, BlendMode, TextureSectionTypeBitMask, CharComponentTextureLayoutsID (noninline relation).
    dbd.writeDbd("ChrModelTextureLayer",
                 "COLUMNS\nint ID\nint TextureType\nint Layer\nint Flags\nint BlendMode\n"
                 "int TextureSectionTypeBitMask\nint<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID\n\n"
                 "LAYOUT 40000002\nBUILD 1.0.0.1\n$id$ID<32>\nTextureType<32>\nLayer<32>\nFlags<32>\n"
                 "BlendMode<32>\nTextureSectionTypeBitMask<32>\n$noninline,relation$CharComponentTextureLayoutsID<32>\n");

    // Build each of the four files directly via a small local helper, since
    // each has a different real field/column shape.
    auto buildLayouts = [&]() {
        uint32_t recordCount = 1;
        size_t recordSize = 8;  // Width, Height
        size_t stringTableSize = 1;
        size_t idListSize = recordCount * 4;
        size_t sectionFileOffset =
            kHeaderSize + kSectionHeaderSize + 2 * kFieldStructureSize + 2 * kFieldStorageInfoSize;
        size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize + idListSize;
        std::vector<uint8_t> buf(total, 0);
        std::memcpy(buf.data(), "WDC5", 4);
        putU32(buf, 4, 5);
        size_t p = 8 + 128;
        putU32(buf, p, recordCount); p += 4;
        putU32(buf, p, 2); p += 4;
        putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
        putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
        putU32(buf, p, kLayoutsHash); p += 4;
        putU32(buf, p, kLayoutsLayoutHash); p += 4;
        putU32(buf, p, 7); p += 4;
        putU32(buf, p, 7); p += 4;
        putU32(buf, p, 0); p += 4;
        putU16(buf, p, 0x04); p += 2;  // flags: non-inline id
        putU16(buf, p, 0); p += 2;
        putU32(buf, p, 2); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 2 * kFieldStorageInfoSize); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 1); p += 4;
        REQUIRE(p == kHeaderSize);
        putU64(buf, p, 0); p += 8;
        putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
        putU32(buf, p, recordCount); p += 4;
        putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, static_cast<uint32_t>(idListSize)); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        REQUIRE(p == kHeaderSize + kSectionHeaderSize);
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
        REQUIRE(p == sectionFileOffset);
        putU32(buf, p, 1024); p += 4;  // Width
        putU32(buf, p, 512); p += 4;   // Height
        p += stringTableSize;
        putU32(buf, p, 7); p += 4;  // idList[0] = 7
        REQUIRE(p == total);
        return buf;
    };

    auto buildFlatRecords = [&](uint32_t tableHash, uint32_t layoutHash, uint32_t fieldCount,
                                 const std::vector<std::vector<uint32_t>>& rows) {
        size_t recordSize = fieldCount * 4;
        size_t stringTableSize = 1;
        size_t sectionFileOffset =
            kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize + fieldCount * kFieldStorageInfoSize;
        size_t total = sectionFileOffset + rows.size() * recordSize + stringTableSize;
        std::vector<uint8_t> buf(total, 0);
        std::memcpy(buf.data(), "WDC5", 4);
        putU32(buf, 4, 5);
        size_t p = 8 + 128;
        putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
        putU32(buf, p, fieldCount); p += 4;
        putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
        putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
        putU32(buf, p, tableHash); p += 4;
        putU32(buf, p, layoutHash); p += 4;
        putU32(buf, p, 1); p += 4;
        putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
        putU32(buf, p, 0); p += 4;
        putU16(buf, p, 0); p += 2;  // flags: 0 -- id is inline
        putU16(buf, p, 0); p += 2;  // idIndex: field 0 is the inline ID
        putU32(buf, p, fieldCount); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, fieldCount * kFieldStorageInfoSize); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 1); p += 4;
        REQUIRE(p == kHeaderSize);
        putU64(buf, p, 0); p += 8;
        putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
        putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
        putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        REQUIRE(p == kHeaderSize + kSectionHeaderSize);
        for (uint32_t i = 0; i < fieldCount; ++i) { putU16(buf, p, 0); p += 2; putU16(buf, p, static_cast<uint16_t>(i * 4)); p += 2; }
        for (uint32_t i = 0; i < fieldCount; ++i) {
            putU16(buf, p, static_cast<uint16_t>(i * 32)); p += 2;
            putU16(buf, p, 32); p += 2;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
        }
        REQUIRE(p == sectionFileOffset);
        for (const auto& row : rows) {
            for (uint32_t v : row) { putU32(buf, p, v); p += 4; }
        }
        p += stringTableSize;
        REQUIRE(p == total);
        return buf;
    };

    // ChrModelTextureLayer's own real layout stores CharComponentTextureLayoutsID
    // as "$noninline,relation$" -- built directly (not via buildFlatRecords,
    // which has no relationship-map support) with 6 inline fields (ID
    // inline this time, via idIndex) plus a real relationship map giving
    // the layout ID by record index, the same real shape
    // chrmodeltexturelayer.db2 itself has.
    auto buildTextureLayerFile = [&]() {
        uint32_t recordCount = 1;
        uint32_t fieldCount = 6;  // ID, TextureType, Layer, Flags, BlendMode, TextureSectionTypeBitMask
        size_t recordSize = fieldCount * 4;
        size_t stringTableSize = 1;
        size_t sectionFileOffset = kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize +
                                    fieldCount * kFieldStorageInfoSize;
        size_t relationshipMapSize = 12 + 1 * 8;
        size_t total = sectionFileOffset + recordCount * recordSize + stringTableSize + relationshipMapSize;
        std::vector<uint8_t> buf(total, 0);
        std::memcpy(buf.data(), "WDC5", 4);
        putU32(buf, 4, 5);
        size_t p = 8 + 128;
        putU32(buf, p, recordCount); p += 4;
        putU32(buf, p, fieldCount); p += 4;
        putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
        putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
        putU32(buf, p, kLayerHash); p += 4;
        putU32(buf, p, kLayerLayoutHash); p += 4;
        putU32(buf, p, 9); p += 4;
        putU32(buf, p, 9); p += 4;
        putU32(buf, p, 0); p += 4;
        putU16(buf, p, 0); p += 2;  // flags: 0 -- ID is inline (idIndex below), CharComponentTextureLayoutsID is the noninline-relation one
        putU16(buf, p, 0); p += 2;  // idIndex: field 0
        putU32(buf, p, fieldCount); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, fieldCount * kFieldStorageInfoSize); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 1); p += 4;
        REQUIRE(p == kHeaderSize);
        putU64(buf, p, 0); p += 8;
        putU32(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
        putU32(buf, p, recordCount); p += 4;
        putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;  // idListSize: 0 -- id is inline here
        putU32(buf, p, static_cast<uint32_t>(relationshipMapSize)); p += 4;
        putU32(buf, p, 0); p += 4;
        putU32(buf, p, 0); p += 4;
        REQUIRE(p == kHeaderSize + kSectionHeaderSize);
        for (uint32_t i = 0; i < fieldCount; ++i) { putU16(buf, p, 0); p += 2; putU16(buf, p, static_cast<uint16_t>(i * 4)); p += 2; }
        for (uint32_t i = 0; i < fieldCount; ++i) {
            putU16(buf, p, static_cast<uint16_t>(i * 32)); p += 2;
            putU16(buf, p, 32); p += 2;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
            putU32(buf, p, 0); p += 4;
        }
        REQUIRE(p == sectionFileOffset);
        // ID=9, TextureType=1, Layer=0, Flags=0, BlendMode=0, TextureSectionTypeBitMask=0xFFFFFFFF
        for (uint32_t v : {9u, 1u, 0u, 0u, 0u, 0xFFFFFFFFu}) { putU32(buf, p, v); p += 4; }
        p += stringTableSize;
        putU32(buf, p, 1); p += 4;  // relationship_map.num_entries
        putU32(buf, p, 7); p += 4;  // min_id
        putU32(buf, p, 7); p += 4;  // max_id
        putU32(buf, p, 7); p += 4;  // entry.foreign_id = CharComponentTextureLayoutsID 7
        putU32(buf, p, 0); p += 4;  // entry.record_index = 0 (flags & 0x02 clear)
        REQUIRE(p == total);
        return buf;
    };

    writeFile(dbd.dir / "charcomponenttexturelayouts.db2", buildLayouts());
    // ID, CharComponentTextureLayoutsID, TextureType, Width, Height, Flags
    writeFile(dbd.dir / "chrmodelmaterial.db2",
              buildFlatRecords(kMaterialHash, kMaterialLayoutHash, 6,
                                {{50, 7, 1, 1024, 512, 0}, {51, 7, 8, 256, 256, 0}, {52, 99, 1, 64, 64, 0}}));
    // ID, CharComponentTextureLayoutID, SectionType, X, Y, Width, Height, OverlapSectionMask
    writeFile(dbd.dir / "charcomponenttexturesections.db2",
              buildFlatRecords(kSectionHash, kSectionLayoutHash, 8,
                                {{1, 7, 0, 0, 0, 512, 256, 0}, {2, 99, 0, 0, 0, 100, 100, 0}}));
    writeFile(dbd.dir / "chrmodeltexturelayer.db2", buildTextureLayerFile());

    std::ostringstream err;
    auto data = husk::chrmodel::load(dbd.dir.string(), dbd.dir.string(), err);
    REQUIRE(data.has_value());
    REQUIRE(data->layouts.size() == 1);
    CHECK(data->layouts[0].id == 7);
    CHECK(data->layouts[0].width == 1024);
    CHECK(data->layouts[0].height == 512);

    REQUIRE(data->materials.size() == 3);
    int layout7Materials = 0;
    for (const auto& m : data->materials) {
        if (m.charComponentTextureLayoutsId == 7) ++layout7Materials;
    }
    CHECK(layout7Materials == 2);

    REQUIRE(data->sections.size() == 2);
    CHECK(data->sections[0].charComponentTextureLayoutId == 7);
    CHECK(data->sections[0].width == 512);

    REQUIRE(data->textureLayers.size() == 1);
    // ChrModelTextureLayer's CharComponentTextureLayoutsID is resolved via
    // the relationship map (noninline,relation), not a decoded field --
    // this is the exact real gap closed for chrmodeltexturelayer.db2.
    CHECK(data->textureLayers[0].charComponentTextureLayoutsId == 7);
    CHECK(data->textureLayers[0].id == 9);
}
