#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

// Typed reader for the tables that key map-wide volumes by id: Particulate
// (a _mpv.wdt volume's PlayerCondition; 69 of its 79 12.1.0 rows are volume
// ids, each on its own map) and VolumeFogCondition (a _fogs.wdt fog's
// PlayerCondition and two values; 64 of 65 rows are fog ids). What the
// condition does to the volume, and what WhenFalse/WhenTrue scale, is not
// established. Same db2table.hpp-backed pattern as liquid_db2.hpp.
namespace husk::volumecond {

struct FogCondition {
    uint32_t playerCondition = 0;  // 0 = none
    float whenFalse = 0;
    float whenTrue = 0;  // equal to whenFalse on every 12.1.0 row
};

struct Data {
    std::unordered_map<uint32_t, uint32_t> particulateConditions;  // volume id -> PlayerCondition (0 = none)
    std::unordered_map<uint32_t, FogCondition> fogConditions;      // VFOG id -> its row
};

// Loads particulate.db2 and volumefogcondition.db2 from `db2Dir`. nullopt,
// with the reason on `err`, when a file or its layout can't be read.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::volumecond
