// Structural convergence proof for canon::assembleEquippedItems
// (canon_item_builder.hpp) against itemappearance::resolve
// (itemappearance_db2.cpp) -- the real DB2 join export_extras.cpp's own
// attachGearAppearance loop also drives -- REFACTOR/README.md stage 3's
// gate. assembleEquippedItems reuses itemappearance::resolve directly
// rather than re-walking itemappearance::Data itself, so these checks
// derive an independent expectation straight from the already-loaded
// itemappearance::Data/modelfiledata::Data/texturefiledata::Data structs,
// not by calling assembleEquippedItems to produce its own expected values --
// same discipline test_canon_definition_convergence.cpp/
// test_canon_selection_convergence.cpp already establish.
//
// No real local itemmodifiedappearance.db2/itemappearance.db2/... fixture
// set exists in this repo (test_data/db2 only has the chrcustomization*
// tables), so this file is synthetic-Data-only -- same fallback every other
// convergence test here takes when its real fixture is absent.

#include <doctest/doctest.h>
#include <filesystem>
#include <sstream>

#include "canon_item_builder.hpp"

using namespace husk;

namespace {

// A minimal, fully-resolvable chain: one ItemModifiedAppearanceID ->
// ItemAppearanceID -> ItemDisplayInfoID, with both a case-1 (ModelMatRes)
// and a case-2 (MaterialRes) row attached to the same ItemDisplayInfoID --
// itemappearance::resolve's own doc comment names this as a real, valid
// shape (neither case implies the absence of the other).
itemappearance::Data makeFullyResolvableData(uint32_t appearanceId, uint32_t itemAppearanceId,
                                              uint32_t itemDisplayInfoId, uint32_t modelResourcesId) {
    itemappearance::Data data;
    data.modifiedAppearances.push_back({appearanceId, itemAppearanceId});
    data.appearances.push_back({itemAppearanceId, itemDisplayInfoId});
    data.displayInfos.push_back({itemDisplayInfoId, modelResourcesId});
    data.modelMatRes.push_back({itemDisplayInfoId, /*materialResourcesId=*/22758, /*textureType=*/2, 0});
    data.materialRes.push_back({itemDisplayInfoId, /*componentSection=*/6, /*materialResourcesId=*/34187});
    return data;
}

std::vector<canon::Item> assemble(const itemappearance::Data& data,
                                   const std::optional<modelfiledata::Data>& modelData,
                                   const std::optional<texturefiledata::Data>& textureData,
                                   const std::vector<appearance::GearEntry>& gear) {
    std::ostringstream err;
    return canon::assembleEquippedItems(data, modelData, textureData, gear, err);
}

}  // namespace

TEST_CASE("canon::assembleEquippedItems: a single gear entry carrying both case-1 (ModelMatRes) and "
          "case-2 (MaterialRes) rows produces one Item with both a GeometryComponent and a "
          "SectionOverlayComponent, each resolved against the independently-derived expectation") {
    itemappearance::Data data = makeFullyResolvableData(15, 154, 1542, 160);
    modelfiledata::Data modelData{{160, {370361}}};
    texturefiledata::Data textureData{{22758, 148134}, {34187, 500001}};

    std::vector<appearance::GearEntry> gear = {{"MAINHAND", 15}};
    std::vector<canon::Item> result = assemble(data, modelData, textureData, gear);

    REQUIRE(result.size() == 1);
    const canon::Item& item = result[0];
    REQUIRE(std::holds_alternative<canon::Db2Row>(item.identity.id));
    CHECK(std::get<canon::Db2Row>(item.identity.id).table == "ItemModifiedAppearance");
    CHECK(std::get<canon::Db2Row>(item.identity.id).row == 15);
    REQUIRE(item.slots.size() == 1);
    CHECK(item.slots[0] == "MAINHAND");

    // assembleEquippedItems mirrors attachGearAppearance's own case-2-then-
    // case-1 push order (export_extras.cpp) -- overlay first, geometry second.
    REQUIRE(item.components.size() == 2);
    REQUIRE(std::holds_alternative<canon::SectionOverlayComponent>(item.components[0]));
    const auto& overlay = std::get<canon::SectionOverlayComponent>(item.components[0]);
    REQUIRE(overlay.sections.size() == 1);
    CHECK(overlay.sections[0].componentSection == 6);
    CHECK(overlay.sections[0].materialResourcesId == 34187);
    CHECK(overlay.sections[0].fileDataId == 500001);

    REQUIRE(std::holds_alternative<canon::GeometryComponent>(item.components[1]));
    const auto& geometry = std::get<canon::GeometryComponent>(item.components[1]);
    CHECK(geometry.modelFileDataIds == std::vector<uint32_t>{370361});
    REQUIRE(geometry.materials.size() == 1);
    CHECK(geometry.materials[0].textureType == 2);
    CHECK(geometry.materials[0].materialResourcesId == 22758);
    CHECK(geometry.materials[0].fileDataId == 148134);
}

