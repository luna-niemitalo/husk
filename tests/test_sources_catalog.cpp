// Tests for src/sources/listfile_catalog.hpp/.cpp -- two slices of
// REFACTOR/RESOURCE_CATALOG.md's FileDataID<->path surface.
// fileDataIdForPath was previously unexercised directly (only reachable
// via CLI-level --chr-model-id auto tests) since it lived as a
// file-private static in export_extras.cpp. pathForFileDataId
// (REFACTOR/AUDIT.md #1.2's forward direction) is new this session,
// consolidated out of export_materials.cpp's texture-tier listfile
// fallback and cmd_export.cpp's exportGearAuxItemModels.

#include <doctest/doctest.h>

#include "../src/listfile_index.hpp"
#include "../src/sources/listfile_catalog.hpp"

using husk::MapListfileIndex;
using husk::sources::contentNameForFileDataId;
using husk::sources::fileDataIdForPath;
using husk::sources::pathForFileDataId;

TEST_CASE("sources::fileDataIdForPath matches a real listfile row, case-insensitively") {
    std::unordered_map<uint32_t, std::string> listfile{
        {4395382, "character/dracthyr/dracthyrmale.m2"},
        {123, "world/some/other/path.m2"},
    };
    MapListfileIndex idx(listfile);
    auto result = fileDataIdForPath(idx, "/tmp/husk-catalog-test-root/Character/Dracthyr/DracthyrMale.m2", "/tmp/husk-catalog-test-root");
    REQUIRE(result.has_value());
    CHECK(*result == 4395382);
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when the listfile is empty") {
    std::unordered_map<uint32_t, std::string> listfile;
    MapListfileIndex idx(listfile);
    CHECK_FALSE(fileDataIdForPath(idx, "/tmp/husk-catalog-test-root/foo.m2", "/tmp/husk-catalog-test-root").has_value());
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when listfileRoot is empty") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "foo.m2"}};
    MapListfileIndex idx(listfile);
    CHECK_FALSE(fileDataIdForPath(idx, "/tmp/husk-catalog-test-root/foo.m2", "").has_value());
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when no row matches") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.m2"}};
    MapListfileIndex idx(listfile);
    CHECK_FALSE(fileDataIdForPath(idx, "/tmp/husk-catalog-test-root/character/nope.m2", "/tmp/husk-catalog-test-root").has_value());
}

TEST_CASE("sources::pathForFileDataId joins listfileRoot with the listfile's own row") {
    std::unordered_map<uint32_t, std::string> listfile{{4395382, "character/dracthyr/dracthyrmale.m2"}};
    MapListfileIndex idx(listfile);
    auto result = pathForFileDataId(idx, "/tmp/husk-catalog-test-root", 4395382);
    REQUIRE(result.has_value());
    CHECK(*result == "/tmp/husk-catalog-test-root/character/dracthyr/dracthyrmale.m2");
}

TEST_CASE("sources::pathForFileDataId returns nullopt when the listfile is empty") {
    std::unordered_map<uint32_t, std::string> listfile;
    MapListfileIndex idx(listfile);
    CHECK_FALSE(pathForFileDataId(idx, "/tmp/husk-catalog-test-root", 1).has_value());
}

TEST_CASE("sources::pathForFileDataId returns nullopt when the FileDataID has no listfile row") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.m2"}};
    MapListfileIndex idx(listfile);
    CHECK_FALSE(pathForFileDataId(idx, "/tmp/husk-catalog-test-root", 999).has_value());
}

TEST_CASE("sources::pathForFileDataId does NOT require listfileRoot non-empty -- an empty root acts "
          "as an identity join, matching export_materials.cpp's original (pre-consolidation) behavior "
          "for a caller that passes --listfile without --listfile-root") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/foo.m2"}};
    MapListfileIndex idx(listfile);
    auto result = pathForFileDataId(idx, "", 1);
    REQUIRE(result.has_value());
    CHECK(*result == "world/foo.m2");
}

TEST_CASE("sources::contentNameForFileDataId returns the listfile row's own stem, no root needed") {
    std::unordered_map<uint32_t, std::string> listfile{{555, "world/goober/bubble.blp"}};
    MapListfileIndex idx(listfile);
    auto result = contentNameForFileDataId(idx, 555);
    REQUIRE(result.has_value());
    CHECK(*result == "bubble");
}

TEST_CASE("sources::contentNameForFileDataId returns nullopt when the listfile is empty") {
    std::unordered_map<uint32_t, std::string> listfile;
    MapListfileIndex idx(listfile);
    CHECK_FALSE(contentNameForFileDataId(idx, 1).has_value());
}

TEST_CASE("sources::contentNameForFileDataId returns nullopt when the FileDataID has no listfile row") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.blp"}};
    MapListfileIndex idx(listfile);
    CHECK_FALSE(contentNameForFileDataId(idx, 999).has_value());
}

TEST_CASE("listfileSpelling: lowercase, forward slashes, and the pre-M2 .mdx/.mdl model extensions as .m2") {
    using husk::sources::listfileSpelling;
    CHECK(listfileSpelling("World\\Azeroth\\Elwynn\\Tree01.MDX") == "world/azeroth/elwynn/tree01.m2");
    CHECK(listfileSpelling("World\\Generic\\Rock.mdl") == "world/generic/rock.m2");
    CHECK(listfileSpelling("World\\wmo\\Inn.wmo") == "world/wmo/inn.wmo");
    CHECK(listfileSpelling("tileset/elwynn/grass.blp") == "tileset/elwynn/grass.blp");
    // Only a trailing extension is rewritten.
    CHECK(listfileSpelling("a.mdx/b.blp") == "a.mdx/b.blp");
}

TEST_CASE("fileDataIdsForGamePaths: one pass resolves every game path, keyed by the caller's spelling") {
    std::unordered_map<uint32_t, std::string> rows{
        {10, "world/azeroth/elwynn/tree01.m2"}, {20, "world/wmo/inn.wmo"}, {30, "tileset/elwynn/grass.blp"}};
    MapListfileIndex listfile(rows);
    auto found = husk::sources::fileDataIdsForGamePaths(
        listfile, {"World\\Azeroth\\Elwynn\\Tree01.mdx", "world/azeroth/elwynn/tree01.m2", "World\\wmo\\Inn.wmo",
                   "World\\Missing.mdx"});
    CHECK(found.size() == 3);
    CHECK(found.at("World\\Azeroth\\Elwynn\\Tree01.mdx") == 10);
    CHECK(found.at("world/azeroth/elwynn/tree01.m2") == 10);
    CHECK(found.at("World\\wmo\\Inn.wmo") == 20);
    CHECK(found.count("World\\Missing.mdx") == 0);
    CHECK(husk::sources::fileDataIdsForGamePaths(listfile, {}).empty());
}
