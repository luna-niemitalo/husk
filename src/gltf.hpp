#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

// Minimal glTF 2.0 binary (.glb) export, via tinygltf (see nix/flake.nix) --
// husk builds the mesh data and hands it to tinygltf rather than
// hand-rolling glTF's JSON/binary-chunk framing itself. Scope matches
// roadmap stage 1 in README.md ("Static mesh, no material"): one
// single-primitive mesh, no material, no image, no skin.
namespace husk::gltf {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Vec2 {
    float x = 0, y = 0;
};

// Mesh data ready to serialize, already in the target coordinate system
// (Y-up) -- writeGlb() does not perform the WoW Z-up -> glTF Y-up
// conversion itself, that's the caller's job (see husk::m2::Vertex).
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    // Flat triangle-corner index buffer; size() must be a multiple of 3.
    std::vector<uint32_t> indices;
};

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// WoW models are Z-up; glTF is Y-up. Per wowdev.wiki M2#Vertices: "to
// convert to Y-up, the X, Y, Z values become (X, -Z, Y)". Applies equally to
// positions and normals (both are plain directions/points in the same
// space); texture coordinates are untouched by this, not spatial.
Vec3 zUpToYUp(const Vec3& v);

// Serializes `mesh` as a minimal glTF binary (.glb): one buffer holding
// positions/normals/texCoords/indices, one mesh with a single TRIANGLES
// primitive, one node, one scene. No material, no image, no skin -- later
// roadmap stages add those. Throws Error if positions/normals/texCoords
// aren't all the same length, or indices is empty or not a multiple of 3.
std::vector<uint8_t> writeGlb(const Mesh& mesh);

}  // namespace husk::gltf
