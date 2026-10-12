#pragma once

#include <cstdint>
#include <optional>

#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// What a surface is made of (TerrainType: "Grass", "Stone", "Snow"...): the
// client picks footstep sounds and sprays by it, and a renderer or weather
// system can ask the same question. A document lists each surface type it
// uses once; terrain ground effects, WMO materials and WMO faces index the
// list.
namespace husk::canon {

struct SurfaceType {
    Ref ref;         // Db2Row TerrainType; name = TerrainDesc
    std::optional<Ref> soundClass;  // Db2Row TerrainTypeSounds; nullopt when TerrainType has no row for `ref`
    std::optional<Ref> footstepSprayRun;   // Db2Row SpellVisualEffectName
    std::optional<Ref> footstepSprayWalk;
    uint32_t flags = 0;  // TerrainType Flags, raw (wowdev.wiki DB/TerrainType: 0x1 footprints)
};

}  // namespace husk::canon
