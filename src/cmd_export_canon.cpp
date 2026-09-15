#include "cmd_export_canon.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "canon_diff.hpp"
#include "canon_model.hpp"
#include "chunk.hpp"           // readChunks, findChunk (AFSB/AFM2 peek)
#include "export_extras.hpp"   // readFileBytes
#include "m2_animation.hpp"    // m2::extractAnimBlob
#include "m2_canon_input.hpp"  // m2input::buildCanonModel, m2input::ExternalAnimBlobs
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

// M2Sequence flags bits (wowdev.wiki M2#Animation_sequences's Flags
// table) -- reimplemented here rather than shared, same private-per-TU
// constant tradeoff canon_model.cpp/export_animation.cpp both already
// accept for this one bit test (see canon_model.cpp's own comment on it).
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;
constexpr uint32_t kSequenceAliasFlag = 0x40;

// One already-resolved sources::Catalog answer, converted to the
// canon::TextureRef shape REFACTOR/AUDIT.md §7.1 settled on -- the
// orchestrator-side half of that decision (canon:: itself performs no
// resolution or conversion of its own, see m2_material_input.hpp).
// `fdid` is threaded through separately since a Resolved<EncodedTexture>
// hit doesn't carry the FileDataID it was resolved from back out.
canon::TextureRef toCanonTextureRef(const sources::Resolved<sources::EncodedTexture>& resolved, uint32_t fdid) {
    canon::TextureRef ref;
    if (!resolved.alternates.empty()) {
        // Non-empty alternates means genuinely ambiguous (Resolved<T>'s own
        // doc comment), regardless of whether `resolved` itself carries a
        // chosen default -- canon::TextureRef::Ambiguous keeps the full
        // candidate set rather than the one guess, same reasoning
        // AlternateTextureCandidate already applied for gltf::Material.
        ref.state = canon::TextureRef::State::Ambiguous;
        ref.candidates.reserve(resolved.alternates.size());
        for (const auto& alt : resolved.alternates) {
            canon::TextureRef::Candidate c;
            c.identity.name = alt.filename;
            c.identity.source = canon::NameSource::Synthesized;  // a same-basename guess, not a verified identity
            c.category = alt.category;
            c.width = alt.width;
            c.height = alt.height;
            ref.candidates.push_back(std::move(c));
        }
        return ref;
    }
    if (!resolved.found()) {
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.unresolvedReason = resolved.reason;
        return ref;
    }
    ref.state = canon::TextureRef::State::Resolved;
    if (fdid != 0) ref.resolved.id = canon::FileDataId{fdid};
    ref.resolved.name = resolved.value->imageName;
    switch (resolved.tier) {
        case sources::ResolutionTier::Listfile:
            ref.resolved.source = canon::NameSource::Listfile;
            break;
        case sources::ResolutionTier::Db2Character:
            ref.resolved.source = canon::NameSource::Db2;
            break;
        default:
            // Literal/fuzzy/knowledge-base tiers all name the texture from
            // a fdid or a heuristically-matched filename, not a real
            // content-path or DB2 fact -- husk-synthesized, per I6.
            ref.resolved.source = canon::NameSource::Synthesized;
            break;
    }
    return ref;
}

