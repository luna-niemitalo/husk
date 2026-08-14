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

struct Data {
    std::vector<Element> elements;
    std::vector<Geoset> geosets;
    std::vector<BoneSet> boneSets;
};

// Loads all three tables from `db2Dir` (chrcustomizationelement.db2/
// chrcustomizationgeoset.db2/chrcustomizationboneset.db2, real lowercase
// casc-tool filenames). Returns nullopt only if every table came back
// empty. Same per-table "missing file/layout leaves that vector empty with
// a diagnostic, doesn't fail the whole load" behavior as chrmodel::load.
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

}  // namespace husk::chrcustomization
