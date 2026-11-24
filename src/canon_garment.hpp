#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "canon_material.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// REFACTOR/GARMENTS.md: what a body wears, as garments a person would name;
// source items are packaging. A pure value type: no DB2 or file access.
namespace husk::canon {

// The body vocabulary garments are stated in (GARMENTS.md "Vocabulary").
// Regions are areas of the composited skin texture; their rectangles are
// the body layout's own (CharComponentTextureSections).
struct BodyRegion {
    std::string name;
    uint32_t sectionType = 0;
    uint32_t x = 0, y = 0, width = 0, height = 0;
};
struct BodyLayout {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<BodyRegion> regions;
};

// One body file a piece draws, picked for the wearer (component_file_pick).
// `ref` absent: nothing was picked, `unresolved` says why.
struct PieceModel {
    std::optional<Ref> ref;
    std::string uri;  // the model bundle's manifest, relative; empty = not exported
    std::string unresolved;
};
struct PieceTexture {
    std::string role;  // the item model's texture slot name (rule texture_slot_name)
    uint32_t textureType = 0;
    uint32_t modelPosition = 0;
    TextureRef texture;
};

struct OverlayPiece {
    std::string region;
    uint32_t section = 0;  // CharComponentTextureSections.SectionType: source only
    TextureRef texture;
    std::optional<TextureRef> mask;  // which of the texture's pixels are this garment's; absent = all
};
struct GeometryPiece {
    std::string part;
    uint32_t group = 0;
    uint32_t variant = 0;
    std::optional<uint32_t> slotIndex;  // ItemDisplayInfo.GeosetGroup index; absent for a body hem moved here
    int32_t value = 0;                  // that index's value as stored
};
struct AttachedPiece {
    std::string point;
    uint32_t attachmentId = 0;
    uint32_t position = 0;  // ItemDisplayInfo.ModelResourcesID index
    PieceModel model;
    std::vector<PieceTexture> textures;
};
struct SkinnedPiece {
    uint32_t position = 0;
    PieceModel model;
    std::vector<PieceTexture> textures;
    // The item model's geosets this piece shows, as raw ids (group * 100 + variant).
    std::vector<uint32_t> shownGeosets;
};
struct HidesPiece {
    std::string part;
    uint32_t group = 0;
};
using PieceKind = std::variant<OverlayPiece, GeometryPiece, AttachedPiece, SkinnedPiece, HidesPiece>;

struct GarmentPiece {
    PieceKind what;
    uint32_t sourceItem = 0;  // index into Garments::sourceItems
    std::string derived;      // the rule that put the piece in this garment; empty = garment_split
};

struct Garment {
    std::string key;  // stable-id key: source identity, never position
    Ref name;
    std::string kind;
    std::string layer;
    int32_t order = 0;
    bool deviates = false;
    std::vector<GarmentPiece> pieces;
};

struct SourceItem {
    std::string key;
    Ref ref;
    std::vector<uint32_t> garments;  // indices into Garments::garments
    std::optional<std::string> slot;
    std::optional<uint32_t> itemSlot;  // the source's slot number (NPC equipment)
    std::string slotToken;             // the source's own slot name (--appearance)
    std::optional<uint32_t> itemDisplayInfo;
};

struct Outfit {
    std::string key;
    Ref source;
    std::vector<uint32_t> garments;
};

struct Garments {
    std::vector<Garment> garments;
    std::vector<SourceItem> sourceItems;
    std::vector<Outfit> outfits;
};

}  // namespace husk::canon
