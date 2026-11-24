// Tests for husk::canon::Selection (src/canon_selection.hpp).

#include <doctest/doctest.h>

#include "canon_selection.hpp"

using namespace husk::canon;

TEST_CASE("CharacterSelection: multiple chosen customization choices, scoped to a chrModelId") {
    CharacterSelection sel;
    sel.chrModelId = 20;  // bloodelffemale_hd, per chrrace_db2.hpp's real derivation

    Ref choice1;
    choice1.id = Db2Row{"ChrCustomizationChoice", 101};
    choice1.name = "Long Fin";
    Ref choice2;
    choice2.id = Db2Row{"ChrCustomizationChoice", 202};
    choice2.name = "Blonde 01";
    sel.chosenChoices = {choice1, choice2};

    Selection selection = sel;

    REQUIRE(std::holds_alternative<CharacterSelection>(selection));
    const auto& got = std::get<CharacterSelection>(selection);
    CHECK(got.chrModelId == 20);
    REQUIRE(got.chosenChoices.size() == 2);
    CHECK(std::get<Db2Row>(got.chosenChoices[0].id).row == 101);
    CHECK(std::get<Db2Row>(got.chosenChoices[1].id).row == 202);
}

TEST_CASE("CreatureSelection: a display ID resolves a set of enabled geoset ids, no per-choice input") {
    CreatureSelection sel;
    sel.creatureDisplayId = 137795;
    sel.enabledGeosetIds = {101, 401, 1301};  // (GeosetIndex+1)*100+GeosetValue-encoded

    Selection selection = sel;

    REQUIRE(std::holds_alternative<CreatureSelection>(selection));
    const auto& got = std::get<CreatureSelection>(selection);
    CHECK(got.creatureDisplayId == 137795);
    CHECK(got.enabledGeosetIds.size() == 3);
}

TEST_CASE("Selection: customization/gear and creature-display selection are exclusive alternatives") {
    CharacterSelection character;
    character.chrModelId = 20;
    Selection asCharacter = character;
    CHECK(std::holds_alternative<CharacterSelection>(asCharacter));
    CHECK_FALSE(std::holds_alternative<CreatureSelection>(asCharacter));

    CreatureSelection creature;
    creature.creatureDisplayId = 137795;
    Selection asCreature = creature;
    CHECK(std::holds_alternative<CreatureSelection>(asCreature));
    CHECK_FALSE(std::holds_alternative<CharacterSelection>(asCreature));
}
