#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Real, typed reader for AnimationData.db2 -- WoW's own table of human-
// readable animation names ("Stand", "Walk", "Death1H", ...), keyed by the
// same M2Sequence::id every husk animation clip already carries. Same
// db2table.hpp-backed thin-wrapper pattern as chrrace_db2.hpp/
// chrcustomization_db2.hpp -- data access only, no naming decisions made
// here (writeCanonBundleOnly, cmd_export_canon.cpp, names the clips).
namespace husk::animationdata {

struct Entry {
    uint32_t id = 0;
    std::string name;  // AnimationData.Name; empty where the layout has no Name (12.x)
    // AnimationData.Fallback: the animation a model without this one plays
    // instead (an AnimationData ID); 0 = Stand.
    uint32_t fallback = 0;
};

struct Data {
    std::vector<Entry> entries;
};

// Loads animationdata.db2 (real lowercase casc-tool filename) from
// `db2Dir`. Returns nullopt when the file is missing/empty/unreadable --
// same "nothing to offer, not a guess" convention as every other loader
// here; callers fall back to husk's own numeric clip naming.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// `data.entries` reshaped into an id -> name lookup. A row with an empty real Name (rare, but not
// impossible -- some internal-only sequences carry no real string) is
// simply absent from the map, same as if AnimationData.db2 didn't resolve
// it at all.
std::unordered_map<uint32_t, std::string> toNameMap(const Data& data);

// id -> Fallback for every row whose Fallback isn't 0, ascending by id.
std::vector<std::pair<uint32_t, uint32_t>> nonzeroFallbacks(const Data& data);

}  // namespace husk::animationdata
