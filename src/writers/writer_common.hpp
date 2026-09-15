#pragma once

#include <optional>

#include "canon_curve.hpp"    // VecCurve, QuatCurve
#include "canon_model.hpp"    // AnimationClip
#include "canon_skeleton.hpp" // Skeleton
#include "m2_primitives.hpp"  // Vec3

// husk::writers: REFACTOR/README.md stage 4's home for target-conversion
// code (`src/writers/`) -- this is the first file placed there. Namespace
// mirrors the directory name the same way husk::canon mirrors its own
// target directory name (`src/canon/`) despite the files themselves still
// living flat under `src/` today (canon_*.cpp) -- REFACTOR/README.md's own
// migration-order note says the directory split hasn't happened yet, only
// the namespace has, and this file follows that same precedent rather than
// inventing a new one.
//
// Shared math both planned leaf writers (the native bundle format and the
// lean glTF exporter) need and neither owns: turning canon::Skeleton's
// absolute bind-pose positions and canon::BoneAnimationCurves' raw
// delta-from-bind-pose keyframes into the parent-relative / final-value
// shape an actual local-transform hierarchy wants. Stays entirely in raw M2
// space (I1: no axis conversion, no glTF-specific representation choices) --
// each writer converts axes and encodes the result its own way afterward.
namespace husk::writers {

// Joint `jointIndex`'s bind-pose translation relative to its own parent
// (`skeleton.joints[jointIndex].parent`), or `globalPosition` itself
// unchanged for a root joint (`parent == -1`). Mirrors
// commands::buildJointAnimation's own bind-translation source
// (`gltf::Skeleton::Joint::localTranslation`, export_skeleton.cpp) in
// effect, but derives it from canon::Skeleton's absolute-position
// representation instead of assuming a parent-relative field already
// exists (canon::Joint deliberately has none -- see its own doc comment).
//
// Throws std::runtime_error on an out-of-range `jointIndex` or an
// out-of-range non-root `parent`. m2input::assembleSkeleton already
// guarantees every constructed Skeleton is acyclic and in-range, but that
// guarantee belongs to the builder, not the type -- canon::Skeleton is a
// plain struct nothing stops a caller (this module's own tests included)
// from building by hand, so this function re-validates at its own boundary
// rather than trusting an invariant it can't see enforced.
m2::Vec3 localBindTranslation(const canon::Skeleton& skeleton, size_t jointIndex);

// One joint's fully-composed local-transform curves for one AnimationClip:
// - translation: localBindTranslation(skeleton, jointIndex) plus each
//   keyframe's raw delta (component-wise), since M2's bind pose carries a
//   real translation but no rotation or scale (canon::Joint's own doc
//   comment: "translation is the whole bind-pose fact").
// - rotation: the raw delta keyframes, unchanged -- bind pose contributes
//   nothing to compose against.
// - scale: the raw delta keyframes, unchanged -- bind pose scale is
//   implicitly (1,1,1).
//
// Deliberately does NOT apply gltf::enforceHemisphereContinuity.
// m2_animation_input.hpp's own doc comment establishes why: raw
// M2CompQuat keyframes are a direct linear int16 decode with no per-
// keyframe sign choice, so they're already hemisphere-continuous in M2
// space -- the discontinuity that fix compensates for is an artifact of
// gltf::rotationZUpToYUp's own quatToMat3->mat3ToQuat round-trip, one
// specific writer's later axis-conversion technique, not a fact about this
// raw-M2-space data. This module never converts axes, so the artifact
// never arises here; a writer that does convert axes is responsible for
// its own continuity fix afterward, same as export_animation.cpp already
// is for glTF today.
//
// Returns nullopt exactly when clip.boneCurves[jointIndex] is nullopt --
// mirrors canon::AnimationClip's own "no curve data for this joint"
// meaning, not a new one.
//
// Throws std::runtime_error when jointIndex is out of range for either
// skeleton.joints or clip.boneCurves (the same shared index space
// canon::AnimationClip's own doc comment states, re-checked here since
// this function is the first thing to index both together), or when
// localBindTranslation itself throws.
struct ComposedJointCurves {
    canon::VecCurve translation;
    canon::QuatCurve rotation;
    canon::VecCurve scale;
};
std::optional<ComposedJointCurves> composeJointCurves(const canon::Skeleton& skeleton,
                                                       const canon::AnimationClip& clip, size_t jointIndex);

}  // namespace husk::writers
