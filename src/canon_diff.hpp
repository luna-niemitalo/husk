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
// existing call site that predates these parameters -- the texture-identity
// check is silently skipped entirely, same "no data, no opinion" convention
// every other optional comparator input in this file follows. Deliberately
// does NOT check `KnownUnresolved`/`Ambiguous` canon states against legacy
// at all: those states are indistinguishable from "the caller supplied no
// TextureResolutions for this slot," so flagging them would produce false
// deviations on the (currently: every) real run with no `--textures`
// corpus available to resolve against.
Report compareMaterialBlendModes(const canon::Model& canonModel, const std::vector<skin::Batch>& batches,
                                 const std::vector<skin::Submesh>& submeshes,
                                 const std::vector<m2::Material>& materials,
                                 const std::vector<gltf::Material>& legacyMaterials = {},
                                 const std::vector<gltf::Primitive>& legacyPrimitives = {});

}  // namespace husk::canon_diff
