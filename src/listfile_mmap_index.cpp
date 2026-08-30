#include "listfile_mmap_index.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace husk {

namespace detail {

MmapFile::MmapFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("husk: couldn't open listfile cache '" + path + "'");

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw std::runtime_error("husk: couldn't stat listfile cache '" + path + "'");
    }
    size_ = static_cast<size_t>(st.st_size);

    if (size_ == 0) {
        ::close(fd);
        throw std::runtime_error("husk: listfile cache '" + path + "' is empty");
    }

    void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);  // the mapping keeps its own reference; the fd itself isn't needed after mmap()
    if (mapped == MAP_FAILED) {
        throw std::runtime_error("husk: couldn't mmap listfile cache '" + path + "'");
    }
    data_ = static_cast<uint8_t*>(mapped);
}

MmapFile::~MmapFile() { unmap(); }

MmapFile::MmapFile(MmapFile&& other) noexcept : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

MmapFile& MmapFile::operator=(MmapFile&& other) noexcept {
    if (this != &other) {
        unmap();
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void MmapFile::unmap() {
    if (data_) ::munmap(data_, size_);
    data_ = nullptr;
    size_ = 0;
}

}  // namespace detail

namespace {

// Shared by both formats: magic + version + source identity, always at
// byte 0 -- read this fixed-size prefix first (no allocation, straight off
// the mapped bytes) before trusting anything format-specific that follows.
// Field order deliberately keeps every 8-byte field 8-aligned from a
// page-aligned mmap base with no compiler-inserted padding (verified by
// the static_asserts below), so a header can be reinterpret_cast straight
// onto the mapped bytes.
struct CommonHeader {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    uint64_t sourceSize;
    int64_t sourceMtimeTicks;
    uint32_t sourcePathLength;
    uint32_t entryCount;
    uint64_t blobSize;
};
static_assert(sizeof(CommonHeader) == 48);
static_assert(offsetof(CommonHeader, sourceSize) % 8 == 0);
static_assert(offsetof(CommonHeader, sourceMtimeTicks) % 8 == 0);
static_assert(offsetof(CommonHeader, blobSize) % 8 == 0);

constexpr char kSortedMagic[8] = {'H', 'U', 'S', 'K', 'L', 'F', 'A', '1'};  // 'A' -- sorted Array
constexpr char kHashMagic[8] = {'H', 'U', 'S', 'K', 'L', 'F', 'H', '1'};    // 'H' -- Hash table
constexpr uint32_t kFormatVersion = 1;

struct SortedHeader {
    CommonHeader common;
    uint64_t fdidsOffset;
    uint64_t cumOffsetsOffset;
    uint64_t blobOffset;
};
static_assert(sizeof(SortedHeader) == sizeof(CommonHeader) + 24);
static_assert(offsetof(SortedHeader, fdidsOffset) % 8 == 0);

struct HashHeader {
    CommonHeader common;
    uint64_t tableSize;
    uint64_t slotsOffset;
    uint64_t blobOffset;
};
static_assert(sizeof(HashHeader) == sizeof(CommonHeader) + 24);
static_assert(offsetof(HashHeader, tableSize) % 8 == 0);

uint64_t roundUp8(uint64_t n) { return (n + 7) & ~uint64_t(7); }

// std::bit_ceil is C++20; this project targets C++17 (CMakeLists.txt).
// Smallest power of 2 >= n, n >= 1.
uint64_t nextPow2(uint64_t n) {
    uint64_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Bound checks mirror listfile_cache.cpp's pre-existing generous ceilings
// (real listfile scale is ~2.2M rows / ~150MB) -- this is husk's own cache
// file, but a corrupt/truncated header (a racing writer's half-finished
// temp file, plain disk corruption) must not let a garbage entryCount or
// blobSize drive an out-of-bounds mmap read below.
constexpr uint64_t kMaxEntryCount = 50'000'000;
constexpr uint64_t kMaxBlobSize = 4ull * 1024 * 1024 * 1024;
constexpr uint32_t kMaxSourcePathLength = 1u << 20;

// Validates the common prefix against the caller's just-observed source
// identity (path/size/mtime) -- same "any mismatch means rebuild, never a
// silent partial read" contract listfile_cache.cpp's tryLoadCache already
// established. Returns false for anything from "too small to hold a
// header" to "right source, wrong format" to "stale/corrupt".
//
// `fullHeaderSize` is sizeof(SortedHeader)/sizeof(HashHeader) (the caller's
// own format-specific header, common prefix plus its own extra fields) --
// NOT sizeof(CommonHeader). build() writes `sourcePath`'s bytes
// immediately after the *whole* format header (see MmapSortedListfileIndex/
// MmapHashListfileIndex::build), so validating against the smaller
// CommonHeader size here would read the wrong bytes and misreport a
// well-formed cache as stale.
bool validateCommonHeader(const detail::MmapFile& mapping, const char magic[8], size_t fullHeaderSize,
                           const std::string& sourcePath, uint64_t sourceSize, int64_t sourceMtimeTicks,
                           const CommonHeader** outHeader) {
    if (mapping.size() < fullHeaderSize) return false;
    const auto* header = reinterpret_cast<const CommonHeader*>(mapping.data());
    if (std::memcmp(header->magic, magic, 8) != 0) return false;
    if (header->version != kFormatVersion) return false;
    if (header->sourceSize != sourceSize) return false;
    if (header->sourceMtimeTicks != sourceMtimeTicks) return false;
    if (header->sourcePathLength > kMaxSourcePathLength) return false;
    if (header->entryCount > kMaxEntryCount) return false;
    if (header->blobSize > kMaxBlobSize) return false;
    if (fullHeaderSize + header->sourcePathLength > mapping.size()) return false;
    std::string_view storedPath(reinterpret_cast<const char*>(mapping.data()) + fullHeaderSize,
                                 header->sourcePathLength);
    if (storedPath != sourcePath) return false;
    *outHeader = header;
    return true;
}

void writeAtomic(const std::string& path, const std::vector<uint8_t>& bytes) {
    auto tmpPath = path + ".tmp." + std::to_string(::getpid()) + "." +
                   std::to_string(reinterpret_cast<uintptr_t>(&bytes));
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("husk: note: couldn't write listfile cache to '" + tmpPath + "'");
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) throw std::runtime_error("husk: note: writing listfile cache to '" + tmpPath + "' failed partway");
    }
    // rename() within one filesystem is atomic -- a concurrent reader's
    // open() either sees the old complete file or the new one, never a
    // half-written one. See listfile_cache.cpp's writeCacheAtomic for the
    // full concurrency rationale this mirrors.
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        throw std::runtime_error("husk: note: couldn't install listfile cache at '" + path + "'");
    }
}

