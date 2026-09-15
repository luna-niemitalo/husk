#include "cmd_export_canon.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "canon_diff.hpp"
#include "canon_model.hpp"
#include "export_extras.hpp"  // readFileBytes
#include "skin.hpp"
#include "writers/bundle_writer.hpp"
#include "writers/gltf_lean.hpp"

// This is `husk export`'s new, second pipeline path (REFACTOR/README.md's
// Migration order, stage 3), run entirely alongside the real, still-shipping
// legacy gltf_*.cpp pipeline rather than in place of it -- the legacy path
// is untouched by this file, and this file's own failure can never affect
// it (see runCanonCompareExport's own doc comment). Its purpose is the
// runtime counterpart to tests/test_canon_*_convergence.cpp's fixture-only
// convergence proofs: those check canon:: against one committed real file;
// `--compare-canon` lets the exact same kind of structural check run
// against ANY real corpus file, in the same invocation, with the same
// flags, that already produces the legacy .glb -- so the two outputs are
// always compared on identical inputs, never a separately-reconstructed
// invocation that could silently drift (different --skin resolution,
// different --textures directory, ...).
//
// Deliberately re-parses the skin file itself (see runCanonCompareExport's
// own doc comment) rather than taking already-parsed batches/submeshes/
// triangleIndices from the caller: a genuinely independent re-derivation
// is a stronger convergence signal than reusing the legacy pipeline's own
// intermediate state would be, and the extra parse cost is negligible next
// to the .glb writes this same call already performs.
namespace husk::commands {

namespace {

void printReport(const std::string& label, const canon_diff::Report& r) {
    for (const auto& d : r.deviations) {
        std::cerr << "husk: canon-compare: DEVIATION (" << label << "): " << d << "\n";
    }
    for (const auto& n : r.notes) {
        std::cerr << "husk: canon-compare: note (" << label << "): " << n << "\n";
    }
}

}  // namespace

void runCanonCompareExport(const m2::Model& model, const std::string& skinPath, const gltf::Mesh& legacyMesh,
                           const gltf::Skeleton& legacySkeleton,
                           const std::vector<gltf::Animation>& legacyAnimations, const std::string& outputPath) {
    try {
        auto skinBytes = readFileBytes(skinPath);
        skin::Header skinHeader = skin::parseHeader(skinBytes);
        std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);
        std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinBytes, skinHeader.submeshes);
        std::vector<skin::Batch> batches = skin::parseBatches(skinBytes, skinHeader.batches);

        canon::Model canonModel = canon::assembleModel(model, batches, submeshes, triangleIndices);

        std::filesystem::path leanGlbPath(outputPath);
        leanGlbPath.replace_extension(".canon.glb");
        std::filesystem::path bundleDir(outputPath);
        bundleDir.replace_extension("");
        bundleDir += ".canon.bundle";

        writers::writeLeanGlb(canonModel, leanGlbPath);
        writers::writeBundle(canonModel, bundleDir);
        std::cerr << "husk: canon-compare: wrote '" << leanGlbPath.string() << "' and '" << bundleDir.string()
                  << "/' alongside '" << outputPath << "'\n";

        bool anyDeviation = false;

        canon_diff::Report meshReport = canon_diff::compareMesh(canonModel.mesh, legacyMesh);
        anyDeviation = anyDeviation || !meshReport.ok();
        printReport("mesh", meshReport);

        canon_diff::Report skeletonReport = canon_diff::compareSkeleton(canonModel.skeleton, legacySkeleton);
        anyDeviation = anyDeviation || !skeletonReport.ok();
        printReport("skeleton", skeletonReport);

        canon_diff::Report animReport = canon_diff::compareAnimations(canonModel, model.sequences, legacyAnimations);
        anyDeviation = anyDeviation || !animReport.ok();
        printReport("animations", animReport);

        canon_diff::Report materialReport =
            canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, model.materials);
        anyDeviation = anyDeviation || !materialReport.ok();
        printReport("materials", materialReport);

        std::cerr << "husk: canon-compare: "
                  << (anyDeviation ? "DEVIATIONS FOUND -- see above" : "clean, no deviations found") << "\n";
    } catch (const std::exception& e) {
        std::cerr << "husk: canon-compare: failed: " << e.what() << "\n";
    }
}

}  // namespace husk::commands
