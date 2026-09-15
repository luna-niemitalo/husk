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

// Blend-mode-only structural check (canon::Model::materials[i] <->
// canon::Model::mesh.primitives[i], mirroring the same alignment
// gltf::Mesh's own primitives/materials pair already has). Full
// texture-identity comparison is out of scope: canon::assembleMaterial
// never resolves real texture bytes (canon_material_builder.hpp's own doc
// comment) -- that half of Stage 3 isn't built yet.
Report compareMaterialBlendModes(const canon::Model& canonModel, const std::vector<skin::Batch>& batches,
                                 const std::vector<skin::Submesh>& submeshes,
                                 const std::vector<m2::Material>& materials);

}  // namespace husk::canon_diff
