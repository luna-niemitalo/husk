#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "canon_garment.hpp"
#include "canon_item.hpp"

// husk::canon: the stage-1 garment split (REFACTOR/GARMENTS.md "How husk
// decomposes WoW items") over already-resolved source items. Pure: the
// caller supplies textures by FileDataID and the body hem's mask.
namespace husk::canon::garments {

// The canonical vocabulary. Region names for SectionType 0-7 (community
// names, agreeing with the body footprints, WIKI_FINDINGS/GARMENTS.md
// "Region rectangles"); `section_<n>` for the rest.
std::string regionName(uint32_t sectionType);
// Body part names for geoset groups WIKI_FINDINGS/M2.md's table reads
// visually; `group_<n>` for every other group.
std::string partName(uint32_t group);
// The default layer of a preset kind; empty for a kind off the list.
std::string defaultLayer(const std::string& kind);
// Every preset kind with its default layer, in schema order
// (schema/garments.schema.json `x-preset-kinds` is its counterpart).
const std::vector<std::pair<std::string, std::string>>& presetKinds();
// Layers bottom up.
const std::vector<std::string>& layers();

// The body hem geoset a legs item switches on with GeosetGroup[0] = 1.
constexpr uint32_t kBodyHemGeoset = 1102;

struct Inputs {
    // A texture by FileDataID (0 = none picked; `unresolved` says why).
    std::function<TextureRef(uint32_t fileDataId, const std::string& unresolved)> texture;
    // An item model's M2 texture type as its slot name (rule texture_slot_name).
    std::function<std::string(uint32_t textureType)> textureRole;
    // gear[i]'s model m's exported bundle manifest, relative; empty = none.
    std::vector<std::vector<std::string>> modelUris;
    // The body hem's UV footprint inside the LegUpper region, as a mask
    // texture (rule hem_uv_footprint); nullopt = the body mesh or layout
    // isn't known, and the hem takes no paint.
    std::optional<TextureRef> bodyHemMask;
};

// --appearance gear as garments, appended to `out`; returns the garment
// indices the gear wears, each once, in garment order.
std::vector<uint32_t> fromGear(const std::vector<Item>& gear, const Inputs& inputs, Garments& out);

// One NPC equipment row. `item` is its display resolved (assembleDisplayItem);
// nullptr when the item tables aren't loaded.
struct NpcItem {
    uint32_t itemDisplayInfo = 0;
    uint32_t itemSlot = 0;
    std::optional<std::string> slot;  // the slot's name; nullopt for a slot with none
    const Item* item = nullptr;
};

// NPC equipment as garments, each display split once per slot it is worn in
// (its source item's garments are the display worn alone in that slot) and
// shared by every NPC wearing it there.
// The hem rule is decided per NPC: see wear().
class NpcGarments {
public:
    NpcGarments(Inputs inputs, Garments& out) : inputs_(std::move(inputs)), out_(out) {}

    // One NPC's equipment; returns the garments it wears, each once, in
    // garment order. A display that draws nothing (no model, overlay,
    // texture or geoset) makes no garment. A legs hem joins the NPC's own
    // chest garment when that is a tunic (one garment per chest and legs
    // display, rule hem_join_outfit; an item-model hem mesh the chest
    // already shows is not repeated), else the legs' own `hem` garment is
    // worn.
    std::vector<uint32_t> wear(const std::vector<NpcItem>& equipment);

private:
    struct Split {
        std::vector<uint32_t> garments;  // the display alone, without its hem garment
        std::optional<uint32_t> main;
        std::optional<uint32_t> hem;     // the legs' own hem garment
        std::string slot;
    };
    const Split& split(const NpcItem& npcItem);

    using DisplayInSlot = std::pair<uint32_t, uint32_t>;  // ItemDisplayInfo, NPC ItemSlot
    Inputs inputs_;
    Garments& out_;
    std::map<DisplayInSlot, Split> byDisplay_;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> joined_;  // (chest, legs display) -> tunic with the hem
};

}  // namespace husk::canon::garments
