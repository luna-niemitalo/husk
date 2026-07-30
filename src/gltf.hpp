#pragma once

#include <array>
#include <cstdint>
#include <optional>
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
    // WoW's M2Material flag 0x01 ("Unlit" -- wowdev.wiki
    // M2#Render_flags_and_blending_modes): rendered without directional
    // lighting in the real client. Translated to glTF's own
    // KHR_materials_unlit extension (writeGlb adds it to the document's
    // extensionsUsed list whenever any material sets this).
    bool unlit = false;
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

    // One M2Batch texture layer beyond the first (textureCount > 1, e.g. a
    // second env-mapped "shine" pass on armor, or a genuine two-texture
    // blend -- wowdev.wiki M2/.skin#Texture_units) -- see FAILURES2.md #6.
    // husk doesn't fake WoW's fixed-function combiner math (Mod2x/Add/...)
    // by wiring this into pbrMetallicRoughness; it's exposed as inert
    // metadata instead (glTF extras + an unused auxiliary image/texture, if
    // one was embeddable), the same "tag it, don't guess at semantics"
    // treatment `billboardMode` already gets, for a custom renderer or a
    // Blender script (material node setup, geometry nodes, ...) to act on.
    struct AdditionalTextureLayer {
        uint32_t fileDataId = 0;  // 0 if this texture isn't file-based (see m2::Texture)
        int texCoord = 0;         // which UV set this layer samples, same convention as baseColorTexCoord
        // Raw encoded image bytes (PNG), or empty if no --textures match was
        // found for this FileDataID -- same "opaque bytes, not decoded"
        // policy as baseColorImagePng.
        std::vector<uint8_t> imagePng;
    };
    std::vector<AdditionalTextureLayer> additionalTextureLayers;

    // A batch's UV scroll/rotate/scale animation (M2TextureTransform,
    // wowdev.wiki M2#Texture_Transforms), when its textureTransformComboIndex
    // resolves to one -- see m2::TextureTransform's doc comment for why
    // this is exposed as inert extras, the same "tag it, don't guess at
    // semantics" treatment additionalTextureLayers/billboardMode get,
    // rather than a real KHR_texture_transform applied to the render.
    // `constant`, when true, means translation/rotation/scaling below are
    // real resolved static values; when false (the animated case -- e.g.
    // scrolling lava/water, almost certainly the common one in practice),
    // they're just each field's un-animated default and not real data.
    struct TextureTransform {
        bool constant = true;
        Vec3 translation;                  // defaults to (0,0,0), Vec3's own default
        float rotation[4] = {0, 0, 0, 1};  // x,y,z,w quaternion, identity default
        Vec3 scaling{1, 1, 1};
    };
    std::optional<TextureTransform> textureTransform;
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
    // The submesh's M2SkinSection::skinSectionId (the "geoset ID" -- see
    // src/skin.hpp's Submesh doc comment, FAILURES2.md #1), or -1 if this
    // primitive didn't come from a real .skin submesh (the batches.empty()
    // fallback case, see cmd_export.cpp's buildMaterialsAndPrimitives).
    // husk doesn't filter by this -- every submesh is always exported,
    // including mutually-exclusive character-customization options -- this
    // is purely inert metadata (glTF extras) for a custom renderer or a
    // Blender script (mesh mask / geometry nodes / driven material, ...) to
    // implement its own geoset selection with, the same "tag it, don't
    // guess at semantics" treatment `billboardMode` already gets.
    int skinSectionId = -1;
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
//
// More than one joint may have parent == -1 -- a real, common M2 shape
// (35% of a real 130k-file corpus, tools/find_multiroot_skeletons.py), not
// corruption, and never rejected here. `joints` itself is never reordered
// or added to on account of this -- see writeGlbMulti's doc comment for how
// the resulting glTF-side multi-root forest is made tooling-friendly
// without touching this vector at all.
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
        // Non-empty ("spherical"/"cylindrical_lock_x"/"_y"/"_z" -- see
        // husk::m2::billboardModeName) for a billboarded joint: this
        // becomes a `"billboard"` key in the joint's glTF node `extras`,
        // for a custom renderer to act on (billboarding is a
        // renderer-camera-relative behavior with no core-glTF equivalent,
        // so this is metadata for the consumer, not something writeGlb
        // itself applies). Empty string (the default): no extras added.
        std::string billboardMode;
    };
    std::vector<Joint> joints;

    // Real husk::bone::Correction data (one entry per resolved `.bone`
    // sidecar file, `husk export`'s `--bones-dir`) -- inert glTF `extras`
    // on the skin object (`bone_correction_sets`), never applied to the
    // bind pose or any animation. Which of a model's several `.bone` files
    // is "correct" for a given character is selected by client-side
    // customization-choice data husk has no access to (no CASC/DBC access,
    // a hard non-goal -- see DESIGN.md's Non-goals, TODO_correctness.md #3,
    // WIKI_FINDINGS.md §4); husk surfaces every slot it can resolve from
    // disk and stops there, same "tag it, don't guess at semantics"
    // treatment as skinSectionId/textureTransform above. Empty (the
    // default) if `--bones-dir none`, or none of a model's BFID-declared
    // FileDataIDs resolved to a real file on disk.
    struct CorrectionSet {
        uint32_t fileDataId = 0;
        struct Correction {
            int joint = -1;  // index into Skeleton::joints
            // Row-major, same convention as husk::bone::Correction::matrix.
            std::array<float, 16> matrix{};
        };
        std::vector<Correction> corrections;
    };
    std::vector<CorrectionSet> correctionSets;

    // Minimal placement anchors for M2Ribbon/M2Particle emitters -- just
    // enough (id, attach joint, relative position) for a custom Blender
    // script to place a marker/empty at the right spot, same "inert glTF
    // extras, never applied to anything writeGlb itself renders" treatment
    // as CorrectionSet above. Every other field (blend mode, texture,
    // curves, ...) has no native glTF slot either, but is high-volume
    // enough (potentially dozens of emitters, each with several animation
    // curves) that it lives in `husk dump-chunks`'s JSON output instead of
    // bloating every .glb with data a plain glTF viewer can't use -- see
    // DESIGN.md's Key design decisions and src/cmd_dump.cpp's doc comment.
    struct EmitterAnchor {
        uint32_t id = 0;   // M2Ribbon::ribbonId / M2Particle::particleId, usually -1
        int joint = -1;    // index into Skeleton::joints
        // Relative to `joint`, already in the target coordinate system
        // (Y-up) -- same caller responsibility as Joint::localTranslation.
        Vec3 position;
    };
    std::vector<EmitterAnchor> ribbonAnchors;
    std::vector<EmitterAnchor> particleAnchors;
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

