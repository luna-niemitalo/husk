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

// Runs canon::assembleModel against the SAME already-parsed `model` and
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
// `canon::TextureResolutions` (REFACTOR/AUDIT.md §7.1's "deliberate next
// step, not yet started" -- now started): one `catalog.texture()` call per
// distinct M2 texture-array index actually referenced by a batch in the
// re-parsed skin, the exact same per-slot inputs
// buildMaterialsAndPrimitives's own catalog call already uses
// (export_materials.cpp), so canon:: and legacy resolve every real texture
// slot through the identical catalog answer -- never a second opinion.
//
// `animDir`/`bonesAreInline` feed a real `canon::ExternalAnimBlobs` (§7.2's
// remaining open sub-gap): resolveExternalAnimBlob (export_animation.hpp)
// is called once per non-inline, non-alias sequence in `model.sequences`,
// mirroring buildAnimations's own external-.anim resolution exactly. Left
// empty when `bonesAreInline` is false -- canon::assembleModel only ever
// reads `model.bones`/`model.sequences`/`model.blob` (the inline M2 source),
// never a .skel-sourced one, so external-anim resolution has nothing valid
// to key against in that case (a separate, pre-existing canon::Model
// limitation, not this parameter's own scope to fix).
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
                           const std::vector<gltf::Material>& legacyMaterials = {});

}  // namespace husk::commands
