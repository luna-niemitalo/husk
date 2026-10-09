// Real-data tier for the ADT terrain export: a real tile (HUSK_TEST_ADT_TILE,
// default test_data/world/maps/azeroth/azeroth_32_49.adt, with its _obj0/
// _tex0 siblings and azeroth.wdt beside it) through the real parser and input
// module. Asserts the structural invariants WIKI_FINDINGS/ADT.md verified on
// this tile, with margins, rather than exact counts -- so a re-extraction
// that changes a doodad or two doesn't fail the suite, but a wrong axis,
// winding, alpha orientation or placement transform does.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/adt.hpp"
#include "../src/adt_canon_input.hpp"
#include "run_husk.hpp"
#include "test_data_paths.hpp"

namespace adt = husk::adt;
namespace adtinput = husk::adtinput;
namespace canon = husk::canon;
namespace fs = std::filesystem;
using husk::test::testAdtTile;

namespace {

fs::path sibling(const fs::path& tile, const std::string& suffix) {
    return tile.parent_path() / (tile.stem().string() + suffix);
}

// The whole tile through adtinput with nothing resolved: geometry, layers,
// liquid and placement transforms don't depend on resolution.
canon::Terrain buildRealTile() {
    fs::path tile = testAdtTile();
    adt::RootFile root = adt::parseRoot(adt::readFile(tile));
    std::optional<adt::ObjFile> obj = adt::parseObj(adt::readFile(sibling(tile, "_obj0.adt")));
    std::optional<adt::TexFile> tex = adt::parseTex(adt::readFile(sibling(tile, "_tex0.adt")));
    // <map>_<x>_<y>.adt: the coordinates are the last two '_' fields.
    std::string stem = tile.stem().string();
    size_t ySep = stem.rfind('_');
    size_t xSep = stem.rfind('_', ySep - 1);
    std::string map = stem.substr(0, xSep);
    auto x = static_cast<uint32_t>(std::stoul(stem.substr(xSep + 1, ySep - xSep - 1)));
    auto y = static_cast<uint32_t>(std::stoul(stem.substr(ySep + 1)));
    uint32_t wdtFlags = adt::parseWdtFlags(adt::readFile(tile.parent_path() / (map + ".wdt")));
    adtinput::TileSources sources{root, obj, tex, wdtFlags, map, x, y};
    adtinput::TextureResolutions textures;
    adtinput::GroundEffectDefinitions groundEffects;
    adtinput::GamePathResolutions gamePaths;
    return adtinput::buildCanonTerrain(sources, {textures, groundEffects, gamePaths});
}

float corner(const canon::TerrainChunk& c, int r, int col) { return c.heights[static_cast<size_t>(r * 17 + col)]; }

const canon::TerrainChunk& chunkAt(const canon::Terrain& t, int gx, int gy) {
    return t.chunks[static_cast<size_t>(gy * 16 + gx)];
}

}  // namespace

TEST_CASE("real ADT: neighbouring chunks agree on their shared edge vertices" *
          doctest::skip(testAdtTile().empty())) {
    canon::Terrain t = buildRealTile();
    float worst = 0.0f;
    for (int gy = 0; gy < 16; ++gy) {
        for (int gx = 0; gx < 16; ++gx) {
            const canon::TerrainChunk& c = chunkAt(t, gx, gy);
            for (int k = 0; k <= 8; ++k) {
                // grid_x steps -Y: this chunk's last column is the next one's first.
                if (gx < 15) worst = std::max(worst, std::abs(corner(c, k, 8) - corner(chunkAt(t, gx + 1, gy), k, 0)));
                // grid_y steps -X: this chunk's last row is the next one's first.
                if (gy < 15) worst = std::max(worst, std::abs(corner(c, 8, k) - corner(chunkAt(t, gx, gy + 1), 0, k)));
            }
        }
    }
    CHECK(worst < 1e-3f);
}