TEST_CASE("canon::assembleEquippedItems: I7's own payoff -- two GearEntrys sharing one "
          "itemModifiedAppearanceId at different slots collapse into ONE canon::Item with two "
          "slots, its components resolved once, not duplicated -- unlike attachGearAppearance's "
          "own one-entry-per-slot shape (export_extras.cpp)") {
    itemappearance::Data data = makeFullyResolvableData(15, 154, 1542, 160);
    modelfiledata::Data modelData{{160, {370361}}};
    texturefiledata::Data textureData{{22758, 148134}, {34187, 500001}};

    std::vector<appearance::GearEntry> gear = {{"MAINHAND", 15}, {"OFFHAND", 15}};
    std::vector<canon::Item> result = assemble(data, modelData, textureData, gear);

    REQUIRE(result.size() == 1);
    const canon::Item& item = result[0];
    REQUIRE(item.slots.size() == 2);
    CHECK(item.slots[0] == "MAINHAND");
    CHECK(item.slots[1] == "OFFHAND");
    // Resolved once, not once per occurrence -- exactly one GeometryComponent
    // and one SectionOverlayComponent, not two of each.
    REQUIRE(item.components.size() == 2);
}

TEST_CASE("canon::assembleEquippedItems: the same slot repeated for the same appearance id is not "
          "pushed twice into Item::slots") {
    itemappearance::Data data = makeFullyResolvableData(15, 154, 1542, 160);
    std::vector<appearance::GearEntry> gear = {{"MAINHAND", 15}, {"MAINHAND", 15}};
    std::vector<canon::Item> result = assemble(data, std::nullopt, std::nullopt, gear);

    REQUIRE(result.size() == 1);
    REQUIRE(result[0].slots.size() == 1);
    CHECK(result[0].slots[0] == "MAINHAND");
}

TEST_CASE("canon::assembleEquippedItems: two distinct itemModifiedAppearanceIds stay two separate "
          "Items, preserving order of first appearance") {
    itemappearance::Data data;
    itemappearance::Data first = makeFullyResolvableData(15, 154, 1542, 160);
    itemappearance::Data second = makeFullyResolvableData(20, 200, 2000, 300);
    data.modifiedAppearances = {first.modifiedAppearances[0], second.modifiedAppearances[0]};
    data.appearances = {first.appearances[0], second.appearances[0]};
    data.displayInfos = {first.displayInfos[0], second.displayInfos[0]};
    data.modelMatRes = {first.modelMatRes[0], second.modelMatRes[0]};
    data.materialRes = {first.materialRes[0], second.materialRes[0]};

    // Gear list deliberately references the second appearance before the
    // first -- Items must come back in gear-list first-appearance order,
    // not table-storage order.
    std::vector<appearance::GearEntry> gear = {{"OFFHAND", 20}, {"MAINHAND", 15}};
    std::vector<canon::Item> result = assemble(data, std::nullopt, std::nullopt, gear);

    REQUIRE(result.size() == 2);
    CHECK(std::get<canon::Db2Row>(result[0].identity.id).row == 20);
    CHECK(std::get<canon::Db2Row>(result[1].identity.id).row == 15);
}

