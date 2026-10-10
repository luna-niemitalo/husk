#pragma once

#include <cstdint>
#include <vector>

#include "canon_primitives.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// The triangles a model collides with, in the model's own space (the same
// frame as Mesh::positions). Self-contained: its own vertices, not indices
// into the render mesh, since an M2's collision vertices are separate data.
namespace husk::canon {

struct CollisionMesh {
    std::vector<Vec3> positions;
    std::vector<uint32_t> indices;  // three per triangle, into `positions`

    // Per triangle; each empty when the source has no such data.
    std::vector<uint32_t> triangleParts;        // index into Mesh::parts (WMO group)
    std::vector<uint32_t> triangleSourceFlags;  // WMO MOPY/MPY2 flags, raw
    std::vector<Vec3> triangleNormals;          // M2 collisionFaceNormals
};

}  // namespace husk::canon
