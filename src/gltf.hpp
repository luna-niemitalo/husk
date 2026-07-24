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

// Up to 4 (joint index, weight) pairs for one vertex -- glTF's
// JOINTS_0/WEIGHTS_0 attributes, lifted straight from M2Vertex's
// bone_indices[4]/bone_weights[4] (see husk::m2::Vertex). An unused slot is
// weight 0, any joint index (glTF ignores zero-weight joints).
struct JointWeights {
    uint8_t joints[4] = {0, 0, 0, 0};
    float weights[4] = {0, 0, 0, 0};
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
    // Optional per-vertex skinning data. Leave empty for an unskinned mesh;
    // if non-empty, must be given alongside a Skeleton (see writeGlb) and
    // have exactly one entry per position.
    std::vector<JointWeights> skinning;
};

// A bind-pose skeleton: joints[i].parent is an index into this same
// vector, or -1 for a root joint. Both translations are already in the
// target coordinate system (Y-up), same caller responsibility as Mesh's
// positions/normals.
struct Skeleton {
    struct Joint {
        int parent = -1;
        // Relative to `parent`'s globalPosition (or to the armature's local
        // origin, for a root joint) -- becomes the joint node's glTF
        // `translation`.
        Vec3 localTranslation;
        // Absolute bind-pose position, used only to compute this joint's
        // inverse bind matrix (a pure translation, since M2's bind pose has
        // no baked rotation/scale -- see README.md roadmap stage 2).
        Vec3 globalPosition;
    };
    std::vector<Joint> joints;
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
// primitive, one node, one scene. No material, no image -- later roadmap
// stages add those. Throws Error if positions/normals/texCoords aren't all
// the same length, or indices is empty or not a multiple of 3.
//
// If `skeleton` is non-null (and non-empty), `mesh.skinning` must be
// non-empty and the same length as `mesh.positions`: this adds a joint node
// per skeleton entry (parented per `Joint::parent`), a glTF skin with
// inverse bind matrices, and JOINTS_0/WEIGHTS_0 accessors on the mesh
// primitive. If `skeleton` is null, `mesh.skinning` must be empty -- no
// orphaned skinning data. Throws Error on any joint's `parent` being out of
// range or self-referential, or on the skeleton/skinning-data mismatches
// above.
std::vector<uint8_t> writeGlb(const Mesh& mesh, const Skeleton* skeleton = nullptr);

}  // namespace husk::gltf
