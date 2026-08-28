#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "resolved.hpp"

// First real tier of `AUDIT.md` §1.1's texture-resolution consolidation
// wired through `Resolved<T>` (`resolved.hpp`) -- not the full three-tier
// merge `RESOURCE_CATALOG.md` describes (that needs the real
// `sources::Catalog` object's tier-order/ranking design, still not built),
// just this one tier's existing, unchanged behavior reporting *why* it hit
// or missed instead of a bare `optional`. See `REFACTOR_LOG.md`'s
// 2026-08-28 "first real tier through Resolved<T>" entry for why this is
// being done one tier at a time.
namespace husk::sources {

// Tier 1 of `RESOURCE_CATALOG.md`'s "Normative tier order": a literal
// `<texturesDir>/<FileDataID>.png`, falling back to `.blp` (decoded via
// husk::blp), PNG winning when both exist -- delegates to
// `husk::commands::resolveTextureBytes` for the actual read/decode, this
// wrapper only adds the tier/reason reporting `AUDIT.md` §1.1's three
// implementations don't uniformly have today (I4's "expected vs. actual on
// failure").
Resolved<std::vector<uint8_t>> resolveLiteralTextureBytes(uint32_t fdid, const std::string& texturesDir,
                                                            const std::string& texturesOutDir);

// Bytes plus the display name tier 2 derives from the listfile's own real
// content path (`stem.filename()`) -- bundled together since both come out
// of the same resolved path, and export_materials.cpp's call site needs
// both (`gm.baseColorImagePng`/`gm.baseColorImageName`).
struct ListfileTextureResult {
    std::vector<uint8_t> bytes;
    std::string imageName;
};

// Tier 2 of `RESOURCE_CATALOG.md`'s "Normative tier order": `<listfileRoot>/
// <real content path>` for `fdid`, read via the same `.png`-then-`.blp`
// resolution tier 1 uses. Delegates to `pathForFileDataId`
// (`listfile_catalog.hpp`, `AUDIT.md` §1.2's already-consolidated forward
// lookup) for the path, then `husk::commands::resolveTextureBytes` for the
// bytes -- this wrapper adds only the `Resolved<T>` provenance, no new
// lookup logic.
Resolved<ListfileTextureResult> resolveListfileTextureBytes(
    uint32_t fdid, const std::unordered_map<uint32_t, std::string>& listfile, const std::string& listfileRoot,
    const std::string& texturesOutDir);

// Bytes plus the two display names export_materials.cpp's fuzzy-pool call
// site derives from the claimed candidate's own path.
struct FuzzyPoolTextureResult {
    std::vector<uint8_t> bytes;
    std::string imageName;       // claimedPath.stem()
    std::string matchedFilename;  // claimedPath.filename()
};

// The read half of tier 3 ("fuzzy same-basename pool") ONLY -- deliberately
// NOT the claim-and-remove-from-the-shared-pool half
// (`claimSoleFuzzyTextureCandidate`) or the genuine-ambiguity fan-out
// (`filterCandidatesForType`/`orderCandidatesForDefault`,
// `AlternateTextureCandidate`). `RESOURCE_CATALOG.md`'s Open Questions
// section explains why: tier 3's real orchestration has three outcomes
// (a sole candidate read successfully; zero candidates at all; 2+
// candidates, a *different* success shape, not a miss), and the caller's
// own control flow -- claim first, only fall through to the ambiguity scan
// when nothing was claimed at all, never when a claimed candidate merely
// failed to decode -- can't be preserved by folding all three into one
// `Resolved<T>`. This wrapper takes an already-claimed path (the caller
// still owns `claimSoleFuzzyTextureCandidate` and its own branch on
// whether anything was claimed) and only reports provenance for the
// deterministic "read what was claimed" step -- same shape as tiers 1/2,
// scoped to the one sub-step that actually has a clean two-way outcome.
Resolved<FuzzyPoolTextureResult> resolveClaimedFuzzyPoolTextureBytes(const std::filesystem::path& claimedPath,
                                                                       const std::string& texturesDir,
                                                                       const std::string& texturesOutDir);

}  // namespace husk::sources
