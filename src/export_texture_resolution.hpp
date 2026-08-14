#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gltf.hpp"

// Texture-candidate resolution (embedded-filename/FileDataID/fuzzy-
// basename-pool/listfile matching, category classification, default-pick
// ordering) and the M2Track-to-glTF-curve resolvers for genuinely-animated
// tint/fade/texture-transform data -- split out of export_materials.cpp
// per TODO/CLEANUP_TODO.md's Item 1 (that file was the largest in `src/`,
// two genuinely separate concerns bundled into one translation unit: this
// one answers "which real bytes does a texture slot resolve to," the
// remaining buildMaterialsAndPrimitives in export_materials.cpp answers
// "how does one .skin batch become a glTF material/primitive," calling
// into these functions rather than duplicating them.
//
// Not part of the public husk::commands API surface (export_materials.hpp
// doesn't re-declare any of this) -- same "cross-file helper gets
// promoted out of an anonymous namespace, but stays internal to this
// feature area" pattern as gltf_mesh_internal.hpp/gltf_skeleton_internal.hpp
// (FILE_SPLIT_TODO.md Item 2's precedent).
namespace husk::commands {

// Reads a texture file's embeddable PNG bytes directly -- `.png` as-is,
// `.blp` decoded and PNG-re-encoded in memory (see src/blp.hpp). Never
// writes an intermediate file unless `texturesOutDir` is given
// (--textures-out), in which case a freshly-decoded `.blp` also gets
// mirrored there as a real `.png`. Returns nullopt if the file can't be
// opened or (for `.blp`) fails to decode.
std::optional<std::vector<uint8_t>> readTextureFileBytes(const std::filesystem::path& path,
                                                          const std::string& texturesDir,
                                                          const std::string& texturesOutDir);

// Tries `<stemPath>.png` first, falling back to `<stemPath>.blp` -- the
// same "PNG wins if both exist" priority scanFuzzyTexturePool's dedup
// uses, so a directory containing both an already-converted PNG and its
// source BLP never decodes the BLP redundantly.
std::optional<std::vector<uint8_t>> resolveTextureBytes(const std::filesystem::path& stemPath,
                                                          const std::string& texturesDir,
                                                          const std::string& texturesOutDir);

// WoW's M2BLEND_* blend modes (wowdev.wiki M2/Rendering#M2BLEND) collapsed
// to glTF's three-way alphaMode: 0 (OPAQUE) maps directly, 1 (ALPHA_KEY,
// alpha-tested) maps to MASK, and everything else -- 2 (a real alpha
// blend), plus the additive/multiply modes 3+ that glTF's core material
// model has no equivalent for -- maps to BLEND as the closest
// approximation.
gltf::Material::AlphaMode alphaModeForBlend(uint16_t blendMode);

// Resolves a genuinely-animated M2Color::color (or equivalent Vec3) track
// into real (seconds, rgb) keyframes -- one gltf::Material::AnimatedColorCurve
// per M2Sequence that has real inline data for this track, plus a
// synthetic global-sequence entry (sequenceIndex left at -1) when the
// track loops independently of any M2Sequence instead.
std::vector<gltf::Material::AnimatedColorCurve> resolveAnimatedColorCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount);

// Same shape as resolveAnimatedColorCurve, but for a genuinely-animated
// M2TextureTransform::rotation track (M2Track<C4Quaternion>, raw floats --
// see m2::TextureTransform::rotation's own doc comment for why this needs
// the raw-quat resolver, not the M2CompQuat-decompressing one).
std::vector<gltf::Material::AnimatedQuatCurve> resolveAnimatedRawQuatCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount);

// Same shape as resolveAnimatedColorCurve, but for a genuinely-animated
// M2Color::alpha or M2TextureWeight::weight track (both M2Track<fixed16>) --
// shared by both since only the source field differs.
std::vector<gltf::Material::AnimatedScalarCurve> resolveAnimatedFixed16Curve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount);

// Shared by scanFuzzyTexturePool (which files belong to this model's pool
// at all) and classifyCandidateCategory (parsing a category token out of
// one of them) -- one lowercasing, not two independently maintained
// copies.
std::string lowercaseModelBasename(const std::string& modelPath);

// Real community-listfile category token embedded in a candidate's own
// filename (e.g. "skin_color" out of "bloodelffemale_hd_skin_color_3500123.
// blp") -- this is metadata a human already attached to the file, not
// husk inferring anything about M2 internals. Returns "" for a bare
// "<modelBasename>_<FileDataID>" file (no category at all), or
// std::nullopt if `path` doesn't even start with the model's own
// basename.
std::optional<std::string> classifyCandidateCategory(const std::filesystem::path& path,
                                                       const std::string& modelBasenameLower);

