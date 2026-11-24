// CLI tier: creature presets' geoset choices in `husk export`'s bundle --
// CreatureModelData -> CreatureDisplayInfo -> CreatureDisplayInfoGeosetData,
// read through --db2-dir/--dbd-dir, the model matched by the FileDataID its
// listfile row gives it. The selection rule itself (0 and 900+ always
// shown) is tests/test_creature_options.cpp's; this file checks the tables
// reach the written bundle.

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_db2_fixtures.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;

namespace {

constexpr uint32_t kModelFdid = 7001;
constexpr uint32_t kModelData = 500;

// A model with geosets 0 and 401 (twoGeosetSkin), listed as kModelFdid.
fs::path writeGeosetModel(const fs::path& dir) {
    fs::create_directories(dir / "creature" / "geo");
    writeFile(dir / "creature" / "geo" / "geo.m2", twoVertexOneMaterialM2());
    writeFile(dir / "creature" / "geo" / "geo00.skin", twoGeosetSkin());
    std::ofstream(dir / "listfile.csv") << kModelFdid << ";creature/geo/geo.m2\n";
    return dir / "creature" / "geo" / "geo.m2";
}

std::string exportArgs(const fs::path& dir, const fs::path& model, const fs::path& bundle) {
    return "export " + model.string() + " -o " + bundle.string() + " --listfile " + (dir / "listfile.csv").string() +
           " --listfile-root " + dir.string();
}

db2fx::Row display(uint32_t id) { return {id, kModelData, db2fx::f32(1.0f), 255, 0, 0, 0}; }

}  // namespace

TEST_CASE("husk export --db2-dir/--dbd-dir: a creature display's CreatureDisplayInfoGeosetData rows become its "
          "preset's shown_geosets, limited to the geosets the mesh has") {
    auto dir = defaultsDir("creaturegeoset");
    fs::path model = writeGeosetModel(dir);
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    rows.creatureModelData = {{kModelData, kModelFdid, db2fx::f32(1.0f), 1}};
    rows.creatureDisplayInfo = {display(42)};
    // (GeosetIndex + 1) * 100 + GeosetValue: 101, 200, 401. Only 401 is
    // one of the mesh's geosets.
    rows.displayGeosets = {{42, 0, 1}, {42, 1, 0}, {42, 3, 1}};
    db2fx::writeCharacterTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, model, bundle) + tables.flags());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("1 creature preset(s) from 1 display(s)") != std::string::npos);

    nlohmann::json m = parseManifest(bundle);
    INFO(m["selection"].dump());
    nlohmann::json presets = m["selection"]["presets"];
    REQUIRE(presets.size() == 1);
    CHECK(appearanceOf(m, presets[0])["shown_geosets"] ==
          nlohmann::json::parse(R"([{"group": 0, "variant": 0}, {"group": 4, "variant": 1}])"));
    REQUIRE(presets[0]["sources"].size() == 1);
    CHECK(presets[0]["sources"][0]["ref"]["id"]["table"] == "CreatureDisplayInfo");
    CHECK(presets[0]["sources"][0]["ref"]["id"]["row"] == 42);

    fs::remove_all(dir);
}

