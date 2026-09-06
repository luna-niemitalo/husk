#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "canon_geoset.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// CANONICAL_MODEL.md's Definition layer (I5) -- "what exists and what is
// valid," scoped to one real ChrModelID. A pure value type, no consumers
// wired to it yet (gltf_skeleton.hpp's CustomizationOption/
// CustomizationChoice stay exactly as they are; migrating them onto this,
// and building the Selection/Resources layers this doc also names, is
// separate, later work).
namespace husk::canon {

// One ChrCustomizationCategory row -- the real UI section header
// ("Face", "Hair", "Body", ...) an Option is grouped under. Client-global,
// not scoped to a ChrModelID, but embedded per-Option below rather than
// referenced by id: chrcustomization_db2.hpp already resolves this join
// once at load time (NamedChoice flattens categoryId/name/orderIndex the
// same way), and nothing in this file's scope needs a standalone
// categories-by-id catalog to look back into.
struct Category {
    Ref ref;  // id = Db2Row{"ChrCustomizationCategory", row}, name = CategoryName_lang (Db2 when resolved)
    uint32_t orderIndex = 0;
};

// One ChrCustomizationChoice row -- a selectable value of an Option, e.g.
// "Long Fin" under "Ears". `geoset` is Definition-layer (which geoset
// element this choice, if selected, would make relevant to skinSectionId
// matching) -- canon::Geoset fits directly, since
// chrcustomization_db2.hpp's own Resolution::geosetId already uses the
// identical GeosetType*100+GeosetID encoding Geoset::fromRawId expects.
//
// Excluded on purpose: today's gltf_skeleton.hpp CustomizationChoice::Material
// (fileDataId/materialResourcesId/relatedChoiceId/contentName). That's "what
// realizes this choice if picked" -- Resources, not Definition -- and I5
// names exactly this kind of blending as the problem to not reproduce.
// boneSetId (the other real Element target, ChrCustomizationBoneSet ->
// a .bone FileDataID) is the same Resources-layer shape as Material for
// the identical reason and is excluded alongside it, though nothing
// upstream exposes it as a per-choice field today to name in a doc
// comment the way Material already is.
struct Choice {
    Ref ref;  // id = Db2Row{"ChrCustomizationChoice", row}; name empty for swatch-only choices (real Name_lang == "0")
    uint32_t orderIndex = 0;
    std::optional<Geoset> geoset;  // absent when this choice has no geoset element (real and common)
};

// One ChrCustomizationOption row -- a player-facing choice category, e.g.
// "Skin Color," scoped to one ChrModelID (carried once on Definition below,
// not repeated per Option: every Option in one catalog shares it, and I6
// treats a duplicated fact as a defect to avoid, not a convenience).
struct Option {
    Ref ref;  // id = Db2Row{"ChrCustomizationOption", row}, name = Name_lang
    uint32_t orderIndex = 0;
    Category category;
    std::vector<Choice> choices;
};

// The whole real customization catalog for one race/model. Named, not a
// bare vector<Option>, because chrModelId is the one fact I5's own example
// turns on: NightElf.face[12] and Dwarf.face[12] are different Definitions
// reached by the same index, so which ChrModelID this catalog is scoped to
// has to travel with it, not just with each Option inside it.
struct Definition {
    uint32_t chrModelId = 0;
    std::vector<Option> options;
};

}  // namespace husk::canon
