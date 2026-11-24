#include "canon_item_builder.hpp"

#include <algorithm>

namespace husk::canon {

namespace {

uint32_t dbRow(const Ref& ref) { return std::get<Db2Row>(ref.id).row; }

componentfile::Pick pickFile(const GearSources& sources, componentfile::Kind kind, const std::vector<uint32_t>* files,
                             uint32_t side) {
    const char* table = kind == componentfile::Kind::Model ? "ModelFileData" : "TextureFileData";
    if (!files) return {std::nullopt, std::string(table) + " lists no file for this resource"};
    if (files->size() > 1 && !sources.components) {
        return {std::nullopt, std::to_string(files->size()) + " files by race and sex; Component" +
                                  table + " is not loaded"};
    }
    static const componentfile::Data kNoTags;
    return componentfile::pick(sources.components ? *sources.components : kNoTags, kind, *files, sources.wearers,
                               side);
}

template <typename Map>
const std::vector<uint32_t>* filesFor(const Map* map, uint32_t resourcesId) {
    if (!map) return nullptr;
    auto it = map->find(resourcesId);
    return it == map->end() ? nullptr : &it->second;
}

componentfile::Pick pickTexture(const GearSources& sources, uint32_t materialResourcesId) {
    return pickFile(sources, componentfile::Kind::Texture, filesFor(sources.textures, materialResourcesId), 0);
}

}  // namespace

std::vector<Item> assembleEquippedItems(const itemappearance::Data& data, const GearSources& sources,
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
        // the same component data.
        if (!firstOccurrence) continue;

        if (!resolution.sectionMaterials.empty()) {
            SectionOverlayComponent overlay;
            overlay.sections.reserve(resolution.sectionMaterials.size());
            for (const auto& m : resolution.sectionMaterials) {
                componentfile::Pick p = pickTexture(sources, m.materialResourcesId);
                overlay.sections.push_back(
                    {m.componentSection, m.materialResourcesId, p.fileDataId.value_or(0), p.unresolved});
            }
            itemIt->components.push_back(std::move(overlay));
        }

        // A ModelResourcesID of 0 means no model in that position (a
        // common case-2-only item), not an unresolved one.
        GeometryComponent geometry;
        if (resolution.modelResourcesIds) {
            for (uint32_t position = 0; position < 2; ++position) {
                uint32_t resourcesId = (*resolution.modelResourcesIds)[position];
                if (resourcesId == 0) continue;
                componentfile::Pick p =
                    pickFile(sources, componentfile::Kind::Model, filesFor(sources.models, resourcesId), position);
                geometry.models.push_back({position, resourcesId, p.fileDataId, p.unresolved});
            }
        }
        for (const auto& m : resolution.materials) {
            componentfile::Pick p = pickTexture(sources, m.materialResourcesId);
            geometry.materials.push_back(
                {m.textureType, m.materialResourcesId, p.fileDataId.value_or(0), m.modelIndex, p.unresolved});
        }
        if (!geometry.models.empty() || !geometry.materials.empty()) itemIt->components.push_back(std::move(geometry));
    }

    return result;
}

}  // namespace husk::canon
