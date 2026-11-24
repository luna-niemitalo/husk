#pragma once

#include "canon_creatures.hpp"
#include "creature_db2.hpp"

// The creature catalog from the creature tables: every Creature row, and
// every display one names, with the model its CreatureModelData row draws.
// Presets are not known here: they are what the model documents chose
// (cmd_export_creatures.cpp fills them from the tree's index).
namespace husk::creaturecatalog {

// Ids are husk::nameUuid of "creature/Creature/<row>",
// "creature_display/CreatureDisplayInfo/<row>" and
// "model/file_data_id/<fdid>", so a re-export keeps them. Model refs carry
// no name; the command names them from its listfile.
canon::CreatureCatalog assemble(const creaturedb::Data& data);

}  // namespace husk::creaturecatalog
