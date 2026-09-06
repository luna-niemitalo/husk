#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "canon_item.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's Selection layer (I5) -- "what did this instance
// choose," scoped to whichever Definition/mechanism it was chosen against.
// A pure value type, no consumers wired to it yet (appearance_string.hpp's
// AppearanceString and gltf_skeleton.hpp's EnabledGeoset/CustomizationChoice/
// CreatureEnabledGeoset/GearItem/GearSectionOverlay stay exactly as they
// are; migrating them onto this is separate, later work).
namespace husk::canon {

// A player-character instance's chosen customization + equipped gear.
// Both real mechanisms coexist on one character -- appearance_string.hpp's
// own grammar carries `cust` and `gear` on the same string -- so they
// share one struct rather than living in separate variant alternatives.
struct CharacterSelection {
    // Scopes this Selection to the Definition it was chosen against.
    // Definition::chrModelId already encodes race+sex (chrrace_db2.hpp's
    // filename->ChrModelID derivation), so a separate raceId/sexId field
    // here would just be the same fact under a second name (I6) --
    // appearance_string.hpp's own race=/sex= fields are the pre-Definition
    // input that *produces* a chrModelId, not additional Selection data.
    uint32_t chrModelId = 0;

    // Chosen ChrCustomizationChoiceIDs -- Ref, not a bare uint32_t,
    // because a consumer checks each one against a
    // Definition::Choice::ref.id (also a
    // Db2Row{"ChrCustomizationChoice", row}) -- comparing like-shaped
    // identities, not an int against a struct field it has to unwrap
    // first.
    std::vector<Ref> chosenChoices;

    struct EquippedItem {
        // Same ItemModifiedAppearanceID identity canon::Item::identity
        // uses. Deliberately not a canon::Item itself: an EquippedItem
        // doesn't carry the item's own resolved geometry/materials
        // (that's Resources, not Selection) -- a consumer joins this
        // identity against a real canon::Item to get that data.
        Ref identity;
        // A Selection can put an item in fewer/different slots than the
        // item itself is capable of -- canon::Item::slots names every
        // slot the *item* can occupy; this names which one *this
        // instance* actually used it for.
        std::vector<Slot> slots;
    };
    std::vector<EquippedItem> equippedItems;
};

// A creature/NPC instance's default geoset selection
// (CreatureDisplayInfoGeosetData) -- a genuinely different mechanism from
// CharacterSelection above: no per-choice caller input, no Definition to
// scope against, just a display ID producing a set of enabled geosets.
// Real WoW data never mixes this with customization choices or equipped
// gear on the same model (DESIGN.md/README.md describe them as separate
// features), so it lives as its own variant alternative rather than a
// third optional group bolted onto CharacterSelection.
struct CreatureSelection {
    uint32_t creatureDisplayId = 0;
    // M2SkinSection::skinSectionId-shaped raw ids, Geoset::fromRawId's
    // own encoding (canon_geoset.hpp) -- bare, not canon::Geoset, since a
    // Selection only needs to say *which* geoset id got turned on, not
    // carry its group/variant decomposition a second time; a consumer
    // already has Geoset::fromRawId to decode this the same way
    // canon_geoset.hpp does.
    std::vector<uint32_t> enabledGeosetIds;
};

// I5: a Selection only means anything paired with the mechanism that
// scopes it, and the two real mechanisms are mutually exclusive in real
// WoW data (see CreatureSelection's own doc comment) -- a variant makes
// that exclusivity structural instead of an unenforced convention over
// two optional struct members.
using Selection = std::variant<CharacterSelection, CreatureSelection>;

}  // namespace husk::canon
