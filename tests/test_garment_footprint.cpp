#include <doctest/doctest.h>

#include "garment_footprint.hpp"

using namespace husk::garment;

namespace {

// The lower-left half of a 4x4 grid: pixel centres (x+0.5, y+0.5) with
// x + y <= 3 lie inside or on the diagonal u + v = 1, ten of sixteen.
const UvTriangle kHalf{{0, 0}, {1, 0}, {0, 1}};

husk::m2::Vertex vertexAt(float u, float v) {
    husk::m2::Vertex vx;
    vx.texCoords[0] = {u, v};
    return vx;
}

}  // namespace

TEST_CASE("rasterize marks the pixel centres inside a triangle, either winding") {
    Mask m = rasterize({kHalf}, 4, 4);
    CHECK(m.count() == 10);
    CHECK(m.at(0, 0));
    CHECK(m.at(3, 0));
    CHECK(m.at(0, 3));
    CHECK_FALSE(m.at(3, 3));
    CHECK_FALSE(m.at(2, 2));

    Mask reversed = rasterize({{kHalf.a, kHalf.c, kHalf.b}}, 4, 4);
    CHECK(reversed.covered == m.covered);
}

TEST_CASE("rasterize clips triangles reaching outside UV 0..1 and skips degenerate ones") {
    CHECK(rasterize({{{0, 0}, {2, 0}, {0, 2}}}, 4, 4).count() == 16);
    CHECK(rasterize({{{0, 0}, {1, 1}, {0.5f, 0.5f}}}, 4, 4).count() == 0);
    CHECK(rasterize({{{-1, -1}, {-0.5f, -1}, {-1, -0.5f}}}, 4, 4).count() == 0);
}

TEST_CASE("bounds, countInside, overlap and unite on rasterized masks") {
    Mask lower = rasterize({kHalf}, 4, 4);
    Mask upper = rasterize({{{1, 1}, {0, 1}, {1, 0}}}, 4, 4);

    auto box = bounds(lower);
    REQUIRE(box);
    CHECK(box->x == 0);
    CHECK(box->y == 0);
    CHECK(box->width == 4);
    CHECK(box->height == 4);
    CHECK_FALSE(bounds(rasterize({}, 4, 4)));

    CHECK(countInside(lower, {2, 2, 2, 2}) == 0);
    CHECK(countInside(lower, {0, 0, 2, 2}) == 4);
    // Edges are inclusive: the four centres exactly on the shared diagonal
    // belong to both halves.
    CHECK(overlapCount(lower, upper) == 4);
    CHECK(unite(lower, upper).count() == 16);
}

TEST_CASE("geosetTriangles reads one geoset's triangles in texture coordinate set 0") {
    std::vector<husk::m2::Vertex> vertices{vertexAt(0, 0), vertexAt(1, 0), vertexAt(0, 1), vertexAt(1, 1)};
    std::vector<uint32_t> indices{0, 1, 2, 1, 3, 2};
    husk::skin::Submesh hem;
    hem.skinSectionId = 1302;
    hem.indexStart = 0;
    hem.indexCount = 3;
    husk::skin::Submesh thigh;
    thigh.skinSectionId = 1101;
    thigh.indexStart = 3;
    thigh.indexCount = 3;

    auto tris = geosetTriangles(vertices, indices, {hem, thigh}, 1302);
    REQUIRE(tris.size() == 1);
    CHECK(tris[0].b.u == 1.0f);
    CHECK(tris[0].c.v == 1.0f);
    CHECK(geosetTriangles(vertices, indices, {hem, thigh}, 1301).empty());
    CHECK(rasterize(tris, 4, 4).count() == 10);
}
