// src/light_db2.hpp (real 12.1.0 light tables on the casc-tool mount,
// skipped without it) and the map input stage's zone light instances and
// their light parameter sets (synthetic rows).

#include <doctest/doctest.h>
#include <bit>
#include <filesystem>
#include <set>
#include <sstream>

#include "../src/light_db2.hpp"
#include "../src/map_scene_input.hpp"
#include "test_data_paths.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

constexpr const char* kMountDb2Dir = "/media/luna/wow_export/live/dbfilesclient";

bool haveRealLightTables() {
    return fs::exists(fs::path(kMountDb2Dir) / "light.db2") && !test::testDbdDir().empty();
}

}  // namespace

TEST_CASE("lightdb::load: real 12.1.0 tables -- zones in world yards, fog keys by half-minute" *
          doctest::skip(!haveRealLightTables())) {
    std::ostringstream err;
    std::optional<lightdb::Data> data = lightdb::load(kMountDb2Dir, test::testDbdDir(), err);
    INFO(err.str());
    REQUIRE(data.has_value());
    CHECK(data->zones.size() == 5343);
    CHECK(data->mapIds.at("azeroth") == 0);
    // Light 2: a local zone west of Elwynn, its clear-weather params 14.
    auto zone = std::find_if(data->zones.begin(), data->zones.end(), [](const lightdb::Zone& z) { return z.id == 2; });
    REQUIRE(zone != data->zones.end());
    CHECK(zone->map == 0);
    CHECK(zone->position[0] == doctest::Approx(-10666.67f));
    CHECK(zone->falloffEnd == doctest::Approx(466.98f));
    CHECK(zone->params[0] == 14);
    const std::vector<lightdb::Key>& keys = data->keysByParams.at(14);
    REQUIRE(keys.size() == 2);
    CHECK(keys[1].time == 1440);
    CHECK(keys[0].fogDensity == 9.0f);
    CHECK(keys[0].endFogColorDistance == 3000.0f);
    CHECK(keys[0].directColor == 1845298u);
    CHECK(keys[0].cloudDensity == 0.5f);

    CHECK(data->params.size() == 1674);
    CHECK(data->params.at(14).glow == doctest::Approx(0.2f));
    CHECK(data->params.at(12).highlightSky);
    // LightParams 1 draws LightSkybox 81, DeathSkybox.mdx.
    CHECK(data->params.at(1).lightSkyboxId == 81);
    CHECK(data->skyboxes.at(81).skyboxFileDataId == 235313u);
    CHECK(data->skyboxes.at(81).name == "ENVIRONMENTS\\Stars\\DeathSkybox.mdx");
}

