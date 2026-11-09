// CLI tier: the character customization catalog in `husk export`'s bundle
// (`definition.customization`, one catalog per ChrModel drawing the file)
// -- each choice's geoset, .bone correction set and textures bound to the
// layout's composite material inputs -- read from synthetic DB2/WoWDBDefs
// fixtures by the real compiled binary (run_husk.hpp). ChrModel derivation
// and the character-texture tier are tests/test_cli_chrmodel_id.cpp's and
// tests/test_cli_chrmodel.cpp's.

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

constexpr uint32_t kModelFdid = 9200;
constexpr uint32_t kChrModel = 42;

// A rigged model whose .skel names .bone FileDataID 424242
// (boneCorrectionSkel), listed as kModelFdid.
fs::path writeCharacterModel(const fs::path& dir) {
    writeFile(dir / "chrcust.m2", tinyValidM2());
    writeFile(dir / "chrcust00.skin", tinyMatchingSkin());
    writeFile(dir / "chrcust.skel", boneCorrectionSkel());
    std::ofstream(dir / "listfile.csv") << kModelFdid << ";chrcust.m2\n";
    return dir / "chrcust.m2";
}

std::string exportArgs(const fs::path& dir, const fs::path& bundle) {
    return "export " + (dir / "chrcust.m2").string() + " -o " + bundle.string() + " --listfile " +
           (dir / "listfile.csv").string();
}

nlohmann::json exportedManifest(const fs::path& dir, const db2fx::Tables& tables, const std::string& extra = "") {
    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, bundle) + tables.flags() + extra);
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    return parseManifest(bundle);
}

// The catalog's choice whose DB2 row is `choiceId`, as {option, choice}
// indices.
std::pair<size_t, size_t> findChoice(const nlohmann::json& manifest, uint32_t choiceId) {
    nlohmann::json options = manifest.at("definition").at("customization").at("options");
    for (size_t o = 0; o < options.size(); ++o) {
        for (size_t c = 0; c < options[o]["choices"].size(); ++c) {
            if (options[o]["choices"][c]["ref"]["id"]["row"] == choiceId) return {o, c};
        }
    }
    FAIL("choice " << choiceId << " not in the catalog: " << options.dump());
    return {0, 0};
}

nlohmann::json choiceAt(const nlohmann::json& manifest, uint32_t choiceId) {
    auto [o, c] = findChoice(manifest, choiceId);
    return manifest.at("definition").at("customization").at("options").at(o).at("choices").at(c);
}

nlohmann::json textureFileDataId(const nlohmann::json& manifest, const nlohmann::json& index) {
    nlohmann::json texture = textureAt(manifest, index);
    INFO(texture.dump());
    return texture["identity"]["id"]["value"];
}

}  // namespace

