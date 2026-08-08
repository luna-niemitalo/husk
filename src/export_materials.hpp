#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gltf.hpp"
#include "m2.hpp"
#include "skin.hpp"

// Batch -> material/primitive resolution (textures, tint/fade, UV
// transforms, multi-texture-layer metadata) -- split out of cmd_export.cpp
// per FILE_SPLIT_TODO.md's Item 1.
namespace husk::commands {

struct BuiltMaterials {
    std::vector<gltf::Material> materials;
    std::vector<gltf::Primitive> primitives;
    // Every distinct Submesh::skinSectionId (the "geoset ID", wowdev.wiki
    // M2/.skin#Submeshes) actually referenced by a batch that became a
    // primitive, sorted ascending -- see skin.hpp's Submesh doc comment.
    // husk doesn't filter geosets yet: every submesh in the .skin file gets
    // exported, unconditionally, even when several are mutually-exclusive
    // character-customization options. Surfaced here so exportGlb can at
    // least warn loudly when more than one distinct geoset actually ended
    // up in the output.
    std::vector<uint16_t> distinctSkinSectionIds;
    // Number of batches whose textureCount is > 1 -- per wowdev.wiki
    // M2/.skin#Texture_units, textureCount (1-4) means the real per-unit
    // texture/UV-mapping/transparency lookups are `textureCount` consecutive
    // entries starting at textureComboIndex, used for real, visually
    // meaningful multi-texture effects -- husk only ever resolves the first
    // one, silently dropping any additional layer. Surfaced so exportGlb
    // can at least say so.
    size_t multiTextureBatchCount = 0;
    // Number of batches whose color (M2Color) or transparency-fade
    // (M2TextureWeight) reference is real per-sequence or global-sequence
    // keyframe animation, not a single constant value -- e.g. an eye-glow
    // or enchant-glow pulse. Core glTF has no way to *play back* an
    // animated material property, so there's no real translation to build
    // -- but the actual keyframe data is resolved and attached as inert
    // `tint_animation`/`fade_animation` material extras.
    size_t animatedTintOrFadeBatchCount = 0;
    // Number of batches whose textureTransformComboIndex resolved to a real
    // M2TextureTransform (UV scroll/rotate/scale animation) -- see
    // gltf::Material::TextureTransform's doc comment: the constant, planar-
    // rotation case gets a real KHR_texture_transform on the render
    // (gltf_mesh.cpp's textureTransformToKhr); every case's raw values are
    // also always exposed as inert extras regardless.
    size_t textureTransformBatchCount = 0;

    // One entry per batch whose embedded texture came from the basename-
    // fuzzy pool (claimSoleFuzzyTextureCandidate), not one of the two real
    // deterministic matches (an exact FileDataID or an exact embedded-
    // filename match). "Sole remaining candidate" is a real, bounded
    // heuristic -- never multiple ambiguous candidates guessed between --
    // but it's still a guess, not a verification. Surfaced here so
    // exportGlb can warn loudly, per match.
    struct FuzzyMatch {
        std::string materialName;
        std::string fileName;
        // The batch's own resolved FileDataID, or 0 if this was a
        // genuinely hardcoded slot (M2Texture::type != 0) with no
        // FileDataID to cross-reference at all -- not just "not checked."
        uint32_t fileDataId = 0;
    };
    std::vector<FuzzyMatch> fuzzyMatches;