// Parses the trailing FileDataID digit-run out of a same-basename fuzzy
// candidate's stem -- either a bare "<model>_<fdid>" file or a
// category-tagged "<model>_<category>_<fdid>" one. Returns std::nullopt
// when the stem doesn't end in a digit run at all.
//
// Used to cross-check a candidate against this model's *own* M2 texture
// array (`M2MaterialInputs::textureFileDataIds`, the TXID chunk) -- see
// buildMaterialsAndPrimitives' fuzzyTexturePool filtering for why a
// candidate whose own FileDataID is already a different, specifically-
// identified texture slot in this exact M2 is never a plausible guess for
// an unrelated hardcoded slot.
std::optional<uint32_t> fuzzyCandidateFileDataId(const std::filesystem::path& path,
                                                   const std::string& modelBasenameLower);

// The actual per-slot candidate set: every same-basename pool file whose
// category is *recognized* and compatible with `textureType`, falling
// back to whatever's left (bare or genuinely unrecognized tokens) only
// when that set is empty -- prefer real category matches, only fall back
// to unlabeled files when nothing labeled exists at all.
std::vector<std::filesystem::path> filterCandidatesForType(const std::vector<std::filesystem::path>& files,
                                                             uint32_t textureType,
                                                             const std::string& modelBasenameLower);

// Reads a PNG's real width/height straight out of its IHDR chunk without
// decoding any pixel data. `readTextureFileBytes` always hands back PNG
// bytes regardless of the source format, so this works uniformly for an
// original `.png` or a decoded `.blp` alike. Returns {0, 0} for anything
// too short to hold a real IHDR.
std::pair<uint32_t, uint32_t> pngDimensions(const std::vector<uint8_t>& png);

// Reorders `candidates` (in place, already filterCandidatesForType's
// output) so the preferred default-pick lands at front() -- see
// export_texture_resolution.cpp's own doc comment for the four real,
// measured/verified signals this uses, in order (glow-variant preference,
// decoded pixel area, skin_color-category preference). `byteCache` is
// shared across every ambiguous batch this export call processes (the
// same cache the caller uses for the actual embed step), not a fresh
// local one -- avoids re-decoding the same candidate once per batch on a
// model with many ambiguous slots sharing one pool.
void orderCandidatesForDefault(std::vector<std::filesystem::path>& candidates, const std::string& texturesDir,
                                const std::string& texturesOutDir, const std::string& modelBasenameLower,
                                std::map<std::filesystem::path, std::vector<uint8_t>>& byteCache,
                                bool preferGlow);

// The scanned, not-yet-claimed same-basename fuzzy candidate pool for one
// model export -- see scanFuzzyTexturePool's doc comment for how it's
// built, claimSoleFuzzyTextureCandidate's for how it's depleted. Shared
// and depleted across every batch in one buildMaterialsAndPrimitives call,
// never rescanned per batch (that would let every unresolved slot on a
// model wire in the exact same file).
struct FuzzyTexturePool {
    std::vector<std::filesystem::path> files;
};

// Hands out the pool's sole remaining entry compatible with `textureType`
// (removing it) only when exactly one is left -- the one case where
// "which slot does this belong to" isn't actually ambiguous. Claim-and-
// remove, not a fresh scan per batch: a real texture directory built for
// one character typically has *several* real files sharing that
// character's basename -- a fresh scan per batch would wire the *same*
// image into every unresolved slot at once.
std::optional<std::filesystem::path> claimSoleFuzzyTextureCandidate(FuzzyTexturePool& pool, uint32_t textureType,
                                                                      const std::string& modelBasenameLower);

// Scans `texturesDir` for every real (.png/.blp) file whose stem starts
// with `modelPath`'s own basename (case-insensitive) -- best-effort
// filename-based resolution for a texture slot, tried before any
// FileDataID-named lookup, since a real texture directory isn't always
// FileDataID-named. Falls back to a "_sdr" stand-in model's real (non-
// "_sdr") basename when the direct scan finds nothing -- see
// export_texture_resolution.cpp's own doc comment for why that specific
// fallback is real, confirmed-by-bytes WoW naming convention, not a
// guess. Returns an empty pool for an empty `texturesDir` or a model path
// with no basename at all.
FuzzyTexturePool scanFuzzyTexturePool(const std::string& texturesDir, const std::string& modelPath);

// Real content-equality signature for a fully-built gltf::Material,
// deliberately excluding `name` (unique per batch by construction,
// meaningless for identity) -- every *other* field that actually affects
// what the material looks like or carries as metadata.
// `buildMaterialsAndPrimitives`'s dedup loop uses this to decide "reuse an
// existing gltf::Material" vs. "this is genuinely a new one".
std::string materialDedupKey(const gltf::Material& gm);

}  // namespace husk::commands
