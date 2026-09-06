// Structural convergence proof for canon::assembleCreatureSelection
// (canon_selection_builder.hpp) against creaturegeoset::resolveDisplay
// (creature_geoset_db2.cpp) -- the real DB2 join export_extras.cpp's own
// attachCreatureGeosets also drives -- REFACTOR/README.md stage 3's gate.
// assembleCreatureSelection reuses resolveDisplay directly rather than
// re-walking Data itself, so these checks derive an independent expectation
// straight from creaturegeoset::Data (the same already-loaded structure
// both assembleCreatureSelection and attachCreatureGeosets consume) using
// resolveDisplay's own documented (GeosetIndex+1)*100+GeosetValue formula,
// not by calling assembleCreatureSelection to produce its own expected
// values. No real local creaturedisplayinfogeosetdata.db2 fixture exists in
// this repo (test_data/db2 has no creature-geoset table), so this file is
// synthetic-Data-only -- same fallback every other convergence test here
// takes when its real fixture is absent.

#include <algorithm>
#include <doctest/doctest.h>

#include "canon_selection_builder.hpp"

using namespace husk;

TEST_CASE("canon::assembleCreatureSelection: a display with several rows converges with "
          "creaturegeoset::resolveDisplay's own (GeosetIndex+1)*100+GeosetValue formula, "
          "independently computed here") {
    creaturegeoset::Data data;
    data.entries = {
        {137795, 0, 1},  // -> (0+1)*100+1 = 101
        {137795, 3, 1},  // -> (3+1)*100+1 = 401
        {137795, 12, 1}, // -> (12+1)*100+1 = 1301
    };

    canon::CreatureSelection result = canon::assembleCreatureSelection(data, 137795);
    CHECK(result.creatureDisplayId == 137795);

    std::vector<uint32_t> expected;
    for (const auto& e : data.entries) {
        if (e.creatureDisplayInfoId != 137795) continue;
        expected.push_back((e.geosetIndex + 1) * 100 + e.geosetValue);
    }
    CHECK(result.enabledGeosetIds == expected);
}

TEST_CASE("canon::assembleCreatureSelection: a display with zero rows yields an empty selection, "
          "not an error -- real and common, most creature displays enable none of their model's "
          "optional geosets") {
    creaturegeoset::Data data;
    data.entries = {{999, 0, 1}};  // some other display's row only

    canon::CreatureSelection result = canon::assembleCreatureSelection(data, 137795);
    CHECK(result.creatureDisplayId == 137795);
    CHECK(result.enabledGeosetIds.empty());
}

TEST_CASE("canon::assembleCreatureSelection: an unrelated displayId's rows are excluded, "
          "independent of table storage order") {
    creaturegeoset::Data data;
    // Deliberately interleaved with another display's rows.
    data.entries = {
        {200, 0, 0},     // other display
        {137795, 1, 2},  // -> 202
        {200, 5, 5},     // other display
        {137795, 0, 0},  // -> 100
    };

    canon::CreatureSelection result = canon::assembleCreatureSelection(data, 137795);
    REQUIRE(result.enabledGeosetIds.size() == 2);
    CHECK(result.enabledGeosetIds[0] == 202);
    CHECK(result.enabledGeosetIds[1] == 100);
}

TEST_CASE("canon::assembleCreatureSelection: enabledGeosetIds preserves resolveDisplay's own "
          "order verbatim -- no sort/dedupe, matching attachCreatureGeosets's own "
          "push-every-resolved-id-unchanged behavior (export_extras.cpp)") {
    creaturegeoset::Data data;
    data.entries = {
        {1, 5, 0},  // -> 600
        {1, 0, 0},  // -> 100
        {1, 5, 0},  // -> 600 again, a real duplicate row should not be collapsed
    };

    canon::CreatureSelection result = canon::assembleCreatureSelection(data, 1);
    REQUIRE(result.enabledGeosetIds.size() == 3);
    CHECK(result.enabledGeosetIds[0] == 600);
    CHECK(result.enabledGeosetIds[1] == 100);
    CHECK(result.enabledGeosetIds[2] == 600);
}
