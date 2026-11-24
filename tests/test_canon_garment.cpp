// canon::garments::fromGear and NpcGarments (canon_garment_builder.hpp): the
// stage-1 split and the hem rules of REFACTOR/GARMENTS.md, on items shaped like
// transmog set 3540's chest and legs (WIKI_FINDINGS/GARMENTS.md "The body's own hem").

#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <set>

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

TEST_CASE("NPC garments: displays get pieces, a display drawing nothing makes no garment") {
    canon::Item boots = item(30, "feet", {6, 7}, {});
    boots.geosetGroups = {1, 0, 0, 0, 0, 0};
    canon::Item nothing = item(31, "", {}, {});
    canon::Garments out;
    canon::garments::NpcGarments npc(inputs(true), out);
    std::vector<uint32_t> worn = npc.wear({{30, 6, std::string("feet"), &boots}, {31, 11, std::nullopt, &nothing}});
    REQUIRE(worn.size() == 1);
    CHECK(out.garments[worn[0]].kind == "boots");
    CHECK(geosets(out.garments[worn[0]]) == std::vector<uint32_t>{502});
    CHECK(overlays(out.garments[worn[0]]) == std::vector<std::pair<uint32_t, bool>>{{6, false}, {7, false}});
    REQUIRE(out.sourceItems.size() == 2);
    CHECK(out.sourceItems[1].itemSlot == 11);
    CHECK(out.sourceItems[1].garments.empty());
    // A second NPC in the same boots shares the garment.
    CHECK(npc.wear({{30, 6, std::string("feet"), &boots}}) == worn);
    CHECK(out.garments.size() == 1);
}

TEST_CASE("NPC garments: a legs hem joins the NPC's own tunic, else its own hem garment is worn") {
    canon::Item tunic = item(40, "chest", {0, 3}, {});
    canon::Item robe = item(41, "chest", {3}, {});
    robe.geosetGroups = {0, 0, 1, 0, 0, 0};
    canon::Item legs = item(42, "legs", {5}, {});
    legs.geosetGroups = {1, 0, 0, 0, 0, 0};
    canon::Garments out;
    canon::garments::NpcGarments npc(inputs(true), out);

    std::vector<uint32_t> inTunic = npc.wear({{40, 3, std::string("chest"), &tunic}, {42, 5, std::string("legs"), &legs}});
    REQUIRE(inTunic.size() == 2);
    const canon::Garment& joined = out.garments[inTunic[1]];
    CHECK(joined.kind == "tunic");
    CHECK(geosets(joined) == std::vector<uint32_t>{1102});
    CHECK(overlays(joined) == std::vector<std::pair<uint32_t, bool>>{{0, false}, {3, false}, {5, true}});
    CHECK(out.garments[inTunic[0]].kind == "leggings");

    std::vector<uint32_t> inRobe = npc.wear({{41, 3, std::string("chest"), &robe}, {42, 5, std::string("legs"), &legs}});
    std::vector<std::string> kinds;
    for (uint32_t g : inRobe) kinds.push_back(out.garments[g].kind);
    CHECK(kinds == std::vector<std::string>{"leggings", "hem", "robe"});

    // The legs item alone is its leggings and its own hem; the tunic item alone is the plain tunic.
    CHECK(out.sourceItems[1].garments.size() == 2);
    CHECK(out.garments[out.sourceItems[0].garments[0]].pieces.size() == 2);
    // The same chest and legs on another NPC reuse the joined tunic.
    size_t count = out.garments.size();
    CHECK(npc.wear({{40, 3, std::string("chest"), &tunic}, {42, 5, std::string("legs"), &legs}}) == inTunic);
    CHECK(out.garments.size() == count);
    CHECK(joined.order == out.garments[out.sourceItems[0].garments[0]].order);
}

TEST_CASE("NPC garments: one display worn in two slots is split once per slot") {
    canon::Item shared = item(60, "", {3, 4}, {});
    canon::Garments out;
    canon::garments::NpcGarments npc(inputs(true), out);
    std::vector<uint32_t> worn = npc.wear({{60, 2, std::string("shirt"), &shared}, {60, 3, std::string("chest"), &shared}});
    REQUIRE(worn.size() == 2);
    CHECK(out.garments[worn[0]].kind == "shirt");
    CHECK(out.garments[worn[1]].kind == "tunic");
    CHECK(out.garments[worn[0]].order < out.garments[worn[1]].order);
    REQUIRE(out.sourceItems.size() == 2);
    CHECK(out.sourceItems[0].key != out.sourceItems[1].key);
}

TEST_CASE("garments: source items stack by their slot's draw rank, so no two of an outfit share layer and order") {
    canon::Item shirt = item(50, "shirt", {0, 5}, {});
    canon::Item robe = item(51, "chest", {0, 1, 5, 6}, {});
    robe.geosetGroups = {0, 0, 1, 0, 0, 0};
    canon::Item legs = item(52, "legs", {5, 6}, {});
    legs.geosetGroups = {1, 0, 1, 0, 0, 0};  // hem 1102, skirt 1302
    canon::Item belt = item(53, "waist", {4}, {});
    canon::Item tabard = item(54, "tabard", {3, 4}, {});
    canon::Item bracers = item(55, "wrist", {1}, {});
    canon::Item gloves = item(56, "hands", {1, 2}, {});
    canon::Item boots = item(57, "feet", {6, 7}, {});
    canon::Garments out;
    canon::garments::NpcGarments npc(inputs(true), out);
    std::vector<uint32_t> worn = npc.wear({{50, 4, std::string("shirt"), &shirt},   {51, 3, std::string("chest"), &robe},
                                           {52, 5, std::string("legs"), &legs},     {53, 6, std::string("waist"), &belt},
                                           {54, 19, std::string("tabard"), &tabard}, {55, 9, std::string("wrist"), &bracers},
                                           {56, 10, std::string("hands"), &gloves}, {57, 8, std::string("feet"), &boots}});
    std::map<std::string, std::pair<std::string, int32_t>> stack;
    std::set<std::pair<std::string, int32_t>> distinct;
    for (uint32_t g : worn) {
        const canon::Garment& garment = out.garments[g];
        CHECK(garment.layerRule == "source_slot_priority");
        stack[garment.kind] = {garment.layer, garment.order};
        distinct.insert({garment.layer, garment.order});
    }
    CHECK(stack["shirt"] == std::pair<std::string, int32_t>{"base", 1});
    CHECK(stack["leggings"] == std::pair<std::string, int32_t>{"base", 2});
    CHECK(stack["skirt"] == std::pair<std::string, int32_t>{"mid", 2});
    CHECK(stack["boots"] == std::pair<std::string, int32_t>{"mid", 4});
    CHECK(stack["robe"] == std::pair<std::string, int32_t>{"mid", 6});
    CHECK(stack["tabard"] == std::pair<std::string, int32_t>{"outer", 7});
    CHECK(stack["belt"] == std::pair<std::string, int32_t>{"outer", 8});
    CHECK(stack["bracers"] == std::pair<std::string, int32_t>{"outer", 9});
    CHECK(stack["gloves"] == std::pair<std::string, int32_t>{"outer", 10});
    // The legs' own hem shares the skirt's stack, but not its coverage (thigh, not skirt).
    CHECK(stack["hem"] == std::pair<std::string, int32_t>{"mid", 2});
    CHECK(distinct.size() == worn.size() - 1);
}
