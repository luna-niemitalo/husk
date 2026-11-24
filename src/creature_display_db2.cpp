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

std::string chrModelName(std::vector<RaceUse> uses) {
    std::sort(uses.begin(), uses.end(), [](const RaceUse& a, const RaceUse& b) { return a.race < b.race; });
    std::vector<std::string> names;
    for (const RaceUse& use : uses) {
        if (!use.raceName.empty() && std::find(names.begin(), names.end(), use.raceName) == names.end()) {
            names.push_back(use.raceName);
        }
    }
    if (names.empty()) return "";
    std::string out = names[0];
    for (size_t i = 1; i < names.size(); ++i) out += " / " + names[i];
    bool oneSex = std::all_of(uses.begin(), uses.end(), [&](const RaceUse& u) { return u.sex == uses[0].sex; });
    if (oneSex && uses[0].sex <= 1) out += uses[0].sex == 0 ? " male" : " female";
    return out;
}

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
    std::string creaturePath = tablePath(db2Dir, "creature.db2");
    auto creatures = db2table::readNamedArrayColumns(creaturePath, dbdDir, {"ID", "DisplayID"}, err);
    auto creatureNames = db2table::readNamedStringColumns(creaturePath, dbdDir, {"Name_lang", "Title_lang"}, err);
    auto textures = texturefiledata::load(db2Dir, dbdDir, err);
    auto chrModels = db2table::readNamedArrayColumns(tablePath(db2Dir, "chrmodel.db2"), dbdDir,
                                                     {"ID", "DisplayID", "CharComponentTextureLayoutID"}, err);
    auto customization = chrcustomization::load(db2Dir, dbdDir, err);
    auto layouts = chrmodel::load(db2Dir, dbdDir, err);
    std::string racesPath = tablePath(db2Dir, "chrraces.db2");
    auto races = db2table::readNamedColumns(racesPath, dbdDir, {"ID"}, err);
    auto raceNames = db2table::readNamedStringColumns(racesPath, dbdDir, {"Name_lang"}, err);
    auto raceModels = db2table::readNamedColumns(tablePath(db2Dir, "chrracexchrmodel.db2"), dbdDir,
                                                 {"ChrRacesID", "ChrModelID", "Sex"}, err);
    if (!models || !displays || !colors || !geosets || !extras || !picks || !items || !creatures || !creatureNames ||
        !textures || !chrModels || !customization || !layouts || !races || !raceNames || !raceModels) {
        return std::nullopt;
    }
    if (races->size() != raceNames->size()) {
        err << racesPath << ": column reads disagree on row count (" << races->size() << ", " << raceNames->size() << ")\n";
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
        out.extras.emplace(first(row[0]), Extra{first(row[1]), first(row[2])});
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
        out.layoutByChrModel.emplace(first(row[0]), first(row[2]));
        auto file = fileByDisplay.find(first(row[1]));
        if (file != fileByDisplay.end()) out.chrModelsByFile[file->second].push_back(first(row[0]));
    }
    for (auto& [file, ids] : out.chrModelsByFile) std::sort(ids.begin(), ids.end());
    std::unordered_map<uint32_t, std::string> raceName;
    for (size_t r = 0; r < races->size(); ++r) raceName.emplace((*races)[r][0].value_or(0), (*raceNames)[r][0].value_or(""));
    std::unordered_map<uint32_t, std::vector<RaceUse>> usesByModel;
    for (const auto& row : *raceModels) {
        uint32_t race = row[0].value_or(0);
        auto name = raceName.find(race);
        usesByModel[row[1].value_or(0)].push_back({race, name != raceName.end() ? name->second : "", row[2].value_or(0)});
    }
    for (auto& [chrModel, uses] : usesByModel) {
        std::string name = chrModelName(std::move(uses));
        if (!name.empty()) out.chrModelNames.emplace(chrModel, std::move(name));
    }
    out.customization = std::move(*customization);
    out.layouts = std::move(*layouts);
    out.textureFiles = std::move(*textures);
    for (const auto& row : *items) out.itemsByExtra[first(row[0])].push_back({first(row[1]), first(row[2])});
    for (size_t i = 0; i < creatures->size(); ++i) {
        const auto& row = (*creatures)[i];
        const auto& names = (*creatureNames)[i];
        Creature creature{first(row[0]), names[0].value_or(""), names[1].value_or("")};
        // DisplayID[4] can repeat a display (weighted by DisplayProbability).
        std::array<uint32_t, 4> displayIds = words<4>(row[1]);
        std::sort(displayIds.begin(), displayIds.end());
        auto last = std::unique(displayIds.begin(), displayIds.end());
        for (auto id = displayIds.begin(); id != last; ++id) {
            if (*id != 0) out.creaturesByDisplay[*id].push_back(creature);
        }
    }
    for (auto& [display, list] : out.creaturesByDisplay) {
        std::sort(list.begin(), list.end(), [](const Creature& a, const Creature& b) { return a.id < b.id; });
    }
    return out;
}

}  // namespace husk::creaturedisplay
