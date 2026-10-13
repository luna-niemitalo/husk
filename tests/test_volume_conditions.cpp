// src/volume_condition_db2.hpp (real 12.1.0 tables on the casc-tool mount,
// skipped without it) and the map stage carrying each volume's
// PlayerCondition as a candidate gate condition (synthetic rows).

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "map_scene_input.hpp"
#include "test_data_paths.hpp"
#include "volume_condition_db2.hpp"

using namespace husk;

namespace {

constexpr const char* kMountDb2Dir = "/media/luna/wow_export/live/dbfilesclient";

bool haveRealTables() {
    return std::filesystem::exists(std::filesystem::path(kMountDb2Dir) / "volumefogcondition.db2") &&
           !test::testDbdDir().empty();
}

const canon::UnresolvedField* field(const canon::PlacedInstance& instance, const std::string& name) {
    const auto& u = instance.effect->unresolved;
    auto it = std::find_if(u.begin(), u.end(), [&](const canon::UnresolvedField& f) { return f.field == name; });
    return it == u.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("volumecond::load: real 12.1.0 Particulate and VolumeFogCondition rows" * doctest::skip(!haveRealTables())) {
    std::ostringstream err;
    std::optional<volumecond::Data> data = volumecond::load(kMountDb2Dir, test::testDbdDir(), err);
    INFO(err.str());
    REQUIRE(data.has_value());
    CHECK(data->particulateConditions.size() == 79);
    CHECK(data->particulateConditions.at(185) == 0);
    CHECK(data->fogConditions.size() == 64);  // 65 rows, one without a fog id
    const volumecond::FogCondition& fog = data->fogConditions.at(959);
    CHECK(fog.playerCondition == 81250);
    CHECK(fog.whenFalse == 1.0f);
    CHECK(fog.whenTrue == 1.0f);
}

TEST_CASE("buildMapEffects: a fog's and a particulate volume's PlayerCondition are candidate gate conditions") {
    wdt::FogsFile fogs;
    wdt::Fog fog;
    fog.id = 959;
    fog.rotation = {0, 0, 0, 1};
    fog.modelFileDataId = 1;
    fogs.fogs = {fog};
    wdt::MpvFile mpv;
    wdt::MapParticulateVolume v;
    v.id = 185;
    v.volume.extent = {10, 10, 10};
    v.boundsMin = {-5, -5, -5};
    v.boundsMax = {5, 5, 5};
    mpv.volumes = {v};
    volumecond::Data conditions;
    conditions.fogConditions[959] = {81250, 0.5f, 0.5f};
    conditions.particulateConditions[185] = 4242;
    mapinput::MapSceneInputs in;
    in.fogs = &fogs;
    in.mpv = &mpv;
    in.conditions = &conditions;

    std::vector<canon::PlacementSet> sets = mapinput::buildMapEffects(in);

    REQUIRE(sets.size() == 2);
    const canon::UnresolvedField* fogGate = field(sets[0].instances[0], "VolumeFogCondition PlayerConditionID");
    REQUIRE(fogGate);
    CHECK(fogGate->candidateRole == "gate.condition");
    CHECK(std::get<double>(fogGate->value) == 81250.0);
    REQUIRE(field(sets[0].instances[0], "VolumeFogCondition WhenTrue"));
    const canon::UnresolvedField* volumeGate = field(sets[1].instances[0], "Particulate PlayerConditionID");
    REQUIRE(volumeGate);
    CHECK(std::get<double>(volumeGate->value) == 4242.0);
}
