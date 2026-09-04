#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "m2_animation.hpp"
#include "m2_header.hpp"
#include "m2_primitives.hpp"
#include "m2_scene.hpp"
#include "m2_skeleton.hpp"

// husk::m2::Model: the whole-file parsed aggregate that cmd_info.cpp/
// cmd_export.cpp/cmd_dump.cpp all build their output from, via one shared
// eager parse (loadModel) rather than each command calling its own subset
// of parse* functions directly. See loadModel's own doc comment for the
// eager-parse-with-per-field-failure-isolation contract.
namespace husk::m2 {

// One array's own parse* call threw ParseError during an otherwise-
// successful loadModel -- see loadModel's doc comment for why this is
// recorded rather than propagated. `field` names the corresponding Model
// member below (snake_case, matching cmd_info.cpp's own printArray labels,
// e.g. "sequences", "particle_emitters", "collision_mesh"); `what` is the
// underlying ParseError::what() message verbatim, so the "expected N
// bytes, blob is M" detail still reaches whoever asks even though the
// field itself is left empty.
struct FieldParseFailure {
    std::string field;
    std::string what;
};

// The whole file, parsed once. Every scalar Header field (name,
// globalFlags, numSkinProfiles, boundingBox, the *FileDataIds/
// skeletonFileId/physFileId/chunkTags family, ...) stays on `header`
// rather than being duplicated here -- reading it off there is exactly as
// direct. `cameras` has no field here either -- see loadModel's doc
// comment. `extendedParticles` (EXP2) is Legion+ chunk-sourced data, not a
// core MD20 header array, so it's out of this type's scope by the same
// boundary that already excludes the other chunk tags.
struct Model {
    Header header;
    std::vector<uint8_t> blob;  // the resolved MD20 blob every array offset above is relative to

    std::vector<uint32_t> globalLoops;  // M2Loop, header.globalLoops

    // M2Sequence, header.sequences -- parsed unconditionally here, unlike
    // callers that only read it under their own guard (e.g. cmd_info.cpp's
    // "only if sequenceLookup.count > 0").
    std::vector<Sequence> sequences;
    std::vector<uint16_t> sequenceLookup;  // header.sequenceLookup, M2#Animation_Lookup hash table

    std::vector<Bone> bones;               // M2CompBone, header.bones
    std::vector<uint16_t> boneLookup;      // header.boneLookup, M2#Key-Bone_Lookup

    std::vector<uint16_t> boneCombos;  // M2Array<uint16_t>, header.boneCombos

    std::vector<Vertex> vertices;  // M2Vertex, header.vertices

    std::vector<Color> colors;                  // M2Color, header.colors
    std::vector<Texture> textures;              // M2Texture, header.textures
    std::vector<TextureWeight> textureWeights;  // M2TextureWeight, header.textureWeights
    std::vector<TextureTransform> textureTransforms;  // M2TextureTransform, header.textureTransforms
    std::vector<uint16_t> textureLookup;  // header.textureLookup, M2#Replacable_texture_lookup

    std::vector<Material> materials;  // M2Material, header.materials

    std::vector<uint16_t> textureCombos;              // header.textureCombos
    std::vector<uint16_t> textureCoordCombos;         // header.textureCoordCombos
    std::vector<uint16_t> textureWeightCombos;        // header.textureWeightCombos
    std::vector<uint16_t> textureTransformCombos;     // header.textureTransformCombos

    // header.collisionPositions/collisionIndices/collisionFaceNormals,
    // dereferenced together by parseCollisionMesh. One FieldParseFailure
    // entry ("collision_mesh") covers all three sub-arrays, matching
    // parseCollisionMesh's own atomicity -- it already reads them as one
    // unit.
    CollisionMesh collisionMesh;

    std::vector<Attachment> attachments;  // M2Attachment, header.attachments
    std::vector<uint16_t> attachmentLookup;  // header.attachmentLookup

    std::vector<Event> events;  // M2Event, header.events
    std::vector<Light> lights;  // M2Light, header.lights

    // header.cameras has no field here: no M2Camera struct or parseCameras
    // function exists in this codebase yet, so there is nothing to
    // eagerly parse. header.cameras (the Array descriptor itself, via
    // `header`) is unaffected.
    std::vector<uint16_t> cameraLookup;  // header.cameraLookup

    std::vector<Ribbon> ribbonEmitters;             // M2Ribbon, header.ribbonEmitters
    std::vector<ParticleEmitter> particleEmitters;  // M2Particle, header.particleEmitters --
                                                     // version-gated, see loadModel's doc comment

    // M2Array<uint16_t>, header.textureCombinerCombos -- count is 0 unless
    // GlobalFlag::kUseTextureCombinerCombos was set (Header::
    // textureCombinerCombos's own doc comment), which parseUint16Array
    // already no-ops on cheaply, so no extra gating is needed here.
    std::vector<uint16_t> textureCombinerCombos;

    // Populated once per field above whose own parse* call threw
    // ParseError -- see loadModel's doc comment. The corresponding field is
    // left default-constructed (empty) on failure, the same value a
    // genuinely-empty-but-valid array also produces; this is the only way
    // to tell the two apart. Empty when every field parsed cleanly (the
    // overwhelming common case).
    std::vector<FieldParseFailure> parseFailures;
};

// Parses the entire file eagerly: every array m2_header.hpp/
// m2_skeleton.hpp/m2_animation.hpp/m2_scene.hpp know how to dereference
// (Model's own field list above), not just whatever subset a given caller
// happens to read.
//
// parseHeader/extractBlob failing (bad magic, too short, a chunked file
// with no MD21 chunk) still throws ParseError and propagates -- there is
// no file at all to build a Model from in that case.
//
// Once the header/blob resolve, each array's own parse* call is wrapped
// independently: a ParseError from one array leaves that field empty and
// appends a FieldParseFailure to Model::parseFailures, rather than
// discarding every other field that did parse cleanly. A single malformed
// section must not blank out an otherwise-readable file.
//
// particleEmitters alone is version-gated *before* being attempted, not
// just wrapped in try/catch: m2::kMinVerifiedParticleVersion exists
// because an incompatible older-version file isn't guaranteed to throw on
// the wrong record stride -- it can silently decode adjacent bytes as
// plausible-looking-but-wrong values instead, a failure class a try/catch
// can't see. Below that version particleEmitters is left empty and this is
// NOT recorded as a FieldParseFailure, since nothing was attempted; a
// caller can re-derive the same check from `model.header.version` and the
// same constant. bones/sequences/ribbons are NOT similarly gated: they're
// parsed unconditionally regardless of `kMinVerifiedRecordStrideVersion`,
// with only a stderr warning that the record stride is unverified below
// Wrath.
Model loadModel(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::m2
