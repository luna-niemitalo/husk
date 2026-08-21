#include "export_materials.hpp"

#include <algorithm>
#include <iostream>
#include <set>

#include "export_texture_resolution.hpp"
#include "m2_shader_names.hpp"

// buildMaterialsAndPrimitives: one .skin batch -> one glTF material +
// primitive (blend mode/render flags, static tint/fade, texture slot,
// multi-texture-layer metadata, UV transform). Texture-candidate
// resolution and the animated-curve resolvers this leans on live in
// export_texture_resolution.hpp/.cpp instead -- split out per
// TODO/CLEANUP_TODO.md's Item 1 (this file was 1,344 lines, two genuinely
// separate concerns bundled into one translation unit: "which real bytes
// does a texture slot resolve to" vs. "how does one batch become a glTF
// material/primitive"). scanDirOrWarn stays here (declared in
// export_materials.hpp) since it's shared with export_skin_resolution.cpp
// too, not exclusive to either concern above.
namespace husk::commands {

namespace {

// M2Material flags (wowdev.wiki M2#Render_flags_and_blending_modes). Only
// 0x04 (two-sided) was translated before; 0x01 (unlit) is the other bit
// with a real glTF equivalent (KHR_materials_unlit). depthTest/depthWrite
// (0x08/0x10) have no core-glTF equivalent at all so aren't translated --
// surfaced as raw `flags` on m2::Material for a consumer that wants them.
constexpr uint16_t kMaterialUnlitFlag = 0x01;
constexpr uint16_t kMaterialTwoSidedFlag = 0x04;

}  // namespace

std::filesystem::directory_iterator scanDirOrWarn(const std::string& dir, const char* purpose) {
    std::error_code statEc;
    auto st = std::filesystem::status(dir, statEc);
    if (statEc) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': " << statEc.message() << "\n";
        return {};
    }
    if (st.type() == std::filesystem::file_type::not_found) {
        std::error_code linkEc;
        auto lst = std::filesystem::symlink_status(dir, linkEc);
        bool brokenSymlink = !linkEc && lst.type() == std::filesystem::file_type::symlink;
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': "
                  << (brokenSymlink ? "broken symlink (target doesn't exist)" : "no such directory")
                  << "\n";
        return {};
    }
    if (st.type() != std::filesystem::file_type::directory) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': not a directory\n";
        return {};
    }
    std::error_code iterEc;
    std::filesystem::directory_iterator it(dir, iterEc);
    if (iterEc) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': " << iterEc.message() << "\n";
        return {};
    }
    return it;
}

