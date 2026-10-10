#pragma once

#include <filesystem>
#include <string>

#include "canon_map.hpp"
#include "canon_model.hpp"
#include "writers/bundle_common.hpp"  // AssetUris

// husk::writers: the native husk bundle, a `model` document
// (REFACTOR/BUNDLE_FORMAT.md "Document kinds and the contract header").
// schema/model.schema.json and schema/placement_set.schema.json define
// what this writes, key by key; tests check every written manifest against
// them (bundle_schema.hpp), so this comment only records what a schema
// can't:
//
// - `.bin` files are raw, tightly packed little-endian arrays: no header,
//   no padding. A BufferSlice names a range in one.
// - Small or non-uniform data (geoset ranges, joint names, material
//   layers, the short curves of a material, light or emitter) is inline
//   JSON; BufferSlice is for large uniform arrays (vertices, keyframes,
//   bind data).
// - `primitives[i].material_index` is canon::resolveMaterialIndex(model,
//   i): materials are deduplicated, not one per primitive.
// - Animation is sparse: a clip lists only joints with curves, and its
//   curves are writers::composeJointCurves's bind-pose-composed local
//   transforms, not the raw keyframes (writer_common.hpp).
// - Every position in the scene is in model space, not relative to its
//   joint; the joint's animation carries it as it would a vertex weighted
//   1.0 to that joint.
// - Owned placement sets are written as sets/<i>.json and are the
//   document's first references, set i being reference i.
// - Mesh attribute channels written today: "wmo.poly_flags" (face, u16,
//   MOPY/MPY2 flags raw; WMO.md's isCollidable is 0x08, or 0x20 without
//   0x04, and 0x100 on MPY2 collision-only faces is undocumented) and
//   "wmo.poly_material" (face, u16, MOMT index raw; 0xFF in MOPY, 0xFFFF in
//   MPY2 marks a collision-only face no primitive draws).
//
// Throws std::runtime_error when `bundleDir` cannot be created, or when
// `writers::composeJointCurves`/`writers::localBindTranslation` would
// (out-of-range joint indices -- a canon::Model built by hand rather than
// by assembleModel is the only way to hit this).
namespace husk::writers {

// `assetUris` links placement-set instances to bundles the caller exported
// (FileDataID -> uri relative to `bundleDir`).
void writeBundle(const canon::Model& model, const std::filesystem::path& bundleDir, const Provenance& provenance,
                 const AssetUris& assetUris = {});

// A map's scene bundle: what the map places as a whole (fog and particulate
// volumes), world frame. Raw source records go into map_scene.bin (written
// only when non-empty), emitter textures into textures/; manifest.json is
// written last.
//
// ```json
// { "schema_version": "0.1.0", "exported_at", "producer", "kind": "map_scene", "endianness": "little",
//   "map_scene": {
//     "map": <Ref, the map's directory name>,
//     "fog_volumes": [   // WDT _fogs VFOG + VFEX + VFE2
//       { "ref": <Ref, name "vfog_<id>">, "id": <uint32, unique across the map's files>,
//         "color": [r,g,b], "intensity": [3 f], "unknown18": <f>, "position": [x,y,z], "rotation": [x,y,z,w],
//         "radius": [3 f], "animation_periods": [4 i32], "flags": <uint32>,
//         "model": <Ref, FileDataID; id kind "none" = the client's errorcube fallback>,
//         "fog_level": <uint32, 0..2>,
//         "palette": { "colors": [[r,g,b] x3], "mode": <f, 1|2> },   // VFE2, candidate roles; OMITTED when absent
//         "extra": { "enabled": <bool>, "extent": [3 f],            // VFEX; OMITTED when absent
//                    "parameters": [13 f, semantics unresolved, default 1.0] },
//         "source_extra": <BufferSlice u8x1, the 96-byte VFEX>,      // OMITTED when absent
//         "source_extra2": <BufferSlice u8x1, the 176-byte VFE2> } ], // OMITTED when absent
//     "particulate_volumes": [   // WDT _mpv PVMI groups
//       { <every resources.particulate_volumes key, world frame; ref name "pvmi_<id>">,
//         "id": <uint32>, "bounds_min": [x,y,z], "bounds_max": [x,y,z],
//         "points": [ { "direction": [x,y], "unknown08": <f>, "unknown0c": <f> } ],   // PVPD, semantics unresolved
//         "boxes": [ { "min": [x,y,z], "max": [x,y,z], "children": [<uint32> into boxes],
//                      "complete": <uint32> } ] } ]   // PVBD hierarchy: boxes[i < points] = leaf of points[i]
//   }
// }
// ```
void writeMapSceneBundle(const canon::MapScene& scene, const std::filesystem::path& bundleDir,
                         const std::string& producer);

}  // namespace husk::writers
