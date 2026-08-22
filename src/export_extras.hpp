#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "appearance_string.hpp"
#include "gltf.hpp"
#include "m2.hpp"

// The attachX() helper group -- each attaches one kind of inert glTF
// `extras` metadata to the exported skin, called once each from
// exportOneModel (cmd_export.cpp) -- split out of cmd_export.cpp per
// FILE_SPLIT_TODO.md's Item 1. External linkage (unlike their previous home
// in an anonymous namespace nested in husk::commands) since they're now
// called cross-TU from cmd_export.cpp.
namespace husk::commands {

// Small shared utility -- reads a whole file into memory, throwing a
// descriptive runtime_error (with strerror()) on any open/read failure.
// Declared here (rather than staying file-local) since both this file's own
// attachX functions and cmd_export.cpp's own remaining pipeline code
// (resolveBones, buildLodTierMeshes, exportOneModel) need it.
std::vector<uint8_t> readFileBytes(const std::string& path);

// --bones-dir: resolves each of the model's/.skel's BFID-declared
// FileDataIDs to a real '<bonesDir>/<id>.bone' file, if present (silently
// skipped otherwise, same "optional, resolve what's there" policy
// --textures already uses for a missing PNG) -- attached as inert
// gltf::Skeleton::CorrectionSet extras, never applied to the bind pose/
// animation (see gltf.hpp's CorrectionSet doc comment).
void attachBoneCorrections(const std::string& bonesDir, bool bonesAreInline, bool haveSkel,
                            const m2::Header& header, const std::vector<uint8_t>& skelBytes,
                            gltf::Skeleton& skeleton);

// Ribbon/particle placement anchors (gltf::Skeleton::EmitterAnchor's doc
// comment): unconditional, no CLI flag -- this data comes straight from
// the model's own already-parsed header arrays. Full field/curve data
// lives in `husk dump-chunks`, not here -- this is placement only.
void attachEmitterAnchors(const std::vector<uint8_t>& blob, const m2::Header& header,
                           gltf::Skeleton& skeleton);

// Attachment/Event/Light placement nodes (gltf::Skeleton::
// Attachment/Event/Light's doc comments): unconditional, no CLI flag, same
// "always attached" treatment as ribbon/particle anchors -- but unlike
// those, these become real child glTF nodes, not skin extras, since a
// bone-relative position marker is all M2Attachment/M2Event/M2Light static
// data ever is. `bone == -1` ("not attached to any bone," real for M2Light
// and possibly M2Attachment) is treated as out of range and throws -- husk
// has no established "unparented placement node" concept yet. `sequenceCount`
// drives Light's animated-track resolution (ambient/diffuse color+intensity,
// attenuation, visibility) the same way M2MaterialInputs::sequenceCount
// drives the material tint/fade curves.
void attachPlacementNodes(const std::vector<uint8_t>& blob, const m2::Header& header,
                           size_t sequenceCount, gltf::Skeleton& skeleton);

// --phys: three-state resolution mirroring --skel (DESIGN.md's Key design
// decisions -- PFID is a single scalar FileDataID, like SKID, not an array
// like BFID/AFID/SFID, so a directory flag doesn't apply here). 'none'
// means never look, even if a same-basename .phys exists; an explicit path
// overrides; unset auto-detects a same-basename '.phys' next to the model.
// Not finding one isn't an error -- most models have no physics data at
// all. Only the minimal per-body placement anchor (gltf::Skeleton::
// PhysicsBody's doc comment) is attached here; the full body/shape/joint/
// PHYV record set is available via `husk dump-chunks` instead.
void attachPhysicsBodies(bool physNone, bool physGiven, const std::string& physPath,
                          const std::string& modelPath, gltf::Skeleton& skeleton);

// --db2-dir/--dbd-dir/--customization-choice-ids: resolves each real
// ChrCustomizationChoiceID against src/chrcustomization_db2.hpp's DB2
// chain (TODO/TODO_correctness.md #2), attaching real geoset selections
// as skeleton.enabledGeosets extras and marking any already-resolved
// --bones-dir CorrectionSet a choice's ChrCustomizationBoneSetID points at
// (TODO_correctness.md #2). Must run after attachBoneCorrections, since it
// only marks existing entries in skeleton.correctionSets rather than
// attaching new '.bone' data itself -- a choice resolving to a
// BoneFileDataID that was never in --bones-dir's own BFID-array scan (a
// real, checkable inconsistency, not assumed impossible) is reported and
// otherwise ignored, not fabricated.
//
// Also attaches the FULL real customization menu (skeleton.
// customizationOptions, TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md's own
// prerequisite) whenever a real ChrModelID can be determined at all.
// --chr-model-id follows the same auto|none|<id> three-state convention
// --textures/--skin-dir/--skel already use, but with 'auto' as the
// *default* rather than something that needs asking for: given only
// --db2-dir/--dbd-dir (no --chr-model-id, no --customization-choice-ids
// at all), this function still attempts the same real derivation
// --chr-model-id auto always did. --chr-model-id none explicitly opts
// out; an explicit --chr-model-id <id> overrides derivation; an explicit
// --customization-choice-ids (with no --chr-model-id) still attempts
// best-effort derivation purely to attach the full menu, unless
// --chr-model-id none says not to. Deliberately not gated behind a
// separate opt-in flag -- Luna's own direct instruction: a downstream
// Blender script needs to see every real choice per option, not just
// whatever this one export run happened to resolve, and this shouldn't
// need "the user uttering the magic words" to get.
void attachCustomizationChoices(const std::string& db2Dir, const std::string& dbdDir,
                                 const std::string& choiceIdsArg, const std::string& chrModelIdArg,
                                 const std::string& modelPath,
                                 const std::unordered_map<uint32_t, std::string>& listfile,
                                 const std::string& listfileRoot, gltf::Skeleton& skeleton);

// --db2-dir/--dbd-dir/--char-layout-id: attaches real character-texture
// placement geometry (gltf::Skeleton::CharTextureLayout's doc comment) as
// inert glTF extras. --db2-dir/--dbd-dir are required; --char-layout-id
// itself no longer is -- when omitted, this auto-derives it from a
// resolved ChrModelID's own real ChrModel.CharComponentTextureLayoutID
// column (the exact same table/row --chr-model-id auto already reads to
// get the ChrModelID itself -- found and closed same-day, real
// interactive use: Luna asked "are --db2-dir/--dbd-dir strictly
// required," which led to checking whether --char-layout-id still needed
// to be separately spelled out too, and it turned out husk was already
// one column-read away from not needing it). Same auto|none|<id>
// three-state convention as --chr-model-id itself for the ChrModelID this
// derivation needs: chrModelIdArg empty/"auto" derives, "none" opts the
// whole fallback out, an explicit numeric ID is used directly, no
// re-derivation. An explicit --char-layout-id always wins outright, same
// as before this fallback existed.
void attachCharTextureLayout(const std::string& db2Dir, const std::string& dbdDir,
                              const std::string& charLayoutIdArg, const std::string& chrModelIdArg,
                              const std::string& modelPath,
                              const std::unordered_map<uint32_t, std::string>& listfile,
                              const std::string& listfileRoot, gltf::Skeleton& skeleton);

// --db2-dir/--dbd-dir/--creature-display-id: resolves a real
// CreatureDisplayInfoID against src/creature_geoset_db2.hpp's
// CreatureDisplayInfoGeosetData.db2, attaching the real default geoset
// selection as skeleton.creatureEnabledGeosets extras. Unlike
// attachCustomizationChoices, this is a true default -- no per-choice
// caller input beyond the display ID itself.
void attachCreatureGeosets(const std::string& db2Dir, const std::string& dbdDir,
                            const std::string& creatureDisplayIdArg, gltf::Skeleton& skeleton);

// `husk export --appearance`'s `gear` entries: resolves each real
// (SLOT, ItemModifiedAppearanceID) pair against src/itemappearance_db2.hpp's
// DB2 chain, attaching case 2 (object-skin section overlay, most body
// armor) as skeleton.gearSectionOverlays and case 1 (standalone-geometry
// items -- weapons, shields, some helms) as skeleton.gearItems -- see
// TODO/EQUIPPED_GEAR_RENDER_TODO.md. Same "husk resolves, never applies"
// policy as attachCustomizationChoices/attachCharTextureLayout above;
// --db2-dir/--dbd-dir are required (same as every other DB2-driven
// enrichment here) -- a non-empty `gear` with either missing is reported
// and skipped, not fatal to the rest of the export. `modelfiledata`/
// `texturefiledata` DB2 tables are each independently optional (a missing
// one just leaves that hop's FileDataID at 0, same "reportable gap, not
// fabricated" treatment `itemappearance::Resolution` itself already uses).
void attachGearAppearance(const std::string& db2Dir, const std::string& dbdDir,
                           const std::vector<appearance::GearEntry>& gear, gltf::Skeleton& skeleton);

// The collision mesh (physics/hit-testing, m2::CollisionMesh) is a plain
// triangle mesh with an unambiguous glTF translation -- unlike geoset
// selection/multi-texture-layers (data with no unambiguous glTF shape,
// hence inert extras only), so when requested it's exported as real
// geometry: one more NamedMesh, unskinned (a collision mesh is static, not
// deformed by the armature), tagged `isCollision` so writeGlbMulti marks
// its node `{"collision": true}` in extras. Off by default -- Blender's
// stock importer has no concept of that extras tag and renders the node
// like any other mesh, and the collision hull is usually a coarse box/
// capsule that's larger than and visually occludes the real character
// (found the hard way: it fully hid a real character render). Appends
// nothing (leaves `namedMeshes` untouched) unless `--collision` was given
// and the model actually has collision data.
void appendCollisionMesh(const m2::Header& header, const std::vector<uint8_t>& blob,
                          const std::string& modelPath, bool collisionRequested,
                          std::vector<gltf::NamedMesh>& namedMeshes);

// Prints exportGlb's final one-line-or-table summary: no renderable
// geometry at all, exactly one render mesh, or several LOD tiers -- each
// shape needs different formatting, so this is one function rather than
// three call sites duplicating the bones/animations suffix logic.
void printExportSummary(const std::string& outputPath, const std::vector<m2::Vertex>& vertices,
                         const std::vector<m2::Bone>& bones,
                         const std::vector<gltf::Animation>& animations,
                         const std::vector<gltf::NamedMesh>& namedMeshes, size_t renderMeshCount);

}  // namespace husk::commands
