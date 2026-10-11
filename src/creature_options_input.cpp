#include "creature_options_input.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace husk::creatureinput {

namespace {

// TextureVariationFileDataID[i] fills M2 texture type 11 + i (Monster1..3,
// wowdev.wiki M2#Textures); the fourth entry has no known type.
constexpr uint32_t kFirstVariationType = 11;
constexpr size_t kTypedVariations = 3;
constexpr const char* kFourthEntrySlot = "variation_4";
constexpr const char* kFourthEntryInference =
    "CreatureDisplayInfo.TextureVariationFileDataID[3] of this model's displays; no texture type in the model "
    "takes it, so which part it textures is unknown";

std::array<float, 4> argbToRgba(uint32_t argb) {
    auto channel = [&](int shift) { return static_cast<float>((argb >> shift) & 0xFF) / 255.0f; };
    return {channel(16), channel(8), channel(0), channel(24)};
}

// The slot texture entry `entry` fills, added when the model has none: the
// texture still belongs to this model, a layer just doesn't read it.
uint32_t slotForEntry(std::vector<canon::TextureSlot>& slots, size_t entry) {
    auto has = [&](const canon::TextureSlot& slot) {
        return entry < kTypedVariations ? slot.sourceType == kFirstVariationType + uint32_t(entry)
                                        : slot.name == kFourthEntrySlot && !slot.sourceType;
    };
    auto it = std::find_if(slots.begin(), slots.end(), has);
    if (it != slots.end()) return static_cast<uint32_t>(it - slots.begin());
    canon::TextureSlot added;
    if (entry < kTypedVariations) {
        added.sourceType = kFirstVariationType + uint32_t(entry);
        added.name = "monster_" + std::to_string(entry + 1);
    } else {
        added.name = kFourthEntrySlot;
        added.inferredFrom = kFourthEntryInference;
    }
    slots.push_back(std::move(added));
    return static_cast<uint32_t>(slots.size() - 1);
}

std::vector<uint32_t> meshGeosetIds(const canon::Mesh& mesh) {
    std::set<uint32_t> ids;
    for (const canon::PrimitiveGeoset& p : mesh.primitives) ids.insert(p.geoset.group * 100 + p.geoset.variant);
    return {ids.begin(), ids.end()};
}

// Not on the wiki; wow.export's creature viewer (tab_creatures.js): a
// display that picks geosets hides 1..899 and shows the ids it lists;
// 0 and 900+ stay as they are.
std::vector<uint32_t> shownGeosets(const std::vector<uint32_t>& meshIds, const std::vector<uint32_t>& picked) {
    std::vector<uint32_t> shown;
    for (uint32_t id : meshIds) {
        bool decided = id > 0 && id < 900;
        if (!decided || std::find(picked.begin(), picked.end(), id) != picked.end()) shown.push_back(id);
    }
    return shown;
}

// NPCModelItemSlotDisplayInfo.ItemSlot names, from wow.export's
// DBNpcEquipment.js (not on the wiki); 11 has none.
const char* npcItemSlotName(uint32_t slot) {
    static const char* const kNames[] = {"head",  "shoulder", "shirt", "chest", "waist", "legs",
                                         "feet",  "wrist",    "hands", "tabard", "back"};
    return slot < std::size(kNames) ? kNames[slot] : nullptr;
}

// The slot a humanoid NPC's baked skin fills: the lowest texture type the
// model takes, as wow.export applies it (the lowest type of the model's
// character texture layout; for character models, type 1, skin).
std::optional<uint32_t> bakeSlot(const std::vector<canon::TextureSlot>& slots) {
    std::optional<uint32_t> best;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].sourceType) continue;
        if (!best || *slots[i].sourceType < *slots[*best].sourceType) best = static_cast<uint32_t>(i);
    }
    return best;
}

canon::Ref db2Ref(const char* table, uint32_t row) {
    return canon::Ref{canon::Db2Row{table, row}, "", canon::NameSource::None};
}

// First-seen option indices, so the same source value is one option.
struct OptionIndex {
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> textureOption;  // (slot, fdid)
    std::unordered_map<uint32_t, uint32_t> particleOption;            // ParticleColorID
    std::unordered_map<uint32_t, uint32_t> choice;                    // ChrCustomizationChoiceID
    std::unordered_map<uint32_t, uint32_t> item;                      // ItemDisplayInfoID
};

}  // namespace

