#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

// REFACTOR/RESOURCE_CATALOG.md's `Resolved<T>` -- the return shape every
// `sources::Catalog` surface (`textureBytes`, `modelPath`,
// `fileDataIdForPath`, `sidecar`, ...) is meant to use once that object
// exists (RESOURCE_CATALOG.md: "not a bare optional... carries HOW the
// answer was reached").
//
// This file is pure scaffolding, landed ahead of the Catalog object itself
// for the same reason src/sources/db2_cache.hpp landed ahead of it
// (REFACTOR_LOG.md's 2026-08-28 "Stage 1/2 prep" entry): it carries no
// resolution-policy risk, so it doesn't need the "resolution ledger diff on
// real fixtures" gate the real §1.1/§1.2 consolidation is waiting on
// (REFACTOR_LOG.md's "Stage 1/2 follow-up" entry). Nothing in this codebase
// constructs a Resolved<T> yet -- that starts once a real tier (§1.1's
// texture resolution, or the remaining two §1.2 forward-lookup sites) is
// migrated to report its own provenance through this type instead of a bare
// optional/bool.
namespace husk::sources {

// Which of RESOURCE_CATALOG.md's texture tiers (or a sidecar/FileDataID
// equivalent) produced an answer -- or, for a miss, which tier was tried
// last before giving up. Deliberately texture-resolution-shaped for now
// (matching the tier order RESOURCE_CATALOG.md's "Normative tier order"
// section already settles) rather than a fully generic enum husk doesn't
// have a second real consumer for yet; extend this list, don't genericize
// it, when `sidecar()`/`db2()` need their own provenance kinds.
enum class ResolutionTier {
    Literal,                // <texturesDir>/<FileDataID>.{png,blp}
    Listfile,                // <listfileRoot>/<real content path>
    ParentDirectorySameBasename,  // one directory level up, same-basename pool
    FuzzySameBasenamePool,   // same-basename pool, claim-and-remove
    KnowledgeBase,           // resolveObjectSkinTextureFromKb's SQLite lookup
    Miss,                    // no tier produced an answer
};

// Human-readable tier name for diagnostics/`describe()` output
// (RESOURCE_CATALOG.md's I4 ledger) -- not used for control flow.
constexpr std::string_view tierName(ResolutionTier tier) {
    switch (tier) {
        case ResolutionTier::Literal:
            return "literal";
        case ResolutionTier::Listfile:
            return "listfile";
        case ResolutionTier::ParentDirectorySameBasename:
            return "parent-directory-same-basename";
        case ResolutionTier::FuzzySameBasenamePool:
            return "fuzzy-same-basename-pool";
        case ResolutionTier::KnowledgeBase:
            return "knowledge-base";
        case ResolutionTier::Miss:
            return "miss";
    }
    return "miss";
}

// A resolution outcome carrying provenance alongside (or instead of) the
// value -- RESOURCE_CATALOG.md: "which tier fired, which directory, which
// fallback". `reason` is free text for the specific detail that varies per
// tier (a directory path tried, a fallback name, an expected-vs-actual pair
// on miss per FOREIGN_DATA.md §2) -- not parsed by anything, only surfaced.
template <typename T>
struct Resolved {
    std::optional<T> value;
    ResolutionTier tier = ResolutionTier::Miss;
    std::string reason;

    bool found() const { return value.has_value(); }
    explicit operator bool() const { return found(); }

    static Resolved<T> hit(T v, ResolutionTier t, std::string reason = {}) {
        return Resolved<T>{std::optional<T>(std::move(v)), t, std::move(reason)};
    }

    static Resolved<T> miss(ResolutionTier attemptedTier, std::string reason) {
        return Resolved<T>{std::nullopt, attemptedTier, std::move(reason)};
    }
};

}  // namespace husk::sources
