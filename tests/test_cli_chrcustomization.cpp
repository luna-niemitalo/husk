// CLI tier: `husk export --db2-dir/--dbd-dir/--customization-choice-ids` --
// exercises the real compiled binary (see run_husk.hpp) against small,
// synthetic, on-disk M2/.skin/.skel fixtures plus a small synthetic
// WoWDBDefs/.db2 fixture set, verifying the real `enabled_geosets`/
// `selected_by_choice_ids` glTF extras land in the actual output .glb.
// TODO/TODO_correctness.md #2, src/chrcustomization_db2.hpp.

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

// A flat, all-inline-fields WDC5 file -- same shape as
// tests/test_cli_chrmodel.cpp's own buildFlatDb2, kept as a local copy per
// this project's own per-file-fixture convention.
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

void writeChrCustomizationDbd(const fs::path& dbdDir, uint32_t elementLayoutHash, uint32_t geosetLayoutHash,
                               uint32_t boneSetLayoutHash) {
    fs::create_directories(dbdDir / "definitions");
    std::ostringstream manifest;
    manifest << "[\n"
             << "  {\"tableName\": \"ChrCustomizationElement\", \"tableHash\": \"61616161\"},\n"
             << "  {\"tableName\": \"ChrCustomizationGeoset\", \"tableHash\": \"62626262\"},\n"
             << "  {\"tableName\": \"ChrCustomizationBoneSet\", \"tableHash\": \"63636363\"}\n"
             << "]\n";
    writeTextFile(dbdDir / "manifest.json", manifest.str());

    std::ostringstream elementDbd;
    elementDbd << "COLUMNS\nint ID\nint ChrCustomizationChoiceID\n"
               << "int<ChrCustomizationGeoset::ID> ChrCustomizationGeosetID\n"
               << "int<ChrCustomizationBoneSet::ID> ChrCustomizationBoneSetID\n\n"
               << "LAYOUT " << std::hex << elementLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
               << "$id$ID<32>\nChrCustomizationChoiceID<32>\nChrCustomizationGeosetID<32>\n"
               << "ChrCustomizationBoneSetID<32>\n";
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationElement.dbd", elementDbd.str());

    std::ostringstream geosetDbd;
    geosetDbd << "COLUMNS\nint ID\nint GeosetType\nint GeosetID\n\n"
              << "LAYOUT " << std::hex << geosetLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
              << "$id$ID<32>\nGeosetType<32>\nGeosetID<32>\n";
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationGeoset.dbd", geosetDbd.str());

    std::ostringstream boneSetDbd;
    boneSetDbd << "COLUMNS\nint ID\nint BoneFileDataID\n\n"
               << "LAYOUT " << std::hex << boneSetLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
               << "$id$ID<32>\nBoneFileDataID<32>\n";
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationBoneSet.dbd", boneSetDbd.str());
}

}  // namespace

