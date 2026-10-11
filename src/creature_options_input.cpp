#include "creature_options_input.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <unordered_map>

#include "canon_definition_builder.hpp"
#include "m2_header.hpp"  // m2::textureTypeName

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
    std::unordered_map<uint32_t, canon::CustomizationPick> choice;    // ChrCustomizationChoiceID
    std::unordered_map<uint32_t, uint32_t> item;                      // ItemDisplayInfoID
};

// A layer's rectangle on its canvas: the whole canvas for a mask of -1,
// else the first section (by section type) its mask names.
std::optional<std::array<uint32_t, 4>> layerRect(const chrmodel::ChrModelTextureLayer& layer,
                                                 const chrmodel::ChrModelMaterial& canvas,
                                                 const std::vector<const chrmodel::CharComponentTextureSection*>& sections) {
    if (layer.textureSectionTypeBitMask == 0xFFFFFFFFu) return std::array<uint32_t, 4>{0, 0, canvas.width, canvas.height};
    for (const chrmodel::CharComponentTextureSection* section : sections) {
        if (section->sectionType < 32 && (layer.textureSectionTypeBitMask & (1u << section->sectionType)) != 0) {
            return std::array<uint32_t, 4>{section->x, section->y, section->width, section->height};
        }
    }
    return std::nullopt;
}

std::string targetInput(uint32_t target) { return "target_" + std::to_string(target); }

// Where a texture target draws: the material document, and its input.
struct TargetSlot {
    uint32_t material = 0;
    std::string input;
};

// Each canvas of a texture layout (one per ChrModelMaterial texture type)
// as a material document whose composite draws the layout's layers, each
// from the input its texture target binds. A slot of that texture type is
// composed by the first layout that has one.
std::map<uint32_t, TargetSlot> addLayoutMaterials(const chrmodel::Data& tables, uint32_t layout, canon::Model& model,
                                                  std::string& notes) {
    std::vector<const chrmodel::CharComponentTextureSection*> sections;
    for (const auto& section : tables.sections) {
        if (section.charComponentTextureLayoutId == layout) sections.push_back(&section);
    }
    std::sort(sections.begin(), sections.end(), [](auto* a, auto* b) { return a->sectionType < b->sectionType; });
    std::vector<const chrmodel::ChrModelMaterial*> canvases;
    for (const auto& material : tables.materials) {
        if (material.charComponentTextureLayoutsId == layout) canvases.push_back(&material);
    }
    std::sort(canvases.begin(), canvases.end(), [](auto* a, auto* b) { return a->textureType < b->textureType; });

    std::map<uint32_t, TargetSlot> targets;
    for (const chrmodel::ChrModelMaterial* canvas : canvases) {
        std::vector<const chrmodel::ChrModelTextureLayer*> layers;
        for (const auto& layer : tables.textureLayers) {
            if (layer.charComponentTextureLayoutsId == layout && layer.textureType == canvas->textureType) layers.push_back(&layer);
        }
        // The table's own order; wow.export draws by target id instead
        // (WIKI_FINDINGS/M2/rendering.md), neither shown from the client.
        std::sort(layers.begin(), layers.end(), [](auto* a, auto* b) { return a->layer < b->layer; });

        canon::MaterialDocument document;
        const char* typeName = m2::textureTypeName(canvas->textureType);
        document.subject = canon::Ref{canon::Db2Row{"ChrModelMaterial", canvas->id},
                                      std::string(typeName ? typeName : "type_" + std::to_string(canvas->textureType)) +
                                          "_layout_" + std::to_string(layout),
                                      canon::NameSource::Synthesized};
        canon::CompositeTexture composite;
        composite.width = canvas->width;
        composite.height = canvas->height;
        uint32_t index = static_cast<uint32_t>(model.materialDocuments.size());
        for (const chrmodel::ChrModelTextureLayer* layer : layers) {
            std::optional<std::array<uint32_t, 4>> rect = layerRect(*layer, *canvas, sections);
            if (!rect) {
                notes += "husk: note: texture layout " + std::to_string(layout) + " layer " + std::to_string(layer->layer) +
                         ": section mask " + std::to_string(layer->textureSectionTypeBitMask) + " names no section; skipped\n";
                continue;
            }
            std::string input = targetInput(layer->chrModelTextureTargetId);
            canon::CompositeLayer drawn;
            drawn.texture = input;
            drawn.rect = *rect;
            if (layer->blendMode <= static_cast<uint32_t>(canon::CompositeBlend::GradientMap)) {
                drawn.blend = static_cast<canon::CompositeBlend>(layer->blendMode);
            }
            drawn.sourceBlendMode = layer->blendMode;
            drawn.sourceLayer = layer->layer;
            composite.layers.push_back(std::move(drawn));
            auto named = [&](const canon::ImageInput& i) { return i.name == input; };
            if (std::none_of(document.imageInputs.begin(), document.imageInputs.end(), named)) {
                document.imageInputs.push_back({input, "The texture customization choices give texture target " +
                                                           std::to_string(layer->chrModelTextureTargetId) +
                                                           " (ChrModelTextureTargetID)."});
            }
            targets.emplace(layer->chrModelTextureTargetId, TargetSlot{index, input});
        }
        document.textures.push_back(std::move(composite));
        document.layers.push_back({canon::SurfaceRole::Base, canon::TextureSource{0u}, canon::UvSetIndex{0}, canon::Combine::Replace});
        model.materialDocuments.push_back(std::move(document));
        for (canon::TextureSlot& slot : model.textureSlots) {
            if (slot.sourceType == canvas->textureType && !slot.composite) slot.composite = index;
        }
    }
    return targets;
}

}  // namespace