TEST_CASE("husk export --db2-dir/--dbd-dir: a choice's geoset and its --bones-dir correction set land on the "
          "catalog's choices, a choice owning several ChrCustomizationElement rows included") {
    auto dir = defaultsDir("chrcustchoice");
    writeCharacterModel(dir);
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, kModelFdid, kChrModel, 0);
    rows.options = {{1, kChrModel, 0, 0, "Face"}};
    rows.choices = {{7, 1, 0, "Narrow"}, {9, 1, 1, "Wide"}};
    // Choice 7's first row carries neither a geoset nor a bone set (most
    // real rows look like that); its second carries the bone set.
    rows.elements = {{1, 7, 0, 0, 0, 0}, {2, 7, 0, 5, 0, 0}, {3, 9, 6, 0, 0, 0}};
    rows.geosets = {{6, 1, 3}};  // 1 * 100 + 3
    rows.boneSets = {{5, 424242, 0}};
    db2fx::writeCharacterTables(tables, rows);

    nlohmann::json m = exportedManifest(dir, tables, " --bones-dir " + dir.string());
    INFO(m["definition"].dump());
    nlohmann::json sets = m["resources"]["rig"]["correction_sets"];
    REQUIRE(sets.size() == 1);
    CHECK(sets[0]["ref"]["id"]["value"] == 424242);

    nlohmann::json narrow = choiceAt(m, 7);
    CHECK(narrow["correction_set"] == 0);
    CHECK_FALSE(narrow.contains("geoset_id"));
    nlohmann::json wide = choiceAt(m, 9);
    CHECK(wide["geoset_id"] == 103);
    CHECK_FALSE(wide.contains("correction_set"));

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir: a choice whose BoneFileDataID isn't among the model's own "
          "correction sets gets no correction_set, not a fabricated one") {
    auto dir = defaultsDir("chrcustchoiceunmatched");
    writeCharacterModel(dir);
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, kModelFdid, kChrModel, 0);
    rows.options = {{1, kChrModel, 0, 0, "Face"}};
    rows.choices = {{3, 1, 0, "Narrow"}};
    rows.elements = {{1, 3, 0, 4, 0, 0}};
    rows.boneSets = {{4, 999999, 0}};
    db2fx::writeCharacterTables(tables, rows);

    nlohmann::json m = exportedManifest(dir, tables, " --bones-dir " + dir.string());
    INFO(m["definition"].dump());
    REQUIRE(m["resources"]["rig"]["correction_sets"].size() == 1);
    CHECK_FALSE(choiceAt(m, 3).contains("correction_set"));

    fs::remove_all(dir);
}

