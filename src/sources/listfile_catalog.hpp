#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../listfile_index.hpp"

// Two slices of REFACTOR/RESOURCE_CATALOG.md's stage-2 FileDataID<->path
// surface (`fileDataIdForPath`/`modelPath` in that document's own naming).
// REFACTOR/AUDIT.md #1.2 names three more forward-direction sites beyond
// what's consolidated here: resolveObjectSkinTextureFromKb's own
// knowledge-base path (a genuinely different backing store -- SQLite, not
// the listfile map -- so not the same duplication) and its listfile-map
// injection (superseded by Catalog::registerPathOverride -- see
// sources::Catalog's own doc comment), and the deeper policy question of
// ranking/caching/a real `Resolved<T>` result type with provenance (I6) --
// those need the real `sources::Catalog` object this document describes,
// not a superficial merge, so they stay untouched here. What both
// `fileDataIdForPath` and `pathForFileDataId` below share is narrower and
// safe to consolidate now: the literal "look this FileDataID/path up in an
// already-loaded listfile" step, moved verbatim (no behavior change) out
// of each of its original call sites so it has one real home under
// src/sources/ instead of being retyped at each one.
//
// All three take `const husk::ListfileIndex&` (listfile_index.hpp) rather
// than a raw std::unordered_map -- see DESIGN.md's "Listfile index"
// section for why: an mmap-backed ListfileIndex answers a lookup with zero
// per-process construction, where materialising a 2.2M-row map cost ~97%
// of --listfile's entire per-invocation runtime.
namespace husk::sources {

// Reverse lookup against an already-loaded ListfileIndex: finds
// `modelPath`'s own real FileDataID by matching its path relative to
// `listfileRoot` against the index's own paths, case-insensitively. A
// linear scan (via ListfileIndex::forEach), not an index -- not worth a
// second, reversed lookup structure for an occasional single lookup.
// Returns nullopt when `listfile`/`listfileRoot` is empty, `modelPath`
// isn't under `listfileRoot`, or no row matches -- all "primary path
// unavailable," not errors; callers that have a filename-based fallback
// should use it when this comes back empty.
std::optional<uint32_t> fileDataIdForPath(const husk::ListfileIndex& listfile, const std::string& modelPath,
                                           const std::string& listfileRoot);

// Forward lookup: `listfileRoot / listfile[fdid]`, or nullopt when
// `listfile` is empty or `fdid` has no row. Deliberately does NOT also
// require `listfileRoot` non-empty -- one of the two original call sites
// never checked that either (relying on an empty root acting as an
// identity join), so requiring it here would be a real behavior change,
// not a verbatim move; the other call site already checks it before ever
// reaching this function. Also does NOT check the resulting path's
// existence, and does NOT strip its extension -- both original call sites
// (export_materials.cpp's texture-tier lookup, exportGearAuxItemModels)
// did different caller-specific things with the raw joined path
// afterward.
std::optional<std::filesystem::path> pathForFileDataId(const husk::ListfileIndex& listfile,
                                                         const std::string& listfileRoot, uint32_t fdid);

// The real --listfile content name for `fdid`, for human-readable display
// (Blender image datablock/material names, --slim-textures' written
// filenames) -- `listfile[fdid]`'s own stem, no root join needed (a display
// name, not a path to read). Returns nullopt when `listfile` is empty or
// `fdid` has no row -- same "primary path unavailable" convention as the
// other two functions here.
std::optional<std::string> contentNameForFileDataId(const husk::ListfileIndex& listfile, uint32_t fdid);

// A path as an in-game file reference stores it (ADT MMDX/MWMO/MTEX name
// tables: backslashes, mixed case, and the pre-M2 `.mdx`/`.mdl` model
// extensions the client itself rewrites to `.m2`), in the listfile's own
// spelling: lowercase, forward slashes, `.m2`.
std::string listfileSpelling(const std::string& gamePath);

// Batch reverse lookup for game-file paths (see `listfileSpelling`): one
// ListfileIndex::forEach pass for all of `gamePaths`, rather than one
// linear scan each like `fileDataIdForPath`. Keyed by the caller's original
// spelling; a path with no listfile row is simply absent from the result.
std::unordered_map<std::string, uint32_t> fileDataIdsForGamePaths(const husk::ListfileIndex& listfile,
                                                                  const std::vector<std::string>& gamePaths);

}  // namespace husk::sources
