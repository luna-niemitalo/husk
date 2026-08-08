#pragma once

// WoW->glTF coordinate-frame primitives: the plain Vec3/Vec2/Quat value
// types every other gltf_*.hpp uses, plus the Z-up->Y-up axis-conversion
// functions built on them. Self-contained -- no dependency on gltf_mesh.hpp/
// gltf_skeleton.hpp/gltf.hpp (FILE_SPLIT_TODO.md Item 3).
namespace husk::gltf {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Vec2 {
    float x = 0, y = 0;
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

// WoW models are Z-up; glTF is Y-up. These three functions are the single
// source of truth for that conversion -- every position, normal, rotation,
// and scale husk ever exports goes through exactly one of them, and all
// three are mechanically derived from one shared 3x3 change-of-basis matrix
// (kWowToGltf, gltf_math.cpp) rather than being three independently hand-typed
// formulas that could silently drift out of sync with each other.
//
// Applies equally to positions and normals (both are plain directions/
// points in the same space); texture coordinates are untouched by this, not
// spatial.
Vec3 zUpToYUp(const Vec3& v);

// A bone-rotation quaternion's vector part gets the same kWowToGltf
// transform as a position, mechanically (convert to a 3x3 rotation matrix,
// conjugate by kWowToGltf, convert back), not a separately hand-typed
// formula -- derived the same way as zUpToYUp. The scalar part (w) is
// basis-independent and untouched either way.
Quat rotationZUpToYUp(const Quat& q);

// A scale vector (the diagonal of a scale matrix) gets kWowToGltf's
// permutation with the signs stripped -- valid specifically because
// kWowToGltf is a *signed permutation* matrix (conjugating a diagonal
// matrix by one just permutes the diagonal; the signs cancel). This
// assumption would need revisiting if kWowToGltf ever became a general
// rotation matrix -- derived the same way as zUpToYUp.
Vec3 scaleZUpToYUp(const Vec3& s);

}  // namespace husk::gltf
