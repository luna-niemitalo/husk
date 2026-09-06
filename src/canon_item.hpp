#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's "Items by identity, not slot" (I7) -- a pure value
// type, no consumers wired to it yet (migrating gltf_skeleton.hpp's
// GearItem/GearSectionOverlay onto it is separate, later work).
namespace husk::canon {

// Slot is a vector, not an enum: appearance_string.hpp's own grammar
// defines SLOT as an opaque caller-chosen `[A-Z_]+` token -- husk
// deliberately does not hardcode Blizzard's equipment-slot enum -- so a
// fixed enum here would invent a closed set the domain doesn't have.
using Slot = std::string;

// Case 1 (today's GearItem): standalone geometry, its own separate `.m2`.
// modelFileDataIds is a vector because one ModelResourcesID can resolve to
// several real FileDataIDs (LOD variants), never collapsed to one -- same
// policy as itemappearance::Resolution::materials.
//
// GearItem::auxGlbPath is excluded: a written-.glb-relative-path is a
// writer transport artifact (I1), not a fact about the item.
struct GeometryComponent {
    std::vector<uint32_t> modelFileDataIds;

    // Real per-texture-role material, keyed by textureType (a texture
    // role, NOT a body section -- see itemappearance_db2.hpp's ModelMatRes
    // doc comment for why TextureType was falsified as a section-enum
    // reading), mirroring GearItem::Material verbatim.
    struct Material {
        uint32_t textureType = 0;
        uint32_t materialResourcesId = 0;
        uint32_t fileDataId = 0;
    };
    std::vector<Material> materials;
};

// Case 2 (today's GearSectionOverlay): a texture-layer overlay onto the
// base character's own body sections, no separate geometry of its own.
struct SectionOverlayComponent {
    struct Section {
        uint32_t componentSection = 0;  // real CharComponentTextureSections.SectionType-space value
        uint32_t materialResourcesId = 0;
        uint32_t fileDataId = 0;        // 0 = TextureFileData.db2 didn't resolve this MaterialResourcesID
    };
    std::vector<Section> sections;  // empty when this appearance carries no case-2 data at all
};

using Component = std::variant<GeometryComponent, SectionOverlayComponent>;

// One equipped item: an identity, the slot(s) it occupies, and the
// component(s) it contributes. Two parallel lists (GearItem vs.
// GearSectionOverlay) collapse to one item type carrying either/both
// component kinds -- an item that is both standalone geometry and a
// section overlay, or one that occupies more than one slot (a tunic whose
// hem is a leg-slot piece), is now structurally expressible.
struct Item {
    // ItemModifiedAppearanceID is a real ItemModifiedAppearance.db2 row
    // (itemappearance_db2.hpp) -- Db2Row{table,row} fits directly, same
    // as any other DB2-row identity under I6; no stronger typed wrapper
    // for "specifically an ItemModifiedAppearanceID" exists elsewhere in
    // this codebase to reuse instead.
    Ref identity;
    std::vector<Slot> slots;
    std::vector<Component> components;
};

}  // namespace husk::canon