// One joint's worth of keyframe data for one animation clip -- glTF's own
// sampler+channel pair, pre-split by TRS property since husk never shares
// a sampler between properties or joints. Each `*Times`/`*Values` pair
// must be the same length; leave a property's pair empty to skip emitting
// that channel entirely for this joint (the ordinary case: most joints
// won't have data for most sequences, see husk::m2::Sequence's doc
// comment). Times are seconds, strictly increasing; values are already in
// the target coordinate system/quaternion convention (Y-up) and, for
// translation, already relative to the joint's parent the same way
// Skeleton::Joint::localTranslation is -- writeGlb samples these directly
// as the node's translation/rotation/scale, it does not add them to the
// bind pose.
struct JointAnimation {
    int joint = -1;  // index into Skeleton::joints, not a glTF node index
    std::vector<float> translationTimes;
    std::vector<Vec3> translationValues;
    // Per-property "step" flag (M2Track::interpolation_type == 0, "values
    // change instantly at the timestamp, with no interpolation whatsoever"
    // -- wowdev.wiki M2#Interpolation), each independent since translation/
    // rotation/scale are three separate M2Tracks with their own
    // interpolation_type. false (the default) means glTF's own LINEAR
    // sampler interpolation; true means STEP. There's no third glTF-side
    // option here -- interpolation_type 2/3 (cubic bezier/hermite) never
    // reaches this struct, since husk::m2::resolveVec3TrackSequence/
    // resolveQuatTrackSequence throw rather than resolve one (see their doc
    // comments): those types are only valid for M2SplineKey tracks, which
    // bone/color/weight tracks never are.
    bool translationStep = false;
    std::vector<float> rotationTimes;
    std::vector<Quat> rotationValues;
    bool rotationStep = false;
    std::vector<float> scaleTimes;
    std::vector<Vec3> scaleValues;
    bool scaleStep = false;
};

