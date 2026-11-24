#include "map_db2.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include "db2table.hpp"

namespace husk::mapdb {

namespace {

std::string lowered(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

// ParentMapID and CosmeticParentMapID are signed 16-bit columns (Map.dbd
// `<16>`) with -1 for "no parent"; the reader may hand back either width of
// the bits, so the low 16 decide.
std::optional<uint32_t> mapRow(const std::optional<uint32_t>& v) {
    if (!v) return std::nullopt;
    int16_t id = static_cast<int16_t>(*v);
    if (id < 0) return std::nullopt;
    return static_cast<uint32_t>(id);
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string path = (std::filesystem::path(db2Dir) / "map.db2").string();
    auto rows = db2table::readNamedColumns(path, dbdDir,
                                           {"ID", "InstanceType", "MapType", "ExpansionID", "MaxPlayers", "ParentMapID",
                                            "CosmeticParentMapID", "AreaTableID"},
                                           err);
    auto strings = db2table::readNamedStringColumns(path, dbdDir, {"Directory", "MapName_lang"}, err);
    if (!rows || !strings) return std::nullopt;
    if (rows->size() != strings->size()) {
        err << "map.db2: column reads disagree on row count (" << rows->size() << ", " << strings->size() << ")\n";
        return std::nullopt;
    }
    Data out;
    for (size_t r = 0; r < rows->size(); ++r) {
        const auto& v = (*rows)[r];
        if (!v[0]) throw std::runtime_error("Map.ID: expected a value in row " + std::to_string(r) + ", got none");
        Row row;
        row.id = *v[0];
        row.directory = (*strings)[r][0].value_or("");
        row.name = (*strings)[r][1].value_or("");
        // Map.dbd: InstanceType<8>, signed.
        row.instanceType = static_cast<int8_t>(v[1].value_or(0));
        row.mapType = v[2].value_or(0);
        row.expansion = v[3].value_or(0);
        row.maxPlayers = v[4].value_or(0);
        row.parent = mapRow(v[5]);
        row.cosmeticParent = mapRow(v[6]);
        row.areaTableId = v[7].value_or(0);
        if (!row.directory.empty()) out.idByDirectory[lowered(row.directory)] = row.id;
        out.rows[row.id] = std::move(row);
    }
    return out;
}

const Row* findByDirectory(const Data& data, std::string_view directory) {
    auto id = data.idByDirectory.find(lowered(directory));
    if (id == data.idByDirectory.end()) return nullptr;
    return &data.rows.at(id->second);
}

std::optional<std::string> instanceTypeName(int32_t instanceType) {
    static const char* const kNames[] = {"none", "party", "raid", "pvp", "arena"};
    if (instanceType < 0 || instanceType >= static_cast<int32_t>(std::size(kNames))) return std::nullopt;
    return kNames[instanceType];
}

}  // namespace husk::mapdb
