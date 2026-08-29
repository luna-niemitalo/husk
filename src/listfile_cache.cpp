#include "listfile_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

#include <unistd.h>  // getpid() -- see writeCacheAtomic's temp-file naming

#include "listfile.hpp"

namespace husk {

namespace {

// "HUSKLFC1" -- HUSK ListFile Cache, format version 1. Bumping kVersion
// (not the magic bytes) is enough to invalidate every existing cache on
// husk upgrade: tryLoadCache rejects anything but an exact version match,
// same as a source mismatch, so an old-format cache just gets silently
// rebuilt rather than misread.
constexpr char kMagic[8] = {'H', 'U', 'S', 'K', 'L', 'F', 'C', '1'};
constexpr uint32_t kVersion = 1;

const char* kCacheFileName = "listfile.bin";
const char* kTagFileName = "listfile.tag";

// "when we are hammering it, it stays cached ... it only stays alive for
// the single session it's actively working on" (Luna's own framing) --
// a fixed idle-timeout tag file, not tied to any particular caller's
// notion of a "session". Refreshed on every use (see touchTag), so a
// corpus scan that never stops calling husk never sees this expire.
constexpr auto kTagFreshness = std::chrono::minutes(10);

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
// a correct in-memory result to return either way.
void touchTag(const std::filesystem::path& cacheDir, const std::filesystem::path& tagPath) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    {
        std::ofstream f(tagPath, std::ios::binary | std::ios::app);
    }
    std::filesystem::last_write_time(tagPath, std::filesystem::file_time_type::clock::now(), ec);
}

// Reads exactly `size` bytes into `dst`, reporting a short read as failure
// rather than silently leaving `dst` partially written -- every call site
// below already treats a `false` return as "this cache file is invalid,
// rebuild" rather than trying to recover a partial value from it.
bool readField(FILE* f, void* dst, size_t size) { return std::fread(dst, 1, size, f) == size; }

// Loads and validates the on-disk cache against the source identity the
// caller just observed (path/size/mtime) -- returns std::nullopt for
// *any* mismatch or malformed byte, whether that's "no cache yet", "wrong
// --listfile", "the CSV changed since this cache was built", or "disk
// corruption/a half-written temp file from a racing writer" (see
// writeCacheAtomic's own comment on the concurrency this must tolerate).
// The caller's response to nullopt is always the same: fall back to a
// real loadListfile() parse -- so this function never needs to distinguish
// *why* the cache didn't load, only whether it did.
std::optional<std::unordered_map<uint32_t, std::string>> tryLoadCache(
    const std::filesystem::path& cachePath, const std::string& sourcePath, uint64_t sourceSize,
    int64_t sourceMtimeTicks) {
    FILE* f = std::fopen(cachePath.c_str(), "rb");
    if (!f) return std::nullopt;

    char magic[8];
    if (!readField(f, magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        std::fclose(f);
        return std::nullopt;
    }

    uint32_t version = 0;
    uint64_t hdrSourceSize = 0;
    int64_t hdrSourceMtimeTicks = 0;
    uint32_t sourcePathLength = 0;
    uint64_t entryCount = 0;
    uint64_t blobSize = 0;
    if (!readField(f, &version, sizeof(version)) || version != kVersion ||
        !readField(f, &hdrSourceSize, sizeof(hdrSourceSize)) ||
        !readField(f, &hdrSourceMtimeTicks, sizeof(hdrSourceMtimeTicks)) ||
        !readField(f, &sourcePathLength, sizeof(sourcePathLength)) ||
        !readField(f, &entryCount, sizeof(entryCount)) || !readField(f, &blobSize, sizeof(blobSize))) {
        std::fclose(f);
        return std::nullopt;
    }

    // Bound checks before any allocation driven by these numbers -- this
    // is husk's own cache file, but it still crosses a boundary (another
    // process's half-written temp file, a stale format, plain disk
    // corruption), so a corrupt header must not be able to drive an
    // unbounded allocation. Real listfile scale is ~2.2M rows / ~150MB;
    // these ceilings are generous multiples of that, not tight fits.
    constexpr uint32_t kMaxSourcePathLength = 1u << 20;             // 1MB
    constexpr uint64_t kMaxEntryCount = 50'000'000;                 // ~20x real row count
    constexpr uint64_t kMaxBlobSize = 4ull * 1024 * 1024 * 1024;    // 4GB
    if (sourcePathLength > kMaxSourcePathLength || entryCount > kMaxEntryCount || blobSize > kMaxBlobSize) {
        std::fclose(f);
        return std::nullopt;
    }

    std::string hdrSourcePath(sourcePathLength, '\0');
    if (sourcePathLength > 0 && !readField(f, hdrSourcePath.data(), sourcePathLength)) {
        std::fclose(f);
        return std::nullopt;
    }

    // Source invalidation (DESIGN.md): path, size, and mtime must all
    // still match what the caller just observed on disk. A mismatch here
    // means either --listfile now points somewhere else, or the same file
    // was modified in place -- either way, a rebuild, never a warning
    // (the caller can't tell "cache miss" from "source changed" and
    // doesn't need to; both take the same fallback path).
    if (hdrSourceSize != sourceSize || hdrSourceMtimeTicks != sourceMtimeTicks || hdrSourcePath != sourcePath) {
        std::fclose(f);
        return std::nullopt;
    }

    std::vector<uint32_t> fdids(entryCount);
    std::vector<uint32_t> lengths(entryCount);
    if (entryCount > 0 && (!readField(f, fdids.data(), entryCount * sizeof(uint32_t)) ||
                            !readField(f, lengths.data(), entryCount * sizeof(uint32_t)))) {
        std::fclose(f);
        return std::nullopt;
    }

    std::vector<char> blob(blobSize);
    if (blobSize > 0 && !readField(f, blob.data(), blobSize)) {
        std::fclose(f);
        return std::nullopt;
    }
    std::fclose(f);

    // fdids[] is written sorted ascending (writeCacheAtomic) -- kept sorted
    // on disk so the packed cache is directly binary-searchable in
    // principle (this project's own listfile.hpp doc comment notes husk
    // has no persistent process to amortize a hash-map build across
    // multiple invocations; a future caller that doesn't need the full
    // unordered_map -- e.g. a handful of point lookups -- could
    // std::lower_bound directly on this array and skip the loop below
    // entirely). Every current caller still wants the full
    // fdid -> path map, so this loop builds exactly that from the sorted
    // array + blob, no parsing (no memchr scans, no digit conversion,
    // no CRLF handling) -- just prefix-summed slices of already-resident
    // bytes.
    std::unordered_map<uint32_t, std::string> result;
    result.reserve(entryCount);
    uint64_t offset = 0;
    for (uint64_t i = 0; i < entryCount; ++i) {
        uint32_t len = lengths[i];
        if (offset + len > blobSize) return std::nullopt;  // truncated/corrupt -- rebuild, don't read OOB
        result.emplace(fdids[i], std::string(blob.data() + static_cast<size_t>(offset), len));
        offset += len;
    }
    return result;
}

// Atomic, concurrency-safe cache write: several `husk` processes (a
// corpus-scan's parallel workers) can hit a cold or just-expired cache at
// the same moment and each independently decide to rebuild. That's fine
// -- rebuilding is redundant work, not a correctness problem, since every
// rebuilder is parsing the same source file. What must never happen is a
// reader observing a *partially written* cache file. So: write to a
// per-process, per-call temp file in the same directory (never cachePath
// directly), then rename() it over cachePath. rename() within one
// filesystem is atomic -- a concurrent reader's open() either lands
// before the rename (sees the old complete file, via its own already-open
// fd even if this rename happens mid-read) or after (sees the new
// complete file); it can never observe a half-written one. If two writers
// race, both temp files are independently valid (same source, so
// equivalent content module unimportant ordering); whichever rename()
// lands last simply wins -- accepted by design, not a bug to fix with
// locking.
void writeCacheAtomic(const std::filesystem::path& cacheDir, const std::filesystem::path& cachePath,
                       const std::string& sourcePath, uint64_t sourceSize, int64_t sourceMtimeTicks,
                       const std::unordered_map<uint32_t, std::string>& entries) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);

