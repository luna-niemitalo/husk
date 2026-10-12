#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "canon_surface.hpp"
#include "surface_db2.hpp"

// husk::surfaceinput: TerrainType rows -> a document's surface table
// (canon_surface.hpp). Shared by the terrain and WMO input stages.
namespace husk::surfaceinput {

// A document's surface types, each TerrainType row once, in first-use
// order. `types` nullptr (no --db2-dir): entries carry the row id only.
class SurfaceTable {
public:
    explicit SurfaceTable(const surfacedb::Data* types) : types_(types) {}
    uint32_t add(uint32_t terrainType);
    std::vector<canon::SurfaceType> take() { return std::move(entries_); }

private:
    const surfacedb::Data* types_;
    std::vector<canon::SurfaceType> entries_;
    std::unordered_map<uint32_t, uint32_t> index_;
};

}  // namespace husk::surfaceinput
