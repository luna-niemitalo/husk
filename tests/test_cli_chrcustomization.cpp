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
#include <iostream>
#include <sstream>
#include <utility>
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

// One row for buildOptionOrChoiceDb2: ID, a foreign key (ChrModelID for
// Option, ChrCustomizationOptionID for Choice), OrderIndex, and a real
// Name_lang string -- the exact 4-column shape both real tables share.
struct NamedRow {
    uint32_t id;
    uint32_t fk;
    uint32_t orderIndex;
    std::string name;
};

// Single-section, all-inline WDC5 fixture with one real string field
// (field index 3, Name_lang) resolved via the real db2::resolveFieldString
// path -- not a mock, the actual production string-resolution code this
// session wired chrcustomization_db2.cpp's Option/Choice loaders through.
// Field layout: [0]=ID [1]=fk [2]=OrderIndex [3]=Name_lang (offset into
// the string table appended right after record data). Single section, so
// db2::stringOffsetSectionCorrection is always 0 here -- multi-section
// string-offset correctness is covered elsewhere (tests/test_db2.cpp).
std::vector<uint8_t> buildOptionOrChoiceDb2(uint32_t tableHash, uint32_t layoutHash,
                                             const std::vector<NamedRow>& rows) {
    constexpr uint32_t fieldCount = 4;
    constexpr size_t recordSize = fieldCount * 4;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize + fieldCount * kFieldStorageInfoSize;
    size_t recordDataEnd = sectionFileOffset + rows.size() * recordSize;

    std::string stringTable;
    std::vector<size_t> nameOffsetInTable(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        nameOffsetInTable[i] = stringTable.size();
        stringTable += rows[i].name;
        stringTable += '\0';
    }
    size_t stringTableSize = stringTable.size();
    size_t total = recordDataEnd + stringTableSize;

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

    for (size_t i = 0; i < rows.size(); ++i) {
        size_t recordPos = sectionFileOffset + i * recordSize;
        putU32(buf, recordPos + 0, rows[i].id);
        putU32(buf, recordPos + 4, rows[i].fk);
        putU32(buf, recordPos + 8, rows[i].orderIndex);
        size_t stringAbsPos = recordDataEnd + nameOffsetInTable[i];
        size_t fieldAbsPos = recordPos + 12;
        putU32(buf, recordPos + 12, static_cast<uint32_t>(stringAbsPos - fieldAbsPos));
    }
    p = recordDataEnd;
    std::memcpy(buf.data() + p, stringTable.data(), stringTable.size());
    p += stringTable.size();
    REQUIRE(p == total);
    return buf;
}

// Single-section WDC5 fixture with exactly 2 fields: [0]=ID [1]=a real
// string field -- ChrRaces.ClientFileString's own shape, same real
// db2::resolveFieldString path as buildOptionOrChoiceDb2 above, kept as a
// separate builder rather than reusing that one because the real field
// *count* must match what the test DBD declares (dbd::resolveFieldNames
// validates field count/shape, not just names).
std::vector<uint8_t> buildChrRacesDb2(uint32_t tableHash, uint32_t layoutHash,
                                       const std::vector<std::pair<uint32_t, std::string>>& rows) {
    constexpr uint32_t fieldCount = 2;
    constexpr size_t recordSize = fieldCount * 4;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize + fieldCount * kFieldStorageInfoSize;
    size_t recordDataEnd = sectionFileOffset + rows.size() * recordSize;

    std::string stringTable;
    std::vector<size_t> nameOffsetInTable(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        nameOffsetInTable[i] = stringTable.size();
        stringTable += rows[i].second;
        stringTable += '\0';
    }
    size_t stringTableSize = stringTable.size();
    size_t total = recordDataEnd + stringTableSize;

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

    for (size_t i = 0; i < rows.size(); ++i) {
        size_t recordPos = sectionFileOffset + i * recordSize;
        putU32(buf, recordPos + 0, rows[i].first);
        size_t stringAbsPos = recordDataEnd + nameOffsetInTable[i];
        size_t fieldAbsPos = recordPos + 4;
        putU32(buf, recordPos + 4, static_cast<uint32_t>(stringAbsPos - fieldAbsPos));
    }
    p = recordDataEnd;
    std::memcpy(buf.data() + p, stringTable.data(), stringTable.size());
    p += stringTable.size();
    REQUIRE(p == total);
    return buf;
}

