#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Typed reader for AreaTable: each area's name, the area it lies in, and its
// flag words. Consumed by loadWorld (cmd_world_shared.cpp) to name and nest
// the world document's zones; same db2table.hpp-backed pattern as
// liquid_db2.hpp.
namespace husk::areadb {

struct Row {
    std::string name;     // AreaName_lang; empty when the row has none
    uint32_t parent = 0;  // ParentAreaID; 0 = a top-level area
    // Flags<32>[2] and MountFlags<32>, as stored; nullopt when the layout
    // has no such column (older layouts carry one Flags word).
    std::optional<std::array<uint32_t, 2>> flags;
    std::optional<uint32_t> mountFlags;
};

using Data = std::unordered_map<uint32_t, Row>;

// Loads areatable.db2 from `db2Dir`. nullopt, with the reason on `err`, when
// the file or its layout can't be read. Throws std::runtime_error on a row
// without an ID, or a Flags array that isn't two words.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// The names of the bits set in `flags` and `mountFlags`, low bit first,
// Flags[0] then Flags[1] then MountFlags. Names are wowdev.wiki DB/AreaTable's
// 10.2 tables (Area Flags, Area Flags2, Area Mount Flags), the newest the
// wiki has; a set bit those tables don't name is left out (the raw words
// still carry it).
std::vector<std::string> flagNames(const std::array<uint32_t, 2>& flags, uint32_t mountFlags);

}  // namespace husk::areadb