BuiltMaterials buildMaterialsAndPrimitives(const std::vector<uint32_t>& triangleIndices,
                                            const std::vector<skin::Submesh>& submeshes,
                                            const std::vector<skin::Batch>& batches,
                                            const M2MaterialInputs& m2,
                                            const std::string& texturesDir,
                                            const std::string& modelPath,
                                            const std::string& texturesOutDir,
                                            const std::unordered_map<uint32_t, std::string>& listfile,
                                            const std::string& listfileRootArg,
                                            uint32_t objectSkinTextureFileDataId) {
    const std::string& listfileRoot = listfileRootArg.empty() ? texturesDir : listfileRootArg;
    BuiltMaterials result;

    // Scanned once per skin/LOD, shared and depleted across every batch
    // below -- see scanFuzzyTexturePool's doc comment for why a fresh
    // per-batch scan would wrongly reuse the same real file across every
    // unresolved slot.
    FuzzyTexturePool fuzzyTexturePool = scanFuzzyTexturePool(texturesDir, modelPath);
    std::string modelBasenameLower = lowercaseModelBasename(modelPath);

    // A same-basename fuzzy candidate whose own trailing FileDataID matches
    // one of this exact M2's own texture-array entries (TXID chunk) is
    // never a plausible guess for an unrelated hardcoded slot -- it's
    // already a real, specifically-identified texture belonging to some
    // *other* M2 texture-array index (e.g. a particle/ribbon-emitter
    // sprite, referenced by array index rather than by any material
    // batch), not an unknown file a human just happened to name after this
    // model. Real bug this fixes: `ethereal2_f.m2`'s monster_1/monster_2/
    // monster_3/environment slots (no FileDataID of their own -- runtime-
    // filled by the client, not stored in the M2 at all) were picking up
    // this same model's own particle-effect sprite textures purely because
    // they share the model's basename under the community listfile's
    // naming convention, rendering the arm/leg geosets with an almost
    // entirely transparent ribbon-trail image instead of no candidate at
    // all.
    if (m2.textureFileDataIds) {
        fuzzyTexturePool.files.erase(
            std::remove_if(fuzzyTexturePool.files.begin(), fuzzyTexturePool.files.end(),
                            [&](const std::filesystem::path& p) {
                                auto fdid = fuzzyCandidateFileDataId(p, modelBasenameLower);
                                return fdid && std::find(m2.textureFileDataIds->begin(),
                                                          m2.textureFileDataIds->end(),
                                                          *fdid) != m2.textureFileDataIds->end();
                            }),
            fuzzyTexturePool.files.end());
    }

    // Content signature (materialDedupKey) -> that material's index in
    // result.materials -- real M2 corpus models routinely have dozens of
    // batches drawing with the exact same effective material (a shared base
    // material split only by which submesh/geoset each batch happens to
    // cover), and until this, husk emitted one full gltf::Material (and one
    // full embedded image) per *batch*, not per distinct material -- a real
    // `bloodelffemale_hd.m2` export had 114 materials where a handful of
    // truly distinct ones would do. A batch whose built gm matches an
    // already-emitted one by every field that isn't purely batch-numbering
    // (materialDedupKey's own doc comment) has its primitive point at the
    // existing material instead of creating a new one.
    std::unordered_map<std::string, size_t> materialByKey;

    // Ambiguous-candidate byte cache, keyed by path -- a real character
    // model can have dozens of hardcoded slots that are *all* ambiguous
    // against the *same* shared candidate pool (confirmed: 19 slots, 94
    // candidates each, on a real `bloodelffemale_hd.m2` export), and
    // without this every one of those slots independently re-reads and
    // re-decodes every candidate from disk -- 1,786 redundant BLP decodes
    // for that one real file, ~5.5 minutes of runtime for what should be
    // ~94. Read once, reused by every ambiguous slot that needs the same
    // file (see gltf_mesh.cpp's emitMaterial for the matching fix that
    // also keeps the *embedded* bytes from being duplicated once per
    // material in the final .glb, not just the read/decode cost here).
    std::map<std::filesystem::path, std::vector<uint8_t>> ambiguousCandidateCache;

    // Per-M2-texture-array-index fuzzy resolution cache -- a real M2
    // texture slot (fdid == 0, or a resolved fdid with no matching local
    // file) can be referenced by more than one batch, e.g.
    // `argustalbukmount.m2`'s texture 0 (the sole monster_1 candidate) is
    // used by both a body batch and a separate horns-geoset batch. Without
    // this, the second batch to reach the block below re-runs
    // claimSoleFuzzyTextureCandidate against the *same* shared, depleting
    // pool -- already emptied by the first batch, so the horns geoset
    // silently got no texture at all (a real, confirmed bug: the pool's
    // "claim and remove" design is correct for genuinely *different*
    // hardcoded slots competing for one pool, but wrong for the same slot
    // being resolved twice). Keying by textureIndex (not materialIndex or
    // gm.textureType) makes every batch that references the identical M2
    // texture-array entry agree on the identical answer, which also lets
    // materialDedupKey (below) correctly merge them into one glTF material
    // instead of two.
    struct FuzzyResolution {
        bool found = false;
        bool isAmbiguous = false;  // true -> ambiguousMatches-style diagnostic, false -> fuzzyMatches-style
        std::string nameSuffix;
        std::vector<uint8_t> baseColorImagePng;
        std::string baseColorImageName;
        std::string matchedFilename;
        std::vector<gltf::Material::AlternateTextureCandidate> alternateTextureCandidates;
    };
    std::unordered_map<uint16_t, FuzzyResolution> fuzzyResolutionByTextureIndex;

    if (batches.empty()) {
        // A genuinely geometry-less .skin (real corpus
        // shape -- pure particle/ribbon VFX models, zero vertices at the M2
        // level, not just an empty batch table) has no triangles to put in
        // a primitive at all. Leave `result.primitives` empty rather than
        // manufacturing one with empty `indices` -- glTF has no valid
        // "primitive with zero indices" representation, so the caller
        // (cmd_export.cpp) skips adding a mesh node for this LOD tier
        // entirely when `result.primitives` comes back empty.
        if (triangleIndices.empty()) {
            return result;
        }
        gltf::Primitive prim;
        prim.indices = triangleIndices;
        result.primitives.push_back(std::move(prim));
        return result;
    }

    std::set<uint16_t> skinSectionIds;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s skinSectionIndex (" +
                                      std::to_string(b.skinSectionIndex) +
                                      ") is out of range for " + std::to_string(submeshes.size()) +
                                      " submeshes");
        }
        const auto& sm = submeshes[b.skinSectionIndex];
        skinSectionIds.insert(sm.skinSectionId);
        if (b.textureCount > 1) {
            ++result.multiTextureBatchCount;
        }
        if (static_cast<size_t>(sm.indexStart) + sm.indexCount > triangleIndices.size()) {
            throw std::runtime_error(
                "submesh " + std::to_string(b.skinSectionIndex) +
                "'s index range runs past the end of the resolved triangle-index buffer -- "
                "corrupted .skin?");
        }

        // The minority case: a submesh with zero indices
        // alongside sibling submeshes that have real geometry (mixed real+
        // empty geosets in one .skin) -- the same "don't manufacture
        // a primitive glTF can't represent" rule applies per-primitive:
        // skip just this batch (no primitive, no material) rather than
        // emitting a zero-indices primitive that would fail writeGlbMulti's
        // hard check.
        if (sm.indexCount == 0) {
            continue;
        }

        gltf::Primitive prim;
        prim.indices.assign(triangleIndices.begin() + sm.indexStart,
                             triangleIndices.begin() + sm.indexStart + sm.indexCount);
        prim.skinSectionId = sm.skinSectionId;

        if (b.materialIndex >= m2.materials.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s materialIndex (" +
                                      std::to_string(b.materialIndex) + ") is out of range for " +
                                      std::to_string(m2.materials.size()) + " materials");
        }
        const auto& mat = m2.materials[b.materialIndex];

        gltf::Material gm;
        gm.alphaMode = alphaModeForBlend(mat.blendMode);
        gm.blendMode = mat.blendMode;
        {
            m2::ShaderNames shaderNames = m2::resolveShaderNames(b.shaderId, b.textureCount);
            if (shaderNames.resolved) {
                gm.pixelShaderName = shaderNames.pixel;
                gm.vertexShaderName = shaderNames.vertex;
            }
        }
        gm.doubleSided = (mat.flags & kMaterialTwoSidedFlag) != 0;
        gm.unlit = (mat.flags & kMaterialUnlitFlag) != 0;
        // Kept as a live prefix on gm.name while the rest of this loop body
        // appends the resolved-texture suffixes below (diagnostics further
        // down still reference the per-batch name) -- stripped back off
        // right before this material is actually stored, once dedup
        // (materialByKey below) has decided whether a new material entry
        // is needed at all. The stored material's own name should describe
        // *what it is* (mat<M>_tex<T>_<id>), not which batch happened to
        // be the first one to produce it.
        std::string batchPrefix = "batch" + std::to_string(bi) + "_";
        gm.name = batchPrefix + "mat" + std::to_string(b.materialIndex);

        // Vertex-color tint + combined alpha/texture-weight fade (static
        // approximation -- see m2::Color/TextureWeight). colorIndex is
        // genuinely optional (0xFFFF/"none" is common and expected);
        // textureWeightComboIndex is not documented as nullable and every
        // real batch this was tested against has a valid one, so it's
        // resolved unconditionally and bounds-checked like any other index.
        if (b.colorIndex != 0xFFFF) {
            if (b.colorIndex >= m2.colors.size()) {
                throw std::runtime_error("batch " + std::to_string(bi) + "'s colorIndex (" +
                                          std::to_string(b.colorIndex) + ") is out of range for " +
                                          std::to_string(m2.colors.size()) + " colors");
            }
            const auto& color = m2.colors[b.colorIndex];
            if (color.color) {
                gm.baseColorFactor[0] = color.color->x;
                gm.baseColorFactor[1] = color.color->y;
                gm.baseColorFactor[2] = color.color->z;
            }
            if (color.alpha) {
                gm.baseColorFactor[3] *= *color.alpha;
            }
            if (color.colorAnimated || color.alphaAnimated) {
                ++result.animatedTintOrFadeBatchCount;
                // Full curve dump -- diagnostic-only
                // extras, see gltf::Material::tintAnimation/
                // alphaFadeAnimation's doc comments. `m2.blob` is only
                // unset for a hypothetical caller that never populated it
                // (none exists today, see M2MaterialInputs's doc comment) --
                // best-effort like additionalTextureLayers/textureTransform
                // above, not required for a usable export.
                if (m2.blob) {
                    if (color.colorAnimated) {
                        gm.tintAnimation = resolveAnimatedColorCurve(*m2.blob, color.colorTrackOffset,
                                                                       m2.sequenceCount);
                    }
                    if (color.alphaAnimated) {
                        gm.alphaFadeAnimation = resolveAnimatedFixed16Curve(
                            *m2.blob, color.alphaTrackOffset, m2.sequenceCount);
                    }
                }
            }
        }
        // Like textureCoordCombos above, treat a completely empty table as
        // "this model doesn't use this feature" rather than an error --
        // unlike textureCoordCombos there's no documented version cutoff
        // for this one, but the same defensive reasoning applies: a
        // model-wide absence of transparency-weight data shouldn't turn
        // into every single batch failing to export.
        if (!m2.textureWeightCombos.empty()) {
            if (b.textureWeightComboIndex >= m2.textureWeightCombos.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s textureWeightComboIndex (" +
                    std::to_string(b.textureWeightComboIndex) + ") is out of range for " +
                    std::to_string(m2.textureWeightCombos.size()) + " textureWeightCombos entries");
            }
            uint16_t weightIndex = m2.textureWeightCombos[b.textureWeightComboIndex];
            if (weightIndex >= m2.textureWeights.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s texture weight (index " +
                    std::to_string(weightIndex) + " via textureWeightCombos[" +
                    std::to_string(b.textureWeightComboIndex) + "]) is out of range for " +
                    std::to_string(m2.textureWeights.size()) + " textureWeights entries");
            }
            const auto& weight = m2.textureWeights[weightIndex];
            if (weight.weight) {
                gm.baseColorFactor[3] *= *weight.weight;
            }
            if (weight.weightAnimated) {
                ++result.animatedTintOrFadeBatchCount;
                if (m2.blob) {
                    gm.weightFadeAnimation = resolveAnimatedFixed16Curve(
                        *m2.blob, weight.weightTrackOffset, m2.sequenceCount);
                }
            }
        }

        if (b.textureCount > 0) {
            if (b.textureComboIndex >= m2.textureCombos.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s textureComboIndex (" +
                    std::to_string(b.textureComboIndex) + ") is out of range for " +
                    std::to_string(m2.textureCombos.size()) + " textureCombos entries");
            }
            uint16_t textureIndex = m2.textureCombos[b.textureComboIndex];
            if (textureIndex >= m2.textures.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s texture (index " +
                    std::to_string(textureIndex) + " via textureCombos[" +
                    std::to_string(b.textureComboIndex) + "]) is out of range for " +
                    std::to_string(m2.textures.size()) + " textures");
            }
            gm.name += "_tex" + std::to_string(textureIndex);

            // M2Texture::type -- see
            // gltf::Material::textureType's doc comment for why this is a
            // real "husk can't resolve this locally" signal for any nonzero
            // value, not just missing PNG data.
            gm.textureType = m2.textures[textureIndex].type;
            // A real semantic name (e.g. "_skin", "_char_hair") instead of
            // a bare "_tex<N>" whenever the type is known -- per Luna's own
            // "clearly named slots based on the texture they utilize" ask.
            // Type 0 (a real embedded/FileDataID-resolvable texture) gets
            // no suffix here; its own filename/FileDataID below already
            // says more than the generic type name would.
            if (const char* typeName = m2::textureTypeName(gm.textureType)) {
                gm.name += std::string("_") + typeName;
            }

            // Second UV set (roadmap "Second UV set" feature): only
            // meaningful pre-Cataclysm, see M2MaterialInputs::
            // textureCoordCombos's doc comment -- an empty table (every
            // modern file) always means UV set 0.
            if (!m2.textureCoordCombos.empty()) {
                if (b.textureCoordComboIndex >= m2.textureCoordCombos.size()) {
                    throw std::runtime_error(
                        "batch " + std::to_string(bi) + "'s textureCoordComboIndex (" +
                        std::to_string(b.textureCoordComboIndex) + ") is out of range for " +
                        std::to_string(m2.textureCoordCombos.size()) +
                        " textureCoordCombos entries");
                }
                uint16_t mapping = m2.textureCoordCombos[b.textureCoordComboIndex];
                // 0xFFFF (-1) is environment mapping, which has no glTF
                // equivalent -- fall back to UV set 0 rather than guessing.
                if (mapping == 1) {
                    gm.baseColorTexCoord = 1;
                }
            }

            uint32_t fdid = (m2.textureFileDataIds && textureIndex < m2.textureFileDataIds->size())
                                 ? (*m2.textureFileDataIds)[textureIndex]
                                 : 0;
            if (fdid == 0 && gm.textureType == 2 && objectSkinTextureFileDataId != 0) {
                fdid = objectSkinTextureFileDataId;
            }
            const std::string& embeddedFilename = m2.textures[textureIndex].filename;
            std::string embeddedStem;
            std::optional<std::vector<uint8_t>> embeddedBytes;
            if (!embeddedFilename.empty() && !texturesDir.empty()) {
                embeddedStem = std::filesystem::path(embeddedFilename).stem().string();
                embeddedBytes = resolveTextureBytes(std::filesystem::path(texturesDir) / embeddedStem,
                                                     texturesDir, texturesOutDir);
            }

            // Priority order: every *deterministic* signal (never a guess,
            // never touches the shared fuzzy pool) before the one heuristic
            // signal (a real-name-only extraction has no other way to
            // identify a texture). `fdid`, when resolved, is recorded in
            // the material name and `gm.baseColorTextureFileDataId`
            // regardless of which path below actually supplies the
            // embedded bytes.
            //
            // The fuzzy pool specifically is deliberately tried *last*, not
            // first: it's a real, if bounded, guess, and the pool is shared
            // and depleted across every batch in this call -- letting a
            // slot draw from it before checking whether it already has a
            // *working*, deterministic match would let an early,
            // genuinely-hardcoded slot claim a real file that actually
            // belongs (by a later-processed slot's own resolvable
            // FileDataID) to someone else, silently mismatching *both*
            // slots.
            if (fdid != 0) {
                gm.name += "_fdid" + std::to_string(fdid);
                gm.baseColorTextureFileDataId = fdid;
            }

            bool embedded = false;
            if (embeddedBytes) {
                // A real embedded path (wowdev.wiki M2#Textures, older/
                // classic-era files per m2::Texture's own doc comment).
                // Not a guess: `filename` is real data straight from this
                // M2, so the same basename (BLP or PNG) is an exact lookup
                // -- the single most precise signal available, tried first
                // regardless of whether a FileDataID also resolved.
                gm.name += "_" + embeddedStem;
                gm.baseColorImagePng = std::move(*embeddedBytes);
                gm.baseColorImageName = embeddedStem;
                embedded = true;
            }
            if (!embedded && fdid != 0 && !texturesDir.empty()) {
                // Deterministic whenever the file is actually present --
                // still the right answer for a real extraction that *does*
                // use the FileDataID-named convention (this project's own
                // test fixtures and the common "casc-tool-style"
                // "<FileDataID>.{png,blp}" layout both do).
                if (auto bytes = resolveTextureBytes(std::filesystem::path(texturesDir) /
                                                          std::to_string(fdid),
                                                      texturesDir, texturesOutDir)) {
                    gm.baseColorImagePng = std::move(*bytes);
                    gm.baseColorImageName = std::to_string(fdid);
                    embedded = true;
                }
            }
            if (!embedded && fdid != 0 && !texturesDir.empty() && !listfile.empty()) {
                // --listfile fallback: a real corpus export commonly keeps
                // files under their real name/path (e.g.
                // "world/goober/bubble.blp"), not renamed to a bare
                // FileDataID -- confirmed against a real 130k-file corpus
                // scan (casc-tool's own FAILURES.md item 13), where 99.9%
                // of "missing" FileDataID textures turned out to be present
                // elsewhere in the tree under their real listfile name.
                // Still deterministic (the listfile is real data, not a
                // guess), so this comes before the fuzzy same-basename pool
                // below -- resolved against `listfileRoot` (--listfile-root,
                // defaulting to texturesDir), deliberately not `texturesDir`
                // itself: the corpus root a listfile's paths are relative to
                // is typically many directories away from any one model,
                // unlike the directory-local matching above.
                if (auto found = listfile.find(fdid); found != listfile.end()) {
                    auto stem = std::filesystem::path(listfileRoot) /
                                std::filesystem::path(found->second).replace_extension();
                    if (auto bytes = resolveTextureBytes(stem, listfileRoot, texturesOutDir)) {
                        gm.baseColorImagePng = std::move(*bytes);
                        gm.baseColorImageName = stem.filename().string();
                        embedded = true;
                    }
                }
            }
            if (!embedded) {
                // Last resort, for whatever's left unresolved after both
                // deterministic paths above -- a genuinely hardcoded slot
                // (fdid == 0, as before this change), *or* a slot whose
                // FileDataID resolved but no "<fdid>.{png,blp}" actually
                // exists in texturesDir (new: previously this case silently
                // embedded nothing, even when a real, descriptively-named
                // file for it was sitting right there unclaimed).
                //
                // Resolved once per M2 texture-array index and cached
                // (fuzzyResolutionByTextureIndex, declared above) -- see its
                // own doc comment for why a second batch referencing the
                // same textureIndex must reuse this answer rather than
                // re-touching the shared, depleting pool.
                auto [cacheIt, isNewIndex] = fuzzyResolutionByTextureIndex.try_emplace(textureIndex);
                FuzzyResolution& resolution = cacheIt->second;
                if (isNewIndex) {
                    if (auto fuzzy =
                            claimSoleFuzzyTextureCandidate(fuzzyTexturePool, gm.textureType, modelBasenameLower)) {
                        if (auto bytes = readTextureFileBytes(*fuzzy, texturesDir, texturesOutDir)) {
                            resolution.found = true;
                            resolution.nameSuffix = "_" + fuzzy->stem().string();
                            resolution.baseColorImagePng = std::move(*bytes);
                            resolution.baseColorImageName = fuzzy->stem().string();
                            resolution.matchedFilename = fuzzy->filename().string();
                        }
                    } else {
                        // Not claimed (removed) from the shared pool -- every
                        // other ambiguous slot is equally uninformed about
                        // *this* slot's own real candidates and deserves its
                        // own independently-filtered view, not whichever's left
                        // after an earlier slot's pick. Narrowed to only the
                        // candidates compatible with this slot's textureType,
                        // preferring real recognized-category matches over
                        // unlabeled ones (filterCandidatesForType's doc
                        // comment) -- a real, grounded exclusion, not a guess
                        // about which one is correct.
                        auto matching =
                            filterCandidatesForType(fuzzyTexturePool.files, gm.textureType, modelBasenameLower);
                        if (matching.size() > 1) {
                            // Genuinely ambiguous: 2+ type-compatible
                            // candidates, no way to tell which one this slot
                            // actually wants. Embed every one of them
                            // (gltf_mesh.hpp's AlternateTextureCandidate doc
                            // comment has the full rationale), same "export
                            // everything, let the client filter" treatment
                            // mutually-exclusive geosets already get --
                            // reordered first so the most plausible default
                            // (orderCandidatesForDefault's doc comment) lands at
                            // front(), the one that becomes the wired default.
                            orderCandidatesForDefault(matching, texturesDir, texturesOutDir, modelBasenameLower,
                                                       ambiguousCandidateCache, mat.blendMode > 2);
                            for (const auto& candidatePath : matching) {
                                auto cached = ambiguousCandidateCache.find(candidatePath);
                                if (cached == ambiguousCandidateCache.end()) {
                                    auto bytes = readTextureFileBytes(candidatePath, texturesDir, texturesOutDir);
                                    if (!bytes) continue;
                                    cached = ambiguousCandidateCache.emplace(candidatePath, std::move(*bytes)).first;
                                }
                                gltf::Material::AlternateTextureCandidate cand;
                                cand.filename = candidatePath.filename().string();
                                auto category = classifyCandidateCategory(candidatePath, modelBasenameLower);
                                cand.category = category.value_or(std::string());
                                auto [candWidth, candHeight] = pngDimensions(cached->second);
                                cand.width = candWidth;
                                cand.height = candHeight;
                                cand.imagePng = cached->second;
                                resolution.alternateTextureCandidates.push_back(std::move(cand));
                            }
                            if (!resolution.alternateTextureCandidates.empty()) {
                                const auto& chosen = resolution.alternateTextureCandidates.front();
                                resolution.found = true;
                                resolution.isAmbiguous = true;
                                resolution.nameSuffix =
                                    "_" + std::filesystem::path(chosen.filename).stem().string();
                                resolution.baseColorImagePng = chosen.imagePng;
                                resolution.baseColorImageName =
                                    std::filesystem::path(chosen.filename).stem().string();
                                resolution.matchedFilename = chosen.filename;
                            }
                        }
                    }
                }
                if (resolution.found) {
                    gm.name += resolution.nameSuffix;
                    gm.baseColorImagePng = resolution.baseColorImagePng;
                    gm.baseColorImageName = resolution.baseColorImageName;
                    gm.alternateTextureCandidates = resolution.alternateTextureCandidates;
                    if (resolution.isAmbiguous) {
                        std::vector<std::string> allFileNames;
                        allFileNames.reserve(resolution.alternateTextureCandidates.size());
                        for (const auto& cand : resolution.alternateTextureCandidates) {
                            allFileNames.push_back(cand.filename);
                        }
                        result.ambiguousMatches.push_back(
                            {gm.name, resolution.matchedFilename, std::move(allFileNames), fdid});
                    } else {
                        result.fuzzyMatches.push_back({gm.name, resolution.matchedFilename, fdid});
                    }
                }
            }

            // Additional texture layers (textureCount > 1): per wowdev.wiki
            // M2/.skin#Texture_units, textureComboIndex is a *base* index --
            // layer i's real combo index is textureComboIndex + i. Resolved
            // best-effort, not with the same "foreign data must fit its own
            // claims" strictness as the primary texture above: this is
            // supplementary metadata, not required for a usable export, so
            // an out-of-range layer is skipped rather than failing the
            // whole batch.
            for (uint16_t layer = 1; layer < b.textureCount; ++layer) {
                size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layer;
                if (comboIdx >= m2.textureCombos.size()) break;
                uint16_t layerTextureIndex = m2.textureCombos[comboIdx];
                if (layerTextureIndex >= m2.textures.size()) continue;

                gltf::Material::AdditionalTextureLayer al;
                if (!m2.textureCoordCombos.empty()) {
                    size_t coordComboIdx = static_cast<size_t>(b.textureCoordComboIndex) + layer;
                    if (coordComboIdx < m2.textureCoordCombos.size() &&
                        m2.textureCoordCombos[coordComboIdx] == 1) {
                        al.texCoord = 1;
                    }
                }
                if (m2.textureFileDataIds && layerTextureIndex < m2.textureFileDataIds->size()) {
                    al.fileDataId = (*m2.textureFileDataIds)[layerTextureIndex];
                    if (al.fileDataId != 0 && !texturesDir.empty()) {
                        if (auto bytes = resolveTextureBytes(std::filesystem::path(texturesDir) /
                                                                  std::to_string(al.fileDataId),
                                                              texturesDir, texturesOutDir)) {
                            al.imagePng = std::move(*bytes);
                        } else if (!listfile.empty()) {
                            // Same --listfile fallback as the primary
                            // baseColorTexture resolution above -- best-
                            // effort, same "supplementary metadata" tier as
                            // the rest of this loop.
                            if (auto found = listfile.find(al.fileDataId); found != listfile.end()) {
                                auto stem = std::filesystem::path(listfileRoot) /
                                            std::filesystem::path(found->second).replace_extension();
                                if (auto bytes2 = resolveTextureBytes(stem, listfileRoot, texturesOutDir)) {
                                    al.imagePng = std::move(*bytes2);
                                }
                            }
                        }
                    }
                }
                gm.additionalTextureLayers.push_back(std::move(al));
            }
        }

        // UV scroll/rotate/scale animation: resolved the same "sentinel
        // means none" way colorIndex is, then exposed as inert extras --
        // see m2::TextureTransform's doc comment for why this never becomes
        // a real KHR_texture_transform on the render. Best-effort like the
        // additional-texture-layers loop just above (an out-of-range index
        // is skipped, not a failure) -- this is supplementary metadata, not
        // required for a usable export.
        if (b.textureTransformComboIndex != 0xFFFF &&
            b.textureTransformComboIndex < m2.textureTransformCombos.size()) {
            uint16_t transformIndex = m2.textureTransformCombos[b.textureTransformComboIndex];
            if (transformIndex < m2.textureTransforms.size()) {
                const auto& xf = m2.textureTransforms[transformIndex];
                gltf::Material::TextureTransform gxf;
                gxf.constant =
                    !xf.translationAnimated && !xf.rotationAnimated && !xf.scalingAnimated;
                if (xf.translation) {
                    gxf.translation = {xf.translation->x, xf.translation->y, xf.translation->z};
                }
                if (xf.rotation) {
                    gxf.rotation[0] = xf.rotation->x;
                    gxf.rotation[1] = xf.rotation->y;
                    gxf.rotation[2] = xf.rotation->z;
                    gxf.rotation[3] = xf.rotation->w;
                }
                if (xf.scaling) {
                    gxf.scaling = {xf.scaling->x, xf.scaling->y, xf.scaling->z};
                }
                gm.textureTransform = gxf;
                ++result.textureTransformBatchCount;

                // The animated case's real keyframe data -- see
                // gltf::Material::textureTransformTranslationAnimation's doc
                // comment for why this can never become real KHR_texture_
                // transform playback. Best-effort like the tint/fade curves
                // above: m2.blob is only unset for a hypothetical caller
                // that never populated it.
                if (m2.blob) {
                    if (xf.translationAnimated) {
                        gm.textureTransformTranslationAnimation = resolveAnimatedColorCurve(
                            *m2.blob, xf.translationTrackOffset, m2.sequenceCount);
                    }
                    if (xf.rotationAnimated) {
                        gm.textureTransformRotationAnimation = resolveAnimatedRawQuatCurve(
                            *m2.blob, xf.rotationTrackOffset, m2.sequenceCount);
                    }
                    if (xf.scalingAnimated) {
                        gm.textureTransformScalingAnimation = resolveAnimatedColorCurve(
                            *m2.blob, xf.scalingTrackOffset, m2.sequenceCount);
                    }
                }
            }
        }

        std::string dedupKey = materialDedupKey(gm);
        auto existing = materialByKey.find(dedupKey);
        if (existing != materialByKey.end()) {
            prim.materialIndex = static_cast<int>(existing->second);
        } else {
            size_t idx = result.materials.size();
            materialByKey.emplace(std::move(dedupKey), idx);
            gm.name = gm.name.substr(batchPrefix.size());
            prim.materialIndex = static_cast<int>(idx);
            result.materials.push_back(std::move(gm));
        }
        result.primitives.push_back(std::move(prim));
    }

    result.distinctSkinSectionIds.assign(skinSectionIds.begin(), skinSectionIds.end());

    // Whatever's left in the pool never got claimed -- either nothing
    // needed it (fine, silent) or 2+ files shared the model's basename and
    // husk couldn't tell which unresolved slot(s) they belonged to. Reported
    // once per skin/LOD, not per batch, so their existence is visible
    // without being noisy.
    if (fuzzyTexturePool.files.size() > 1) {
        std::cout << "husk: note: " << fuzzyTexturePool.files.size()
                  << " texture file(s) in '" << texturesDir
                  << "' share this model's basename but husk can't tell which hardcoded texture "
                     "slot each belongs to -- none were embedded\n";
    }

    return result;
}

}  // namespace husk::commands