// Common sorted-entries prep both build() paths would otherwise duplicate:
// order by fdid ascending (needed for MmapSortedListfileIndex's binary
// search; MmapHashListfileIndex doesn't strictly need it, but building
// from the same sorted vector keeps both formats' blobs laid out in the
// identical, deterministic fdid order -- easier to eyeball/diff while
// developing, and free since the sort already has to happen for A).
std::vector<std::pair<uint32_t, const std::string*>> sortedEntries(
    const std::unordered_map<uint32_t, std::string>& entries) {
    std::vector<std::pair<uint32_t, const std::string*>> sorted;
    sorted.reserve(entries.size());
    for (const auto& kv : entries) sorted.emplace_back(kv.first, &kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return sorted;
}

}  // namespace

// --- MmapSortedListfileIndex --------------------------------------------

MmapSortedListfileIndex::MmapSortedListfileIndex(detail::MmapFile mapping) : mapping_(std::move(mapping)) {
    const auto* header = reinterpret_cast<const SortedHeader*>(mapping_.data());
    entryCount_ = header->common.entryCount;
    fdids_ = reinterpret_cast<const uint32_t*>(mapping_.data() + header->fdidsOffset);
    cumOffsets_ = reinterpret_cast<const uint64_t*>(mapping_.data() + header->cumOffsetsOffset);
    blob_ = reinterpret_cast<const char*>(mapping_.data() + header->blobOffset);
}

