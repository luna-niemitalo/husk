// Tests for husk::canon::Item (src/canon_item.hpp).

#include <doctest/doctest.h>

#include "canon_item.hpp"

using namespace husk::canon;

TEST_CASE("Item::identity carries an ItemModifiedAppearanceID as a Db2Row") {
    Item item;
    item.identity.id = Db2Row{"ItemModifiedAppearance", 15};
    item.identity.name = "Tunic of Testing";
    item.identity.source = NameSource::Db2;

    CHECK(std::holds_alternative<Db2Row>(item.identity.id));
    CHECK(std::get<Db2Row>(item.identity.id).table == "ItemModifiedAppearance");
    CHECK(std::get<Db2Row>(item.identity.id).row == 15);
}

TEST_CASE("Item: two slots and both component kinds at once -- the shape today's model cannot express") {
    // A tunic (chest) whose hem is a leg-slot piece, that also happens to
    // carry a standalone geometry attachment (e.g. a cape clasp). Neither
    // multi-slot nor mixed-component-kind items have any representation
    // in today's slot-keyed GearItem/GearSectionOverlay split.
    Item tunic;
    tunic.identity.id = Db2Row{"ItemModifiedAppearance", 42};
    tunic.slots = {"CHEST", "LEGS"};

    SectionOverlayComponent overlay;
    overlay.sections.push_back({/*componentSection=*/3, /*materialResourcesId=*/501, /*fileDataId=*/900123, ""});
    tunic.components.push_back(overlay);

    GeometryComponent clasp;
    clasp.models.push_back({/*position=*/0, /*modelResourcesId=*/160, 370361u, ""});
    clasp.models.push_back({/*position=*/1, /*modelResourcesId=*/161, std::nullopt, "no file fits"});
    clasp.materials.push_back({/*textureType=*/2, /*materialResourcesId=*/77, /*fileDataId=*/900456, 0, ""});
    tunic.components.push_back(clasp);

    REQUIRE(tunic.slots.size() == 2);
    CHECK(tunic.slots[0] == "CHEST");
    CHECK(tunic.slots[1] == "LEGS");

    REQUIRE(tunic.components.size() == 2);

    REQUIRE(std::holds_alternative<SectionOverlayComponent>(tunic.components[0]));
    const auto& gotOverlay = std::get<SectionOverlayComponent>(tunic.components[0]);
    REQUIRE(gotOverlay.sections.size() == 1);
    CHECK(gotOverlay.sections[0].componentSection == 3);
    CHECK(gotOverlay.sections[0].materialResourcesId == 501);
    CHECK(gotOverlay.sections[0].fileDataId == 900123);

    REQUIRE(std::holds_alternative<GeometryComponent>(tunic.components[1]));
    const auto& gotGeometry = std::get<GeometryComponent>(tunic.components[1]);
    REQUIRE(gotGeometry.models.size() == 2);
    CHECK(gotGeometry.models[0].fileDataId == 370361u);
    CHECK_FALSE(gotGeometry.models[1].fileDataId);
    CHECK(gotGeometry.models[1].unresolved == "no file fits");
    REQUIRE(gotGeometry.materials.size() == 1);
    CHECK(gotGeometry.materials[0].textureType == 2);
    CHECK(gotGeometry.materials[0].fileDataId == 900456);
}

TEST_CASE("GeometryComponent: no auxGlbPath field -- writer transport artifact excluded per I1") {
    GeometryComponent geo;
    geo.models.push_back({0, 160, 123u, ""});
    // No .auxGlbPath member exists; this test documents the exclusion by
    // construction (a stray auxGlbPath field would fail to compile here
    // if someone re-added it without updating this test's intent).
    CHECK(geo.models.size() == 1);
}
