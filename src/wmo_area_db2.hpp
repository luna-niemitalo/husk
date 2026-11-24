#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>

// Typed reader for WMOAreaTable: the names the client shows inside a WMO,
// per group, and the AreaTable row each maps to. Consumed by
// wmoinput::buildCanonWmo for each part's area; same db2table.hpp-backed
// pattern as area_db2.hpp.
namespace husk::wmoareadb {

struct Row {
    uint32_t id = 0;
    std::string name;          // AreaName_lang; empty when the row has none
    uint32_t areaTableId = 0;  // 0: none
};

// Keyed by (WMOID, NameSetID, WMOGroupID): WMOID is the root's MOHD wmoID,
// WMOGroupID a group's MOGP groupID (-1: the row for the WMO as a whole),
// NameSetID the placement's MODF nameSet.
using Key = std::tuple<uint32_t, uint32_t, int32_t>;
using Data = std::map<Key, Row>;

// Loads wmoareatable.db2 from `db2Dir`. nullopt, with the reason on `err`,
// when the file or its layout can't be read. Throws std::runtime_error on a
// row without an ID or WMOID.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::wmoareadb