// One catalog.texture() call per distinct M2 texture-array index actually
// referenced by any of `batches`' real texture units (layer i's combo
// index is `textureComboIndex + i`, m2_material_input.hpp's own
// convention) -- mirrors buildMaterialsAndPrimitives's own per-layer
// resolution (export_materials.cpp) closely enough that both go through the
// identical catalog answer for the same (modelPath, textureSlotIndex), but
// does not replicate its embedded-filename tier (pre-Cataclysm inline
// texture bytes, a real M2 field canon::TextureRef has no shape for) --
// a texture unit with no real fdid AND an embedded filename is left absent
// from the returned map (falls back to canon::TextureRef's own default
// KnownUnresolved, same as an unresolved caller-omitted slot).
m2input::TextureResolutions buildTextureResolutions(const m2::Model& model, const std::vector<skin::Batch>& batches,
                                                   sources::Catalog& catalog, const std::string& modelPath,
                                                   uint32_t objectSkinTextureFileDataId) {
    m2input::TextureResolutions result;
    sources::TextureModelContext modelCtx;
    modelCtx.modelPath = modelPath;
    if (model.header.textureFileDataIds) modelCtx.ownTextureFileDataIds = *model.header.textureFileDataIds;

    for (const auto& b : batches) {
        for (uint32_t layer = 0; layer < b.textureCount; ++layer) {
            size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layer;
            if (comboIdx >= model.textureCombos.size()) break;  // best-effort beyond layer 0, same as legacy
            uint16_t textureIndex = model.textureCombos[comboIdx];
            if (textureIndex >= model.textures.size()) break;
            if (result.count(textureIndex)) continue;  // catalog memoizes per slot anyway; avoid redundant work

            const m2::Texture& tex = model.textures[textureIndex];
            uint32_t fdid = (model.header.textureFileDataIds && textureIndex < model.header.textureFileDataIds->size())
                                 ? (*model.header.textureFileDataIds)[textureIndex]
                                 : 0;
            if (fdid == 0 && tex.type == 2 && objectSkinTextureFileDataId != 0) {
                fdid = objectSkinTextureFileDataId;
            }
            if (fdid == 0 && !tex.filename.empty()) continue;  // embedded-filename tier, out of scope here

            bool preferGlowVariant = false;
            if (b.materialIndex < model.materials.size()) {
                preferGlowVariant = model.materials[b.materialIndex].blendMode > 2;
            }
            modelCtx.textureSlotIndex = textureIndex;
            auto resolved = catalog.texture(fdid, tex.type, modelCtx, preferGlowVariant);
            result.emplace(textureIndex, toCanonTextureRef(resolved, fdid));
        }
    }
    return result;
}

// Zero-pads `value` to at least `width` digits (e.g. zeroPad(69, 4) ==
// "0069") -- same tiny helper export_animation.cpp's own
// findAnimFileByBasename uses, reimplemented rather than shared (see this
// file's own note below on why nothing here calls into export_animation.*
// at all).
std::string zeroPad(unsigned value, size_t width) {
    std::string s = std::to_string(value);
    if (s.size() < width) s.insert(0, width - s.size(), '0');
    return s;
}

// Finds the FileDataID for sequence (animId, subAnimId) in `animFileIds`,
// mirroring export_animation.cpp's own (private, anonymous-namespace)
// findAnimFileId exactly.
uint32_t findAnimFileId(const std::optional<std::vector<m2::Header::AnimFileEntry>>& animFileIds, uint16_t animId,
                        uint16_t subAnimId) {
    if (!animFileIds) return 0;
    for (const auto& e : *animFileIds) {
        if (e.animId == animId && e.subAnimId == subAnimId && e.fileId != 0) return e.fileId;
    }
    return 0;
}

// Real wow.export-style same-basename fallback naming, mirroring
// export_animation.cpp's own (public) findAnimFileByBasename exactly.
std::filesystem::path findAnimFileByBasename(const std::string& modelPath, const std::string& animDir,
                                              uint16_t animId, uint16_t subAnimId) {
    std::string baseName = std::filesystem::path(modelPath).stem().string();
    std::string fileName = baseName + zeroPad(animId, 4) + "-" + zeroPad(subAnimId, 2) + ".anim";
    return std::filesystem::path(animDir) / fileName;
}

// One non-inline, non-alias sequence's already-loaded external .anim
// payload bytes, mirroring buildAnimations's own external-.anim resolution
// (export_animation.cpp: FileDataID-named file first, same-basename
// fallback second, AFSB-over-AFM2 priority in a chunked file) exactly --
// reimplemented independently in THIS file rather than calling into or
// factoring shared code out of export_animation.cpp/.hpp. Deliberate:
// `--compare-canon`'s whole point is comparing canon:: against an
// UNTOUCHED legacy pipeline (this file's own top doc comment already says
// so) -- editing a legacy source file to share code with this one, even a
// behavior-preserving pure refactor, means the "legacy" side of every
// comparison this tool ever runs is no longer the real, ordinarily-shipping
// legacy pipeline but a version modified for canon::'s benefit. A small
// amount of duplication here is the correct price for that guarantee, the
// same tradeoff canon_model.cpp/m2_animation_input.cpp already made
// for the sequence-flag bit tests (see their own comments on it) — applied
// here to a whole resolution routine instead of one bit test, but the same
// principle.
std::optional<std::vector<uint8_t>> resolveExternalAnimBlobForCanon(
    const m2::Sequence& seq, const std::optional<std::vector<m2::Header::AnimFileEntry>>& animFileIds,
    bool animChunked, const std::string& animDir, const std::string& modelPath) {
    if (animDir.empty()) return std::nullopt;

    std::filesystem::path animPath;
    uint32_t fileId = findAnimFileId(animFileIds, seq.id, seq.variationIndex);
    if (fileId != 0) {
        animPath = std::filesystem::path(animDir) / (std::to_string(fileId) + ".anim");
    }
    std::ifstream f;
    if (!animPath.empty()) f.open(animPath, std::ios::binary);
    if (!f.is_open()) {
        animPath = findAnimFileByBasename(modelPath, animDir, seq.id, seq.variationIndex);
        f.open(animPath, std::ios::binary);
    }
    if (!f.is_open()) return std::nullopt;  // not available locally under either naming convention

    std::vector<uint8_t> animFileBytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!animChunked) {
        return m2::extractAnimBlob(animFileBytes, animChunked);
    }
    auto topChunks = readChunks(animFileBytes.data(), animFileBytes.size());
    if (auto afsb = findChunk(topChunks, "AFSB")) {
        return std::vector<uint8_t>(afsb->data, afsb->data + afsb->size);
    }
    if (findChunk(topChunks, "AFM2")) {
        return m2::extractAnimBlob(animFileBytes, animChunked);
    }
    return std::nullopt;  // neither AFM2 nor AFSB -- an unrecognized future shape
}

