// src/light_db2.hpp (real 12.1.0 Light/LightData/Map tables on the
// casc-tool mount, skipped without it) and the map input stage's zone fog
// instances (synthetic rows).

#include <doctest/doctest.h>
#include <filesystem>
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
    const std::vector<lightdb::FogKey>& keys = data->keysByParams.at(14);
    REQUIRE(keys.size() == 2);
    CHECK(keys[1].time == 1440);
    CHECK(keys[0].fogDensity == 9.0f);
    CHECK(keys[0].endFogColorDistance == 3000.0f);
}

TEST_CASE("buildMapEffects: a map's Light zones become zone fog instances, the map-wide one everywhere") {
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
    lightdb::Zone elsewhere = local;
    elsewhere.id = 3;
    elsewhere.map = 1;
    lights.zones = {global, local, elsewhere};
    lightdb::FogKey noon;
    noon.time = 1440;
    noon.fogDensity = 9;
    noon.endFogColor = 0xFF336699u;
    lights.keysByParams[12] = {noon};
    lights.keysByParams[14] = {noon, noon};
    mapinput::MapSceneInputs in;
    in.map = "Azeroth";
    in.lights = &lights;

    std::vector<canon::PlacementSet> sets = mapinput::buildMapEffects(in);

    REQUIRE(sets.size() == 1);
    CHECK(sets[0].ref.name == "zone_fogs");
    REQUIRE(sets[0].instances.size() == 2);
    const canon::PlacedInstance& everywhere = sets[0].instances[0];
    CHECK(everywhere.asset.name == "classic_zone_fog_global");
    CHECK(everywhere.effect->inputs.empty());
    // Slot 5 names params with no rows of its own reading: unresolved.
    REQUIRE(everywhere.effect->unresolved.size() == 1);
    CHECK(everywhere.effect->unresolved[0].field == "Light params slot 5");

    const canon::PlacedInstance& zone = sets[0].instances[1];
    CHECK(zone.id == 2);
    CHECK(zone.asset.name == "classic_zone_fog");
    CHECK(zone.translation.x == -10666.0f);
    CHECK(std::get<double>(zone.effect->inputs.at(1).value.value) == 467.0);
    const canon::ZoneFog& fog = *zone.effect->style.at(0).blocks.at(0).zoneFog;
    REQUIRE(fog.cycles[canon::ZoneFog::Clear].size() == 2);
    // Half-minutes become seconds; BGRA 99 66 33 FF -> RGB 0x33 0x66 0x99.
    CHECK(fog.cycles[canon::ZoneFog::Clear][0].time == 43200.0f);
    CHECK(fog.cycles[canon::ZoneFog::Clear][0].density == 9.0f);
    CHECK(fog.cycles[canon::ZoneFog::Clear][0].endColor.x == doctest::Approx(0x33 / 255.0));
    // Slot 1 names params 15, which LightData has no rows for.
    CHECK(fog.cycles[canon::ZoneFog::ClearUnderwater].empty());
    CHECK(zone.effect->unresolved.at(0).field == "Light params slot 1");
}
