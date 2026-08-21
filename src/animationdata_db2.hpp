#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Real, typed reader for AnimationData.db2 -- WoW's own table of human-
// readable animation names ("Stand", "Walk", "Death1H", ...), keyed by the
// same M2Sequence::id every husk animation clip already carries (see
// export_animation.cpp's "anim_<id>_<variationIndex>" naming). Same
// db2table.hpp-backed thin-wrapper pattern as chrrace_db2.hpp/
// chrcustomization_db2.hpp -- data access only, no naming/enrichment
// decisions made here (see cmd_export.cpp's caller, which decides how a
// resolved name feeds into a clip's own extras).
namespace husk::animationdata {

struct Entry {
    uint32_t id = 0;
    std::string name;  // real AnimationData.Name string; never empty for a real row
};

struct Data {
    std::vector<Entry> entries;
};

// Loads animationdata.db2 (real lowercase casc-tool filename) from
// `db2Dir`. Returns nullopt when the file is missing/empty/unreadable --
// same "nothing to offer, not a guess" convention as every other loader
// here; callers fall back to husk's own numeric clip naming.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// `data.entries` reshaped into an id -> name lookup for buildAnimations'
// own convenience. A row with an empty real Name (rare, but not
// impossible -- some internal-only sequences carry no real string) is
// simply absent from the map, same as if AnimationData.db2 didn't resolve
// it at all.
std::unordered_map<uint32_t, std::string> toNameMap(const Data& data);

}  // namespace husk::animationdata
