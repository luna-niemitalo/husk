#include "canon_selection_builder.hpp"

namespace husk::canon {

CreatureSelection assembleCreatureSelection(const creaturegeoset::Data& data, uint32_t creatureDisplayId) {
    CreatureSelection result;
    result.creatureDisplayId = creatureDisplayId;

    for (const auto& r : creaturegeoset::resolveDisplay(data, creatureDisplayId)) {
        result.enabledGeosetIds.push_back(r.geosetId);
    }
    return result;
}

}  // namespace husk::canon