std::optional<std::string_view> MmapSortedListfileIndex::lookup(uint32_t fdid) const {
    const uint32_t* begin = fdids_;
    const uint32_t* end = fdids_ + entryCount_;
    const uint32_t* it = std::lower_bound(begin, end, fdid);
    if (it == end || *it != fdid) return std::nullopt;
    auto i = static_cast<size_t>(it - begin);
    uint64_t offset = cumOffsets_[i];
    uint64_t length = cumOffsets_[i + 1] - offset;
    return std::string_view(blob_ + offset, length);
}

void MmapSortedListfileIndex::forEach(const std::function<bool(uint32_t, std::string_view)>& visit) const {
    for (uint32_t i = 0; i < entryCount_; ++i) {
        uint64_t offset = cumOffsets_[i];
        uint64_t length = cumOffsets_[i + 1] - offset;
        if (!visit(fdids_[i], std::string_view(blob_ + offset, length))) return;
    }
}

bool MmapSortedListfileIndex::tryLoad(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                                       int64_t sourceMtimeTicks, std::unique_ptr<MmapSortedListfileIndex>& outIndex) {
    detail::MmapFile mapping;
    try {
        mapping = detail::MmapFile(path);
    } catch (const std::exception&) {
        return false;
    }
    const CommonHeader* common = nullptr;
    if (!validateCommonHeader(mapping, kSortedMagic, sizeof(SortedHeader), sourcePath, sourceSize, sourceMtimeTicks,
                               &common)) {
        return false;
    }
    if (mapping.size() < sizeof(SortedHeader)) return false;
    const auto* header = reinterpret_cast<const SortedHeader*>(mapping.data());
    // Bound the three variable sections against the real mapped file size
    // before trusting their stored offsets for any pointer arithmetic --
    // same "corrupt header must not drive an out-of-bounds read" discipline
    // as validateCommonHeader.
    uint64_t fdidsEnd = header->fdidsOffset + uint64_t(header->common.entryCount) * sizeof(uint32_t);
    uint64_t cumOffsetsEnd = header->cumOffsetsOffset + uint64_t(header->common.entryCount + 1) * sizeof(uint64_t);
    uint64_t blobEnd = header->blobOffset + header->common.blobSize;
    if (fdidsEnd > mapping.size() || cumOffsetsEnd > mapping.size() || blobEnd > mapping.size()) return false;
    if (header->fdidsOffset % 4 != 0 || header->cumOffsetsOffset % 8 != 0) return false;

    outIndex = std::make_unique<MmapSortedListfileIndex>(std::move(mapping));
    return true;
}

