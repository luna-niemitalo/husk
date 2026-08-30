#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// The read interface every --listfile consumer goes through now, instead of
// a materialised std::unordered_map<uint32_t, std::string> -- see
// DESIGN.md's "Listfile index" section for why: measured, the 2.2M-entry
// map itself (allocating and hash-inserting 2.2M std::strings) was ~97% of
// --listfile's per-invocation cost, not the ~148MB CSV/cache read (~18ms,
// page-cached). ListfileIndex lets a lookup cost approach that read floor:
// MmapSortedListfileIndex/MmapHashListfileIndex (listfile_mmap_index.hpp)
// mmap a prebuilt on-disk cache and answer lookup() straight out of the
// mapped bytes, with zero per-process construction.
//
// Every std::string_view lookup()/forEach() hands back is a borrow into the
// ListfileIndex's own backing storage (a std::unordered_map's strings for
// MapListfileIndex, an mmap'd region for the mmap-backed implementations) --
// valid only for the ListfileIndex object's own lifetime, never longer. A
// caller that needs a value to outlive the index must copy it into a
// std::string/std::filesystem::path immediately, same as every current
// consumer already does at its point of use (sources/listfile_catalog.cpp).
namespace husk {

class ListfileIndex {
public:
    virtual ~ListfileIndex() = default;

    virtual bool empty() const = 0;
    virtual size_t size() const = 0;

    // nullopt when `fdid` has no row -- the same "primary path unavailable,
    // caller falls back" contract every existing listfile consumer already
    // expects from a std::unordered_map::find() miss.
    virtual std::optional<std::string_view> lookup(uint32_t fdid) const = 0;

    // Visits (fdid, path) rows until `visit` returns false or every row has
    // been seen. The two real uses today are sources::fileDataIdForPath's
    // reverse path -> fdid scan (listfile_catalog.cpp, stops at the first
    // match) and husk db2-build's full-table dump (cmd_db2_build.cpp,
    // always returns true to walk every row) -- neither is a hot
    // per-lookup path, but the early-exit return keeps a reverse scan's
    // existing "return on first match" behavior rather than forcing a full
    // O(size()) walk every time. An implementation is free to make an
    // unstopped walk O(backing storage size) rather than O(size()) (e.g. a
    // hash table walks every slot, not just occupied ones).
    virtual void forEach(const std::function<bool(uint32_t, std::string_view)>& visit) const = 0;
};

// Adapts an already-loaded std::unordered_map<uint32_t, std::string> (e.g.
// loadListfile()'s raw CSV parse, or a synthetic test fixture) to
// ListfileIndex with no copy -- holds a reference, so `map` must outlive
// this wrapper.
class MapListfileIndex final : public ListfileIndex {
public:
    explicit MapListfileIndex(const std::unordered_map<uint32_t, std::string>& map) : map_(map) {}

    bool empty() const override { return map_.empty(); }
    size_t size() const override { return map_.size(); }

    std::optional<std::string_view> lookup(uint32_t fdid) const override {
        auto it = map_.find(fdid);
        if (it == map_.end()) return std::nullopt;
        return std::string_view(it->second);
    }

    void forEach(const std::function<bool(uint32_t, std::string_view)>& visit) const override {
        for (const auto& [fdid, path] : map_) {
            if (!visit(fdid, path)) return;
        }
    }

private:
    const std::unordered_map<uint32_t, std::string>& map_;
};

// Same as MapListfileIndex, but owns the map by value instead of borrowing
// it -- for a caller (loadListfileCached's own error-fallback paths) that
// has a freshly-parsed map with nowhere else to keep it alive.
class OwningMapListfileIndex final : public ListfileIndex {
public:
    explicit OwningMapListfileIndex(std::unordered_map<uint32_t, std::string> map) : map_(std::move(map)) {}

    bool empty() const override { return map_.empty(); }
    size_t size() const override { return map_.size(); }

    std::optional<std::string_view> lookup(uint32_t fdid) const override {
        auto it = map_.find(fdid);
        if (it == map_.end()) return std::nullopt;
        return std::string_view(it->second);
    }

    void forEach(const std::function<bool(uint32_t, std::string_view)>& visit) const override {
        for (const auto& [fdid, path] : map_) {
            if (!visit(fdid, path)) return;
        }
    }

private:
    std::unordered_map<uint32_t, std::string> map_;
};

// The real "no --listfile given" state -- every listfile-consuming function
// already special-cases an empty map today; this gives that state a real
// object (usable as a default parameter value) instead of a
// default-constructed empty map at every call site.
class EmptyListfileIndex final : public ListfileIndex {
public:
    bool empty() const override { return true; }
    size_t size() const override { return 0; }
    std::optional<std::string_view> lookup(uint32_t) const override { return std::nullopt; }
    void forEach(const std::function<bool(uint32_t, std::string_view)>&) const override {}
};

}  // namespace husk
