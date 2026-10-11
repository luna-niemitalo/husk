#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Typed reader for the zone-light tables, fog only: Light (a map's light
// zones: a sphere and up to 8 LightParams slots) and LightData (each
// LightParams' values keyed by time of day), plus Map's directory names to
// find a map's zones. Same db2table.hpp-backed pattern as liquid_db2.hpp.
// Lighting proper (sky, ambient, direct) is LIGHTING_TODO.md's.
namespace husk::lightdb {

struct Zone {
    uint32_t id = 0;
    int16_t map = 0;  // Map.ID
    // World frame, yards, as stored: 3,812 of 4,108 local zones land on a
    // tile their map lists (the wiki's inches-and-axis-swap describes the
    // old DBC, not this table).
    std::array<float, 3> position{};
    float falloffStart = 0;
    float falloffEnd = 0;
    // wowdev.wiki DB/Light: 0 clear, 1 clear underwater, 2 storm, 3 storm
    // underwater, 4 death; 5..7 unnamed. 0 = none.
    std::array<uint16_t, 8> params{};
};

// One LightData row's fog columns (WoWDBDefs names). Colours are packed
// BGRA as stored.
struct FogKey {
    uint32_t time = 0;  // half-minutes since midnight: 0..2870 in 12.1.0, noon = 1440
    float fogEnd = 0;
    float fogScaler = 0;
    float fogDensity = 0;
    float fogHeight = 0;
    float fogHeightScaler = 0;
    float fogHeightDensity = 0;
    float fogZScalar = 0;
    float mainFogStartDist = 0;
    float mainFogEndDist = 0;
    float sunFogAngle = 0;
    float sunFogStrength = 0;
    float endFogColorDistance = 0;
    float fogStartOffset = 0;
    uint32_t skyFogColor = 0;
    uint32_t endFogColor = 0;
    uint32_t sunFogColor = 0;
    uint32_t fogHeightColor = 0;
    uint32_t endFogHeightColor = 0;
    std::array<float, 4> fogHeightCoefficients{};
    std::array<float, 4> mainFogCoefficients{};
    std::array<float, 4> heightDensityFogCoefficients{};
};

struct Data {
    std::vector<Zone> zones;
    std::unordered_map<uint32_t, std::vector<FogKey>> keysByParams;  // LightParams ID -> keys by time
    std::unordered_map<std::string, int16_t> mapIds;                 // lowercase Map.Directory -> Map.ID
};

// Loads light.db2, lightdata.db2 and map.db2 from `db2Dir`. nullopt, with
// the reason on `err`, when a file or its layout can't be read.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::lightdb
