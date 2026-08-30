#include "listfile_cache.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>

#include "listfile.hpp"
#include "listfile_mmap_index.hpp"

namespace husk {

namespace {

// "when we are hammering it, it stays cached ... it only stays alive for
// the single session it's actively working on" (Luna's own framing) --
// a fixed idle-timeout tag file, not tied to any particular caller's
// notion of a "session". Refreshed on every use (see touchTag), so a
// corpus scan that never stops calling husk never sees this expire.
constexpr auto kTagFreshness = std::chrono::minutes(10);

const char* kSortedCacheFileName = "listfile.sorted.bin";
const char* kHashCacheFileName = "listfile.hash.bin";
const char* kTagFileName = "listfile.tag";

enum class Backend { Sorted, Hash };

Backend selectedBackend() {
    const char* override = std::getenv("HUSK_LISTFILE_BACKEND");
    if (override && std::string(override) == "hash") return Backend::Hash;
    // "sorted" (mmap'd binary search) is the default: measured, both
    // backends sit at the no-listfile floor and within noise of each other,
    // so this is not a speed choice -- sorted wins on being 41% smaller on
    // disk (154MB vs 261MB; open addressing needs load-factor slack) and on
    // the simpler failure mode. DESIGN.md's "Listfile index" section has the
    // full table. `hash` stays reachable so that comparison stays
    // reproducible instead of becoming folklore.
    return Backend::Sorted;
}

bool isTagFresh(const std::filesystem::path& tagPath) {
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(tagPath, ec);
    if (ec) return false;  // no tag at all -- cold
    auto age = std::filesystem::file_time_type::clock::now() - mtime;
    return age >= std::chrono::seconds(0) && age < kTagFreshness;
}

// Refreshes the tag file's mtime to "now", creating it first if this is
// the first use. Deliberately not fatal on failure (e.g. a read-only
// cache dir) -- the cache is a pure optimization; the caller already has
// a correct in-memory or mmap'd result to return either way.
void touchTag(const std::filesystem::path& cacheDir, const std::filesystem::path& tagPath) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    {
        std::ofstream f(tagPath, std::ios::binary | std::ios::app);
    }
    std::filesystem::last_write_time(tagPath, std::filesystem::file_time_type::clock::now(), ec);
}

}  // namespace

std::filesystem::path listfileCacheDir() {
    const char* override = std::getenv("HUSK_CACHE_DIR");
    if (override && *override) return std::filesystem::path(override);

    const char* xdgCache = std::getenv("XDG_CACHE_HOME");
    if (xdgCache && *xdgCache) return std::filesystem::path(xdgCache) / "husk";

    const char* home = std::getenv("HOME");
    if (home && *home) return std::filesystem::path(home) / ".cache" / "husk";

    // No HOME, no XDG_CACHE_HOME -- fall back to a temp dir rather than
    // fail outright, same last-resort tact-fetch's own CacheDir() takes.
    return std::filesystem::temp_directory_path() / "husk";
}

std::unique_ptr<ListfileIndex> loadListfileCached(const std::string& path) {
    std::error_code ec;
    uint64_t sourceSize = std::filesystem::file_size(path, ec);
    std::filesystem::file_time_type sourceMtime;
    if (!ec) sourceMtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        // Can't stat the source at all -- let loadListfile produce the
        // real "couldn't open --listfile" error itself (single source of
        // truth for that message) rather than duplicating it here. The
        // caller (loadListfile) throws, so this never returns in that case.
        auto raw = loadListfile(path);
        return std::make_unique<OwningMapListfileIndex>(std::move(raw));
    }
    int64_t sourceMtimeTicks = sourceMtime.time_since_epoch().count();

    auto cacheDir = listfileCacheDir();
    Backend backend = selectedBackend();
    auto cacheFileName = backend == Backend::Hash ? kHashCacheFileName : kSortedCacheFileName;
    auto cachePath = cacheDir / cacheFileName;
    auto tagPath = cacheDir / kTagFileName;

    // Staleness gate (DESIGN.md): a cache older than kTagFreshness is
    // never even considered for reuse, regardless of whether the source
    // file itself is unchanged -- this is the deliberate "session
    // boundary". Source-identity checking (path/size/mtime) only matters
    // within that window, inside tryLoad.
    if (isTagFresh(tagPath)) {
        if (backend == Backend::Hash) {
            std::unique_ptr<MmapHashListfileIndex> index;
            if (MmapHashListfileIndex::tryLoad(cachePath.string(), path, sourceSize, sourceMtimeTicks, index)) {
                touchTag(cacheDir, tagPath);
                return index;
            }
        } else {
            std::unique_ptr<MmapSortedListfileIndex> index;
            if (MmapSortedListfileIndex::tryLoad(cachePath.string(), path, sourceSize, sourceMtimeTicks, index)) {
                touchTag(cacheDir, tagPath);
                return index;
            }
        }
    }

    auto fresh = loadListfile(path);
    std::error_code mkEc;
    std::filesystem::create_directories(cacheDir, mkEc);
    try {
        if (backend == Backend::Hash) {
            MmapHashListfileIndex::build(cachePath.string(), path, sourceSize, sourceMtimeTicks, fresh);
        } else {
            MmapSortedListfileIndex::build(cachePath.string(), path, sourceSize, sourceMtimeTicks, fresh);
        }
    } catch (const std::exception&) {
        // Same "cache write failure is never fatal" policy as before --
        // the caller already has a correct in-memory result to hand back.
        return std::make_unique<OwningMapListfileIndex>(std::move(fresh));
    }
    touchTag(cacheDir, tagPath);

    if (backend == Backend::Hash) {
        std::unique_ptr<MmapHashListfileIndex> index;
        if (MmapHashListfileIndex::tryLoad(cachePath.string(), path, sourceSize, sourceMtimeTicks, index)) {
            return index;
        }
    } else {
        std::unique_ptr<MmapSortedListfileIndex> index;
        if (MmapSortedListfileIndex::tryLoad(cachePath.string(), path, sourceSize, sourceMtimeTicks, index)) {
            return index;
        }
    }
    // The file we just wrote failed to re-load -- shouldn't happen, but
    // fall back to the in-memory result rather than losing the parse.
    return std::make_unique<OwningMapListfileIndex>(std::move(fresh));
}

}  // namespace husk
