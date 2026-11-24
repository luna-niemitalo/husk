#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>

// Typed reader for Map: each map's name, directory, kind and the maps it
// hangs under. Consumed by the world and tile documents' map identity
// (cmd_world_shared.cpp); same db2table.hpp-backed pattern as area_db2.hpp.
namespace husk::mapdb {

struct Row {
    uint32_t id = 0;
    std::string name;       // MapName_lang; empty when the row has none
    std::string directory;  // Directory, as stored (World\Maps\<Directory>\)
    int32_t instanceType = 0;
    uint32_t mapType = 0;
    uint32_t expansion = 0;  // ExpansionID
    uint32_t maxPlayers = 0;
    std::optional<uint32_t> parent;          // ParentMapID; nullopt when stored as -1
    std::optional<uint32_t> cosmeticParent;  // CosmeticParentMapID; nullopt when stored as -1
    uint32_t areaTableId = 0;                // 0: none
};

struct Data {
    std::unordered_map<uint32_t, Row> rows;
    std::unordered_map<std::string, uint32_t> idByDirectory;  // Directory lowercased
};

// Loads map.db2 from `db2Dir`. nullopt, with the reason on `err`, when the
// file or its layout can't be read. Throws std::runtime_error on a row
// without an ID.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// The row whose Directory is `directory`, case ignored (map directories on
// disk are lowercased by the extraction); nullptr when none.
const Row* findByDirectory(const Data& data, std::string_view directory);

// InstanceType's name per the WoWDBDefs Map.dbd column comment (0 none,
// 1 party, 2 raid, 3 pvp, 4 arena); nullopt for any other value, which the
// raw value still carries.
std::optional<std::string> instanceTypeName(int32_t instanceType);

}  // namespace husk::mapdb
