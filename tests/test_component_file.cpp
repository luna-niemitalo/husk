// src/component_file_db2.hpp: which of a resource's race/sex files a wearer
// draws (synthetic tags), and the real 12.1.5 tables on the casc-tool mount
// (skipped without it).

#include <doctest/doctest.h>

#include <filesystem>
#include <sstream>

#include "component_file_db2.hpp"
#include "test_data_paths.hpp"

using namespace husk;
using componentfile::Kind;

namespace {

constexpr const char* kMountDb2Dir = "/media/luna/wow_export/builds/12.1.5.70783/dbfilesclient";

bool haveRealTables() {
    return std::filesystem::exists(std::filesystem::path(kMountDb2Dir) / "componentmodelfiledata.db2") &&
           !test::testDbdDir().empty();
}

// A chest resource: human (1) and blood elf (10) files per sex; Void Elf
// (29) falls back to blood elf models.
componentfile::Data chestTags() {
    componentfile::Data data;
    data.models[101] = {1, 0, 0, -1};
    data.models[102] = {1, 1, 0, -1};
    data.models[103] = {10, 0, 0, -1};
    data.models[104] = {10, 1, 0, -1};
    data.races[29].model[0] = {10, std::nullopt};
    data.races[29].model[1] = {10, std::nullopt};
    return data;
}

}  // namespace

TEST_CASE("componentfile::pick: the wearer's race and sex pick one file; a fallback race stands in") {
    componentfile::Data data = chestTags();
    std::vector<uint32_t> files = {101, 102, 103, 104};

    CHECK(componentfile::pick(data, Kind::Model, files, {{10, 1}}).fileDataId == 104u);
    CHECK(componentfile::pick(data, Kind::Model, files, {{29, 0}}).fileDataId == 103u);

    componentfile::Pick none = componentfile::pick(data, Kind::Model, files, {{52, 0}});
    CHECK_FALSE(none.fileDataId);
    CHECK(none.unresolved == "no file fits race 52 sex 0, its fallback race, or any race");
}

TEST_CASE("componentfile::pick: one file needs no wearer; several need one") {
    componentfile::Data data = chestTags();
    CHECK(componentfile::pick(data, Kind::Model, {103}, {}).fileDataId == 103u);
    componentfile::Pick unknown = componentfile::pick(data, Kind::Model, {101, 102}, {});
    CHECK_FALSE(unknown.fileDataId);
    CHECK(unknown.unresolved == "2 files by race and sex; the wearer's race and sex are unknown");
}

TEST_CASE("componentfile::pick: exact sex before any; class-specific files are skipped; ties stay unpicked") {
    componentfile::Data data;
    data.models[1] = {0, 2, 0, -1};   // any race, any sex
    data.models[2] = {0, 1, 0, -1};   // any race, female
    data.models[3] = {0, 1, 12, -1};  // demon hunter only
    CHECK(componentfile::pick(data, Kind::Model, {1, 2, 3}, {{4, 1}}).fileDataId == 2u);
    CHECK(componentfile::pick(data, Kind::Model, {1, 2, 3}, {{4, 0}}).fileDataId == 1u);

    data.models[4] = {0, 2, 0, -1};
    componentfile::Pick tie = componentfile::pick(data, Kind::Model, {1, 4}, {{4, 0}});
    CHECK_FALSE(tie.fileDataId);
    CHECK(tie.unresolved == "files 1, 4 all fit race 0 gender 2");
}

TEST_CASE("componentfile::pick: a shoulder's side is its PositionIndex") {
    componentfile::Data data;
    data.models[143132] = {0, 2, 0, 0};
    data.models[143436] = {0, 2, 0, 1};
    std::vector<uint32_t> files = {143132, 143436};
    CHECK(componentfile::pick(data, Kind::Model, files, {{1, 0}}, 0).fileDataId == 143132u);
    CHECK(componentfile::pick(data, Kind::Model, files, {{1, 0}}, 1).fileDataId == 143436u);
}

TEST_CASE("componentfile::pick: texture GenderIndex 3 is any, 2 is none") {
    componentfile::Data data;
    data.textures[10] = {0, 3, 0, -1};
    data.textures[11] = {0, 2, 0, -1};
    CHECK(componentfile::pick(data, Kind::Texture, {10, 11}, {{1, 0}}).fileDataId == 10u);
}

TEST_CASE("componentfile::pick: several possible wearer races must agree") {
    componentfile::Data data = chestTags();
    std::vector<uint32_t> files = {101, 102, 103, 104};
    CHECK(componentfile::pick(data, Kind::Model, files, {{10, 0}, {29, 0}}).fileDataId == 103u);
    componentfile::Pick split = componentfile::pick(data, Kind::Model, files, {{1, 0}, {10, 0}});
    CHECK_FALSE(split.fileDataId);
    CHECK(split.unresolved == "the wearer's race is one of several, and they draw different files");
}

TEST_CASE("componentfile::load: real 12.1.5 tags, copy-table rows included" * doctest::skip(!haveRealTables())) {
    std::ostringstream err;
    std::optional<componentfile::Data> data = componentfile::load(kMountDb2Dir, test::testDbdDir(), err);
    INFO(err.str());
    REQUIRE(data.has_value());
    // 65 stored records; the rest are copy-table rows.
    CHECK(data->models.size() > 59000);

    // ModelResourcesID 88202, a chest with one file per race and sex.
    const componentfile::Tag& bloodElfMale = data->models.at(8101365);
    CHECK(bloodElfMale.raceId == 10);
    CHECK(bloodElfMale.genderIndex == 0);
    CHECK(bloodElfMale.positionIndex == -1);  // BitpackedSigned, stored as 2 bits

    // Void Elf (29) male models fall back to blood elf male; Pandaren 25 to 24.
    CHECK(data->races.at(29).model[0].raceId == 10);
    CHECK(data->races.at(25).model[0].raceId == 24);
    CHECK(data->races.at(1).model[0].raceId == 0);

    std::vector<uint32_t> chest;
    for (const auto& [fdid, tag] : data->models) {
        if (fdid >= 8101365 && fdid <= 8101403) chest.push_back(fdid);
    }
    chest.push_back(8176656);
    CHECK(componentfile::pick(*data, Kind::Model, chest, {{29, 0}}).fileDataId == 8101365u);
    CHECK(componentfile::pick(*data, Kind::Model, chest, {{24, 1}, {25, 1}, {26, 1}}).fileDataId == 8101391u);

    // ModelResourcesID 16402, a shoulder: left and right files.
    CHECK(data->models.at(143132).positionIndex == 0);
    CHECK(data->models.at(143436).positionIndex == 1);
}
