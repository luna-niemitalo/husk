#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "canon_ref.hpp"
#include "phys.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's "Version-collapsed physics" -- a pure value type, no
// consumers wired to it yet (gltf_skeleton.hpp's PhysicsBody/PhysicsJoint
// stay exactly as they are, as that section's own text says: they're a
// deliberate writer-tier reduction, not the thing this file replaces).
namespace husk::canon {

// phys::File already IS the version-collapsed graph this section asks for:
// every PHYS chunk-tag variant (BODY/BDY2/BDY3/BDY4, SHAP/SHP2, WELJ/WLJ2/
// WLJ3, SHOJ both strides, REVJ/REV2, PRSJ/PRS2, ... -- phys.hpp's own
// top-of-file comment) already converges there at the parse layer, full
// frame matrices and shape geometry intact, nothing gated behind
// phys::ParseError or a raw byte offset (checked against I1). So
// canon::Physics doesn't re-derive that shape from scratch -- it is that
// shape. The one real addition canon layers on top is boneRef below.
using Physics = phys::File;

// Body::boneIndex is a plain array position into the model's own bone
// list. Contrast canon_curve.hpp's SequenceRef, which deliberately stays a
// bare index because a sequence/global-sequence entry never carries a name
// anywhere in this codebase (Ref's decorative fields would be dead
// weight there): a bone is the opposite case. export_skeleton.cpp already
// resolves real, trust-tiered bone names today (tier 0's keyBoneId table,
// attachment/event tables, topology-synthesized fallback -- see
// TODO/BONE_NAME_DEDUCTION_TODO.md), which is exactly the M2Embedded-vs-
// Synthesized distinction NameSource exists to carry. So a bone reference
// is the case *for* wrapping in Ref, not against it.
//
// This helper fills the identity half unconditionally; name/source are
// optional because most call sites (e.g. a bare Body::boneIndex) have no
// resolution available and want the None default Geoset::fromRawId also
// leaves until something supplies a real choice. canon_skeleton.hpp's
// Joint is the case that *does* have a name in hand (keyBoneName's table,
// or a synthesized "bone_<index>") at construction time, so it passes both
// rather than building a second identical identity-only helper.
inline Ref boneRef(uint32_t boneIndex, std::string name = {}, NameSource source = NameSource::None) {
    Ref ref;
    ref.id = RecordIndex{boneIndex};
    ref.name = std::move(name);
    ref.source = source;
    return ref;
}

}  // namespace husk::canon