// One resolveExternalAnimBlobForCanon call per non-inline, non-alias
// sequence, keyed the same way ExternalAnimBlobs' own doc comment
// requires: a pure-alias sequence never gets its own entry here
// (m2input::buildCanonModel's pass 3 resolves it via its terminal's index
// instead, and a non-alias sequence IS its own terminal, so keying by `si`
// directly is correct for every entry this function does produce). Empty
// when `animDir` is empty, same "nothing to resolve" no-op every other
// opt-in enrichment here uses.
m2input::ExternalAnimBlobs buildExternalAnimBlobs(const m2::Model& model, const std::string& animDir,
                                                   const std::string& modelPath) {
    m2input::ExternalAnimBlobs blobs;
    if (animDir.empty()) return blobs;

    bool animChunked = (model.header.globalFlags & 0x200000) != 0;

    for (size_t si = 0; si < model.sequences.size(); ++si) {
        const m2::Sequence& seq = model.sequences[si];
        if ((seq.flags & kSequenceStoredInlineFlag) != 0) continue;
        if ((seq.flags & kSequenceAliasFlag) != 0) continue;  // pure alias -- resolved via its terminal, not here
        if (auto blob = resolveExternalAnimBlobForCanon(seq, model.header.animFileIds, animChunked, animDir,
                                                          modelPath)) {
            blobs.emplace(static_cast<uint32_t>(si), std::move(*blob));
        }
    }
    return blobs;
}

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
                           const std::vector<gltf::Animation>& legacyAnimations, const std::string& outputPath,
                           sources::Catalog& catalog, const std::string& modelPath,
                           uint32_t objectSkinTextureFileDataId, const std::string& animDir, bool bonesAreInline,
                           const std::vector<gltf::Material>& legacyMaterials) {
    try {
        auto skinBytes = readFileBytes(skinPath);
        skin::Header skinHeader = skin::parseHeader(skinBytes);
        std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);
        std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinBytes, skinHeader.submeshes);
        std::vector<skin::Batch> batches = skin::parseBatches(skinBytes, skinHeader.batches);

        m2input::TextureResolutions textureResolutions =
            buildTextureResolutions(model, batches, catalog, modelPath, objectSkinTextureFileDataId);
        // See runCanonCompareExport's own doc comment (header): external-anim
        // resolution only makes sense against the inline M2 source
        // m2input::buildCanonModel actually reads -- a .skel-sourced bones/
        // sequences pair would need keys into a different sequence array
        // entirely, which canon::Model has no representation of at all yet.
        m2input::ExternalAnimBlobs externalAnimBlobs =
            bonesAreInline ? buildExternalAnimBlobs(model, animDir, modelPath) : m2input::ExternalAnimBlobs{};

        canon::Model canonModel = m2input::buildCanonModel(model, batches, submeshes, triangleIndices,
                                                             externalAnimBlobs, textureResolutions);

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
            canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, model.materials, legacyMaterials,
                                                   legacyMesh.primitives);
        anyDeviation = anyDeviation || !materialReport.ok();
        printReport("materials", materialReport);

        std::cerr << "husk: canon-compare: "
                  << (anyDeviation ? "DEVIATIONS FOUND -- see above" : "clean, no deviations found") << "\n";
    } catch (const std::exception& e) {
        std::cerr << "husk: canon-compare: failed: " << e.what() << "\n";
    }
}

}  // namespace husk::commands
