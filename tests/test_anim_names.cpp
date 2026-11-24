// anim_names.hpp: wow.export's AnimMapper.js list parsed on constructed
// text, the checkout's commit read from a constructed .git, and the real
// checkout when reference/wow.export is present.

#include <cstdlib>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "anim_names.hpp"

namespace fs = std::filesystem;
using husk::animnames::checkoutCommit;
using husk::animnames::load;
using husk::animnames::parseAnimMapper;

namespace {

#ifdef HUSK_TEST_WOW_EXPORT_DIR
constexpr const char* kWowExportDirDefault = HUSK_TEST_WOW_EXPORT_DIR;
#else
constexpr const char* kWowExportDirDefault = nullptr;
#endif

// HUSK_TEST_WOW_EXPORT_DIR wins; else the baked reference/wow.export when
// it holds the list; else empty (skip).
std::string testWowExportDir() {
    if (const char* env = std::getenv("HUSK_TEST_WOW_EXPORT_DIR")) return env;
    if (kWowExportDirDefault == nullptr) return {};
    std::error_code ec;
    if (fs::exists(fs::path(kWowExportDirDefault) / std::string(husk::animnames::kFile), ec)) {
        return kWowExportDirDefault;
    }
    return {};
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

constexpr const char* kSmallList =
    "/*! header */\n"
    "const ANIM_NAMES = [\n"
    "\t\"Stand\",\n"
    "\t\"Death\",\r\n"
    "\t\"\",\n"
    "\t\"Stop\"\n"
    "];\n"
    "const get_anim_name = (anim_id) => {};\n";

}  // namespace

TEST_CASE("parseAnimMapper: line position is the id; an empty entry has no name") {
    auto names = parseAnimMapper(kSmallList, "AnimMapper.js");
    CHECK(names.size() == 3);
    CHECK(names.at(0) == "Stand");
    CHECK(names.at(1) == "Death");
    CHECK(names.count(2) == 0);
    CHECK(names.at(3) == "Stop");
}

TEST_CASE("parseAnimMapper: anything but one quoted name per line fails, naming line, expected and actual") {
    std::string bad = "const ANIM_NAMES = [\n\t\"Stand\",\n\tStand2,\n];\n";
    CHECK_THROWS_WITH_AS(parseAnimMapper(bad, "f.js"),
                         "f.js:3: expected one double-quoted name per line, got 'Stand2'", std::runtime_error);
    CHECK_THROWS_WITH_AS(parseAnimMapper("const X = [];\n", "f.js"),
                         doctest::Contains("expected a line 'const ANIM_NAMES = ['"), std::runtime_error);
    CHECK_THROWS_WITH_AS(parseAnimMapper("const ANIM_NAMES = [\n\"Stand\",\n", "f.js"),
                         doctest::Contains("'];' closing the list"), std::runtime_error);
    CHECK_THROWS_WITH_AS(parseAnimMapper("const ANIM_NAMES = [\n];\n", "f.js"),
                         doctest::Contains("at least one name"), std::runtime_error);
    CHECK_THROWS_WITH_AS(parseAnimMapper("const ANIM_NAMES = [\n\"a\\\"b\",\n];\n", "f.js"),
                         doctest::Contains("without quotes or escapes"), std::runtime_error);
}

TEST_CASE("checkoutCommit: HEAD's branch from a loose ref, else packed-refs; detached HEAD as is") {
    fs::path dir = fs::temp_directory_path() / "husk_anim_names_git";
    fs::remove_all(dir);
    const std::string a(40, 'a');
    const std::string b(40, 'b');
    writeText(dir / ".git" / "HEAD", "ref: refs/heads/main\n");
    writeText(dir / ".git" / "packed-refs", "# pack-refs with: peeled\n" + b + " refs/heads/main\n");
    CHECK(checkoutCommit(dir) == b);
    writeText(dir / ".git" / "refs" / "heads" / "main", a + "\n");
    CHECK(checkoutCommit(dir) == a);
    writeText(dir / ".git" / "HEAD", b + "\n");
    CHECK(checkoutCommit(dir) == b);
    CHECK(checkoutCommit(dir / "nowhere").empty());
    fs::remove_all(dir);
}

TEST_CASE("load: a directory without AnimMapper.js fails with the path it expected") {
    fs::path dir = fs::temp_directory_path() / "husk_anim_names_empty";
    fs::create_directories(dir);
    CHECK_THROWS_WITH_AS(load(dir), doctest::Contains("expected a wow.export checkout"), std::runtime_error);
    fs::remove_all(dir);
}

TEST_CASE("load: the real reference/wow.export checkout names the well-known ids") {
    std::string checkout = testWowExportDir();
    if (checkout.empty()) {
        MESSAGE("skipped: no wow.export checkout (HUSK_TEST_WOW_EXPORT_DIR or reference/wow.export)");
        return;
    }
    husk::animnames::Table table = load(checkout);
    CHECK(table.names.size() > 1700);
    CHECK(table.names.at(0) == "Stand");
    CHECK(table.names.at(1) == "Death");
    CHECK(table.names.at(4) == "Walk");
    CHECK(table.names.at(5) == "Run");
    CHECK(table.names.at(41) == "SwimIdle");
    CHECK(table.detail.find("github.com/Kruithne/wow.export src/js/3D/AnimMapper.js at ") == 0);
    CHECK(table.detail.find("unknown commit") == std::string::npos);
}