TEST_CASE("husk export-appearances: a tree's inline lists move into one appearances.json the manifests point at; "
          "ids stay, and a rerun keeps them") {
    auto dir = defaultsDir("appearances-tree");
    fs::path model = writeGeosetModel(dir);
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    rows.creatureModelData = {{kModelData, kModelFdid, db2fx::f32(1.0f), 1}};
    rows.creatureDisplayInfo = {display(42), display(43)};
    rows.displayGeosets = {{42, 3, 1}};
    db2fx::writeCharacterTables(tables, rows);

    fs::path tree = dir / "tree";
    fs::path bundle = tree / "models" / (std::to_string(kModelFdid) + ".canon.bundle");
    auto exported = runHusk(exportArgs(dir, model, bundle) + tables.flags());
    INFO(exported.output);
    REQUIRE(exported.exitCode == 0);
    nlohmann::json before = parseManifest(bundle);
    REQUIRE(before["selection"]["presets"].size() == 2);
    std::string pointer = before["selection"]["presets"][0]["appearance"];
    fs::path manifestPath = bundle / "manifest.json";
    std::string inlineText = readTextFile(manifestPath);
    fs::path copy = dir / "tree-copy";
    fs::copy(tree, copy, fs::copy_options::recursive);
    std::vector<std::string> outputs;

    for (int run = 0; run < 2; ++run) {
        auto moved = runHusk("export-appearances " + tree.string());
        INFO(moved.output);
        REQUIRE(moved.exitCode == 0);
        CHECK(moved.output.find("appearances.json: 2 appearances of 1 models") != std::string::npos);

        nlohmann::json after = parseManifest(bundle);  // validates the pointers across the shared file
        CHECK(after["selection"]["presets"] == before["selection"]["presets"]);
        const nlohmann::json* list = nullptr;
        for (const auto& r : after["references"]) {
            if (r["kind"] == "appearances") list = &r;
        }
        REQUIRE(list);
        CHECK_FALSE(list->contains("document"));
        CHECK((*list)["asset"]["uri"] == "../../appearances.json");

        std::vector<husk::bundle_schema::Problem> problems = husk::bundle_schema::validateDocument(tree / "appearances.json");
        CHECK(problems.empty());
        nlohmann::json shared = nlohmann::json::parse(readTextFile(tree / "appearances.json"));
        REQUIRE(shared["references"].size() == 1);
        CHECK(shared["references"][0]["asset"]["uri"] == "models/" + std::to_string(kModelFdid) + ".canon.bundle/manifest.json");
        nlohmann::json entry = entryById(shared["appearances"], pointer);
        CHECK(entry["model"] == shared["references"][0]["id"]);
        CHECK(entry["shown_geosets"] == nlohmann::json::parse(R"([{"group": 0, "variant": 0}, {"group": 4, "variant": 1}])"));
        outputs.push_back(readTextFile(manifestPath) + readTextFile(tree / "appearances.json"));
    }

    // Bundles are content-addressed in the store: the rewrite is the one
    // reference's text and nothing else, and a rerun or a fresh copy writes
    // the same bytes.
    std::string externalText = readTextFile(manifestPath);
    size_t prefix = 0;
    while (prefix < externalText.size() && externalText[prefix] == inlineText[prefix]) ++prefix;
    size_t suffix = 0;
    while (suffix < externalText.size() - prefix &&
           externalText[externalText.size() - 1 - suffix] == inlineText[inlineText.size() - 1 - suffix]) {
        ++suffix;
    }
    std::string changed = externalText.substr(prefix, externalText.size() - prefix - suffix);
    INFO(changed);
    CHECK(changed.find("\"uri\": \"../../appearances.json\"") != std::string::npos);
    CHECK(changed.size() < 64);
    CHECK(inlineText.substr(prefix, inlineText.size() - prefix - suffix).find("\"document\"") != std::string::npos);
    CHECK(outputs[0] == outputs[1]);
    auto fresh = runHusk("export-appearances " + copy.string());
    REQUIRE(fresh.exitCode == 0);
    CHECK(readTextFile(copy / "models" / (std::to_string(kModelFdid) + ".canon.bundle") / "manifest.json") +
              readTextFile(copy / "appearances.json") ==
          outputs[0]);
    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir: a display with no geoset rows, on a model whose displays pick "
          "geosets, shows only the geosets no pick decides (an empty pick, not an absent one)") {
    auto dir = defaultsDir("creaturegeoset-empty");
    fs::path model = writeGeosetModel(dir);
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    rows.creatureModelData = {{kModelData, kModelFdid, db2fx::f32(1.0f), 1}};
    rows.creatureDisplayInfo = {display(99)};
    rows.displayGeosets = {{42, 3, 1}};  // another display's
    db2fx::writeCharacterTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, model, bundle) + tables.flags());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json m = parseManifest(bundle);
    nlohmann::json presets = m["selection"]["presets"];
    REQUIRE(presets.size() == 1);
    CHECK(appearanceOf(m, presets[0])["shown_geosets"] == nlohmann::json::parse(R"([{"group": 0, "variant": 0}])"));

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir: a model whose CreatureModelData picks no geosets "
          "(CreatureGeosetDataID 0) gets presets that leave geosets unchosen") {
    auto dir = defaultsDir("creaturegeoset-unpicked");
    fs::path model = writeGeosetModel(dir);
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    rows.creatureModelData = {{kModelData, kModelFdid, db2fx::f32(1.0f), 0}};
    rows.creatureDisplayInfo = {display(42)};
    rows.displayGeosets = {{42, 3, 1}};
    db2fx::writeCharacterTables(tables, rows);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, model, bundle) + tables.flags());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json m = parseManifest(bundle);
    nlohmann::json presets = m["selection"]["presets"];
    REQUIRE(presets.size() == 1);
    // No geosets and no textures: the body chooses nothing, so no appearance.
    CHECK_FALSE(presets[0].contains("appearance"));

    fs::remove_all(dir);
}

TEST_CASE("husk export without --db2-dir/--dbd-dir: no creature presets") {
    auto dir = defaultsDir("creaturegeoset-no-db2");
    fs::path model = writeGeosetModel(dir);

    auto bundle = dir / "out.bundle";
    auto result = runHusk(exportArgs(dir, model, bundle));
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json m = parseManifest(bundle);
    CHECK((!m.contains("selection") || !m["selection"].contains("presets") || m["selection"]["presets"].empty()));

    fs::remove_all(dir);
}
