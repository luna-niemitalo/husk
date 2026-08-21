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
// Stage 6. Same db2table.hpp-backed, thin-wrapper, "data access only"
// pattern as chrcustomization_db2.hpp -- final FileDataID resolution
// (ModelResourcesID -> .m2 FileDataID, MaterialResourcesID -> texture
// FileDataID) is left to modelfiledata_db2.hpp/texturefiledata_db2.hpp,
// same split chrcustomization_db2.hpp already draws for its own material
// chain.
//
// The real chain, confirmed against the current local extraction's own
// live layout hashes (all 4 tables' current LAYOUT verified against
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
//     -> ItemDisplayInfoModelMatRes rows keyed by ItemDisplayInfoID, each
//        carrying a real (MaterialResourcesID, TextureType, ModelIndex)
//        triple -- one ItemDisplayInfoID can own several of these (e.g.
//        a weapon's own texture plus a separate glow-layer texture), kept
//        as a vector rather than picking one, same policy as
//        modelfiledata_db2.hpp's own ModelResourcesID ambiguity.
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

// One ItemDisplayInfoModelMatRes row's relevant fields.
struct ModelMatRes {
    uint32_t itemDisplayInfoId = 0;
    uint32_t materialResourcesId = 0;
    uint32_t textureType = 0;
    uint32_t modelIndex = 0;
};

struct Data {
    std::vector<ModifiedAppearance> modifiedAppearances;
    std::vector<Appearance> appearances;
    std::vector<DisplayInfo> displayInfos;
    std::vector<ModelMatRes> modelMatRes;
};

// Loads all four tables from `db2Dir` (itemmodifiedappearance.db2/
// itemappearance.db2/itemdisplayinfo.db2/itemdisplayinfomodelmatres.db2,
// real lowercase casc-tool filenames). Returns nullopt only if every table
// came back empty.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// One real ItemDisplayInfoModelMatRes row, resolved -- kept alongside its
// own textureType/modelIndex so the caller can tell a weapon's base
// texture apart from a secondary glow layer, same "resolve, don't
// interpret" split as chrcustomization::MaterialResolution.
struct Resolution {
    std::optional<uint32_t> itemDisplayInfoId;
    std::optional<uint32_t> modelResourcesId;  // feed to modelfiledata::Data for the real .m2 FileDataID(s)
    std::vector<ModelMatRes> materials;  // feed materialResourcesId to texturefiledata::Data per entry
};

// Resolves one real ItemModifiedAppearanceID against already-loaded `data`.
// A dangling reference at any hop (an ItemAppearanceID/ItemDisplayInfoID
// that matches no row) is reported to `err` and stops that hop's own
// field(s) from being filled -- never fabricated, same convention as
// chrcustomization::resolveChoice.
Resolution resolve(const Data& data, uint32_t itemModifiedAppearanceId, std::ostream& err);

}  // namespace husk::itemappearance
