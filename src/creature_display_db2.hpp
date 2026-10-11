#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Typed reader for the creature display chain: CreatureModelData (which
// rows draw a given M2), CreatureDisplayInfo (each row one way to dress
// that model), CreatureDisplayInfoGeosetData and ParticleColor. Same
// db2table-backed pattern as creature_geoset_db2.hpp.
namespace husk::creaturedisplay {

struct ModelData {
    uint32_t id = 0;
    float scale = 1;
    // CreatureModelData.CreatureGeosetDataID: nonzero when the model's
    // displays pick geosets through CreatureDisplayInfoGeosetData.
    bool displaysPickGeosets = false;
};

struct Display {
    uint32_t id = 0;
    float scale = 1;
    uint32_t alpha = 255;
    uint32_t particleColorId = 0;
    // TextureVariationFileDataID[4]; slot i fills M2 texture type 11 + i
    // for i < 3. 0 = the slot is not set.
    std::array<uint32_t, 4> textures{};
    // ExtendedDisplayInfoID != 0: a humanoid NPC dressed through the
    // character path (baked skin, customization, items), not by texture
    // variation.
    bool humanoid = false;
};

// ParticleColor's three colour ramps, ARGB packed as stored; entry i
// recolours emitters whose particle colour index is 11 + i.
struct ParticleColors {
    std::array<uint32_t, 3> start{};
    std::array<uint32_t, 3> mid{};
    std::array<uint32_t, 3> end{};
};

struct Data {
    std::unordered_map<uint32_t, std::vector<ModelData>> modelDataByFile;
    std::unordered_map<uint32_t, std::vector<Display>> displaysByModelData;
    // Geoset ids as (GeosetIndex + 1) * 100 + GeosetValue (creature_geoset_db2.hpp).
    std::unordered_map<uint32_t, std::vector<uint32_t>> geosetsByDisplay;
    std::unordered_map<uint32_t, ParticleColors> particleColors;
};

// Loads the four tables from `db2Dir`. nullopt when any can't be read.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::creaturedisplay
