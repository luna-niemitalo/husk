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
// that model), CreatureDisplayInfoGeosetData and ParticleColor; and for
// humanoid NPCs, CreatureDisplayInfoExtra (baked skin), its
// CreatureDisplayInfoOption customization choices,
// NPCModelItemSlotDisplayInfo equipment and the TextureFileData rows the
// bakes name. Same db2table-backed pattern as creature_geoset_db2.hpp.
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
    // ExtendedDisplayInfoID: a humanoid NPC dressed through the character
    // path (Data::extras), not by texture variation. 0 = not one.
    uint32_t extra = 0;
};

// A humanoid NPC's CreatureDisplayInfoExtra row: its baked skin, as
// MaterialResourcesIDs (0 = none).
struct Extra {
    uint32_t bake = 0;
    uint32_t hdBake = 0;
};

struct CustomizationPick {
    uint32_t option = 0;  // ChrCustomizationOptionID
    uint32_t choice = 0;  // ChrCustomizationChoiceID
};

struct NpcItem {
    uint32_t itemDisplayInfo = 0;
    uint32_t slot = 0;  // NPCModelItemSlotDisplayInfo.ItemSlot, raw
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
    std::unordered_map<uint32_t, Extra> extras;
    std::unordered_map<uint32_t, std::vector<CustomizationPick>> customizationByExtra;
    std::unordered_map<uint32_t, std::vector<NpcItem>> itemsByExtra;
    // MaterialResourcesID -> texture FileDataID (texturefiledata_db2.hpp's
    // UsageType 0 rule), only for the bakes `extras` name.
    std::unordered_map<uint32_t, uint32_t> bakeTextures;
};

// Loads the tables from `db2Dir`. nullopt when any can't be read.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::creaturedisplay
