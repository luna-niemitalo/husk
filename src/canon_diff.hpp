#pragma once

#include <string>
#include <vector>

#include "canon_model.hpp"
#include "gltf_mesh.hpp"
#include "gltf_skeleton.hpp"
#include "m2_animation.hpp"  // m2::Sequence
#include "m2_header.hpp"     // m2::Material
#include "skin.hpp"

// husk::canon_diff: structural comparison between the canon:: pipeline's
// output and the legacy gltf_*.cpp pipeline's own output for the SAME
// model, SAME .skin tier, SAME parsed bytes -- the runtime counterpart to
// the tests/test_canon_*_convergence.cpp fixture-only proofs, usable
// against any real corpus file rather than just the one committed fixture.
//
// Every comparison here checks structural facts, not byte-identical
// serialization -- canon:: and the legacy pipeline deliberately diverge in
// a few small, already-documented ways (REFACTOR/AUDIT.md, canon_*.hpp doc
// comments); those land in a Report's `notes`, never `deviations`. Anything
// landing in `deviations` is either a real bug in one of the two pipelines
// or a divergence nobody has decided is acceptable yet -- either way, worth
// a human's attention before Stage 3 cuts `cmd_export.cpp` over for real
// (REFACTOR/README.md's Migration order, stage 3's own gate: "every
// difference is explained and attributed").
namespace husk::canon_diff {

struct Report {
    std::vector<std::string> deviations;  // unexpected -- needs a human decision
    std::vector<std::string> notes;       // expected, already-documented scope gaps

