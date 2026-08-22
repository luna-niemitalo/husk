// CLI tier: `husk appearance-string` -- exercises the real compiled binary
// (see run_husk.hpp). Written alongside the command's migration from
// hand-rolled argv parsing to CLI11 (TODO/CLEANUP_TODO.md #3) -- this
// command previously had zero CLI-tier coverage.
//
// The `--db2-dir`/`--dbd-dir`-driven `gear` resolution tests below
// (TODO/CHAR_TEXTURE_COMPOSITING_TODO.md Stage 6, src/itemappearance_db2.hpp/
// src/modelfiledata_db2.hpp) use a synthetic WoWDBDefs/.db2 fixture set,
// same "own local copy of buildFlatDb2/writeTextFile, not a shared header"
// convention tests/test_cli_chrmodel_id.cpp's own comment already
// documents -- every field in these synthetic tables is declared plain
// inline, not the real production `$id$`/`$relation$` shape, since the
// resolution logic under test only cares about column *names* resolving
// correctly, not the real WDC5 storage layout (see src/db2table.hpp).

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

namespace {

void putU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) { std::memcpy(buf.data() + offset, &v, 2); }
void putU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) { std::memcpy(buf.data() + offset, &v, 4); }
void putU64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) { std::memcpy(buf.data() + offset, &v, 8); }

constexpr size_t kHeaderSize = 204;
constexpr size_t kSectionHeaderSize = 40;
constexpr size_t kFieldStructureSize = 4;
constexpr size_t kFieldStorageInfoSize = 24;

// A flat, all-inline-fields WDC5 file -- same shape as
// tests/test_cli_chrmodel_id.cpp's own buildFlatDb2, kept as a local copy
// per this project's own per-file-fixture convention.
std::vector<uint8_t> buildFlatDb2(uint32_t tableHash, uint32_t layoutHash,
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
    putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
    putU32(buf, p, fieldCount); p += 4;
    putU32(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32(buf, p, tableHash); p += 4;
    putU32(buf, p, layoutHash); p += 4;
    putU32(buf, p, 1); p += 4;
    putU32(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
    putU32(buf, p, 0); p += 4;
    putU16(buf, p, 0); p += 2;
    putU16(buf, p, 0); p += 2;
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
}

void writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream f(path);
    f << text;
}

// Writes the 6-table synthetic DB2 fixture set used by the gear-resolution
// tests below (src/itemappearance_db2.hpp/src/modelfiledata_db2.hpp/
// src/texturefiledata_db2.hpp's own chain).
void writeItemAppearanceDbd(const fs::path& dbdDir) {
    fs::create_directories(dbdDir / "definitions");
    writeTextFile(dbdDir / "manifest.json",
                  "[\n"
                  "  {\"tableName\": \"ItemModifiedAppearance\", \"tableHash\": \"a0000001\"},\n"
                  "  {\"tableName\": \"ItemAppearance\", \"tableHash\": \"a0000002\"},\n"
                  "  {\"tableName\": \"ItemDisplayInfo\", \"tableHash\": \"a0000003\"},\n"
                  "  {\"tableName\": \"ItemDisplayInfoModelMatRes\", \"tableHash\": \"a0000004\"},\n"
                  "  {\"tableName\": \"ModelFileData\", \"tableHash\": \"a0000005\"},\n"
                  "  {\"tableName\": \"TextureFileData\", \"tableHash\": \"a0000006\"},\n"
                  "  {\"tableName\": \"ItemDisplayInfoMaterialRes\", \"tableHash\": \"a0000007\"}\n"
                  "]\n");
    writeTextFile(dbdDir / "definitions" / "ItemModifiedAppearance.dbd",
                  "COLUMNS\nint ID\nint ItemAppearanceID\n\n"
                  "LAYOUT b0000001\nBUILD 1.0.0.1\n$id$ID<32>\nItemAppearanceID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemAppearance.dbd",
                  "COLUMNS\nint ID\nint ItemDisplayInfoID\n\n"
                  "LAYOUT b0000002\nBUILD 1.0.0.1\n$id$ID<32>\nItemDisplayInfoID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemDisplayInfo.dbd",
                  "COLUMNS\nint ID\nint ModelResourcesID\n\n"
                  "LAYOUT b0000003\nBUILD 1.0.0.1\n$id$ID<32>\nModelResourcesID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemDisplayInfoModelMatRes.dbd",
                  "COLUMNS\nint ID\nint ItemDisplayInfoID\nint MaterialResourcesID\nint TextureType\nint ModelIndex\n\n"
                  "LAYOUT b0000004\nBUILD 1.0.0.1\n$id$ID<32>\nItemDisplayInfoID<32>\nMaterialResourcesID<32>\n"
                  "TextureType<32>\nModelIndex<32>\n");
    writeTextFile(dbdDir / "definitions" / "ModelFileData.dbd",
                  "COLUMNS\nint FileDataID\nint ModelResourcesID\n\n"
                  "LAYOUT b0000005\nBUILD 1.0.0.1\n$id$FileDataID<32>\nModelResourcesID<32>\n");
    writeTextFile(dbdDir / "definitions" / "TextureFileData.dbd",
                  "COLUMNS\nint FileDataID\nint MaterialResourcesID\nint UsageType\n\n"
                  "LAYOUT b0000006\nBUILD 1.0.0.1\n$id$FileDataID<32>\nMaterialResourcesID<32>\nUsageType<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemDisplayInfoMaterialRes.dbd",
                  "COLUMNS\nint ID\nint ItemDisplayInfoID\nint ComponentSection\nint MaterialResourcesID\n\n"
                  "LAYOUT b0000007\nBUILD 1.0.0.1\n$id$ID<32>\nItemDisplayInfoID<32>\nComponentSection<32>\n"
                  "MaterialResourcesID<32>\n");
}

}  // namespace

