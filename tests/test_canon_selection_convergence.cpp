// Structural convergence proof for canon::assembleCreatureSelection and
// canon::assembleCharacterSelection (canon_selection_builder.hpp).
//
// assembleCreatureSelection is checked against creaturegeoset::resolveDisplay
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
//
// assembleCharacterSelection is a pure composition function over two
// already-independently-tested inputs (a raw chosen-choice-id list,
// mirroring attachCustomizationChoices' own as-is/no-filtering use of its
// parsed --customization-choice-ids list -- export_extras.cpp; and a
// std::vector<canon::Item>, canon::assembleEquippedItems' own output,
// test_canon_item_convergence.cpp) -- so its checks build those inputs as
// literals/via assembleEquippedItems and verify the composition directly,
// not a DB2 join of its own.

#include <algorithm>
#include <doctest/doctest.h>
#include <sstream>

#include "canon_item_builder.hpp"
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

TEST_CASE("canon::assembleCharacterSelection: a realistic composition -- several chosen choice "
          "ids plus a canon::Item list from assembleEquippedItems -- produces the correct "
          "CharacterSelection shape, checked field by field") {
    itemappearance::Data data;
    data.modifiedAppearances.push_back({15, 154});
    data.appearances.push_back({154, 1542});
    data.displayInfos.push_back({1542, /*modelResourcesId=*/160});
    data.modelMatRes.push_back({1542, /*materialResourcesId=*/22758, /*textureType=*/2, 0});
    std::ostringstream err;
    std::vector<appearance::GearEntry> gear = {{"MAINHAND", 15}};
    std::vector<canon::Item> items = canon::assembleEquippedItems(data, std::nullopt, std::nullopt, gear, err);
    REQUIRE(items.size() == 1);

    std::vector<uint32_t> chosenChoiceIds = {101, 202, 303};
    canon::CharacterSelection result = canon::assembleCharacterSelection(20, chosenChoiceIds, items);

    CHECK(result.chrModelId == 20);
    REQUIRE(result.chosenChoices.size() == 3);
    for (size_t i = 0; i < chosenChoiceIds.size(); ++i) {
        REQUIRE(std::holds_alternative<canon::Db2Row>(result.chosenChoices[i].id));
        CHECK(std::get<canon::Db2Row>(result.chosenChoices[i].id).table == "ChrCustomizationChoice");
        CHECK(std::get<canon::Db2Row>(result.chosenChoices[i].id).row == chosenChoiceIds[i]);
        CHECK(result.chosenChoices[i].name.empty());
        CHECK(result.chosenChoices[i].source == canon::NameSource::None);
    }

    REQUIRE(result.equippedItems.size() == 1);
    CHECK(std::get<canon::Db2Row>(result.equippedItems[0].identity.id).row == 15);
    REQUIRE(result.equippedItems[0].slots.size() == 1);
    CHECK(result.equippedItems[0].slots[0] == "MAINHAND");
}

TEST_CASE("canon::assembleCharacterSelection: empty chosenChoiceIds and/or empty equippedItems "
          "yield empty fields, not an error") {
    canon::CharacterSelection allEmpty = canon::assembleCharacterSelection(20, {}, {});
    CHECK(allEmpty.chrModelId == 20);
    CHECK(allEmpty.chosenChoices.empty());
    CHECK(allEmpty.equippedItems.empty());

    canon::Item item;
    item.identity.id = canon::Db2Row{"ItemModifiedAppearance", 15};
    item.slots = {"MAINHAND"};
    canon::CharacterSelection onlyItems = canon::assembleCharacterSelection(20, {}, {item});
    CHECK(onlyItems.chosenChoices.empty());
    REQUIRE(onlyItems.equippedItems.size() == 1);

    canon::CharacterSelection onlyChoices = canon::assembleCharacterSelection(20, {101}, {});
    REQUIRE(onlyChoices.chosenChoices.size() == 1);
    CHECK(onlyChoices.equippedItems.empty());
}

TEST_CASE("canon::assembleCharacterSelection: EquippedItem genuinely drops canon::Item::components "
          "-- a real Item with populated components produces an EquippedItem carrying only "
          "identity+slots") {
    canon::Item item;
    item.identity.id = canon::Db2Row{"ItemModifiedAppearance", 15};
    item.slots = {"MAINHAND", "OFFHAND"};
    canon::GeometryComponent geometry;
    geometry.modelFileDataIds = {370361};
    item.components.push_back(geometry);
    REQUIRE(!item.components.empty());  // precondition: this Item really does carry component data

    canon::CharacterSelection result = canon::assembleCharacterSelection(20, {}, {item});

    REQUIRE(result.equippedItems.size() == 1);
    const auto& equipped = result.equippedItems[0];
    // CharacterSelection::EquippedItem's own type has no components field at
    // all (canon_selection.hpp) -- nothing to assert false on beyond
    // confirming identity+slots carried over correctly, which is itself
    // the proof components didn't (there's no member to have silently
    // carried it).
    CHECK(std::get<canon::Db2Row>(equipped.identity.id).row == 15);
    CHECK(equipped.slots == item.slots);
}

TEST_CASE("canon::assembleCharacterSelection: preserves input order for both chosenChoices and "
          "equippedItems") {
    canon::Item first;
    first.identity.id = canon::Db2Row{"ItemModifiedAppearance", 20};
    canon::Item second;
    second.identity.id = canon::Db2Row{"ItemModifiedAppearance", 15};

    std::vector<uint32_t> chosenChoiceIds = {303, 101, 202};
    canon::CharacterSelection result = canon::assembleCharacterSelection(20, chosenChoiceIds, {first, second});

    REQUIRE(result.chosenChoices.size() == 3);
    CHECK(std::get<canon::Db2Row>(result.chosenChoices[0].id).row == 303);
    CHECK(std::get<canon::Db2Row>(result.chosenChoices[1].id).row == 101);
    CHECK(std::get<canon::Db2Row>(result.chosenChoices[2].id).row == 202);

    REQUIRE(result.equippedItems.size() == 2);
    CHECK(std::get<canon::Db2Row>(result.equippedItems[0].identity.id).row == 20);
    CHECK(std::get<canon::Db2Row>(result.equippedItems[1].identity.id).row == 15);
}
