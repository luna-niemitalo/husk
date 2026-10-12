// src/surface_db2.hpp against the real 12.1.0 TerrainType tables on the
// casc-tool mount (skipped without it).

#include <doctest/doctest.h>

#include <filesystem>
#include <sstream>

#include "surface_db2.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

constexpr const char* kMountDb2Dir = "/media/luna/wow_export/live/dbfilesclient";

bool haveRealSurfaceTables() {
    return std::filesystem::exists(std::filesystem::path(kMountDb2Dir) / "terraintype.db2") && !test::testDbdDir().empty();
}

}  // namespace

TEST_CASE("surfacedb::load: real 12.1.0 TerrainType rows with their sound classes" *
          doctest::skip(!haveRealSurfaceTables())) {
    std::ostringstream err;
    std::optional<surfacedb::Data> data = surfacedb::load(kMountDb2Dir, test::testDbdDir(), err);
    INFO(err.str());
    REQUIRE(data.has_value());
    CHECK(data->types.size() == 23);
    const surfacedb::TerrainType& grass = data->types.at(5);
    CHECK(grass.name == "Grass");
    CHECK(grass.soundId == 6);
    CHECK(grass.soundName == "Grass");
    CHECK(data->types.at(3).name == "Snow");
    CHECK(data->types.at(3).flags == 1);
    CHECK(data->types.at(10).name == "Default");
}
