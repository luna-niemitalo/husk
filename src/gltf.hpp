#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

// Minimal glTF 2.0 binary (.glb) export, via tinygltf (see nix/flake.nix) --
// husk builds the mesh data and hands it to tinygltf rather than
// hand-rolling glTF's JSON/binary-chunk framing itself.
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

// One glTF material, roadmap stage 5 (see README.md): WoW's blend mode
// translated to glTF's alphaMode, and the "two-sided" render flag
// translated to doubleSided. Deliberately not attempting real PBR
// authoring (roughness/metalness/normal maps) -- WoW's own shader model
// doesn't map cleanly onto metallic-roughness, so those are left at
// tinygltf/glTF's own defaults (fully rough, non-metal, flat white).
struct Material {
    std::string name;
    enum class AlphaMode { Opaque, Mask, Blend };
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    // RGBA, glTF's own default (opaque white) when left untouched --
    // WoW's per-batch vertex-color tint (RGB) and combined alpha/texture-
    // weight fade (A) land here, a *static* (not animated, roadmap stage 6)
    // approximation -- see husk::m2::Color/TextureWeight's doc comments.
    float baseColorFactor[4] = {1, 1, 1, 1};
    // Raw encoded image bytes (PNG) for baseColorTexture, or empty for no
    // texture -- a material without one still gets its alphaMode/
    // doubleSided/baseColorFactor applied correctly, it just renders as a
    // flat tinted surface in that mode rather than showing the actual WoW
    // texture. husk doesn't decode/encode image formats itself (see blp/,
    // a separate Python tool) -- this is opaque bytes handed straight to
    // tinygltf to embed.
    std::vector<uint8_t> baseColorImagePng;
    // Which of Mesh::texCoords (0) or Mesh::texCoords2 (1) baseColorImagePng
    // should be sampled with -- from the .skin Batch's own
    // textureCoordComboIndex (wowdev.wiki M2/.skin#geosetIndex's "Texture
    // mapping lookup table": -1/0/1 for envmap/UV0/UV1). Ignored when
    // baseColorImagePng is empty. Environment mapping (-1) has no glTF
    // equivalent and isn't attempted -- callers fall back to 0.
    int baseColorTexCoord = 0;
};

// One glTF primitive's worth of triangles: a slice of triangle-corner
// indices (into the positions/normals/texCoords/skinning arrays below),
// drawn with one material. M2 splits a model into per-submesh batches that
// can each use a different material (see src/skin.hpp's Submesh/Batch) --
// this is where that split shows up on the glTF side.
struct Primitive {
    // Flat triangle-corner index buffer; size() must be a multiple of 3.
    std::vector<uint32_t> indices;
    // Index into the `materials` vector passed to writeGlb, or -1 for none
    // (renders with glTF's own default material).
    int materialIndex = -1;
};

// Mesh data ready to serialize, already in the target coordinate system
// (Y-up) -- writeGlb() does not perform the WoW Z-up -> glTF Y-up
// conversion itself, that's the caller's job (see husk::m2::Vertex).
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texCoords;
    // M2's second UV set (M2Vertex.tex_coords[1]) -- optional: leave empty
    // for TEXCOORD_0-only output (every stage before this one), or fill in
    // with exactly one entry per position to also emit TEXCOORD_1. Which
    // set a given material's baseColorTexture actually samples is that
    // Material's own baseColorTexCoord, not decided here.
    std::vector<Vec2> texCoords2;
    // One or more triangle groups, each with its own material -- see
    // Primitive above. Every index in every primitive must be in range for
    // positions/normals/texCoords.
    std::vector<Primitive> primitives;
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
// positions/normals/texCoords/indices, one mesh with one TRIANGLES
// primitive per `mesh.primitives` entry, one node, one scene. Throws Error
// if positions/normals/texCoords aren't all the same length, `primitives`
// is empty, any primitive's indices is empty or not a multiple of 3 or out
// of range for positions, or any primitive's materialIndex is out of range
// for `materials`. `mesh.texCoords2`, if non-empty, must be the same
// length as `mesh.positions` (adds a shared TEXCOORD_1 accessor to every
// primitive) -- Error if it's some other length.
//
// `materials` becomes the glTF document's materials array 1:1 (see
// Material's doc comment) -- pass an empty vector for the roadmap-stage-1-
// through-4 behavior (no material, no image; every primitive must then
// leave materialIndex at -1).
//
// If `skeleton` is non-null (and non-empty), `mesh.skinning` must be
// non-empty and the same length as `mesh.positions`: this adds a joint node
// per skeleton entry (parented per `Joint::parent`), a glTF skin with
// inverse bind matrices, and JOINTS_0/WEIGHTS_0 accessors on every
// primitive. If `skeleton` is null, `mesh.skinning` must be empty -- no
// orphaned skinning data. Throws Error on any joint's `parent` being out of
// range or self-referential, or on the skeleton/skinning-data mismatches
// above.
std::vector<uint8_t> writeGlb(const Mesh& mesh, const std::vector<Material>& materials = {},
                               const Skeleton* skeleton = nullptr);

}  // namespace husk::gltf
