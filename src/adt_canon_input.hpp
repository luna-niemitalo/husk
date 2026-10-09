#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "adt.hpp"
#include "canon_terrain.hpp"

// husk::adtinput: ADT -> canon::Terrain, the ADT counterpart of
// m2_canon_input.hpp. Every on-disk convention is resolved here and nowhere
// else; see TODO/WORLD/ADT_EXPORT_FINDINGS.md for how each was verified.
// Pure: anything needing file or DB2 access arrives pre-resolved.
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

// Keyed by FileDataID. A texture absent from the map becomes
// TextureRef::KnownUnresolved.
using TextureResolutions = std::unordered_map<uint32_t, canon::TextureRef>;

// Keyed by GroundEffectTexture row. An effect absent from the map becomes a
// bare Ref (no density, no doodads).
using GroundEffectDefinitions = std::unordered_map<uint32_t, canon::GroundEffect>;

canon::Terrain buildCanonTerrain(const TileSources& sources, const TextureResolutions& textures,
                                  const GroundEffectDefinitions& groundEffects);

}  // namespace husk::adtinput