// One glTF animation clip. `joints` may be sparse -- only joints with real
// keyframe data for this clip need an entry (see husk::m2::Sequence's
// flags for why most of a real model's sequences will only cover a subset
// of its joints, or none at all) -- but every JointAnimation::joint must be
// in range for whatever Skeleton is passed to writeGlb alongside this.
struct Animation {
    std::string name;
    std::vector<JointAnimation> joints;
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
// If `skeleton` is non-null (and non-empty), it adds a joint node per
// skeleton entry (parented per `Joint::parent`) and a glTF skin with
// inverse bind matrices regardless of `mesh.skinning`. Whether `mesh`
// itself is deformed by that skeleton is decided by `mesh.skinning`:
// non-empty (must be the same length as `mesh.positions`) attaches the skin
// to `mesh`'s node and adds JOINTS_0/WEIGHTS_0 accessors; empty leaves
// `mesh`'s node unskinned even though the skeleton/skin still exist in the
// document (see writeGlbMulti's doc comment for why -- an unskinned
// NamedMesh entry alongside a skinned one is the concrete use case). If
// `skeleton` is null, `mesh.skinning` must be empty -- no orphaned skinning
// data. Throws Error on any joint's `parent` being out of range or
// self-referential, or on the skeleton/skinning-data mismatches above.
// `skeleton->correctionSets`, if non-empty, becomes a
// `bone_correction_sets` key in the skin's own glTF `extras` (see
// Skeleton::CorrectionSet's doc comment) -- Error if any correction's
// `joint` is out of range for `skeleton`. `skeleton->ribbonAnchors`/
// `particleAnchors`, if non-empty, likewise become `ribbon_emitters`/
// `particle_emitters` keys on the same skin `extras` object (see
// Skeleton::EmitterAnchor's doc comment) -- Error under the same
// out-of-range-joint condition.
//
// `animations`, if non-empty, requires a non-null/non-empty `skeleton` --
// each becomes one glTF animation clip (see Animation's doc comment).
// Throws Error if any JointAnimation::joint is out of range for `skeleton`,
// or any of its three time/value pairs have mismatched lengths.
std::vector<uint8_t> writeGlb(const Mesh& mesh, const std::vector<Material>& materials = {},
                               const Skeleton* skeleton = nullptr,
                               const std::vector<Animation>& animations = {});

// One mesh's worth of writeGlb's `mesh`/`materials` pair, plus a `name` that
// becomes its glTF node's `name` -- the unit `writeGlbMulti` (below) repeats
// once per entry. `materials` is this mesh's own list; `mesh.primitives[i]
// .materialIndex` indexes into it, not into any other entry's list (each
// gets its own local numbering, same convention as writeGlb's single-mesh
// case -- writeGlbMulti remaps them into one shared glTF materials array
// internally).
struct NamedMesh {
    std::string name;
    Mesh mesh;
    std::vector<Material> materials;

