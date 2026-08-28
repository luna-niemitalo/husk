// Tests for src/sources/texture_catalog.hpp/.cpp -- AUDIT.md §1.1's tier 1
// (literal <texturesDir>/<FileDataID>.{png,blp}) wired through Resolved<T>.
// Delegates entirely to husk::commands::resolveTextureBytes (unchanged);
// this only covers the new tier/reason reporting this wrapper adds.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "../src/sources/texture_catalog.hpp"

using husk::sources::resolveClaimedFuzzyPoolTextureBytes;
using husk::sources::resolveLiteralTextureBytes;
using husk::sources::resolveListfileTextureBytes;
using husk::sources::ResolutionTier;

namespace {

namespace fs = std::filesystem;

// A minimal, real 8-byte PNG signature is enough here: readTextureFileBytes
// (export_texture_resolution.cpp) reads a .png file's raw bytes verbatim,
// with no decode/validation for that extension -- only .blp goes through
// blp::decode. So any non-empty file with a .png extension is a genuine hit.
void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST_CASE("sources::resolveLiteralTextureBytes hits when <texturesDir>/<fdid>.png exists") {
    auto dir = fs::temp_directory_path() / "husk-texture-catalog-test-hit";
    fs::create_directories(dir);
    std::vector<uint8_t> pngBytes = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xAB};
    writeFile(dir / "424242.png", pngBytes);

    auto r = resolveLiteralTextureBytes(424242, dir.string(), "");
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::Literal);
    CHECK(*r.value == pngBytes);
    CHECK(r.reason.find("424242") != std::string::npos);
}

TEST_CASE("sources::resolveLiteralTextureBytes misses (with a reason) when neither .png nor .blp exists") {
    auto dir = fs::temp_directory_path() / "husk-texture-catalog-test-miss";
    fs::create_directories(dir);

    auto r = resolveLiteralTextureBytes(999999, dir.string(), "");
    CHECK_FALSE(r.found());
    CHECK(r.tier == ResolutionTier::Literal);
    CHECK_FALSE(r.reason.empty());
}

TEST_CASE("sources::resolveLiteralTextureBytes misses when texturesDir is empty") {
    auto r = resolveLiteralTextureBytes(1, "", "");
    CHECK_FALSE(r.found());
    CHECK(r.tier == ResolutionTier::Literal);
    CHECK(r.reason == "texturesDir is empty");
}

TEST_CASE("sources::resolveListfileTextureBytes hits when the listfile names a real, present file") {
    auto root = fs::temp_directory_path() / "husk-texture-catalog-test-listfile-hit";
    fs::create_directories(root / "world" / "goober");
    std::vector<uint8_t> pngBytes = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    writeFile(root / "world" / "goober" / "bubble.png", pngBytes);

    std::unordered_map<uint32_t, std::string> listfile{{555, "world/goober/bubble.blp"}};
    auto r = resolveListfileTextureBytes(555, listfile, root.string(), "");
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::Listfile);
    CHECK(r.value->bytes == pngBytes);
    CHECK(r.value->imageName == "bubble");
}

TEST_CASE("sources::resolveListfileTextureBytes misses (with a reason) when the listfile has no row for the fdid") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.blp"}};
    auto r = resolveListfileTextureBytes(999, listfile, "/tmp/husk-texture-catalog-test-listfile-miss", "");
    CHECK_FALSE(r.found());
    CHECK(r.tier == ResolutionTier::Listfile);
    CHECK(r.reason.find("999") != std::string::npos);
}

TEST_CASE("sources::resolveListfileTextureBytes misses (with a reason) when the listfile row's file doesn't exist") {
    auto root = fs::temp_directory_path() / "husk-texture-catalog-test-listfile-dangling";
    fs::create_directories(root);
    std::unordered_map<uint32_t, std::string> listfile{{7, "world/nope.blp"}};
    auto r = resolveListfileTextureBytes(7, listfile, root.string(), "");
    CHECK_FALSE(r.found());
    CHECK(r.tier == ResolutionTier::Listfile);
    CHECK(r.reason.find("nope") != std::string::npos);
}

TEST_CASE("sources::resolveClaimedFuzzyPoolTextureBytes hits when the claimed path is a real, readable file") {
    auto dir = fs::temp_directory_path() / "husk-texture-catalog-test-fuzzy-hit";
    fs::create_directories(dir);
    std::vector<uint8_t> pngBytes = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xEF};
    auto claimed = dir / "bloodelffemale_skin_color_3500123.png";
    writeFile(claimed, pngBytes);

    auto r = resolveClaimedFuzzyPoolTextureBytes(claimed, dir.string(), "");
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    CHECK(r.value->bytes == pngBytes);
    CHECK(r.value->imageName == "bloodelffemale_skin_color_3500123");
    CHECK(r.value->matchedFilename == "bloodelffemale_skin_color_3500123.png");
}

TEST_CASE("sources::resolveClaimedFuzzyPoolTextureBytes misses (with a reason) when the claimed path can't be read") {
    auto dir = fs::temp_directory_path() / "husk-texture-catalog-test-fuzzy-miss";
    fs::create_directories(dir);
    auto claimed = dir / "does_not_exist.png";  // claimSoleFuzzyTextureCandidate already popped this from the
                                                  // pool by the time this is called -- it just isn't on disk.

    auto r = resolveClaimedFuzzyPoolTextureBytes(claimed, dir.string(), "");
    CHECK_FALSE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    CHECK(r.reason.find("does_not_exist.png") != std::string::npos);
}
