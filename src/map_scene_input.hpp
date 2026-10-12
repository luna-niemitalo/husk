#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "canon_placement.hpp"
#include "light_db2.hpp"
#include "wdt_volumes.hpp"
#include "wmo_canon_input.hpp"  // TextureResolutions

// husk::mapinput: a map's volume sidecars -> world placement sets of effect
// instances (REFACTOR/EFFECTS.md, literal tier). No filesystem access here;
// the caller reads the files and resolves names and textures.
namespace husk::mapinput {

// WDT.md _fogs: a VFOG whose model is 0 draws spells/errorcube.m2.
inline constexpr uint32_t kFallbackFogModel = 166046;

struct MapSceneInputs {
    std::string map;                     // the map's directory name
    const wdt::FogsFile* fogs = nullptr;  // nullptr: the map has no _fogs.wdt
    const wdt::MpvFile* mpv = nullptr;    // nullptr: the map has no _mpv.wdt
    // Particulate emitter and colour-grading textures.
    wmoinput::TextureResolutions textures;
    // Listfile names for fog and skybox models.
    std::unordered_map<uint32_t, std::string> assetNames;
    const lightdb::Data* lights = nullptr;  // nullptr: no zone lights
};

// What the map's zone lights use by FileDataID, for the caller to resolve
// into `textures` and `assetNames`.
struct ZoneLightAssets {
    std::set<uint32_t> textures;  // colour-grading lookup textures
    std::set<uint32_t> models;    // skybox models
};
ZoneLightAssets zoneLightAssets(const MapSceneInputs& inputs);

// World frame. `fogs` holds the _fogs.wdt volumes (classic_volumetric_fog),
// `particulate_volumes` the _mpv.wdt ones (classic_map_particulate_volume)
// and `zone_lights` the map's Light zones (classic_zone_light, or
// classic_zone_light_global for the map-wide one) with the light parameter
// sets they use, each instance's id its source's own id: separate id
// spaces (31 fog/particulate ids collide within a map in the corpus). A set
// with no instances is left out.
std::vector<canon::PlacementSet> buildMapEffects(const MapSceneInputs& inputs);

}  // namespace husk::mapinput