TEST_CASE("canon::assembleEquippedItems: a real ModelResourcesID of 0 with no materials means "
          "'no standalone geometry' -- no fabricated empty GeometryComponent, same distinction "
          "attachGearAppearance itself must draw (export_extras.cpp's own comment there)") {
    itemappearance::Data data;
    data.modifiedAppearances.push_back({367, 495});
    data.appearances.push_back({495, 233});
    data.displayInfos.push_back({233, /*modelResourcesId=*/0});
    // Case-2 only: a real ItemDisplayInfoMaterialRes row, no ModelMatRes at all.
    data.materialRes.push_back({233, 6, 34187});

    std::vector<appearance::GearEntry> gear = {{"FEET", 367}};
    std::vector<canon::Item> result = assemble(data, std::nullopt, std::nullopt, gear);

    REQUIRE(result.size() == 1);
    REQUIRE(result[0].components.size() == 1);
    CHECK(std::holds_alternative<canon::SectionOverlayComponent>(result[0].components[0]));
}

TEST_CASE("canon::assembleEquippedItems: a gear entry whose resolve() doesn't produce a real "
          "ItemDisplayInfoID is skipped entirely, not represented as an empty Item") {
    itemappearance::Data data;
    data.modifiedAppearances.push_back({1, 2});
    // No Appearance row for itemAppearanceId 2 -- the chain dangles here.

    std::vector<appearance::GearEntry> gear = {{"MAINHAND", 1}, {"MAINHAND", 999}};  // 999 not in data at all
    std::vector<canon::Item> result = assemble(data, std::nullopt, std::nullopt, gear);

    CHECK(result.empty());
}

TEST_CASE("canon::assembleEquippedItems: modelData absent leaves modelFileDataIds empty, "
          "materials' fileDataId stay resolvable independently via textureData") {
    itemappearance::Data data = makeFullyResolvableData(15, 154, 1542, 160);
    texturefiledata::Data textureData{{22758, 148134}, {34187, 500001}};

    std::vector<appearance::GearEntry> gear = {{"MAINHAND", 15}};
    std::vector<canon::Item> result = assemble(data, std::nullopt, textureData, gear);

    REQUIRE(result.size() == 1);
    REQUIRE(result[0].components.size() == 2);
    const auto& geometry = std::get<canon::GeometryComponent>(result[0].components[1]);
    CHECK(geometry.modelFileDataIds.empty());
    REQUIRE(geometry.materials.size() == 1);
    CHECK(geometry.materials[0].fileDataId == 148134);  // still resolved -- textureData was given
}

TEST_CASE("canon::assembleEquippedItems: textureData absent leaves every fileDataId unresolved "
          "(0), modelFileDataIds still resolve independently via modelData") {
    itemappearance::Data data = makeFullyResolvableData(15, 154, 1542, 160);
    modelfiledata::Data modelData{{160, {370361}}};

    std::vector<appearance::GearEntry> gear = {{"MAINHAND", 15}};
    std::vector<canon::Item> result = assemble(data, modelData, std::nullopt, gear);

    REQUIRE(result.size() == 1);
    REQUIRE(result[0].components.size() == 2);
    const auto& geometry = std::get<canon::GeometryComponent>(result[0].components[1]);
    CHECK(geometry.modelFileDataIds == std::vector<uint32_t>{370361});
    REQUIRE(geometry.materials.size() == 1);
    CHECK(geometry.materials[0].fileDataId == 0);
    const auto& overlay = std::get<canon::SectionOverlayComponent>(result[0].components[0]);
    REQUIRE(overlay.sections.size() == 1);
    CHECK(overlay.sections[0].fileDataId == 0);
}

TEST_CASE("canon::assembleEquippedItems: no real local itemmodifiedappearance.db2 fixture set "
          "exists in test_data/db2 -- real-data convergence relies on synthetic Data above, same "
          "fallback every other convergence test here takes when its real fixture is absent" *
          doctest::skip(!std::filesystem::exists("test_data/db2/itemmodifiedappearance.db2"))) {
    // Placeholder gate: flips to a real assembleEquippedItems-vs-Data check
    // (same shape as test_canon_definition_convergence.cpp's real-data case)
    // the moment a real itemmodifiedappearance/itemappearance/itemdisplayinfo/
    // itemdisplayinfomodelmatres/itemdisplayinfomaterialres.db2 fixture set
    // lands in test_data/db2.
    CHECK(std::filesystem::exists("test_data/db2/itemmodifiedappearance.db2"));
}
