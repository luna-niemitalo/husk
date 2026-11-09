// CLI tier: `husk export --appearance` -- the appearance written as one
// preset (`selection.presets`), its gear as `definition.items` (the item's
// model exported as its own bundle under items/, its textures, its body
// section overlays), its 'cust' choices as the preset's customization
// picks; plus the --appearance/--customization-choice-ids conflict.
// Synthetic M2/.skel plus DB2/WoWDBDefs fixtures, real compiled binary
// (run_husk.hpp). The DB2 chain's own resolution is
// tests/test_cli_appearance.cpp's.

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

void writeCharacter(const fs::path& dir, const std::string& stem) {
    writeFile(dir / (stem + ".m2"), tinyValidM2());
    writeFile(dir / (stem + "00.skin"), tinyMatchingSkin());
    writeFile(dir / (stem + ".skel"), boneCorrectionSkel());
}

// The ItemModifiedAppearance -> ItemAppearance -> ItemDisplayInfo chain
// and its model/texture tables, each with the given rows.
struct ItemRows {
    std::vector<db2fx::Row> modifiedAppearance;  // ID, ItemAppearanceID
    std::vector<db2fx::Row> appearance;          // ID, ItemDisplayInfoID
    std::vector<db2fx::Row> displayInfo;         // ID, ModelResourcesID
    std::vector<db2fx::Row> modelMatRes;         // ID, ItemDisplayInfoID, MaterialResourcesID, TextureType, ModelIndex
    std::vector<db2fx::Row> modelFileData;       // FileDataID, ModelResourcesID
    std::vector<db2fx::Row> textureFileData;     // FileDataID, MaterialResourcesID, UsageType
    std::vector<db2fx::Row> materialRes;         // ID, ItemDisplayInfoID, ComponentSection, MaterialResourcesID
};

void writeItemTables(db2fx::Tables& t, const ItemRows& rows) {
    t.table("ItemModifiedAppearance", db2fx::ints({"ID", "ItemAppearanceID"}), rows.modifiedAppearance);
    t.table("ItemAppearance", db2fx::ints({"ID", "ItemDisplayInfoID"}), rows.appearance);
    t.table("ItemDisplayInfo", db2fx::ints({"ID", "ModelResourcesID"}), rows.displayInfo);
    t.table("ItemDisplayInfoModelMatRes",
            db2fx::ints({"ID", "ItemDisplayInfoID", "MaterialResourcesID", "TextureType", "ModelIndex"}),
            rows.modelMatRes);
    t.table("ModelFileData", db2fx::ints({"FileDataID", "ModelResourcesID"}), rows.modelFileData);
    t.table("TextureFileData", db2fx::ints({"FileDataID", "MaterialResourcesID", "UsageType"}), rows.textureFileData);
    t.table("ItemDisplayInfoMaterialRes",
            db2fx::ints({"ID", "ItemDisplayInfoID", "ComponentSection", "MaterialResourcesID"}), rows.materialRes);
}

// ItemModifiedAppearanceID 15 -> ItemAppearanceID 154 -> ItemDisplayInfoID
// 1542 -> ModelResourcesID 160 -> .m2 FileDataID 370361 (real local data:
// creature/pygmy/pygmyshaman.m2); `withTexture` adds MaterialResourcesID
// 22758 -> texture FileDataID 148134 as its texture type 2.
ItemRows mainHandItem(bool withTexture) {
    ItemRows rows;
    rows.modifiedAppearance = {{15, 154}};
    rows.appearance = {{154, 1542}};
    rows.displayInfo = {{1542, 160}};
    rows.modelFileData = {{370361, 160}};
    if (withTexture) {
        rows.modelMatRes = {{1, 1542, 22758, 2, 0}};
        rows.textureFileData = {{148134, 22758, 0}};
    }
    return rows;
}

// The preset --appearance writes: the last one.
nlohmann::json appearancePreset(nlohmann::json& m) {
    nlohmann::json presets = m["selection"]["presets"];
    REQUIRE(presets.is_array());
    REQUIRE(!presets.empty());
    return presets.back();
}

nlohmann::json textureFileDataId(const nlohmann::json& manifest, const nlohmann::json& index) {
    nlohmann::json texture = textureAt(manifest, index);
    return texture["identity"]["id"]["value"];
}

}  // namespace

