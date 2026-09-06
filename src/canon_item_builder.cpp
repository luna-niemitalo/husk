#include "canon_item_builder.hpp"

#include <algorithm>

namespace husk::canon {

namespace {

uint32_t dbRow(const Ref& ref) { return std::get<Db2Row>(ref.id).row; }

uint32_t resolveFileDataId(const std::optional<texturefiledata::Data>& textureData, uint32_t materialResourcesId) {
    if (!textureData) return 0;
    auto it = textureData->find(materialResourcesId);
    return it != textureData->end() ? it->second : 0;
}

}  // namespace

std::vector<Item> assembleEquippedItems(const itemappearance::Data& data,
                                         const std::optional<modelfiledata::Data>& modelData,
                                         const std::optional<texturefiledata::Data>& textureData,
                                         const std::vector<appearance::GearEntry>& gear, std::ostream& err) {
    std::vector<Item> result;

    for (const auto& entry : gear) {
        uint32_t appearanceId = static_cast<uint32_t>(entry.itemModifiedAppearanceId);
        itemappearance::Resolution resolution = itemappearance::resolve(data, appearanceId, err);
        if (!resolution.itemDisplayInfoId) {
            err << "husk: note: gear entry '" << entry.slot << ":" << entry.itemModifiedAppearanceId
                << "' didn't resolve to a real ItemDisplayInfoID -- skipping\n";
            continue;
        }

        auto itemIt = std::find_if(result.begin(), result.end(),
                                    [&](const Item& item) { return dbRow(item.identity) == appearanceId; });
        bool firstOccurrence = itemIt == result.end();
        if (firstOccurrence) {
            Item item;
            item.identity.id = Db2Row{"ItemModifiedAppearance", appearanceId};
            result.push_back(std::move(item));
            itemIt = result.end() - 1;
        }

        if (std::find(itemIt->slots.begin(), itemIt->slots.end(), entry.slot) == itemIt->slots.end()) {
            itemIt->slots.push_back(entry.slot);
        }

        // I7's own payoff: components are resolved once per distinct
        // appearance id, at its first occurrence only -- a second gear
        // entry sharing the same id must not re-resolve (and duplicate)
        // the same component data, unlike attachGearAppearance's own
        // one-entry-per-slot loop (export_extras.cpp).
        if (!firstOccurrence) continue;

        if (!resolution.sectionMaterials.empty()) {
            SectionOverlayComponent overlay;
            overlay.sections.reserve(resolution.sectionMaterials.size());
            for (const auto& m : resolution.sectionMaterials) {
                overlay.sections.push_back({m.componentSection, m.materialResourcesId,
                                             resolveFileDataId(textureData, m.materialResourcesId)});
            }
            itemIt->components.push_back(std::move(overlay));
        }

        // A real ModelResourcesID of 0 means "no standalone geometry" (a
        // real, common case-2-only item) -- not "unresolved," since
        // itemappearance::resolve always fills the optional with
        // DisplayInfo::modelResourcesId's raw value, zero included. Same
        // check attachGearAppearance itself must draw for the same reason
        // (export_extras.cpp's own comment there).
        bool hasRealModel = resolution.modelResourcesId && *resolution.modelResourcesId != 0;
        if (hasRealModel || !resolution.materials.empty()) {
            GeometryComponent geometry;
            if (hasRealModel && modelData) {
                auto it = modelData->find(*resolution.modelResourcesId);
                if (it != modelData->end()) geometry.modelFileDataIds = it->second;
            }
            geometry.materials.reserve(resolution.materials.size());
            for (const auto& m : resolution.materials) {
                geometry.materials.push_back(
                    {m.textureType, m.materialResourcesId, resolveFileDataId(textureData, m.materialResourcesId)});
            }
            itemIt->components.push_back(std::move(geometry));
        }
    }

    return result;
}

}  // namespace husk::canon
