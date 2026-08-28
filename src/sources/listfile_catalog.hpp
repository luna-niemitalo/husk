#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

// First slice of REFACTOR/RESOURCE_CATALOG.md's stage-2 FileDataID<->path
// surface (`fileDataIdForPath`/`modelPath` in that document's own naming).
// Only the reverse direction (path -> FileDataID) lives here so far --
// REFACTOR/AUDIT.md #1.2 names four *forward* FileDataID -> path
// implementations too (export_materials.cpp's texture-tier lookup,
// exportGearAuxItemModels, resolveObjectSkinTextureFromKb's knowledge-base
// path plus its listfile-map injection), each with different result shapes
// (a texture stem vs. a full existence-checked model path) -- consolidating
// those needs the real sources::Catalog object RESOURCE_CATALOG.md
// describes, not a superficial merge, so they're deliberately untouched
// here. This is the one existing reverse-lookup implementation, moved
// verbatim (no logic change) so it's independently testable and has a real
// home under src/sources/ instead of living as a file-private static in
// export_extras.cpp.
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

}  // namespace husk::sources
