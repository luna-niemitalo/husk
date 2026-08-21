#include "texturefiledata_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::texturefiledata {

namespace {
std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}
}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "texturefiledata.db2"), dbdDir,
                                            {"FileDataID", "MaterialResourcesID", "UsageType"}, err);
    if (!rows) return std::nullopt;

    Data data;
    for (const auto& row : *rows) {
        const std::optional<uint32_t>& fileDataIdOpt = row[0];
        const std::optional<uint32_t>& materialResourcesIdOpt = row[1];
        const std::optional<uint32_t>& usageTypeOpt = row[2];
        if (!fileDataIdOpt || !materialResourcesIdOpt || !usageTypeOpt) continue;
        if (*usageTypeOpt != 0) continue;  // a real but different-purpose row, see header doc
        data.emplace(*materialResourcesIdOpt, *fileDataIdOpt);
    }
    if (data.empty()) return std::nullopt;
    return data;
}

}  // namespace husk::texturefiledata
