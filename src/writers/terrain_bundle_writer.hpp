#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "canon_terrain.hpp"
#include "writers/bundle_common.hpp"  // AssetUris

// husk::writers: canon::Terrain -> bundle directory. Sibling of
// bundle_writer.hpp; same BufferSlice/Ref/texture shapes (bundle_common.hpp),
// its own top-level section. REFACTOR/BUNDLE_FORMAT.md's "Terrain tile
// bundles" is the consumer-facing description of this schema.
//
// ```json
// {
//   "schema_version": "0.1.0", "kind": "terrain_tile",
//   "exported_at": ..., "producer": ..., "endianness": "little",
//   "terrain": {
//     "map": <Ref>, "tile_x": <u32>, "tile_y": <u32>,
//     "frame": "wow_world_yards_x_north_y_west_z_up",
//     "tile_size": 533.333, "chunk_size": 33.333, "quad_size": 4.1667,
//     "chunk_vertex_layout": "interleaved_9x9_corners_8x8_centres",
//     "chunk_topology": "quad_centre_fan_4_triangles_holes_skip_quad",
//     "textures": [ { "diffuse": {<texture_state + texture Ref w/ uri>}, "height": {...}?,
//                     "repeats_per_chunk", "height_scale", "height_offset" } ],
//     "ground_effects": [ { "ref": <Ref>, "density"?, "doodads": [ { "doodad": <Ref>, "model": <Ref, uri?>, "weight", "flags" } ] } ],
//     "chunks": {
//       "count": 256,                                       // index = grid_y * 16 + grid_x
//       "origin":     <BufferSlice f32x2, count 256>,       // world (X, Y) of corner vertex (0, 0)
//       "heights":    <BufferSlice f32x145, count 256>,     // absolute world Z
//       "normals":    <BufferSlice f32x3, count 256*145, semantic NORMAL>,
//       "hole_rows":  <BufferSlice u8x8, count 256>,        // bit c of byte r: quad (r, c) absent
//       "dominant_layer": <BufferSlice u8x64, count 256>,   // per quad, row-major: layer index
//       "ground_effect_suppressed_rows": <BufferSlice u8x8, count 256>,
//       "area_index": <BufferSlice u32x1, count 256>, "areas": [ <Ref> ],
//       "layers": [ [ { "texture_index", "ground_effect_index"?, "overbright"?, "animation"?,
//                       "alpha"?: <BufferSlice u8x1, count 4096> } ] ]   // one array per chunk
//     },
//     "liquids": [ { "chunk_grid_x", "chunk_grid_y", "liquid_type": <Ref>, "liquid_object"?: <Ref>,
//                    "quad_rect": {x, y, width, height}, "quad_exists": <u8x1>,
//                    "height_source": "heightmap"|"min_height_level"|"zero",
//                    "heights": <f32x1>, "depth"?: <f32x1>, "uv"?: <f32x2> } ],
//     "placements": [ { "kind": "model"|"map_object", "asset": <Ref, uri?>, "unique_id",
//                       "position": [x,y,z], "rotation": [x,y,z,w], "scale", "flags", "doodad_set"? } ]
//   }
// }
// ```
namespace husk::writers {

// A resolved terrain texture with no payload of its own is written as a
// reference to `textureUris[fdid]` when present (the shared-texture case).
void writeTerrainBundle(const canon::Terrain& terrain, const std::filesystem::path& bundleDir,
                         const AssetUris& modelUris = {}, const AssetUris& textureUris = {},
                         const std::string& producer = "husk dev");

}  // namespace husk::writers
