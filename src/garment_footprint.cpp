#include "garment_footprint.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace husk::garment {

namespace {

float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

void rasterizeOne(const UvTriangle& t, Mask& mask) {
    const float w = float(mask.width);
    const float h = float(mask.height);
    const float x0 = t.a.u * w, y0 = t.a.v * h;
    const float x1 = t.b.u * w, y1 = t.b.v * h;
    const float x2 = t.c.u * w, y2 = t.c.v * h;
    const float area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    // Either winding: flip the sign so "inside" is always >= 0.
    const float sign = area > 0 ? 1.0f : -1.0f;

    const float minX = std::max(0.0f, std::floor(std::min({x0, x1, x2})));
    const float maxX = std::min(w - 1, std::ceil(std::max({x0, x1, x2})));
    const float minY = std::max(0.0f, std::floor(std::min({y0, y1, y2})));
    const float maxY = std::min(h - 1, std::ceil(std::max({y0, y1, y2})));
    for (float py = minY; py <= maxY; ++py) {
        for (float px = minX; px <= maxX; ++px) {
            const float cx = px + 0.5f, cy = py + 0.5f;
            if (sign * edge(x0, y0, x1, y1, cx, cy) < 0) continue;
            if (sign * edge(x1, y1, x2, y2, cx, cy) < 0) continue;
            if (sign * edge(x2, y2, x0, y0, cx, cy) < 0) continue;
            mask.covered[size_t(py) * mask.width + size_t(px)] = 1;
        }
    }
}

void requireSameGrid(const Mask& a, const Mask& b) {
    if (a.width == b.width && a.height == b.height) return;
    throw std::logic_error("garment masks on different grids: " + std::to_string(a.width) + "x" +
                           std::to_string(a.height) + " vs " + std::to_string(b.width) + "x" +
                           std::to_string(b.height));
}

}  // namespace

size_t Mask::count() const {
    return size_t(std::count(covered.begin(), covered.end(), uint8_t{1}));
}

Mask rasterize(const std::vector<UvTriangle>& triangles, uint32_t width, uint32_t height) {
    Mask mask{width, height, std::vector<uint8_t>(size_t(width) * height, 0)};
    for (const UvTriangle& t : triangles) rasterizeOne(t, mask);
    return mask;
}

std::vector<UvTriangle> geosetTriangles(const std::vector<m2::Vertex>& vertices,
                                        const std::vector<uint32_t>& triangleIndices,
                                        const std::vector<skin::Submesh>& submeshes, uint16_t geosetId) {
    auto uvOf = [&](uint32_t index) {
        const m2::Vec2& tc = vertices.at(index).texCoords[0];
        return Uv{tc.x, tc.y};
    };
    std::vector<UvTriangle> out;
    for (const skin::Submesh& s : submeshes) {
        if (s.skinSectionId != geosetId) continue;
        for (uint32_t i = s.indexStart; i + 2 < s.indexStart + s.indexCount; i += 3) {
            out.push_back({uvOf(triangleIndices.at(i)), uvOf(triangleIndices.at(i + 1)),
                           uvOf(triangleIndices.at(i + 2))});
        }
    }
    return out;
}

std::optional<PixelRect> bounds(const Mask& mask) {
    uint32_t minX = mask.width, minY = mask.height, maxX = 0, maxY = 0;
    bool any = false;
    for (uint32_t y = 0; y < mask.height; ++y) {
        for (uint32_t x = 0; x < mask.width; ++x) {
            if (!mask.at(x, y)) continue;
            any = true;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    if (!any) return std::nullopt;
    return PixelRect{minX, minY, maxX - minX + 1, maxY - minY + 1};
}

size_t countInside(const Mask& mask, const PixelRect& rect) {
    size_t n = 0;
    const uint32_t endY = std::min(mask.height, rect.y + rect.height);
    const uint32_t endX = std::min(mask.width, rect.x + rect.width);
    for (uint32_t y = rect.y; y < endY; ++y) {
        for (uint32_t x = rect.x; x < endX; ++x) n += mask.at(x, y) ? 1 : 0;
    }
    return n;
}

size_t overlapCount(const Mask& a, const Mask& b) {
    requireSameGrid(a, b);
    size_t n = 0;
    for (size_t i = 0; i < a.covered.size(); ++i) n += (a.covered[i] && b.covered[i]) ? 1 : 0;
    return n;
}

Mask unite(const Mask& a, const Mask& b) {
    requireSameGrid(a, b);
    Mask out = a;
    for (size_t i = 0; i < out.covered.size(); ++i) out.covered[i] = (a.covered[i] || b.covered[i]) ? 1 : 0;
    return out;
}

}  // namespace husk::garment
