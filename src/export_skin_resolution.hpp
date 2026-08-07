#pragma once

#include <string>
#include <utility>
#include <vector>

#include "m2.hpp"

// Resolves --skin/--skin-dir/--lod ('auto', a same-basename numbered scan,
// or an explicit --lod selection) into concrete .skin file paths -- split
// out of cmd_export.cpp per FILE_SPLIT_TODO.md's Item 1.
namespace husk::commands {

// Shared by every resolveAutoSkinPaths mode below: husk's own non-goal (no
// CASC/listfile access) means a model with no SFID chunk at all has no
// FileDataIDs to auto-select from, 'all'/--lod alike.
const std::vector<uint32_t>& requireSkinFileDataIds(const m2::Header& header,
                                                      const std::string& modelPath);

// Resolves the literal "auto" .skin path via the M2's own SFID chunk,
// honoring an optional --lod selection (`lodArg`, "" if not given). "":
// SFID entry 0, "the main skin aka lod0" (wowdev.wiki M2#SFID), the
// highest-detail LOD. "<n>": SFID entry n instead (0-based). "all": every
// entry, so husk export can emit one glTF node per LOD tier in a single
// .glb (see exportGlb) instead of just one. Each result pairs the entry's
// own index (for node naming) with its resolved local path. husk doesn't
// resolve any of these FileDataIDs to a WoW/CASC path itself -- this only
// ever looks for `<skinDir>/<FileDataID>.skin` on the local filesystem,
// the same convention `--textures` already uses for PNGs.
std::vector<std::pair<std::string, std::string>> resolveAutoSkinPaths(const m2::Header& header,
                                                                        const std::string& skinDir,
                                                                        const std::string& modelPath,
                                                                        const std::string& lodArg);

// Scans `modelPath`'s own directory for files named exactly
// `<model-basename><digits>.skin` (e.g. "bloodelffemale00.skin" for
// "bloodelffemale.m2") -- the naming convention a real casc-tool-style
// extraction actually produces, as opposed to `resolveAutoSkinPaths`'s
// FileDataID-renamed-directory convention. Returns every match found,
// sorted by that numeric suffix ascending -- 0 is always "the main skin
// aka lod0", the highest-detail LOD. Empty if `modelPath`'s directory
// doesn't exist or has no match.
//
// A digit-suffix match of *any* length is ambiguous when one model's
// basename is itself a numeric-suffix prefix of another model's basename
// in the same directory. WoW's own convention is always exactly 2 digits
// (`00`-`0N`), so a 2-digit suffix match is preferred whenever at least one
// exists for a basename; 1-digit/3+-digit matches are treated as a
// fallback only when no 2-digit match exists at all.
std::vector<std::pair<int, std::string>> findSameBasenameSkins(const std::string& modelPath);

// Resolves `--skin auto` for the common case of no explicit --lod: folds
// what used to be two independent code paths (an omitted .skin positional
// -> findSameBasenameSkins only; the literal word "auto" ->
// resolveAutoSkinPaths only) into the single 'auto' state DESIGN.md
// decided on. Order matters: the SFID-declared FileDataID (the model's own
// self-description) is tried first, in `skinDir` (unless `skinDirNone`,
// which skips this stage entirely); the same-basename numbered scan next
// to the model is the fallback, tried only when the SFID stage didn't
// resolve to a file that actually exists.
std::vector<std::pair<std::string, std::string>> resolveSkin(const m2::Header& header,
                                                               const std::string& modelPath,
                                                               const std::string& skinDir,
                                                               bool skinDirNone);

}  // namespace husk::commands
