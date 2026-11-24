#pragma once

#include <ostream>
#include <vector>

#include "appearance_string.hpp"
#include "canon_item.hpp"
#include "component_file_db2.hpp"
#include "itemappearance_db2.hpp"
#include "modelfiledata_db2.hpp"
#include "texturefiledata_db2.hpp"

// husk::canon: assembly of canon::Item (I7, "items by identity, not slot")
// from the real itemappearance:: data-access layer (itemappearance_db2.hpp).
//
// Deliberately reuses itemappearance::resolve directly rather than
// re-walking itemappearance::Data itself -- that function already IS the
// real DB2 join chain; this file's own job is purely grouping+shaping its
// per-call Resolution into canon::Item's identity-first shape.
//
// One item can be equipped at two slots (a real, structurally valid
// appearance_string.hpp `gear=SLOT:id,SLOT:id` input), so
// assembleEquippedItems groups by itemModifiedAppearanceId first: entries
// sharing one appearance id collapse into ONE canon::Item with multiple
// Item::slots, its components resolved exactly once -- not once per
// occurrence.
namespace husk::canon {

// The tables past ItemDisplayInfo, each optional: a missing one leaves its
// files unpicked, never a hard failure. `wearers`: who wears the gear
// (componentfile::pick); empty when unknown.
struct GearSources {
    const modelfiledata::Data* models = nullptr;
    const texturefiledata::Candidates* textures = nullptr;
    const componentfile::Data* components = nullptr;
    std::vector<componentfile::Wearer> wearers;
};

std::vector<Item> assembleEquippedItems(const itemappearance::Data& data, const GearSources& sources,
                                         const std::vector<appearance::GearEntry>& gear, std::ostream& err);

}  // namespace husk::canon
