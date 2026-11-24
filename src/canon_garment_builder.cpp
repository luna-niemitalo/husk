#include "canon_garment_builder.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <utility>

namespace husk::canon::garments {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The body geoset group each GeosetGroup index drives, per slot (WIKI_FINDINGS/
// GARMENTS.md "Slot → GeosetGroup table", ref WMV WoWItem.cpp via wow.export
// DBItemGeosets.js); the item model's AttachmentGeosetGroup uses the same
// groups (wow.export SLOT_TO_GEOSET_GROUPS, "Item models").
const std::map<std::string, std::vector<uint32_t>>& slotGroups() {
    static const std::map<std::string, std::vector<uint32_t>> kGroups = {
        {"head", {27, 21}},   {"shoulder", {26}}, {"shirt", {8, 10}}, {"chest", {8, 10, 13, 22, 28}},
        {"waist", {18}},      {"legs", {11, 9, 13}}, {"feet", {5, 20}}, {"hands", {4, 23}},
        {"back", {15}},       {"tabard", {12}},
    };
    return kGroups;
}

// Slots whose item models hang rigidly at an attachment point; every other
// slot's model is a collection model skinned to the body.
struct Attachment {
    const char* point;
    uint32_t id;
};
std::optional<Attachment> attachmentFor(const std::string& slot, uint32_t position) {
    if (slot == "head") return Attachment{"head", 11};
    if (slot == "shoulder") return position == 0 ? Attachment{"shoulder_left", 6} : Attachment{"shoulder_right", 5};
    if (slot == "main_hand" || slot == "ranged") return Attachment{"hand_right", 1};
    if (slot == "off_hand") return Attachment{"hand_left", 2};
    return std::nullopt;
}

std::string kindFor(const std::string& slot, const Item* item) {
    static const std::map<std::string, std::string> kBySlot = {
        {"shirt", "shirt"}, {"legs", "leggings"}, {"waist", "belt"}, {"feet", "boots"}, {"wrist", "bracers"},
        {"hands", "gloves"}, {"back", "cloak"}, {"tabard", "tabard"}, {"head", "helm"}, {"shoulder", "pauldrons"},
        {"main_hand", "weapon"}, {"off_hand", "weapon"}, {"ranged", "weapon"}, {"neck", "necklace"},
    };
    if (slot == "chest") return item && item->geosetGroups[2] > 0 ? "robe" : "tunic";
    auto it = kBySlot.find(slot);
    return it == kBySlot.end() ? "item" : it->second;
}

bool sharesSet(const Item& a, const Item& b) {
    return std::any_of(a.transmogSets.begin(), a.transmogSets.end(), [&](uint32_t s) {
        return std::find(b.transmogSets.begin(), b.transmogSets.end(), s) != b.transmogSets.end();
    });
}

std::string itemKey(const Item& item) {
    const auto& row = std::get<Db2Row>(item.identity.id);
    return "source_item/db2_row/" + row.table + "/" + std::to_string(row.row);
}

// The slot's rank in the body composite's draw order, lowest drawn first
// (REFACTOR/GARMENTS.md "Source slot priority"): wow.export EquipmentSlots.js
// SLOT_LAYER, its ties in slot-id order as its stable sort leaves them.
std::optional<int32_t> slotRank(const std::string& slot) {
    static const std::map<std::string, int32_t> kRanks = {
        {"shirt", 1}, {"legs", 2},  {"head", 3},   {"feet", 4},       {"shoulder", 5}, {"chest", 6},
        {"tabard", 7}, {"waist", 8}, {"wrist", 9}, {"hands", 10},     {"main_hand", 11}, {"ranged", 11},
        {"off_hand", 12}, {"back", 13},
    };
    auto it = kRanks.find(slot);
    if (it == kRanks.end()) return std::nullopt;
    return it->second;
}

// `slot`: the source item's slot the garment was cut from.
uint32_t addGarment(Garments& out, std::string key, std::string kind, const std::string& slot) {
    Garment g;
    g.key = std::move(key);
    g.layer = defaultLayer(kind);
    // An unnamed source slot (NPC ItemSlot 11) gives no kind to stand on;
    // declared, so the validator reports it as known.
    g.deviates = g.layer.empty() && kind == "item";
    if (auto rank = slotRank(slot); rank && !g.layer.empty()) {
        g.order = *rank;
        g.layerRule = "source_slot_priority";
        // The feet slot draws its LegLower paint under the chest's; `outer` would put it over.
        if (slot == "feet") g.layer = "mid";
    }
    g.kind = std::move(kind);
    out.garments.push_back(std::move(g));
    return static_cast<uint32_t>(out.garments.size() - 1);
}

// One item's stage-1 split: its main garment, a legs skirt garment, and
// the hem pieces held back for the hem rule.
struct ItemSplit {
    uint32_t main = 0;
    std::optional<uint32_t> skirt;
    std::vector<GarmentPiece> hem;
};

// `itemHemOwner`: item-model hem pieces already placed, by model FileDataID
// and geoset, for gear worn together (a set's chest and legs often show the
// same mesh, and it is one piece); nullptr splits the item on its own.
ItemSplit splitItem(const Item& item, const std::string& slot, const std::string& key, uint32_t sourceIndex,
                    const Inputs& inputs, const std::vector<std::string>* modelUris, Garments& out,
                    std::map<std::pair<uint32_t, uint32_t>, uint32_t>* itemHemOwner) {
    ItemSplit split;
    std::string kind = kindFor(slot, &item);
    split.main = addGarment(out, key + "/" + kind, kind, slot);
    uint32_t garment = split.main;
    auto add = [&](uint32_t g, PieceKind what, std::string derived = "") {
        out.garments[g].pieces.push_back({std::move(what), sourceIndex, std::move(derived)});
    };

    const SectionOverlayComponent* overlays = nullptr;
    const GeometryComponent* models = nullptr;
    for (const Component& c : item.components) {
        if (const auto* o = std::get_if<SectionOverlayComponent>(&c)) overlays = o;
        if (const auto* m = std::get_if<GeometryComponent>(&c)) models = m;
    }

    auto groups = slotGroups().find(slot);
    if (groups != slotGroups().end()) {
        for (uint32_t index = 0; index < groups->second.size(); ++index) {
            int32_t value = item.geosetGroups[index];
            if (value <= 0) continue;
            uint32_t group = groups->second[index];
            GeometryPiece piece{partName(group), group, 1 + static_cast<uint32_t>(value), index, value};
            if (slot == "legs" && group * 100 + piece.variant == kBodyHemGeoset) {
                split.hem.push_back({piece, sourceIndex, "hem_body_geoset"});
            } else if (slot == "legs" && group == 13) {
                if (!split.skirt) split.skirt = addGarment(out, key + "/skirt", "skirt", slot);
                add(*split.skirt, piece);
            } else {
                add(garment, piece);
            }
        }
    }
    bool bodyHem = !split.hem.empty();
    if (overlays) {
        for (const auto& s : overlays->sections) {
            OverlayPiece piece{regionName(s.componentSection), s.componentSection,
                               inputs.texture(s.fileDataId, s.unresolved), std::nullopt};
            if (bodyHem && s.componentSection == 5 && inputs.bodyHemMask) {
                OverlayPiece hemPaint = piece;
                hemPaint.mask = inputs.bodyHemMask;
                split.hem.push_back({hemPaint, sourceIndex, "hem_uv_footprint"});
            }
            add(garment, std::move(piece));
        }
    }
    if (!models) return split;
    for (size_t m = 0; m < models->models.size(); ++m) {
        const auto& model = models->models[m];
        PieceModel pm;
        if (model.fileDataId) {
            pm.ref = Ref{FileDataId{*model.fileDataId}, "", NameSource::None};
            if (modelUris && m < modelUris->size()) pm.uri = (*modelUris)[m];
        } else {
            pm.unresolved = model.unresolved;
        }
        std::vector<PieceTexture> textures;
        for (const auto& t : models->materials) {
            if (t.modelIndex != model.position) continue;
            textures.push_back({inputs.textureRole(t.textureType), t.textureType, t.modelIndex,
                                inputs.texture(t.fileDataId, t.unresolved)});
        }
        if (auto point = attachmentFor(slot, model.position)) {
            add(garment, AttachedPiece{point->point, point->id, model.position, pm, textures});
            continue;
        }
        SkinnedPiece body{model.position, pm, textures, {}};
        std::vector<uint32_t> hemGeosets;
        if (groups != slotGroups().end()) {
            for (uint32_t index = 0; index < groups->second.size(); ++index) {
                int32_t value = item.attachmentGeosetGroups[index];
                if (value < 0) continue;
                uint32_t id = groups->second[index] * 100 + 1 + static_cast<uint32_t>(value);
                bool hemMesh = groups->second[index] == 13 && (slot == "chest" || slot == "legs");
                (hemMesh ? hemGeosets : body.shownGeosets).push_back(id);
            }
        }
        add(garment, body);
        for (uint32_t id : hemGeosets) {
            SkinnedPiece hemPiece{model.position, pm, textures, {id}};
            uint32_t fdid = model.fileDataId.value_or(0);
            if (itemHemOwner && fdid != 0) {
                auto owner = itemHemOwner->find({fdid, id});
                if (owner != itemHemOwner->end()) {
                    out.sourceItems[sourceIndex].garments.push_back(owner->second);
                    continue;
                }
            }
            if (slot == "chest") {
                add(garment, hemPiece, "item_model_hem");
                if (itemHemOwner && fdid != 0) (*itemHemOwner)[{fdid, id}] = garment;
            } else {
                split.hem.push_back({hemPiece, sourceIndex, "item_model_hem"});
                if (itemHemOwner && fdid != 0) (*itemHemOwner)[{fdid, id}] = UINT32_MAX;
            }
        }
    }
    return split;
}

// The model file and geosets of a skinned piece, the identity of an
// item-model hem mesh.
std::optional<std::pair<uint32_t, std::vector<uint32_t>>> skinnedMesh(const GarmentPiece& piece) {
    const auto* s = std::get_if<SkinnedPiece>(&piece.what);
    if (!s || !s->model.ref) return std::nullopt;
    return std::pair{std::get<FileDataId>(s->model.ref->id).value, s->shownGeosets};
}

}  // namespace

std::string regionName(uint32_t sectionType) {
    static const std::array<const char*, 8> kNames = {"arm_upper",   "arm_lower",   "hand",      "torso_upper",
                                                      "torso_lower", "leg_upper",   "leg_lower", "foot"};
    return sectionType < kNames.size() ? kNames[sectionType] : "section_" + std::to_string(sectionType);
}

std::string partName(uint32_t group) {
    static const std::map<uint32_t, const char*> kNames = {
        {0, "hair"},  {4, "glove_cuff"}, {5, "boot_shaft"}, {7, "ears"},  {8, "sleeve"},     {9, "knee"},
        {11, "thigh"}, {12, "tabard"},   {13, "skirt"},     {15, "cloak"}, {17, "eye_glow"}, {18, "belt"},
        {20, "boot_foot"}, {22, "torso"}, {35, "earrings"}, {36, "necklace"}, {39, "arm_band"},
    };
    auto it = kNames.find(group);
    return it != kNames.end() ? it->second : "group_" + std::to_string(group);
}

const std::vector<std::string>& layers() {
    static const std::vector<std::string> kLayers = {"skin", "underwear", "base", "mid", "outer", "over"};
    return kLayers;
}

const std::vector<std::pair<std::string, std::string>>& presetKinds() {
    static const std::vector<std::pair<std::string, std::string>> kKinds = {
        {"shirt", "base"},    {"leggings", "base"}, {"socks", "base"},    {"tunic", "mid"},    {"robe", "mid"},
        {"dress", "mid"},     {"skirt", "mid"},     {"trousers", "mid"},  {"overalls", "mid"}, {"hem", "mid"},
        {"tabard", "outer"},  {"belt", "outer"},    {"bracers", "outer"}, {"gloves", "outer"}, {"boots", "outer"},
        {"cloak", "over"},    {"pauldrons", "over"}, {"helm", "over"},    {"weapon", "over"},  {"necklace", "over"},
    };
    return kKinds;
}

std::string defaultLayer(const std::string& kind) {
    for (const auto& [k, layer] : presetKinds()) {
        if (k == kind) return layer;
    }
    return "";
}

const NpcGarments::Split& NpcGarments::split(const NpcItem& npcItem) {
    // NPCs wear one display in several slots (3454 in seven); the slot decides
    // the split (geoset groups, kind, draw rank), so it is part of the identity.
    auto [it, added] = byDisplay_.try_emplace({npcItem.itemDisplayInfo, npcItem.itemSlot});
    Split& split = it->second;
    if (!added) return split;
    split.slot = npcItem.slot.value_or("");

    std::string key = "source_item/db2_row/ItemDisplayInfo/" + std::to_string(npcItem.itemDisplayInfo) + "/slot/" +
                      std::to_string(npcItem.itemSlot);
    uint32_t sourceIndex = static_cast<uint32_t>(out_.sourceItems.size());
    SourceItem source;
    source.key = key;
    source.ref.id = Db2Row{"ItemDisplayInfo", npcItem.itemDisplayInfo};
    source.slot = npcItem.slot;
    source.itemSlot = npcItem.itemSlot;
    source.itemDisplayInfo = npcItem.itemDisplayInfo;
    out_.sourceItems.push_back(std::move(source));

    if (!npcItem.item) {
        // Unresolved: the display's identity only, as a garment of the slot's kind.
        split.main = addGarment(out_, key + "/garment", npcItem.slot ? kindFor(*npcItem.slot, nullptr) : "item", split.slot);
        split.garments = {*split.main};
        out_.sourceItems[sourceIndex].garments = split.garments;
        return split;
    }

    ItemSplit parts = splitItem(*npcItem.item, split.slot, key, sourceIndex, inputs_, nullptr, out_, nullptr);
    bool drawsNothing = out_.garments[parts.main].pieces.empty() && !parts.skirt && parts.hem.empty();
    if (drawsNothing) {
        out_.garments.pop_back();
        return split;
    }
    split.main = parts.main;
    split.garments.push_back(parts.main);
    if (parts.skirt) split.garments.push_back(*parts.skirt);
    if (!parts.hem.empty()) {
        split.hem = addGarment(out_, key + "/hem", "hem", split.slot);
        for (GarmentPiece& piece : parts.hem) {
            if (piece.derived != "hem_uv_footprint") piece.derived = "hem_own_garment";
            out_.garments[*split.hem].pieces.push_back(std::move(piece));
        }
    }
    auto& listed = out_.sourceItems[sourceIndex].garments;
    listed = split.garments;
    if (split.hem) listed.push_back(*split.hem);
    return split;
}

std::vector<uint32_t> NpcGarments::wear(const std::vector<NpcItem>& equipment) {
    std::set<uint32_t> worn;
    std::optional<DisplayInSlot> chestDisplay;
    std::vector<DisplayInSlot> legsDisplays;
    for (const NpcItem& npcItem : equipment) {
        const Split& s = split(npcItem);
        worn.insert(s.garments.begin(), s.garments.end());
        DisplayInSlot at{npcItem.itemDisplayInfo, npcItem.itemSlot};
        if (s.slot == "chest" && !chestDisplay && s.main) chestDisplay = at;
        if (s.hem) legsDisplays.push_back(at);
    }
    std::optional<uint32_t> tunic;
    if (chestDisplay && out_.garments[*byDisplay_.at(*chestDisplay).main].kind == "tunic") {
        tunic = byDisplay_.at(*chestDisplay).main;
    }
    for (const DisplayInSlot& legsAt : legsDisplays) {
        uint32_t legs = legsAt.first;
        uint32_t ownHem = *byDisplay_.at(legsAt).hem;
        if (!tunic) {
            worn.insert(ownHem);
            continue;
        }
        auto [it, added] = joined_.try_emplace({chestDisplay->first, legs}, 0);
        if (added) {
            const Garment& chest = out_.garments[*tunic];
            Garment joined;
            joined.key = chest.key + "/hem/" + std::to_string(legs);
            joined.kind = chest.kind;
            joined.layer = chest.layer;
            joined.order = chest.order;
            joined.layerRule = chest.layerRule;
            joined.pieces = chest.pieces;
            std::set<std::pair<uint32_t, std::vector<uint32_t>>> shown;
            for (const GarmentPiece& piece : chest.pieces) {
                if (auto mesh = skinnedMesh(piece)) shown.insert(*mesh);
            }
            for (GarmentPiece piece : out_.garments[ownHem].pieces) {
                if (auto mesh = skinnedMesh(piece); mesh && shown.count(*mesh)) continue;
                if (piece.derived != "hem_uv_footprint") piece.derived = "hem_join_outfit";
                joined.pieces.push_back(std::move(piece));
            }
            out_.garments.push_back(std::move(joined));
            it->second = static_cast<uint32_t>(out_.garments.size() - 1);
        }
        worn.erase(*tunic);
        worn.insert(it->second);
    }
    return {worn.begin(), worn.end()};
}

std::vector<uint32_t> fromGear(const std::vector<Item>& gear, const Inputs& inputs, Garments& out) {
    // Pieces held back for the hem split, owned by a legs item until a tunic of its set claims them.
    struct PendingHem {
        size_t gearIndex;
        uint32_t sourceItem;
        std::vector<GarmentPiece> pieces;
    };
    std::vector<PendingHem> hems;
    // Item-model hem pieces already placed, by model FileDataID and geoset: a set's
    // chest and legs often show the same mesh, and it is one piece.
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> itemHemOwner;
    std::vector<uint32_t> mainGarment(gear.size());
    std::vector<std::string> slotOf(gear.size());
    std::vector<uint32_t> sourceOf(gear.size());

    // Chests first, so a hem shown by both a chest and its set's legs lands on the tunic.
    std::vector<size_t> order(gear.size());
    for (size_t i = 0; i < gear.size(); ++i) order[i] = i;
    for (size_t i = 0; i < gear.size(); ++i) slotOf[i] = gear[i].slots.empty() ? "" : lower(gear[i].slots.front());
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return (slotOf[a] == "chest") > (slotOf[b] == "chest"); });

    for (size_t i : order) {
        const Item& item = gear[i];
        const std::string& slot = slotOf[i];
        std::string key = itemKey(item);
        SourceItem source;
        source.key = key;
        source.ref = item.identity;
        source.itemDisplayInfo = item.itemDisplayInfoId;
        if (!item.slots.empty()) source.slotToken = item.slots.front();
        if (!slot.empty()) source.slot = slot;
        sourceOf[i] = static_cast<uint32_t>(out.sourceItems.size());
        out.sourceItems.push_back(std::move(source));
        const std::vector<std::string>* uris = i < inputs.modelUris.size() ? &inputs.modelUris[i] : nullptr;
        ItemSplit split = splitItem(item, slot, key, sourceOf[i], inputs, uris, out, &itemHemOwner);
        mainGarment[i] = split.main;
        if (!split.hem.empty()) hems.push_back({i, sourceOf[i], std::move(split.hem)});
    }

    // Hem split: a legs item's hem joins the tunic of its set, else is a hem garment of its own.
    for (PendingHem& hem : hems) {
        const Item& legs = gear[hem.gearIndex];
        std::optional<uint32_t> tunic;
        for (size_t j = 0; j < gear.size(); ++j) {
            if (slotOf[j] != "chest" || !sharesSet(gear[j], legs)) continue;
            if (out.garments[mainGarment[j]].kind == "tunic") tunic = mainGarment[j];
            break;
        }
        uint32_t target = tunic ? *tunic : addGarment(out, itemKey(legs) + "/hem", "hem", slotOf[hem.gearIndex]);
        for (GarmentPiece& piece : hem.pieces) {
            if (piece.derived != "hem_uv_footprint") piece.derived = tunic ? "hem_join_set" : "hem_own_garment";
            out.garments[target].pieces.push_back(std::move(piece));
        }
    }
    // Item-model hems deduplicated against a legs item's hem, whose owner is
    // known only now; the piece loop below links that source item.
    for (SourceItem& s : out.sourceItems) {
        s.garments.erase(std::remove(s.garments.begin(), s.garments.end(), UINT32_MAX), s.garments.end());
    }

    std::set<uint32_t> worn;
    for (uint32_t g = 0; g < out.garments.size(); ++g) {
        for (const GarmentPiece& piece : out.garments[g].pieces) {
            auto& list = out.sourceItems[piece.sourceItem].garments;
            if (std::find(list.begin(), list.end(), g) == list.end()) list.push_back(g);
        }
    }
    for (size_t i = 0; i < gear.size(); ++i) {
        auto& list = out.sourceItems[sourceOf[i]].garments;
        if (list.empty()) list.push_back(mainGarment[i]);
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        worn.insert(list.begin(), list.end());
    }
    std::vector<uint32_t> result(worn.begin(), worn.end());

    std::map<uint32_t, std::set<uint32_t>> bySet;
    for (size_t i = 0; i < gear.size(); ++i) {
        for (uint32_t set : gear[i].transmogSets) {
            const auto& list = out.sourceItems[sourceOf[i]].garments;
            bySet[set].insert(list.begin(), list.end());
        }
    }
    for (const auto& [set, garments] : bySet) {
        Outfit outfit;
        outfit.key = "outfit/db2_row/TransmogSet/" + std::to_string(set);
        outfit.source.id = Db2Row{"TransmogSet", set};
        outfit.garments.assign(garments.begin(), garments.end());
        out.outfits.push_back(std::move(outfit));
    }
    return result;
}

}  // namespace husk::canon::garments
