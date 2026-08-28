#pragma once

#include <cstdint>
#include <string>
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

}  // namespace husk::sources