    // True for a physics/hit-testing collision mesh (husk::m2::CollisionMesh),
    // distinct from every other NamedMesh entry (a render mesh, one per LOD
    // tier). Adds a `{"collision": true}` key to this entry's glTF node
    // `extras`, the same "tag it, don't guess at semantics" treatment
    // skinSectionId/billboardMode already get -- writeGlbMulti doesn't skip
    // rendering it (no core-glTF "don't draw this" flag exists), only marks
    // it for a custom renderer or Blender script to filter out. Implies
    // `mesh.skinning` is left empty (see writeGlbMulti's doc comment for
    // sharing a skeleton without being skinned by it) -- a collision mesh is
    // static, not deformed by the armature.
    bool isCollision = false;
};

// Serializes multiple meshes into one .glb, each as its own named node (and
// its own glTF mesh/primitives/materials) -- husk export --lod all's case
// (see README.md): one node per LOD tier of the same M2, so a DCC tool's
// outliner shows them as separate, individually-toggleable objects instead
// of silently overwriting one another. `meshes` may only be empty when
// `skeleton` is non-null and has at least one joint -- a genuinely
// geometry-less M2 (a pure particle/ribbon VFX model, 3,807 real corpus
// files have zero vertices at the M2 level, not just an empty
// .skin) still exports its skeleton and ribbon/particle emitter anchors
// (Skeleton::ribbonAnchors/particleAnchors), just with no mesh node at all
// -- glTF has no valid "mesh with zero primitives" representation to fall
// back to instead (see cmd_export.cpp's per-LOD-tier skip). Otherwise
// (`meshes` empty and no skeleton), Error -- there'd be nothing to put in
// the document. Every non-empty entry is validated exactly like writeGlb's
// single `mesh`/`materials` (same Error conditions, "writeGlbMulti" in
// place of "writeGlb" in the message).
//
// If `skeleton` has more than one root joint (`Joint::parent == -1` on more
// than one entry -- a real, common M2 bone-forest shape, not corruption,
// see Skeleton's own doc comment above), one
// additional plain (non-joint) glTF node is synthesized as the parent of
// every real root joint, appended past the end of the joint-node range
// (index `meshCount + skeleton->joints.size()`) with a default/identity
// transform. It becomes the sole scene-root entry standing in for those
// joints (each real root is still reached via this node's own `.children`,
// not listed individually) and `skin.skeleton` is set to it -- the shape
// glTF's own tooling ecosystem already anticipates for multi-rooted
// skeletons (see github.com/KhronosGroup/glTF/issues/1270). It is never
// added to `skin.joints` and never gets an inverse bind matrix -- every
// vertex/emitter-anchor/correction/animation joint index still refers to a
// real M2 bone by its original, unshifted position (the one invariant this
// must never break -- see Skeleton's own doc comment above). A single-root skeleton (the
// overwhelming majority of real models) is unaffected: no synthetic node,
// `skin.skeleton` left unset, output identical to before this existed.
//
// `skeleton`/`animations` are shared across every entry, not per-mesh --
// valid because every LOD of one M2 draws from the same `bones` array (only
// the triangle/vertex-index *subset* a .skin selects differs per LOD, see
// src/skin.hpp), so one bind-pose skeleton and one set of animation clips
// cover all of them. If `skeleton` is given, each entry independently
// chooses skinned (mesh.skinning non-empty, must match that entry's own
// mesh.positions length) or unskinned (mesh.skinning left empty -- the
// node gets no glTF `skin` reference at all, and isn't deformed by the
// armature) -- e.g. a render mesh alongside an unskinned collision-mesh
// entry, parented directly to the scene root like any other node. There's
// exactly one glTF skin object, shared by every *skinned* mesh node.
//
// writeGlb(mesh, materials, skeleton, animations) is exactly
// writeGlbMulti({{"", mesh, materials}}, skeleton, animations) -- the
// single-mesh case is this function with one, unnamed, entry.
std::vector<uint8_t> writeGlbMulti(const std::vector<NamedMesh>& meshes,
                                    const Skeleton* skeleton = nullptr,
                                    const std::vector<Animation>& animations = {});

}  // namespace husk::gltf
