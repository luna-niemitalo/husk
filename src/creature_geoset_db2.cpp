#include "creature_geoset_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::creaturegeoset {

namespace {

uint32_t orZero(const std::optional<uint32_t>& v) { return v.value_or(0); }

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    auto path = (std::filesystem::path(db2Dir) / "creaturedisplayinfogeosetdata.db2").string();
    auto rows =
        db2table::readNamedColumns(path, dbdDir, {"CreatureDisplayInfoID", "GeosetIndex", "GeosetValue"}, err);
    if (!rows) return std::nullopt;

    Data data;
    data.entries.reserve(rows->size());
    for (const auto& row : *rows) {
        data.entries.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    }
    return data;
}

std::vector<ResolvedGeoset> resolveDisplay(const Data& data, uint32_t displayId) {
    std::vector<ResolvedGeoset> result;
    for (const auto& e : data.entries) {
        if (e.creatureDisplayInfoId != displayId) continue;
        result.push_back({e.geosetIndex, e.geosetValue, (e.geosetIndex + 1) * 100 + e.geosetValue});
    }
    return result;
}

}  // namespace husk::creaturegeoset
