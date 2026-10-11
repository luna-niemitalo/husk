#include "creature_display_db2.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>

#include "creature_geoset_db2.hpp"
#include "db2table.hpp"
#include "texturefiledata_db2.hpp"

namespace husk::creaturedisplay {

namespace {

std::string tablePath(const std::string& db2Dir, const char* file) {
    return (std::filesystem::path(db2Dir) / file).string();
}

uint32_t first(const std::optional<std::vector<uint32_t>>& v) { return v && !v->empty() ? v->front() : 0; }

float asFloat(uint32_t bits) { return std::bit_cast<float>(bits); }

template <size_t N>
std::array<uint32_t, N> words(const std::optional<std::vector<uint32_t>>& v) {
    std::array<uint32_t, N> out{};
    if (!v) return out;
    for (size_t i = 0; i < N && i < v->size(); ++i) out[i] = (*v)[i];
    return out;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string modelPath = tablePath(db2Dir, "creaturemodeldata.db2");
    std::string displayPath = tablePath(db2Dir, "creaturedisplayinfo.db2");
    std::string colorPath = tablePath(db2Dir, "particlecolor.db2");
    // readNamedArrayColumns reads scalars too (one element each), so each
    // table is parsed once.
    auto models = db2table::readNamedArrayColumns(modelPath, dbdDir,
                                                  {"ID", "FileDataID", "ModelScale", "CreatureGeosetDataID"}, err);
    auto displays = db2table::readNamedArrayColumns(displayPath, dbdDir,
                                                    {"ID", "ModelID", "CreatureModelScale", "CreatureModelAlpha",
                                                     "ParticleColorID", "ExtendedDisplayInfoID",
                                                     "TextureVariationFileDataID"},
                                                    err);
    auto colors = db2table::readNamedArrayColumns(colorPath, dbdDir, {"ID", "Start", "MID", "End"}, err);
    auto geosets = creaturegeoset::load(db2Dir, dbdDir, err);
    auto extras = db2table::readNamedArrayColumns(tablePath(db2Dir, "creaturedisplayinfoextra.db2"), dbdDir,
                                                  {"ID", "BakeMaterialResourcesID", "HDBakeMaterialResourcesID"}, err);
    auto picks = db2table::readNamedArrayColumns(tablePath(db2Dir, "creaturedisplayinfooption.db2"), dbdDir,
                                                 {"CreatureDisplayInfoExtraID", "ChrCustomizationOptionID",
                                                  "ChrCustomizationChoiceID"},
                                                 err);
    auto items = db2table::readNamedArrayColumns(tablePath(db2Dir, "npcmodelitemslotdisplayinfo.db2"), dbdDir,
                                                 {"NpcModelID", "ItemDisplayInfoID", "ItemSlot"}, err);
    auto textures = texturefiledata::load(db2Dir, dbdDir, err);
    auto chrModels = db2table::readNamedArrayColumns(tablePath(db2Dir, "chrmodel.db2"), dbdDir, {"ID", "DisplayID"}, err);
    auto customization = chrcustomization::load(db2Dir, dbdDir, err);
    if (!models || !displays || !colors || !geosets || !extras || !picks || !items || !textures || !chrModels ||
        !customization) {
        return std::nullopt;
    }

    Data out;
    for (const auto& row : *models) {
        ModelData model{first(row[0]), asFloat(first(row[2])), first(row[3]) != 0};
        out.modelDataByFile[first(row[1])].push_back(model);
    }
    for (const auto& row : *displays) {
        Display display;
        display.id = first(row[0]);
        display.scale = asFloat(first(row[2]));
        display.alpha = row[3] ? first(row[3]) : 255;
        display.particleColorId = first(row[4]);
        display.extra = first(row[5]);
        display.textures = words<4>(row[6]);
        out.displaysByModelData[first(row[1])].push_back(display);
    }
    for (const creaturegeoset::GeosetEntry& e : geosets->entries) {
        out.geosetsByDisplay[e.creatureDisplayInfoId].push_back((e.geosetIndex + 1) * 100 + e.geosetValue);
    }
    for (const auto& row : *colors) {
        out.particleColors[first(row[0])] = {words<3>(row[1]), words<3>(row[2]), words<3>(row[3])};
    }
    for (const auto& row : *extras) {
        Extra extra{first(row[1]), first(row[2])};
        for (uint32_t bake : {extra.bake, extra.hdBake}) {
            auto texture = textures->find(bake);
            if (texture != textures->end()) out.bakeTextures.emplace(bake, texture->second);
        }
        out.extras.emplace(first(row[0]), extra);
    }
    for (const auto& row : *picks) out.customizationByExtra[first(row[0])].push_back({first(row[1]), first(row[2])});

    std::unordered_map<uint32_t, uint32_t> fileByModelData;
    for (const auto& row : *models) fileByModelData.emplace(first(row[0]), first(row[1]));
    std::unordered_map<uint32_t, uint32_t> fileByDisplay;
    for (const auto& row : *displays) {
        auto file = fileByModelData.find(first(row[1]));
        if (file != fileByModelData.end()) fileByDisplay.emplace(first(row[0]), file->second);
    }
    for (const auto& row : *chrModels) {
        auto file = fileByDisplay.find(first(row[1]));
        if (file != fileByDisplay.end()) out.chrModelsByFile[file->second].push_back(first(row[0]));
    }
    for (auto& [file, ids] : out.chrModelsByFile) std::sort(ids.begin(), ids.end());
    out.customization = std::move(*customization);
    for (const auto& row : *items) out.itemsByExtra[first(row[0])].push_back({first(row[1]), first(row[2])});
    return out;
}

}  // namespace husk::creaturedisplay
