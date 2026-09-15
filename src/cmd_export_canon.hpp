#pragma once

#include <string>
#include <vector>

#include "gltf_mesh.hpp"
#include "gltf_skeleton.hpp"
#include "m2.hpp"

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
void runCanonCompareExport(const m2::Model& model, const std::string& skinPath, const gltf::Mesh& legacyMesh,
                           const gltf::Skeleton& legacySkeleton,
                           const std::vector<gltf::Animation>& legacyAnimations, const std::string& outputPath);

}  // namespace husk::commands
