#pragma once

#include <cstdint>

#include "canon_selection.hpp"
#include "creature_geoset_db2.hpp"

// husk::canon: assembly of one canon::CreatureSelection from the real
// creaturegeoset:: data-access layer (creature_geoset_db2.hpp) -- the
// adjacent twin of export_extras.cpp's own attachCreatureGeosets loop
// (cmd_export.cpp/export_extras.cpp), built to prove structural convergence
// (REFACTOR/README.md stage 3's gate) without touching the existing
// pipeline.
//
// Deliberately thin: creaturegeoset::resolveDisplay already performs the
// real DB2 join and the (GeosetIndex+1)*100+GeosetValue geosetId formula --
// this function only shapes its flat ResolvedGeoset list into
// CreatureSelection::enabledGeosetIds. No sort/dedupe here, matching
// attachCreatureGeosets's own behavior of pushing every resolved geosetId
// through unchanged, in resolveDisplay's own order.
namespace husk::canon {

CreatureSelection assembleCreatureSelection(const creaturegeoset::Data& data, uint32_t creatureDisplayId);

// Assembly of one canon::CharacterSelection from the pieces its own two
// real inputs already resolve elsewhere -- the adjacent twin of
// export_extras.cpp's own attachCustomizationChoices (chosenChoiceIds) and
// canon::assembleEquippedItems (equippedItems), composed here rather than
// re-deriving either.
//
// Deliberately thin: attachCustomizationChoices uses its own parsed
// --customization-choice-ids list as-is, with no filtering/dedup/sort
// before attaching it, so this function does the same for chosenChoices.
// equippedItems is reduced from canon::Item to CharacterSelection::
// EquippedItem by dropping Item::components -- Resources-layer data a
// consumer re-joins against the real canon::Item list, not Selection's job
// to carry twice (see CharacterSelection::EquippedItem's own doc comment).
CharacterSelection assembleCharacterSelection(uint32_t chrModelId, const std::vector<uint32_t>& chosenChoiceIds,
                                               const std::vector<Item>& equippedItems);

}  // namespace husk::canon
