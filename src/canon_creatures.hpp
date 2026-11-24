#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "canon_ref.hpp"

// A corpus's creature catalog (REFACTOR/BUNDLE_FORMAT.md "Creature
// catalogs"): what creatures the client knows, what kind each is, and which
// model and preset draws each of its displays. Where a creature lives is
// server data, not in the client, so the catalog places nothing.
namespace husk::canon {

struct CreatureDisplayUse {
    size_t display = 0;  // index into CreatureCatalog::displays; written as that display's id
    float weight = 0;    // the source's relative weight among the creature's displays, as stored
};

struct Creature {
    std::string id;  // stable UUID
    Ref ref;         // identity is the source's record (Creature row), the name its own
    std::string title;
    std::optional<Ref> type;
    std::optional<Ref> family;
    uint32_t classification = 0;               // Creature.Classification as stored; no table names it
    std::vector<CreatureDisplayUse> displays;  // slot order; a display may repeat
};

struct CreatureModel {
    std::string id;  // stable UUID
    Ref ref;         // the model file (FileDataID), named from the listfile when one is given
};

struct CreatureDisplay {
    std::string id;  // stable UUID
    Ref ref;         // the source's record (CreatureDisplayInfo row)
    std::optional<uint32_t> model;   // FileDataID of the model the display dresses; one of CreatureCatalog::models
    std::optional<uint32_t> preset;  // index into that model document's selection.presets
    std::optional<float> scale;      // the display's own scale
    std::optional<float> baseScale;  // the model data row's scale
    std::optional<int32_t> sizeClass;  // the source's size class as stored; no table names it
    // What the display layers on its model, as source rows (none named:
    // husk reads none of those tables); absent when stored as 0.
    std::optional<Ref> objectEffectPackage;
    std::optional<Ref> stateSpellVisualKit;
    std::optional<Ref> animReplacementSet;
    std::optional<Ref> dissolveEffect;
    std::optional<Ref> mountPoofSpellVisualKit;
};

struct CreatureCatalog {
    Ref subject;
    std::vector<Creature> creatures;      // ascending by source row
    std::vector<CreatureDisplay> displays;  // ascending by source row
    std::vector<CreatureModel> models;      // every display's model once, ascending by FileDataID
};

}  // namespace husk::canon