TEST_CASE("real ADT: decoded normals agree with the heightfield's own slope (MCNR is X, Y, Z)" *
          doctest::skip(testAdtTile().empty())) {
    canon::Terrain t = buildRealTile();
    double cosSum = 0.0;
    size_t n = 0;
    const float q = canon::kTerrainQuadSize;
    for (const canon::TerrainChunk& c : t.chunks) {
        for (int r = 1; r < 8; ++r) {
            for (int col = 1; col < 8; ++col) {
                // Row r+1 is one quad further -X, column c+1 one quad further -Y.
                float dzdx = (corner(c, r - 1, col) - corner(c, r + 1, col)) / (2 * q);
                float dzdy = (corner(c, r, col - 1) - corner(c, r, col + 1)) / (2 * q);
                float len = std::sqrt(dzdx * dzdx + dzdy * dzdy + 1);
                const canon::Vec3& stored = c.normals[static_cast<size_t>(r * 17 + col)];
                cosSum += (-dzdx * stored.x - dzdy * stored.y + stored.z) / len;
                ++n;
            }
        }
    }
    CHECK(cosSum / static_cast<double>(n) > 0.98);
}

TEST_CASE("real ADT: splat alpha never sums past full coverage, so base = 1 - sum(alpha) partitions" *
          doctest::skip(testAdtTile().empty())) {
    canon::Terrain t = buildRealTile();
    size_t layered = 0;
    int worstExcess = 0;
    for (const canon::TerrainChunk& c : t.chunks) {
        CHECK(c.layers.size() <= 8);
        if (c.layers.size() > 1) ++layered;
        for (size_t px = 0; px < 64 * 64; ++px) {
            int sum = 0;
            for (size_t l = 1; l < c.layers.size(); ++l) sum += c.layers[l].alpha[px];
            worstExcess = std::max(worstExcess, sum - 255);
        }
    }
    CHECK(layered > 0);
    // Rounding of 8-bit maps allows one step per extra layer, no more.
    CHECK(worstExcess <= 7);
}

TEST_CASE("real ADT: placements land on their own tile, mostly upright" *
          doctest::skip(testAdtTile().empty())) {
    canon::Terrain t = buildRealTile();
    REQUIRE_FALSE(t.placements.empty());
    float maxX = t.chunks.front().origin.x;
    float maxY = t.chunks.front().origin.y;
    float minX = maxX - canon::kTerrainTileSize;
    float minY = maxY - canon::kTerrainTileSize;

    size_t onTile = 0;
    std::vector<float> ups;
    for (const canon::Placement& p : t.placements) {
        if (p.position.x >= minX && p.position.x <= maxX && p.position.y >= minY && p.position.y <= maxY) ++onTile;
        if (p.kind != canon::Placement::Kind::Model) continue;
        // World z of the rotated model +Z: 1 - 2(x^2 + y^2).
        ups.push_back(1 - 2 * (p.rotation.x * p.rotation.x + p.rotation.y * p.rotation.y));
    }
    CHECK(static_cast<double>(onTile) / static_cast<double>(t.placements.size()) > 0.9);
    REQUIRE_FALSE(ups.empty());
    std::nth_element(ups.begin(), ups.begin() + static_cast<long>(ups.size() / 2), ups.end());
    CHECK(ups[ups.size() / 2] > 0.9f);
}

TEST_CASE("real ADT: liquid surfaces are whole-chunk rects with heights for every vertex" *
          doctest::skip(testAdtTile().empty())) {
    canon::Terrain t = buildRealTile();
    REQUIRE_FALSE(t.liquids.empty());
    for (const canon::LiquidSurface& l : t.liquids) {
        CHECK(l.quadX + l.width <= 8);
        CHECK(l.quadY + l.height <= 8);
        CHECK(l.heights.size() == static_cast<size_t>(l.width + 1) * (l.height + 1));
        CHECK(l.quadExists.size() == static_cast<size_t>(l.width) * l.height);
    }
}

TEST_CASE("real ADT: husk export-terrain exports the real tile end to end" * doctest::skip(testAdtTile().empty())) {
    fs::path out = fs::temp_directory_path() / "husk-test-real-adt.bundle";
    fs::remove_all(out);
    auto r = husk::test::runHusk("export-terrain '" + testAdtTile() + "' '" + out.string() + "'");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(fs::exists(out / "manifest.json"));
    CHECK(r.output.find("husk: warning:") == std::string::npos);
}
