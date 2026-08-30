#include "export_materials.hpp"

#include <algorithm>
#include <iostream>
#include <set>

#include "export_texture_resolution.hpp"
#include "m2_shader_names.hpp"
#include "sources/listfile_catalog.hpp"
#include "sources/texture_catalog.hpp"

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

std::unordered_map<uint32_t, CustomizationNameEntry> buildCustomizationNameLookup(
    const gltf::Skeleton& skeleton) {
    std::unordered_map<uint32_t, CustomizationNameEntry> out;
    for (const auto& option : skeleton.customizationOptions) {
        for (const auto& choice : option.choices) {
            for (const auto& mat : choice.materials) {
                if (mat.fileDataId == 0) continue;
                out[mat.fileDataId] = {option.optionName, choice.choiceName};
            }
        }
    }
    return out;
}

sources::CharacterTextureContext buildCharacterTextureContext(const gltf::Skeleton& skeleton) {
    sources::CharacterTextureContext ctx;

    if (skeleton.charTextureLayout) {
        std::unordered_map<uint32_t, uint32_t> rowCountByType;
        std::unordered_map<uint32_t, uint32_t> lastTargetByType;
        for (const auto& layer : skeleton.charTextureLayout->textureLayers) {
            ++rowCountByType[layer.textureType];
            lastTargetByType[layer.textureType] = layer.chrModelTextureTargetId;
        }
        for (const auto& [textureType, count] : rowCountByType) {
            if (count == 1) ctx.singleLayerTargetByTextureType[textureType] = lastTargetByType[textureType];
        }
    }

    // A target claimed by more than one resolved EnabledMaterial (a real
    // but out-of-scope case -- see chrcustomization::Element::
    // relatedChoiceId's own doc comment, "Blindfold"'s two related-choice-
    // conditioned materials for one target) keeps whichever this scan
    // reaches last, same "good enough for a best-effort tier, not a strict
    // identity claim" tolerance buildCustomizationNameLookup's own doc
    // comment already states for the identical shape of ambiguity.
    for (const auto& mat : skeleton.enabledMaterials) {
        ctx.fileDataIdByTarget[mat.chrModelTextureTargetId] = mat.fileDataId;
    }

    return ctx;
}

