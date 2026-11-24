// canon::garments::fromGear (canon_garment_builder.hpp): the stage-1 split
// and the hem rules of REFACTOR/GARMENTS.md, on items shaped like transmog
// set 3540's chest and legs (WIKI_FINDINGS/GARMENTS.md "The body's own hem").

#include <doctest/doctest.h>

#include <algorithm>

#include "canon_garment_builder.hpp"

using namespace husk;

namespace {

canon::Item item(uint32_t row, const char* slot, std::vector<uint32_t> sections, std::vector<uint32_t> sets) {
    canon::Item i;
    i.identity.id = canon::Db2Row{"ItemModifiedAppearance", row};
    i.slots = {slot};
    canon::SectionOverlayComponent overlay;
    for (uint32_t s : sections) overlay.sections.push_back({s, 1000 + s, 2000 + s, ""});
    if (!overlay.sections.empty()) i.components.push_back(overlay);
    i.transmogSets = std::move(sets);
    return i;
}

canon::garments::Inputs inputs(bool withMask) {
    canon::garments::Inputs in;
    in.texture = [](uint32_t fdid, const std::string&) {
        canon::TextureRef t;
        t.resolved.id = canon::FileDataId{fdid};
        return t;
    };
    in.textureRole = [](uint32_t type) { return "type_" + std::to_string(type); };
    if (withMask) {
        canon::TextureRef mask;
        mask.resolved.name = "hem_mask_1102";
        in.bodyHemMask = mask;
    }
    return in;
}

std::vector<uint32_t> geosets(const canon::Garment& g) {
    std::vector<uint32_t> out;
    for (const auto& p : g.pieces) {
        if (const auto* geo = std::get_if<canon::GeometryPiece>(&p.what)) out.push_back(geo->group * 100 + geo->variant);
    }
    return out;
}

std::vector<std::pair<uint32_t, bool>> overlays(const canon::Garment& g) {
    std::vector<std::pair<uint32_t, bool>> out;
    for (const auto& p : g.pieces) {
        if (const auto* o = std::get_if<canon::OverlayPiece>(&p.what)) out.push_back({o->section, o->mask.has_value()});
    }
    return out;
}

const canon::Garment& byKind(const canon::Garments& g, const std::string& kind) {
    auto it = std::find_if(g.garments.begin(), g.garments.end(), [&](const canon::Garment& x) { return x.kind == kind; });
    REQUIRE(it != g.garments.end());
    return *it;
}

}  // namespace

TEST_CASE("garments: set 3540's body hem 1102 moves from the legs item to the tunic of the same set") {
    canon::Item chest = item(217938, "CHEST", {0, 1, 3, 4}, {3540});
    chest.geosetGroups = {2, 0, 0, 1, 0, 0};  // sleeves 803, torso 2202
    canon::Item legs = item(218068, "LEGS", {5, 6}, {3540});
    legs.geosetGroups = {1, 0, 0, 0, 0, 0};  // hem 1102

    canon::Garments out;
    std::vector<uint32_t> worn = canon::garments::fromGear({chest, legs}, inputs(true), out);

    const canon::Garment& tunic = byKind(out, "tunic");
    CHECK(tunic.layer == "mid");
    CHECK(geosets(tunic) == std::vector<uint32_t>{803, 2202, 1102});
    CHECK(overlays(tunic) == std::vector<std::pair<uint32_t, bool>>{{0, false}, {1, false}, {3, false}, {4, false}, {5, true}});
    const canon::Garment& leggings = byKind(out, "leggings");
    CHECK(leggings.layer == "base");
    CHECK(geosets(leggings).empty());
    CHECK(overlays(leggings) == std::vector<std::pair<uint32_t, bool>>{{5, false}, {6, false}});
    CHECK(worn.size() == 2);
    // Wearing the legs item exactly is wearing both garments.
    REQUIRE(out.sourceItems.size() == 2);
    const auto& legsSource = *std::find_if(out.sourceItems.begin(), out.sourceItems.end(),
                                           [](const canon::SourceItem& s) { return s.slotToken == "LEGS"; });
    CHECK(legsSource.garments.size() == 2);
    REQUIRE(out.outfits.size() == 1);
    CHECK(out.outfits[0].garments.size() == 2);
}

TEST_CASE("garments: a hem with no tunic of its set is a hem garment of its own; no mask, no hem paint") {
    canon::Item legs = item(5, "LEGS", {5}, {});
    legs.geosetGroups = {1, 0, 0, 0, 0, 0};
    canon::Garments out;
    canon::garments::fromGear({legs}, inputs(false), out);
    const canon::Garment& hem = byKind(out, "hem");
    CHECK(geosets(hem) == std::vector<uint32_t>{1102});
    CHECK(overlays(hem).empty());
    CHECK(overlays(byKind(out, "leggings")) == std::vector<std::pair<uint32_t, bool>>{{5, false}});
}

TEST_CASE("garments: a legs item's skirt geoset is a skirt garment, a chest's makes a robe") {
    canon::Item legs = item(6, "LEGS", {5}, {});
    legs.geosetGroups = {3, 0, 1, 0, 0, 0};  // thigh 1104, skirt 1302
    canon::Item chest = item(7, "CHEST", {3}, {});
    chest.geosetGroups = {0, 0, 1, 0, 0, 0};
    canon::Garments out;
    canon::garments::fromGear({legs, chest}, inputs(true), out);
    CHECK(geosets(byKind(out, "skirt")) == std::vector<uint32_t>{1302});
    CHECK(geosets(byKind(out, "leggings")) == std::vector<uint32_t>{1104});
    CHECK(geosets(byKind(out, "robe")) == std::vector<uint32_t>{1302});
}

TEST_CASE("garments: an item-model hem shown by a set's chest and legs is one piece, on the chest's garment") {
    auto withModel = [](canon::Item i) {
        canon::GeometryComponent g;
        g.models.push_back({0, 77, 5000u, ""});
        g.materials.push_back({2, 88, 6000, 0, ""});
        i.components.push_back(g);
        return i;
    };
    canon::Item chest = withModel(item(8, "CHEST", {}, {1}));
    canon::Item legs = withModel(item(9, "LEGS", {}, {1}));
    canon::Garments out;
    canon::garments::fromGear({legs, chest}, inputs(true), out);
    size_t hemPieces = 0;
    for (const auto& g : out.garments) {
        for (const auto& p : g.pieces) {
            if (p.derived == "item_model_hem") {
                ++hemPieces;
                CHECK(g.kind == "tunic");
                CHECK(std::get<canon::SkinnedPiece>(p.what).shownGeosets == std::vector<uint32_t>{1301});
            }
        }
    }
    CHECK(hemPieces == 1);
    const auto& legsSource = *std::find_if(out.sourceItems.begin(), out.sourceItems.end(),
                                           [](const canon::SourceItem& s) { return s.slotToken == "LEGS"; });
    CHECK(legsSource.garments.size() == 2);  // its leggings and the tunic holding the shared hem
}
