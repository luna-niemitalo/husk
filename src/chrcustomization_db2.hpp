#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// Real, typed reader for the DB2 chain that resolves a real
// ChrCustomizationChoiceID (a specific character-customization pick, e.g.
// "Hairstyle 7") to the real geoset/bone-correction-set data it enables --
// TODO/TODO_correctness.md #2. Same db2table.hpp-backed, thin-wrapper
// pattern as chrmodel_db2.hpp.
//
// The real chain, verified against Luna's own local casc-tool extraction,
// not guessed (see chrcustomization_db2.cpp's resolveChoice/load for the
// full verification detail):
//
//   ChrCustomizationChoiceID
//     -> ChrCustomizationElement.ChrCustomizationGeosetID  -> ChrCustomizationGeoset
//          geoset_id = GeosetType * 100 + GeosetID  (same convention husk's
//          own geoset_group/geoset_variant extras already use)
//     -> ChrCustomizationElement.ChrCustomizationBoneSetID -> ChrCustomizationBoneSet
//          .BoneFileDataID  (the real FileDataID of a '.bone' file --
//          exactly what --bones-dir already resolves)
//
// Scope, deliberately: this is the *data access* layer only, same split
// chrmodel_db2.hpp already draws. It does NOT enumerate which
// ChrCustomizationChoiceIDs exist for a given option/race/model -- that
// needs ChrCustomizationOption/ChrCustomizationChoice, both confirmed
// 0 bytes in the current local extraction (a real casc-tool gap, not a
// husk one -- see TODO/CHAR_TEXTURE_COMPOSITING_TODO.md, which documents
// the same gap for its own adjacent customization-choice chain). The
// caller supplies real choice IDs directly, same "hand husk a plain local
// answer, don't make it
// guess" pattern --char-layout-id already established.
namespace husk::chrcustomization {

// One ChrCustomizationElement row's relevant fields -- the table also
// carries ChrCustomizationMaterialID/CondModelID/DisplayInfoID/etc, not
// read here since nothing downstream of this reader consumes them yet.
// Real, verified against local data: one ChrCustomizationChoiceID owns
// *several* Element rows, not one (a real choice can have 15+), each
// typically carrying only one nonzero "which other table" field -- see
// resolveChoice's own doc comment for why every row must be scanned, not
// just the first match.
struct Element {
    uint32_t choiceId = 0;
    uint32_t geosetId = 0;    // 0 = "no geoset element" (real and common, not an error)
    uint32_t boneSetId = 0;   // 0 = "no boneset element" (real and common, not an error)
};

struct Geoset {
    uint32_t id = 0;
    uint32_t geosetType = 0;
    uint32_t geosetId = 0;
};

struct BoneSet {
    uint32_t id = 0;
    uint32_t boneFileDataId = 0;
    uint32_t modelFileDataId = 0;
};

// One ChrCustomizationOption row -- a player-facing choice category, e.g.
// "Skin Color" or "Hair Style", scoped to one real ChrModelID (one
// race+gender combination, same key chrmodel_db2.hpp's own tables use).
struct Option {
    uint32_t id = 0;
    uint32_t chrModelId = 0;
    std::string name;       // real Name_lang string; empty when unresolved
    uint32_t orderIndex = 0;  // real UI display order within this model's option list
};

// One ChrCustomizationChoice row -- one selectable value of an Option, e.g.
// "Long Fin" under "Ears". Many real choices (every color swatch) carry no
// real name at all (Name_lang resolves to the literal string "0") -- not a
// parse failure, the client draws those from SwatchColor instead.
struct Choice {
    uint32_t id = 0;
    uint32_t optionId = 0;
    std::string name;
    uint32_t orderIndex = 0;
};

struct Data {
    std::vector<Element> elements;
    std::vector<Geoset> geosets;
    std::vector<BoneSet> boneSets;
    std::vector<Option> options;  // empty when chrcustomizationoption.db2 wasn't loadable
    std::vector<Choice> choices;  // empty when chrcustomizationchoice.db2 wasn't loadable
};

// Loads all five tables from `db2Dir` (chrcustomizationelement.db2/
// chrcustomizationgeoset.db2/chrcustomizationboneset.db2/
// chrcustomizationoption.db2/chrcustomizationchoice.db2, real lowercase
// casc-tool filenames). Returns nullopt only if every table came back
// empty. Same per-table "missing file/layout leaves that vector empty with
// a diagnostic, doesn't fail the whole load" behavior as chrmodel::load --
// `options`/`choices` staying empty (e.g. those two files were never
// fetched locally) does not block `elements`/`geosets`/`boneSets` from
// loading, and vice versa.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// Resolves one real ChrCustomizationChoiceID against already-loaded `data`.
// Either result field is nullopt when that choice has no such element (real
// and common -- most choices carry only one of geoset/boneset/material/...,
// not all of them) or the referenced target row doesn't exist in `data`
// (a real dangling reference, reported to `err`, not fabricated).
struct Resolution {
    std::optional<uint32_t> geosetId;        // GeosetType*100+GeosetID, ready to compare against M2SkinSection.skinSectionId
    std::optional<uint32_t> boneFileDataId;  // ready to compare against a resolved --bones-dir CorrectionSet::fileDataId
};
Resolution resolveChoice(const Data& data, uint32_t choiceId, std::ostream& err);

// One real (Option name, Choice name) pair for a given ChrModelID, paired
// with what that choice actually resolves to via resolveChoice -- the real
// mapping from human-readable names to husk's own geoset/bone-correction
// selector. Requires `data.options`/`data.choices` to be populated (both
// tables loaded); returns empty when either is missing, same "nothing to
// offer, not a guess" convention as every other loader here.
struct NamedChoice {
    uint32_t optionId = 0;
    std::string optionName;
    uint32_t optionOrderIndex = 0;
    uint32_t choiceId = 0;
    std::string choiceName;
    uint32_t choiceOrderIndex = 0;
    Resolution resolution;
};
std::vector<NamedChoice> namedChoicesForModel(const Data& data, uint32_t chrModelId, std::ostream& err);

// A sensible default choice per option for `chrModelId`: the choice with
// the lowest real OrderIndex within that option (ties broken by lowest
// ChoiceID, deterministic). This mirrors the real character-creation UI's
// own display order -- OrderIndex 0 is the first choice a player sees --
// but is NOT the same guarantee CreatureDisplayInfoGeosetData.db2 gives
// creature_geoset_db2.hpp (a real, authoritative default baked into the
// data itself); no DB2 table states an explicit default for a player
// option, so this is husk's own heuristic, not a client-verified fact.
// Loudly not a substitute for real player-character customization data
// when that's available -- see TODO/TODO_correctness.md #2. Requires
// `data.options`/`data.choices`; returns empty when either is missing.
std::vector<uint32_t> defaultChoiceIdsForModel(const Data& data, uint32_t chrModelId);

}  // namespace husk::chrcustomization
