// Tests for husk::canon::Definition (src/canon_definition.hpp).

#include <doctest/doctest.h>

#include "canon_definition.hpp"

using namespace husk::canon;

TEST_CASE("Category: id as a Db2Row, real name/orderIndex") {
    Category cat;
    cat.ref.id = Db2Row{"ChrCustomizationCategory", 3};
    cat.ref.name = "Hair";
    cat.ref.source = NameSource::Db2;
    cat.orderIndex = 2;

    REQUIRE(std::holds_alternative<Db2Row>(cat.ref.id));
    CHECK(std::get<Db2Row>(cat.ref.id).table == "ChrCustomizationCategory");
    CHECK(std::get<Db2Row>(cat.ref.id).row == 3);
    CHECK(cat.ref.name == "Hair");
    CHECK(cat.orderIndex == 2);
}

TEST_CASE("Option: a Category plus multiple Choices, one with a geoset element, one without") {
    Option option;
    option.ref.id = Db2Row{"ChrCustomizationOption", 100};
    option.ref.name = "Ears";
    option.ref.source = NameSource::Db2;
    option.orderIndex = 0;

    option.category.ref.id = Db2Row{"ChrCustomizationCategory", 1};
    option.category.ref.name = "Face";
    option.category.orderIndex = 0;

    Choice finned;
    finned.ref.id = Db2Row{"ChrCustomizationChoice", 200};
    finned.ref.name = "Long Fin";
    finned.orderIndex = 0;
    finned.geoset = Geoset::fromRawId(301);  // group 3, variant 1
    option.choices.push_back(finned);

    Choice swatchOnly;
    swatchOnly.ref.id = Db2Row{"ChrCustomizationChoice", 201};
    // real swatch-only choices carry no Name_lang string at all
    swatchOnly.orderIndex = 1;
    option.choices.push_back(swatchOnly);

    REQUIRE(option.choices.size() == 2);

    CHECK(option.choices[0].ref.name == "Long Fin");
    REQUIRE(option.choices[0].geoset.has_value());
    CHECK(option.choices[0].geoset->group == 3);
    CHECK(option.choices[0].geoset->variant == 1);

    CHECK(option.choices[1].ref.name.empty());
    CHECK_FALSE(option.choices[1].geoset.has_value());

    CHECK(option.category.ref.name == "Face");
}

TEST_CASE("Definition: chrModelId scopes the whole catalog, distinct catalogs reach the same index differently") {
    Definition nightElfFace;
    nightElfFace.chrModelId = 10;

    Option faceOption;
    faceOption.ref.id = Db2Row{"ChrCustomizationOption", 5};
    faceOption.ref.name = "Face";
    Choice face12;
    face12.ref.id = Db2Row{"ChrCustomizationChoice", 512};
    face12.orderIndex = 12;
    faceOption.choices.push_back(face12);
    nightElfFace.options.push_back(faceOption);

    Definition dwarfFace;
    dwarfFace.chrModelId = 20;
    dwarfFace.options = nightElfFace.options;  // same shape, different scope

    CHECK(nightElfFace.chrModelId != dwarfFace.chrModelId);
    CHECK(nightElfFace.options[0].choices[0].orderIndex == dwarfFace.options[0].choices[0].orderIndex);
}
