#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "listfile_index.hpp"

// The two candidate zero-materialisation backings benchmarked for
// DESIGN.md's "Listfile index" section: mmap a prebuilt on-disk file and
// answer ListfileIndex::lookup() straight out of the mapped bytes -- no
// per-process heap allocation, no construction loop, proportional to
// nothing but the lookup itself.
//
// Both formats are POD and pointer-free (every reference into the mapped
// region is a byte offset from the mapping's own base, resolved with
// pointer arithmetic at lookup time -- never a stored pointer, which would
// be meaningless in a different process's address space or across a
// remap). Both are safe to place on tmpfs and mmap MAP_SHARED from several
// concurrent processes at once -- the kernel backs all of their mappings
// with the same physical pages either way (this is standard page-cache
// behavior for a file-backed mapping, true on a normal disk filesystem
// too; tmpfs's only difference is that its pages are never written back to
// a block device, not that sharing is somehow more or less real there --
// see DESIGN.md for the measured comparison).
//
// - MmapSortedListfileIndex ("option A"): the on-disk cache's own sorted
//   FileDataID array, binary-searched directly on the mapped memory.
// - MmapHashListfileIndex ("option B"): a prebuilt open-addressed hash
//   table, probed directly on the mapped memory.
//
// Neither type owns the CSV-parsing/staleness-checking policy around it --
// that lives in listfile_cache.hpp/.cpp, which builds these files from
// loadListfile()'s output and picks which one loadListfileCached() hands
// back.
namespace husk {

namespace detail {

// RAII mmap of a whole read-only file. Never copyable (the mapping is the
// single owner every returned std::string_view borrows from) -- move-only,
// matching every ListfileIndex implementation's own non-owning-view
// contract: the mapping must outlive every view taken from it, so it can
// never live in two places that might independently unmap it.
class MmapFile {
public:
    // The empty/unmapped state -- lets a caller default-construct a slot to
    // move a real mapping into after a try/catch (tryLoad's own pattern),
    // without needing an optional<MmapFile> wrapper. data()/size() are
    // null/0 until a real mapping is moved in.
    MmapFile() = default;

    // Throws std::runtime_error if `path` can't be opened, fstat'd, or
    // mapped -- a cache file this class is asked to open is expected to
    // exist and be well-formed by the time a caller gets here (the loader
    // that picks this path already checked it against a fresh header);
    // any failure at this point is a genuine I/O problem, not a normal
    // "cache miss" the caller has its own fallback for.
    explicit MmapFile(const std::string& path);
    ~MmapFile();

    MmapFile(const MmapFile&) = delete;
    MmapFile& operator=(const MmapFile&) = delete;
    MmapFile(MmapFile&& other) noexcept;
    MmapFile& operator=(MmapFile&& other) noexcept;

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    void unmap();

    uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

}  // namespace detail

class MmapSortedListfileIndex final : public ListfileIndex {
public:
    explicit MmapSortedListfileIndex(detail::MmapFile mapping);

    bool empty() const override { return entryCount_ == 0; }
    size_t size() const override { return entryCount_; }
    std::optional<std::string_view> lookup(uint32_t fdid) const override;
    void forEach(const std::function<bool(uint32_t, std::string_view)>& visit) const override;

    // Validates `path`'s header against the observed source identity
    // without loading anything else -- returns false (and the mapping is
    // dropped) on any magic/version/source mismatch, the same "any
    // mismatch means rebuild" contract listfile_cache.cpp's staleness gate
    // already documents. On success, `outIndex` is left holding the
    // mapping (zero further construction: the returned object's
    // constructor only computes a handful of pointers from the header's
    // own stored byte offsets).
    static bool tryLoad(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                         int64_t sourceMtimeTicks, std::unique_ptr<MmapSortedListfileIndex>& outIndex);

    // Builds this format from a plain (fdid -> path) map (loadListfile()'s
    // own output) and atomically installs it at `path`. Sorts by fdid once
    // (needed for binary search), computes cumulative blob offsets once --
    // this is the format's one and only O(N) construction step, paid once
    // per cache rebuild, never on a warm load.
    static void build(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                       int64_t sourceMtimeTicks, const std::unordered_map<uint32_t, std::string>& entries);

private:
    detail::MmapFile mapping_;
    const uint32_t* fdids_ = nullptr;      // [entryCount_], sorted ascending
    const uint64_t* cumOffsets_ = nullptr;  // [entryCount_ + 1], blob byte offsets
    const char* blob_ = nullptr;
    uint32_t entryCount_ = 0;
};

class MmapHashListfileIndex final : public ListfileIndex {
public:
    explicit MmapHashListfileIndex(detail::MmapFile mapping);

    bool empty() const override { return entryCount_ == 0; }
    size_t size() const override { return entryCount_; }
    std::optional<std::string_view> lookup(uint32_t fdid) const override;
    void forEach(const std::function<bool(uint32_t, std::string_view)>& visit) const override;

    static bool tryLoad(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                         int64_t sourceMtimeTicks, std::unique_ptr<MmapHashListfileIndex>& outIndex);

    // Builds an open-addressed table (linear probing, load factor 0.5,
    // power-of-2 size) in memory once, then writes it out in that exact
    // layout -- a warm load mmaps this file and probes it as-is, no
    // rehashing, no rebuild.
    static void build(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                       int64_t sourceMtimeTicks, const std::unordered_map<uint32_t, std::string>& entries);

private:
    struct Slot {
        uint32_t fdid;    // 0 == empty (loadListfile never emits a real fdid == 0 entry)
        uint32_t length;
        uint64_t offset;  // into blob_
    };
    static_assert(sizeof(Slot) == 16);

    detail::MmapFile mapping_;
    const Slot* slots_ = nullptr;
    const char* blob_ = nullptr;
    uint64_t tableSize_ = 0;  // power of 2
    uint32_t entryCount_ = 0;
};

}  // namespace husk
