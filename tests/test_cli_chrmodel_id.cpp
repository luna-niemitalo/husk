// CLI tier: `husk export --chr-model-id` -- exercises the real compiled
// binary (see run_husk.hpp) against small, synthetic, on-disk M2/.skin/
// .skel fixtures plus a small synthetic WoWDBDefs/.db2 fixture set. Split
// out of tests/test_cli_chrcustomization.cpp (FILE_SPLIT_TODO.md Item 5) --
// covers --chr-model-id's own default-choice auto-selection, the `auto`
// derivation chain (filename race+sex fallback, the primary --listfile
// FileDataID chain, genuine-ambiguity/no-match reporting), and the `none`
// opt-out -- distinct from that file's own --customization-choice-ids scope,
// same "different flag, different file" rule tests/test_cli_chrmodel.cpp
// already exists for --char-layout-id. See src/chrrace_db2.hpp.

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
    // The non-default choice (10, "Long") must never be *selected* into
    // enabled_geosets (its own real object shape, {"choice_id":N,
    // "geoset_id":M} with no other keys between them, tinygltf's own
    // alphabetical-key Object serialization) -- it's fine, and expected,
    // for it to show up in chr_customization_options' full real menu below.
    CHECK(bytes.find("\"choice_id\":10,\"geoset_id\"") == std::string::npos);
    // The full real customization menu is now attached automatically
    // (no separate opt-in flag) whenever a real ChrModelID is known --
    // both options, all three real choices, including the non-default one.
    CHECK(bytes.find("chr_customization_options") != std::string::npos);
    CHECK(bytes.find("\"option_name\":\"Face\"") != std::string::npos);
    CHECK(bytes.find("\"option_name\":\"Hair\"") != std::string::npos);
    CHECK(bytes.find("\"choice_name\":\"Long\"") != std::string::npos);
    CHECK(bytes.find("\"choice_name\":\"Short\"") != std::string::npos);
    CHECK(bytes.find("\"choice_name\":\"Braid\"") != std::string::npos);

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

