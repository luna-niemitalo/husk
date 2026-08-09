// Tests for the community-listfile.csv-style loader (src/listfile.hpp/.cpp),
// used by `husk export --listfile` as a last-resort FileDataID -> real-name
// fallback (see export_materials.cpp's own fallback tier, tested end to end
// in test_cli.cpp).

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "../src/listfile.hpp"

using namespace husk;

namespace {

std::string writeTempListfile(const std::string& content) {
    std::string path = std::filesystem::temp_directory_path() / "husk_test_listfile.csv";
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

}  // namespace

TEST_CASE("loadListfile: parses well-formed FileDataID;path lines") {
    auto path = writeTempListfile(
        "200859;world/goober/bubble.blp\n"
        "1018799;character/human/male/deathknighteyeglow.blp\n");
    auto result = loadListfile(path);
    REQUIRE(result.size() == 2);
    CHECK(result.at(200859) == "world/goober/bubble.blp");
    CHECK(result.at(1018799) == "character/human/male/deathknighteyeglow.blp");
    std::remove(path.c_str());
}

TEST_CASE("loadListfile: tolerates CRLF line endings") {
    auto path = writeTempListfile("200859;world/goober/bubble.blp\r\n");
    auto result = loadListfile(path);
    REQUIRE(result.size() == 1);
    CHECK(result.at(200859) == "world/goober/bubble.blp");
    std::remove(path.c_str());
}

TEST_CASE("loadListfile: skips malformed lines instead of failing the whole load") {
    auto path = writeTempListfile(
        "200859;world/goober/bubble.blp\n"
        "not-a-number;junk/row.blp\n"
        "\n"
        ";no-id-at-all.blp\n"
        "5210137;\n"  // no path after the ';'
        "1018799;character/human/male/deathknighteyeglow.blp\n");
    auto result = loadListfile(path);
    REQUIRE(result.size() == 2);
    CHECK(result.at(200859) == "world/goober/bubble.blp");
    CHECK(result.at(1018799) == "character/human/male/deathknighteyeglow.blp");
    std::remove(path.c_str());
}

TEST_CASE("loadListfile: a path that can't be opened throws, not a silent empty map") {
    CHECK_THROWS_AS(loadListfile("/nonexistent/path/does_not_exist.csv"), std::runtime_error);
}
