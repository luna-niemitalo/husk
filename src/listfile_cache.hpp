#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

// A persistent, on-disk cache in front of listfile.hpp's loadListfile --
// same real data, same public contract (a bad `path` still throws
// std::runtime_error, a malformed line is still silently skipped), just
// without re-parsing ~148MB/2.2M rows of CSV on every invocation. See
// DESIGN.md's "Listfile cache" section for the full staleness/
// invalidation/concurrency design and why it's shaped this way.
namespace husk {

// $XDG_CACHE_HOME/husk (falling back to $HOME/.cache/husk, then a temp
// dir) -- mirrors the sibling tact-fetch project's TactFetchCacheDir()
// convention (~/dev/tact-fetch/src/cache_dir.h) so a user who already
// knows that project's layout recognizes this one. $HUSK_CACHE_DIR
// overrides both, for tests and for a caller who wants an isolated cache
// -- same "explicit env var beats the XDG default" shape husk_config.hpp
// already uses for $HUSK_CONFIG.
std::filesystem::path listfileCacheDir();

// Same result loadListfile(path) would produce, but backed by
// listfileCacheDir()'s on-disk cache. On a cache hit (a fresh-enough tag
// file, and the cache's own recorded source path/size/mtime still match
// `path`'s current stat), this skips the CSV parse entirely and rebuilds
// the map from a packed binary format instead. On a miss (cold, expired,
// or the source changed), this calls loadListfile(path) exactly as
// before, then writes a fresh cache for next time -- never slower than
// the uncached path, only ever faster.
std::unordered_map<uint32_t, std::string> loadListfileCached(const std::string& path);

}  // namespace husk
