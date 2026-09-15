#pragma once

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// canon::'s own Vec2/Vec3/Quat value types -- REFACTOR/CANONICAL_MODEL.md's
// I1 ("what canon must not contain") constrains canon:: struct fields, not
// just assembly function signatures (see that doc's own extended section);
// canon::Mesh/Skeleton/Curve used to spell these as m2::Vec2/m2::Vec3/
// m2::Quat, a namespaced M2 type riding along on every canon:: struct that
// touches geometry or a curve. The real severity was always closer to
// "wrong namespace on a generic type" than "leaked M2 semantics" -- none of
// m2::Vec3/Vec2/Quat carry M2-specific behavior, just three or four floats
// -- but canon:: is meant to work unchanged whether the source format is M2
// or something else entirely (Luna's own framing, 2026-09-15: "cannon is
// not dependant on m2"), so a field typed m2::Vec3 is still the wrong
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
