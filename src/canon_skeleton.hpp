#pragma once

#include <string>
#include <vector>

#include "canon_physics.hpp"  // boneRef
#include "canon_ref.hpp"
#include "m2_primitives.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's Resources layer, the joint hierarchy + bind pose --
// a pure value type, no consumers wired to it yet (gltf_skeleton.hpp's
// Skeleton/Joint stay exactly as they are; migrating export_skeleton.cpp
// onto this is separate, later work). Deliberately excludes GeosetTag
// (canon::Geoset on the mesh side replaces it, CANONICAL_MODEL.md's
// "Geosets are first-class") and bone corrections/physics/customization
// (each is its own concern with its own canon type or none yet) -- a
// canon::Skeleton is only the hierarchy and bind pose.
namespace husk::canon {

// M2's four billboard flags plus "none" are the complete real set
// (m2::billboardModeName's own if-chain, m2_header.cpp) -- nothing
// upstream can ever produce a fifth. gltf::Skeleton::Joint carries this
// as a string because glTF extras need one; canon has no such
// constraint, so the fixed set gets a real enum instead of re-stringifying
// it only to have every consumer parse the string back apart.
enum class BillboardMode {
    None,
    Spherical,
    CylindricalLockX,
    CylindricalLockY,
    CylindricalLockZ,
};

// One joint's bind pose + hierarchy position. M2 bind pose carries no
// baked rotation or scale (see gltf::Skeleton::Joint::globalPosition's
// doc comment) -- translation is the whole bind-pose fact.
struct Joint {
    int parent = -1;  // index into the owning Skeleton::joints, -1 for a root

    // Bind-pose position, absolute (not parent-relative) -- a writer that
    // needs a parent-relative offset derives it from the hierarchy, same
    // as any other absolute-coordinate canon field (I1: no glTF-shaped
    // "already relative to parent" convenience baked in here).
    m2::Vec3 globalPosition;

    BillboardMode billboard = BillboardMode::None;

    // id = RecordIndex{bone index} via boneRef; name/source are
    // NameSource::M2Embedded (m2::keyBoneName table hit) or
    // NameSource::Synthesized ("bone_<index>" fallback) -- the exact
    // distinction NameSource exists to carry (canon_ref.hpp).
    Ref ref;

    // DESIGN.md's "canon:: bone naming" tier 2: a deterministic,
    // chain/symmetry-derived label, computed unconditionally for every
    // joint (named or not) -- a genuinely separate fact from ref.name,
    // which stays hand-authored-or-nothing. See canon_bone_naming.hpp.
    std::string structuralLabel;
};

struct Skeleton {
    std::vector<Joint> joints;
};

}  // namespace husk::canon
