// CLI tier: `husk export --db2-dir/--dbd-dir/--char-layout-id` -- exercises
// the real compiled binary (see run_husk.hpp) against small, synthetic,
// on-disk M2/.skin/.skel fixtures plus a small synthetic WoWDBDefs/.db2
// fixture set, verifying the real `chr_texture_layout` glTF extras land in
// the actual output .glb, not just that the loader functions return the
// right in-memory values (already covered by tests/test_db2table.cpp's
// unit tests).

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
// tests/test_cli_db2.cpp's own buildDb2WithFields, kept as a local copy per
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

}  // namespace

TEST_CASE("husk export --db2-dir/--dbd-dir/--char-layout-id attaches real chr_texture_layout "
          "extras to the output .glb, end to end") {
    auto dir = defaultsDir("chrmodel");
    writeFile(dir / "chrmodel.m2", tinyValidM2());
    writeFile(dir / "chrmodel00.skin", tinyMatchingSkin());
    writeFile(dir / "chrmodel.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    fs::create_directories(dbdDir / "definitions");

    const uint32_t kMaterialHash = 0x51515151;
    const uint32_t kMaterialLayoutHash = 0x52525252;
    const uint32_t kLayoutsHash = 0x53535353;
    const uint32_t kLayoutsLayoutHash = 0x54545454;

    writeTextFile(dbdDir / "manifest.json",
                  "[\n"
                  "  {\"tableName\": \"ChrModelMaterial\", \"tableHash\": \"51515151\"},\n"
                  "  {\"tableName\": \"CharComponentTextureLayouts\", \"tableHash\": \"53535353\"}\n"
                  "]\n");
    writeTextFile(dbdDir / "definitions" / "ChrModelMaterial.dbd",
                  "COLUMNS\n"
                  "int ID\n"
                  "int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID\n"
                  "int TextureType\nint Width\nint Height\nint Flags\n\n"
                  "LAYOUT 52525252\nBUILD 1.0.0.1\n$id$ID<32>\n"
                  "CharComponentTextureLayoutsID<32>\nTextureType<32>\nWidth<32>\nHeight<32>\nFlags<32>\n");
    writeTextFile(dbdDir / "definitions" / "CharComponentTextureLayouts.dbd",
                  "COLUMNS\nint ID\nint Width\nint Height\n\n"
                  "LAYOUT 54545454\nBUILD 1.0.0.1\n$id$ID<32>\nWidth<32>\nHeight<32>\n");

    writeFile(db2Dir / "chrmodelmaterial.db2",
              buildFlatDb2(kMaterialHash, kMaterialLayoutHash, {{1, 42, 1, 1024, 512, 0}}));
    writeFile(db2Dir / "charcomponenttexturelayouts.db2",
              buildFlatDb2(kLayoutsHash, kLayoutsLayoutHash, {{42, 2048, 1024}}));

    auto result = runHusk("export " + (dir / "chrmodel.m2").string() + " --db2-dir " + db2Dir.string() +
                           " --dbd-dir " + dbdDir.string() + " --char-layout-id 42");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached character texture-layout 42") != std::string::npos);
    CHECK(result.output.find("1 material(s)") != std::string::npos);

    fs::path glbPath = dir / "chrmodel.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("chr_texture_layout") != std::string::npos);
    CHECK(bytes.find("\"layout_id\":42") != std::string::npos);
    CHECK(bytes.find("\"width\":2048") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --db2-dir given without --dbd-dir/--char-layout-id skips the feature, "
          "doesn't fail the export") {
    auto dir = defaultsDir("chrmodelpartial");
    writeFile(dir / "chrmodelpartial.m2", tinyValidM2());
    writeFile(dir / "chrmodelpartial00.skin", tinyMatchingSkin());
    writeFile(dir / "chrmodelpartial.skel", boneCorrectionSkel());
    fs::path db2Dir = dir / "db2";
    fs::create_directories(db2Dir);

    auto result = runHusk("export " + (dir / "chrmodelpartial.m2").string() + " --db2-dir " + db2Dir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("must all be given together") != std::string::npos);

    fs::path glbPath = dir / "chrmodelpartial.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("chr_texture_layout") == std::string::npos);

    fs::remove_all(dir);
}
