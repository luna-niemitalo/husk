// CLI tier: `husk blp-export` -- exercises the real compiled binary (see
// run_husk.hpp) against a small, synthetic, on-disk BLP2 fixture (same
// hand-built shape tests/test_blp.cpp's own unit tests use, kept local here
// rather than shared, same "CLI-boundary test, not a blp.hpp unit test"
// precedent tests/test_cli_db2.cpp already established).

#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

namespace {

void putU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) {
    std::memcpy(buf.data() + offset, &v, 4);
}

constexpr size_t kHeaderSize = 1172;  // 0x494, husk::blp's own fixed header + palette region

// One 4x4 DXT1 block, solid red, opaque -- same construction as
// tests/test_blp.cpp's own dxt1SolidBlock/buildHeader, minimal subset
// needed here (this is a CLI round-trip test, not exercising decode
// edge cases -- those already live in tests/test_blp.cpp).
std::vector<uint8_t> tinyDxt1Blp() {
    std::vector<uint8_t> buf(kHeaderSize, 0);
    std::memcpy(buf.data(), "BLP2", 4);
    putU32(buf, 0x04, 1);        // version
    buf[0x08] = 2;                // colorEncoding: DXT
    buf[0x09] = 8;                 // alphaBitDepth
    buf[0x0A] = 0;                 // preferredFormat: DXT1
    buf[0x0B] = 1;                 // hasMipmaps
    putU32(buf, 0x0C, 4);          // width
    putU32(buf, 0x10, 4);          // height
    putU32(buf, 0x14, static_cast<uint32_t>(kHeaderSize));  // mipOffsets[0]

    // color0 == color1 (both pure red, rgb565) so index 0 always resolves
    // to color0 regardless of which of DXT1's two interpolation modes the
    // decoder picks -- indices all zero (4 bytes of 0).
    uint16_t red565 = static_cast<uint16_t>(31 << 11);
    std::vector<uint8_t> block;
    block.push_back(static_cast<uint8_t>(red565));
    block.push_back(static_cast<uint8_t>(red565 >> 8));
    block.push_back(static_cast<uint8_t>(red565));
    block.push_back(static_cast<uint8_t>(red565 >> 8));
    block.insert(block.end(), {0, 0, 0, 0});

    buf.insert(buf.end(), block.begin(), block.end());
    return buf;
}

fs::path defaultsDir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("husk-blp-export-test-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool isPng(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G';
}

}  // namespace

TEST_CASE("husk blp-export: single file converts a real BLP2 to a real PNG") {
    auto dir = defaultsDir("single");
    writeFile(dir / "red.blp", tinyDxt1Blp());

    auto result = runHusk("blp-export " + (dir / "red.blp").string() + " " + (dir / "red.png").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("4x4") != std::string::npos);
    REQUIRE(fs::exists(dir / "red.png"));
    CHECK(isPng(dir / "red.png"));

    fs::remove_all(dir);
}

TEST_CASE("husk blp-export --dir: converts every .blp in a directory, mirroring basenames") {
    auto dir = defaultsDir("dirmode");
    writeFile(dir / "a.blp", tinyDxt1Blp());
    writeFile(dir / "b.blp", tinyDxt1Blp());
    writeFile(dir / "not_a_blp.txt", {1, 2, 3});  // must be ignored, not a .blp
    auto outDir = dir / "out";

    auto result = runHusk("blp-export --dir " + dir.string() + " " + outDir.string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2/2 file(s) converted") != std::string::npos);
    REQUIRE(fs::exists(outDir / "a.png"));
    REQUIRE(fs::exists(outDir / "b.png"));
    CHECK(isPng(outDir / "a.png"));
    CHECK(isPng(outDir / "b.png"));
    CHECK_FALSE(fs::exists(outDir / "not_a_blp.png"));

    fs::remove_all(dir);
}

TEST_CASE("husk blp-export --dir: a bad file is skipped with a diagnostic, not fatal to the batch") {
    auto dir = defaultsDir("dirmode-bad");
    writeFile(dir / "good.blp", tinyDxt1Blp());
    writeFile(dir / "bad.blp", {1, 2, 3, 4});  // too short to even hold a magic
    auto outDir = dir / "out";

    auto result = runHusk("blp-export --dir " + dir.string() + " " + outDir.string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1/2 file(s) converted") != std::string::npos);
    CHECK(result.output.find("1 failed") != std::string::npos);
    REQUIRE(fs::exists(outDir / "good.png"));
    CHECK_FALSE(fs::exists(outDir / "bad.png"));

    fs::remove_all(dir);
}

TEST_CASE("husk blp-export: a genuinely bad file (no --dir) exits nonzero") {
    auto dir = defaultsDir("bad-single");
    writeFile(dir / "bad.blp", {1, 2, 3, 4});

    auto result = runHusk("blp-export " + (dir / "bad.blp").string() + " " + (dir / "bad.png").string());
    CHECK(result.exitCode != 0);
    CHECK_FALSE(fs::exists(dir / "bad.png"));

    fs::remove_all(dir);
}
