#include "texturefiledata_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::texturefiledata {

namespace {
std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}
}  // namespace

std::optional<Candidates> loadCandidates(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "texturefiledata.db2"), dbdDir,
                                            {"FileDataID", "MaterialResourcesID", "UsageType"}, err);
    if (!rows) return std::nullopt;

    Candidates candidates;
    for (const auto& row : *rows) {
        const std::optional<uint32_t>& fileDataIdOpt = row[0];
        const std::optional<uint32_t>& materialResourcesIdOpt = row[1];
        const std::optional<uint32_t>& usageTypeOpt = row[2];
        if (!fileDataIdOpt || !materialResourcesIdOpt || !usageTypeOpt) continue;
        if (*usageTypeOpt != 0) continue;  // a real but different-purpose row, see header doc
        candidates[*materialResourcesIdOpt].push_back(*fileDataIdOpt);
    }
    if (candidates.empty()) return std::nullopt;
    return candidates;
}

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::optional<Candidates> candidates = loadCandidates(db2Dir, dbdDir, err);
    if (!candidates) return std::nullopt;
    Data data;
    for (const auto& [materialResourcesId, files] : *candidates) data.emplace(materialResourcesId, files.front());
    return data;
}

}  // namespace husk::texturefiledata
