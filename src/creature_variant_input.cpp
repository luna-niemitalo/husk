#include "creature_variant_input.hpp"

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

std::array<float, 4> argbToRgba(uint32_t argb) {
    auto channel = [&](int shift) { return static_cast<float>((argb >> shift) & 0xFF) / 255.0f; };
    return {channel(16), channel(8), channel(0), channel(24)};
}

std::optional<uint32_t> slotForType(const std::vector<canon::TextureSlot>& slots, uint32_t type) {
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].sourceType == type) return static_cast<uint32_t>(i);
    }
    return std::nullopt;
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

}  // namespace

std::vector<canon::Variant> assembleVariants(const creaturedisplay::Data& data, uint32_t modelFileDataId,
                                             const canon::Model& model,
                                             const std::function<canon::TextureRef(uint32_t)>& texture,
                                             VariantReport& report) {
    std::vector<canon::Variant> variants;
    auto modelRows = data.modelDataByFile.find(modelFileDataId);
    if (modelRows == data.modelDataByFile.end()) return variants;

    std::vector<uint32_t> meshIds = meshGeosetIds(model.mesh);
    std::unordered_map<uint32_t, canon::TextureRef> textures;
    auto textureFor = [&](uint32_t fdid) -> const canon::TextureRef& {
        auto it = textures.find(fdid);
        if (it == textures.end()) it = textures.emplace(fdid, texture(fdid)).first;
        return it->second;
    };
    std::map<std::string, size_t> byLook;

    for (const creaturedisplay::ModelData& modelData : modelRows->second) {
        auto displays = data.displaysByModelData.find(modelData.id);
        if (displays == data.displaysByModelData.end()) continue;
        for (const creaturedisplay::Display& display : displays->second) {
            ++report.displays;
            if (display.humanoid) {
                ++report.humanoid;
                continue;
            }
            canon::Variant look;
            std::string key;
            for (size_t i = 0; i < display.textures.size(); ++i) {
                uint32_t fdid = display.textures[i];
                if (fdid == 0) continue;
                key += "t" + std::to_string(i) + ":" + std::to_string(fdid) + ";";
                std::optional<uint32_t> slot =
                    i < kTypedVariations ? slotForType(model.textureSlots, kFirstVariationType + uint32_t(i)) : std::nullopt;
                if (slot) {
                    look.textures.push_back({*slot, textureFor(fdid)});
                } else {
                    look.unresolved.push_back({"TextureVariationFileDataID[" + std::to_string(i) + "]",
                                               canon::UnresolvedField::Kind::Id, double(fdid), std::nullopt});
                }
            }
            if (modelData.displaysPickGeosets) {
                auto picked = data.geosetsByDisplay.find(display.id);
                look.shownGeosets = shownGeosets(
                    meshIds, picked == data.geosetsByDisplay.end() ? std::vector<uint32_t>{} : picked->second);
                key += "g";
                for (uint32_t id : *look.shownGeosets) key += std::to_string(id) + ",";
            }
            if (display.particleColorId != 0) {
                key += "p" + std::to_string(display.particleColorId) + ";";
                auto colors = data.particleColors.find(display.particleColorId);
                if (colors == data.particleColors.end()) {
                    look.unresolved.push_back({"ParticleColorID", canon::UnresolvedField::Kind::Id,
                                               double(display.particleColorId), std::nullopt});
                } else {
                    for (size_t i = 0; i < 3; ++i) {
                        look.particleColors.push_back({argbToRgba(colors->second.start[i]),
                                                       argbToRgba(colors->second.mid[i]),
                                                       argbToRgba(colors->second.end[i])});
                    }
                }
            }
            if (display.alpha != 255) {
                look.opacity = static_cast<float>(display.alpha) / 255.0f;
                key += "a" + std::to_string(display.alpha);
            }

            auto [it, added] = byLook.emplace(key, variants.size());
            if (added) variants.push_back(std::move(look));
            variants[it->second].sources.push_back(
                {canon::Ref{canon::Db2Row{"CreatureDisplayInfo", display.id}, "", canon::NameSource::None},
                 display.scale, modelData.scale});
        }
    }
    return variants;
}

}  // namespace husk::creatureinput