void addCreatureOptions(const creaturedisplay::Data& data, uint32_t modelFileDataId, canon::Model& model,
                        const std::function<canon::TextureRef(uint32_t)>& texture, OptionsReport& report) {
    auto modelRows = data.modelDataByFile.find(modelFileDataId);
    if (modelRows == data.modelDataByFile.end()) return;

    std::vector<uint32_t> meshIds = meshGeosetIds(model.mesh);
    OptionIndex index;
    std::unordered_map<uint32_t, canon::TextureRef> resolved;
    auto textureFor = [&](uint32_t fdid) -> const canon::TextureRef& {
        auto it = resolved.find(fdid);
        if (it == resolved.end()) it = resolved.emplace(fdid, texture(fdid)).first;
        return it->second;
    };
    if (auto chrModels = data.chrModelsByFile.find(modelFileDataId); chrModels != data.chrModelsByFile.end()) {
        uint32_t optionBase = 0;
        for (uint32_t chrModel : chrModels->second) {
            std::ostringstream err;
            canon::Definition definition = canon::assembleDefinition(data.customization, chrModel, err);
            report.notes += err.str();
            for (size_t o = 0; o < definition.options.size(); ++o) {
                const std::vector<canon::Choice>& choices = definition.options[o].choices;
                for (size_t c = 0; c < choices.size(); ++c) {
                    index.choice.emplace(std::get<canon::Db2Row>(choices[c].ref.id).row,
                                         canon::CustomizationPick{optionBase + static_cast<uint32_t>(o),
                                                                  static_cast<uint32_t>(c)});
                }
            }
            optionBase += static_cast<uint32_t>(definition.options.size());
            model.customization.push_back(std::move(definition));
        }

        // Each layout once, then every choice's textures onto their targets.
        std::map<uint32_t, std::map<uint32_t, TargetSlot>> targetsByLayout;
        optionBase = 0;
        for (const canon::Definition& definition : model.customization) {
            auto layout = data.layoutByChrModel.find(definition.chrModelId);
            if (layout == data.layoutByChrModel.end() || layout->second == 0) {
                optionBase += static_cast<uint32_t>(definition.options.size());
                continue;
            }
            if (!targetsByLayout.count(layout->second)) {
                targetsByLayout[layout->second] = addLayoutMaterials(data.layouts, layout->second, model, report.notes);
            }
            const std::map<uint32_t, TargetSlot>& targets = targetsByLayout.at(layout->second);
            for (size_t o = 0; o < definition.options.size(); ++o) {
                for (size_t c = 0; c < definition.options[o].choices.size(); ++c) {
                    uint32_t choiceId = std::get<canon::Db2Row>(definition.options[o].choices[c].ref.id).row;
                    std::ostringstream err;
                    chrcustomization::Resolution resolution = chrcustomization::resolveChoice(data.customization, choiceId, err);
                    report.notes += err.str();
                    for (const chrcustomization::MaterialResolution& material : resolution.materials) {
                        auto target = targets.find(material.chrModelTextureTargetId);
                        auto file = data.textureFiles.find(material.materialResourcesId);
                        if (target == targets.end() || file == data.textureFiles.end()) {
                            ++report.unplacedChoiceTextures;
                            continue;
                        }
                        canon::ChoiceTexture bound;
                        bound.choice = {optionBase + static_cast<uint32_t>(o), static_cast<uint32_t>(c)};
                        bound.material = target->second.material;
                        bound.input = target->second.input;
                        bound.texture = textureFor(file->second);
                        if (material.relatedChoiceId != 0) {
                            auto related = index.choice.find(material.relatedChoiceId);
                            if (related == index.choice.end()) {
                                ++report.unplacedChoiceTextures;
                                continue;
                            }
                            bound.onlyWith = related->second;
                        }
                        model.choiceTextures.push_back(std::move(bound));
                    }
                }
            }
            optionBase += static_cast<uint32_t>(definition.options.size());
        }
    }
    std::map<std::string, size_t> presetByChoices;

    auto textureOption = [&](uint32_t slot, uint32_t fdid) {
        auto [it, added] = index.textureOption.emplace(std::pair{slot, fdid}, 0);
        if (added) {
            it->second = static_cast<uint32_t>(model.textureSlots[slot].options.size());
            model.textureSlots[slot].options.push_back(textureFor(fdid));
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
        auto bakeTexture = data.textureFiles.find(bake);
        std::optional<uint32_t> slot = bakeSlot(model.textureSlots);
        if (bake != 0 && bakeTexture != data.textureFiles.end() && slot) {
            preset.textures.push_back({*slot, textureOption(*slot, bakeTexture->second)});
            key += "b" + std::to_string(bakeTexture->second) + ";";
        } else if (bake != 0) {
            preset.unresolved.push_back({"BakeMaterialResourcesID", canon::UnresolvedField::Kind::Id, double(bake), std::nullopt});
            key += "b?" + std::to_string(bake) + ";";
        }
        if (auto picks = data.customizationByExtra.find(extraId); picks != data.customizationByExtra.end()) {
            for (const creaturedisplay::CustomizationPick& pick : picks->second) {
                key += "c" + std::to_string(pick.choice) + ";";
                auto found = index.choice.find(pick.choice);
                if (found == index.choice.end()) {
                    preset.unresolved.push_back(
                        {"ChrCustomizationChoiceID", canon::UnresolvedField::Kind::Id, double(pick.choice), std::nullopt});
                    continue;
                }
                preset.customization.push_back(found->second);
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
