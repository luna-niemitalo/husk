#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// A liquid surface a model owns (WMO MLIQ), in the model's own space.
namespace husk::canon {

// A height grid of vertsX * vertsY vertices; vertex (x, y) sits at
// (corner.x + x * tileSize, corner.y + y * tileSize, heights[y * vertsX + x]).
// Arrays are row-major, x fastest. Tiles are (vertsX - 1) * (vertsY - 1),
// tile (x, y) spanning vertices (x..x+1, y..y+1).
struct LiquidGrid {
    uint32_t part = 0;  // mesh part (WMO group) the liquid belongs to
    Identity material;  // RecordIndex into Model::materials
    // Db2Row LiquidType, from the source's own rules (WMO.md MLIQ), or
    // None when those rules don't decide it; sourceLiquidType is the raw
    // MOGP.groupLiquid either way.
    Ref liquidType;
    uint32_t sourceLiquidType = 0;
    Vec3 corner;
    float tileSize = 0;
    uint32_t vertsX = 0;
    uint32_t vertsY = 0;
    // Absolute model-space Z. A vertex no existing tile touches may hold
    // any value (real ones fall outside their group's bounds).
    std::vector<float> heights;
    // Raw per-vertex bytes ahead of the height: flow data for water, int16
    // UV * 256 for magma; which applies depends on the liquid type.
    std::vector<std::array<uint8_t, 4>> vertexData;
    std::vector<uint8_t> tileExists;  // 0/1 per tile
    std::vector<uint8_t> tileFlags;   // raw source tile byte (WMO SMOLTile)
};

}  // namespace husk::canon
