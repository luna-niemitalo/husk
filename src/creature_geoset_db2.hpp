#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// Real, typed reader for CreatureDisplayInfoGeosetData.db2 -- the
// authoritative default-geoset selection for a given CreatureDisplayInfoID
// (TODO/TODO_correctness.md's geoset-selection gap, creature half). Unlike
// player characters (chrcustomization_db2.hpp), this *is* a true default:
// no per-choice caller input is needed, the table itself names which
// geosets a given creature display shows.
//
// Same db2table.hpp-backed thin-wrapper pattern as chrmodel_db2.hpp/
// chrcustomization_db2.hpp.
//
// The real geoset-ID formula, cross-checked against reference/wow.export's
// own `DBCreatures.js` (`(GeosetIndex + 1) * 100 + GeosetValue`) --
// deliberately *not* the character convention (`GeosetType*100+GeosetID`,
// chrcustomization_db2.hpp) despite the superficial resemblance; the "+1"
// on GeosetIndex is real and not a typo, matches this project's own
// group*100+variant reading of M2SkinSection.skinSectionId (DESIGN.md's
// geoset-group table) once applied.
//
// Scope, deliberately: data access only, same split as every other db2
// reader here. It does NOT resolve which CreatureDisplayInfoID applies to
// a given .m2 model -- that's the CreatureDisplayInfo.ModelID chain,
// unrelated to this table. The caller supplies the real
// CreatureDisplayInfoID directly, same "hand husk a plain local answer,
// don't make it guess" pattern as --char-layout-id.
namespace husk::creaturegeoset {

struct GeosetEntry {
    uint32_t creatureDisplayInfoId = 0;
    uint32_t geosetIndex = 0;
    uint32_t geosetValue = 0;
};

struct Data {
    std::vector<GeosetEntry> entries;
};

// Loads creaturedisplayinfogeosetdata.db2 (real lowercase casc-tool
// filename) from `db2Dir`. Returns nullopt only if the file/layout can't be
// read at all -- same "nothing to offer" convention as every other loader
// here.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// Every entry in `data` whose CreatureDisplayInfoID matches `displayId`,
// each resolved to its real geoset ID via the (GeosetIndex+1)*100+GeosetValue
// formula. Empty (not an error) when this display has no rows -- most
// creature displays use none of their model's optional geosets at all.
struct ResolvedGeoset {
    uint32_t geosetIndex = 0;
    uint32_t geosetValue = 0;
    uint32_t geosetId = 0;
};
std::vector<ResolvedGeoset> resolveDisplay(const Data& data, uint32_t displayId);

}  // namespace husk::creaturegeoset