TEST_CASE("husk export --customization-choice-ids: an unresolvable choice ID is reported and doesn't fail "
          "the export") {
    auto dir = defaultsDir("chrcustchoicemissing");
    writeCharacterModel(dir);
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    rows.elements = {{1, 3, 0, 0, 0, 0}};
    db2fx::writeCharacterTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, bundle) + tables.flags() + " --customization-choice-ids 404");
    INFO(result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("ChrCustomizationChoiceID 404 matched no row") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --customization-choice-ids without --db2-dir/--dbd-dir skips character data, "
          "doesn't fail the export") {
    auto dir = defaultsDir("chrcustchoicepartial");
    writeCharacterModel(dir);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, bundle) + " --customization-choice-ids 3");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("character textures need --db2-dir and --dbd-dir") != std::string::npos);

    nlohmann::json m = parseManifest(bundle);
    CHECK_FALSE(m["definition"].contains("customization"));

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir: the material chain (ChrCustomizationMaterial -> TextureFileData) "
          "binds a choice's texture to the layout composite's input for its ChrModelTextureTargetID") {
    auto dir = defaultsDir("chrcustmaterial");
    writeCharacterModel(dir);
    writeFile(dir / "888.png", solidColorPng(4, 4, 10, 20, 30));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, kModelFdid, kChrModel, 42);
    rows.options = {{1, kChrModel, 0, 0, "Skin Color"}};
    rows.choices = {{99, 1, 0, "Pale"}};
    rows.elements = {{1, 99, 0, 0, 5, 0}};
    rows.materials = {{5, 7, 777}};          // target 7, MaterialResourcesID 777
    rows.textureFileData = {{888, 777, 0}};  // base-skin row: FileDataID 888
    rows.chrModelMaterial = {{1, 42, 1, 4, 4, 0}};
    rows.layouts = {{42, 4, 4}};
    rows.sections = {{1, 42, 0, 0, 0, 4, 4, 0}};
    rows.layers = {{1, 42, 1, 0, 0, 1, 0b1, 7}};
    db2fx::writeCharacterTables(tables, rows);

    nlohmann::json m = exportedManifest(dir, tables);
    INFO(m["definition"].dump());
    nlohmann::json textures = choiceAt(m, 99)["textures"];
    REQUIRE(textures.size() == 1);
    CHECK(textures[0]["input"] == "target_7");
    CHECK_FALSE(textures[0].contains("only_with"));
    CHECK(textureFileDataId(m, textures[0]["texture"]) == 888);

    nlohmann::json material = nlohmann::json::parse(readTextFile(dir / "out.bundle" / "materials" / "0.json"));
    INFO(material.dump());
    nlohmann::json composite = material["resources"]["textures"][0]["composite"];
    REQUIRE(composite["layers"].size() == 1);
    CHECK(composite["layers"][0]["texture"]["input"] == "target_7");
    CHECK(composite["layers"][0]["rect"] == std::vector<int>{0, 0, 4, 4});

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir: a material conditional on another choice "
          "(RelatedChrCustomizationChoiceID) is bound only_with that choice; the unconditional one isn't -- "
          "a 'Tiara' hairstyle carries one material per hair colour") {
    auto dir = defaultsDir("chrcustrelated");
    writeCharacterModel(dir);
    writeFile(dir / "888.png", solidColorPng(4, 4, 10, 20, 30));
    writeFile(dir / "889.png", solidColorPng(4, 4, 30, 20, 10));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, kModelFdid, kChrModel, 42);
    rows.options = {{1, kChrModel, 0, 0, "Hairstyle"}, {2, kChrModel, 1, 0, "Hair Color"}};
    rows.choices = {{99, 1, 0, "Tiara"}, {200, 2, 0, "Blonde"}};
    rows.elements = {{1, 99, 0, 0, 5, 0}, {2, 99, 0, 0, 6, 200}};
    rows.materials = {{5, 7, 777}, {6, 8, 778}};
    rows.textureFileData = {{888, 777, 0}, {889, 778, 0}};
    rows.chrModelMaterial = {{1, 42, 1, 4, 4, 0}};
    rows.layouts = {{42, 4, 4}};
    rows.layers = {{1, 42, 1, 0, 0, 0, 0xFFFFFFFFu, 7}, {2, 42, 1, 1, 0, 0, 0xFFFFFFFFu, 8}};
    db2fx::writeCharacterTables(tables, rows);

    nlohmann::json m = exportedManifest(dir, tables);
    INFO(m["definition"].dump());
    nlohmann::json textures = choiceAt(m, 99)["textures"];
    REQUIRE(textures.size() == 2);
    CHECK(textures[0]["input"] == "target_7");
    CHECK(textureFileDataId(m, textures[0]["texture"]) == 888);
    CHECK_FALSE(textures[0].contains("only_with"));
    CHECK(textures[1]["input"] == "target_8");
    CHECK(textureFileDataId(m, textures[1]["texture"]) == 889);
    auto [blondeOption, blondeChoice] = findChoice(m, 200);
    CHECK(textures[1]["only_with"]["option"] == blondeOption);
    CHECK(textures[1]["only_with"]["choice"] == blondeChoice);

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir: catalog options carry their ChrCustomizationCategory (ref, name "
          "and order) when the category resolves") {
    auto dir = defaultsDir("chrcustcategory");
    writeCharacterModel(dir);
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, kModelFdid, kChrModel, 0);
    // Real shape (ChrModelID 20, 12.1.0): options 119/122 under category 2
    // ("Face"), option 583 under none.
    rows.options = {{119, kChrModel, 0, 2, "Skin Color"}, {122, kChrModel, 1, 2, "Hair Color"},
                    {583, kChrModel, 2, 0, "Ears"}};
    rows.choices = {{1190, 119, 0, "Pale"}, {1220, 122, 0, "Black"}, {5830, 583, 0, "Round"}};
    rows.categories = {{2, 1, "Face"}};
    db2fx::writeCharacterTables(tables, rows);

    nlohmann::json m = exportedManifest(dir, tables);
    nlohmann::json options = m["definition"]["customization"]["options"];
    INFO(options.dump());
    REQUIRE(options.size() == 3);
    for (size_t o : {0u, 1u}) {
        CHECK(options[o]["category"]["ref"]["id"]["table"] == "ChrCustomizationCategory");
        CHECK(options[o]["category"]["ref"]["id"]["row"] == 2);
        CHECK(options[o]["category"]["ref"]["name"] == "Face");
        CHECK(options[o]["category"]["order"] == 1);
    }
    CHECK(options[0]["ref"]["name"] == "Skin Color");
    CHECK(options[2]["ref"]["id"]["row"] == 583);

    fs::remove_all(dir);
}
