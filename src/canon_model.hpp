#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "canon_animation.hpp"  // BoneAnimationCurves
#include "canon_collision.hpp"  // CollisionMesh
#include "canon_curve.hpp"      // SequenceRef
#include "canon_liquid.hpp"     // LiquidGrid
#include "canon_material.hpp"   // Material
#include "canon_mesh.hpp"       // Mesh
#include "canon_placement.hpp"  // PlacementSet
#include "canon_ref.hpp"        // Identity
#include "canon_scene.hpp"      // Scene
#include "canon_skeleton.hpp"   // Skeleton

// husk::canon: canon::Model, the whole-model composition root -- the first
// place every already-built, independently-convergence-proven canon::
// piece (Skeleton, Mesh, Material, per-bone animation curves) is wired
// together for one real model.
//
// `assembleModel` is pure composition and takes ONLY canon:: values --
// no m2::/skin:: type appears anywhere in its signature. That is the
// architectural point of this file, not an incidental detail: canon:: is
// the internal representation, format-agnostic by construction (Luna's own
// framing, 2026-09-15 -- "cannon is not dependant on m2, and cannon is not
// exclusive to m2, m2 is just 1 input format to cannon"). Deciding *how*
// to turn one specific input format's bytes into a Mesh/Skeleton/Material
// list/AnimationClip list is entirely that format's own input-module's
// job -- for M2, that's `husk::m2input::buildCanonModel`
// (m2_canon_input.hpp), which calls the same assembleMesh/assembleSkeleton/
// assembleMaterial/assembleBoneAnimation functions this file used to call
// directly, then hands the results here.
//
// This split is itself a real correction, not the original design: earlier
// revisions of this file had `assembleModel` accept `m2::Model`/
// `skin::Batch`/`skin::Submesh` directly and walk them itself -- silently
// making the composition root a second m2-input-module in disguise.
// Nothing in REFACTOR/CANONICAL_MODEL.md's I1 ("what canon must not
// contain") technically forbade it -- I1 only constrains canon:: struct
// *fields* (no paths/tinygltf/bpy/GPU types), not assembly *function
// signatures* -- so it never tripped any written rule, and every
// convergence test happened to feed it m2 data anyway, so nothing forced
// the boundary to be honest until it was pointed out directly. Fixed here;
// REFACTOR/CANONICAL_MODEL.md's I1 was also extended with an explicit
// clause to name this so it can't drift back in unnoticed.
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
// skinning + per-geoset primitive slices), a DEDUPED set of Materials (no
// longer required to be 1:1 with `mesh.primitives` -- see
// `primitiveMaterials` below for how a primitive finds its material), and
// one AnimationClip per resolved input.
//
// Primitive-to-material correspondence: `primitiveMaterials[i]` names
// which `materials` entry `mesh.primitives[i]` draws with, by that
// material's own `Material::ref.id` (today: always `RecordIndex{j}`
// meaning `materials[j]`, since every real producer is local -- see
// `Material::ref`'s own doc comment). This replaces an earlier positional
// convention (`materials[i]` implicitly describing `mesh.primitives[i]`)
// that assumed one distinct Material per primitive -- true of legacy's
// per-BATCH output, never true of legacy's own deduped `gltf::Material`
// list (`materialDedupKey`, export_texture_resolution.cpp), and canon::
// should match the latter, not the former, since a Model with real
// identity per material is the whole point of this indirection.
struct Model {
    Ref subject;  // the model itself; set by whoever knows its identity (the export command)
    Skeleton skeleton;
    Mesh mesh;
    std::vector<Material> materials;
    std::vector<Identity> primitiveMaterials;  // one per mesh.primitives entry, see above
    std::vector<AnimationClip> animations;
    Scene scene;  // attachments, events, lights, emitters

    // Placement sets this model owns (a WMO's doodad sets). An always-on set
    // is shown wherever the model is placed; the rest are turned on per
    // placement (PlacedInstance::activeSets).
    struct OwnedSet {
        PlacementSet set;
        bool alwaysOn = false;
        // Written inside the model's manifest rather than as its own file:
        // a set nothing else will share (an M2's effects).
        bool inlineDocument = false;
    };
    std::vector<OwnedSet> placementSets;
    // The fog a part's 0 fog slot names (WMO MFOG record 0, when it is a
    // real fog and not the empty entry). nullopt: the enclosing fog.
    std::optional<InstanceRef> defaultFog;
    std::vector<LiquidGrid> liquids;
    std::vector<LiquidType> liquidTypes;  // each type `liquids` name, once, first-seen order
    CollisionMesh collision;  // empty = the model has none (M2 only)
};

// Packs already-built canon:: pieces into a Model, validating the
// structural invariants this composition step owns: `primitiveMaterials`
// must have exactly one entry per `mesh.primitives` entry, and any
// `RecordIndex` identity in it must be in range for `materials` (the only
// `Identity` kind this function can locally validate -- a `FileDataId`/
// `Db2Row` identity, naming a bundle-external material, isn't resolvable
// against a local `materials` vector at all, so it's accepted unchecked;
// validating THAT reference is the consumer's job once cross-bundle
// loading exists). Every bone reference in `scene`, and every instance anchor, must be a `RecordIndex`
// in range for `skeleton.joints`, every primitive's `part` must be in range
// for `mesh.parts`, instance ids must be unique within each placement set, and a
// local light's placement set must be a RecordIndex in range for
// `placementSets`. Every part fog and ambient volume, and `defaultFog`, must
// name an effect instance of `placementSets`.
// Each liquid's part and RecordIndex material must be in range, and its
// arrays sized to its grid. Collision indices must be in range for its
// positions, three per triangle, with none or one normal per triangle.
// Mesh attribute names must be unique, each channel sized to its domain.
//
// Every argument is taken by value and moved from -- this function's own
// job is assembly, not borrowing; a caller done building these pieces has
// no further use for them.
Model assembleModel(Skeleton skeleton, Mesh mesh, std::vector<Material> materials,
                     std::vector<Identity> primitiveMaterials, std::vector<AnimationClip> animations,
                     Scene scene = {}, std::vector<Model::OwnedSet> placementSets = {},
                     std::vector<LiquidGrid> liquids = {}, CollisionMesh collision = {},
                     std::optional<InstanceRef> defaultFog = std::nullopt);

// Resolves `model.primitiveMaterials[primitiveIndex]` to an index into
// `model.materials` -- the one shared implementation of this lookup (I2):
// `canon_diff.cpp` and both real writers (`writers/gltf_lean.cpp`,
// `writers/bundle_writer.cpp`) all need the identical answer, so it lives
// here once rather than being reimplemented at each call site. `RecordIndex`
// is the only `Identity` kind resolvable against a single local `Model`
// today (every real producer is local -- `Material::ref`'s own doc
// comment); nullopt for any other kind, an out-of-range `primitiveIndex`,
// or a `RecordIndex` out of range for `materials` (the last case is
// unreachable for a `Model` built via `assembleModel`, which already
// validates it at construction -- checked again here defensively since
// this function has no other way to know how its `Model` was built).
std::optional<size_t> resolveMaterialIndex(const Model& model, size_t primitiveIndex);

}  // namespace husk::canon
