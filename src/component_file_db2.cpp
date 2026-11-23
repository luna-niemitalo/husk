#include "component_file_db2.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>

#include "db2table.hpp"

namespace husk::componentfile {

namespace {

constexpr uint32_t kModelAnyGender = 2;
constexpr uint32_t kTextureAnyGender = 3;

std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}

std::unordered_map<uint32_t, Tag> loadTags(const std::string& path, const std::string& dbdDir, bool hasPosition,
                                           std::ostream& err) {
    std::unordered_map<uint32_t, Tag> tags;
    std::vector<std::string> columns = {"ID", "RaceID", "GenderIndex", "ClassID"};
    if (hasPosition) columns.push_back("PositionIndex");
    auto rows = db2table::readNamedColumns(path, dbdDir, columns, err);
    if (!rows) return tags;
    for (const auto& row : *rows) {
        if (!row[0]) continue;
        Tag tag{row[1].value_or(0), row[2].value_or(0), row[3].value_or(0), -1};
        if (hasPosition && row[4]) tag.positionIndex = static_cast<int32_t>(*row[4]);
        tags[*row[0]] = tag;
    }
    return tags;
}

// ChrRaces stores "no fallback sex" as -1 in an 8-bit field.
RaceFallback fallback(const std::optional<uint32_t>& race, const std::optional<uint32_t>& sex) {
    RaceFallback f;
    f.raceId = race.value_or(0);
    if (sex && (*sex == 0 || *sex == 1)) f.sex = *sex;
    return f;
}

std::unordered_map<uint32_t, RaceFallbacks> loadRaces(const std::string& db2Dir, const std::string& dbdDir,
                                                      std::ostream& err) {
    std::unordered_map<uint32_t, RaceFallbacks> races;
    auto rows = db2table::readNamedColumns(
        joinPath(db2Dir, "chrraces.db2"), dbdDir,
        {"ID", "MaleModelFallbackRaceID", "MaleModelFallbackSex", "FemaleModelFallbackRaceID",
         "FemaleModelFallbackSex", "MaleTextureFallbackRaceID", "MaleTextureFallbackSex",
         "FemaleTextureFallbackRaceID", "FemaleTextureFallbackSex"},
        err);
    if (!rows) return races;
    for (const auto& row : *rows) {
        if (!row[0]) continue;
        RaceFallbacks f;
        f.model[0] = fallback(row[1], row[2]);
        f.model[1] = fallback(row[3], row[4]);
        f.texture[0] = fallback(row[5], row[6]);
        f.texture[1] = fallback(row[7], row[8]);
        races[*row[0]] = f;
    }
    return races;
}

std::string joined(const std::set<uint32_t>& ids) {
    std::ostringstream out;
    for (uint32_t id : ids) out << (out.tellp() > 0 ? ", " : "") << id;
    return out.str();
}

Pick pickOne(const Data& data, Kind kind, const std::vector<uint32_t>& files, const Wearer& wearer, uint32_t side) {
    const auto& tags = kind == Kind::Model ? data.models : data.textures;
    const uint32_t anyGender = kind == Kind::Model ? kModelAnyGender : kTextureAnyGender;

    struct Level {
        uint32_t race;
        uint32_t sex;
    };
    std::vector<Level> levels{{wearer.raceId, wearer.sex}};
    if (auto race = data.races.find(wearer.raceId); race != data.races.end()) {
        const RaceFallback& f = kind == Kind::Model ? race->second.model[wearer.sex] : race->second.texture[wearer.sex];
        if (f.raceId != 0) levels.push_back({f.raceId, f.sex.value_or(wearer.sex)});
    }
    levels.push_back({0, wearer.sex});

    for (const Level& level : levels) {
        for (uint32_t gender : {level.sex, anyGender}) {
            std::set<uint32_t> fits;
            for (uint32_t file : files) {
                auto tag = tags.find(file);
                if (tag == tags.end()) continue;
                const Tag& t = tag->second;
                if (t.classId != 0 || t.raceId != level.race || t.genderIndex != gender) continue;
                if (kind == Kind::Model && t.positionIndex != -1 && t.positionIndex != static_cast<int32_t>(side)) continue;
                fits.insert(file);
            }
            if (fits.size() == 1) return {*fits.begin(), ""};
            if (fits.size() > 1) {
                return {std::nullopt, "files " + joined(fits) + " all fit race " + std::to_string(level.race) +
                                          " gender " + std::to_string(gender)};
            }
        }
    }
    return {std::nullopt, "no file fits race " + std::to_string(wearer.raceId) + " sex " +
                              std::to_string(wearer.sex) + ", its fallback race, or any race"};
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    Data data;
    data.models = loadTags(joinPath(db2Dir, "componentmodelfiledata.db2"), dbdDir, true, err);
    data.textures = loadTags(joinPath(db2Dir, "componenttexturefiledata.db2"), dbdDir, false, err);
    data.races = loadRaces(db2Dir, dbdDir, err);
    if (data.models.empty() && data.textures.empty()) return std::nullopt;
    return data;
}

Pick pick(const Data& data, Kind kind, const std::vector<uint32_t>& files, const std::vector<Wearer>& wearers,
          uint32_t side) {
    if (files.empty()) return {std::nullopt, "the resource lists no file"};
    if (files.size() == 1) return {files.front(), ""};
    if (wearers.empty()) {
        return {std::nullopt, std::to_string(files.size()) + " files by race and sex; the wearer's race and sex are unknown"};
    }
    std::optional<Pick> agreed;
    for (const Wearer& wearer : wearers) {
        Pick p = pickOne(data, kind, files, wearer, side);
        if (!p.fileDataId) return p;
        if (agreed && agreed->fileDataId != p.fileDataId) {
            return {std::nullopt, "the wearer's race is one of several, and they draw different files"};
        }
        agreed = p;
    }
    return *agreed;
}

}  // namespace husk::componentfile
