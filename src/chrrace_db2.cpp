#include "chrrace_db2.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

#include "db2table.hpp"

namespace husk::chrrace {

namespace {

uint32_t orZero(const std::optional<uint32_t>& v) { return v.value_or(0); }

std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<Race> loadRaces(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<Race> result;
    auto intRows =
        db2table::readNamedColumns(joinPath(db2Dir, "chrraces.db2"), dbdDir, {"ID"}, err);
    if (!intRows) return result;
    auto strRows = db2table::readNamedStringColumns(joinPath(db2Dir, "chrraces.db2"), dbdDir,
                                                      {"ClientFileString"}, err);
    for (size_t i = 0; i < intRows->size(); ++i) {
        std::string token = (strRows && i < strRows->size()) ? (*strRows)[i][0].value_or("") : "";
        result.push_back({orZero((*intRows)[i][0]), std::move(token)});
    }
    return result;
}

std::vector<RaceModel> loadRaceModels(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<RaceModel> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "chrracexchrmodel.db2"), dbdDir,
                                            {"ChrRacesID", "Sex", "ChrModelID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    }
    return result;
}

std::vector<ChrModelDisplay> loadChrModels(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<ChrModelDisplay> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "chrmodel.db2"), dbdDir,
                                            {"ID", "DisplayID", "CharComponentTextureLayoutID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    }
    return result;
}

std::vector<CreatureDisplay> loadCreatureDisplays(const std::string& db2Dir, const std::string& dbdDir,
                                                    std::ostream& err) {
    std::vector<CreatureDisplay> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "creaturedisplayinfo.db2"), dbdDir,
                                            {"ID", "ModelID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1])});
    }
    return result;
}

std::vector<CreatureModel> loadCreatureModels(const std::string& db2Dir, const std::string& dbdDir,
                                               std::ostream& err) {
    std::vector<CreatureModel> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "creaturemodeldata.db2"), dbdDir,
                                            {"ID", "FileDataID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1])});
    }
    return result;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    Data data;
    data.races = loadRaces(db2Dir, dbdDir, err);
    data.raceModels = loadRaceModels(db2Dir, dbdDir, err);
    data.chrModels = loadChrModels(db2Dir, dbdDir, err);
    data.creatureDisplays = loadCreatureDisplays(db2Dir, dbdDir, err);
    data.creatureModels = loadCreatureModels(db2Dir, dbdDir, err);
    if (data.races.empty() && data.raceModels.empty() && data.chrModels.empty() &&
        data.creatureDisplays.empty() && data.creatureModels.empty()) {
        return std::nullopt;
    }
    return data;
}

std::optional<ParsedName> parseModelBasename(const std::string& modelPath) {
    std::string stem = toLower(std::filesystem::path(modelPath).stem().string());
    if (endsWith(stem, "_hd")) stem = stem.substr(0, stem.size() - 3);

    // "female" checked first -- "male" is a literal substring of "female",
    // so checking male first would misparse every female model.
    if (endsWith(stem, "female")) {
        return ParsedName{stem.substr(0, stem.size() - 6), 1};
    }
    if (endsWith(stem, "male")) {
        return ParsedName{stem.substr(0, stem.size() - 4), 0};
    }
    return std::nullopt;
}

std::optional<uint32_t> deriveChrModelId(const Data& data, const ParsedName& parsed, std::ostream& err) {
    std::vector<uint32_t> matchingRaceIds;
    for (const auto& race : data.races) {
        if (toLower(race.clientFileString) == parsed.raceToken) matchingRaceIds.push_back(race.id);
    }
    if (matchingRaceIds.empty()) {
        err << "husk: note: --chr-model-id auto: parsed race token '" << parsed.raceToken
            << "' from the model's own filename, but it matched no real ChrRaces.ClientFileString -- "
               "skipping\n";
        return std::nullopt;
    }

    std::set<uint32_t> distinctModelIds;
    for (const auto& rm : data.raceModels) {
        if (rm.sex != parsed.sex) continue;
        if (std::find(matchingRaceIds.begin(), matchingRaceIds.end(), rm.chrRacesId) == matchingRaceIds.end()) {
            continue;
        }
        distinctModelIds.insert(rm.chrModelId);
    }

    if (distinctModelIds.empty()) {
        err << "husk: note: --chr-model-id auto: race token '" << parsed.raceToken << "' (sex "
            << parsed.sex << ") matched a real ChrRaces row, but no ChrRaceXChrModel row -- skipping\n";
        return std::nullopt;
    }
    if (distinctModelIds.size() > 1) {
        err << "husk: note: --chr-model-id auto: race token '" << parsed.raceToken << "' (sex "
            << parsed.sex << ") resolves to " << distinctModelIds.size()
            << " distinct real ChrModelIDs (";
        bool first = true;
        for (uint32_t id : distinctModelIds) {
            if (!first) err << ", ";
            err << id;
            first = false;
        }
        err << ") -- genuinely ambiguous (e.g. an alternate form), not guessing; supply "
               "--chr-model-id <id> directly instead\n";
        return std::nullopt;
    }
    return *distinctModelIds.begin();
}

std::optional<uint32_t> deriveChrModelIdFromFileDataId(const Data& data, uint32_t modelFileDataId,
                                                         std::ostream& err) {
    std::vector<uint32_t> matchingCreatureModelIds;
    for (const auto& cm : data.creatureModels) {
        if (cm.fileDataId == modelFileDataId) matchingCreatureModelIds.push_back(cm.id);
    }
    if (matchingCreatureModelIds.empty()) {
        err << "husk: note: --chr-model-id auto: FileDataID " << modelFileDataId
            << " matched no real CreatureModelData row -- skipping\n";
        return std::nullopt;
    }

    std::vector<uint32_t> matchingDisplayIds;
    for (const auto& cd : data.creatureDisplays) {
        if (std::find(matchingCreatureModelIds.begin(), matchingCreatureModelIds.end(), cd.modelId) !=
            matchingCreatureModelIds.end()) {
            matchingDisplayIds.push_back(cd.id);
        }
    }
    if (matchingDisplayIds.empty()) {
        err << "husk: note: --chr-model-id auto: FileDataID " << modelFileDataId
            << " matched a real CreatureModelData row, but no CreatureDisplayInfo row -- skipping\n";
        return std::nullopt;
    }

    std::set<uint32_t> distinctModelIds;
    for (const auto& cmd : data.chrModels) {
        if (std::find(matchingDisplayIds.begin(), matchingDisplayIds.end(), cmd.displayId) !=
            matchingDisplayIds.end()) {
            distinctModelIds.insert(cmd.chrModelId);
        }
    }

    if (distinctModelIds.empty()) {
        err << "husk: note: --chr-model-id auto: FileDataID " << modelFileDataId
            << " resolves to a real CreatureDisplayInfo row, but no ChrModel row (not a real player "
               "character model) -- skipping\n";
        return std::nullopt;
    }
    if (distinctModelIds.size() > 1) {
        err << "husk: note: --chr-model-id auto: FileDataID " << modelFileDataId << " resolves to "
            << distinctModelIds.size() << " distinct real ChrModelIDs (";
        bool first = true;
        for (uint32_t id : distinctModelIds) {
            if (!first) err << ", ";
            err << id;
            first = false;
        }
        err << ") -- genuinely ambiguous, not guessing; supply --chr-model-id <id> directly instead\n";
        return std::nullopt;
    }
    return *distinctModelIds.begin();
}

}  // namespace husk::chrrace
