#pragma once

#include <cstdint>

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
// This helper only fills in the identity half. The name half needs the
// whole skeleton's own resolution (attachment/event lookups, topology
// walk) that a single Body has no access to -- a writer attaches
// name/source once it has actually resolved one, same as Geoset::fromRawId
// leaving name/source at their None default until a customization choice
// supplies one.
inline Ref boneRef(uint16_t boneIndex) {
    Ref ref;
    ref.id = RecordIndex{boneIndex};
    return ref;
}

}  // namespace husk::canon