TEST_CASE("husk export: --appearance and --customization-choice-ids are mutually exclusive") {
    auto dir = defaultsDir("gearconflict");
    writeFile(dir / "gearconflict.m2", tinyValidM2());
    writeFile(dir / "gearconflict00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "gearconflict.m2").string() +
                           " --appearance \"husk-appearance/1 gear=MAINHAND:15\""
                           " --customization-choice-ids 1");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("mutually exclusive") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: a malformed husk-appearance/1 string fails cleanly, not a crash") {
    auto dir = defaultsDir("gearmalformed");
    writeFile(dir / "gearmalformed.m2", tinyValidM2());
    writeFile(dir / "gearmalformed00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "gearmalformed.m2").string() +
                           " --appearance \"not-a-real-appearance-string\"");
    CHECK(result.exitCode != 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: 'gear' entries without --db2-dir/--dbd-dir are reported and skipped, "
          "the preset gets no items, export still succeeds") {
    auto dir = defaultsDir("gearnodb2");
    writeCharacter(dir, "gearnodb2");

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "gearnodb2.m2").string() + " -o " + bundle.string() +
                          " --appearance \"husk-appearance/1 gear=MAINHAND:15\"");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("--appearance gear needs --db2-dir and --dbd-dir") != std::string::npos);

    nlohmann::json m = parseManifest(bundle);
    CHECK((!m["definition"].contains("items") || m["definition"]["items"].empty()));
    nlohmann::json preset = appearancePreset(m);
    CHECK((!preset.contains("items") || preset["items"].empty()));

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: a standalone-geometry gear entry resolves through ItemModifiedAppearance "
          "-> ItemAppearance -> ItemDisplayInfo -> ItemDisplayInfoModelMatRes to its model and texture "
          "FileDataIDs, as a definition item the preset wears") {
    auto dir = defaultsDir("gearcase1");
    writeCharacter(dir, "gearcase1");
    writeFile(dir / "148134.png", solidColorPng(4, 4, 10, 20, 30));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    writeItemTables(tables, mainHandItem(true));

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "gearcase1.m2").string() + " -o " + bundle.string() +
                          " --appearance \"husk-appearance/1 gear=MAINHAND:15\"" + tables.flags());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json m = parseManifest(bundle);
    nlohmann::json items = m["definition"]["items"];
    INFO(items.dump());
    INFO(m["resources"]["textures"].dump());
    REQUIRE(items.size() == 1);
    CHECK(items[0]["ref"]["id"]["table"] == "ItemModifiedAppearance");
    CHECK(items[0]["ref"]["id"]["row"] == 15);
    REQUIRE(items[0]["models"].size() == 1);
    CHECK(items[0]["models"][0]["id"]["value"] == 370361);
    REQUIRE(items[0]["textures"].size() == 1);
    CHECK(items[0]["textures"][0]["texture_type"] == 2);
    CHECK(textureFileDataId(m, items[0]["textures"][0]["texture"]) == 148134);
    CHECK((!items[0].contains("overlays") || items[0]["overlays"].empty()));

    nlohmann::json preset = appearancePreset(m);
    INFO(preset.dump());
    REQUIRE(preset["items"].size() == 1);
    CHECK(preset["items"][0]["item"] == 0);
    CHECK(preset["items"][0]["source"]["slot_token"] == "MAINHAND");

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: a gear item whose model --listfile/--listfile-root finds is exported as "
          "its own bundle at items/<fdid>.bundle/, which the item and the references name by relative uri") {
    auto dir = defaultsDir("gearauxexport");
    writeCharacter(dir, "gearauxexport");
    fs::path corpusRoot = dir / "corpus";
    fs::create_directories(corpusRoot / "item" / "objectcomponents" / "weapon");
    writeFile(corpusRoot / "item" / "objectcomponents" / "weapon" / "fakesword.m2", tinyValidM2());
    writeFile(corpusRoot / "item" / "objectcomponents" / "weapon" / "fakesword00.skin", tinyMatchingSkin());
    std::ofstream(dir / "listfile.csv") << "370361;item/objectcomponents/weapon/fakesword.m2\n";
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    writeItemTables(tables, mainHandItem(false));

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "gearauxexport.m2").string() + " -o " + bundle.string() +
                          " --appearance \"husk-appearance/1 gear=MAINHAND:15\"" + tables.flags() + " --listfile " +
                          (dir / "listfile.csv").string() + " --listfile-root " + corpusRoot.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    const std::string uri = "items/370361.bundle/manifest.json";
    REQUIRE(fs::exists(bundle / uri));
    nlohmann::json item = parseManifest(bundle / "items" / "370361.bundle");
    CHECK(item["subject"]["id"]["value"] == 370361);

    nlohmann::json m = parseManifest(bundle);
    INFO(m["definition"]["items"].dump());
    INFO(m["references"].dump());
    CHECK(m["definition"]["items"][0]["models"][0]["uri"] == uri);
    bool referenced = false;
    for (const auto& r : m["references"]) {
        referenced = referenced || (r.contains("asset") && r["asset"].value("uri", "") == uri);
    }
    CHECK(referenced);

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: an object-skin section-overlay gear entry (boots spanning LEG_LOWER + "
          "FOOT) keeps every ComponentSection -> texture pair, not collapsed to one") {
    auto dir = defaultsDir("gearcase2");
    writeCharacter(dir, "gearcase2");
    writeFile(dir / "500001.png", solidColorPng(4, 4, 10, 20, 30));
    writeFile(dir / "500002.png", solidColorPng(4, 4, 30, 20, 10));
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    // Real ItemDisplayInfoID 233 (boots): no model, two body sections.
    ItemRows rows;
    rows.modifiedAppearance = {{367, 495}};
    rows.appearance = {{495, 233}};
    rows.displayInfo = {{233, 0}};
    rows.textureFileData = {{500001, 34187, 0}, {500002, 27801, 0}};
    rows.materialRes = {{1, 233, 6, 34187}, {2, 233, 7, 27801}};
    writeItemTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "gearcase2.m2").string() + " -o " + bundle.string() +
                          " --appearance \"husk-appearance/1 gear=FEET:367\"" + tables.flags());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json m = parseManifest(bundle);
    nlohmann::json items = m["definition"]["items"];
    INFO(items.dump());
    INFO(m["resources"]["textures"].dump());
    REQUIRE(items.size() == 1);
    CHECK(items[0]["ref"]["id"]["row"] == 367);
    CHECK((!items[0].contains("models") || items[0]["models"].empty()));
    nlohmann::json overlays = items[0]["overlays"];
    REQUIRE(overlays.size() == 2);
    CHECK(overlays[0]["component_section"] == 6);
    CHECK(textureFileDataId(m, overlays[0]["texture"]) == 500001);
    CHECK(overlays[1]["component_section"] == 7);
    CHECK(textureFileDataId(m, overlays[1]["texture"]) == 500002);

    nlohmann::json preset = appearancePreset(m);
    REQUIRE(preset["items"].size() == 1);
    CHECK(preset["items"][0]["slot"] == "feet");
    CHECK(preset["items"][0]["source"]["slot_token"] == "FEET");

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: 'cust' entries become the preset's customization picks; one the "
          "model's catalogs lack is the preset's unresolved") {
    auto dir = defaultsDir("gearcust");
    writeCharacter(dir, "gearcust");
    std::ofstream(dir / "listfile.csv") << "9400;gearcust.m2\n";
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, 9400, 42, 0);
    rows.options = {{1, 42, 0, 0, "Face"}, {2, 42, 1, 0, "Hair"}};
    rows.choices = {{7, 1, 0, "Narrow"}, {8, 1, 1, "Wide"}, {9, 2, 0, "Braid"}};
    db2fx::writeCharacterTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "gearcust.m2").string() + " -o " + bundle.string() +
                          " --appearance \"husk-appearance/1 cust=8,9,404\"" + tables.flags() + " --listfile " +
                          (dir / "listfile.csv").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json m = parseManifest(bundle);
    nlohmann::json preset = appearancePreset(m);
    INFO(preset.dump());
    REQUIRE(preset["customization"].size() == 2);
    CHECK(preset["customization"][0] == nlohmann::json{{"option", 0}, {"choice", 1}});
    CHECK(preset["customization"][1] == nlohmann::json{{"option", 1}, {"choice", 0}});
    REQUIRE(preset["unresolved"]["fields"].size() == 1);
    CHECK(preset["unresolved"]["fields"][0]["field"] == "ChrCustomizationChoiceID");
    CHECK(preset["unresolved"]["fields"][0]["value"] == 404);

    fs::remove_all(dir);
}