void MmapSortedListfileIndex::build(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                                     int64_t sourceMtimeTicks,
                                     const std::unordered_map<uint32_t, std::string>& entries) {
    auto sorted = sortedEntries(entries);
    auto entryCount = static_cast<uint32_t>(sorted.size());

    uint64_t headerSize = sizeof(SortedHeader);
    uint64_t afterPath = roundUp8(headerSize + sourcePath.size());
    uint64_t fdidsOffset = afterPath;
    uint64_t afterFdids = roundUp8(fdidsOffset + uint64_t(entryCount) * sizeof(uint32_t));
    uint64_t cumOffsetsOffset = afterFdids;
    uint64_t afterCumOffsets = cumOffsetsOffset + uint64_t(entryCount + 1) * sizeof(uint64_t);
    uint64_t blobOffset = afterCumOffsets;

    uint64_t blobSize = 0;
    for (const auto& kv : sorted) blobSize += kv.second->size();

    std::vector<uint8_t> out(blobOffset + blobSize, 0);

    SortedHeader header{};
    std::memcpy(header.common.magic, kSortedMagic, 8);
    header.common.version = kFormatVersion;
    header.common.sourceSize = sourceSize;
    header.common.sourceMtimeTicks = sourceMtimeTicks;
    header.common.sourcePathLength = static_cast<uint32_t>(sourcePath.size());
    header.common.entryCount = entryCount;
    header.common.blobSize = blobSize;
    header.fdidsOffset = fdidsOffset;
    header.cumOffsetsOffset = cumOffsetsOffset;
    header.blobOffset = blobOffset;
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + headerSize, sourcePath.data(), sourcePath.size());

    auto* fdidsOut = reinterpret_cast<uint32_t*>(out.data() + fdidsOffset);
    auto* cumOffsetsOut = reinterpret_cast<uint64_t*>(out.data() + cumOffsetsOffset);
    uint64_t runningOffset = 0;
    for (size_t i = 0; i < sorted.size(); ++i) {
        fdidsOut[i] = sorted[i].first;
        cumOffsetsOut[i] = runningOffset;
        std::memcpy(out.data() + blobOffset + runningOffset, sorted[i].second->data(), sorted[i].second->size());
        runningOffset += sorted[i].second->size();
    }
    cumOffsetsOut[sorted.size()] = runningOffset;

    writeAtomic(path, out);
}

// --- MmapHashListfileIndex ------------------------------------------------

MmapHashListfileIndex::MmapHashListfileIndex(detail::MmapFile mapping) : mapping_(std::move(mapping)) {
    const auto* header = reinterpret_cast<const HashHeader*>(mapping_.data());
    entryCount_ = header->common.entryCount;
    tableSize_ = header->tableSize;
    slots_ = reinterpret_cast<const Slot*>(mapping_.data() + header->slotsOffset);
    blob_ = reinterpret_cast<const char*>(mapping_.data() + header->blobOffset);
}

namespace {
uint64_t hashFdid(uint32_t fdid) {
    // A simple multiplicative (Fibonacci) hash: FileDataIDs are already
    // near-arbitrary 32-bit values (real-world IDs, not small sequential
    // counters clustering in one region), so this is sufficient to spread
    // them across the table without a heavier general-purpose hash.
    return (static_cast<uint64_t>(fdid) * 0x9E3779B97F4A7C15ull);
}
}  // namespace

std::optional<std::string_view> MmapHashListfileIndex::lookup(uint32_t fdid) const {
    if (tableSize_ == 0) return std::nullopt;
    uint64_t mask = tableSize_ - 1;
    uint64_t idx = hashFdid(fdid) & mask;
    for (uint64_t probes = 0; probes < tableSize_; ++probes) {
        const Slot& slot = slots_[idx];
        if (slot.fdid == 0) return std::nullopt;  // empty slot -- probe chain ends here
        if (slot.fdid == fdid) return std::string_view(blob_ + slot.offset, slot.length);
        idx = (idx + 1) & mask;
    }
    return std::nullopt;  // table is full with no match -- shouldn't happen at load factor 0.5, but terminate safely
}

void MmapHashListfileIndex::forEach(const std::function<bool(uint32_t, std::string_view)>& visit) const {
    for (uint64_t i = 0; i < tableSize_; ++i) {
        const Slot& slot = slots_[i];
        if (slot.fdid == 0) continue;
        if (!visit(slot.fdid, std::string_view(blob_ + slot.offset, slot.length))) return;
    }
}

