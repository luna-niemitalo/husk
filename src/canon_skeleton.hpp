#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "canon_physics.hpp"    // boneRef
#include "canon_primitives.hpp" // Vec3
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's Resources layer, the joint hierarchy + bind pose --
// a pure value type. Deliberately excludes geosets (canon::Geoset lives on
// the mesh side, CANONICAL_MODEL.md's "Geosets are first-class") and bone
// corrections/physics/customization
// (each is its own concern with its own canon type or none yet) -- a
// canon::Skeleton is only the hierarchy and bind pose.
namespace husk::canon {

// M2's four billboard flags plus "none" are the complete real set
// (m2::billboardModeName's own if-chain, m2_header.cpp) -- nothing
// upstream can ever produce a fifth, so the fixed set is a real enum
// rather than a string every consumer would parse back apart.
enum class BillboardMode {
    None,
    Spherical,
    CylindricalLockX,
    CylindricalLockY,
    CylindricalLockZ,
};

// One joint's bind pose + hierarchy position. M2 bind pose carries no
// baked rotation or scale (M2CompBone stores only a pivot) -- translation
// is the whole bind-pose fact.
struct Joint {
    int parent = -1;  // index into the owning Skeleton::joints, -1 for a root

    // Bind-pose position, absolute (not parent-relative) -- a writer that
    // needs a parent-relative offset derives it from the hierarchy, same
    // as any other absolute-coordinate canon field (I1: no glTF-shaped
    // "already relative to parent" convenience baked in here).
    Vec3 globalPosition;

    BillboardMode billboard = BillboardMode::None;

    // id = RecordIndex{bone index} via boneRef; name/source are
    // NameSource::M2Embedded (m2::keyBoneName table hit) or
    // NameSource::Synthesized ("bone_<index>" fallback) -- the exact
    // distinction NameSource exists to carry (canon_ref.hpp).
    Ref ref;

    // REFACTOR/CANON_DESIGN.md's "canon:: bone naming" tier 2: a deterministic,
    // chain/symmetry-derived label, computed unconditionally for every
    // joint (named or not) -- a genuinely separate fact from ref.name,
    // which stays hand-authored-or-nothing. See canon_bone_naming.hpp.
    std::string structuralLabel;
};

struct Skeleton {
    std::vector<Joint> joints;
};

// One .bone file: a matrix per corrected joint. How the client applies one
// (multiply order, space, whether children inherit it) is unknown, so a
// set is data only (TODO/BONE_CORRECTION_APPLICATION_TODO.md).
struct CorrectionSet {
    Ref ref;  // id = FileDataId of the .bone
    struct Correction {
        uint32_t joint = 0;
        std::array<float, 16> matrix{};  // row-major, as stored
    };
    std::vector<Correction> corrections;
};

}  // namespace husk::canon
