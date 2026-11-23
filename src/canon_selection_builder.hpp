#pragma once

#include <cstdint>

#include "canon_selection.hpp"
#include "creature_geoset_db2.hpp"

// husk::canon: assembly of one canon::CreatureSelection from the real
// creaturegeoset:: data-access layer (creature_geoset_db2.hpp).
//
// Deliberately thin: creaturegeoset::resolveDisplay already performs the
// real DB2 join and the (GeosetIndex+1)*100+GeosetValue geosetId formula --
// this function only shapes its flat ResolvedGeoset list into
// CreatureSelection::enabledGeosetIds. No sort/dedupe here: every resolved
// geosetId passes through unchanged, in resolveDisplay's own order.
namespace husk::canon {

CreatureSelection assembleCreatureSelection(const creaturegeoset::Data& data, uint32_t creatureDisplayId);

// Assembly of one canon::CharacterSelection from the pieces its own two
// real inputs already resolve elsewhere -- a parsed choice-id list
// (chosenChoiceIds) and canon::assembleEquippedItems (equippedItems),
// composed here rather than re-deriving either.
//
// Deliberately thin: chosenChoiceIds is used as-is, with no
// filtering/dedup/sort.
// equippedItems is reduced from canon::Item to CharacterSelection::
// EquippedItem by dropping Item::components -- Resources-layer data a
// consumer re-joins against the real canon::Item list, not Selection's job
// to carry twice (see CharacterSelection::EquippedItem's own doc comment).
CharacterSelection assembleCharacterSelection(uint32_t chrModelId, const std::vector<uint32_t>& chosenChoiceIds,
                                               const std::vector<Item>& equippedItems);

}  // namespace husk::canon
