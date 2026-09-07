#pragma once

#include <filesystem>
#include <string>

#include "canon_model.hpp"

// husk::writers: the native husk bundle (REFACTOR/BUNDLE_FORMAT.md) --
// glTF-free by design (Luna's 2026-09-06 resolution, superseding that doc's
// "stage-4 question, not yet decided" framing for the mesh/skeleton/
// animation binary container -- see BUNDLE_FORMAT.md's own note pointing
// back here). writeBundle is the only entry point; everything else in this
// file is implementation detail of it, exposed in the header only where a
// test needs to check byte-level output independently.
//
// ## The manifest.json contract (canonical description -- match this
// exactly for a from-scratch reader in any language; no other document
// states this schema at this level of precision)
//
// A `.bin` file is raw, tightly packed, native-endian (little-endian --
// `manifest.json`'s own top-level `"endianness"` field states this
// explicitly, once, rather than per-slice) bytes and nothing else: no
// header, no length prefix, no alignment padding beyond a component's own
// natural size. `manifest.json` is the only place that gives those bytes
// meaning.
//
// A **BufferSlice** is the repeated JSON shape naming one such byte range:
// ```json
// { "file": "mesh.bin", "byte_offset": 0, "byte_length": 49152,
//   "component_type": "f32", "component_count": 3, "count": 4096,
//   "semantic": "POSITION" }
// ```
// `component_type` is one of `"f32"|"u32"|"u16"|"u8"|"i32"` (this writer
// never emits `"u16"` today -- no source field is 16-bit -- listed because
// a reader must still accept it: `component_type` is a closed vocabulary
// tag, not something this writer alone gets to narrow). `byte_length ==
// count * component_count * sizeof(component_type)`, always -- a reader
// needing anything beyond `fread`/`mmap` at `(byte_offset, byte_length)`
// plus that arithmetic is out of contract. `semantic` is present only on
// slices that are genuinely GPU-vertex-attribute-shaped (mesh
// positions/normals/uv0/uv1/joints0/weights0/indices below); it borrows
// glTF's well-known attribute names purely because they are widely
// recognized -- nothing here requires glTF's own accessor/bufferView JSON
// shape or any glTF library to read.
//
// Small, non-uniform, or per-item structural/semantic data (geoset ranges,
// joint names, billboard modes, material layer descriptions, the small
// per-keyframe curves a material's tint/alpha-fade/UV-animation carries) is
// plain inline JSON, never a BufferSlice -- BufferSlice is reserved for
// large uniform numeric arrays where direct byte-range loading actually
// matters (per-vertex mesh attributes, per-keyframe animation values,
// per-joint bind data).
//
// A `canon::Ref` is always serialized the same way, everywhere it appears
// (bone identities, geoset identities, material layer identities, texture
// identities):
// ```json
// { "id": { "kind": "none" }, "name": "...", "name_source": "..." }
// ```
// `id.kind` is one of `"none"` (no fields beyond `kind`), `"file_data_id"`
// (`{"kind":"file_data_id","value":<uint32>}`), `"db2_row"`
// (`{"kind":"db2_row","table":<string>,"row":<uint32>}`), or
// `"record_index"` (`{"kind":"record_index","value":<uint32>}`) -- the
// exact four `canon::Identity` alternatives (`canon_ref.hpp`), one JSON
// shape per alternative, never collapsed to a single ambiguous `"value"`
// field. `name_source` is one of `"m2_embedded"|"listfile"|"db2"|
// "synthesized"|"none"` (`canon::NameSource`, verbatim).
//
// Top-level manifest shape (a deliberate **subset** of BUNDLE_FORMAT.md's
// full target schema -- `definition`/`selection`/`items`/`sources`/
// `references` are NOT written here, since no canon builder feeds them into
// `canon::Model` today; adding them is separate, later work, not a
// regression of this writer):
// ```json
// {
//   "schema_version": "0.1.0",
//   "exported_at": "2026-09-06T12:00:00Z",
//   "producer": "husk dev",
//   "endianness": "little",
//   "resources": {
//     "mesh": { ... },
//     "skeleton": { ... },
//     "animation": [ ... ],
//     "materials": [ ... ]
//   }
// }
// ```
//
// ### resources.mesh (backed by mesh.bin)
// ```json
// {
//   "positions": <BufferSlice, semantic "POSITION", f32x3>,
//   "normals":   <BufferSlice, semantic "NORMAL",   f32x3>,
//   "uv0":       <BufferSlice, semantic "TEXCOORD_0", f32x2>,
//   "uv1":       <BufferSlice, semantic "TEXCOORD_1", f32x2>,   // OMITTED entirely when canon::Mesh::uv1 is nullopt
//   "joints0":   <BufferSlice, semantic "JOINTS_0",  u8x4>,     // OMITTED (with weights0) when canon::Mesh::skinning is empty
//   "weights0":  <BufferSlice, semantic "WEIGHTS_0", f32x4>,
//   "indices":   <BufferSlice, semantic "INDICES",   u32x1>,
//   "primitives": [
//     { "geoset_id": <uint32, group*100+variant, reconstructible>,
//       "geoset_group": <uint32>, "geoset_variant": <uint32>,
//       "index_start": <uint32>, "index_count": <uint32>,
//       "material_index": <uint32> }   // this primitive's index into resources.materials -- see Model::materials' 1:1 doc comment
//   ]
// }
// ```
//
// ### resources.skeleton (backed by skeleton.bin)
// ```json
// {
//   "joint_count": <uint32>,
//   "parents":         <BufferSlice, i32x1, -1 for a root -- canon::Joint::parent verbatim>,
//   "bind_translation": <BufferSlice, f32x3, PARENT-RELATIVE -- writers::localBindTranslation per joint, not raw globalPosition>,
//   "joints": [
//     { "billboard": "none"|"spherical"|"cylindrical_lock_x"|"cylindrical_lock_y"|"cylindrical_lock_z",
//       "ref": <Ref>, "structural_label": <string> }
//   ]
// }
// ```
//
// ### resources.animation (backed by animation.bin, one shared file across every clip)
// ```json
// [
//   { "sequence_index": <uint32>, "sequence_kind": "sequence"|"global_sequence",
//     "joints": [
//       { "joint_index": <uint32>,
//         "translation": { "times": <BufferSlice f32x1>, "values": <BufferSlice f32x3>, "interpolation": "step"|"linear" },
//         "rotation":    { "times": <BufferSlice f32x1>, "values": <BufferSlice f32x4>, "interpolation": "step"|"linear" },
//         "scale":       { "times": <BufferSlice f32x1>, "values": <BufferSlice f32x3>, "interpolation": "step"|"linear" } }
//     ]
//   }
// ]
// ```
// Sparse by construction: `joints` lists only joints where
// `writers::composeJointCurves` returned real data -- one entry per real
// `canon::AnimationClip::boneCurves[i]` that is non-nullopt, never padded
// to skeleton joint count. `times`/`values` come from
// `writers::composeJointCurves`'s already-bind-pose-composed output (final
// local-transform curves), not the raw delta keyframes -- see
// `writer_common.hpp`'s own doc comment for exactly what that composition
// means for each channel.
//
// ### resources.materials
// ```json
// [
//   { "layers": [
//       { "identity": <Ref>,
//         "role": <KnownRole enum name, lowercase, or the raw open string alternative -- canon::LayerRole visited>,
//         "uv": "uv_set_0"|"uv_set_1"|...|"environment_mapped",   // canon::UvRef visited; "uv_set_<N>" for UvSetIndex{N}
//         "texture_state": "resolved"|"known_unresolved"|"ambiguous",  // canon::TextureRef::state, faithful even though every real producer today yields known_unresolved
//         "texture": <Ref>,                 // present only when texture_state == "resolved"
//         "unresolved_reason": <string>,    // present only when texture_state == "known_unresolved"
//         "candidates": [ { "identity": <Ref>, "category": <string>, "width": <uint32>, "height": <uint32> } ],  // present only when texture_state == "ambiguous"
//         "blend": <BlendOp enum name, lowercase>,
//         "tint":       <inline curve, VecCurve, values as [r,g,b]>,   // omitted when nullopt
//         "alpha_fade": <inline curve, ScalarCurve, values as plain numbers>,  // omitted when nullopt
//         "uv_animation": { "translation": <inline curve, VecCurve>, "rotation": <inline curve, QuatCurve, values as [x,y,z,w]>, "scaling": <inline curve, VecCurve> }  // object omitted when nullopt; each of its 3 members omitted independently when that sub-curve is nullopt
//       }
//     ],
//     "diffuse_layer": <Ref>, "specular_layer": <Ref>, "emission_layer": <Ref>, "alpha_layer": <Ref>   // each omitted when nullopt
//   }
// ]
// ```
// An **inline curve** (used only for tint/alpha_fade/uv_animation's three
// sub-curves -- small, per-material data, not GPU-shaped, so BufferSlice
// would be overhead without benefit):
// ```json
// { "sequence_index": <uint32>, "sequence_kind": "sequence"|"global_sequence",
//   "interpolation": "step"|"linear",
//   "keyframes": [ [<time_seconds>, <value>], ... ] }
// ```
// `<value>` is a plain number for a `ScalarCurve`, `[x,y,z]` for a
// `VecCurve`, `[x,y,z,w]` for a `QuatCurve` -- the same per-type value
// shape `resources.materials[].layers[].tint`/`alpha_fade`/`uv_animation`
// above already states.
//
// `resources.mesh.primitives[i].material_index == i` is relied on
// verbatim from `canon::Model::materials`'s own doc comment ("materials[i]
// describes mesh.primitives[i]") -- confirmed by re-reading that comment
// before writing this, not assumed; see also
// `tests/test_writers_bundle.cpp`'s explicit regression test for it.
//
// Throws std::runtime_error when `bundleDir` cannot be created, or when
// `writers::composeJointCurves`/`writers::localBindTranslation` would
// (out-of-range joint indices -- canon::Model built by hand rather than by
// assembleModel is the only way to hit this).
namespace husk::writers {

void writeBundle(const canon::Model& model, const std::filesystem::path& bundleDir,
                  const std::string& producer = "husk dev");

}  // namespace husk::writers
