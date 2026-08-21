#include "modelfiledata_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::modelfiledata {

namespace {
std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}
}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "modelfiledata.db2"), dbdDir,
                                            {"FileDataID", "ModelResourcesID"}, err);
    if (!rows) return std::nullopt;

    Data data;
    for (const auto& row : *rows) {
        const std::optional<uint32_t>& fileDataIdOpt = row[0];
        const std::optional<uint32_t>& modelResourcesIdOpt = row[1];
        if (!fileDataIdOpt || !modelResourcesIdOpt) continue;
        data[*modelResourcesIdOpt].push_back(*fileDataIdOpt);
    }
    if (data.empty()) return std::nullopt;
    return data;
}

}  // namespace husk::modelfiledata
