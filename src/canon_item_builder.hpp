#pragma once

#include <optional>
#include <ostream>
#include <vector>

#include "appearance_string.hpp"
#include "canon_item.hpp"
#include "itemappearance_db2.hpp"
#include "modelfiledata_db2.hpp"
#include "texturefiledata_db2.hpp"

// husk::canon: assembly of canon::Item (I7, "items by identity, not slot")
// from the real itemappearance:: data-access layer (itemappearance_db2.hpp)
// -- the adjacent twin of export_extras.cpp's own attachGearAppearance loop,
// built to prove structural convergence (REFACTOR/README.md stage 3's gate)
// without touching the existing pipeline.
//
// Deliberately reuses itemappearance::resolve directly rather than
// re-walking itemappearance::Data itself -- that function already IS the
// real DB2 join chain; this file's own job is purely grouping+shaping its
// per-call Resolution into canon::Item's identity-first shape.
//
// The real I7 payoff over attachGearAppearance's own shape: that function
// produces one GearItem/GearSectionOverlay PER GEAR ENTRY, so one item
// equipped at two slots (a real, structurally valid appearance_string.hpp
// `gear=SLOT:id,SLOT:id` input) produces two duplicate-content entries with
// no link between them. assembleEquippedItems instead groups by
// itemModifiedAppearanceId first: entries sharing one appearance id collapse
// into ONE canon::Item with multiple Item::slots, its components resolved
// exactly once -- not once per occurrence.
namespace husk::canon {

// `modelData`/`textureData` are each independently optional, mirroring
// attachGearAppearance's own "a missing table just leaves that hop's
// FileDataID unresolved, never a hard failure" policy.
std::vector<Item> assembleEquippedItems(const itemappearance::Data& data,
                                         const std::optional<modelfiledata::Data>& modelData,
                                         const std::optional<texturefiledata::Data>& textureData,
                                         const std::vector<appearance::GearEntry>& gear, std::ostream& err);

}  // namespace husk::canon
