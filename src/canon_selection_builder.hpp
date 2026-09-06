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

}  // namespace husk::canon
