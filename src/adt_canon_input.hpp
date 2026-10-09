#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "adt.hpp"
#include "canon_terrain.hpp"
#include "groundeffect_db2.hpp"

// husk::adtinput: ADT -> canon::Terrain, the ADT counterpart of
// m2_canon_input.hpp. Every on-disk convention is resolved here and nowhere
// else; WIKI_FINDINGS/WORLD.md records how each was verified against real
// tiles. Pure: anything needing file, listfile or DB2 access arrives
// pre-resolved in `Resolutions` (cmd_export_terrain.cpp builds it).
namespace husk::adtinput {

struct TileSources {
    const adt::RootFile& root;
    const std::optional<adt::ObjFile>& obj;
    const std::optional<adt::TexFile>& tex;
    std::optional<uint32_t> wdtFlags;  // required whenever `tex` is present (selects the alpha format)
    std::string mapName;
    uint32_t tileX = 0;  // from the file name; checked against MCNK #0's position
    uint32_t tileY = 0;
};

// Keyed by FileDataID. A texture absent here becomes
// TextureRef::KnownUnresolved.
using TextureResolutions = std::unordered_map<uint32_t, canon::TextureRef>;
// Keyed by GroundEffectTexture row. An effect absent here becomes a bare
// Ref (no density, no doodads).
using GroundEffectDefinitions = std::unordered_map<uint32_t, canon::GroundEffect>;
// Name-table path (MMDX/MWMO/MTEX, exactly as the tile stores it) ->
// FileDataID. A path absent here keeps its raw string as an AdtEmbedded-
// sourced name with no identity.
using GamePathResolutions = std::unordered_map<std::string, uint32_t>;

// Held by reference, like TileSources: export-world shares the last two
// across every tile of the run.
struct Resolutions {
    const TextureResolutions& textures;
    const GroundEffectDefinitions& groundEffects;
    const GamePathResolutions& gamePaths;
};

canon::Terrain buildCanonTerrain(const TileSources& sources, const Resolutions& resolutions);

// Every texture FileDataID the tile references -- MDID, nonzero MHID, and
// each MTEX path `gamePaths` resolves -- deduplicated, in first-seen order:
// what a caller resolves into `Resolutions::textures` before building.
std::vector<uint32_t> textureFileDataIds(const adt::TexFile& tex, const GamePathResolutions& gamePaths);

// GroundEffectTexture rows joined to their GroundEffectDoodad models, as the
// rules `Resolutions::groundEffects` carries. A slot whose doodad row is
// missing keeps its Db2Row identity with no model.
GroundEffectDefinitions groundEffectDefinitions(const groundeffect::Data& data);

// Every name-table path the tile's files reference (MMDX, MWMO, MTEX), for
// `Resolutions::gamePaths`.
std::vector<std::string> gamePaths(const std::optional<adt::ObjFile>& obj, const std::optional<adt::TexFile>& tex);

}  // namespace husk::adtinput
