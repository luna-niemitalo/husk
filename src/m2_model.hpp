#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "m2_animation.hpp"
#include "m2_header.hpp"
#include "m2_primitives.hpp"
#include "m2_scene.hpp"
#include "m2_skeleton.hpp"

// husk::m2::Model: the whole-file parsed aggregate, per REFACTOR/AUDIT.md
// §2.1 ("There is no m2::Model") and REFACTOR/CANONICAL_MODEL.md's "Stage
// 1's missing piece". Today cmd_info.cpp/cmd_export.cpp/cmd_dump.cpp each
// hand-assemble a *different* partial view of the same file from loose
// parse* calls (12/11/0 kinds respectively -- AUDIT.md's own table); this
// is the coherent decoder output all three should eventually consume a
// view of instead, so "what does husk think is in this file" stops having
// three different answers depending on which verb you typed.
//
// This is a pure addition (REFACTOR migration step 2 of "m2::Model
// aggregate") -- no existing command has been migrated to use it yet. See
// loadModel's own doc comment for the eager-parse-with-per-field-failure-
// isolation contract this type exists to make possible without changing
// any of the three commands' own observable behavior once they do migrate.
namespace husk::m2 {

// One array's own parse* call threw ParseError during an otherwise-
// successful loadModel -- see loadModel's doc comment for why this is
// recorded rather than propagated. `field` names the corresponding Model
// member below (snake_case, matching cmd_info.cpp's own printArray labels
// where one already exists, e.g. "sequences", "particle_emitters",
// "collision_mesh"); `what` is the underlying ParseError::what() message
// verbatim, preserving the real "expected N bytes, blob is M" detail the
// exception itself carried (CLAUDE.md: "on failure, always print expected
// and actual values" -- this is where those values still reach whoever
// asks, since the field itself is left empty).
struct FieldParseFailure {
    std::string field;
    std::string what;
};

// The whole file, parsed once. Holds the union of every array the three
// existing commands (cmd_info.cpp, cmd_info_json.cpp, cmd_export.cpp) parse
// via a real parse* call today, plus `globalLoops`/`boneCombos`/
// `collisionMesh` (parseGlobalLoops/parseUint16Array/parseCollisionMesh all
// exist and are real, just currently unused by any command -- see
// loadModel's doc comment). Two things are deliberately NOT duplicated
// here, since they already live on `header` and reading them off there is
// exactly as direct: every scalar Header field (name, globalFlags,
// numSkinProfiles, boundingBox, the *FileDataIds/skeletonFileId/
// physFileId/chunkTags family, ...), and `cameras`/`extendedParticles` --
// see loadModel's doc comment for why those two are excluded outright
// rather than merely omitted from this comment.
struct Model {
    Header header;
    std::vector<uint8_t> blob;  // the resolved MD20 blob every array offset above is relative to

    // M2Loop, header.globalLoops -- parseGlobalLoops exists but no command
    // reads it today (not even a count-only print); included per this
    // task's "include what has a real parser" instruction.
    std::vector<uint32_t> globalLoops;

    // M2Sequence, header.sequences -- cmd_info.cpp only calls parseSequences
    // when header.sequenceLookup.count > 0 (a real, already-divergent
    // quirk from cmd_export.cpp's own unconditional call whenever bones are
    // inline; AUDIT.md §2.1's "different answers" is this, concretely).
    // Parsed unconditionally here.
    std::vector<Sequence> sequences;
    std::vector<uint16_t> sequenceLookup;  // header.sequenceLookup, M2#Animation_Lookup hash table

    std::vector<Bone> bones;               // M2CompBone, header.bones
    std::vector<uint16_t> boneLookup;      // header.boneLookup, M2#Key-Bone_Lookup

    // M2Array<uint16_t>, header.boneCombos -- parseUint16Array (the same
    // generic reader every other uint16 combo array below uses) applies
    // directly; no command reads this array at all today.
    std::vector<uint16_t> boneCombos;

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
    // dereferenced together by parseCollisionMesh (export_extras.cpp's
    // appendCollisionMesh already calls it from cmd_export.cpp, just not
    // through this type). One FieldParseFailure entry ("collision_mesh")
    // covers all three sub-arrays, matching parseCollisionMesh's own
    // atomicity -- it already reads them as one unit.
    CollisionMesh collisionMesh;

    std::vector<Attachment> attachments;  // M2Attachment, header.attachments
    std::vector<uint16_t> attachmentLookup;  // header.attachmentLookup

    std::vector<Event> events;  // M2Event, header.events
    std::vector<Light> lights;  // M2Light, header.lights