void addCreatureOptions(const creaturedisplay::Data& data, uint32_t modelFileDataId, canon::Model& model,
                        const std::function<canon::TextureRef(uint32_t)>& texture, OptionsReport& report) {
    auto modelRows = data.modelDataByFile.find(modelFileDataId);
    if (modelRows == data.modelDataByFile.end()) return;

    std::vector<uint32_t> meshIds = meshGeosetIds(model.mesh);
    OptionIndex index;
    std::map<std::string, size_t> presetByChoices;

    auto textureOption = [&](uint32_t slot, uint32_t fdid) {
        auto [it, added] = index.textureOption.emplace(std::pair{slot, fdid}, 0);
        if (added) {
            it->second = static_cast<uint32_t>(model.textureSlots[slot].options.size());
            model.textureSlots[slot].options.push_back(texture(fdid));
        }
        return it->second;
    };
    auto particleOption = [&](uint32_t id, const creaturedisplay::ParticleColors& colors) {
        auto [it, added] = index.particleOption.emplace(id, 0);
        if (added) {
            it->second = static_cast<uint32_t>(model.particleColorOptions.size());
            canon::ParticleColorOption option;
            option.ref = canon::Ref{canon::Db2Row{"ParticleColor", id}, "", canon::NameSource::None};
            for (size_t i = 0; i < 3; ++i) {
                option.ramps.push_back(
                    {argbToRgba(colors.start[i]), argbToRgba(colors.mid[i]), argbToRgba(colors.end[i])});
            }
            model.particleColorOptions.push_back(std::move(option));
        }
        return it->second;
    };

    // A humanoid NPC: its baked skin as an option of the bake slot, and its
    // customization choices and items by identity.
    auto addNpcLook = [&](uint32_t extraId, canon::Preset& preset, std::string& key) {
        key += "x";
        auto extra = data.extras.find(extraId);
        if (extra == data.extras.end()) {
            preset.unresolved.push_back({"ExtendedDisplayInfoID", canon::UnresolvedField::Kind::Id, double(extraId), std::nullopt});
            key += std::to_string(extraId) + ";";
            return;
        }
        uint32_t bake = extra->second.hdBake != 0 ? extra->second.hdBake : extra->second.bake;
        auto bakeTexture = data.bakeTextures.find(bake);
        std::optional<uint32_t> slot = bakeSlot(model.textureSlots);
        if (bake != 0 && bakeTexture != data.bakeTextures.end() && slot) {
            preset.textures.push_back({*slot, textureOption(*slot, bakeTexture->second)});
            key += "b" + std::to_string(bakeTexture->second) + ";";
        } else if (bake != 0) {
            preset.unresolved.push_back({"BakeMaterialResourcesID", canon::UnresolvedField::Kind::Id, double(bake), std::nullopt});
            key += "b?" + std::to_string(bake) + ";";
        }
        if (auto picks = data.customizationByExtra.find(extraId); picks != data.customizationByExtra.end()) {
            for (const creaturedisplay::CustomizationPick& pick : picks->second) {
                auto [it, added] = index.choice.emplace(pick.choice, static_cast<uint32_t>(model.customizationChoices.size()));
                if (added) {
                    model.customizationChoices.push_back(
                        {db2Ref("ChrCustomizationOption", pick.option), db2Ref("ChrCustomizationChoice", pick.choice)});
                }
                preset.customization.push_back(it->second);
                key += "c" + std::to_string(pick.choice) + ";";
            }
        }
        if (auto items = data.itemsByExtra.find(extraId); items != data.itemsByExtra.end()) {
            for (const creaturedisplay::NpcItem& item : items->second) {
                if (item.itemDisplayInfo == 0) continue;
                const char* name = npcItemSlotName(item.slot);
                auto [it, added] = index.item.emplace(item.itemDisplayInfo, static_cast<uint32_t>(model.items.size()));
                if (added) model.items.push_back(db2Ref("ItemDisplayInfo", item.itemDisplayInfo));
                preset.items.push_back({name ? std::optional<std::string>(name) : std::nullopt, item.slot, it->second});
                key += "i" + std::to_string(item.slot) + ":" + std::to_string(item.itemDisplayInfo) + ";";
            }
        }
    };

    for (const creaturedisplay::ModelData& modelData : modelRows->second) {
        auto displays = data.displaysByModelData.find(modelData.id);
        if (displays == data.displaysByModelData.end()) continue;
        for (const creaturedisplay::Display& display : displays->second) {
            ++report.displays;
            canon::Preset preset;
            std::string key;
            if (display.extra != 0) {
                ++report.humanoid;
                addNpcLook(display.extra, preset, key);
            }
            for (size_t i = 0; i < display.textures.size(); ++i) {
                uint32_t fdid = display.textures[i];
                if (fdid == 0) continue;
                key += "t" + std::to_string(i) + ":" + std::to_string(fdid) + ";";
                uint32_t slot = slotForEntry(model.textureSlots, i);
                preset.textures.push_back({slot, textureOption(slot, fdid)});
            }
            if (modelData.displaysPickGeosets) {
                auto picked = data.geosetsByDisplay.find(display.id);
                preset.shownGeosets = shownGeosets(
                    meshIds, picked == data.geosetsByDisplay.end() ? std::vector<uint32_t>{} : picked->second);
                key += "g";
                for (uint32_t id : *preset.shownGeosets) key += std::to_string(id) + ",";
            }
            if (display.particleColorId != 0) {
                key += "p" + std::to_string(display.particleColorId) + ";";
                auto colors = data.particleColors.find(display.particleColorId);
                if (colors == data.particleColors.end()) {
                    preset.unresolved.push_back({"ParticleColorID", canon::UnresolvedField::Kind::Id,
                                                 double(display.particleColorId), std::nullopt});
                } else {
                    preset.particleColors = particleOption(display.particleColorId, colors->second);
                }
            }
            if (display.alpha != 255) {
                preset.opacity = static_cast<float>(display.alpha) / 255.0f;
                key += "a" + std::to_string(display.alpha);
            }

            auto [it, added] = presetByChoices.emplace(key, model.presets.size());
            if (added) model.presets.push_back(std::move(preset));
            model.presets[it->second].sources.push_back(
                {canon::Ref{canon::Db2Row{"CreatureDisplayInfo", display.id}, "", canon::NameSource::None},
                 display.scale, modelData.scale});
        }
    }
}

}  // namespace husk::creatureinput