BuiltMaterials buildMaterialsAndPrimitives(
    const std::vector<uint32_t>& triangleIndices, const std::vector<skin::Submesh>& submeshes,
    const std::vector<skin::Batch>& batches, const M2MaterialInputs& m2, husk::sources::Catalog& catalog,
    const std::string& texturesDir, const std::string& modelPath, const std::string& texturesOutDir,
    const husk::ListfileIndex& listfile, const std::string& listfileRootArg,
    uint32_t objectSkinTextureFileDataId,
    const std::unordered_map<uint32_t, CustomizationNameEntry>& customizationNames) {
    const std::string& listfileRoot = listfileRootArg.empty() ? texturesDir : listfileRootArg;
    BuiltMaterials result;

    // Model identity + this M2's own texture-array FileDataIDs -- catalog's
    // own pool-exclusion rule (ethereal2_f.m2's particle-sprite-vs-
    // hardcoded-slot bug, see sources::Catalog's own doc comment) applies
    // once, the first time this modelPath is seen by `catalog`.
    husk::sources::TextureModelContext modelCtx;
    modelCtx.modelPath = modelPath;
    if (m2.textureFileDataIds) modelCtx.ownTextureFileDataIds = *m2.textureFileDataIds;

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

    // The ambiguous-candidate byte cache and the per-M2-texture-array-index
    // memoization that used to live here (a real character model can have
    // dozens of hardcoded slots all ambiguous against the *same* shared
    // pool -- 19 slots, 94 candidates each, on a real `bloodelffemale_hd.m2`
    // export -- and more than one batch can reference the identical M2
    // texture-array entry, e.g. `argustalbukmount.m2`'s monster_1 texture
    // used by both a body batch and a horns-geoset batch) are now owned by
    // `catalog` itself -- see sources::Catalog::texture()'s own doc comment
    // for the identical reasoning, moved rather than duplicated.

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
                // Real --listfile content name and/or ChrCustomizationOption/
                // Choice name, when either resolves this exact FileDataID --
                // independent of which tier below actually supplies
                // baseColorImagePng's bytes (e.g. a local "<fdid>.png" file
                // can exist even when the listfile also knows this
                // FileDataID's real content-relative path). See gltf::
                // Material::realContentName/customizationChoiceName's own
                // doc comments -- the name-priority assignment at this
                // batch's dedup point (below) is what actually consumes
                // these.
                if (auto name = husk::sources::contentNameForFileDataId(listfile, fdid)) {
                    gm.realContentName = *name;
                }
                if (auto nameIt = customizationNames.find(fdid); nameIt != customizationNames.end()) {
                    gm.customizationOptionName = nameIt->second.optionName;
                    gm.customizationChoiceName = nameIt->second.choiceName;
                }
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
            if (!embedded) {
                // Every fdid-driven tier -- literal, listfile, and the
                // same-basename fuzzy pool (including its claim-and-remove
                // state and genuine-ambiguity fan-out) -- now resolves
                // through one sources::Catalog::texture() call
                // (AUDIT.md §1.1, RESOURCE_CATALOG.md's Settled section
                // "Tier 3's shape"). The old three-way branch (claimed-and-
                // read / nothing-claimed-so-scan-for-ambiguity / 2+
                // candidates) collapses into one Resolved<EncodedTexture>,
                // with genuine ambiguity riding `alternates` rather than a
                // separate success shape. `catalog` memoizes per (modelPath,
                // textureSlotIndex) internally, so a second batch
                // referencing the identical M2 texture-array entry gets the
                // identical answer without re-touching the shared, depleting
                // pool -- the same guarantee this file's own local
                // per-textureIndex cache used to provide, now catalog-owned.
                //
                // Tier 1/2 hits (literal/listfile -- deterministic, no
                // guess) don't get a `gm.name` suffix or a fuzzy/ambiguous
                // diagnostic entry; only a genuine tier-3 fuzzy-pool hit
                // does, matching this function's pre-Catalog behavior
                // exactly.
                modelCtx.textureSlotIndex = textureIndex;
                auto resolved = catalog.texture(fdid, gm.textureType, modelCtx, mat.blendMode > 2);
                if (resolved.found()) {
                    gm.baseColorImagePng = std::move(resolved.value->bytes);
                    gm.baseColorImageName = resolved.value->imageName;
                    if (resolved.tier == husk::sources::ResolutionTier::FuzzySameBasenamePool) {
                        gm.name += "_" + resolved.value->imageName;
                        for (auto& alt : resolved.alternates) {
                            gltf::Material::AlternateTextureCandidate cand;
                            cand.filename = alt.filename;
                            cand.category = alt.category;
                            cand.width = alt.width;
                            cand.height = alt.height;
                            cand.imagePng = std::move(alt.imagePng);
                            gm.alternateTextureCandidates.push_back(std::move(cand));
                        }
                        if (!gm.alternateTextureCandidates.empty()) {
                            std::vector<std::string> allFileNames;
                            allFileNames.reserve(gm.alternateTextureCandidates.size());
                            for (const auto& cand : gm.alternateTextureCandidates) {
                                allFileNames.push_back(cand.filename);
                            }
                            result.ambiguousMatches.push_back(
                                {gm.name, resolved.value->matchedFilename, std::move(allFileNames), fdid});
                        } else {
                            result.fuzzyMatches.push_back({gm.name, resolved.value->matchedFilename, fdid});
                        }
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
                        // Same tier 1 -> tier 2 order as the primary
                        // baseColorTexture resolution above, now through the
                        // same Resolved<T>-wrapped helpers (AUDIT.md §1.1) --
                        // best-effort, same "supplementary metadata" tier as
                        // the rest of this loop, so a miss here is silently
                        // left blank rather than reported.
                        if (auto resolved = husk::sources::resolveLiteralTextureBytes(al.fileDataId, texturesDir,
                                                                                        texturesOutDir)) {
                            al.imagePng = std::move(*resolved.value);
                        } else if (!listfile.empty()) {
                            if (auto resolved2 = husk::sources::resolveListfileTextureBytes(
                                    al.fileDataId, listfile, listfileRoot, texturesOutDir)) {
                                al.imagePng = std::move(resolved2.value->bytes);
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
            // The full verbose chain -- kept as extras-only diagnostics
            // (gltf::Material::diagnosticName) for cross-referencing this
            // material back to its source .skin batch/texture index, now
            // that gm.name itself (below) prefers a cleaner, human-readable
            // identity when one resolves.
            gm.diagnosticName = gm.name.substr(batchPrefix.size());
            // Real display-name priority: a resolved ChrCustomizationOption/
            // Choice name (this material's own baseColorTextureFileDataId
            // cross-referenced a real customization choice) -> else the
            // texture's own semantic type name (m2::textureTypeName,
            // already human-readable, e.g. "skin"/"char_hair") -> else fall
            // back to the full diagnostic chain (the common case for
            // non-customization prop/weapon materials, which have no
            // textureType-name-worthy slot at all).
            if (!gm.customizationChoiceName.empty()) {
                gm.name = gm.customizationOptionName.empty()
                              ? gm.customizationChoiceName
                              : gm.customizationOptionName + ": " + gm.customizationChoiceName;
            } else if (const char* typeName = m2::textureTypeName(gm.textureType)) {
                gm.name = typeName;
            } else {
                gm.name = gm.diagnosticName;
            }
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
    // without being noisy. Read from `catalog` now (this model's pool state
    // is catalog-owned, not a local variable here) -- see
    // sources::Catalog::remainingTexturePoolSize's own doc comment.
    size_t remainingPoolFiles = catalog.remainingTexturePoolSize(modelPath);
    if (remainingPoolFiles > 1) {
        std::cout << "husk: note: " << remainingPoolFiles
                  << " texture file(s) in '" << texturesDir
                  << "' share this model's basename but husk can't tell which hardcoded texture "
                     "slot each belongs to -- none were embedded\n";
    }

    return result;
}

}  // namespace husk::commands
