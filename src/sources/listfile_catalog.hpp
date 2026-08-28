#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

// Two slices of REFACTOR/RESOURCE_CATALOG.md's stage-2 FileDataID<->path
// surface (`fileDataIdForPath`/`modelPath` in that document's own naming).
// REFACTOR/AUDIT.md #1.2 names three more forward-direction sites beyond
// what's consolidated here: resolveObjectSkinTextureFromKb's own
// knowledge-base path (a genuinely different backing store -- SQLite, not
// the listfile map -- so not the same duplication) and its listfile-map
// injection (`cmd_export.cpp`'s `listfile.emplace(...)` after a KB hit),
// and the deeper policy question of ranking/caching/a real `Resolved<T>`
// result type with provenance (I6) -- those need the real `sources::Catalog`
// object this document describes, not a superficial merge, so they stay
// untouched here. What both `fileDataIdForPath` and `pathForFileDataId`
// below share is narrower and safe to consolidate now: the literal
// "look this FileDataID/path up in an already-loaded listfile map, joined
// against listfileRoot" step, moved verbatim (no behavior change) out of
// each of its 3 original call sites so it has one real home under
// src/sources/ instead of being retyped at each one.
namespace husk::sources {

// Reverse lookup against an already-loaded --listfile map (FileDataID ->
// real path): finds `modelPath`'s own real FileDataID by matching its path
// relative to `listfileRoot` against the listfile's own paths,
// case-insensitively. A linear scan, not an index -- not worth the memory
// of a second, reversed copy of a multi-million-row community listfile for
// an occasional single lookup. Returns nullopt when `listfile`/
// `listfileRoot` is empty, `modelPath` isn't under `listfileRoot`, or no
// listfile row matches -- all "primary path unavailable," not errors;
// callers that have a filename-based fallback should use it when this
// comes back empty.
std::optional<uint32_t> fileDataIdForPath(const std::unordered_map<uint32_t, std::string>& listfile,
                                           const std::string& modelPath, const std::string& listfileRoot);

// Forward lookup: `listfileRoot / listfile[fdid]`, or nullopt when
// `listfile` is empty or `fdid` has no listfile row. Deliberately does NOT
// also require `listfileRoot` non-empty -- one of the two original call
// sites never checked that either (relying on an empty root acting as an
// identity join), so requiring it here would be a real behavior change,
// not a verbatim move; the other call site already checks it before ever
// reaching this function. Also does NOT check the resulting path's
// existence, and does NOT strip its extension -- both original call sites
// (export_materials.cpp's
// texture-tier lookup, exportGearAuxItemModels) did different
// caller-specific things with the raw joined path afterward (extension
// stripping for further fuzzy-extension matching in one, an explicit
// existence check with its own diagnostic message in the other), and this
// helper only replaces the "look it up" step both duplicated identically,
// not that caller-specific handling.
std::optional<std::filesystem::path> pathForFileDataId(const std::unordered_map<uint32_t, std::string>& listfile,
                                                         const std::string& listfileRoot, uint32_t fdid);

// The real --listfile content name for `fdid`, for human-readable display
// (Blender image datablock/material names, --slim-textures' written
// filenames) -- `listfile[fdid]`'s own stem, no root join needed (a display
// name, not a path to read). Verbatim move of the identical
// `listfile.find(fdid)` + `.stem().string()` step two call sites
// duplicated (`export_materials.cpp`'s `gm.realContentName`,
// `export_extras.cpp`'s `cm.contentName` -- `AUDIT.md` §1.2's "fifth site"
// entry's own follow-up). Returns nullopt when `listfile` is empty or
// `fdid` has no row -- same "primary path unavailable" convention as the
// other two functions here.
std::optional<std::string> contentNameForFileDataId(const std::unordered_map<uint32_t, std::string>& listfile,
                                                      uint32_t fdid);

}  // namespace husk::sources
