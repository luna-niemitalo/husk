// CLI tier: `husk export --db2-dir/--dbd-dir/--customization-choice-ids` --
// exercises the real compiled binary (see run_husk.hpp) against small,
// synthetic, on-disk M2/.skin/.skel fixtures plus a small synthetic
// WoWDBDefs/.db2 fixture set, verifying the real `enabled_geosets`/
// `selected_by_choice_ids` glTF extras land in the actual output .glb.
// TODO/TODO_correctness.md #2, src/chrcustomization_db2.hpp. The
// `--chr-model-id auto` derivation tests (lowest-OrderIndex default-choice
// selection, the filename race+sex fallback, the primary --listfile
// FileDataID chain, and the `none` opt-out) moved to
// tests/test_cli_chrmodel_id.cpp (FILE_SPLIT_TODO.md Item 5) -- a different
// flag, same "different flag, different file" rule
// tests/test_cli_chrmodel.cpp already exists for --char-layout-id.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <tiny_gltf.h>
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

// One row for buildOptionOrChoiceDb2: ID, a foreign key (ChrModelID for
// Option, ChrCustomizationOptionID for Choice), OrderIndex, and a real
// Name_lang string -- same shape/builder as tests/test_cli_chrmodel_id.cpp's
// own copy (per-file-fixture convention, see that file's own doc comment),
// needed here (unlike this file's other tests) to give a real
// ChrCustomizationOption/Choice a real human name for the material-naming
// test below.
struct NamedRow {
    uint32_t id;
    uint32_t fk;
    uint32_t orderIndex;
    std::string name;
};

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

// One row for buildOptionWithCategoryDb2 -- ChrCustomizationOption with a
// real ChrCustomizationCategoryID column, which buildOptionOrChoiceDb2's
// own fixed 4-field shape (id, fk, orderIndex, name) has no room for. A
// separate, purpose-built builder rather than widening
// buildOptionOrChoiceDb2 itself -- that one's two existing call sites use
// their own already-passing bespoke DBD text this session deliberately
// doesn't touch (see the module-level "no extensive alteration beyond the
// task's own scope" convention).
struct OptionWithCategoryRow {
    uint32_t id;
    uint32_t chrModelId;
    uint32_t orderIndex;
    uint32_t categoryId;
    std::string name;
};

std::vector<uint8_t> buildOptionWithCategoryDb2(uint32_t tableHash, uint32_t layoutHash,
                                                 const std::vector<OptionWithCategoryRow>& rows) {
    constexpr uint32_t fieldCount = 5;
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
        putU32(buf, recordPos + 4, rows[i].chrModelId);
        putU32(buf, recordPos + 8, rows[i].orderIndex);
        putU32(buf, recordPos + 12, rows[i].categoryId);
        size_t stringAbsPos = recordDataEnd + nameOffsetInTable[i];
        size_t fieldAbsPos = recordPos + 16;
        putU32(buf, recordPos + 16, static_cast<uint32_t>(stringAbsPos - fieldAbsPos));
    }
    p = recordDataEnd;
    std::memcpy(buf.data() + p, stringTable.data(), stringTable.size());
    p += stringTable.size();
    REQUIRE(p == total);
    return buf;
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
    CHECK(result.output.find("--db2-dir/--dbd-dir are required") != std::string::npos);

    fs::path glbPath = dir / "chrcustchoicepartial.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("enabled_geosets") == std::string::npos);

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

