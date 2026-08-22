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
    uint32_t materialId = 0;  // 0 = "no material element" (real and common, not an error)
    // 0 = this material applies unconditionally. Nonzero = this material
    // only applies when the *other* named ChrCustomizationChoiceID (from a
    // different, related option) is *also* the one currently selected --
    // real, confirmed against local data: e.g. a real "Tiara" Hairstyle
    // choice carries 10 Element rows, each pairing the same
    // ChrCustomizationChoiceID with a *different* RelatedChrCustomizationChoiceID
    // (one of that model's own real Hair Color choices) and its own
    // distinct ChrCustomizationMaterialID -- one dedicated tiara-compatible
    // material per hair color, not 10 simultaneously valid alternatives.
    // See resolveChoice's own doc comment for why this must not be dropped.
    uint32_t relatedChoiceId = 0;
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

// One ChrCustomizationMaterial row -- real: ChrModelTextureTargetID<32>
// TODO/CHAR_TEXTURE_COMPOSITING_TODO.md Stage 3's own material chain
// (ChrCustomizationOption -> _Choice -> _Element.ChrCustomizationMaterialID
// -> this table -> MaterialResourcesID -> TextureFileData.db2's real
// FileDataID, resolved by src/texturefiledata_db2.hpp, deliberately kept
// out of this reader -- same "data access only" split chrmodel_db2.hpp
// already draws for its own tables).
struct Material {
    uint32_t id = 0;
    uint32_t chrModelTextureTargetId = 0;
    uint32_t materialResourcesId = 0;
};

// One ChrCustomizationCategory row -- the real UI section header a group
// of Options is shown under in the in-game character-creation screen
// (e.g. "Face", "Hair", "Body", "Accessories", "Markings"). Client-global,
// not scoped to a ChrModelID -- unlike Option/Choice, the same category
// row is shared across every race/gender.
struct Category {
    uint32_t id = 0;
    std::string name;         // real CategoryName_lang string; empty when unresolved
    uint32_t orderIndex = 0;  // real UI section display order
};

// One ChrCustomizationOption row -- a player-facing choice category, e.g.
// "Skin Color" or "Hair Style", scoped to one real ChrModelID (one
// race+gender combination, same key chrmodel_db2.hpp's own tables use).
struct Option {
    uint32_t id = 0;
    uint32_t chrModelId = 0;
    std::string name;       // real Name_lang string; empty when unresolved
    uint32_t orderIndex = 0;  // real UI display order within this model's option list
    uint32_t categoryId = 0;  // ChrCustomizationCategoryID; 0 = no real category resolved
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
    std::vector<Option> options;   // empty when chrcustomizationoption.db2 wasn't loadable
    std::vector<Choice> choices;   // empty when chrcustomizationchoice.db2 wasn't loadable
    std::vector<Material> materials;  // empty when chrcustomizationmaterial.db2 wasn't loadable
    std::vector<Category> categories;  // empty when chrcustomizationcategory.db2 wasn't loadable
};

// Loads all seven tables from `db2Dir` (chrcustomizationelement.db2/
// chrcustomizationgeoset.db2/chrcustomizationboneset.db2/
// chrcustomizationoption.db2/chrcustomizationchoice.db2/
// chrcustomizationmaterial.db2/chrcustomizationcategory.db2, real
// lowercase casc-tool filenames).
// Returns nullopt only if every table came back empty. Same per-table
// "missing file/layout leaves that vector empty with a diagnostic, doesn't
// fail the whole load" behavior as chrmodel::load -- `options`/`choices`/
// `materials` staying empty (e.g. those files were never fetched locally)
// does not block `elements`/`geosets`/`boneSets` from loading, and vice
// versa.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

// Resolves one real ChrCustomizationChoiceID against already-loaded `data`.
// Either result field is nullopt when that choice has no such element (real
// and common -- most choices carry only one of geoset/boneset/material/...,
// not all of them) or the referenced target row doesn't exist in `data`
// (a real dangling reference, reported to `err`, not fabricated).
// One real ChrCustomizationMaterial a choice resolves to -- `materialResourcesId`
// is TextureFileData.db2's own join key (src/texturefiledata_db2.hpp), not a
// FileDataID itself; resolving the final FileDataID is left to the caller,
// same "data access only" split as the rest of this file.
struct MaterialResolution {
    uint32_t chrModelTextureTargetId = 0;
    uint32_t materialResourcesId = 0;
    // Element::relatedChoiceId, carried through unchanged -- 0 means this
    // material is unconditional; nonzero means it only applies when that
    // other real ChrCustomizationChoiceID is also selected. resolveChoice
    // does not filter on this (it resolves one choice in isolation and has
    // no notion of "what else is currently selected") -- the caller must,
    // see resolveChoice's own doc comment.
    uint32_t relatedChoiceId = 0;
};

struct Resolution {
    std::optional<uint32_t> geosetId;        // GeosetType*100+GeosetID, ready to compare against M2SkinSection.skinSectionId
    std::optional<uint32_t> boneFileDataId;  // ready to compare against a resolved --bones-dir CorrectionSet::fileDataId
    // A choice can carry more than one ChrCustomizationMaterialID-bearing
    // Element row (real, e.g. a multi-part composite look) -- unlike
    // geosetId/boneFileDataId (each choice only ever carries at most one
    // real value, per ChrCustomizationElement's own "only one of
    // Geoset/SkinnedModel/Material/BoneSet/CondModel is non-0 at a time"
    // documented exclusivity), so this is a vector even when usually one
    // entry long. Empty (not an error) when this choice has no material
    // element at all.
    std::vector<MaterialResolution> materials;
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
    // Resolved from Option::categoryId against Data::categories -- 0/empty
    // when the option carries no real ChrCustomizationCategoryID, or it
    // doesn't match any loaded Category row (chrcustomizationcategory.db2
    // wasn't loadable, or a genuine dangling reference), never guessed.
    uint32_t categoryId = 0;
    std::string categoryName;
    uint32_t categoryOrderIndex = 0;
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
