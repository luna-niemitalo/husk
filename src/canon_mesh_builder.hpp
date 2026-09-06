#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "canon_geoset.hpp"
#include "skin.hpp"

// husk::canon: assembly of per-primitive geoset identity from a .skin's
// batch/submesh tables -- the adjacent twin of buildMaterialsAndPrimitives's
// batch loop (export_materials.cpp), built to prove structural convergence
// (REFACTOR/README.md stage 3's gate) without touching the existing
// pipeline. Geoset-id/index-range extraction only -- material/texture
// resolution is a separate, much larger canon concern not built yet.
namespace husk::canon {

// indexStart/indexCount are carried here rather than left for the caller to
// re-slice from skin::Submesh, since a caller only has this type's own
// batch-filtered result to work from (skipped/invalid submeshes never
// appear here at all) -- re-deriving the range from the original submesh
// list would mean re-doing this function's own filtering.
struct PrimitiveGeoset {
    Geoset geoset;
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
};

// Mirrors buildMaterialsAndPrimitives's batch loop (export_materials.cpp):
// for each batch, resolves its submesh, skips a zero-indexCount submesh (a
// real, documented case -- an empty geoset alongside siblings with real
// geometry), and otherwise emits one PrimitiveGeoset per batch. Throws
// std::runtime_error on the same two corruption cases that function
// guards against: an out-of-range skinSectionIndex, or a submesh index
// range running past triangleIndexCount.
std::vector<PrimitiveGeoset> assemblePrimitiveGeosets(const std::vector<skin::Batch>& batches,
                                                       const std::vector<skin::Submesh>& submeshes,
                                                       size_t triangleIndexCount);

}  // namespace husk::canon
