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

CharacterSelection assembleCharacterSelection(uint32_t chrModelId, const std::vector<uint32_t>& chosenChoiceIds,
                                               const std::vector<Item>& equippedItems) {
    CharacterSelection result;
    result.chrModelId = chrModelId;

    for (uint32_t id : chosenChoiceIds) {
        Ref ref;
        ref.id = Db2Row{"ChrCustomizationChoice", id};
        result.chosenChoices.push_back(std::move(ref));
    }

    for (const auto& item : equippedItems) {
        result.equippedItems.push_back({item.identity, item.slots});
    }

    return result;
}

}  // namespace husk::canon
