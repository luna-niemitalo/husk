// CLI tier: character texture layouts in `husk export` -- the layout a
// ChrModel names (ChrModel.CharComponentTextureLayoutID) as composite
// material documents, and --char-layout-id/--chr-model-id steering the
// character-texture tier of texture resolution. Synthetic M2/.skin/.skel
// plus DB2/WoWDBDefs fixtures, real compiled binary (run_husk.hpp).

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_db2_fixtures.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;

namespace {

constexpr uint32_t kModelFdid = 9100;

// A skin-textured (type 1) character model with a skeleton (the
// character-texture tier only runs for a rigged model), listed as
// kModelFdid.
fs::path writeCharacterModel(const fs::path& dir) {
    writeFile(dir / "chrmodel.m2", oneTexturedModelWithType(1));
    writeFile(dir / "chrmodel00.skin", oneTexturedModelSkin());
    writeFile(dir / "chrmodel.skel", boneCorrectionSkel());
    std::ofstream(dir / "listfile.csv") << kModelFdid << ";chrmodel.m2\n";
    return dir / "chrmodel.m2";
}

std::string exportArgs(const fs::path& dir, const fs::path& bundle) {
    return "export " + (dir / "chrmodel.m2").string() + " -o " + bundle.string() + " --listfile " +
           (dir / "listfile.csv").string();
}

// Layout 42: one 1024x512 skin canvas drawn by one layer from texture
// target 7; option 1's choice 99 puts MaterialResourcesID 777 (FileDataID
// 888) on that target.
db2fx::CharacterTables layout42Tables() {
    db2fx::CharacterTables rows;
    rows.chrModelMaterial = {{1, 42, 1, 1024, 512, 0}};
    rows.layouts = {{42, 2048, 1024}};
    rows.layers = {{1, 42, 1, 0, 0, 0, 0xFFFFFFFFu, 7}};
    rows.options = {{1, 7, 0, 0, "Skin Color"}};
    rows.choices = {{99, 1, 0, "Pale"}};
    rows.elements = {{1, 99, 0, 0, 5, 0}};
    rows.materials = {{5, 7, 777}};
    rows.textureFileData = {{888, 777, 0}};
    return rows;
}

}  // namespace

TEST_CASE("husk export --db2-dir/--dbd-dir: the texture layout of the ChrModel drawing the file becomes a "
          "composite material document, its canvas the ChrModelMaterial's size") {
    auto dir = defaultsDir("chrmodel");
    writeCharacterModel(dir);
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows = layout42Tables();
    db2fx::drawModelAs(rows, kModelFdid, 7, 42);
    db2fx::writeCharacterTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, bundle) + tables.flags());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json m = parseManifest(bundle);
    REQUIRE(fs::exists(bundle / "materials" / "0.json"));
    nlohmann::json material = nlohmann::json::parse(readTextFile(bundle / "materials" / "0.json"));
    INFO(material.dump());
    CHECK(material["subject"]["id"]["table"] == "ChrModelMaterial");
    CHECK(material["subject"]["id"]["row"] == 1);
    nlohmann::json composite = material["resources"]["textures"][0]["composite"];
    CHECK(composite["width"] == 1024);
    CHECK(composite["height"] == 512);
    REQUIRE(composite["layers"].size() == 1);
    CHECK(composite["layers"][0]["texture"]["input"] == "target_7");
    CHECK(composite["layers"][0]["rect"] == std::vector<int>{0, 0, 1024, 512});

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir/--chr-model-id, no --char-layout-id: the layout comes from "
          "ChrModel.CharComponentTextureLayoutID and fills the skin slot through the character-texture tier") {
    auto dir = defaultsDir("chrmodelautolayout");
    writeCharacterModel(dir);
    writeFile(dir / "888.png", solidColorPng(4, 4, 200, 100, 50));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows = layout42Tables();
    rows.chrModel = {{7, 999, 42}};
    db2fx::writeCharacterTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, bundle) + tables.flags() + " --chr-model-id 7 --explain-textures");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("type=1 -> db2-character HIT") != std::string::npos);
    CHECK(result.output.find("names FileDataID 888 for ChrModelTextureTargetID 7") != std::string::npos);

    nlohmann::json m = parseManifest(bundle);
    nlohmann::json textures = m["resources"]["textures"];
    INFO(textures.dump());
    REQUIRE(textures.size() == 1);
    CHECK(textures[0]["state"] == "resolved");
    CHECK(textures[0]["identity"]["name"] == "888");
    CHECK(textures[0]["identity"]["name_source"] == "db2");

    fs::remove_all(dir);
}

TEST_CASE("husk export --char-layout-id: names the layout outright, without a ChrModel") {
    auto dir = defaultsDir("chrmodelexplicitlayout");
    writeCharacterModel(dir);
    writeFile(dir / "888.png", solidColorPng(4, 4, 200, 100, 50));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::writeCharacterTables(tables, layout42Tables());

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, bundle) + tables.flags() +
                          " --char-layout-id 42 --customization-choice-ids 99 --explain-textures");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("type=1 -> db2-character HIT") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --db2-dir given without --dbd-dir skips character data, doesn't fail the export") {
    auto dir = defaultsDir("chrmodelpartial");
    writeCharacterModel(dir);
    fs::create_directories(dir / "db2");

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, bundle) + " --db2-dir " + (dir / "db2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("character textures need --db2-dir and --dbd-dir") != std::string::npos);

    nlohmann::json m = parseManifest(bundle);
    CHECK_FALSE(fs::exists(bundle / "materials"));
    CHECK_FALSE(m["definition"].contains("customization"));

    fs::remove_all(dir);
}