bool MmapHashListfileIndex::tryLoad(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                                     int64_t sourceMtimeTicks, std::unique_ptr<MmapHashListfileIndex>& outIndex) {
    detail::MmapFile mapping;
    try {
        mapping = detail::MmapFile(path);
    } catch (const std::exception&) {
        return false;
    }
    const CommonHeader* common = nullptr;
    if (!validateCommonHeader(mapping, kHashMagic, sizeof(HashHeader), sourcePath, sourceSize, sourceMtimeTicks,
                               &common)) {
        return false;
    }
    if (mapping.size() < sizeof(HashHeader)) return false;
    const auto* header = reinterpret_cast<const HashHeader*>(mapping.data());
    if (header->tableSize == 0 || (header->tableSize & (header->tableSize - 1)) != 0) return false;  // must be pow2
    uint64_t slotsEnd = header->slotsOffset + header->tableSize * sizeof(MmapHashListfileIndex::Slot);
    uint64_t blobEnd = header->blobOffset + header->common.blobSize;
    if (slotsEnd > mapping.size() || blobEnd > mapping.size()) return false;
    if (header->slotsOffset % 8 != 0) return false;

    outIndex = std::make_unique<MmapHashListfileIndex>(std::move(mapping));
    return true;
}

void MmapHashListfileIndex::build(const std::string& path, const std::string& sourcePath, uint64_t sourceSize,
                                   int64_t sourceMtimeTicks, const std::unordered_map<uint32_t, std::string>& entries) {
    auto sorted = sortedEntries(entries);
    auto entryCount = static_cast<uint32_t>(sorted.size());

    // Load factor 0.5: next power of 2 at or above entryCount * 2, minimum
    // 1 so an empty listfile still produces a well-formed (if degenerate)
    // table rather than a zero-sized one lookup() would need to special-case.
    uint64_t minSlots = entryCount == 0 ? 1 : uint64_t(entryCount) * 2;
    uint64_t tableSize = nextPow2(minSlots);

    std::vector<Slot> slots(tableSize, Slot{0, 0, 0});
    std::vector<uint8_t> blobBytes;
    uint64_t blobSize = 0;
    for (const auto& kv : sorted) blobSize += kv.second->size();
    blobBytes.reserve(blobSize);

    uint64_t mask = tableSize - 1;
    uint64_t runningOffset = 0;
    for (const auto& kv : sorted) {
        uint64_t idx = hashFdid(kv.first) & mask;
        while (slots[idx].fdid != 0) idx = (idx + 1) & mask;  // linear probe to the next empty slot
        slots[idx] = Slot{kv.first, static_cast<uint32_t>(kv.second->size()), runningOffset};
        blobBytes.insert(blobBytes.end(), kv.second->begin(), kv.second->end());
        runningOffset += kv.second->size();
    }

    uint64_t headerSize = sizeof(HashHeader);
    uint64_t afterPath = roundUp8(headerSize + sourcePath.size());
    uint64_t slotsOffset = afterPath;
    uint64_t afterSlots = slotsOffset + tableSize * sizeof(Slot);  // Slot is 16 bytes, already 8-aligned
    uint64_t blobOffset = afterSlots;

    std::vector<uint8_t> out(blobOffset + blobSize, 0);

    HashHeader header{};
    std::memcpy(header.common.magic, kHashMagic, 8);
    header.common.version = kFormatVersion;
    header.common.sourceSize = sourceSize;
    header.common.sourceMtimeTicks = sourceMtimeTicks;
    header.common.sourcePathLength = static_cast<uint32_t>(sourcePath.size());
    header.common.entryCount = entryCount;
    header.common.blobSize = blobSize;
    header.tableSize = tableSize;
    header.slotsOffset = slotsOffset;
    header.blobOffset = blobOffset;
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + headerSize, sourcePath.data(), sourcePath.size());
    std::memcpy(out.data() + slotsOffset, slots.data(), slots.size() * sizeof(Slot));
    std::memcpy(out.data() + blobOffset, blobBytes.data(), blobBytes.size());

    writeAtomic(path, out);
}

}  // namespace husk
