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
    CHECK(presets[0]["shown_geosets"] == std::vector<int>{0, 401});
    REQUIRE(presets[0]["sources"].size() == 1);
    CHECK(presets[0]["sources"][0]["ref"]["id"]["table"] == "CreatureDisplayInfo");
    CHECK(presets[0]["sources"][0]["ref"]["id"]["row"] == 42);

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
    CHECK(presets[0]["shown_geosets"] == std::vector<int>{0});

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
    CHECK_FALSE(presets[0].contains("shown_geosets"));

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
