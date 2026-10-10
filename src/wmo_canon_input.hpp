#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "canon_model.hpp"
#include "wmo.hpp"

// husk::wmoinput: WMO root + group files -> canon::Model. A WMO is an
// ordinary object model (REFACTOR/PLACEMENT_SETS.md): no skeleton, one mesh
// whose parts are the WMO's groups, materials from MOMT, one owned placement
// set per doodad set (MODS), set 0 always on, and MNLD/MOLT lights as
// scene.localLights (an MNLD light switches with its doodad set), and each
// group's MLIQ as a LiquidGrid, MPVD particulate volumes in
// scene.particulateVolumes, and MAVG/MAVD/MBVD in scene.ambientVolumes.
namespace husk::wmoinput {

struct WmoInputs {
    const wmo::RootFile* root = nullptr;
    // One entry per root group (MOGI order), the base-LOD group file; nullopt
    // when the caller couldn't find or read it -- that part then has no
    // primitives.
    std::vector<std::optional<wmo::GroupFile>> groups;
    // Material and particulate-volume textures and doodad models, keyed by
    // FileDataID, resolved by the caller (no filesystem access here).
    std::unordered_map<uint32_t, canon::TextureRef> textures;
    std::unordered_map<uint32_t, std::string> assetNames;  // listfile names for doodad and light-cookie FileDataIDs
};

// The wiki name of WMO shader `id` (WMO.md "Shader types"), or nullptr.
const char* shaderName(uint32_t id);

// Throws std::runtime_error when the inputs contradict each other: a groups
// list that isn't one per MOGI entry, a batch naming a material past MOMT, a
// doodad naming a MODI slot past the table, a group with vertices but no
// normals, an MNLD/MOLT light type outside the wiki's enum, or a liquid naming
// a material past MOMT.
canon::Model buildCanonWmo(const WmoInputs& inputs);

}  // namespace husk::wmoinput
