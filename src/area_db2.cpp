#include "area_db2.hpp"

#include <filesystem>
#include <stdexcept>

#include "db2table.hpp"

namespace husk::areadb {

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string path = (std::filesystem::path(db2Dir) / "areatable.db2").string();
    auto rows = db2table::readNamedColumns(path, dbdDir, {"ID", "ParentAreaID"}, err);
    auto names = db2table::readNamedStringColumns(path, dbdDir, {"AreaName_lang"}, err);
    if (!rows || !names) return std::nullopt;
    if (rows->size() != names->size()) {
        err << "areatable.db2: column reads disagree on row count (" << rows->size() << ", " << names->size() << ")\n";
        return std::nullopt;
    }
    Data out;
    for (size_t r = 0; r < rows->size(); ++r) {
        const auto& id = (*rows)[r][0];
        if (!id) throw std::runtime_error("AreaTable.ID: expected a value in row " + std::to_string(r) + ", got none");
        out[*id] = {(*names)[r][0].value_or(""), (*rows)[r][1].value_or(0)};
    }
    return out;
}

}  // namespace husk::areadb
