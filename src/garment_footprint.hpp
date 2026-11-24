#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "m2_skeleton.hpp"
#include "skin.hpp"

// A body mesh's UV footprint on the composited skin texture: which pixels
// of the composite a geoset's triangles sample. The garment split
// (REFACTOR/GARMENTS.md stage 2) cuts a legs item's hem out of its
// LegUpper/LegLower texture with the hem geoset's footprint.
namespace husk::garment {

struct Uv {
    float u = 0;
    float v = 0;
};

struct UvTriangle {
    Uv a, b, c;
};

struct PixelRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// One byte per composite pixel, row-major, row 0 at v = 0 (the top row of
// the texture, as M2 UVs and BLP rows both run).
struct Mask {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> covered;

    size_t count() const;
    bool at(uint32_t x, uint32_t y) const { return covered[size_t(y) * width + x] != 0; }
};

// Marks every pixel whose centre lies inside or on the edge of a triangle,
// the sampling rule a GPU bake of the same triangles uses. UV 0..1 spans
// the whole grid; triangles reaching outside it are clipped to it.
Mask rasterize(const std::vector<UvTriangle>& triangles, uint32_t width, uint32_t height);

// The triangles of every submesh whose skinSectionId is `geosetId`, in
// texture coordinate set 0 (the one the body's composite texture uses).
// `triangleIndices` is skin::resolveTriangleIndices's output for the same
// .skin the submeshes came from.
std::vector<UvTriangle> geosetTriangles(const std::vector<m2::Vertex>& vertices,
                                        const std::vector<uint32_t>& triangleIndices,
                                        const std::vector<skin::Submesh>& submeshes, uint16_t geosetId);

std::optional<PixelRect> bounds(const Mask& mask);
size_t countInside(const Mask& mask, const PixelRect& rect);
// Both masks must share one grid.
size_t overlapCount(const Mask& a, const Mask& b);
Mask unite(const Mask& a, const Mask& b);

}  // namespace husk::garment