    // header.cameras has no field here: no M2Camera struct or parseCameras
    // function exists anywhere in this codebase yet (husk's own
    // CLAUDE.md Status: "M2Camera is still count-only (not dereferenced)")
    // -- there is nothing to eagerly parse. header.cameras (the Array
    // descriptor itself, still exposed via `header`) is all `husk info`
    // shows today, unchanged.
    std::vector<uint16_t> cameraLookup;  // header.cameraLookup

    std::vector<Ribbon> ribbonEmitters;             // M2Ribbon, header.ribbonEmitters
    std::vector<ParticleEmitter> particleEmitters;  // M2Particle, header.particleEmitters --
                                                     // version-gated, see loadModel's doc comment

    // M2Array<uint16_t>, header.textureCombinerCombos -- only wire-populated
    // by parseHeader when GlobalFlag::kUseTextureCombinerCombos is set (see
    // Header::textureCombinerCombos's own doc comment); count is 0
    // otherwise, which parseUint16Array already no-ops on cheaply, so no
    // extra gating is needed here beyond what the header parse already did.
    std::vector<uint16_t> textureCombinerCombos;

    // Populated once per field above whose own parse* call threw
    // ParseError -- see loadModel's doc comment. The corresponding field is
    // left default-constructed (empty) on failure, the same value a
    // genuinely-empty-but-valid array also produces; this is the only way
    // to tell the two apart. Empty when every field parsed cleanly (the
    // overwhelming common case).
    std::vector<FieldParseFailure> parseFailures;
};

// Parses the *entire* file eagerly: every array m2_header.hpp/
// m2_skeleton.hpp/m2_animation.hpp/m2_scene.hpp know how to dereference
// (Model's own field list above), not just the subset any one existing
// command happens to read today (REFACTOR/AUDIT.md §2.1).
//
// Contract, decided as part of this task rather than assumed:
//
// parseHeader/extractBlob failing (bad magic, too short, a chunked file
// with no MD21 chunk) still throws ParseError and propagates -- there is
// no file at all to build a Model from in that case, matching every
// existing caller's own behavior (cmd_info.cpp/cmd_export.cpp/
// cmd_dump.cpp all fail identically here today).
//
// Once the header/blob resolve, each *array's* own parse* call is wrapped
// independently: a ParseError from one array (a genuinely malformed
// section deeper in an otherwise-readable file) leaves that one field
// empty and appends a FieldParseFailure to Model::parseFailures, rather
// than discarding every other field that *did* parse cleanly. This is the
// conservative option, chosen over "propagate the first throw and give up
// on the whole file", for a concrete reason: today's three commands parse
// conditionally and *differently* (cmd_info.cpp's parseSequences call only
// runs `if (h.sequenceLookup.count > 0)`; cmd_export.cpp calls it
// unconditionally whenever bones are inline -- two different real guards
// on the same array, see AUDIT.md §2.1) and so never attempt -- and never
// discover a malformed instance of -- an array their own guard happens to
// skip. An eager whole-file parse *newly* attempts every array on every
// file, and across a real 130k+-file corpus (CLAUDE.md's Hazards/
// corpus-scan history) "some one field somewhere is malformed" is a real,
// observed case, not a hypothetical one -- a single throw must not blank
// out a file that is otherwise perfectly readable, and future migrations
// of cmd_info/cmd_info_json/cmd_export need this type to hold everything
// that *did* parse so they can reproduce their current per-array
// conditional behavior on top of it without husk's own eager load having
// already destroyed the rest of the data over one bad section.
//
// One array is version-gated *before* being attempted at all, not just
// wrapped in try/catch: particleEmitters. m2::kMinVerifiedParticleVersion
// exists because an incompatible older-version file isn't guaranteed to
// throw on the wrong (492-byte Cata+) record stride -- it can silently
// decode adjacent bytes as plausible-looking-but-wrong field values
// instead (the "silent misread" failure class this project's own
// discipline treats as strictly worse than a loud bounds failure, since a
// try/catch can't see it at all). Below that version, particleEmitters is
// left empty -- matching cmd_info.cpp/cmd_info_json.cpp's own existing
// "count-only" policy for the exact same reason -- and this is NOT
// recorded as a FieldParseFailure, since nothing was attempted; it is the
// same version check a caller can already re-derive from
// `model.header.version` and this same constant. bones/sequences/ribbons
// are NOT similarly version-gated here: today's commands already parse
// them unconditionally regardless of `kMinVerifiedRecordStrideVersion`,
// only warning to stderr that the record stride is unverified below
// Wrath -- that pre-existing silent-misread risk is unchanged by this
// task (not newly introduced, not newly fixed; printing the warning
// itself is presentation-layer, out of scope for a struct).
//
// Cost: measured, not assumed -- see REFACTOR_LOG.md's entry for this
// task for the real numbers this eager parse costs against
// bloodelffemale.m2 and a real corpus file, compared to the current
// per-command partial-parse cost.
Model loadModel(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::m2
