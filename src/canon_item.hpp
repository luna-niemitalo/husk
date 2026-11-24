#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's "Items by identity, not slot" (I7) -- a pure value
// type.
namespace husk::canon {

// Slot is a vector, not an enum: appearance_string.hpp's own grammar
// defines SLOT as an opaque caller-chosen `[A-Z_]+` token -- husk
// deliberately does not hardcode Blizzard's equipment-slot enum -- so a
// fixed enum here would invent a closed set the domain doesn't have.
using Slot = std::string;

// Case 1: standalone geometry, its own separate `.m2`s: one per
// ItemDisplayInfo.ModelResourcesID position, each the file the wearer's
// race and sex draw (component_file_db2.hpp). `fileDataId` absent:
// `unresolved` says why (unknown wearer, a tie, no fitting file).
//
// No exported-file path here: where a writer put the item's model is a
// writer transport artifact (I1), not a fact about the item.
struct GeometryComponent {
    struct Model {
        uint32_t position = 0;  // ModelResourcesID index; shoulders: 0 left, 1 right
        uint32_t modelResourcesId = 0;
        std::optional<uint32_t> fileDataId;
        std::string unresolved;
    };
    std::vector<Model> models;

    // Real per-texture-role material, keyed by textureType (a texture
    // role, NOT a body section -- see itemappearance_db2.hpp's ModelMatRes
    // doc comment for why TextureType was falsified as a section-enum
    // reading). `modelIndex`: the model position it textures.
    struct Material {
        uint32_t textureType = 0;
        uint32_t materialResourcesId = 0;
        uint32_t fileDataId = 0;  // 0 = not picked, see `unresolved`
        uint32_t modelIndex = 0;
        std::string unresolved;
    };
    std::vector<Material> materials;
};

// Case 2: a texture-layer overlay onto the
// base character's own body sections, no separate geometry of its own.
struct SectionOverlayComponent {
    struct Section {
        uint32_t componentSection = 0;  // real CharComponentTextureSections.SectionType-space value
        uint32_t materialResourcesId = 0;
        uint32_t fileDataId = 0;        // 0 = not picked, see `unresolved`
        std::string unresolved;
    };
    std::vector<Section> sections;  // empty when this appearance carries no case-2 data at all
};

using Component = std::variant<GeometryComponent, SectionOverlayComponent>;

// One equipped item: an identity, the slot(s) it occupies, and the
// component(s) it contributes. One item type carries either/both
// component kinds, so an item that is both standalone geometry and a
// section overlay, or one that occupies more than one slot (a tunic whose
// hem is a leg-slot piece), is structurally expressible.
struct Item {
    // ItemModifiedAppearanceID is a real ItemModifiedAppearance.db2 row
    // (itemappearance_db2.hpp) -- Db2Row{table,row} fits directly, same
    // as any other DB2-row identity under I6; no stronger typed wrapper
    // for "specifically an ItemModifiedAppearanceID" exists elsewhere in
    // this codebase to reuse instead.
    Ref identity;
    std::vector<Slot> slots;
    std::vector<Component> components;
    // What the display switches on (itemappearance::DisplayInfo), and the
    // transmog sets listing the appearance: the garment split's inputs.
    std::optional<uint32_t> itemDisplayInfoId;
    std::array<int32_t, 6> geosetGroups{};
    std::array<int32_t, 6> attachmentGeosetGroups{};
    std::vector<uint32_t> transmogSets;
};

}  // namespace husk::canon
