#include "animationdata_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::animationdata {

namespace {

std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string path = joinPath(db2Dir, "animationdata.db2");
    auto idRows = db2table::readNamedColumns(path, dbdDir, {"ID"}, err);
    if (!idRows || idRows->empty()) return std::nullopt;
    auto nameRows = db2table::readNamedStringColumns(path, dbdDir, {"Name"}, err);

    Data data;
    data.entries.reserve(idRows->size());
    for (size_t i = 0; i < idRows->size(); ++i) {
        uint32_t id = (*idRows)[i][0].value_or(0);
        std::string name = (nameRows && i < nameRows->size()) ? (*nameRows)[i][0].value_or("") : "";
        data.entries.push_back({id, std::move(name)});
    }
    return data;
}

std::unordered_map<uint32_t, std::string> toNameMap(const Data& data) {
    std::unordered_map<uint32_t, std::string> result;
    for (const auto& entry : data.entries) {
        if (!entry.name.empty()) {
            result[entry.id] = entry.name;
        }
    }
    return result;
}

}  // namespace husk::animationdata