    std::vector<std::pair<uint32_t, const std::string*>> sorted;
    sorted.reserve(entries.size());
    for (const auto& kv : entries) sorted.emplace_back(kv.first, &kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    uint64_t blobSize = 0;
    for (const auto& kv : sorted) blobSize += kv.second->size();

    auto tmpPath = cacheDir / (std::string(kCacheFileName) + ".tmp." + std::to_string(::getpid()) + "." +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) {
        std::cerr << "husk: note: couldn't write listfile cache to '" << tmpPath.string()
                  << "' -- continuing without a persistent cache this run\n";
        return;
    }

    bool ok = true;
    auto write = [&](const void* data, size_t size) {
        if (ok && std::fwrite(data, 1, size, f) != size) ok = false;
    };

    write(kMagic, sizeof(kMagic));
    write(&kVersion, sizeof(kVersion));
    write(&sourceSize, sizeof(sourceSize));
    write(&sourceMtimeTicks, sizeof(sourceMtimeTicks));
    auto sourcePathLength = static_cast<uint32_t>(sourcePath.size());
    write(&sourcePathLength, sizeof(sourcePathLength));
    uint64_t entryCount = sorted.size();
    write(&entryCount, sizeof(entryCount));
    write(&blobSize, sizeof(blobSize));
    write(sourcePath.data(), sourcePath.size());
    for (const auto& kv : sorted) write(&kv.first, sizeof(kv.first));
    for (const auto& kv : sorted) {
        auto len = static_cast<uint32_t>(kv.second->size());
        write(&len, sizeof(len));
    }
    for (const auto& kv : sorted) write(kv.second->data(), kv.second->size());

    std::fclose(f);
    if (!ok) {
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
        std::cerr << "husk: note: writing listfile cache failed partway -- continuing without a persistent "
                     "cache this run\n";
        return;
    }

    std::filesystem::rename(tmpPath, cachePath, ec);
    if (ec) {
        std::cerr << "husk: note: couldn't install listfile cache at '" << cachePath.string()
                  << "': " << ec.message() << " -- continuing without a persistent cache this run\n";
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
    }
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

std::unordered_map<uint32_t, std::string> loadListfileCached(const std::string& path) {
    std::error_code ec;
    uint64_t sourceSize = std::filesystem::file_size(path, ec);
    std::filesystem::file_time_type sourceMtime;
    if (!ec) sourceMtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        // Can't stat the source at all -- let loadListfile produce the
        // real "couldn't open --listfile" error itself (single source of
        // truth for that message) rather than duplicating it here.
        return loadListfile(path);
    }
    int64_t sourceMtimeTicks = sourceMtime.time_since_epoch().count();

    auto cacheDir = listfileCacheDir();
    auto cachePath = cacheDir / kCacheFileName;
    auto tagPath = cacheDir / kTagFileName;

    // Staleness gate (DESIGN.md): a cache older than kTagFreshness is
    // never even considered for reuse, regardless of whether the source
    // file itself is unchanged -- this is the deliberate "session
    // boundary" (Luna's framing: cached while actively hammering it,
    // reparsed once idle). Source-identity checking (path/size/mtime)
    // only matters within that window, inside tryLoadCache.
    if (isTagFresh(tagPath)) {
        auto cached = tryLoadCache(cachePath, path, sourceSize, sourceMtimeTicks);
        if (cached) {
            touchTag(cacheDir, tagPath);
            return std::move(*cached);
        }
    }

    auto fresh = loadListfile(path);
    writeCacheAtomic(cacheDir, cachePath, path, sourceSize, sourceMtimeTicks, fresh);
    touchTag(cacheDir, tagPath);
    return fresh;
}

}  // namespace husk