// Optional Option/Choice/Races/RaceModels/ChrModel/CreatureDisplay/
// CreatureModel layout hashes -- 0 means "don't write that definition"
// (most existing tests only need Element/Geoset/BoneSet).
void writeChrCustomizationDbd(const fs::path& dbdDir, uint32_t elementLayoutHash, uint32_t geosetLayoutHash,
                               uint32_t boneSetLayoutHash, uint32_t optionLayoutHash = 0,
                               uint32_t choiceLayoutHash = 0, uint32_t racesLayoutHash = 0,
                               uint32_t raceModelsLayoutHash = 0, uint32_t chrModelLayoutHash = 0,
                               uint32_t creatureDisplayLayoutHash = 0, uint32_t creatureModelLayoutHash = 0) {
    fs::create_directories(dbdDir / "definitions");
    std::ostringstream manifest;
    manifest << "[\n"
             << "  {\"tableName\": \"ChrCustomizationElement\", \"tableHash\": \"61616161\"},\n"
             << "  {\"tableName\": \"ChrCustomizationGeoset\", \"tableHash\": \"62626262\"},\n"
             << "  {\"tableName\": \"ChrCustomizationBoneSet\", \"tableHash\": \"63636363\"}";
    if (optionLayoutHash != 0) manifest << ",\n  {\"tableName\": \"ChrCustomizationOption\", \"tableHash\": \"64646464\"}";
    if (choiceLayoutHash != 0) manifest << ",\n  {\"tableName\": \"ChrCustomizationChoice\", \"tableHash\": \"65656565\"}";
    if (racesLayoutHash != 0) manifest << ",\n  {\"tableName\": \"ChrRaces\", \"tableHash\": \"66666666\"}";
    if (raceModelsLayoutHash != 0) manifest << ",\n  {\"tableName\": \"ChrRaceXChrModel\", \"tableHash\": \"67676767\"}";
    if (chrModelLayoutHash != 0) manifest << ",\n  {\"tableName\": \"ChrModel\", \"tableHash\": \"68686868\"}";
    if (creatureDisplayLayoutHash != 0) manifest << ",\n  {\"tableName\": \"CreatureDisplayInfo\", \"tableHash\": \"69696969\"}";
    if (creatureModelLayoutHash != 0) manifest << ",\n  {\"tableName\": \"CreatureModelData\", \"tableHash\": \"6a6a6a6a\"}";
    manifest << "\n]\n";
    writeTextFile(dbdDir / "manifest.json", manifest.str());

    if (racesLayoutHash != 0) {
        std::ostringstream racesDbd;
        racesDbd << "COLUMNS\nint ID\nlocstring ClientFileString\n\n"
                 << "LAYOUT " << std::hex << racesLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
                 << "$id$ID<32>\nClientFileString<32>\n";
        writeTextFile(dbdDir / "definitions" / "ChrRaces.dbd", racesDbd.str());
    }
    if (raceModelsLayoutHash != 0) {
        std::ostringstream rxmDbd;
        rxmDbd << "COLUMNS\nint ID\nint ChrRacesID\nint Sex\nint ChrModelID\n\n"
               << "LAYOUT " << std::hex << raceModelsLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
               << "$id$ID<32>\nChrRacesID<32>\nSex<32>\nChrModelID<32>\n";
        writeTextFile(dbdDir / "definitions" / "ChrRaceXChrModel.dbd", rxmDbd.str());
    }
    if (chrModelLayoutHash != 0) {
        std::ostringstream chrModelDbd;
        chrModelDbd << "COLUMNS\nint ID\nint DisplayID\n\n"
                    << "LAYOUT " << std::hex << chrModelLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
                    << "$id$ID<32>\nDisplayID<32>\n";
        writeTextFile(dbdDir / "definitions" / "ChrModel.dbd", chrModelDbd.str());
    }
    if (creatureDisplayLayoutHash != 0) {
        std::ostringstream cdiDbd;
        cdiDbd << "COLUMNS\nint ID\nint ModelID\n\n"
               << "LAYOUT " << std::hex << creatureDisplayLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
               << "$id$ID<32>\nModelID<32>\n";
        writeTextFile(dbdDir / "definitions" / "CreatureDisplayInfo.dbd", cdiDbd.str());
    }
    if (creatureModelLayoutHash != 0) {
        std::ostringstream cmdDbd;
        cmdDbd << "COLUMNS\nint ID\nint FileDataID\n\n"
               << "LAYOUT " << std::hex << creatureModelLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
               << "$id$ID<32>\nFileDataID<32>\n";
        writeTextFile(dbdDir / "definitions" / "CreatureModelData.dbd", cmdDbd.str());
    }

    if (optionLayoutHash != 0) {
        std::ostringstream optionDbd;
        optionDbd << "COLUMNS\nint ID\nint ChrModelID\nint OrderIndex\nlocstring Name_lang\n\n"
                  << "LAYOUT " << std::hex << optionLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
                  << "$id$ID<32>\nChrModelID<32>\nOrderIndex<32>\nName_lang<32>\n";
        writeTextFile(dbdDir / "definitions" / "ChrCustomizationOption.dbd", optionDbd.str());
    }
    if (choiceLayoutHash != 0) {
        std::ostringstream choiceDbd;
        choiceDbd << "COLUMNS\nint ID\nint ChrCustomizationOptionID\nint OrderIndex\nlocstring Name_lang\n\n"
                  << "LAYOUT " << std::hex << choiceLayoutHash << std::dec << "\nBUILD 1.0.0.1\n"
                  << "$id$ID<32>\nChrCustomizationOptionID<32>\nOrderIndex<32>\nName_lang<32>\n";
        writeTextFile(dbdDir / "definitions" / "ChrCustomizationChoice.dbd", choiceDbd.str());
    }

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

TEST_CASE("husk export --chr-model-id: auto-selects the lowest-OrderIndex choice per real "
          "ChrCustomizationOption, resolves each to its real geoset, and names both in the note "
          "output -- TODO_correctness.md #2's 'sensible default' derivation") {
    auto dir = defaultsDir("chrmodelidauto");
    writeFile(dir / "chrmodelidauto.m2", tinyValidM2());
    writeFile(dir / "chrmodelidauto00.skin", tinyMatchingSkin());
    writeFile(dir / "chrmodelidauto.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0xa1a1a1a1, 0xa2a2a2a2, 0xa3a3a3a3, 0xa4a4a4a4, 0xa5a5a5a5);

    writeFile(db2Dir / "chrcustomizationelement.db2",
              buildFlatDb2(0x61616161, 0xa1a1a1a1, {{1, 11, 6, 0}, {2, 20, 7, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2",
              buildFlatDb2(0x62626262, 0xa2a2a2a2, {{6, 2, 5}, {7, 3, 1}}));  // 205, 301
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0xa3a3a3a3, {}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0xa4a4a4a4,
                                     {{1, 42, 0, "Face"}, {2, 42, 1, "Hair"}}));
    // Option 1 ("Face"): choice 10 (OrderIndex 1, "Long") and choice 11
    // (OrderIndex 0, "Short") -- default must pick 11, the lower OrderIndex,
    // not the lower ID or declaration order. Option 2 ("Hair"): one choice.
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0xa5a5a5a5,
                                     {{10, 1, 1, "Long"}, {11, 1, 0, "Short"}, {20, 2, 0, "Braid"}}));

    auto result = runHusk("export " + (dir / "chrmodelidauto.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id 42");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("auto-selected 2 default choice(s)") != std::string::npos);
    CHECK(result.output.find("Face (option 1) -> Short (choice 11, OrderIndex 0)") != std::string::npos);
    CHECK(result.output.find("Hair (option 2) -> Braid (choice 20, OrderIndex 0)") != std::string::npos);
    CHECK(result.output.find("2 real geoset selection(s)") != std::string::npos);

    fs::path glbPath = dir / "chrmodelidauto.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("\"choice_id\":11") != std::string::npos);
    CHECK(bytes.find("\"geoset_id\":205") != std::string::npos);
    CHECK(bytes.find("\"choice_id\":20") != std::string::npos);
    CHECK(bytes.find("\"geoset_id\":301") != std::string::npos);
    // The non-default choice (10, "Long") must never be selected.
    CHECK(bytes.find("\"choice_id\":10") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id: an explicit --customization-choice-ids always wins over "
          "the default-derivation path when both are given") {
    auto dir = defaultsDir("chrmodelidoverride");
    writeFile(dir / "chrmodelidoverride.m2", tinyValidM2());
    writeFile(dir / "chrmodelidoverride00.skin", tinyMatchingSkin());
    writeFile(dir / "chrmodelidoverride.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0xb1b1b1b1, 0xb2b2b2b2, 0xb3b3b3b3, 0xb4b4b4b4, 0xb5b5b5b5);

    writeFile(db2Dir / "chrcustomizationelement.db2",
              buildFlatDb2(0x61616161, 0xb1b1b1b1, {{1, 10, 6, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0xb2b2b2b2, {{6, 2, 5}}));
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0xb3b3b3b3, {}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0xb4b4b4b4, {{1, 42, 0, "Face"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0xb5b5b5b5, {{10, 1, 0, "Default"}}));

    // Explicit choice 10 happens to equal the real default here anyway --
    // the point of this test is that the explicit-pick code path runs
    // (no "auto-selected" note), not that the resolved geoset differs.
    auto result = runHusk("export " + (dir / "chrmodelidoverride.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --customization-choice-ids 10 --chr-model-id 42");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("auto-selected") == std::string::npos);
    CHECK(result.output.find("1 real geoset selection(s)") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id: no ChrCustomizationOption/Choice rows for this model is "
          "reported and doesn't fail the export") {
    auto dir = defaultsDir("chrmodelidempty");
    writeFile(dir / "chrmodelidempty.m2", tinyValidM2());
    writeFile(dir / "chrmodelidempty00.skin", tinyMatchingSkin());
    writeFile(dir / "chrmodelidempty.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0xc1c1c1c1, 0xc2c2c2c2, 0xc3c3c3c3);
    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0xc1c1c1c1, {}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0xc2c2c2c2, {}));
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0xc3c3c3c3, {{5, 1}}));

    auto result = runHusk("export " + (dir / "chrmodelidempty.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id 42");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("resolved no ChrCustomizationOption/Choice rows") != std::string::npos);

    fs::path glbPath = dir / "chrmodelidempty.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("enabled_geosets") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: derives a real ChrModelID from the .m2's own filename "
          "(race token + sex, real WoW naming convention) and resolves default choices from it") {
    auto dir = defaultsDir("chrmodelidautoderive");
    writeFile(dir / "dracthyrfemale.m2", tinyValidM2());
    writeFile(dir / "dracthyrfemale00.skin", tinyMatchingSkin());
    writeFile(dir / "dracthyrfemale.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0xd1d1d1d1, 0xd2d2d2d2, 0xd3d3d3d3, 0xd4d4d4d4, 0xd5d5d5d5, 0xd6d6d6d6,
                              0xd7d7d7d7);

    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0xd1d1d1d1, {{1, 11, 6, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0xd2d2d2d2, {{6, 2, 5}}));  // 205
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0xd3d3d3d3, {}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0xd4d4d4d4, {{1, 89, 0, "Ears"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0xd5d5d5d5, {{11, 1, 0, "Short Fin"}}));
    writeFile(db2Dir / "chrraces.db2", buildChrRacesDb2(0x66666666, 0xd6d6d6d6, {{52, "Dracthyr"}}));
    writeFile(db2Dir / "chrracexchrmodel.db2",
              buildFlatDb2(0x67676767, 0xd7d7d7d7, {{1, 52, 1, 89}}));  // race 52, sex 1 (female) -> ChrModelID 89

    auto result = runHusk("export " + (dir / "dracthyrfemale.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id auto");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("derived ChrModelID 89 from") != std::string::npos);
    CHECK(result.output.find("race token 'dracthyr', sex 1") != std::string::npos);
    CHECK(result.output.find("auto-selected 1 default choice(s)") != std::string::npos);
    CHECK(result.output.find("Ears (option 1) -> Short Fin (choice 11, OrderIndex 0)") != std::string::npos);

    fs::path glbPath = dir / "dracthyrfemale.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("\"geoset_id\":205") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: a genuinely ambiguous race+sex match (e.g. an alternate "
          "form, several distinct real ChrModelIDs) is reported and skipped, never guessed") {
    auto dir = defaultsDir("chrmodelidautoambig");
    writeFile(dir / "dracthyrmale.m2", tinyValidM2());
    writeFile(dir / "dracthyrmale00.skin", tinyMatchingSkin());
    writeFile(dir / "dracthyrmale.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0xe1e1e1e1, 0xe2e2e2e2, 0xe3e3e3e3, 0, 0, 0xe6e6e6e6, 0xe7e7e7e7);
    // A throwaway row (choiceId 0 matches nothing real) -- chrcustomization::load
    // returns nullopt when every one of its 5 tables is empty, and this test
    // deliberately doesn't populate Option/Choice, so at least one of
    // Element/Geoset/BoneSet must be non-empty to reach the --chr-model-id
    // auto logic at all.
    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0xe1e1e1e1, {{1, 0, 0, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0xe2e2e2e2, {}));
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0xe3e3e3e3, {}));
    writeFile(db2Dir / "chrraces.db2", buildChrRacesDb2(0x66666666, 0xe6e6e6e6, {{52, "Dracthyr"}}));
    // Two distinct real ChrModelIDs (89 dragon form, 127 Visage form) for
    // the same real (race, sex) -- the actual shape found in real local
    // data this session.
    writeFile(db2Dir / "chrracexchrmodel.db2",
              buildFlatDb2(0x67676767, 0xe7e7e7e7, {{1, 52, 0, 89}, {2, 52, 0, 127}}));

    auto result = runHusk("export " + (dir / "dracthyrmale.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id auto");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("resolves to 2 distinct real ChrModelIDs (89, 127)") != std::string::npos);
    CHECK(result.output.find("not guessing") != std::string::npos);
    CHECK(result.output.find("derived ChrModelID") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: a filename that doesn't fit the real "
          "<race><sex> naming convention at all is skipped, not force-matched") {
    auto dir = defaultsDir("chrmodelidautonomatch");
    writeFile(dir / "dwagonbiddies69.m2", tinyValidM2());
    writeFile(dir / "dwagonbiddies6900.skin", tinyMatchingSkin());
    writeFile(dir / "dwagonbiddies69.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0xf1f1f1f1, 0xf2f2f2f2, 0xf3f3f3f3, 0, 0, 0xf6f6f6f6, 0xf7f7f7f7);
    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0xf1f1f1f1, {{1, 0, 0, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0xf2f2f2f2, {}));
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0xf3f3f3f3, {}));
    writeFile(db2Dir / "chrraces.db2", buildChrRacesDb2(0x66666666, 0xf6f6f6f6, {{52, "Dracthyr"}}));
    writeFile(db2Dir / "chrracexchrmodel.db2", buildFlatDb2(0x67676767, 0xf7f7f7f7, {{1, 52, 0, 89}}));

    auto result = runHusk("export " + (dir / "dwagonbiddies69.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id auto");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("doesn't match the real character-model naming convention") != std::string::npos);
    CHECK(result.output.find("derived ChrModelID") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: the FileDataID chain (via --listfile) is the primary "
          "path and correctly disambiguates a race+sex pair that's genuinely ambiguous by filename "
          "alone -- the real Dracthyr case (dragon form vs. Visage form share a race+sex)") {
    auto dir = defaultsDir("chrmodelidautofdid");
    writeFile(dir / "dracthyrmale.m2", tinyValidM2());
    writeFile(dir / "dracthyrmale00.skin", tinyMatchingSkin());
    writeFile(dir / "dracthyrmale.skel", boneCorrectionSkel());

    auto listfilePath = dir / "listfile.csv";
    { std::ofstream f(listfilePath); f << "9001;dracthyrmale.m2\n"; }

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0xa1a1a1a1, 0xa2a2a2a2, 0xa3a3a3a3, 0xa4a4a4a4, 0xa5a5a5a5, 0xa6a6a6a6,
                              0xa7a7a7a7, 0xa8a8a8a8, 0xa9a9a9a9, 0xaaaaaaaa);

    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0xa1a1a1a1, {{1, 11, 6, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0xa2a2a2a2, {{6, 2, 5}}));  // 205
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0xa3a3a3a3, {}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0xa4a4a4a4, {{1, 127, 0, "Ears"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0xa5a5a5a5, {{11, 1, 0, "Short Fin"}}));
    writeFile(db2Dir / "chrraces.db2", buildChrRacesDb2(0x66666666, 0xa6a6a6a6, {{52, "Dracthyr"}}));
    // Genuinely ambiguous by race+sex alone -- both 89 and 127 are real
    // candidates for "Dracthyr, male" (matches the real local data this
    // session found: dragon form 89, Visage form 127).
    writeFile(db2Dir / "chrracexchrmodel.db2",
              buildFlatDb2(0x67676767, 0xa7a7a7a7, {{1, 52, 0, 89}, {2, 52, 0, 127}}));
    // The FileDataID chain only defines a real ChrModel row for 127 --
    // proving the primary path resolves to exactly the right answer
    // (127) rather than inheriting the race+sex path's ambiguity.
    writeFile(db2Dir / "chrmodel.db2", buildFlatDb2(0x68686868, 0xa8a8a8a8, {{127, 600}}));
    writeFile(db2Dir / "creaturedisplayinfo.db2", buildFlatDb2(0x69696969, 0xa9a9a9a9, {{600, 500}}));
    writeFile(db2Dir / "creaturemodeldata.db2", buildFlatDb2(0x6a6a6a6a, 0xaaaaaaaa, {{500, 9001}}));

    auto result = runHusk("export " + (dir / "dracthyrmale.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --listfile " + listfilePath.string() +
                           " --listfile-root " + dir.string() + " --chr-model-id auto");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("derived ChrModelID 127 from") != std::string::npos);
    CHECK(result.output.find("own FileDataID 9001") != std::string::npos);
    CHECK(result.output.find("CreatureModelData/CreatureDisplayInfo/ChrModel chain") != std::string::npos);
    // Must NOT fall through to the ambiguous race+sex report -- the
    // primary FileDataID path already resolved a real answer.
    CHECK(result.output.find("genuinely ambiguous") == std::string::npos);
    CHECK(result.output.find("filename fallback") == std::string::npos);

    fs::path glbPath = dir / "dracthyrmale.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("\"geoset_id\":205") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --customization-choice-ids + --char-layout-id: the real material chain "
          "(ChrCustomizationMaterial -> TextureFileData) resolves a real FileDataID into "
          "chr_enabled_materials extras, and chr_texture_layout's own texture_layers carry the "
          "matching chr_model_texture_target_id join key -- TODO/CHAR_TEXTURE_COMPOSITING_TODO.md "
          "Stage 3. Pixel compositing itself is deliberately NOT husk's job (Blender shader nodes "
          "are the right layer for that -- see this file's own module doc comment), so this test "
          "only checks the real data exposure, not any rendered result.") {
    auto dir = defaultsDir("chrcustmaterial");
    writeFile(dir / "chrcustmaterial.m2", tinyValidM2());
    writeFile(dir / "chrcustmaterial00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcustmaterial.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    fs::create_directories(dbdDir / "definitions");

    writeTextFile(dbdDir / "manifest.json",
                  "[\n"
                  "  {\"tableName\": \"ChrCustomizationElement\", \"tableHash\": \"c0000001\"},\n"
                  "  {\"tableName\": \"ChrCustomizationMaterial\", \"tableHash\": \"c0000004\"},\n"
                  "  {\"tableName\": \"TextureFileData\", \"tableHash\": \"c0000005\"},\n"
                  "  {\"tableName\": \"ChrModelMaterial\", \"tableHash\": \"c0000006\"},\n"
                  "  {\"tableName\": \"CharComponentTextureLayouts\", \"tableHash\": \"c0000007\"},\n"
                  "  {\"tableName\": \"ChrModelTextureLayer\", \"tableHash\": \"c0000008\"}\n"
                  "]\n");
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationElement.dbd",
                  "COLUMNS\nint ID\nint ChrCustomizationChoiceID\n"
                  "int<ChrCustomizationGeoset::ID> ChrCustomizationGeosetID\n"
                  "int<ChrCustomizationBoneSet::ID> ChrCustomizationBoneSetID\n"
                  "int<ChrCustomizationMaterial::ID> ChrCustomizationMaterialID\n\n"
                  "LAYOUT d0000001\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrCustomizationChoiceID<32>\nChrCustomizationGeosetID<32>\n"
                  "ChrCustomizationBoneSetID<32>\nChrCustomizationMaterialID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationMaterial.dbd",
                  "COLUMNS\nint ID\nint ChrModelTextureTargetID\n"
                  "int<TextureFileData::MaterialResourcesID> MaterialResourcesID\n\n"
                  "LAYOUT d0000004\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrModelTextureTargetID<32>\nMaterialResourcesID<32>\n");
    writeTextFile(dbdDir / "definitions" / "TextureFileData.dbd",
                  "COLUMNS\nint<FileData::ID> FileDataID\nint MaterialResourcesID\nint UsageType\n\n"
                  "LAYOUT d0000005\nBUILD 1.0.0.1\n"
                  "$id$FileDataID<32>\nMaterialResourcesID<32>\nUsageType<32>\n");
    writeTextFile(dbdDir / "definitions" / "ChrModelMaterial.dbd",
                  "COLUMNS\nint ID\n"
                  "int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID\n"
                  "int TextureType\nint Width\nint Height\nint Flags\n\n"
                  "LAYOUT d0000006\nBUILD 1.0.0.1\n$id$ID<32>\n"
                  "CharComponentTextureLayoutsID<32>\nTextureType<32>\nWidth<32>\nHeight<32>\nFlags<32>\n");
    writeTextFile(dbdDir / "definitions" / "CharComponentTextureLayouts.dbd",
                  "COLUMNS\nint ID\nint Width\nint Height\n\n"
                  "LAYOUT d0000007\nBUILD 1.0.0.1\n$id$ID<32>\nWidth<32>\nHeight<32>\n");
    writeTextFile(dbdDir / "definitions" / "ChrModelTextureLayer.dbd",
                  "COLUMNS\nint ID\n"
                  "int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID\n"
                  "int TextureType\nint Layer\nint Flags\nint BlendMode\n"
                  "int TextureSectionTypeBitMask\nint ChrModelTextureTargetID\n\n"
                  "LAYOUT d0000008\nBUILD 1.0.0.1\n$id$ID<32>\n"
                  "CharComponentTextureLayoutsID<32>\nTextureType<32>\nLayer<32>\nFlags<32>\n"
                  "BlendMode<32>\nTextureSectionTypeBitMask<32>\nChrModelTextureTargetID<32>\n");

    // Choice 99 owns one Element row carrying ChrCustomizationMaterialID 5
    // (no geoset/boneset -- this test is only about the material chain).
    writeFile(db2Dir / "chrcustomizationelement.db2",
              buildFlatDb2(0xc0000001, 0xd0000001, {{1, 99, 0, 0, 5}}));
    // Material 5 targets ChrModelTextureTargetID 7, real MaterialResourcesID 777.
    writeFile(db2Dir / "chrcustomizationmaterial.db2",
              buildFlatDb2(0xc0000004, 0xd0000004, {{5, 7, 777}}));
    // TextureFileData: MaterialResourcesID 777 -> real FileDataID 888, UsageType 0
    // (the base-skin row this reader keeps -- see texturefiledata_db2.hpp).
    writeFile(db2Dir / "texturefiledata.db2", buildFlatDb2(0xc0000005, 0xd0000005, {{888, 777, 0}}));
    // Stage 2: layout 42, one TextureType-1 base atlas.
    writeFile(db2Dir / "chrmodelmaterial.db2", buildFlatDb2(0xc0000006, 0xd0000006, {{1, 42, 1, 4, 4, 0}}));
    writeFile(db2Dir / "charcomponenttexturelayouts.db2",
              buildFlatDb2(0xc0000007, 0xd0000007, {{42, 4, 4}}));
    // ChrModelTextureLayer: layout 42, ChrModelTextureTargetID 7 (matching
    // ChrCustomizationMaterial's own target above) -- the real join key a
    // downstream node-graph consumer needs.
    writeFile(db2Dir / "chrmodeltexturelayer.db2",
              buildFlatDb2(0xc0000008, 0xd0000008, {{1, 42, 1, 0, 0, 1, 0b1, 7}}));

    auto result = runHusk("export " + (dir / "chrcustmaterial.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --char-layout-id 42 "
                           "--customization-choice-ids 99");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 real material selection(s)") != std::string::npos);

    fs::path glbPath = dir / "chrcustmaterial.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("chr_enabled_materials") != std::string::npos);
    CHECK(bytes.find("\"choice_id\":99") != std::string::npos);
    CHECK(bytes.find("\"chr_model_texture_target_id\":7") != std::string::npos);
    CHECK(bytes.find("\"material_resources_id\":777") != std::string::npos);
    CHECK(bytes.find("\"file_data_id\":888") != std::string::npos);
    // No compositing extras -- husk stops at real data exposure.
    CHECK(bytes.find("chr_composited_textures") == std::string::npos);

    fs::remove_all(dir);
}
