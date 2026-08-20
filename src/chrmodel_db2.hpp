#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// Real, typed per-table structs for the handful of DB2 tables that drive
// WoW's character texture compositing -- TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's
// (deleted once implemented, see CLAUDE_HISTORY.md) "Stage 2: real placement
// geometry": `ChrModelMaterial` (per-model base atlas dims by TextureType),
// `CharComponentTextureSections` (real placement rects by SectionType),
// `ChrModelTextureLayer` (which section a texture layer targets and how it
// blends), `CharComponentTextureLayouts` (the atlas size all of the above
// share, keyed by the same ID).
//
// Built on `db2table.hpp`'s generic named-column reader rather than
// duplicating per-table decode logic four times -- each loader here is a
// thin wrapper naming its own real columns.
//
// Scope, deliberately: this is the *data access* layer only. It does NOT
// resolve *which* CharComponentTextureLayoutsID applies to a given .m2
// model itself -- that's `chrrace_db2.hpp`'s own real ChrModel.db2 ->
// CharComponentTextureLayoutID column, joined against whichever
// ChrModelID `--chr-model-id` resolves (`cmd_export.cpp`'s
// attachCharTextureLayout) -- and this module does NOT attempt any pixel
// compositing (Stage 4, deliberately reverted, see CLAUDE_HISTORY.md). An
// explicit `--char-layout-id` still overrides that derivation outright,
// same "hand husk a plain local answer instead of making it guess" option
// every other opt-in sidecar in this project has.
//
// Never a hard dependency, same tier as db2.hpp/dbd.hpp: reads whatever
// `--db2-dir` points at (real lowercase `casc-tool`-exported filenames --
// chrmodelmaterial.db2, charcomponenttexturesections.db2,
// chrmodeltexturelayer.db2, charcomponenttexturelayouts.db2), resolved
// against a required `--dbd-dir` WoWDBDefs checkout for real column names.
// A missing file, unresolved layout, or missing --dbd-dir leaves that
// table's own vector empty (with a diagnostic) rather than failing the
// whole load -- partial real data (e.g. materials without sections) is
// still useful.
namespace husk::chrmodel {

struct ChrModelMaterial {
    uint32_t id = 0;
    uint32_t charComponentTextureLayoutsId = 0;
    uint32_t textureType = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t flags = 0;
};

// CharComponentTextureSections' own real column is named
// "CharComponentTextureLayoutID" -- singular "Layout", unlike
// ChrModelMaterial/ChrModelTextureLayer's "CharComponentTextureLayoutsID"
// -- a real Blizzard naming inconsistency across these tables, not a typo
// in this struct.
struct CharComponentTextureSection {
    uint32_t id = 0;
    uint32_t charComponentTextureLayoutId = 0;
    uint32_t sectionType = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t overlapSectionMask = 0;
};

struct ChrModelTextureLayer {
    uint32_t id = 0;
    uint32_t charComponentTextureLayoutsId = 0;
    uint32_t textureType = 0;
    uint32_t layer = 0;
    uint32_t flags = 0;
    uint32_t blendMode = 0;
    uint32_t textureSectionTypeBitMask = 0;
    // Real, current-layout field: ChrModelTextureTargetID<32>[2] -- an
    // array (see reference/wow.export's own DBCharacterCustomization.js,
    // `chr_model_texture_layer_row.ChrModelTextureTargetID[0]`, the real
    // client join key). Only the first element is real/used by the client
    // for this join; db2table::readNamedColumns already decodes an inline
    // array field's first element for any scalar-style named-column
    // request, so no new array-reading machinery was needed to expose it
    // here. This is the join key ChrCustomizationMaterial.
    // ChrModelTextureTargetID (chrcustomization_db2.hpp) matches against --
    // NOT textureType, which is a separate real field used to join against
    // ChrModelMaterial for base-atlas sizing instead. 0 on layouts that
    // predate this field (real, older builds; see chrmodel_db2.cpp's
    // loader for the fallback).
    uint32_t chrModelTextureTargetId = 0;
};

struct CharComponentTextureLayout {
    uint32_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Data {
    std::vector<CharComponentTextureLayout> layouts;
    std::vector<ChrModelMaterial> materials;
    std::vector<CharComponentTextureSection> sections;
    std::vector<ChrModelTextureLayer> textureLayers;
};

// Loads all four tables from `db2Dir`. Returns nullopt only if every single
// table came back empty (nothing to offer at all) -- a caller filtering by
// a specific CharComponentTextureLayoutsID should still check its own
// filtered results for emptiness separately, since a non-empty `Data` here
// only means *some* real data loaded, not that the requested ID exists in
// it.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::chrmodel
