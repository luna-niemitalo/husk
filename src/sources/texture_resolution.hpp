#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Texture-candidate resolution behind sources::Catalog: reading and
// decoding texture files, and the same-basename fuzzy pool (category
// classification, tag matching, default-pick ordering).
namespace husk::sources {

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
//
// Superseded, for the texture types filterCandidatesByTextureTag has a
// real tag mapping for, by that function -- see its own doc comment for
// why (this one's category classification requires a same-basename
// prefix filterCandidatesByTextureTag doesn't). Still the only filter for
// every other type -- this function itself is unchanged; it stays the
// live path for object_skin, the weapon/environment/monster replaceable
// types, and anything else filterCandidatesByTextureTag returns nullopt
// for.
std::vector<std::filesystem::path> filterCandidatesForType(const std::vector<std::filesystem::path>& files,
                                                             uint32_t textureType,
                                                             const std::string& modelBasenameLower);

// Real, corpus-wide texture-filename tokens (kTextureTagVocabulary,
// export_texture_resolution.cpp) present anywhere in `stemLower` as a
// substring, not a delimited token -- real ground-truth filenames run
// tags together with no separator at all (e.g. the real
// "bloodelffemalenakedtorsoskin00_105_hd.blp", "naked"+"torso"+"skin",
// zero underscores between them), so a split('_') tokenizer would miss
// the majority of the corpus this exists to recognize. A file's tag set
// is a *description*, not a classification -- a candidate can and
// usually does carry several.
std::set<std::string> extractTextureTags(const std::string& stemLower);

// True when `stemLower` carries a real, delimited "hd" token (split on
// '_', exact match -- same idiom as export_texture_resolution.cpp's own
// isGlowVariantCandidate, deliberately *not* a substring test the way
// extractTextureTags is, since "hd" as a bare substring collides with
// real English words far more readily than the longer vocabulary tokens
// do). Used for both a candidate's own stem and a model's own basename --
// see filterCandidatesByTextureTag's doc comment for the hard partition
// this drives.
bool filenameCarriesHdToken(const std::string& stemLower);

// The real per-slot tag-conjunction query for `textureType`, when one is
// established -- nullopt for every type this table has no confident
// real-corpus tag for (the weapon/environment/monster replaceable types,
// object_skin -- see export_texture_resolution.cpp's own doc comment on
// textureTypeTagClauses for why object_skin specifically can never get
// one), in which case the caller should fall back to
// filterCandidatesForType exactly as before this function existed.
//
// `files` is expected to already be `_hd`-partitioned by the caller
// (scanFuzzyTexturePoolForBasename does this at scan time, per model
// variant) -- this function itself does no `_hd` filtering, since by the
// time a candidate reaches here it's already guaranteed to match the
// model's own `_hd`-ness.
std::optional<std::vector<std::filesystem::path>> filterCandidatesByTextureTag(
    const std::vector<std::filesystem::path>& files, uint32_t textureType);

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

    // The subset of `files` admitted by the original startswith(modelBasename)
    // rule alone -- computed once at scan time (scanFuzzyTexturePoolForBasename),
    // independent of the later tag-vocabulary widening that also admits a file
    // carrying a real kTextureTagVocabulary token with no basename relation at
    // all. A texture type with no real tag clause in textureTypeTagClauses
    // (object_skin, every non-character replaceable type) has no way to tell a
    // widened-pool file apart from noise -- filterCandidatesForType's own
    // category classification requires the basename prefix to even parse a
    // category out of a candidate, so a same-basename requirement was always
    // implicit in what that path can do with a file. This set lets a caller
    // restrict back down to that original admission for exactly those types,
    // without a second directory scan or a second stored file list to keep in
    // sync with pool depletion -- membership is checked against the live
    // (already-depleted) `files`, never mutated on its own.
    std::set<std::filesystem::path> narrowAdmitted;
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

}  // namespace husk::sources
