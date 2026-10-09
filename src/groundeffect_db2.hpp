#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Real, typed reader for the terrain ground-cover ("detail doodad") tables:
// GroundEffectTexture.db2 (one row per MCLY.effectId: a density plus up to
// four weighted GroundEffectDoodad slots) joined to GroundEffectDoodad.db2
// (one row per doodad: the M2 it scatters). Consumed by `husk
// export-terrain`/`export-world` (cmd_export_terrain.cpp), turned into
// canon::GroundEffect rules by adtinput::groundEffectDefinitions.
//
// Same db2table.hpp-backed thin-wrapper pattern as creature_geoset_db2.hpp.
// Data access only: which effect a terrain layer uses is the ADT's own
// MCLY.effectId, not something this reader decides.
//
// Known open question (WIKI_FINDINGS/WORLD.md): `DoodadWeight` decodes to
// values like 1286/1362 through both husk DB2 paths, where the wiki says a
// row's weights total ~16. Weights are carried through as read; only their
// ratios are meaningful to a consumer until that is settled.
namespace husk::groundeffect {

struct DoodadSlot {
    uint32_t doodadId = 0;  // GroundEffectDoodad row; never 0 (empty slots are dropped)
    uint32_t weight = 0;
};

struct TextureRow {
    uint32_t id = 0;
    std::optional<uint32_t> density;
    std::vector<DoodadSlot> doodads;
};

struct DoodadRow {
    uint32_t modelFileId = 0;  // M2 FileDataID
    uint32_t flags = 0;
};

struct Data {
    std::vector<TextureRow> textures;
    std::unordered_map<uint32_t, DoodadRow> doodads;  // keyed by GroundEffectDoodad.ID
};

// Loads groundeffecttexture.db2 + groundeffectdoodad.db2 (real lowercase
// casc-tool filenames) from `db2Dir`. Returns nullopt, with the reason on
// `err`, when either file or its layout can't be read -- same "nothing to
// offer" convention as every other loader here. Throws std::runtime_error
// on a row that reads but contradicts the table's own shape (no ID, or
// DoodadID/DoodadWeight arrays of different lengths).
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::groundeffect
