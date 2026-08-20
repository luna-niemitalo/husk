#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gltf_math.hpp"
#include "gltf_mesh.hpp"  // Material::AnimatedColorCurve/AnimatedScalarCurve, reused by Light below

// Skeleton-side glTF export data model (FILE_SPLIT_TODO.md Item 3): the
// bind-pose Skeleton, its `.bone`/ribbon/particle/`.phys`/Attachment/Event/
// Light extras, and per-clip JointAnimation/Animation data writeGlbMulti
// (gltf.hpp) consumes. Depends only on gltf_math.hpp's Vec3/Quat.
namespace husk::gltf {

// A bind-pose skeleton: joints[i].parent is an index into this same
// vector, or -1 for a root joint. Both translations are already in the
// target coordinate system (Y-up), same caller responsibility as Mesh's
// positions/normals.
//
// More than one joint may have parent == -1 -- a real, common M2 shape, not
// corruption, and never rejected here. `joints` itself is never reordered
// or added to on account of this -- see writeGlbMulti's doc comment for how
// the resulting glTF-side multi-root forest is made tooling-friendly
// without touching this vector at all.
// TODO: Remove: corpus-scan stat backing "common" (35% of a real 130k-file
// corpus, tools/find_multiroot_skeletons.py).
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
        // A real semantic bone name (e.g. "ArmL", "Head", "FootL" -- from
        // m2::keyBoneName(Bone::keyBoneId)), or empty if this bone isn't in
        // that ~193-entry table. Becomes this joint's glTF node `.name`
        // when non-empty; writeGlbMulti falls back to "bone_<index>"
        // otherwise. Resolved by the caller (m2-specific), not here --
        // gltf.cpp stays M2-format-agnostic, same split billboardMode
        // already follows (a resolved string in, not raw M2 bit flags).
        // Blender's stock glTF importer otherwise falls back to its own
        // generic "Bone"/"Node" numbering, which isn't guaranteed to match
        // husk's own bone-index order, making every index-keyed extras
        // payload (correction-set/emitter-anchor joint indices, `husk
        // info`'s own bone listing) hard to correlate back to what's
        // actually selected in Blender.
        std::string name;
    };
    std::vector<Joint> joints;

    // Real husk::bone::Correction data (one entry per resolved `.bone`
    // sidecar file, `husk export`'s `--bones-dir`) -- inert glTF `extras`
    // on the skin object (`bone_correction_sets`), never applied to the
    // bind pose or any animation. Which of a model's several `.bone` files
    // is "correct" for a given character is selected by client-side
    // customization-choice data husk has no access to (no CASC/DBC access,
    // a hard non-goal -- see DESIGN.md#Non-goals); husk surfaces every slot
    // it can resolve from disk and stops there, same "tag it, don't guess
    // at semantics" treatment as skinSectionId/textureTransform above.
    // Empty (the default) if `--bones-dir none`, or none of a model's
    // BFID-declared FileDataIDs resolved to a real file on disk.
    // TODO: Remove: `WIKI_FINDINGS/BONE.md` (dev-trace
    // citations backing the non-goal above).
    struct CorrectionSet {
        uint32_t fileDataId = 0;
        struct Correction {
            int joint = -1;  // index into Skeleton::joints
            // Row-major, same convention as husk::bone::Correction::matrix.
            std::array<float, 16> matrix{};
        };
        std::vector<Correction> corrections;
        // Real ChrCustomizationChoiceID(s) whose ChrCustomizationBoneSet.
        // BoneFileDataID resolves to this set's fileDataId (`husk export
        // --customization-choice-ids`, src/chrcustomization_db2.hpp) --
        // empty (the default) when no supplied choice ID selects this set,
        // or the flag wasn't used at all. Still doesn't *apply* the
        // correction to the bind pose/animation, same inert-extras
        // treatment as the rest of this struct -- just marks which of
        // several resolved sets a real customization choice actually
        // picks, closing the "which BFID slot applies" half of
        // TODO_correctness.md #2 that plain --bones-dir resolution alone
        // can't answer.
        std::vector<uint32_t> selectedByChoiceIds;
    };
    std::vector<CorrectionSet> correctionSets;

    // Real geoset selections resolved from caller-supplied
    // ChrCustomizationChoiceIDs (`husk export --customization-choice-ids`,
    // src/chrcustomization_db2.hpp) -- inert glTF extras only, same
    // treatment as CorrectionSet above: husk does not filter/hide any
    // primitive based on this, it only attaches the real resolved data for
    // a human/Blender script to act on. `geosetId` uses the same
    // `group*100+variant` convention already used for a primitive's own
    // `geoset_id`/`geoset_group`/`geoset_variant` extras (gltf_mesh.hpp),
    // so a consumer can match this directly against those without any unit
    // conversion. See src/chrcustomization_db2.hpp for the full DB2 chain
    // this resolves.
    struct EnabledGeoset {
        uint32_t choiceId = 0;
        uint32_t geosetId = 0;
    };
    std::vector<EnabledGeoset> enabledGeosets;

    // Real texture-material selections resolved from caller-supplied
    // ChrCustomizationChoiceIDs (same source as EnabledGeoset above,
    // src/chrcustomization_db2.hpp's Resolution::materials) -- the other
    // half of TODO/CHAR_TEXTURE_COMPOSITING_TODO.md Stage 3, the material
    // chain rather than the geoset chain. `fileDataId` is 0 when
    // TextureFileData.db2 (src/texturefiledata_db2.hpp) didn't resolve this
    // choice's own MaterialResourcesID -- a real, reportable gap (see
    // cmd_export.cpp's attachCustomizationChoices), not fabricated. Same
    // inert-extras treatment as EnabledGeoset: husk does not composite or
    // apply any of this on its own -- pixel compositing is deliberately
    // NOT husk's job (a prior session-local attempt at doing it in
    // software here was reverted; Blender's own shader nodes -- Mix Color
    // already has Multiply/Overlay/Screen built in -- are the right layer
    // for that, see Stage 4/5's TODO note and the planned Blender-side
    // tooling). `chrModelTextureTargetId` is the real join key against
    // CharTextureLayout::textureLayers' own `chrModelTextureTargetId`
    // above, letting that node-graph tooling find this material's real
    // placement rect/blend mode without husk resolving it first.
    struct EnabledMaterial {
        uint32_t choiceId = 0;
        uint32_t chrModelTextureTargetId = 0;
        uint32_t materialResourcesId = 0;
        uint32_t fileDataId = 0;  // 0 = TextureFileData.db2 didn't resolve this MaterialResourcesID
    };
    std::vector<EnabledMaterial> enabledMaterials;

    // Real default geoset selections resolved from a caller-supplied
    // CreatureDisplayInfoID (`husk export --creature-display-id`,
    // src/creature_geoset_db2.hpp) -- same inert-extras treatment as
    // EnabledGeoset above, but a genuinely different source and formula
    // (CreatureDisplayInfoGeosetData, `(GeosetIndex+1)*100+GeosetValue`,
    // not ChrCustomizationChoice/GeosetType*100+GeosetID), so it stays its
    // own field rather than overloading EnabledGeoset's choice_id with a
    // meaning it doesn't have. Unlike EnabledGeoset, this *is* a true
    // default -- no per-choice caller input needed beyond the display ID
    // itself.
    struct CreatureEnabledGeoset {
        uint32_t geosetIndex = 0;
        uint32_t geosetValue = 0;
        uint32_t geosetId = 0;
    };
    std::vector<CreatureEnabledGeoset> creatureEnabledGeosets;

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

    // Minimal per-body placement anchors for `.phys` physics/collision
    // bodies (`husk export --phys`) -- same "inert glTF extras, never
    // applied to anything writeGlb itself renders" treatment as
    // CorrectionSet/EmitterAnchor above. A `.phys` body is already
    // structurally an anchor (position + owning bone), like M2Ribbon/
    // M2Particle, not a flat correction-matrix table like `.bone` -- the
    // full body/shape/joint/PHYV record set has no native glTF slot either,
    // and is high-volume enough (a single real file can have 40+ bodies,
    // each with several shapes/joints) that it lives in `husk dump-chunks`'s
    // JSON output instead, same split as EmitterAnchor -- see DESIGN.md's
    // Key design decisions.
    struct PhysicsBody {
        uint32_t id = 0;  // this body's own index into the .phys file's BODY/BDY3/BDY4 array
        int joint = -1;   // index into Skeleton::joints
        // Relative to `joint`, already in the target coordinate system
        // (Y-up) -- same caller responsibility as EmitterAnchor::position.
        Vec3 position;
        uint16_t bodyType = 0;  // husk::phys::Body::type -- do NOT assume 0 is "the root"
    };
    std::vector<PhysicsBody> physicsBodies;

    // Real DB2-derived character texture-layout geometry (`husk export
    // --db2-dir/--dbd-dir/--char-layout-id`) -- inert glTF extras only,
    // never applied to anything writeGlb itself renders, same treatment as
    // CorrectionSet/PhysicsBody above. Populated from src/chrmodel_db2.hpp's
    // typed tables (ChrModelMaterial/CharComponentTextureSections/
    // ChrModelTextureLayer/CharComponentTextureLayouts), filtered to one
    // caller-supplied CharComponentTextureLayoutsID -- husk has no way to
    // derive *which* layout ID applies to a given .m2 model on its own
    // (that needs ChrModel.db2 plus a real display-ID/race/gender identity
    // this project doesn't have yet, see chrmodel_db2.hpp's module
    // comment), so this is opt-in and explicit, not automatic. A human/
    // Blender script can use the real placement rects (Section) and blend
    // info (TextureLayer) to manually position/composite
    // `alternate_textures` candidates -- husk itself does not attempt that
    // here.
    struct CharTextureLayout {
        uint32_t layoutId = 0;
        uint32_t width = 0;   // CharComponentTextureLayouts::Width -- the base atlas size
        uint32_t height = 0;  // CharComponentTextureLayouts::Height
        struct Material {
            uint32_t id = 0;
            uint32_t textureType = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t flags = 0;
        };
        std::vector<Material> materials;
        struct Section {
            uint32_t id = 0;
            uint32_t sectionType = 0;
            uint32_t x = 0;
            uint32_t y = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t overlapSectionMask = 0;
        };
        std::vector<Section> sections;
        struct TextureLayer {
            uint32_t id = 0;
            uint32_t textureType = 0;
            uint32_t layer = 0;
            uint32_t flags = 0;
            uint32_t blendMode = 0;
            uint32_t textureSectionTypeBitMask = 0;
            // Real join key against EnabledMaterial::chrModelTextureTargetId
            // above (src/chrmodel_db2.hpp's own doc comment on why this
            // isn't textureType) -- lets a downstream Blender node-graph
            // script find *this* layer's real placement rect/blend mode for
            // a specific resolved customization choice, without husk doing
            // that join itself.
            uint32_t chrModelTextureTargetId = 0;
        };
        std::vector<TextureLayer> textureLayers;
    };
    std::optional<CharTextureLayout> charTextureLayout;

    // Attachments/Events/Lights -- unlike every anchor list above, a
    // bone-relative M2Attachment/M2Event/M2Light position marker has no
    // *mesh-shaped* non-glTF-representable data (no blend modes, no
    // triangles), so writeGlbMulti gives each one a real, plain child glTF
    // node instead of another skin `extras` entry -- see writeGlbMulti's
    // doc comment for the exact node shape/naming/parenting. Attachment/
    // Light do each carry a small amount of animated-track data with no
    // core-glTF slot of their own (animateAttached/the Light curves below),
    // attached as extras on that same node rather than the skin's.
    // TODO: Remove: former M2_GAPS_TODO.md Item 6; M2_COMPLETENESS.md used
    // to call this "node-possible, unclaimed".
    // `joint` must be a valid index into `joints` -- M2's own `bone == -1`
    // ("not attached to any bone," wowdev.wiki M2#Lights) is not yet
    // representable here (there's no established "unparented placement
    // node" concept in this codebase) and is treated like any other
    // out-of-range joint: Error. No real fixture this project has seen
    // exercises `bone == -1` for an Attachment/Light; flagged here rather
    // than guessed at.
    struct Attachment {
        uint32_t id = 0;  // M2Attachment::id -- meaning depends on model type, wowdev.wiki's table
        int joint = -1;   // index into Skeleton::joints
        Vec3 position;    // relative to `joint`, already Y-up (same convention as EmitterAnchor::position)
        // M2Attachment::animate_attached ("whether the attached model is
        // animated with this one, default true") -- an M2Track<uint8_t>,
        // same raw-0/1-flag shape and "no core-glTF slot, inert node
        // extras" treatment as Light::visibility below. Extras key
        // "animate_attached", empty when there's no real keyframe data.
        std::vector<Material::AnimatedScalarCurve> animateAttached;
    };
    std::vector<Attachment> attachments;

    struct Event {
        std::string identifier;  // M2Event::identifier, e.g. "$DTH" -- not unique, see writeGlbMulti's node-naming note
        int joint = -1;
        Vec3 position;
        // M2Event::data ("passed when the event is fired," wowdev.wiki
        // M2#Events) -- documented to exist, not documented to mean
        // anything per-event; carried through raw as node extras key
        // "data", same "expose the opaque field, don't guess" policy as
        // PCOL's flags (WIKI_FINDINGS/M2.md).
        uint32_t data = 0;
    };
    std::vector<Event> events;

    struct Light {
        int joint = -1;
        Vec3 position;
        uint16_t type = 0;  // M2Light::type, 0 = directional, 1 = point
        // Every animated field (ambient/diffuse color+intensity,
        // attenuation start/end, visibility) resolved the same way the
        // animated material tint/fade curves are (Material::
        // AnimatedColorCurve/AnimatedScalarCurve, reused here rather than
        // duplicated -- same shape, same "seconds -> value, one entry per
        // M2Sequence with real inline data plus a global-sequence entry"
        // convention). No core-glTF slot for any of these exists (same
        // reasoning as tint/fade) -- attached as inert `light_animation`
        // node extras for a custom renderer/Blender script, empty when a
        // given track has no real keyframe data.
        std::vector<Material::AnimatedColorCurve> ambientColor;
        std::vector<Material::AnimatedScalarCurve> ambientIntensity;
        std::vector<Material::AnimatedColorCurve> diffuseColor;
        std::vector<Material::AnimatedScalarCurve> diffuseIntensity;
        std::vector<Material::AnimatedScalarCurve> attenuationStart;
        std::vector<Material::AnimatedScalarCurve> attenuationEnd;
        std::vector<Material::AnimatedScalarCurve> visibility;  // 0.0/1.0, not a fixed16 scale
    };
    std::vector<Light> lights;

    // One inert placeholder joint per distinct geoset ID a model's
    // primitives carry (Primitive::skinSectionId, gltf_mesh.hpp) --
    // the mechanism tools/husk_blender_geoset_mask.py uses to make WoW's
    // mutually-exclusive geoset variants (hairstyles, boot cuffs, eye-glow,
    // ...) toggleable in Blender via a Geometry Nodes dropdown, with zero
    // custom Blender import tooling required. Each becomes a real `skin.joints` entry, appended
    // strictly *after* every real bone (indices 0..joints.size()-1 above
    // are never touched or renumbered on account of this -- the one
    // invariant every other index-keyed payload in this struct already
    // depends on) with an identity bind pose that is never posed or
    // animated. Deliberately the one exception to the
    // Attachment/Event/Light convention just above (those are plain child
    // nodes, *never* added to `skin.joints`) -- a geoset tag must be a real
    // skin joint, since the entire mechanism rides on Blender's stock glTF
    // importer creating one real vertex group per skin joint as a side
    // effect of ordinary skin-weight import (verified empirically).
    //
    // emitMeshNode (gltf_mesh.cpp) does the actual tagging: any vertex
    // touched by a Primitive whose skinSectionId matches this tag's
    // geosetId gets that tag's joint woven into a second JOINTS_1/
    // WEIGHTS_1 attribute set, split evenly across however many distinct
    // tags reference it (a vertex shared at a geoset seam can carry more
    // than one). Because the tag joint is never posed, its contribution to
    // the real skin deformation is a verified no-op (Blender's Armature
    // modifier normalizes total influence weight across every joint set,
    // so an identity-pose joint changes nothing) -- this struct only ever
    // says which geoset IDs exist; it carries no per-vertex data itself.
    //
    // A model with no geosets at all (`skinSectionId == -1` on every
    // primitive, the `.skin`-less fallback case) leaves this empty --
    // no tag joints, no JOINTS_1/WEIGHTS_1 attributes, output identical to
    // before this existed.
    struct GeosetTag {
        int geosetId = 0;  // Primitive::skinSectionId this tag corresponds to
    };
    std::vector<GeosetTag> geosetTags;
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

    // M2Sequence's own per-sequence metadata (movement-speed sync, blend
    // timing, replay hint, animated bounding volume, variation/alias
    // linkage) -- core glTF has no clip-level field for any of these
    // (playback speed scaling, blend time, replay count, ...), so they're
    // exposed as inert `extras` on the animation itself, same "tag it,
    // don't guess at semantics" treatment Skeleton::CorrectionSet/
    // EmitterAnchor already get. `aliasNext`/`isAlias` mirror
    // husk::m2::Sequence's own fields raw -- when `isAlias` is true, this
    // clip's actual keyframe data (JointAnimation entries above) was
    // borrowed from the resolved terminal sequence, not from this
    // sequence's own (empty) tracks, but every other field here still
    // describes this sequence's own M2Sequence record, not the terminal's.
    // nullopt for a clip that isn't backed by a single M2Sequence record at
    // all (buildGlobalSequenceAnimations's global_seq_<n> clips) -- there's
    // no per-sequence movespeed/blend timing/bounds to expose for those.
    // TODO: Remove: `WIKI_FINDINGS/M2.md` citation for the aliasNext/isAlias
    // raw-mirroring finding.
    struct SequenceMetadata {
        float movespeed = 0;
        int16_t frequency = 0;
        uint32_t replayMin = 0;
        uint32_t replayMax = 0;
        uint16_t blendTimeIn = 0;
        uint16_t blendTimeOut = 0;
        // Already in the target coordinate system (Y-up) -- same caller
        // responsibility as Skeleton::Joint::localTranslation.
        Vec3 boundsMin;
        Vec3 boundsMax;
        float boundsRadius = 0;
        int16_t variationNext = -1;
        uint16_t aliasNext = 0;
        bool isAlias = false;
    };
    std::optional<SequenceMetadata> sequenceMetadata;
};

}  // namespace husk::gltf
