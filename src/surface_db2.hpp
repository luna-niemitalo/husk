#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

// Typed reader for the surface tables: TerrainType (what a surface is,
// e.g. "Grass", "Stone": the footstep sound class and sprays it plays) and
// TerrainTypeSounds (that sound class's name). Terrain layers name a
// TerrainType through GroundEffectTexture's Sound column, WMO materials
// through MOMT's ground_type, WMO faces through MOQG. Same db2table.hpp-
// backed pattern as liquid_db2.hpp.
namespace husk::surfacedb {

struct TerrainType {
    uint32_t id = 0;
    std::string name;            // TerrainDesc
    uint32_t footstepSprayRun = 0;   // SpellVisualEffectName row; 0 = none
    uint32_t footstepSprayWalk = 0;
    uint32_t soundId = 0;            // TerrainTypeSounds row
    std::string soundName;           // TerrainTypeSounds Name; empty when the row is absent
    uint32_t flags = 0;              // wowdev.wiki DB/TerrainType: 0x1 footprints
};

struct Data {
    std::unordered_map<uint32_t, TerrainType> types;  // by TerrainType ID
};

// Loads terraintype.db2 and terraintypesounds.db2 from `db2Dir`. nullopt,
// with the reason on `err`, when a file or its layout can't be read.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::surfacedb