TEST_CASE("husk appearance-string --validate: a well-formed string round-trips") {
    auto result = runHusk("appearance-string --validate \"husk-appearance/1 race=10 sex=1\"");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("valid") != std::string::npos);
    CHECK(result.output.find("race=10 sex=1") != std::string::npos);
}

TEST_CASE("husk appearance-string --validate: a malformed string fails cleanly") {
    auto result = runHusk("appearance-string --validate \"not-a-real-appearance-string\"");
    CHECK(result.exitCode != 0);
}

TEST_CASE("husk appearance-string: --validate is required -- CLI11's RequiredError, not a crash") {
    auto result = runHusk("appearance-string");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("required") != std::string::npos);
}

TEST_CASE("husk appearance-string: gear entries stay opaque without --db2-dir/--dbd-dir") {
    auto result = runHusk("appearance-string --validate \"husk-appearance/1 gear=MAINHAND:15\"");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("gear entries not resolved") != std::string::npos);
}

TEST_CASE("husk appearance-string --db2-dir/--dbd-dir: a real gear ItemModifiedAppearanceID "
          "resolves through the full ItemModifiedAppearance -> ItemAppearance -> ItemDisplayInfo "
          "-> ItemDisplayInfoModelMatRes chain to a real equipped-item model FileDataID (via "
          "ModelFileData) and texture FileDataID (via TextureFileData) -- "
          "TODO/CHAR_TEXTURE_COMPOSITING_TODO.md Stage 6.") {
    auto dir = defaultsDir("appearancegear");
    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeItemAppearanceDbd(dbdDir);

    // ItemModifiedAppearanceID 15 -> ItemAppearanceID 154.
    writeFile(db2Dir / "itemmodifiedappearance.db2", buildFlatDb2(0xa0000001, 0xb0000001, {{15, 154}}));
    // ItemAppearanceID 154 -> ItemDisplayInfoID 1542.
    writeFile(db2Dir / "itemappearance.db2", buildFlatDb2(0xa0000002, 0xb0000002, {{154, 1542}}));
    // ItemDisplayInfoID 1542 -> ModelResourcesID 160.
    writeFile(db2Dir / "itemdisplayinfo.db2", buildFlatDb2(0xa0000003, 0xb0000003, {{1542, 160}}));
    // ItemDisplayInfoID 1542 owns one MaterialResourcesID 22758 row, TextureType 2.
    writeFile(db2Dir / "itemdisplayinfomodelmatres.db2",
              buildFlatDb2(0xa0000004, 0xb0000004, {{1, 1542, 22758, 2, 0}}));
    // ModelResourcesID 160 -> real .m2 FileDataID 370361.
    writeFile(db2Dir / "modelfiledata.db2", buildFlatDb2(0xa0000005, 0xb0000005, {{370361, 160}}));
    // MaterialResourcesID 22758 -> real texture FileDataID 148134, UsageType 0.
    writeFile(db2Dir / "texturefiledata.db2", buildFlatDb2(0xa0000006, 0xb0000006, {{148134, 22758, 0}}));

    std::ostringstream cmd;
    cmd << "appearance-string --validate \"husk-appearance/1 gear=MAINHAND:15\""
        << " --db2-dir " << db2Dir.string() << " --dbd-dir " << dbdDir.string();
    auto result = runHusk(cmd.str());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("gear MAINHAND:15 -> ItemDisplayInfoID=1542") != std::string::npos);
    CHECK(result.output.find("model=370361") != std::string::npos);
    CHECK(result.output.find("texture(type=2)=148134") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk appearance-string --db2-dir/--dbd-dir: an ItemDisplayInfoMaterialRes-bearing item "
          "resolves real base-character-mesh section overlays (case 2, EQUIPPED_GEAR_RENDER_TODO.md "
          "step 1's own real ComponentSection finding) alongside case 1's own ModelMatRes texture -- "
          "a real multi-section item (e.g. boots spanning LEG_LOWER + FOOT) carries more than one "
          "section() entry, not just the first.") {
    auto dir = defaultsDir("appearancegearsection");
    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeItemAppearanceDbd(dbdDir);

    // Real-shaped chain, IDs inspired by a real local ItemDisplayInfoID (233)
    // that genuinely carries two ComponentSection rows (6=LEG_LOWER, 7=FOOT).
    writeFile(db2Dir / "itemmodifiedappearance.db2", buildFlatDb2(0xa0000001, 0xb0000001, {{367, 495}}));
    writeFile(db2Dir / "itemappearance.db2", buildFlatDb2(0xa0000002, 0xb0000002, {{495, 233}}));
    writeFile(db2Dir / "itemdisplayinfo.db2", buildFlatDb2(0xa0000003, 0xb0000003, {{233, 0}}));
    writeFile(db2Dir / "itemdisplayinfomodelmatres.db2", buildFlatDb2(0xa0000004, 0xb0000004, {}));
    writeFile(db2Dir / "modelfiledata.db2", buildFlatDb2(0xa0000005, 0xb0000005, {}));
    // MaterialResourcesID 34187/27801 -> real texture FileDataIDs, UsageType 0.
    writeFile(db2Dir / "texturefiledata.db2",
              buildFlatDb2(0xa0000006, 0xb0000006, {{500001, 34187, 0}, {500002, 27801, 0}}));
    // ItemDisplayInfoID 233 owns two ItemDisplayInfoMaterialRes rows: section 6
    // (LEG_LOWER) -> MaterialResourcesID 34187, section 7 (FOOT) -> 27801.
    writeFile(db2Dir / "itemdisplayinfomaterialres.db2",
              buildFlatDb2(0xa0000007, 0xb0000007, {{1, 233, 6, 34187}, {2, 233, 7, 27801}}));

    std::ostringstream cmd;
    cmd << "appearance-string --validate \"husk-appearance/1 gear=FEET:367\""
        << " --db2-dir " << db2Dir.string() << " --dbd-dir " << dbdDir.string();
    auto result = runHusk(cmd.str());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("gear FEET:367 -> ItemDisplayInfoID=233") != std::string::npos);
    CHECK(result.output.find("section(6)=500001") != std::string::npos);
    CHECK(result.output.find("section(7)=500002") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk appearance-string --db2-dir/--dbd-dir: an ItemModifiedAppearanceID with no "
          "matching row anywhere in the chain reports unresolved, not a crash or a guess.") {
    auto dir = defaultsDir("appearancegearunresolved");
    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeItemAppearanceDbd(dbdDir);

    writeFile(db2Dir / "itemmodifiedappearance.db2", buildFlatDb2(0xa0000001, 0xb0000001, {{1, 2}}));
    writeFile(db2Dir / "itemappearance.db2", buildFlatDb2(0xa0000002, 0xb0000002, {}));
    writeFile(db2Dir / "itemdisplayinfo.db2", buildFlatDb2(0xa0000003, 0xb0000003, {}));
    writeFile(db2Dir / "itemdisplayinfomodelmatres.db2", buildFlatDb2(0xa0000004, 0xb0000004, {}));
    writeFile(db2Dir / "modelfiledata.db2", buildFlatDb2(0xa0000005, 0xb0000005, {}));
    writeFile(db2Dir / "texturefiledata.db2", buildFlatDb2(0xa0000006, 0xb0000006, {}));

    std::ostringstream cmd;
    cmd << "appearance-string --validate \"husk-appearance/1 gear=MAINHAND:999\""
        << " --db2-dir " << db2Dir.string() << " --dbd-dir " << dbdDir.string();
    auto result = runHusk(cmd.str());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("gear MAINHAND:999 -- unresolved") != std::string::npos);

    fs::remove_all(dir);
}
