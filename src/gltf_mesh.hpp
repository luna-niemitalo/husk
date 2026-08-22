#pragma once

#include <array>
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
    // The real, un-collapsed WoW M2BLEND_* value (wowdev.wiki
    // M2/Rendering#M2BLEND) alphaMode above was derived from. Core glTF
    // only has three alpha behaviors (Opaque/Mask/Blend) -- modes 3+
    // (NoAlphaAdd/Add/Mod/Mod2x/...) have no core equivalent at all and
    // collapse to Blend, which is a real, visible wrong-answer for an
    // additive-designed asset (e.g. a mostly-black particle/glow texture:
    // WoW adds it, contributing nothing where black; naive alpha-blend
    // instead shows a solid dark panel -- confirmed against a real corpus
    // render, `creature/celestialfoxwyvern/celestialfoxwyvern.m2`,
    // blend_mode 4). Extras-only (`blend_mode`), present only for
    // blendMode > 2 -- 0 (Opaque) and 1 (AlphaKey) map to alphaMode
    // exactly (Opaque/Mask respectively) with nothing lost, and 2 (a real
    // alpha blend) maps to Blend exactly too; only 3+ is where the
    // collapse above actually throws information away, so that's the only
    // range worth a consumer reading. A Blender-side companion script can
    // rebuild real additive/multiply shading the way
    // `tools/husk_blender_geoset_mask.py` already rebuilds geoset
    // selection and texture-layout overlays for other core-glTF gaps.
    uint16_t blendMode = 0;
    // The real Combiners_*/Diffuse_* pixel/vertex shader names the Cata+
    // client would pick for this batch (M2Batch::shaderId + textureCount,
    // husk::m2::resolveShaderNames -- see src/m2_shader_names.hpp's doc
    // comment for the wowdev.wiki source and TODO/MULTI_TEXTURE_LAYER_TODO.md
    // for why this -- not a WotLK-era heuristic -- is the mechanism that
    // applies here). Extras-only (`shader_names`/`pixel_shader`/
    // `vertex_shader`), same "tag it, don't guess at rendering" treatment
    // blendMode above gets; empty when shaderId didn't resolve (an
    // out-of-range 0x8000 table index -- rare, see resolveShaderNames).
    std::string pixelShaderName;
    std::string vertexShaderName;
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
    // The real source filename (no extension) that supplied
    // baseColorImagePng -- e.g. "bloodelffemale_hd_hair_color_5196731" --
    // set at every resolution site in export_materials.cpp (M2's own
    // embedded filename, a "<FileDataID>" exact match, a sole fuzzy match,
    // or the chosen candidate out of an ambiguous pool). Used only to name
    // the emitted glTF `Image`/`Texture` (gltf_mesh.cpp's emitMaterial) so
    // Blender's importer shows this real name instead of an auto-generated
    // "Image_<N>" -- purely cosmetic, glTF's own `baseColorTexture.index`
    // reference doesn't depend on it at all. Empty only when
    // baseColorImagePng itself is (nothing resolved).
    std::string baseColorImageName;
    // Real --listfile content-path stem (no extension) for
    // baseColorTextureFileDataId, e.g. "scalpupperhair00_08" --
    // independent of which tier actually supplied baseColorImagePng's
    // bytes (a local "<FileDataID>.png" file can exist even when the
    // listfile also knows this FileDataID's real content-relative path,
    // in which case baseColorImageName above stays the bare FileDataID
    // string but this field carries the real name). Set in
    // export_materials.cpp whenever --listfile resolves
    // baseColorTextureFileDataId, regardless of embed path. Used to
    // prefer a clean, human name over a bare FileDataID for
    // --slim-textures' written filename (gltf_mesh.cpp's
    // writeSlimTextureFile). Empty when --listfile wasn't given, or didn't
    // resolve this FileDataID.
    std::string realContentName;
    // Real `ChrCustomizationOption`/`Choice` name(s) this material's own
    // baseColorTextureFileDataId cross-references against the model's
    // real customization menu (gltf::Skeleton::customizationOptions),
    // when one exists -- set in export_materials.cpp. Used for both the
    // material's own display `name` (below) and, when realContentName
    // above is empty, --slim-textures' written filename -- same priority
    // in both cases. Empty when this material's texture isn't a real
    // customization choice (e.g. any non-character prop/weapon material,
    // or a character material husk couldn't cross-reference at all).
    std::string customizationOptionName;
    std::string customizationChoiceName;
    // The full verbose diagnostic chain this material's own `name` field
    // used to be unconditionally
    // ("mat<N>_tex<T>_<typeName>_fdid<id>_<embeddedStem-or-nameSuffix>",
    // export_materials.cpp) -- still computed and carried here (extras-only,
    // `diagnostic_name`) so a Blender material can still be cross-referenced
    // back to its source .skin batch/texture index, now that `name` itself
    // prefers a cleaner, human-readable identity when one resolves (real
    // customization choice/option name, else a bare textureTypeName, else
    // this chain -- see export_materials.cpp's name-priority assignment).
    // Never empty once a material is built (always at least "mat<N>"),
    // same "always present, cheap" treatment textureType's own
    // bookkeeping gets elsewhere in this file.
    std::string diagnosticName;
    // Which of Mesh::texCoords (0) or Mesh::texCoords2 (1) baseColorImagePng
    // should be sampled with -- from the .skin Batch's own
    // textureCoordComboIndex (wowdev.wiki M2/.skin#geosetIndex's "Texture
    // mapping lookup table": -1/0/1 for envmap/UV0/UV1). Ignored when
    // baseColorImagePng is empty. Environment mapping (-1) has no glTF
    // equivalent and isn't attempted -- callers fall back to 0.
    int baseColorTexCoord = 0;

    // One M2Batch texture layer beyond the first (textureCount > 1, e.g. a
    // second env-mapped "shine" pass on armor, or a genuine two-texture
    // blend -- wowdev.wiki M2/.skin#Texture_units, real combiner formulas
    // transcribed in documentation/wowdev-wiki/wikitext/
    // Pixel_shader_logic_for_mixing_colors.wiki -- see
    // TODO/MULTI_TEXTURE_LAYER_TODO.md for the real rendering plan).
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
    // resolves to one. `constant`, when true, means translation/rotation/
    // scaling below are real resolved static values; when false (the
    // animated case -- e.g. scrolling lava/water, almost certainly the
    // common one in practice), they're just each field's un-animated
    // default and not real data -- KHR_texture_transform's extension has no
    // animation-channel target, so the animated case has no honest glTF
    // representation regardless of effort and stays extras-only (see
    // gltf_mesh.cpp's emitMaterial). The constant case *is* wired to a real
    // KHR_texture_transform on baseColorTexture, whenever the rotation is a
    // pure planar (Z-axis) one and a baseColorTexture exists to attach the
    // extension to -- see gltf_mesh.cpp's textureTransformToKhr and
    // DESIGN.md's Key design decisions for the pivot-correction derivation.
    // These raw values are still always surfaced below too, as a
    // diagnostic and as the animated case's only representation.
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
    // embedded (real image bytes, not just a filename), and the extras array
    // lets a human or script swap in the real one once known. Empty in every
    // other case (a sole match, or a genuinely FileDataID-resolved slot,
    // stay exactly as precise as before -- this only fires on real
    // ambiguity).
    //
    // The pool this list is drawn from is filtered to candidates whose
    // filename category (see AlternateTextureCandidate::category) is
    // actually compatible with this slot's own textureType before this
    // struct is ever populated (`export_materials.cpp`'s
    // `candidateAllowedForType`) -- a hair-color file no longer ends up in a
    // jewelry slot's list just because both are ambiguous. One arbitrary
    // *remaining* candidate (bare/`skin_color`-category preferred over a
    // narrower overlay like `face` when this is a compositing slot -- see
    // that function's doc comment -- alphabetically first otherwise,
    // deterministic either way) also becomes the actual baseColorImagePng so
    // the export still renders as *something* plausible by default rather
    // than bare or confidently wrong.
    struct AlternateTextureCandidate {
        std::string filename;  // real file basename, e.g. "bloodelffemale_hd_skin_color_3500123.png"
        // The community-listfile category token parsed out of `filename`
        // (e.g. "skin_color", "face", "hair_color") -- empty for a bare
        // "<model>_<FileDataID>" file, or when the filename doesn't carry a
        // recognized token at all. See `export_materials.cpp`'s
        // `classifyCandidateCategory`/`candidateCategoryTypes` for where
        // this vocabulary comes from (`reference/wow.export`'s own
        // character-customization code, not a husk guess).
        std::string category;
        // Real decoded pixel dimensions (export_materials.cpp's
        // `pngDimensions`) -- not decoration, load-bearing for
        // `orderCandidatesForDefault`'s own ranking (largest wins), and
        // surfaced here so a human comparing candidates in Blender doesn't
        // have to decode each one by hand to tell a full atlas apart from
        // a same-category *patch* meant to be composited onto one small
        // sub-region of it: real evidence found several same-category
        // "skin_color" files are exactly that -- non-transparent overlay
        // patches whose own content pixel-matches one specific region of
        // a much larger sibling (e.g. a chest-strap design confirmed to
        // align exactly with a torso region of the real base atlas), not
        // independent whole-slot alternatives at all.
        uint32_t width = 0;
        uint32_t height = 0;
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
    struct AnimatedQuatCurve {
        int sequenceIndex = -1;  // -1 == global-sequence-driven, not tied to one M2Sequence
        std::vector<std::pair<float, std::array<float, 4>>> keyframes;  // seconds -> x,y,z,w quaternion
    };
    struct AnimatedColorCurve {
        int sequenceIndex = -1;  // -1 == global-sequence-driven, not tied to one M2Sequence
        std::vector<std::pair<float, Vec3>> keyframes;  // seconds -> rgb 0..1, NOT a spatial vector
    };
    struct AnimatedScalarCurve {
        int sequenceIndex = -1;
        std::vector<std::pair<float, float>> keyframes;  // seconds -> 0..1
    };
    // The animated case's actual keyframe data for M2TextureTransform's
    // translation/rotation/scaling tracks -- extras key
    // "texture_transform_animation", same "full curve as inert diagnostic +
    // Blender-script playback source" treatment tintAnimation/
    // alphaFadeAnimation below get (core glTF has no animation-channel
    // target for a material's UV transform either, despite
    // KHR_texture_transform existing for the constant case -- see
    // TextureTransform's own doc comment above). One entry per track that's
    // genuinely animated (TextureTransform::translationAnimated/etc. true);
    // empty when that track is constant (already captured in
    // `textureTransform` above) or has no data at all. translation/scaling
    // reuse AnimatedColorCurve's shape (seconds -> Vec3) even though neither
    // is a color -- same (seconds, Vec3) keyframe shape, no reason to
    // duplicate the struct.
    std::vector<AnimatedColorCurve> textureTransformTranslationAnimation;
    std::vector<AnimatedQuatCurve> textureTransformRotationAnimation;
    std::vector<AnimatedColorCurve> textureTransformScalingAnimation;

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
