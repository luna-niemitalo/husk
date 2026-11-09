#pragma once

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// canon::'s own Vec2/Vec3/Quat value types -- REFACTOR/CANONICAL_MODEL.md's
// I1 ("what canon must not contain") constrains canon:: struct fields, not
// just assembly function signatures (see that doc's own extended section).
// canon:: is meant to work unchanged whether the source format is M2
// or something else entirely, so a field typed m2::Vec3 is the wrong
// namespace even though the bytes are identical. These three exist so every
// producer (m2_mesh_input.cpp, m2_skeleton_input.cpp, m2_material_input.cpp,
// m2_animation_input.cpp) converts at its own input-module boundary instead
// of canon:: borrowing M2's types by convenience.
//
// Quat's field order matches m2::Quat/Blizzard's own C4Quaternion (w last,
// not first) -- not a canon:: convention of its own, just the layout every
// real producer already has in hand, so the boundary conversion stays a
// plain memberwise copy rather than a reordering.
namespace husk::canon {

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

}  // namespace husk::canon
