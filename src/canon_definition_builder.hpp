#pragma once

#include <cstdint>
#include <ostream>

#include "canon_definition.hpp"
#include "chrcustomization_db2.hpp"

// husk::canon: assembly of one canon::Definition from the real
// chrcustomization:: data-access layer (chrcustomization_db2.hpp) --
// the adjacent twin of export_extras.cpp's own customizationOptions-building
// loop (buildCustomizationOptions-shaped code around
// chrcustomization::namedChoicesForModel, cmd_export.cpp/export_extras.cpp),
// built to prove structural convergence (REFACTOR/README.md stage 3's gate)
// without touching the existing pipeline.
//
// Deliberately reuses chrcustomization::namedChoicesForModel directly rather
// than re-walking Data::options/choices/categories itself -- that function
// already IS the real DB2 traversal (join Option -> Category, Option ->
// matching Choices, Choice -> resolveChoice's geosetId), and re-deriving it
// here would duplicate real logic, not converge with it. This file's own job
// is purely shaping that flat per-(option,choice) list into canon::Definition's
// nested Option/Choice/Category shape.
namespace husk::canon {

// Groups namedChoicesForModel's flat NamedChoice rows under one canon::Option
// per real optionId (each option's Category embedded once, mirroring
// export_extras.cpp's own find-or-insert grouping). Real, intentional
// divergence from export_extras.cpp: that function preserves whatever order
// namedChoicesForModel happens to yield (Data::options' own table order,
// itself whatever order chrcustomizationoption.db2's rows load in -- not a
// display order), because it's building inert extras where order isn't
// semantically load-bearing. canon::Definition's own doc comment states no
// such exemption, so this sorts Definition::options by real optionOrderIndex
// (ties broken by optionId) and each Option::choices by real choiceOrderIndex
// (ties broken by choiceId) -- deterministic, and matching the real
// character-creation UI's own display order, the same convention
// defaultChoiceIdsForModel's own doc comment already states for a related
// concern.
//
// Mirrors namedChoicesForModel's own foreign-data behavior exactly: returns
// an empty Definition{chrModelId, {}} (not a thrown error) when
// data.options/data.choices aren't both populated -- a real "these DB2
// tables genuinely weren't fetched locally" case this project hits often,
// not corruption.
//
// A choice with no real name (namedChoicesForModel's choiceName empty --
// covers both genuine swatch-only choices, real Name_lang == "0", and any
// unresolved case) or a category that never resolved (categoryId == 0,
// covers both "no real ChrCustomizationCategoryID at all" and "a dangling
// reference" -- namedChoicesForModel's own categoryId stays 0 in both cases,
// indistinguishable from this function's own inputs) gets Ref::name left
// empty with NameSource::None, never a fabricated placeholder string.
Definition assembleDefinition(const chrcustomization::Data& data, uint32_t chrModelId, std::ostream& err);

}  // namespace husk::canon
