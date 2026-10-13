#include "volume_condition_db2.hpp"

#include <bit>
#include <filesystem>

#include "db2table.hpp"

namespace husk::volumecond {

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string particulatePath = (std::filesystem::path(db2Dir) / "particulate.db2").string();
    std::string fogPath = (std::filesystem::path(db2Dir) / "volumefogcondition.db2").string();
    auto particulate = db2table::readNamedColumns(particulatePath, dbdDir, {"ID", "PlayerConditionID"}, err);
    auto fogs = db2table::readNamedColumns(fogPath, dbdDir, {"VFOG_uID", "PlayerConditionID", "WhenFalse", "WhenTrue"}, err);
    if (!particulate || !fogs) return std::nullopt;

    Data out;
    for (const auto& row : *particulate) out.particulateConditions[row[0].value_or(0)] = row[1].value_or(0);
    // A row with no VFOG_uID (1 in 12.1.0) names no fog.
    for (const auto& row : *fogs) {
        if (!row[0]) continue;
        out.fogConditions[*row[0]] = {row[1].value_or(0), std::bit_cast<float>(row[2].value_or(0)),
                                      std::bit_cast<float>(row[3].value_or(0))};
    }
    return out;
}

}  // namespace husk::volumecond
