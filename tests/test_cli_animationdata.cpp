// CLI tier: `husk export --db2-dir/--dbd-dir` attaches a real, human-
// readable AnimationData.db2 name ("Stand", ...) to a matching animation
// clip's own `sequence_metadata.animation_data_name` extra -- exercises the
// real compiled binary (see run_husk.hpp) against a small, synthetic,
// on-disk WoWDBDefs/.db2 fixture, same convention as
// tests/test_cli_chrmodel.cpp/test_cli_chrcustomization.cpp.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
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

}  // namespace

TEST_CASE("husk export --db2-dir/--dbd-dir attaches a real AnimationData.db2 name to a matching "
          "sequence's sequence_metadata extras, end to end") {
    auto dir = defaultsDir("animdataname");
    writeFile(dir / "animdataname.m2", tinyAnimatedM2());  // 1 inline sequence, id=100
    writeFile(dir / "animdataname00.skin", tinyMatchingSkin());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    fs::create_directories(dbdDir / "definitions");

    const uint32_t kAnimHash = 0x61616161;
    const uint32_t kAnimLayoutHash = 0x62626262;

    writeTextFile(dbdDir / "manifest.json",
                  "[\n  {\"tableName\": \"AnimationData\", \"tableHash\": \"61616161\"}\n]\n");
    writeTextFile(dbdDir / "definitions" / "AnimationData.dbd",
                  "COLUMNS\nint ID\nstring Name\n\n"
                  "LAYOUT 62626262\nBUILD 1.0.0.1\n$id$ID<32>\nName\n");

    // Sequence 100 (tinyAnimatedM2's own id) gets a real name; sequence 999
    // is unrelated noise, proving lookup is by id, not by table position.
    writeFile(db2Dir / "animationdata.db2",
              buildAnimationDataDb2(kAnimHash, kAnimLayoutHash, {{100, "Stand"}, {999, "Walk"}}));

    auto result = runHusk("export " + (dir / "animdataname.m2").string() + " --skin " +
                           (dir / "animdataname00.skin").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::path glbPath = dir / "animdataname.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("\"animation_data_name\":\"Stand\"") != std::string::npos);
    CHECK(bytes.find("\"anim_100_0\"") != std::string::npos);  // stable machine name unchanged
    CHECK(bytes.find("Walk") == std::string::npos);  // id 999 never matched, never attached

    fs::remove_all(dir);
}

TEST_CASE("husk export with no --db2-dir/--dbd-dir: sequence_metadata has no "
          "animation_data_name at all (not an empty string)") {
    auto dir = defaultsDir("animdatanamenodb2");
    writeFile(dir / "animdatanamenodb2.m2", tinyAnimatedM2());
    writeFile(dir / "animdatanamenodb200.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "animdatanamenodb2.m2").string() + " --skin " +
                           (dir / "animdatanamenodb200.skin").string());
    CHECK(result.exitCode == 0);

    fs::path glbPath = dir / "animdatanamenodb2.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("animation_data_name") == std::string::npos);

    fs::remove_all(dir);
}
