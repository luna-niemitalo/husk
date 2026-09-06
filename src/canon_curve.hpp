#pragma once

#include <utility>
#include <vector>

#include "m2_primitives.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's "One curve representation" -- today gltf::JointAnimation
// (parallel-array TRS keyframes) and gltf::Material::AnimatedColorCurve/
// AnimatedScalarCurve/AnimatedQuatCurve (array-of-pairs keyframes) are the
// same concept in incompatible shapes; this is the one shape. A pure value
// type, no consumers wired to it yet (migrating gltf_skeleton.hpp/
// gltf_mesh.hpp onto it is separate, later work).
namespace husk::canon {

// Which timeline a curve plays against. husk::m2 keeps two separate,
// differently-shaped arrays a track can be scoped to -- M2Sequence (the
// normal case) and Header::globalLoops (a track whose global_sequence
// field isn't the "none" sentinel, looping continuously and independent
// of any M2Sequence -- see m2::TrackMeta's doc comment). Not canon::Ref:
// neither array entry carries a name of its own anywhere in this
// codebase (m2::Sequence has no display name field, and the global-loops
// table is just durations), so bundling Ref's decorative name/source
// fields here would be dead weight on every real value.
struct SequenceRef {
    enum class Kind { Sequence, GlobalSequence };
    Kind kind = Kind::Sequence;
    uint32_t index = 0;  // Sequence: m2::Sequence array index. GlobalSequence: Header::globalLoops array index.

    static SequenceRef sequence(uint32_t index) { return {Kind::Sequence, index}; }
    static SequenceRef globalSequence(uint32_t index) { return {Kind::GlobalSequence, index}; }
};

// M2TrackBase::interpolation_type collapses to exactly this choice by the
// time it reaches a curve: 0 is Step, 1 is Linear, and 2/3 (cubic bezier/
// hermite) never survive m2::resolveVec3TrackSequence/
// resolveQuatTrackSequence, which throw rather than resolve one -- per
// wowdev.wiki, those are "only valid for M2SplineKey tracks", which no
// track a Curve is built from ever is (see m2_animation.hpp's TrackMeta
// doc comment). No third enumerator: nothing upstream can ever produce it.
enum class Interpolation { Step, Linear };

// One property's worth of sampled keyframe animation -- a bone's
// translation/rotation/scale (today's gltf::JointAnimation, one Curve per
// property instead of three parallel-array fields) or a material's
// tint/fade/UV-transform track (today's gltf::Material::AnimatedXCurve).
// T is m2::Vec3 (translation/scale/color), m2::Quat (rotation), or float
// (scalar weight/alpha) -- the three real value shapes this codebase
// resolves a track into; see ScalarCurve/VecCurve/QuatCurve below.
template <typename T>
struct Curve {
    SequenceRef sequence;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<std::pair<float, T>> keyframes;  // seconds -> value, strictly increasing, file order
};

using ScalarCurve = Curve<float>;
using VecCurve = Curve<m2::Vec3>;
using QuatCurve = Curve<m2::Quat>;

}  // namespace husk::canon
