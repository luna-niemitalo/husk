// CLI tier: `husk export` names a sequence clip from AnimationData.db2
// ("Stand", ...) when --db2-dir/--dbd-dir are given, and always carries the
// clip's M2Sequence facts -- the real binary against a small synthetic
// WoWDBDefs/.db2 fixture.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_bundle_read.hpp"
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

// Single-section, 2-field ([0]=ID, [1]=a real string field) WDC5 fixture --
// AnimationData.db2's own real (ID, Name) shape, same builder pattern as
// tests/test_cli_chrcustomization.cpp's buildChrRacesDb2, kept as a
// separate per-file copy per this project's own fixture convention.
std::vector<uint8_t> buildAnimationDataDb2(uint32_t tableHash, uint32_t layoutHash,
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

void writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream f(path);
    f << text;
}


// Writes the AnimationData table: sequence 100 (tinyAnimatedM2's id) is
// named; 999 is noise, so a match is by id, not table position.
void writeAnimationDataTables(const fs::path& db2Dir, const fs::path& dbdDir, const char* name100) {
    fs::create_directories(db2Dir);
    fs::create_directories(dbdDir / "definitions");
    writeTextFile(dbdDir / "manifest.json",
                  "[\n  {\"tableName\": \"AnimationData\", \"tableHash\": \"61616161\"}\n]\n");
    writeTextFile(dbdDir / "definitions" / "AnimationData.dbd",
                  "COLUMNS\nint ID\nstring Name\n\n"
                  "LAYOUT 62626262\nBUILD 1.0.0.1\n$id$ID<32>\nName\n");
    writeFile(db2Dir / "animationdata.db2",
              buildAnimationDataDb2(0x61616161, 0x62626262, {{100, name100}, {999, "Walk"}}));
}

const nlohmann::json& onlySequenceClip(const nlohmann::json& manifest) {
    const nlohmann::json& clips = manifest["resources"]["rig"]["animation"];
    REQUIRE(clips.size() == 1);
    REQUIRE(clips[0]["sequence_kind"] == "sequence");
    return clips[0];
}

}  // namespace

TEST_CASE("husk export --db2-dir/--dbd-dir names a sequence clip from AnimationData, by id") {
    auto dir = defaultsDir("animdataname");
    writeFile(dir / "animdataname.m2", tinyAnimatedM2());  // 1 inline sequence, id 100
    writeFile(dir / "animdataname00.skin", tinyMatchingSkin());
    writeAnimationDataTables(dir / "db2", dir / "dbd", "Stand");

    auto result = runHusk("export " + (dir / "animdataname.m2").string() + " --skin " +
                           (dir / "animdataname00.skin").string() + " --db2-dir " + (dir / "db2").string() +
                           " --dbd-dir " + (dir / "dbd").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir / "animdataname.bundle");
    const nlohmann::json& animation = onlySequenceClip(manifest)["animation"];
    CHECK(animation["id"]["table"] == "AnimationData");
    CHECK(animation["id"]["row"] == 100);
    CHECK(animation["name"] == "Stand");
    CHECK(animation["name_source"] == "db2");
    CHECK(readTextFile(dir / "animdataname.bundle" / "manifest.json").find("Walk") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export without --db2-dir/--dbd-dir: the clip keeps its AnimationData id, with no name "
          "(not an empty one)") {
    auto dir = defaultsDir("animdatanamenodb2");
    writeFile(dir / "animdatanamenodb2.m2", tinyAnimatedM2());
    writeFile(dir / "animdatanamenodb200.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "animdatanamenodb2.m2").string() + " --skin " +
                           (dir / "animdatanamenodb200.skin").string() + " --db2-dir none --dbd-dir none");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir / "animdatanamenodb2.bundle");
    const nlohmann::json& animation = onlySequenceClip(manifest)["animation"];
    CHECK(animation["id"]["row"] == 100);
    CHECK_FALSE(animation.contains("name"));
    CHECK_FALSE(animation.contains("name_source"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: a sequence clip carries its M2Sequence facts -- variation, length, flags") {
    auto dir = defaultsDir("animdatafacts");
    writeFile(dir / "animdatafacts.m2", tinyAnimatedM2());  // duration 1000 ms, flags 0x20
    writeFile(dir / "animdatafacts00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "animdatafacts.m2").string() + " --skin " +
                           (dir / "animdatafacts00.skin").string() + " --db2-dir none --dbd-dir none");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    const nlohmann::json clip = onlySequenceClip(parseManifest(dir / "animdatafacts.bundle"));
    CHECK(clip["variation"] == 0);
    CHECK(clip["duration"] == 1.0);
    CHECK(clip["move_speed"] == 0.0);
    CHECK(clip["blend_in"] == 0.0);
    CHECK(clip["source"]["flags"] == "0x20");

    fs::remove_all(dir);
}
