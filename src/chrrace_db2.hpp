#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// Real, typed reader for the DB2 chain that derives a real ChrModelID from
// a character .m2's own filename, using WoW's own real client-side naming
// convention (verified against real local data, e.g.
// test_data/character/bloodelf/female/bloodelffemale_hd.m2): a basename of
// the form <ClientFileString><"male"|"female">[_hd].m2, case-insensitive.
//
// Same db2table.hpp-backed thin-wrapper pattern as chrmodel_db2.hpp/
// chrcustomization_db2.hpp. Deliberately an *exact*, case-insensitive match
// only (Luna, 2026-08-20): "if user opens file 'dracthyrfemale' match it to
// a dracthyr female... if the user opens file 'dwagon_biddies_69' that's
// not gonna match dracthyr female no matter how hard you try". No fuzzy/
// substring matching -- a basename that doesn't parse into a real
// <race><sex> shape, or a race token that doesn't exactly match a real
// ChrRaces.ClientFileString, derives nothing rather than guessing.
//
// Real, verified-against-local-data wrinkle the race+sex path must handle:
// a (race token, sex) pair is NOT always 1:1 with ChrModelID. Two real,
// distinct causes, both confirmed against local data:
//   - a shared model across two ChrRaces rows with the same
//     ClientFileString (Alliance/Horde Dracthyr, IDs 52/70, both
//     "Dracthyr") -- harmless, both resolve to the SAME ChrModelID, so
//     this collapses to one real answer;
//   - a genuinely different alternate form under the SAME race+sex --
//     Dracthyr male's real available models are ChrModelID 127
//     ("dracthyrmale.m2", ChrModel.Sex=0) *and* 89
//     ("dracthyrdragon.m2", ChrModel.Sex=3 -- shared, no sex in the name
//     at all, playable by either sex via in-game transformation), so
//     "Dracthyr" + "male" alone really is genuinely ambiguous between the
//     two. deriveChrModelId (race+sex path) only returns a value when
//     every matching ChrRaceXChrModel row collapses to one distinct
//     ChrModelID -- genuine ambiguity is reported and left unresolved,
//     never guessed at.
//
// A second, more precise path exists for exactly this case:
// deriveChrModelIdFromFileDataId resolves the input .m2's own real
// FileDataID directly -- CreatureModelData.FileDataID ->
// CreatureDisplayInfo.ModelID -> ChrModel.DisplayID -- which is never
// ambiguous for a real file (verified: dracthyrmale.m2's own FileDataID
// 4395382 resolves to exactly ChrModelID 127, not 89, cleanly separating
// the two real Dracthyr models the race+sex path alone can't tell apart).
// Requires knowing the model's own FileDataID, which this module doesn't
// derive itself (see cmd_export.cpp's caller, which resolves it via
// --listfile the same way texture resolution already does) -- callers
// should prefer this path when a FileDataID is available, falling back
// to the race+sex path (parseModelBasename/deriveChrModelId) only when
// it isn't.
namespace husk::chrrace {

struct Race {
    uint32_t id = 0;
    std::string clientFileString;  // real client-side path token, e.g. "Dracthyr", "BloodElf"
};

struct RaceModel {
    uint32_t chrRacesId = 0;
    uint32_t sex = 0;  // 0 = male, 1 = female (real ChrRaceXChrModel convention)
    uint32_t chrModelId = 0;
};

// One ChrModel.db2 row's identity link -- which real CreatureDisplayInfoID
// this ChrModelID renders as.
struct ChrModelDisplay {
    uint32_t chrModelId = 0;
    uint32_t displayId = 0;
};

// One CreatureDisplayInfo.db2 row's identity link -- which real
// CreatureModelDataID this display uses.
struct CreatureDisplay {
    uint32_t id = 0;
    uint32_t modelId = 0;
};

// One CreatureModelData.db2 row's identity link -- the real FileDataID of
// the .m2 this model entry renders.
struct CreatureModel {
    uint32_t id = 0;
    uint32_t fileDataId = 0;
};

struct Data {
    std::vector<Race> races;
    std::vector<RaceModel> raceModels;
    std::vector<ChrModelDisplay> chrModels;
    std::vector<CreatureDisplay> creatureDisplays;
    std::vector<CreatureModel> creatureModels;
};

// Loads chrraces.db2/chrracexchrmodel.db2/chrmodel.db2/
// creaturedisplayinfo.db2/creaturemodeldata.db2 (real lowercase casc-tool
// filenames) from `db2Dir`. Returns nullopt only if every table came back
// empty -- same per-table "missing file/layout leaves that vector empty
// with a diagnostic, doesn't fail the whole load" behavior as every other
// reader here.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// Parses `modelPath`'s own basename (stem, case-insensitive, an optional
// trailing "_hd" stripped first) into a (race token, sex) pair per the real
// <ClientFileString><"male"|"female"> convention. Returns nullopt when the
// basename doesn't end in "male"/"female" at all -- not a report-worthy
// failure, most .m2 files (creatures, world objects, weapons) were never
// going to match this convention in the first place.
struct ParsedName {
    std::string raceToken;  // lowercased
    uint32_t sex = 0;
};
std::optional<ParsedName> parseModelBasename(const std::string& modelPath);

// Resolves a parsed (race token, sex) against already-loaded `data`: every
// ChrRaces row whose ClientFileString case-insensitively equals raceToken,
// joined against ChrRaceXChrModel for that sex. Returns the single
// ChrModelID only when every match collapses to one distinct value (see
// this file's own module comment for the two real cases that can produce
// more than one candidate, and why that's reported, not resolved, here).
std::optional<uint32_t> deriveChrModelId(const Data& data, const ParsedName& parsed, std::ostream& err);

// Resolves `modelFileDataId` (the input .m2's own real FileDataID, from
// the caller -- this module has no way to derive it itself) through the
// real CreatureModelData -> CreatureDisplayInfo -> ChrModel chain. Never
// ambiguous for a real, single-purpose character model file (verified:
// dracthyrmale.m2's own FileDataID resolves to exactly one ChrModelID,
// cleanly distinguishing it from dracthyrdragon.m2's shared model despite
// both being valid answers to "Dracthyr, male" alone) -- but still reports
// and returns nullopt rather than guessing if the real chain does yield
// more than one distinct ChrModelID, or none at all (e.g. a non-player
// creature FileDataID with no ChrModel entry).
std::optional<uint32_t> deriveChrModelIdFromFileDataId(const Data& data, uint32_t modelFileDataId,
                                                         std::ostream& err);

}  // namespace husk::chrrace
