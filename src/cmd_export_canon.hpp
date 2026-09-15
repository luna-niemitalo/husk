#pragma once

#include <string>
#include <vector>

#include "gltf_mesh.hpp"
#include "gltf_skeleton.hpp"
#include "m2.hpp"
#include "sources/catalog.hpp"

// husk::commands: `husk export --compare-canon`'s own path -- REFACTOR/
// README.md stage 3's runtime convergence gate. See cmd_export_canon.cpp's
// doc comment for the full rationale; this header only declares the one
// entry point cmd_export.cpp calls.
namespace husk::commands {

// Runs m2input::buildCanonModel against the SAME already-parsed `model` and
// the FIRST resolved skin tier (re-parsed here from `skinPath`,
// independently of the legacy pipeline's own parse of it -- deliberately:
// this is meant to be a genuinely separate re-derivation, not a reuse of
// legacy's internal state), writes its own '<outputPath minus extension>
// .canon.glb' (writers::writeLeanGlb) and '....canon.bundle/' (writers::
// writeBundle) alongside the legacy .glb, then structurally compares the
// result against `legacyMesh`/`legacySkeleton`/`legacyAnimations` (the
// legacy pipeline's own already-built artifacts for that SAME tier) via
// canon_diff.hpp, printing a report to stderr.
//
// Never throws and never affects the legacy export's own exit code: this
// is a diagnostic sidecar (`--compare-canon`, opt-in, off by default), same
// "opt-in enrichment failure never aborts the primary output" policy every
// other DB2/knowledge-base enrichment in cmd_export.cpp already follows --
// any failure (a canon:: assembly throw, a write failure) is caught and
// reported, not propagated.
//
// `catalog`/`modelPath`/`objectSkinTextureFileDataId` feed a real
// `m2input::TextureResolutions` (REFACTOR/AUDIT.md §7.1's "deliberate next
// step, not yet started" -- now started): one `catalog.texture()` call per
// distinct M2 texture-array index actually referenced by a batch in the
// re-parsed skin, the exact same per-slot inputs
// buildMaterialsAndPrimitives's own catalog call already uses
// (export_materials.cpp), so canon:: and legacy resolve every real texture
// slot through the identical catalog answer -- never a second opinion.
//
// `animDir`/`bonesAreInline` feed a real `m2input::ExternalAnimBlobs` (§7.2's
// external-.anim resolution): a file-local resolver in cmd_export_canon.cpp
// is called once per non-inline, non-alias sequence in whichever sequence
// array is actually in effect (model.sequences, or `skelBytes`' own
// SKS1-parsed sequences when `haveSkel` and `!bonesAreInline`), mirroring
// buildAnimations's own external-.anim resolution exactly.
//
// `haveSkel`/`skelBytes`: when `!bonesAreInline && haveSkel`, this function
// independently re-parses `skelBytes` (skel::parseBones/parseSequences/
// boneTrackBlob/findAnimFileIds -- the same real `.skel` bytes the legacy
// pipeline already resolved and parsed once via `commands::resolveBones`,
// re-derived here rather than reused, same "genuinely separate
// re-derivation" policy this file's own top doc comment states for the
// skin tier) and builds a real `m2input::ExternalSkeletonSource`, closing
// AUDIT.md §7.2's "no `.skel`-sourced coverage at all" gap -- previously
// `runCanonCompareExport` always read `model.bones`/`model.sequences`
// regardless of `bonesAreInline`, silently comparing against the model's
// own (usually empty) inline data for every `.skel`-sourced export. When
// `bonesAreInline` is true, or `haveSkel` is false (a genuinely 0-bone
// model, or `--skel none`), this parameter has no effect -- exactly
// today's inline-only behavior.
//
// `legacyMaterials`: the SAME LOD tier's own resolved `gltf::Material` list
// (`NamedMesh::materials`, matching `legacyMesh`) -- forwarded to
// canon_diff::compareMaterialBlendModes so it can check canon's newly-wired
// texture-resolution answers against legacy's own
// `baseColorTextureFileDataId` (AUDIT.md §7.1). Default empty for the same
// "predates this parameter, no behavior change" reason every other optional
// parameter in this codebase defaults empty.
void runCanonCompareExport(const m2::Model& model, const std::string& skinPath, const gltf::Mesh& legacyMesh,
                           const gltf::Skeleton& legacySkeleton,
                           const std::vector<gltf::Animation>& legacyAnimations, const std::string& outputPath,
                           husk::sources::Catalog& catalog, const std::string& modelPath,
                           uint32_t objectSkinTextureFileDataId, const std::string& animDir, bool bonesAreInline,
                           bool haveSkel, const std::vector<uint8_t>& skelBytes,
                           const std::vector<gltf::Material>& legacyMaterials = {});

}  // namespace husk::commands
