#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Which of an item resource's files a wearer draws. A ModelResourcesID or
// MaterialResourcesID lists one file per race/sex variant (ModelFileData,
// TextureFileData); ComponentModelFileData and ComponentTextureFileData tag
// each such file with the race, sex, class and (models only) shoulder side
// it is for. In 12.1.5, 3,073 model resources list several files and all
// but 6 of their 59,388 files carry a tag; all 17,157 files of the 8,480
// multi-file texture resources do.
//
// GenderIndex differs per table (wow.export DBComponent*FileData.js):
// models 0 male, 1 female, 2 any; textures 0 male, 1 female, 2 none,
// 3 any. ChrRaces names a model and a texture fallback race and sex per
// race and sex (Nightborne -> Night Elf, Void Elf -> Blood Elf).
//
// IN, non-critical: every table is optional; a table that doesn't load
// leaves its map empty, and a multi-file resource then stays unpicked.
namespace husk::componentfile {

struct Tag {
    uint32_t raceId = 0;  // 0 = any race
    uint32_t genderIndex = 0;
    uint32_t classId = 0;  // 0 = any class
    int32_t positionIndex = -1;  // models: 0 left, 1 right, -1 either
};

struct RaceFallback {
    uint32_t raceId = 0;  // 0 = none
    std::optional<uint32_t> sex;  // absent: the wearer's own
};

// Per ChrRaces row: index 0 male, 1 female.
struct RaceFallbacks {
    RaceFallback model[2];
    RaceFallback texture[2];
};

struct Data {
    std::unordered_map<uint32_t, Tag> models;    // by model FileDataID
    std::unordered_map<uint32_t, Tag> textures;  // by texture FileDataID
    std::unordered_map<uint32_t, RaceFallbacks> races;
};

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

struct Wearer {
    uint32_t raceId = 0;
    uint32_t sex = 0;  // 0 male, 1 female
};

enum class Kind { Model, Texture };

// `fileDataId` set: the file; otherwise `unresolved` says why none was
// chosen. A tie is never broken by order.
struct Pick {
    std::optional<uint32_t> fileDataId;
    std::string unresolved;
};

// Picks among `files` for each of `wearers` (more than one when the
// wearer's race is only known as a set, e.g. the three Pandaren rows):
// the picks must agree. `side`: models only, the item's model position
// (0 left, 1 right), matched against PositionIndex.
//
// One file is that file. Otherwise, over tagged files of class 0 (the
// wearer's class is not known) whose side fits: the wearer's race, then its
// ChrRaces fallback race and sex, then race 0; within each, the wearer's
// sex before "any". The first level holding exactly one file wins; one
// holding several is a tie.
Pick pick(const Data& data, Kind kind, const std::vector<uint32_t>& files, const std::vector<Wearer>& wearers,
          uint32_t side = 0);

}  // namespace husk::componentfile
