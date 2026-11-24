// The place tables naming maps and their parts: src/map_db2.hpp (real
// tables on the casc-tool mount, skipped without it) and the map block
// built from it (commands::world::mapInfo).

#include <doctest/doctest.h>
#include <filesystem>
#include <sstream>

#include "../src/cmd_world_shared.hpp"
#include "../src/map_db2.hpp"
#include "test_data_paths.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

constexpr const char* kLiveDb2Dir = "/media/luna/wow_export/live/dbfilesclient";

bool haveLiveTable(const char* file) { return fs::exists(fs::path(kLiveDb2Dir) / file) && !test::testDbdDir().empty(); }

mapdb::Data syntheticMaps() {
    mapdb::Data data;
    mapdb::Row azeroth;
    azeroth.id = 0;
    azeroth.name = "Eastern Kingdoms";
    azeroth.directory = "Azeroth";
    azeroth.areaTableId = 0;
    mapdb::Row copy;
    copy.id = 2000;
    copy.name = "Phased Copy";
    copy.directory = "PhasedCopy";
    copy.instanceType = 7;
    copy.parent = 0;
    copy.cosmeticParent = 4242;  // no such row
    copy.areaTableId = 12;
    for (mapdb::Row* row : {&azeroth, &copy}) {
        data.idByDirectory[row->directory == "Azeroth" ? "azeroth" : "phasedcopy"] = row->id;
        data.rows[row->id] = *row;
    }
    return data;
}

}  // namespace

TEST_CASE("mapInfo: a map's row by directory, case ignored; parents and area as named refs") {
    mapdb::Data maps = syntheticMaps();
    areadb::Data areas;
    areas[12].name = "Elwynn Forest";

    CHECK_FALSE(commands::world::mapInfo("phasedcopy", nullptr, &areas).has_value());
    CHECK_FALSE(commands::world::mapInfo("nosuchmap", &maps, &areas).has_value());

    std::optional<canon::MapInfo> info = commands::world::mapInfo("phasedcopy", &maps, &areas);
    REQUIRE(info.has_value());
    CHECK(std::get<canon::Db2Row>(info->ref.id).table == "Map");
    CHECK(std::get<canon::Db2Row>(info->ref.id).row == 2000);
    CHECK(info->ref.name == "Phased Copy");
    CHECK(info->ref.source == canon::NameSource::Db2);
    CHECK(info->directory == "PhasedCopy");
    CHECK(info->instanceType == 7);
    CHECK_FALSE(info->instanceTypeName.has_value());
    REQUIRE(info->parent.has_value());
    CHECK(info->parent->name == "Eastern Kingdoms");
    // A parent row the table lacks is still the row, unnamed.
    REQUIRE(info->cosmeticParent.has_value());
    CHECK(std::get<canon::Db2Row>(info->cosmeticParent->id).row == 4242);
    CHECK(info->cosmeticParent->name.empty());
    CHECK(info->cosmeticParent->source == canon::NameSource::None);
    REQUIRE(info->area.has_value());
    CHECK(std::get<canon::Db2Row>(info->area->id).table == "AreaTable");
    CHECK(info->area->name == "Elwynn Forest");

    std::optional<canon::MapInfo> azeroth = commands::world::mapInfo("AZEROTH", &maps, nullptr);
    REQUIRE(azeroth.has_value());
    CHECK(azeroth->instanceTypeName == "none");
    CHECK_FALSE(azeroth->parent.has_value());
    CHECK_FALSE(azeroth->area.has_value());

    std::optional<canon::Ref> ref = commands::world::mapRef("azeroth", &maps);
    REQUIRE(ref.has_value());
    CHECK(ref->name == "Eastern Kingdoms");
}

TEST_CASE("mapdb::instanceTypeName: the Map.dbd comment's five names, nothing past them") {
    CHECK(mapdb::instanceTypeName(0) == "none");
    CHECK(mapdb::instanceTypeName(2) == "raid");
    CHECK(mapdb::instanceTypeName(4) == "arena");
    CHECK_FALSE(mapdb::instanceTypeName(5).has_value());
    CHECK_FALSE(mapdb::instanceTypeName(-1).has_value());
}

TEST_CASE("mapdb::load: live Map table -- Azeroth and an instance" * doctest::skip(!haveLiveTable("map.db2"))) {
    std::ostringstream err;
    std::optional<mapdb::Data> data = mapdb::load(kLiveDb2Dir, test::testDbdDir(), err);
    INFO(err.str());
    REQUIRE(data.has_value());
    const mapdb::Row* azeroth = mapdb::findByDirectory(*data, "azeroth");
    REQUIRE(azeroth != nullptr);
    CHECK(azeroth->id == 0);
    CHECK(azeroth->name == "Eastern Kingdoms");
    CHECK(azeroth->instanceType == 0);
    CHECK_FALSE(azeroth->parent.has_value());
    const mapdb::Row& deadmines = data->rows.at(36);
    CHECK(deadmines.name == "Deadmines");
    CHECK(deadmines.instanceType == 1);
    CHECK(deadmines.maxPlayers == 10);
    const mapdb::Row& moltenCore = data->rows.at(409);
    CHECK(moltenCore.directory == "MoltenCore");
    CHECK(moltenCore.instanceType == 2);
    CHECK(moltenCore.maxPlayers == 40);
    CHECK(moltenCore.areaTableId == 2717);
    CHECK(data->rows.at(655).parent == 654u);  // GilneasPhase1 is a phase of Gilneas' map
}
