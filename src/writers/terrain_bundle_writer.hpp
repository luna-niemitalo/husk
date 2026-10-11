#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "canon_terrain.hpp"
#include "writers/bundle_common.hpp"  // AssetUris

// husk::writers: canon::Terrain -> a `model` document whose content is a
// heightfield (REFACTOR/BUNDLE_FORMAT.md "Terrain tile bundles"), in the
// tile's own frame (canon_terrain.hpp). schema/model.schema.json's
// `resources.terrain` and `definition.terrain` define it key by key; what
// a schema can't say:
//
// - terrain.bin holds the chunk arrays and alpha maps, liquid.bin the
//   liquid arrays; both raw little-endian, like every bundle .bin.
// - The tile's two placement sets, `doodads` (M2s) and `map_objects`
//   (WMOs), are sets/0.json and sets/1.json and references 0 and 1.
//   Instance ids are ADT uniqueIds, unique within a set and map-wide: the
//   same id in two tiles is one object spanning both, to be loaded once.
//   A map object's selection names the WMO doodad sets (MODS index) it
//   turns on beyond the always-on set 0, MWDR/MWDS already resolved.
namespace husk::writers {

// A resolved terrain texture with no payload of its own is written as a
// reference to `textureUris[fdid]` when present (the shared-texture case).
void writeTerrainBundle(const canon::Terrain& terrain, const std::filesystem::path& bundleDir,
                        const Provenance& provenance, const AssetUris& modelUris = {},
                        const AssetUris& textureUris = {});

}  // namespace husk::writers
