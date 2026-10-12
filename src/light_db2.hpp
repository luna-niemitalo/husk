#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Typed reader for the zone-light tables: Light (a map's light zones: a
// sphere and up to 8 LightParams slots), LightParams (a parameter set's
// settings that don't change with time) with its LightSkybox, LightData
// (a parameter set's values keyed by time of day), plus Map's directory
// names to find a map's zones. Same db2table.hpp-backed pattern as
// liquid_db2.hpp. Column names are WoWDBDefs'.
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

// One LightData row. Colours are packed as stored: 0x00RRGGBB, with a high
// byte of 1, 3 or 0xFF on under 2% of rows.
struct Key {
    uint32_t time = 0;  // half-minutes since midnight: 0..2870 in 12.1.0, noon = 1440
    uint32_t directColor = 0;
    uint32_t ambientColor = 0;
    uint32_t skyTopColor = 0;
    uint32_t skyMiddleColor = 0;
    uint32_t skyBand1Color = 0;
    uint32_t skyBand2Color = 0;
    uint32_t skySmogColor = 0;
    uint32_t skyFogColor = 0;
    uint32_t sunColor = 0;
    uint32_t cloudSunColor = 0;
    uint32_t cloudEmissiveColor = 0;
    uint32_t cloudLayer1AmbientColor = 0;
    uint32_t cloudLayer2AmbientColor = 0;
    uint32_t oceanCloseColor = 0;
    uint32_t oceanFarColor = 0;
    uint32_t riverCloseColor = 0;
    uint32_t riverFarColor = 0;
    uint32_t shadowOpacity = 0;  // grey levels (0x4D4D4D) on every row seen
    uint32_t horizonAmbientColor = 0;
    uint32_t groundAmbientColor = 0;
    uint32_t endFogColor = 0;
    uint32_t sunFogColor = 0;
    uint32_t fogHeightColor = 0;
    uint32_t endFogHeightColor = 0;
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
    float cloudDensity = 0;
    float endFogColorDistance = 0;
    float fogStartOffset = 0;
    float sunFogStrength = 0;
    uint32_t colorGradingFileDataId = 0;  // 0 = none
    uint32_t darkerColorGradingFileDataId = 0;
    uint32_t field_10_0_0_44649_042 = 0;  // 0..6 in 12.1.0
    float field_12_0_0_63854_043 = 0;     // 0.2..1 in 12.1.0
    std::array<float, 4> fogHeightCoefficients{};
    std::array<float, 4> mainFogCoefficients{};
    std::array<float, 4> heightDensityFogCoefficients{};
};

struct Skybox {
    uint32_t id = 0;
    std::string name;  // the model's old path, e.g. "environments\stars\deepholm_sky01.mdx"
    uint32_t flags = 0;
    uint32_t skyboxFileDataId = 0;
    uint32_t celestialSkyboxFileDataId = 0;  // 0 = none
};

// One LightParams row.
struct Params {
    uint32_t id = 0;
    std::array<float, 3> overrideCelestialSphere{};
    std::array<float, 3> overrideSunPosition{};
    bool highlightSky = false;
    uint16_t lightSkyboxId = 0;  // 0 = none
    uint8_t cloudTypeId = 0;
    float glow = 0;
    float waterShallowAlpha = 0;
    float waterDeepAlpha = 0;
    float oceanShallowAlpha = 0;
    float oceanDeepAlpha = 0;
    uint32_t flags = 0;
    uint32_t ssaoSettingsId = 0;
    float sunPolar = 0;    // degrees as observed (0, 90, 135, 225; one 6300)
    float sunAzimuth = 0;  // degrees, -90..90
    float sunAttenuationStart = 0;
    float sunAttenuationEnd = 0;
    // Field_12_0_0_63534_016 .. _029 and Field_12_0_1_65617_030 .. _031, as
    // stored (_027 is an integer column; its bits are here unconverted).
    std::array<uint32_t, 16> unnamed{};
};

struct Data {
    std::vector<Zone> zones;
    std::unordered_map<uint32_t, std::vector<Key>> keysByParams;  // LightParams ID -> keys by time
    // LightParams rows by ID. 1,196 of the 2,377 IDs Light names in 12.1.0
    // have keys but no row here: the build's file holds only 1,674.
    std::unordered_map<uint32_t, Params> params;
    std::unordered_map<uint32_t, Skybox> skyboxes;                   // LightSkybox rows by ID
    std::unordered_map<std::string, int16_t> mapIds;                 // lowercase Map.Directory -> Map.ID
};

// Loads light.db2, lightdata.db2, lightparams.db2, lightskybox.db2 and
// map.db2 from `db2Dir`. nullopt, with the reason on `err`, when a file or
// its layout can't be read.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::lightdb
