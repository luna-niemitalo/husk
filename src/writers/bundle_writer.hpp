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
// - Owned placement sets are written as sets/<i>.json, or inline in the
//   reference's `document` (an M2's effects), and are the document's first
//   references, set i being reference i.
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

// A `world` document (schema/world.schema.json): the map's tile grid, cell
// i's tile being reference i with uri `tileUris[i]` (empty: known, not
// exported), and its off-grid placement sets as sets/<i>.json (the
// map-wide WMO, and the map's volumes as effect instances, their effect
// documents in effects/ and particle textures in textures/); manifest.json
// is written last. `assetUris` links placed assets and fog shape models.
// Throws when tileUris doesn't match the cells.
void writeWorldBundle(const canon::World& world, const std::filesystem::path& bundleDir, const Provenance& provenance,
                      const std::vector<std::string>& tileUris, const AssetUris& assetUris = {});

}  // namespace husk::writers
