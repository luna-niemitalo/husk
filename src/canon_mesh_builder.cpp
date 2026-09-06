#include "canon_mesh_builder.hpp"

#include <stdexcept>
#include <string>

namespace husk::canon {

std::vector<PrimitiveGeoset> assemblePrimitiveGeosets(const std::vector<skin::Batch>& batches,
                                                       const std::vector<skin::Submesh>& submeshes,
                                                       size_t triangleIndexCount) {
    std::vector<PrimitiveGeoset> result;
    result.reserve(batches.size());

    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s skinSectionIndex (" +
                                      std::to_string(b.skinSectionIndex) +
                                      ") is out of range for " + std::to_string(submeshes.size()) +
                                      " submeshes");
        }
        const auto& sm = submeshes[b.skinSectionIndex];
        if (static_cast<size_t>(sm.indexStart) + sm.indexCount > triangleIndexCount) {
            throw std::runtime_error(
                "submesh " + std::to_string(b.skinSectionIndex) +
                "'s index range runs past the end of the resolved triangle-index buffer -- "
                "corrupted .skin?");
        }

        // A submesh with zero indices alongside siblings that have real
        // geometry -- no primitive to build, same skip export_materials.cpp
        // applies.
        if (sm.indexCount == 0) {
            continue;
        }

        PrimitiveGeoset pg;
        pg.geoset = Geoset::fromRawId(sm.skinSectionId);
        pg.indexStart = sm.indexStart;
        pg.indexCount = sm.indexCount;
        result.push_back(pg);
    }

    return result;
}

}  // namespace husk::canon