TEST_CASE("husk export --customization-choice-ids (no --chr-model-id at all): the full real "
          "chr_customization_options menu still gets attached automatically, via the same "
          "best-effort ChrModelID auto-derivation --chr-model-id auto uses -- Luna's own direct "
          "instruction that this must be on by default, not gated behind a second flag") {
    auto dir = defaultsDir("chrcustchoiceenrich");
    writeFile(dir / "dracthyrfemale.m2", tinyValidM2());
    writeFile(dir / "dracthyrfemale00.skin", tinyMatchingSkin());
    writeFile(dir / "dracthyrfemale.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    // Element/geoset/boneset/option/choice/races/raceModels -- no chrModel/
    // creatureDisplay/creatureModel tables, since the filename-fallback
    // derivation path (no --listfile given here) never needs them.
    writeChrCustomizationDbd(dbdDir, 0x11111111, 0x12121212, 0x13131313, 0x14141414, 0x15151515,
                              0x16161616, 0x17171717);

    // Choice 11 is a real geoset pick, "Ears -> Short Fin" (OrderIndex 0,
    // the real default too, but that's incidental to this test).
    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0x11111111, {{1, 11, 6, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0x12121212, {{6, 2, 5}}));  // 205
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0x13131313, {}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0x14141414, {{1, 89, 0, "Ears"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0x15151515, {{11, 1, 0, "Short Fin"}}));
    writeFile(db2Dir / "chrraces.db2", buildChrRacesDb2(0x66666666, 0x16161616, {{52, "Dracthyr"}}));
    writeFile(db2Dir / "chrracexchrmodel.db2",
              buildFlatDb2(0x67676767, 0x17171717, {{1, 52, 1, 89}}));  // race 52, sex 1 (female) -> 89

    // Explicit choice 11 -- deliberately the same as the real default, same
    // as an existing sibling test's own convention: the point here is that
    // the explicit-choice-IDs code path runs (not the --chr-model-id
    // default-derivation path), while chr_customization_options still shows
    // up because ChrModelID gets derived as a best-effort side effect.
    auto result = runHusk("export " + (dir / "dracthyrfemale.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --customization-choice-ids 11");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("auto-selected") == std::string::npos);  // explicit-choice path, not the default one
    CHECK(result.output.find("derived ChrModelID 89 from") != std::string::npos);  // the enrichment attempt
    CHECK(result.output.find("attached the full real customization menu") != std::string::npos);

    fs::path glbPath = dir / "dracthyrfemale.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("chr_customization_options") != std::string::npos);
    CHECK(bytes.find("\"option_name\":\"Ears\"") != std::string::npos);
    CHECK(bytes.find("\"choice_name\":\"Short Fin\"") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir alone (no --customization-choice-ids, no "
          "--chr-model-id at all): the full real chr_customization_options menu still gets "
          "attached automatically -- 'auto' is the real default for --chr-model-id, same "
          "auto|none|<id> convention as --textures/--skin-dir/--skel, not a magic-words flag") {
    auto dir = defaultsDir("chrcustdefaultauto");
    writeFile(dir / "dracthyrfemale.m2", tinyValidM2());
    writeFile(dir / "dracthyrfemale00.skin", tinyMatchingSkin());
    writeFile(dir / "dracthyrfemale.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0x21212121, 0x22222222, 0x23232323, 0x24242424, 0x25252525,
                              0x26262626, 0x27272727);

    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0x21212121, {{1, 11, 6, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0x22222222, {{6, 2, 5}}));
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0x23232323, {}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0x24242424, {{1, 89, 0, "Ears"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0x25252525, {{11, 1, 0, "Short Fin"}}));
    writeFile(db2Dir / "chrraces.db2", buildChrRacesDb2(0x66666666, 0x26262626, {{52, "Dracthyr"}}));
    writeFile(db2Dir / "chrracexchrmodel.db2", buildFlatDb2(0x67676767, 0x27272727, {{1, 52, 1, 89}}));

    // Neither --customization-choice-ids nor --chr-model-id given at all.
    auto result = runHusk("export " + (dir / "dracthyrfemale.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("derived ChrModelID 89 from") != std::string::npos);
    CHECK(result.output.find("attached the full real customization menu") != std::string::npos);

    fs::path glbPath = dir / "dracthyrfemale.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("chr_customization_options") != std::string::npos);
    CHECK(bytes.find("\"choice_name\":\"Short Fin\"") != std::string::npos);
    // Auto-derivation also picks the real default choice, same as
    // --chr-model-id auto explicitly would.
    CHECK(bytes.find("\"geoset_id\":205") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id none: explicitly opts out of ChrModelID derivation, even "
          "though --db2-dir/--dbd-dir are given -- the real opt-out state of the auto|none|<id> "
          "convention") {
    auto dir = defaultsDir("chrmodelidnone");
    writeFile(dir / "dracthyrfemale.m2", tinyValidM2());
    writeFile(dir / "dracthyrfemale00.skin", tinyMatchingSkin());
    writeFile(dir / "dracthyrfemale.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeChrCustomizationDbd(dbdDir, 0x31313131, 0x32323232, 0x33333333, 0x34343434, 0x35353535,
                              0x36363636, 0x37373737);

    writeFile(db2Dir / "chrcustomizationelement.db2", buildFlatDb2(0x61616161, 0x31313131, {{1, 11, 6, 0}}));
    writeFile(db2Dir / "chrcustomizationgeoset.db2", buildFlatDb2(0x62626262, 0x32323232, {{6, 2, 5}}));
    writeFile(db2Dir / "chrcustomizationboneset.db2", buildFlatDb2(0x63636363, 0x33333333, {}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0x34343434, {{1, 89, 0, "Ears"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0x35353535, {{11, 1, 0, "Short Fin"}}));
    writeFile(db2Dir / "chrraces.db2", buildChrRacesDb2(0x66666666, 0x36363636, {{52, "Dracthyr"}}));
    writeFile(db2Dir / "chrracexchrmodel.db2", buildFlatDb2(0x67676767, 0x37373737, {{1, 52, 1, 89}}));

    auto result = runHusk("export " + (dir / "dracthyrfemale.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("derived ChrModelID") == std::string::npos);
    CHECK(result.output.find("attached the full real customization menu") == std::string::npos);

    fs::path glbPath = dir / "dracthyrfemale.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("chr_customization_options") == std::string::npos);
    CHECK(bytes.find("enabled_geosets") == std::string::npos);

    fs::remove_all(dir);
}
