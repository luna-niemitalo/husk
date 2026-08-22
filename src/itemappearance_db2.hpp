#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// Real, typed reader for the DB2 chain that resolves a real
// ItemModifiedAppearanceID (what husk-appearance/1's `gear=SLOT:id` entries
// carry, src/appearance_string.hpp) to the real equipped-item geometry/
// texture data needed to render it -- TODO/CHAR_TEXTURE_COMPOSITING_TODO.md
// Stage 6, TODO/EQUIPPED_GEAR_RENDER_TODO.md step 1. Same db2table.hpp-backed,
// thin-wrapper, "data access only" pattern as chrcustomization_db2.hpp --
// final FileDataID resolution (ModelResourcesID -> .m2 FileDataID,
// MaterialResourcesID -> texture FileDataID) is left to
// modelfiledata_db2.hpp/texturefiledata_db2.hpp, same split
// chrcustomization_db2.hpp already draws for its own material chain.
//
// The real chain, confirmed against the current local extraction's own
// live layout hashes (all tables' current LAYOUT verified against
// reference/WoWDBDefs, cross-checked against cmd_db2_build.cpp's own
// already-shipped, wow.export-matching SQL for the adjacent object-skin
// problem -- see that file's own doc comment for the "NOT
// ModelMaterialResourcesID_0/1 directly" correctness note, which applies
// here too):
//
//   ItemModifiedAppearanceID
//     -> ItemModifiedAppearance.ItemAppearanceID -> ItemAppearance
//          .ItemDisplayInfoID -> ItemDisplayInfo
//     -> ItemDisplayInfo.ModelResourcesID (element 0 of the real 2-entry
//        array field -- same "_0 only, never an OR across both slots"
//        convention cmd_db2_build.cpp's own comment documents for this
//        exact field)
//
// From ItemDisplayInfoID, TWO real, structurally different sibling tables
// branch off -- EQUIPPED_GEAR_RENDER_TODO.md's own "case 1 vs case 2"
// split, confirmed against reference/wow.export's real, shipping renderer
// code (src/js/db/caches/DBItemDisplayInfoModelMatRes.js vs.
// DBItemCharTextures.js, src/js/modules/tab_characters.js's own equipped-
// item compositing loop), not guessed from column names alone:
//
//   - ItemDisplayInfoModelMatRes rows keyed by ItemDisplayInfoID, each
//     carrying a real (MaterialResourcesID, TextureType, ModelIndex)
//     triple -- this is case 1's texture data (the texture applied to the
//     item's OWN separate geometry, e.g. a weapon's blade texture or a
//     shield's own material). `TextureType`'s real distinct local values
//     ({2,3,4,5,24}) rule out any body-section-enum reading (falsified,
//     see EQUIPPED_GEAR_RENDER_TODO.md) -- it's a texture-*role* selector
//     (diffuse/detail/specular-ish channel), matching that
//     DBItemDisplayInfoModelMatRes.js never filters on it at all, just
//     collects every FileDataID reachable via MaterialResourcesID. One
//     ItemDisplayInfoID can own several of these, kept as a vector rather
//     than picking one, same policy as modelfiledata_db2.hpp's own
//     ModelResourcesID ambiguity.
//   - ItemDisplayInfoMaterialRes (a DIFFERENT, sibling real table -- note
//     "Material", not "Model" -- confirmed as a real, separate,
//     230k-row-local table, not a typo/alias for ModelMatRes above) rows
//     keyed by ItemDisplayInfoID, each carrying a real (ComponentSection,
//     MaterialResourcesID) pair. This is case 2's texture data (the
//     texture recolors/overlays a specific section of the BASE
//     CHARACTER's own mesh -- most body armor). `ComponentSection`'s real
//     local values are exactly {0..8}, matching
//     reference/wow.export's own shipping `COMPONENT_SECTION` enum
//     (ARM_UPPER=0, ARM_LOWER=1, HAND=2, TORSO_UPPER=3, TORSO_LOWER=4,
//     LEG_UPPER=5, LEG_LOWER=6, FOOT=7, ACCESSORY=8) -- a real subset of
//     CharComponentTextureSections.SectionType's own broader {0..14} local
//     range (that table also covers non-item sections, e.g. face/scalp).
//     Real multi-section items exist locally (e.g. a boots-shaped
//     ItemDisplayInfoID carrying both LEG_LOWER=6 and FOOT=7 rows, each
//     its own MaterialResourcesID) -- kept as a vector, same policy as
//     ModelMatRes above, never collapsed to one.
//
// `ChrModelTextureTargetID` (the real join key the player-customization
// chain uses to select a `chr_texture_layout` section,
// chrmodel_db2.hpp's ChrModelTextureLayer/chrcustomization_db2.hpp's
// ChrCustomizationMaterial) is NOT reachable from this chain -- confirmed
// absent from every column of ItemDisplayInfo/ItemDisplayInfoModelMatRes/
// ItemDisplayInfoMaterialRes in the current local WoWDBDefs layout, and a
// real MaterialResourcesID-namespace join against ChrCustomizationMaterial
// returns only 2 matches out of 35,706 distinct local
// ItemDisplayInfoModelMatRes.MaterialResourcesID values -- noise-level,
// not a real shared key. ComponentSection (above) is the real section
// signal for case 2, not ChrModelTextureTargetID.
namespace husk::itemappearance {

struct ModifiedAppearance {
    uint32_t id = 0;
    uint32_t itemAppearanceId = 0;
};

struct Appearance {
    uint32_t id = 0;
    uint32_t itemDisplayInfoId = 0;
};

struct DisplayInfo {
    uint32_t id = 0;
    uint32_t modelResourcesId = 0;  // ModelResourcesID_0 only, see this file's own doc comment
};

// One ItemDisplayInfoModelMatRes row's relevant fields -- case 1 (the
// item's own standalone-geometry texture), see this file's own doc comment.
struct ModelMatRes {
    uint32_t itemDisplayInfoId = 0;
    uint32_t materialResourcesId = 0;
    uint32_t textureType = 0;
    uint32_t modelIndex = 0;
};

// One ItemDisplayInfoMaterialRes row's relevant fields -- case 2 (a
// base-character-mesh section overlay), see this file's own doc comment.
// A DIFFERENT, sibling real table from ModelMatRes above, not a duplicate.
struct MaterialRes {
    uint32_t itemDisplayInfoId = 0;
    uint32_t componentSection = 0;    // real CharComponentTextureSections.SectionType-space value, {0..8} for items
    uint32_t materialResourcesId = 0;
};

struct Data {
    std::vector<ModifiedAppearance> modifiedAppearances;
    std::vector<Appearance> appearances;
    std::vector<DisplayInfo> displayInfos;
    std::vector<ModelMatRes> modelMatRes;
    std::vector<MaterialRes> materialRes;
};

// Loads all five tables from `db2Dir` (itemmodifiedappearance.db2/
// itemappearance.db2/itemdisplayinfo.db2/itemdisplayinfomodelmatres.db2/
// itemdisplayinfomaterialres.db2, real lowercase casc-tool filenames).
// Returns nullopt only if every table came back empty.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// One real ItemModifiedAppearanceID, resolved -- `materials` (case 1) kept
// alongside their own textureType/modelIndex so the caller can tell a
// weapon's base texture apart from a secondary glow layer;
// `sectionMaterials` (case 2) kept alongside their own componentSection so
// the caller knows which base-character-mesh section each texture
// overlays. Both are "resolve, don't interpret" data, same split as
// chrcustomization::MaterialResolution -- a real item can carry either,
// both, or neither (e.g. a pure recolor item carries only sectionMaterials).
struct Resolution {
    std::optional<uint32_t> itemDisplayInfoId;
    std::optional<uint32_t> modelResourcesId;  // feed to modelfiledata::Data for the real .m2 FileDataID(s)
    std::vector<ModelMatRes> materials;        // feed materialResourcesId to texturefiledata::Data per entry
    std::vector<MaterialRes> sectionMaterials;  // feed materialResourcesId to texturefiledata::Data per entry
};

// Resolves one real ItemModifiedAppearanceID against already-loaded `data`.
// A dangling reference at any hop (an ItemAppearanceID/ItemDisplayInfoID
// that matches no row) is reported to `err` and stops that hop's own
// field(s) from being filled -- never fabricated, same convention as
// chrcustomization::resolveChoice.
Resolution resolve(const Data& data, uint32_t itemModifiedAppearanceId, std::ostream& err);

}  // namespace husk::itemappearance
