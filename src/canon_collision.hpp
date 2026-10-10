#pragma once

#include <cstdint>
#include <vector>

#include "canon_primitives.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// An M2's collision triangles, in the model's own space (the same frame as
// Mesh::positions), with their own vertices. A WMO has none: its faces
// carry their collision flags as a Mesh attribute (wmo.poly_flags).
namespace husk::canon {

struct CollisionMesh {
    std::vector<Vec3> positions;
    std::vector<uint32_t> indices;  // three per triangle, into `positions`

    std::vector<Vec3> triangleNormals;  // one per triangle (M2 collisionFaceNormals); empty when absent
};

}  // namespace husk::canon
