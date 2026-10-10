#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "canon_map.hpp"
#include "wdt_volumes.hpp"
#include "wmo_canon_input.hpp"  // TextureResolutions

// husk::mapinput: a map's volume sidecars -> canon::MapScene. No filesystem
// access here; the caller reads the files and resolves names and textures.
namespace husk::mapinput {

struct MapSceneInputs {
    std::string map;                     // the map's directory name
    const wdt::FogsFile* fogs = nullptr;  // nullptr: the map has no _fogs.wdt
    const wdt::MpvFile* mpv = nullptr;    // nullptr: the map has no _mpv.wdt
    wmoinput::TextureResolutions textures;                 // particulate emitter textures
    std::unordered_map<uint32_t, std::string> assetNames;  // listfile names for fog models
};

canon::MapScene buildMapScene(const MapSceneInputs& inputs);

}  // namespace husk::mapinput
