#include "surface_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::surfacedb {

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string typePath = (std::filesystem::path(db2Dir) / "terraintype.db2").string();
    std::string soundPath = (std::filesystem::path(db2Dir) / "terraintypesounds.db2").string();
    auto types = db2table::readNamedColumns(typePath, dbdDir, {"ID", "FootstepSprayRun", "FootstepSprayWalk", "SoundID", "Flags"}, err);
    auto typeNames = db2table::readNamedStringColumns(typePath, dbdDir, {"TerrainDesc"}, err);
    auto sounds = db2table::readNamedColumns(soundPath, dbdDir, {"ID"}, err);
    auto soundNames = db2table::readNamedStringColumns(soundPath, dbdDir, {"Name"}, err);
    if (!types || !typeNames || !sounds || !soundNames) return std::nullopt;
    if (types->size() != typeNames->size() || sounds->size() != soundNames->size()) {
        err << "surface db2: column reads disagree on row count\n";
        return std::nullopt;
    }
    std::unordered_map<uint32_t, std::string> soundName;
    for (size_t r = 0; r < sounds->size(); ++r) soundName[(*sounds)[r][0].value_or(0)] = (*soundNames)[r][0].value_or("");

    Data out;
    for (size_t r = 0; r < types->size(); ++r) {
        const auto& row = (*types)[r];
        TerrainType t;
        t.id = row[0].value_or(0);
        t.name = (*typeNames)[r][0].value_or("");
        t.footstepSprayRun = row[1].value_or(0);
        t.footstepSprayWalk = row[2].value_or(0);
        t.soundId = row[3].value_or(0);
        t.flags = row[4].value_or(0);
        if (auto found = soundName.find(t.soundId); found != soundName.end()) t.soundName = found->second;
        out.types[t.id] = std::move(t);
    }
    return out;
}

}  // namespace husk::surfacedb