TEST_CASE("buildMapEffects: a map's Light zones index one shared light-params table, the map-wide one everywhere") {
    lightdb::Data lights;
    lights.mapIds["azeroth"] = 0;
    lightdb::Zone global;
    global.id = 1;
    global.params = {12, 0, 0, 0, 0, 7, 0, 0};
    lightdb::Zone local;
    local.id = 2;
    local.position = {-10666, 64, 0};
    local.falloffStart = 399;
    local.falloffEnd = 467;
    local.params = {14, 15, 0, 0, 0, 0, 0, 0};
    lightdb::Zone neighbour = local;
    neighbour.id = 4;
    neighbour.params = {14, 12, 0, 0, 0, 0, 0, 0};
    lightdb::Zone elsewhere = local;
    elsewhere.id = 3;
    elsewhere.map = 1;
    lights.zones = {global, local, elsewhere, neighbour};
    lightdb::Key noon;
    noon.time = 1440;
    noon.fogDensity = 9;
    noon.endFogColor = 0xFF336699u;
    noon.colorGradingFileDataId = 1140733;
    noon.field_12_0_0_63854_043 = 1;
    lights.keysByParams[12] = {noon};
    lights.keysByParams[14] = {noon, noon};
    lightdb::Params params12;
    params12.id = 12;
    params12.glow = 0.4f;
    params12.flags = 0x14;
    params12.sunPolar = 90;
    params12.lightSkyboxId = 81;
    params12.unnamed = {0x3F800000, 0x3F800000, 0, 0, 0, 0, 0, 0, 0, 0x3F800000, 0x3F800000, 0, 0x3F800000,
                        std::bit_cast<uint32_t>(0.35f), 0x3F800000, std::bit_cast<uint32_t>(0.3f)};
    lights.params[12] = params12;
    lights.skyboxes[81] = {81, "ENVIRONMENTS\\Stars\\DeathSkybox.mdx", 0x2, 235313, 0};
    mapinput::MapSceneInputs in;
    in.map = "Azeroth";
    in.lights = &lights;
    in.assetNames[235313] = "deathskybox";

    mapinput::ZoneLightAssets assets = mapinput::zoneLightAssets(in);
    CHECK(assets.textures == std::set<uint32_t>{1140733});
    CHECK(assets.models == std::set<uint32_t>{235313});

    std::vector<canon::PlacementSet> sets = mapinput::buildMapEffects(in);

    REQUIRE(sets.size() == 1);
    CHECK(sets[0].ref.name == "zone_lights");
    REQUIRE(sets[0].instances.size() == 3);
    const canon::PlacedInstance& everywhere = sets[0].instances[0];
    CHECK(everywhere.asset.name == "classic_zone_light_global");
    CHECK(everywhere.effect->inputs.empty());
    // Slot 5 is past the named conditions: unresolved.
    REQUIRE(everywhere.effect->unresolved.size() == 1);
    CHECK(everywhere.effect->unresolved[0].field == "Light params slot 5");

    const canon::PlacedInstance& zone = sets[0].instances[1];
    CHECK(zone.id == 2);
    CHECK(zone.asset.name == "classic_zone_light");
    CHECK(zone.translation.x == -10666.0f);
    CHECK(std::get<double>(zone.effect->inputs.at(1).value.value) == 467.0);
    const canon::ZoneLight& light = *zone.effect->style.at(0).blocks.at(0).zoneLight;
    // Slot 1 names params 15, which LightData has no keys for.
    CHECK_FALSE(light.params[canon::ZoneLight::ClearUnderwater]);
    CHECK(zone.effect->unresolved.at(0).field == "Light params slot 1");

    // Params 12 and 14 once each, though three zones use them.
    const std::vector<canon::LightParams>& table = sets[0].lightParams;
    REQUIRE(table.size() == 2);
    CHECK(*everywhere.effect->style.at(0).blocks.at(0).zoneLight->params[canon::ZoneLight::Clear] == 0);
    CHECK(*light.params[canon::ZoneLight::Clear] == 1);
    const canon::ZoneLight& neighbourLight = *sets[0].instances[2].effect->style.at(0).blocks.at(0).zoneLight;
    CHECK(*neighbourLight.params[canon::ZoneLight::Clear] == 1);
    CHECK(*neighbourLight.params[canon::ZoneLight::ClearUnderwater] == 0);

    const canon::LightParams& p12 = table[0];
    CHECK(std::get<canon::Db2Row>(p12.ref.id).row == 12);
    REQUIRE(p12.settings);
    CHECK(p12.settings->glow == 0.4f);
    CHECK(p12.settings->sunPolar == doctest::Approx(1.5707963f));
    REQUIRE(p12.settings->skybox);
    CHECK(p12.settings->skybox->model.name == "deathskybox");
    CHECK(p12.settings->skybox->flags == 0x2);
    // Only Field_12_0_1_65617_031 differs from its common value (0.7).
    REQUIRE(p12.settings->unresolved.size() == 1);
    CHECK(p12.settings->unresolved[0].field == "LightParams Field_12_0_1_65617_031");
    CHECK(std::get<double>(p12.settings->unresolved[0].value) == doctest::Approx(0.3));
    // Params 14 has keys but no LightParams row.
    CHECK_FALSE(table[1].settings);
    REQUIRE(table[1].keys.size() == 2);

    // Half-minutes become seconds; 0xFF336699 -> RGB 0x33 0x66 0x99, high byte unresolved.
    const canon::LightKey& key = p12.keys.at(0);
    CHECK(key.time == 43200.0f);
    CHECK(key.fog.density == 9.0f);
    CHECK(key.fog.endColor.x == doctest::Approx(0x33 / 255.0));
    REQUIRE(key.colorGrading);
    CHECK(std::get<canon::FileDataId>(key.colorGrading->resolved.id).value == 1140733u);
    CHECK_FALSE(key.darkerColorGrading);
    REQUIRE(key.unresolved.size() == 1);
    CHECK(key.unresolved[0].field == "LightData EndFogColor high byte");
    CHECK(std::get<double>(key.unresolved[0].value) == 255.0);
}
