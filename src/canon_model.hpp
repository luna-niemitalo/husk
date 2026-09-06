#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "canon_animation_builder.hpp"  // BoneAnimationCurves
#include "canon_curve.hpp"              // SequenceRef
#include "canon_material.hpp"           // Material
#include "canon_material_builder.hpp"   // M2MaterialInputs
#include "canon_mesh_builder.hpp"       // Mesh
#include "canon_skeleton.hpp"           // Skeleton
#include "m2.hpp"
#include "skin.hpp"

// husk::canon: canon::Model, the whole-model composition root -- the first
// place every already-built, independently-convergence-proven canon::
// piece (Skeleton, Mesh, Material, per-bone animation curves) is wired
// together for one real model, per Luna's explicit direction (this task's
// own brief). Purely a wiring layer: every fact canon::Model exposes was
// already derived by assembleSkeleton/assembleMesh/assembleMaterial/
// assembleBoneAnimation, each with its own convergence proof against the
// real production pipeline -- assembleModel (canon_model.cpp) does not
// re-derive any of their internal logic, only decides *how many times* and
// *with what arguments* to call each.
namespace husk::canon {

// One M2Sequence's worth of assembled bone animation, gathered across every
// joint (assembleBoneAnimation's own per-(bone,sequence) shape, called once
// per joint instead of leaving that fan-out to a separate caller each time).
// `boneCurves[i]` corresponds to `Model::skeleton.joints[i]` -- same
// index-correspondence convention Mesh::primitives uses relative to
// Mesh::positions/normals/uv0/skinning (one shared index space, not a
// second identity scheme). nullopt at index i means joint i has no real
// curve data for this sequence (assembleBoneAnimation's own "nothing to
// animate" return), not that the joint doesn't exist.
struct AnimationClip {
    SequenceRef sequence;
    std::vector<std::optional<BoneAnimationCurves>> boneCurves;
};

// The whole model: one bind-pose skeleton, one flat mesh (positions +
// skinning + per-geoset primitive slices), one Material per Mesh::primitives
// entry, and one AnimationClip per real inline M2Sequence.
//
// Batch-to-primitive-to-material correspondence: `materials[i]` describes
// `mesh.primitives[i]` -- the SAME index correspondence
// buildMaterialsAndPrimitives (export_materials.cpp) already establishes
// between its own per-batch material list and per-batch primitive list (one
// real M2Batch produces exactly one gltf::Primitive and, when textureCount >
// 0, exactly one gltf::Material, both pushed in the same batch-iteration
// order). canon::Model mirrors that existing rule rather than inventing a
// new one: assembleMesh's own primitives vector is built by
// assemblePrimitiveGeosets iterating `batches` in order, skipping only a
// zero-indexCount submesh's batch -- assembleModel below applies that exact
// same skip when building `materials`, so the two vectors stay index-aligned
// by construction, not by coincidence.
struct Model {
    Skeleton skeleton;
    Mesh mesh;
    std::vector<Material> materials;
    std::vector<AnimationClip> animations;
};

// Orchestrates the composition: assembleSkeleton(bones) for the skeleton;
// assembleMesh(vertices, bones.size(), batches, submeshes, triangleIndices)
// for the mesh; assembleMaterial once per batch surviving
// assemblePrimitiveGeosets's own zero-indexCount skip (see Model::materials'
// own doc comment for why this mirrors, not reinvents, the batch<->primitive
// correspondence); assembleBoneAnimation once per (joint, real inline
// sequence) pair, gathered per sequence into one AnimationClip.
//
// "Real inline sequence": mirrors buildAnimations's own filter
// (export_animation.cpp) as closely as assembleBoneAnimation's own inline-
// only scope allows -- kSequenceStoredInlineFlag (0x20, "the animation data
// is in the .m2 file") is a private constant in that file's anonymous
// namespace, so this reimplements the same bit test directly on
// m2::Sequence::flags rather than reusing it. Unlike buildAnimations, this
// does NOT resolve a pure-alias sequence (flags & kSequenceAliasFlag,
// without the inline bit) via its alias target -- assembleBoneAnimation's
// own doc comment already states alias resolution is separate, later work,
// so a pure-alias sequence is simply absent from Model::animations rather
// than approximated. A future consumer needing alias sequences too is
// exactly the "separate, later work" both doc comments already name.
//
// `sequenceIndex` for assembleMaterial: a real judgment call, checked
// against production first rather than guessed. export_materials.cpp's own
// resolveAnimatedColorCurve/resolveAnimatedFixed16Curve/
// resolveAnimatedRawQuatCurve resolve EVERY real M2Sequence (plus a
// synthetic global-sequence entry) into ONE flat curve list living inside a
// single gltf::Material -- i.e. production's own Material is NOT scoped to
// one sequence at all. canon::MaterialLayer, by contrast, already made the
// opposite choice before this task (canon_material_builder.hpp's own doc
// comment: "canon::MaterialLayer::tint/alphaFade/uvAnimation each hold
// exactly one canon::Curve... a real, intentional divergence from
// export_materials.cpp"). So neither "once per sequence" nor "a fixed
// index" is a full mirror of production -- production's real answer
// (resolve every sequence into one material) doesn't fit canon::Material's
// already-landed single-sequence shape at all. Given that constraint,
// assembleModel calls assembleMaterial ONCE per batch, with a fixed
// `sequenceIndex` (the first real inline sequence found, i.e. `animations`
// built above; 0 when there are none at all) -- preserving the simple,
// stated 1:1 `materials[i]` <-> `mesh.primitives[i]` correspondence
// (Model::materials' own doc comment) rather than multiplying materials by
// sequence count for a consumer that doesn't exist yet. Consequence,
// flagged for review rather than silently accepted: a multi-sequence
// model's `Model::materials` only reflects ONE sequence's worth of
// animated tint/fade/UV-transform curves -- the other sequences' material
// animation is simply not represented anywhere in `Model` today. Fixing
// this for real needs either a per-sequence Material curve LIST (closing
// the gap canon_material_builder.hpp's own doc comment already names) or a
// deliberate multi-material-per-primitive shape change -- neither is done
// here, since no writer needs it yet and canon_material.hpp's shape isn't
// this task's to redesign.
//
// Throws std::runtime_error wherever assembleSkeleton/assembleMesh/
// assembleMaterial/assembleBoneAnimation already would -- no new corruption
// checks are added here.
Model assembleModel(const m2::Model& model, const std::vector<skin::Batch>& batches,
                     const std::vector<skin::Submesh>& submeshes,
                     const std::vector<uint32_t>& triangleIndices);

}  // namespace husk::canon
