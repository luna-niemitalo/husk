// CLI tier: `husk export --db2-dir/--dbd-dir/--creature-display-id` --
// exercises the real compiled binary (see run_husk.hpp) against small,
// synthetic, on-disk M2/.skin fixtures plus a small synthetic
// WoWDBDefs/.db2 fixture set, verifying the real `creature_enabled_geosets`
// glTF extras land in the actual output .glb. CreatureDisplayInfoGeosetData.db2
// provides the authoritative default-geoset selection for a creature display
// (unlike player-character choices which need caller input).

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

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

// A flat, all-inline-fields WDC5 file for CreatureDisplayInfoGeosetData.db2.
// Simple fixture: 3 uint32 fields (CreatureDisplayInfoID, GeosetIndex, GeosetValue),
// matching the real table's structure.
std::vector<uint8_t> buildCreatureGeosetDb2(uint32_t tableHash, uint32_t layoutHash,
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

void writeCreatureGeosetDbd(const fs::path& dbdDir) {
    fs::create_directories(dbdDir / "definitions");
    std::ostringstream manifest;
    manifest << "[\n"
             << "  {\"tableName\": \"CreatureDisplayInfoGeosetData\", \"tableHash\": \"61616161\"}\n"
             << "]\n";
    writeTextFile(dbdDir / "manifest.json", manifest.str());

    std::ostringstream geosetDbd;
    geosetDbd << "COLUMNS\n"
              << "int<CreatureDisplayInfo::ID> CreatureDisplayInfoID\n"
              << "int GeosetIndex\n"
              << "int GeosetValue\n\n"
              << "LAYOUT 62626262\nBUILD 1.0.0.1\n"
              << "$id$CreatureDisplayInfoID<32>\nGeosetIndex<32>\nGeosetValue<32>\n";
    writeTextFile(dbdDir / "definitions" / "CreatureDisplayInfoGeosetData.dbd", geosetDbd.str());
}

}  // namespace

TEST_CASE("husk export --creature-display-id resolves real creature geoset selections and attaches "
          "creature_enabled_geosets extras to the root joint, end to end") {
    auto dir = defaultsDir("creaturegeoset");
    writeFile(dir / "creaturegeoset.m2", tinyValidM2());
    writeFile(dir / "creaturegeoset00.skin", tinyMatchingSkin());
    writeFile(dir / "creaturegeoset.skel", boneCorrectionSkel());  // Need a skeleton for extras to attach

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeCreatureGeosetDbd(dbdDir);

    // CreatureDisplayInfoID 42 has 3 geoset entries:
    // - GeosetIndex=0, GeosetValue=1 -> geosetId = (0+1)*100+1 = 101
    // - GeosetIndex=1, GeosetValue=0 -> geosetId = (1+1)*100+0 = 200
    // - GeosetIndex=2, GeosetValue=3 -> geosetId = (2+1)*100+3 = 303
    writeFile(db2Dir / "creaturedisplayinfogeosetdata.db2",
              buildCreatureGeosetDb2(0x61616161, 0x62626262,
                                    {{42, 0, 1}, {42, 1, 0}, {42, 2, 3}}));

    auto result = runHusk("export " + (dir / "creaturegeoset.m2").string() +
                          " --db2-dir " + db2Dir.string() +
                          " --dbd-dir " + dbdDir.string() +
                          " --creature-display-id 42");
    CHECK(result.exitCode == 0);

    fs::path glbPath = dir / "creaturegeoset.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());

    // Check that the key exists and contains the expected geoset data
    CHECK(bytes.find("creature_enabled_geosets") != std::string::npos);
    CHECK(bytes.find("\"geoset_index\":0") != std::string::npos);
    CHECK(bytes.find("\"geoset_value\":1") != std::string::npos);
    CHECK(bytes.find("\"geoset_id\":101") != std::string::npos);
    CHECK(bytes.find("\"geoset_index\":1") != std::string::npos);
    CHECK(bytes.find("\"geoset_value\":0") != std::string::npos);
    CHECK(bytes.find("\"geoset_id\":200") != std::string::npos);
    CHECK(bytes.find("\"geoset_index\":2") != std::string::npos);
    CHECK(bytes.find("\"geoset_value\":3") != std::string::npos);
    CHECK(bytes.find("\"geoset_id\":303") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --creature-display-id: when a creature display ID has no geoset data, "
          "creature_enabled_geosets is not attached (it's empty, not a fabricated zero-entry array)") {
    auto dir = defaultsDir("creaturegeoset-empty");
    writeFile(dir / "creaturegeoset-empty.m2", tinyValidM2());
    writeFile(dir / "creaturegeoset-empty00.skin", tinyMatchingSkin());
    writeFile(dir / "creaturegeoset-empty.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeCreatureGeosetDbd(dbdDir);

    // No entries for display ID 99 in the database
    writeFile(db2Dir / "creaturedisplayinfogeosetdata.db2",
              buildCreatureGeosetDb2(0x61616161, 0x62626262,
                                    {{42, 0, 1}}));  // Only has entries for display 42

    auto result = runHusk("export " + (dir / "creaturegeoset-empty.m2").string() +
                          " --db2-dir " + db2Dir.string() +
                          " --dbd-dir " + dbdDir.string() +
                          " --creature-display-id 99");
    CHECK(result.exitCode == 0);

    fs::path glbPath = dir / "creaturegeoset-empty.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());

    // The key should not be present when no geosets resolve for the display
    CHECK(bytes.find("creature_enabled_geosets") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export without --creature-display-id: creature_enabled_geosets is not attached") {
    auto dir = defaultsDir("creaturegeoset-no-flag");
    writeFile(dir / "creaturegeoset-no-flag.m2", tinyValidM2());
    writeFile(dir / "creaturegeoset-no-flag00.skin", tinyMatchingSkin());
    writeFile(dir / "creaturegeoset-no-flag.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeCreatureGeosetDbd(dbdDir);

    writeFile(db2Dir / "creaturedisplayinfogeosetdata.db2",
              buildCreatureGeosetDb2(0x61616161, 0x62626262,
                                    {{42, 0, 1}}));

    // Export without --creature-display-id flag
    auto result = runHusk("export " + (dir / "creaturegeoset-no-flag.m2").string() +
                          " --db2-dir " + db2Dir.string() +
                          " --dbd-dir " + dbdDir.string());
    CHECK(result.exitCode == 0);

    fs::path glbPath = dir / "creaturegeoset-no-flag.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());

    // Without the flag, the key should not appear
    CHECK(bytes.find("creature_enabled_geosets") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export without --db2-dir/--dbd-dir: creature_enabled_geosets is not attached "
          "even if --creature-display-id is given") {
    auto dir = defaultsDir("creaturegeoset-no-db2");
    writeFile(dir / "creaturegeoset-no-db2.m2", tinyValidM2());
    writeFile(dir / "creaturegeoset-no-db200.skin", tinyMatchingSkin());
    writeFile(dir / "creaturegeoset-no-db2.skel", boneCorrectionSkel());

    // Export with --creature-display-id but no --db2-dir/--dbd-dir
    auto result = runHusk("export " + (dir / "creaturegeoset-no-db2.m2").string() +
                          " --creature-display-id 42");
    CHECK(result.exitCode == 0);

    fs::path glbPath = dir / "creaturegeoset-no-db2.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());

    // Without DB2 data, the key should not appear
    CHECK(bytes.find("creature_enabled_geosets") == std::string::npos);

    fs::remove_all(dir);
}
