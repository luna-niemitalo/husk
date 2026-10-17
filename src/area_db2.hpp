#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

// Typed reader for AreaTable: each area's name and the area it lies in.
// Consumed by loadWorld (cmd_world_shared.cpp) to name and nest the world
// document's zones; same db2table.hpp-backed pattern as liquid_db2.hpp.
namespace husk::areadb {

struct Row {
    std::string name;     // AreaName_lang; empty when the row has none
    uint32_t parent = 0;  // ParentAreaID; 0 = a top-level area
};

using Data = std::unordered_map<uint32_t, Row>;

// Loads areatable.db2 from `db2Dir`. nullopt, with the reason on `err`, when
// the file or its layout can't be read. Throws std::runtime_error on a row
// without an ID.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::areadb