    bool ok() const { return deviations.empty(); }
    void append(Report&& other);
};

// Positions/normals/uv0/uv1/skinning/shared-index-buffer primitive slices.
// `legacyMesh` must be the exact LOD tier `canonMesh` was built from
// (same batches/submeshes/triangleIndices) -- canon::Model has no LOD
// concept (REFACTOR/README.md stage 3), so comparing against a different
// tier is meaningless, not merely inaccurate.
Report compareMesh(const canon::Mesh& canonMesh, const gltf::Mesh& legacyMesh, float epsilon = 1e-4f);

Report compareSkeleton(const canon::Skeleton& canonSkeleton, const gltf::Skeleton& legacySkeleton,
                       float epsilon = 1e-4f);

// `modelSequences` is the model's own parsed `m2::Sequence` array --
// needed to reconstruct legacy's "anim_<id>_<variationIndex>" clip-name
// convention (export_animation.cpp), since canon::AnimationClip only
// carries the sequence's plain array index, not its semantic id.
Report compareAnimations(const canon::Model& canonModel, const std::vector<m2::Sequence>& modelSequences,
                         const std::vector<gltf::Animation>& legacyAnimations,
                         float translationEpsilon = 1e-3f);

// Blend-mode check, resolved through each side's own real indirection --
// `canon::Model::primitiveMaterials[i]` (a RecordIndex into the now-DEDUPED
// `canon::Model::materials`) on canon's side, `gltf::Primitive::
// materialIndex` (an index into the caller-supplied, also-deduped
// `legacyMaterials`) on legacy's side -- rather than assuming either list
// is 1:1 with the surviving-batch/primitive count (it isn't, on either
// side, once both pipelines dedup). `expectedBlendMode[i]` is still
// independently re-derived from `batches`/`materials` directly, same
// "don't trust canon's own skip logic" discipline this function always
// had.
//
// Also checks texture identity where both sides actually have an opinion:
// when canon's primary layer (layers[0]) is `TextureRef::State::Resolved`
// with a real `FileDataId` (AUDIT.md §7.1's orchestrator wiring, closed
// 2026-09-15 -- meaning the caller supplied `TextureResolutions`, not the
// empty default), it must name the same FileDataID legacy's own
// `gltf::Material::baseColorTextureFileDataId` does for that primitive's
// own resolved legacy material. `legacyMaterials`/`legacyPrimitives` are
// both optional (`{}` default): when either is empty -- including every
// existing call site that predates these parameters -- every check below
// that needs legacy's own material data (texture identity, the
// KnownUnresolved/Ambiguous state check, and the curve comparison) is
// silently skipped entirely, same "no data, no opinion" convention every
// other optional comparator input in this file follows.
//
// KnownUnresolved/Ambiguous (closed 2026-09-15): checked against the one
// legacy field each state actually has a real counterpart for --
// `TextureRef::State::KnownUnresolved` (husk asserts a real texture slot
// exists but couldn't get bytes for it) against `gltf::Material::
// baseColorImagePng` being empty (legacy's own "couldn't get bytes"
// signal, per that field's own doc comment); `TextureRef::State::Ambiguous`
// (multiple real candidate files, no in-file tiebreak) against
// `gltf::Material::alternateTextureCandidates` being non-empty (legacy's
// own "genuine ambiguity found" signal, per that field's own doc comment
// -- populated only when export_materials.cpp itself found 2+ same-
// basename candidates, never for a sole match). A mismatch either
// direction (canon says unresolved/ambiguous, legacy nonetheless got real
// bytes or found no candidates) is a real deviation, not a false positive
// from an absent `--textures` corpus -- both sides ran against the same
// corpus, so both should reach the same conclusion about it.
//
// Curve comparison (closed 2026-09-15): canon's primary layer's `tint`/
// `alphaFade`/`uvAnimation` (canon_material.hpp) are each resolved against
// exactly one sequence (`m2_canon_input.cpp`'s `materialSequenceIndex`),
// recoverable from the curve's own `SequenceRef`. Matched against legacy's
// corresponding `AnimatedXCurve` vector entry whose own `sequenceIndex`
// names that same sequence (or `-1` for a global-sequence-driven curve,
// `export_transform.hpp`'s `resolveAnimatedCurveGeneric` convention), then
// compared keyframe-for-keyframe (raw M2-space values -- these are colors/
// UV-space transforms, not spatial positions, so no zUpToYUp conversion
// applies, unlike `compareMesh`/`compareSkeleton`/`compareAnimations`
// above). `alphaFade` alone can legitimately match either of legacy's two
// separate `alphaFadeAnimation`/`weightFadeAnimation` vectors (canon has
// only one `alphaFade` slot per layer and prefers color-driven data when
// both are animated -- `m2_material_input.hpp`'s own doc comment); a match
// against either one is accepted, only a match against neither is a
// deviation. Only checks when canon actually populated the optional field
// -- the reverse direction (legacy carries curve data for the chosen
// sequence that canon's own optional field is empty for) isn't checked,
// same one-directional "verify what canon asserts" scope every other check
// in this function already has (blend op, texture identity above).
//
// Additional texture layers (closed 2026-09-16): `canon::Material::
// layers[1..]` (M2Batch::textureCount > 1, ~79% of the real .skin corpus
// per WIKI_FINDINGS/M2/skin.md) are no longer unchecked. Legacy's own
// secondary-layer shape (`gltf::Material::additionalTextureLayers`,
// gltf_mesh.hpp) is a bare {fileDataId, texCoord, imagePng} tuple built by
// export_materials.cpp's own `layerOffset` loop -- the same loop shape
// m2_material_input.cpp's assembleMaterial independently walks to build
// `layers[1..]`, so `layers[j + 1]` and `additionalTextureLayers[j]` name
// the same real M2 texture unit by construction. A layer-*count* mismatch
// (`layers.size() - 1` vs `additionalTextureLayers.size()`) is a real
// deviation on its own; for each layer both sides have, only texture
// identity (Resolved FileDataID match) and the KnownUnresolved state
// (legacy's `imagePng` must also be empty) are checked -- the same
// "compare what's real on both sides" discipline as everywhere else in
// this file. Blend op and tint/alphaFade/uvAnimation curves are NOT
// checked for additional layers: canon's own assembleMaterial never
// populates those fields on anything but `layers.front()`, and legacy's
// `AdditionalTextureLayer` has no field for any of them either -- there is
// nothing real on either side to compare, not an omission. Ambiguous is
// also not checked for additional layers: export_materials.cpp's own
// additional-layer resolution only tries the literal/listfile tiers,
// never the fuzzy pool that populates `alternateTextureCandidates`, so
// legacy has no signal to confirm or deny a canon Ambiguous report there.
Report compareMaterialBlendModes(const canon::Model& canonModel, const std::vector<skin::Batch>& batches,
                                 const std::vector<skin::Submesh>& submeshes,
                                 const std::vector<m2::Material>& materials,
                                 const std::vector<gltf::Material>& legacyMaterials = {},
                                 const std::vector<gltf::Primitive>& legacyPrimitives = {},
                                 float curveEpsilon = 1e-4f);

}  // namespace husk::canon_diff
