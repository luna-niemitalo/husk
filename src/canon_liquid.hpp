#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_material.hpp"  // TextureRef
#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// A liquid surface a model owns (WMO MLIQ), in the model's own space, and
// the liquid types and objects a document's liquids name.
namespace husk::canon {

// One LiquidType texture slot: its frames in animation order. A frame of
// FileDataID 0 is a texture the client generates (sourceKind 0..2).
struct LiquidTextureSlot {
    int32_t sourceKind = -1;  // LiquidTypeXTexture.Type, raw: -1 a file
    std::vector<TextureRef> frames;
};

// A LiquidType a document's liquids name. Without the liquid DB2s only
// `ref` is known; `flags` is set exactly when the row was read.
struct LiquidType {
    Ref ref;  // Db2Row LiquidType, named from LiquidType.Name
    std::optional<uint32_t> flags;
    Ref material;  // Db2Row LiquidMaterial, None when the row wasn't read
    // LiquidMaterial.LVF and Flags; nullopt when that row is absent (12.1.0
    // has 5 material rows, while its types name 11 materials).
    std::optional<uint32_t> materialVertexFormat;
    std::optional<uint32_t> materialFlags;
    std::vector<LiquidTextureSlot> textures;
};

// An MH2O instance's LiquidObject row. Without the DB2s, or for an id the
// client builds itself (42 and others absent from the table), only `ref`.
struct LiquidObject {
    Ref ref;  // Db2Row LiquidObject
    struct Row {
        float flowDirection = 0;  // radians; stored as degrees (0..359 in 12.1.0)
        float flowSpeed = 0;      // as stored, unit unverified (-1..7)
        uint32_t fishable = 0;
        uint32_t reflection = 0;
    };
    std::optional<Row> row;
};

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
