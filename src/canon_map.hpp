#pragma once

#include <cstdint>
#include <vector>

#include "canon_placement.hpp"
#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// A map as a whole: its tile grid, and what it places rather than a tile
// (volumes span tiles freely). World frame, the same as canon::Terrain
// (+X north, +Y west, Z up, origin at the map centre).
namespace husk::canon {

// One present cell of a world's tile grid: the tile, and the world-frame
// box its terrain surface and liquids occupy (placed objects not included).
struct WorldCell {
    uint32_t tileX = 0;  // canon::terrainTileCorner places the tile
    uint32_t tileY = 0;
    Ref tile;
    Vec3 boundsMin;
    Vec3 boundsMax;
};

// A map as the root of a place: its tiles and what it places as a whole.
struct World {
    Ref subject;  // the map
    std::vector<WorldCell> cells;
    // What the world places off the grid, world frame: the map-wide WMO of a
    // map without terrain, and the map's fog and particulate volumes as
    // effect instances (mapinput::buildMapEffects).
    std::vector<PlacementSet> placements;
};

}  // namespace husk::canon
