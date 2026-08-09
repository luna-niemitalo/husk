#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// A local community-listfile.csv-style FileDataID -> real-path snapshot
// (github.com/wowdev/wow-listfile). Optional, local-only, user-supplied --
// husk never fetches, generates, or requires one; this is the same "already
// on disk, never live CASC" tier DESIGN.md's Non-goals section already
// carves out for a local WoWDBDefs checkout (`--dbd-dir`). Used purely as a
// last-resort FileDataID -> real-name lookup for `husk export --listfile`,
// when the exact "<FileDataID>.{blp,png}" convention already tried against
// --textures has no match.
namespace husk {

// Loads "<FileDataID>;<real relative path>" per line (the format the
// wowdev/wow-listfile project's default release uses -- lowercase,
// forward-slash paths). Malformed lines (no ';', non-numeric ID) are
// skipped, not fatal: this is already a best-effort fallback tier, and a
// multi-million-line community CSV occasionally has stray rows that
// shouldn't abort the whole load. Throws std::runtime_error if `path`
// itself can't be opened -- unlike a malformed *line*, a bad --listfile
// path is a direct user mistake worth failing loudly on, not silently
// no-op'ing.
std::unordered_map<uint32_t, std::string> loadListfile(const std::string& path);

}  // namespace husk
