#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gltf_math.hpp"

// Mesh-side glTF export data model (FILE_SPLIT_TODO.md Item 3): per-vertex
// skinning weights, materials/textures, triangle-index primitives, and the
// Mesh/NamedMesh containers writeGlbMulti (gltf.hpp) consumes. Depends only
// on gltf_math.hpp's Vec2/Vec3.
namespace husk::gltf {

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
    // blend -- wowdev.wiki M2/.skin#Texture_units).
    // TODO: Remove: FAILURES2.md #6.
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

    // M2Texture::type (wowdev.wiki M2#Textures) for this batch's primary
    // texture -- 0 ("NONE") means a real, filename/FileDataID-based texture
    // (the ordinary case); any other value means the client resolves this
    // slot at runtime from DBC-driven character-customization/item-tint data
    // husk has no access to (see m2::Texture's doc comment), so an empty
    // baseColorImagePng here is a real "husk can't resolve this slot at
    // all" signal, not "the --textures directory just didn't have the
    // file." Extras-only (`texture_type`), and -- like additionalTextureLayers/
    // textureTransform above -- present only when there's something extra
    // to say: a nonzero value. 0 is the assumed default when the key is
    // absent, matching every other extras field's own "absence means
    // ordinary" convention, so writing it out for the common case would be
    // redundant.
    uint32_t textureType = 0;

    // The resolved M2/.skin FileDataID for baseColorImagePng's texture,
    // when one exists -- set whenever the batch's own textureCombos ->
    // texture -> TXID lookup resolved a real FileDataID, *regardless* of
    // which local file (the exact "<FileDataID>.png", the M2's own
    // embedded filename, or a basename-fuzzy match) actually supplied the
    // embedded bytes. Extras-only (`texture_file_data_id`), same
    // "present only when there's something extra to say, 0 means absent"
    // convention as textureType above -- a real client-side FileDataID is
    // never 0, so 0 unambiguously means "husk never resolved one for this
    // slot," not "the real ID happens to be zero." Matters because real
    // texture directories are often named descriptively rather than by
    // FileDataID.
    uint32_t baseColorTextureFileDataId = 0;

    // A hardcoded/customization-driven texture slot (textureType != 0) with
    // 2+ same-basename candidate files in --textures has no in-file data to
    // pick "the" correct one from (see textureType's own doc comment) -- but
    // unlike that structural gap, the candidate *list itself* is real,
    // directory-scanned data (FuzzyTexturePool, src/export_materials.cpp),
    // the same source a sole (unambiguous) match already uses. Same "export
    // everything, let the client filter" treatment `Submesh::skinSectionId`
    // already gets for mutually-exclusive geosets: every candidate is
    // embedded (real image bytes, not just a filename), one arbitrary one
    // (alphabetically first, deterministic) also becomes the actual
    // baseColorImagePng so the export still renders as *something* by
    // default rather than bare, and the extras array lets a human or script
    // swap in the real one once known. Empty in every other case (a sole
    // match, or a genuinely FileDataID-resolved slot, stay exactly as
    // precise as before -- this only fires on real ambiguity).
    struct AlternateTextureCandidate {
        std::string filename;  // real file basename, e.g. "bloodelffemale_hd_skin_color_3500123.png"
        std::vector<uint8_t> imagePng;
    };
    std::vector<AlternateTextureCandidate> alternateTextureCandidates;

    // A batch's M2Color::color/M2TextureWeight::weight (colorAnimated/
    // weightAnimated) is real per-sequence or global-sequence keyframe
    // animation, not the single constant value baseColorFactor can hold --
    // see m2::Color/TextureWeight's doc comments. Core glTF has no
    // animation-channel target for a material property at all, so there's
    // no real *playback* to build (unlike a bone's translation/rotation/
    // scale) -- but the full curve is still real, useful data, resolved via
    // resolveVec3TrackSequence/resolveRawIntTrackSequence the same way bone
    // tracks are (see cmd_export.cpp's buildMaterialsAndPrimitives) and
    // attached as inert extras for a custom renderer or Blender script to
    // play back itself. One entry per M2Sequence that has real inline data
    // for this track, in the model's own sequence-array order, plus --
    // when the track is global-sequence-driven instead -- one synthetic
    // entry with `sequenceIndex == -1` (a continuous loop independent of
    // any M2Sequence, see resolveVec3GlobalSequenceTrack's doc comment).
    struct AnimatedColorCurve {
        int sequenceIndex = -1;  // -1 == global-sequence-driven, not tied to one M2Sequence
        std::vector<std::pair<float, Vec3>> keyframes;  // seconds -> rgb 0..1, NOT a spatial vector
    };
    struct AnimatedScalarCurve {
        int sequenceIndex = -1;
        std::vector<std::pair<float, float>> keyframes;  // seconds -> 0..1
    };
    // M2Color::color's animated curve -- extras key "tint_animation". Empty
    // when colorAnimated is false (either genuinely no data, or a constant
    // value already folded into baseColorFactor above).
    std::vector<AnimatedColorCurve> tintAnimation;
    // M2Color::alpha's animated curve -- extras key "fade_animation"."alpha".
    std::vector<AnimatedScalarCurve> alphaFadeAnimation;
    // M2TextureWeight::weight's animated curve -- extras key
    // "fade_animation"."weight". Multiplies with alphaFadeAnimation the same
    // way the static baseColorFactor[3] path does (see cmd_export.cpp) --
    // husk doesn't combine the two itself (that needs resampling both onto
    // a shared timeline, real work with no glTF slot to justify it here),
    // exposed separately for a downstream consumer to combine.
    std::vector<AnimatedScalarCurve> weightFadeAnimation;
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
    // src/skin.hpp's Submesh doc comment), or -1 if this primitive didn't
    // come from a real .skin submesh (the batches.empty() fallback case,
    // see cmd_export.cpp's buildMaterialsAndPrimitives).
    // TODO: Remove: FAILURES2.md #1.
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

// One mesh's worth of writeGlb's `mesh`/`materials` pair, plus a `name` that
// becomes its glTF node's `name` -- the unit `writeGlbMulti` (gltf.hpp)
// repeats once per entry. `materials` is this mesh's own list; `mesh
// .primitives[i].materialIndex` indexes into it, not into any other entry's
// list (each gets its own local numbering, same convention as writeGlb's
// single-mesh case -- writeGlbMulti remaps them into one shared glTF
// materials array internally).
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

}  // namespace husk::gltf
