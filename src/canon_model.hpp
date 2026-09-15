#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
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
// entry, and one AnimationClip per real inline M2Sequence, per distinct
// global sequence actually used by any bone track, and per pure-alias
// M2Sequence that resolves to a real inline terminal (see assembleModel's
// own doc comment for all three).
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

// One real M2Sequence index's already-loaded external .anim payload bytes
// (the AFSB chunk's raw payload, or an AFM2-extracted blob -- see
// m2::extractAnimBlob / commands::buildAnimations's AFSB-priority-over-AFM2
// rule, export_animation.cpp). Resolution -- deciding which FileDataID's
// bytes to fetch, finding the file on disk, and reading it -- happens
// entirely outside canon:: (invariant I1, "no paths in canon::";
// REFACTOR/AUDIT.md §7.2); assembleModel only consumes already-resolved
// bytes handed to it by its caller. Keyed by the SAME `model.sequences`
// index space every other per-sequence map in this file uses (matching
// buildAnimations's own resolution unit: one non-inline, non-alias
// sequence's id/variationIndex determines which file is fetched, so a
// pure-alias sequence's entry -- if any -- belongs under its resolved
// terminal's index, not the alias's own). Empty by default: a sequence
// with no entry here that also isn't real-inline or a resolvable alias to
// a real-inline/external terminal is simply absent from
// canon::Model::animations, exactly today's behavior.
using ExternalAnimBlobs = std::unordered_map<uint32_t, std::vector<uint8_t>>;

// Orchestrates the composition: assembleSkeleton(bones) for the skeleton;
// assembleMesh(vertices, bones.size(), batches, submeshes, triangleIndices)
// for the mesh; assembleMaterial once per batch surviving
// assemblePrimitiveGeosets's own zero-indexCount skip (see Model::materials'
// own doc comment for why this mirrors, not reinvents, the batch<->primitive
// correspondence); animations are built in three passes, mirroring
// buildAnimations/buildGlobalSequenceAnimations's own three real shapes
// (export_animation.cpp) as closely as canon's inline-only, no-filesystem
// scope allows:
//
// 1. assembleBoneAnimation once per (joint, sequence) pair, for every
//    sequence that is EITHER real inline OR has an entry in the caller-
//    supplied `externalAnimBlobs` (ExternalAnimBlobs, above) -- gathered
//    per sequence into one AnimationClip tagged
//    SequenceRef::sequence(sequenceArrayIndex). "Real inline sequence":
//    kSequenceStoredInlineFlag (0x20, "the animation data is in the .m2
//    file") is a private constant in export_animation.cpp's anonymous
//    namespace, so this reimplements the same bit test directly on
//    m2::Sequence::flags rather than reusing it. A pure-alias sequence
//    (flags & kSequenceAliasFlag, no inline bit) is skipped here even
//    when it happens to have its own `externalAnimBlobs` entry --
//    aliases are resolved exclusively by pass 3 below, against their
//    terminal's own index, matching ExternalAnimBlobs' own keying
//    convention.
// 2. assembleBoneAnimationGlobal once per (joint, distinct global-sequence
//    index actually referenced by any bone's translation/rotation/scale
//    track), gathered per index into one AnimationClip tagged
//    SequenceRef::globalSequence(globalLoopsIndex) -- mirrors
//    buildGlobalSequenceAnimations's own std::set<uint16_t>-of-referenced-
//    indices discovery exactly (it does NOT iterate
//    Header::globalLoops's own array franchise; a global sequence with no
//    bone track pointing at it produces no clip either way).
// 3. A pure-alias sequence (flags & kSequenceAliasFlag, without the inline
//    bit -- kSequenceAliasFlag, 0x40, reimplemented here the same
//    private-constant way as kSequenceStoredInlineFlag) is resolved by
//    following aliasNext to its terminal non-alias sequence, mirroring
//    commands::resolveAliasChain's own bounded-hop-count cycle/out-of-range
//    defensiveness exactly (reimplemented, not called -- that function is
//    commands::, gltf::Skeleton-flavored, not worth a shared header for one
//    bit of pure M2-sequence-array logic). When the terminal sequence is
//    itself real-inline (pass 1 already built its clip), the alias gets its
//    OWN AnimationClip tagged SequenceRef::sequence(theAliasesOwnIndex) --
//    matching buildAnimations's own "clip name always comes from the
//    original alias sequence's id/variationIndex" convention -- whose
//    boneCurves are copied from the terminal's already-assembled clip
//    (reused, not re-derived: the terminal's own curves were already
//    computed in pass 1). This works identically whether the terminal's
//    own clip came from real-inline data or from a supplied external
//    blob -- pass 1 registers both the same way, so pass 3 doesn't need
//    to know which. When the terminal has neither (not real-inline, and
//    no `externalAnimBlobs` entry), the alias produces no clip either,
//    same "absent, not approximated" handling every other unresolvable
//    sequence already gets.
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
// `externalAnimBlobs` defaults to empty, preserving every existing call
// site's exact current behavior (no external-.anim sequence ever produces
// a clip) when the caller has none to offer -- see ExternalAnimBlobs'
// own doc comment above for its keying convention.
//
// `textureResolutions` (default empty, same "preserve existing behavior
// when the caller has nothing to offer" rule): forwarded verbatim to every
// assembleMaterial call this function makes (TextureResolutions, keyed by
// M2 texture array index -- canon_material_builder.hpp's own doc comment
// has the full contract). assembleModel does not resolve or interpret
// these itself, only passes the same caller-supplied map to every batch's
// material -- the map is model-wide (keyed by an M2-global texture index,
// not a per-batch-local one), so reusing it unchanged across every
// assembleMaterial call in the per-batch loop is correct, not an
// oversight.
Model assembleModel(const m2::Model& model, const std::vector<skin::Batch>& batches,
                     const std::vector<skin::Submesh>& submeshes,
                     const std::vector<uint32_t>& triangleIndices,
                     const ExternalAnimBlobs& externalAnimBlobs = {},
                     const TextureResolutions& textureResolutions = {});

}  // namespace husk::canon
