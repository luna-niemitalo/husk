// src/cmd_world_shared.hpp: the export-world helpers that need no files.

#include <doctest/doctest.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "cmd_world_shared.hpp"

using namespace husk;

TEST_CASE("dropMissingDoodadSets: a selection of a set the WMO lacks is dropped and reported") {
    canon::PlacementSet set;
    canon::PlacedInstance wmo;
    wmo.id = 53492465;
    wmo.asset = canon::Ref{canon::FileDataId{100}, "", canon::NameSource::None};
    wmo.activeSets = std::vector<canon::Ref>{canon::Ref{canon::RecordIndex{0}, "", canon::NameSource::None},
                                             canon::Ref{canon::RecordIndex{8}, "", canon::NameSource::None}};
    canon::PlacedInstance unknown = wmo;
    unknown.asset = canon::Ref{canon::FileDataId{200}, "", canon::NameSource::None};
    set.instances = {wmo, unknown};
    std::vector<std::string> warnings;

    commands::world::dropMissingDoodadSets(set, {{100, 8}}, [&](const std::string& w) { warnings.push_back(w); });

    REQUIRE(set.instances[0].activeSets->size() == 1);
    CHECK(std::get<canon::RecordIndex>(set.instances[0].activeSets->at(0).id).value == 0);
    CHECK(set.instances[1].activeSets->size() == 2);  // its set count isn't known: left alone
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "placement 53492465 selects doodad set 8 of WMO 100, which has 8 sets: dropped from the selection");
}
