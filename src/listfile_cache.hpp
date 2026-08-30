#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "listfile_index.hpp"

// A persistent, on-disk cache in front of listfile.hpp's loadListfile,
// handed back as a ListfileIndex (listfile_index.hpp) instead of a
// materialised std::unordered_map<uint32_t, std::string> -- see
// DESIGN.md's "Listfile index" section for the full staleness/
// invalidation/concurrency design (mostly unchanged from the original
// map-based cache this replaced) and the measured case for why: a warm
// load now mmaps a prebuilt file and answers lookups straight out of the
// mapped bytes, with no per-process construction loop at all.
namespace husk {

// $XDG_CACHE_HOME/husk (falling back to $HOME/.cache/husk, then a temp
// dir) -- mirrors the sibling tact-fetch project's TactFetchCacheDir()
// convention (~/dev/tact-fetch/src/cache_dir.h) so a user who already
// knows that project's layout recognizes this one. $HUSK_CACHE_DIR
// overrides both, for tests and for a caller who wants an isolated cache
// -- same "explicit env var beats the XDG default" shape husk_config.hpp
// already uses for $HUSK_CONFIG. Also the knob DESIGN.md's tmpfs
// evaluation used to point the cache at /dev/shm for comparison -- see
// that section for the measured result.
std::filesystem::path listfileCacheDir();

// Same result loadListfile(path) would produce (byte-identical path
// strings for every FileDataID), but backed by listfileCacheDir()'s
// on-disk cache and returned as a ListfileIndex rather than a map. On a
// warm hit (a fresh-enough tag file, and the cache's own recorded source
// path/size/mtime still match `path`'s current stat), this mmaps the
// prebuilt cache file and returns an index over it directly -- no CSV
// parse, no per-process map construction. On a miss (cold, expired, or
// the source changed), this calls loadListfile(path) exactly as before,
// writes a fresh cache, then mmaps that -- never slower than the
// uncached path, only ever faster.
//
// Backend selection: $HUSK_LISTFILE_BACKEND selects which on-disk format
// gets built/read ("sorted" -- mmap'd sorted-array binary search, or
// "hash" -- mmap'd open-addressed hash table; both are ListfileIndex
// implementations in listfile_mmap_index.hpp, both benchmarked in
// DESIGN.md's "Listfile index" section). Defaults to "sorted", the
// section's own recommendation -- see that section for the numbers behind
// the choice and why $HUSK_LISTFILE_BACKEND is kept rather than deleting
// the loser outright.
std::unique_ptr<ListfileIndex> loadListfileCached(const std::string& path);

}  // namespace husk
