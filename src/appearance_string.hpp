#pragma once

#include <stdexcept>
#include <string>
#include <vector>

// husk's own compact serialization of "what a character looks like":
// customization choices + per-slot transmog appearances. Deliberately NOT
// an addon export format (MogIt/Wowhead-dressing-room-style strings encode
// raw item IDs, which are ambiguous post-Legion -- one appearance can be
// reached via many item IDs, and an addon's dump just picks whichever one
// happened to be equipped). Every ID this format carries is one husk's own
// DB2 pipeline already resolves unambiguously:
//   - cust: ChrCustomizationChoiceID, same ID space as `husk export
//     --customization-choice-ids` (src/chrcustomization_db2.hpp).
//   - gear: (slot, ItemModifiedAppearanceID) -- the appearance actually
//     displayed, not the equipped item's own ID. husk has no code yet that
//     resolves an ItemModifiedAppearanceID to a texture (see cmd_appearance.cpp's
//     doc comment) -- this format only carries the data forward until that
//     chain exists.
//
// Grammar (v1), space-separated fields after a version tag:
//   husk-appearance/1 race=<ChrRacesID> sex=<0|1> cust=<id,id,...> gear=<SLOT:id,SLOT:id,...>
// `race`/`sex` are optional (0 fields omitted); `cust`/`gear` are each
// optional lists, empty when absent. `SLOT` is an opaque uppercase token
// (`[A-Z_]+`) -- husk's grammar does not hardcode Blizzard's equipment-slot
// name enum, so a caller free to use whatever slot naming its own data
// source provides; husk never validates it against a fixed list.
namespace husk::appearance {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct GearEntry {
    std::string slot;
    int itemModifiedAppearanceId = 0;
};

struct AppearanceString {
    int raceId = 0;
    int sexId = 0;
    std::vector<int> customizationChoiceIds;
    std::vector<GearEntry> gear;
};

// Throws ParseError (naming expected vs. actual) on anything that doesn't
// match the v1 grammar above -- a version tag other than "husk-appearance/1",
// an unknown field key, a non-numeric ID, or a slot token containing
// anything outside [A-Z_].
AppearanceString parse(const std::string& text);

// Canonical form: `cust` sorted ascending, `gear` sorted by slot then by
// appearance ID -- so two strings describing the same character always
// serialize identically, regardless of input order.
std::string serialize(const AppearanceString& value);

}  // namespace husk::appearance
