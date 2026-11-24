#include "wmo_area_db2.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

#include "db2table.hpp"

namespace husk::wmoareadb {

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string path = (std::filesystem::path(db2Dir) / "wmoareatable.db2").string();
    auto rows = db2table::readNamedColumns(path, dbdDir, {"ID", "WMOID", "NameSetID", "WMOGroupID", "AreaTableID"}, err);
    auto names = db2table::readNamedStringColumns(path, dbdDir, {"AreaName_lang"}, err);
    if (!rows || !names) return std::nullopt;
    if (rows->size() != names->size()) {
        err << "wmoareatable.db2: column reads disagree on row count (" << rows->size() << ", " << names->size() << ")\n";
        return std::nullopt;
    }
    Data out;
    for (size_t r = 0; r < rows->size(); ++r) {
        const auto& v = (*rows)[r];
        if (!v[0] || !v[1]) {
            throw std::runtime_error("WMOAreaTable row " + std::to_string(r) + ": expected an ID and a WMOID, got " +
                                     (v[0] ? "an ID only" : v[1] ? "a WMOID only" : "neither"));
        }
        // WMOGroupID is a signed 32-bit column whose -1 is the WMO as a whole.
        Key key{*v[1], v[2].value_or(0), static_cast<int32_t>(v[3].value_or(0))};
        out[key] = Row{*v[0], (*names)[r][0].value_or(""), v[4].value_or(0)};
    }
    return out;
}

}  // namespace husk::wmoareadb