TEST_CASE("husk export --customization-choice-ids resolves a real geoset selection and marks a "
          "matching --bones-dir correction set, end to end -- also regression-tests one choice "
          "owning several ChrCustomizationElement rows") {
    auto dir = defaultsDir("chrcustchoice");
    writeFile(dir / "chrcustchoice.m2", tinyValidM2());
    writeFile(dir / "chrcustchoice00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcustchoice.skel", boneCorrectionSkel());  // BFID 424242
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0x71717171, 0x72727272, 0x73737373);

    // Real-shaped case this test regression-covers: choice 7 owns TWO
    // ChrCustomizationElement rows -- the first carries neither a geoset
    // nor a boneset (0/0, matching most real rows for a given choice), the
    // second carries the real ChrCustomizationBoneSetID. Choice 9 owns one
    // row with a real ChrCustomizationGeosetID. An earlier version of the
    // reader only looked at the *first* row per choice and silently missed
    // both real values here.
    writeFile(db2Dir / "chrcustomizationelement.db2",
              buildFlatDb2(0x61616161, 0x71717171, {{1, 7, 0, 0}, {2, 7, 0, 5}, {3, 9, 6, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2",
              buildFlatDb2(0x62626262, 0x72727272, {{6, 1, 3}}));  // geoset_id = 1*100+3 = 103
    writeFile(db2Dir / "chrcustomizationboneset.db2",
              buildFlatDb2(0x63636363, 0x73737373, {{5, 424242}}));  // matches the .bone file above

    auto result = runHusk("export " + (dir / "chrcustchoice.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --bones-dir " + dir.string() +
                           " --customization-choice-ids 7,9");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 real geoset selection(s)") != std::string::npos);
    CHECK(result.output.find("1 matched bone-correction-set selection(s)") != std::string::npos);

    fs::path glbPath = dir / "chrcustchoice.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("enabled_geosets") != std::string::npos);
    CHECK(bytes.find("\"choice_id\":9") != std::string::npos);
    CHECK(bytes.find("\"geoset_id\":103") != std::string::npos);
    CHECK(bytes.find("selected_by_choice_ids") != std::string::npos);
    CHECK(bytes.find("\"file_data_id\":424242") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --customization-choice-ids: a choice resolving to a real BoneFileDataID "
          "that isn't among this model's own resolved --bones-dir sets is reported, not silently "
          "dropped or fabricated") {
    auto dir = defaultsDir("chrcustchoiceunmatched");
    writeFile(dir / "chrcustchoiceunmatched.m2", tinyValidM2());
    writeFile(dir / "chrcustchoiceunmatched00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcustchoiceunmatched.skel", boneCorrectionSkel());  // BFID 424242
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0x81818181, 0x82828282, 0x83838383);

    writeFile(db2Dir / "chrcustomizationelement.db2",
              buildFlatDb2(0x61616161, 0x81818181, {{1, 3, 0, 4}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0x82828282, {}));
    writeFile(db2Dir / "chrcustomizationboneset.db2",
              buildFlatDb2(0x63636363, 0x83838383, {{4, 999999}}));  // does NOT match 424242

    auto result = runHusk("export " + (dir / "chrcustchoiceunmatched.m2").string() + " --db2-dir " +
                           db2Dir.string() + " --dbd-dir " + dbdDir.string() + " --bones-dir " +
                           dir.string() + " --customization-choice-ids 3");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("0 matched bone-correction-set selection(s)") != std::string::npos);
    CHECK(result.output.find("wasn't among this model's own resolved --bones-dir correction sets") !=
          std::string::npos);

    fs::path glbPath = dir / "chrcustchoiceunmatched.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("selected_by_choice_ids") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --customization-choice-ids: an unresolvable choice ID is reported and "
          "doesn't fail the export") {
    auto dir = defaultsDir("chrcustchoicemissing");
    writeFile(dir / "chrcustchoicemissing.m2", tinyValidM2());
    writeFile(dir / "chrcustchoicemissing00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcustchoicemissing.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0x91919191, 0x92929292, 0x93939393);
    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0x91919191, {{1, 3, 0, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0x92929292, {}));
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0x93939393, {}));

    auto result = runHusk("export " + (dir / "chrcustchoicemissing.m2").string() + " --db2-dir " +
                           db2Dir.string() + " --dbd-dir " + dbdDir.string() +
                           " --customization-choice-ids 404");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("ChrCustomizationChoiceID 404 matched no row") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --customization-choice-ids given without --db2-dir/--dbd-dir skips the "
          "feature, doesn't fail the export") {
    auto dir = defaultsDir("chrcustchoicepartial");
    writeFile(dir / "chrcustchoicepartial.m2", tinyValidM2());
    writeFile(dir / "chrcustchoicepartial00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcustchoicepartial.skel", boneCorrectionSkel());

    auto result = runHusk("export " + (dir / "chrcustchoicepartial.m2").string() +
                           " --customization-choice-ids 3");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("must all be given together") != std::string::npos);

    fs::path glbPath = dir / "chrcustchoicepartial.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("enabled_geosets") == std::string::npos);

    fs::remove_all(dir);
}