TEST_CASE("husk export --customization-choice-ids: a material conditional on another real "
          "ChrCustomizationChoiceID (RelatedChrCustomizationChoiceID) is only attached when that "
          "other choice is also part of this export's own selection -- real data found this "
          "session: a real 'Tiara' Hairstyle choice carries one dedicated material per real Hair "
          "Color choice, not one unconditional material; blending all of them in indiscriminately "
          "(this test's own regression) was the real bug found in Blender") {
    auto dir = defaultsDir("chrcustrelated");
    writeFile(dir / "chrcustrelated.m2", tinyValidM2());
    writeFile(dir / "chrcustrelated00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcustrelated.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    fs::create_directories(dbdDir / "definitions");

    writeTextFile(dbdDir / "manifest.json",
                  "[\n"
                  "  {\"tableName\": \"ChrCustomizationElement\", \"tableHash\": \"c0000001\"},\n"
                  "  {\"tableName\": \"ChrCustomizationMaterial\", \"tableHash\": \"c0000004\"},\n"
                  "  {\"tableName\": \"TextureFileData\", \"tableHash\": \"c0000005\"}\n"
                  "]\n");
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationElement.dbd",
                  "COLUMNS\nint ID\nint ChrCustomizationChoiceID\n"
                  "int<ChrCustomizationGeoset::ID> ChrCustomizationGeosetID\n"
                  "int<ChrCustomizationBoneSet::ID> ChrCustomizationBoneSetID\n"
                  "int<ChrCustomizationMaterial::ID> ChrCustomizationMaterialID\n"
                  "int<ChrCustomizationChoice::ID> RelatedChrCustomizationChoiceID\n\n"
                  "LAYOUT d0000001\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrCustomizationChoiceID<32>\nChrCustomizationGeosetID<32>\n"
                  "ChrCustomizationBoneSetID<32>\nChrCustomizationMaterialID<32>\n"
                  "RelatedChrCustomizationChoiceID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationMaterial.dbd",
                  "COLUMNS\nint ID\nint ChrModelTextureTargetID\n"
                  "int<TextureFileData::MaterialResourcesID> MaterialResourcesID\n\n"
                  "LAYOUT d0000004\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrModelTextureTargetID<32>\nMaterialResourcesID<32>\n");
    writeTextFile(dbdDir / "definitions" / "TextureFileData.dbd",
                  "COLUMNS\nint<FileData::ID> FileDataID\nint MaterialResourcesID\nint UsageType\n\n"
                  "LAYOUT d0000005\nBUILD 1.0.0.1\n"
                  "$id$FileDataID<32>\nMaterialResourcesID<32>\nUsageType<32>\n");

    // Choice 99 ("Tiara") owns two Element rows: material 5 (unconditional,
    // RelatedChrCustomizationChoiceID 0) and material 6 (only applies
    // together with choice 200, a real Hair Color choice -- real shape,
    // not invented, see real choice 6639's own 10 such rows found this
    // session).
    writeFile(db2Dir / "chrcustomizationelement.db2",
              buildFlatDb2(0xc0000001, 0xd0000001, {{1, 99, 0, 0, 5, 0}, {2, 99, 0, 0, 6, 200}}));
    writeFile(db2Dir / "chrcustomizationmaterial.db2",
              buildFlatDb2(0xc0000004, 0xd0000004, {{5, 7, 777}, {6, 8, 778}}));
    writeFile(db2Dir / "texturefiledata.db2",
              buildFlatDb2(0xc0000005, 0xd0000005, {{888, 777, 0}, {889, 778, 0}}));

    // Choice 200 is not selected -- only the unconditional material (5,
    // FileDataID 888) should attach; the conditional one (6, FileDataID
    // 889) must be skipped, not blended in.
    auto onlyTiara = runHusk("export " + (dir / "chrcustrelated.m2").string() + " --db2-dir " + db2Dir.string() +
                              " --dbd-dir " + dbdDir.string() + " --customization-choice-ids 99");
    CHECK(onlyTiara.exitCode == 0);
    CHECK(onlyTiara.output.find("1 real material selection(s)") != std::string::npos);
    CHECK(onlyTiara.output.find("1 conditional material(s) skipped") != std::string::npos);

    fs::path glbPath1 = dir / "chrcustrelated.glb";
    REQUIRE(fs::exists(glbPath1));
    std::ifstream glb1(glbPath1, std::ios::binary);
    std::string bytes1((std::istreambuf_iterator<char>(glb1)), std::istreambuf_iterator<char>());
    CHECK(bytes1.find("\"file_data_id\":888") != std::string::npos);
    CHECK(bytes1.find("\"file_data_id\":889") == std::string::npos);
    fs::remove(glbPath1);

    // Both choices selected -- now the conditional material (889) is
    // real and correct to attach too, alongside the unconditional one.
    auto both = runHusk("export " + (dir / "chrcustrelated.m2").string() + " --db2-dir " + db2Dir.string() +
                         " --dbd-dir " + dbdDir.string() + " --customization-choice-ids 99,200");
    CHECK(both.exitCode == 0);
    CHECK(both.output.find("2 real material selection(s)") != std::string::npos);
    CHECK(both.output.find("conditional material(s) skipped") == std::string::npos);

    fs::path glbPath2 = dir / "chrcustrelated.glb";
    REQUIRE(fs::exists(glbPath2));
    std::ifstream glb2(glbPath2, std::ios::binary);
    std::string bytes2((std::istreambuf_iterator<char>(glb2)), std::istreambuf_iterator<char>());
    CHECK(bytes2.find("\"file_data_id\":888") != std::string::npos);
    CHECK(bytes2.find("\"file_data_id\":889") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a material whose resolved FileDataID cross-references a real "
          "ChrCustomizationOption/Choice gets that choice's own human name as its display `name` "
          "(not the verbose diagnostic chain, which moves to `diagnostic_name` extras instead), and "
          "this doesn't regress dedup -- two batches resolving to the exact same texture still merge "
          "into one glTF material despite the new name-priority logic touching this same code path "
          "(materialDedupKey itself never reads gm.name -- this is the regression check for that).") {
    auto dir = defaultsDir("chrcust-material-name");
    writeFile(dir / "custname.m2", oneTexturedModel(888));
    writeFile(dir / "custname00.skin", twoBatchesSameComboSkin());
    // attachCustomizationChoices only runs alongside a real skeleton
    // (cmd_export.cpp's `if (!bones.empty())` block) -- a real bone needs
    // to exist for this test's --chr-model-id/customization-menu wiring to
    // exercise at all, same auto-detected external .skel other
    // customization-choice tests in this file use.
    writeFile(dir / "custname.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    fs::create_directories(dbdDir / "definitions");

    writeTextFile(dbdDir / "manifest.json",
                  "[\n"
                  "  {\"tableName\": \"ChrCustomizationElement\", \"tableHash\": \"c0000001\"},\n"
                  "  {\"tableName\": \"ChrCustomizationMaterial\", \"tableHash\": \"c0000004\"},\n"
                  "  {\"tableName\": \"TextureFileData\", \"tableHash\": \"c0000005\"},\n"
                  "  {\"tableName\": \"ChrCustomizationOption\", \"tableHash\": \"64646464\"},\n"
                  "  {\"tableName\": \"ChrCustomizationChoice\", \"tableHash\": \"65656565\"}\n"
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
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationOption.dbd",
                  "COLUMNS\nint ID\nint ChrModelID\nint OrderIndex\nlocstring Name_lang\n\n"
                  "LAYOUT a4a4a4a4\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrModelID<32>\nOrderIndex<32>\nName_lang<32>\n");
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationChoice.dbd",
                  "COLUMNS\nint ID\nint ChrCustomizationOptionID\nint OrderIndex\nlocstring Name_lang\n\n"
                  "LAYOUT a5a5a5a5\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrCustomizationOptionID<32>\nOrderIndex<32>\nName_lang<32>\n");

    // Choice 99 (option 10, "Hair Color") owns one Element row carrying
    // ChrCustomizationMaterialID 5.
    writeFile(db2Dir / "chrcustomizationelement.db2",
              buildFlatDb2(0xc0000001, 0xd0000001, {{1, 99, 0, 0, 5}}));
    // Material 5 targets ChrModelTextureTargetID 7, real MaterialResourcesID 777.
    writeFile(db2Dir / "chrcustomizationmaterial.db2",
              buildFlatDb2(0xc0000004, 0xd0000004, {{5, 7, 777}}));
    // TextureFileData: MaterialResourcesID 777 -> real FileDataID 888 -- the
    // exact same FileDataID this test's own oneTexturedModel(888) uses, so
    // the M2's own resolved baseColorTextureFileDataId cross-references
    // this real customization choice.
    writeFile(db2Dir / "texturefiledata.db2", buildFlatDb2(0xc0000005, 0xd0000005, {{888, 777, 0}}));
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionOrChoiceDb2(0x64646464, 0xa4a4a4a4, {{10, 42, 0, "Hair Color"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0x65656565, 0xa5a5a5a5, {{99, 10, 0, "Blonde 01"}}));

    auto result = runHusk("export " + (dir / "custname.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id 42");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    fs::path glbPath = dir / "custname.glb";
    REQUIRE(fs::exists(glbPath));
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, glbPath.string()));

    // One merged material (not two), despite twoBatchesSameComboSkin()'s
    // two separate batches -- both resolve the exact same FileDataID, so
    // dedup must still collapse them.
    REQUIRE(model.materials.size() == 1);
    // The real customization choice's own name, not husk's verbose
    // diagnostic chain.
    CHECK(model.materials[0].name == "Hair Color: Blonde 01");
    REQUIRE(model.materials[0].extras.Has("diagnostic_name"));
    CHECK(model.materials[0].extras.Get("diagnostic_name").Get<std::string>().find("fdid888") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id: chr_customization_options extras carry a real "
          "ChrCustomizationCategory grouping (category_id/category_name/category_order_index) for "
          "an option whose ChrCustomizationCategoryID resolves, and carry none at all -- no "
          "fabricated grouping -- for an option whose ChrCustomizationCategoryID is 0") {
    auto dir = defaultsDir("chrcustcategory");
    writeFile(dir / "chrcustcategory.m2", tinyValidM2());
    writeFile(dir / "chrcustcategory00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcustcategory.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    fs::create_directories(dbdDir / "definitions");

    writeTextFile(dbdDir / "manifest.json",
                  "[\n"
                  "  {\"tableName\": \"ChrCustomizationOption\", \"tableHash\": \"c1000001\"},\n"
                  "  {\"tableName\": \"ChrCustomizationChoice\", \"tableHash\": \"c1000002\"},\n"
                  "  {\"tableName\": \"ChrCustomizationCategory\", \"tableHash\": \"c1000003\"}\n"
                  "]\n");
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationOption.dbd",
                  "COLUMNS\nint ID\nint ChrModelID\nint OrderIndex\n"
                  "int<ChrCustomizationCategory::ID> ChrCustomizationCategoryID\nlocstring Name_lang\n\n"
                  "LAYOUT d1000001\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrModelID<32>\nOrderIndex<32>\nChrCustomizationCategoryID<32>\nName_lang<32>\n");
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationChoice.dbd",
                  "COLUMNS\nint ID\nint ChrCustomizationOptionID\nint OrderIndex\nlocstring Name_lang\n\n"
                  "LAYOUT d1000002\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nChrCustomizationOptionID<32>\nOrderIndex<32>\nName_lang<32>\n");
    // Unused (4th) int column kept only so this reuses buildOptionOrChoiceDb2's
    // existing 4-field (id, fk, orderIndex, name) shape unchanged -- real
    // ChrCustomizationCategory has no such field, this is a synthetic-fixture
    // convenience, not a claim about the real table's layout (see
    // ChrCustomizationCategory.dbd's own real COLUMNS for the true shape).
    writeTextFile(dbdDir / "definitions" / "ChrCustomizationCategory.dbd",
                  "COLUMNS\nint ID\nint Unused\nint OrderIndex\nlocstring CategoryName_lang\n\n"
                  "LAYOUT d1000003\nBUILD 1.0.0.1\n"
                  "$id$ID<32>\nUnused<32>\nOrderIndex<32>\nCategoryName_lang<32>\n");

    // Real local data this mirrors (ChrModelID 20, verified via
    // `husk db2-export` + sqlite3 against test_data/db2/chrcustomization{option,category}.db2
    // before writing this synthetic fixture): options 119/122 group under
    // category 2 ("Face"), option 583 has no category at all (categoryId 0).
    writeFile(db2Dir / "chrcustomizationoption.db2",
              buildOptionWithCategoryDb2(0xc1000001, 0xd1000001,
                                          {{119, 42, 0, 2, "Skin Color"},
                                           {122, 42, 1, 2, "Hair Color"},
                                           {583, 42, 2, 0, "Ears"}}));
    writeFile(db2Dir / "chrcustomizationchoice.db2",
              buildOptionOrChoiceDb2(0xc1000002, 0xd1000002,
                                      {{1190, 119, 0, "Pale"}, {1220, 122, 0, "Black"}, {5830, 583, 0, "Round"}}));
    writeFile(db2Dir / "chrcustomizationcategory.db2",
              buildOptionOrChoiceDb2(0xc1000003, 0xd1000003, {{2, 0, 1, "Face"}}));

    auto result = runHusk("export " + (dir / "chrcustcategory.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --chr-model-id 42");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    fs::path glbPath = dir / "chrcustcategory.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());

    // "Skin Color"/"Hair Color" (option 119/122) both carry the real "Face"
    // category grouping.
    CHECK(bytes.find("\"option_name\":\"Skin Color\"") != std::string::npos);
    CHECK(bytes.find("\"category_id\":2") != std::string::npos);
    CHECK(bytes.find("\"category_name\":\"Face\"") != std::string::npos);
    CHECK(bytes.find("\"category_order_index\":1") != std::string::npos);

    CHECK(bytes.find("\"option_id\":583") != std::string::npos);  // "Ears" is really present

    // "Ears" (option 583) has ChrCustomizationCategoryID 0 -- no category
    // key at all should be attached for it, not a fabricated/zeroed one.
    // Counted rather than isolated by substring position -- tinygltf's own
    // Value::Object serializes keys alphabetically, not insertion order, so
    // "choices" can land *before* "option_id" within the very same object
    // once there's no category_id/category_name/category_order_index block
    // ahead of it (confirmed directly: this broke an earlier version of
    // this check that assumed "choices" always comes after "option_id").
    // Exactly 2 real category attachments exist (options 119/122) -- if
    // Ears got a fabricated third, this count would catch it regardless of
    // where in the file it landed.
    auto countOccurrences = [&](const std::string& needle) {
        size_t count = 0, pos = 0;
        while ((pos = bytes.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.size();
        }
        return count;
    };
    CHECK(countOccurrences("\"category_id\"") == 2);
    CHECK(countOccurrences("\"category_name\"") == 2);

    fs::remove_all(dir);
}