    // One entry per batch whose hardcoded/customization-driven slot had 2+
    // same-basename candidates (a genuinely ambiguous case, never a guess
    // between fewer than 2) -- unlike FuzzyMatch above, husk no longer
    // silently embeds nothing here: every candidate is embedded as a real
    // alternate image/texture (gltf::Material::alternateTextureCandidates),
    // with one arbitrary (alphabetically first) candidate also wired in as
    // the actual baseColorTexture so the export still renders as something
    // by default. Surfaced here so exportGlb can warn loudly which default
    // was picked and name every real alternative sitting in the file.
    struct AmbiguousMatch {
        std::string materialName;
        std::string defaultFileName;
        std::vector<std::string> allFileNames;
        uint32_t fileDataId = 0;  // same meaning as FuzzyMatch::fileDataId
    };
    std::vector<AmbiguousMatch> ambiguousMatches;
};

// Everything buildMaterialsAndPrimitives needs out of the M2 itself (as
// opposed to the .skin file's own submeshes/batches) to resolve one batch
// into a real glTF material -- bundled since the M2 side alone is seven
// distinct arrays/optionals by this point, one call site, no real
// abstraction cost.
struct M2MaterialInputs {
    std::vector<m2::Material> materials;
    std::vector<m2::Texture> textures;
    std::vector<uint16_t> textureCombos;
    // Pre-Cataclysm only (wowdev.wiki M2/.skin#geosetIndex: "Still present
    // but unused in Cataclysm") -- empty in almost every modern file; when
    // empty, batch.textureCoordComboIndex is never dereferenced at all and
    // every material just uses UV set 0.
    std::vector<uint16_t> textureCoordCombos;
    std::vector<m2::Color> colors;
    std::vector<m2::TextureWeight> textureWeights;
    std::vector<uint16_t> textureWeightCombos;
    std::vector<m2::TextureTransform> textureTransforms;
    std::vector<uint16_t> textureTransformCombos;
    std::optional<std::vector<uint32_t>> textureFileDataIds;
    // For resolving a genuinely-animated M2Color/M2TextureWeight curve
    // (colorAnimated/alphaAnimated/weightAnimated) into real keyframe data.
    // `blob` is the same MD20 bytes `colors`/`textureWeights` above were
    // parsed from (M2Track offsets are relative to it, not the .skin file);
    // `sequenceCount` is the model's own header.sequences.count.
    const std::vector<uint8_t>* blob = nullptr;
    size_t sequenceCount = 0;
};

// Resolves the .skin file's batches (M2's actual material/texture
// linkage, see src/skin.hpp's Batch doc comment) into one glTF material +
// primitive per batch: batch -> submesh (a slice of `triangleIndices`) ->
// material (blend mode/render flags -> alphaMode/doubleSided, plus a
// static color tint/alpha and texture-weight fade multiplied into
// baseColorFactor) -> texture (via the textureCombos lookup table -> M2's
// textures array -> optionally a FileDataID, via TXID, resolved to a real
// PNG if `texturesDir` is given, and which of the mesh's two UV sets to
// sample it with, via textureCoordCombos). If the .skin has no batches at
// all, falls back to stage-1-through-4 behavior: one primitive covering
// every triangle, no material.
// `texturesOutDir`, when non-empty (--textures-out), also writes each
// freshly-decoded `.blp` texture to disk as a real `.png`, mirroring its
// location relative to `texturesDir` -- never the source directory itself,
// and never for a texture that was already a `.png` (nothing was decoded
// for those). Purely a convenience copy: embedding itself always happens
// in-memory regardless of whether this is set.
BuiltMaterials buildMaterialsAndPrimitives(const std::vector<uint32_t>& triangleIndices,
                                            const std::vector<skin::Submesh>& submeshes,
                                            const std::vector<skin::Batch>& batches,
                                            const M2MaterialInputs& m2,
                                            const std::string& texturesDir,
                                            const std::string& modelPath,
                                            const std::string& texturesOutDir = "");

// A directory scan finding zero matches looks identical to "the directory
// doesn't exist"/"can't be read" unless this distinguishes them. Shared by
// buildMaterialsAndPrimitives's fuzzy-texture-pool scan and
// findSameBasenameSkins's (export_skin_resolution.cpp) same-basename .skin
// scan -- both need the same missing-vs-unreadable-vs-wrong-type
// diagnostic.
std::filesystem::directory_iterator scanDirOrWarn(const std::string& dir, const char* purpose);

}  // namespace husk::commands
