#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Typed reader for the creature catalog (REFACTOR/BUNDLE_FORMAT.md
// "Creature catalogs"): Creature (name, title, type, family, displays),
// CreatureType and CreatureFamily (names), and the two rows a display needs
// to name its model, CreatureDisplayInfo -> CreatureModelData. Same
// db2table.hpp-backed pattern as area_db2.hpp. creature_display_db2.hpp
// reads the same display chain for one model's options and presets; this
// reader serves the corpus-wide catalog and reads only what it needs.
namespace husk::creaturedb {

struct Creature {
    uint32_t id = 0;
    std::string name;     // Name_lang; empty = none
    std::string title;    // Title_lang, e.g. "Innkeeper"; empty = none
    uint32_t type = 0;    // CreatureType row; 0 = none
    uint32_t family = 0;  // CreatureFamily row; 0 = none
    uint32_t classification = 0;  // Classification, a client enum with no table
    std::array<uint32_t, 4> displays{};  // DisplayID[4]; 0 = slot unused
    std::array<float, 4> weights{};      // DisplayProbability[4], as stored
};

struct Display {
    uint32_t modelData = 0;  // CreatureModelData row (ModelID)
    float scale = 1;         // CreatureModelScale
    int32_t sizeClass = 0;   // SizeClass, a client enum with no table
    // Rows of tables husk doesn't read, as stored; 0 = none.
    uint32_t objectEffectPackage = 0;      // ObjectEffectPackageID
    uint32_t stateSpellVisualKit = 0;      // StateSpellVisualKitID
    uint32_t animReplacementSet = 0;       // AnimReplacementSetID
    uint32_t dissolveEffect = 0;           // DissolveEffectID
    uint32_t mountPoofSpellVisualKit = 0;  // MountPoofSpellVisualKitID
};

struct ModelData {
    uint32_t fileDataId = 0;  // the .m2
    float scale = 1;          // ModelScale
};

struct Data {
    std::vector<Creature> creatures;  // ascending by id
    std::unordered_map<uint32_t, std::string> types;     // CreatureType row -> Name_lang
    std::unordered_map<uint32_t, std::string> families;  // CreatureFamily row -> Name_lang
    std::unordered_map<uint32_t, Display> displays;
    std::unordered_map<uint32_t, ModelData> models;
};

// IN, critical: loads the five tables from `db2Dir`. nullopt, with the
// reason on `err`, when a file or its layout can't be read. Throws
// std::runtime_error on a row without an ID or a non-finite scale or weight.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::creaturedb
