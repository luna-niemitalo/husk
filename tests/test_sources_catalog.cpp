// Tests for src/sources/listfile_catalog.hpp/.cpp -- two slices of
// REFACTOR/RESOURCE_CATALOG.md's FileDataID<->path surface.
// fileDataIdForPath was previously unexercised directly (only reachable
// via CLI-level --chr-model-id auto tests) since it lived as a
// file-private static in export_extras.cpp. pathForFileDataId
// (REFACTOR/AUDIT.md #1.2's forward direction) is new this session,
// consolidated out of export_materials.cpp's texture-tier listfile
// fallback and cmd_export.cpp's exportGearAuxItemModels.

#include <doctest/doctest.h>

#include "../src/sources/listfile_catalog.hpp"

using husk::sources::contentNameForFileDataId;
using husk::sources::fileDataIdForPath;
using husk::sources::pathForFileDataId;

TEST_CASE("sources::fileDataIdForPath matches a real listfile row, case-insensitively") {
    std::unordered_map<uint32_t, std::string> listfile{
        {4395382, "character/dracthyr/dracthyrmale.m2"},
        {123, "world/some/other/path.m2"},
    };
    auto result = fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/Character/Dracthyr/DracthyrMale.m2", "/tmp/husk-catalog-test-root");
    REQUIRE(result.has_value());
    CHECK(*result == 4395382);
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when the listfile is empty") {
    std::unordered_map<uint32_t, std::string> listfile;
    CHECK_FALSE(fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/foo.m2", "/tmp/husk-catalog-test-root").has_value());
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when listfileRoot is empty") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "foo.m2"}};
    CHECK_FALSE(fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/foo.m2", "").has_value());
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when no row matches") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.m2"}};
    CHECK_FALSE(fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/character/nope.m2", "/tmp/husk-catalog-test-root").has_value());
}

TEST_CASE("sources::pathForFileDataId joins listfileRoot with the listfile's own row") {
    std::unordered_map<uint32_t, std::string> listfile{{4395382, "character/dracthyr/dracthyrmale.m2"}};
    auto result = pathForFileDataId(listfile, "/tmp/husk-catalog-test-root", 4395382);
    REQUIRE(result.has_value());
    CHECK(*result == "/tmp/husk-catalog-test-root/character/dracthyr/dracthyrmale.m2");
}

TEST_CASE("sources::pathForFileDataId returns nullopt when the listfile is empty") {
    std::unordered_map<uint32_t, std::string> listfile;
    CHECK_FALSE(pathForFileDataId(listfile, "/tmp/husk-catalog-test-root", 1).has_value());
}

TEST_CASE("sources::pathForFileDataId returns nullopt when the FileDataID has no listfile row") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.m2"}};
    CHECK_FALSE(pathForFileDataId(listfile, "/tmp/husk-catalog-test-root", 999).has_value());
}

TEST_CASE("sources::pathForFileDataId does NOT require listfileRoot non-empty -- an empty root acts "
          "as an identity join, matching export_materials.cpp's original (pre-consolidation) behavior "
          "for a caller that passes --listfile without --listfile-root") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/foo.m2"}};
    auto result = pathForFileDataId(listfile, "", 1);
    REQUIRE(result.has_value());
    CHECK(*result == "world/foo.m2");
}

TEST_CASE("sources::contentNameForFileDataId returns the listfile row's own stem, no root needed") {
    std::unordered_map<uint32_t, std::string> listfile{{555, "world/goober/bubble.blp"}};
    auto result = contentNameForFileDataId(listfile, 555);
    REQUIRE(result.has_value());
    CHECK(*result == "bubble");
}

TEST_CASE("sources::contentNameForFileDataId returns nullopt when the listfile is empty") {
    std::unordered_map<uint32_t, std::string> listfile;
    CHECK_FALSE(contentNameForFileDataId(listfile, 1).has_value());
}

TEST_CASE("sources::contentNameForFileDataId returns nullopt when the FileDataID has no listfile row") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.blp"}};
    CHECK_FALSE(contentNameForFileDataId(listfile, 999).has_value());
}
