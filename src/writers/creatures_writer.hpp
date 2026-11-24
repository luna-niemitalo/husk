#pragma once

#include <string>

#include "canon_creatures.hpp"
#include "writers/bundle_common.hpp"  // AssetUris, Provenance

// A `creatures` document (schema/creatures.schema.json): the corpus's
// creature catalog, one file, returned as its text for the caller to write.
// Each model a display draws is one `model` reference, in ascending
// FileDataID order, with uri `modelUris[fdid]` relative to the document
// (absent: known, not exported); a display names its reference by index.
namespace husk::writers {

std::string creatureCatalogDocument(const canon::CreatureCatalog& catalog, const Provenance& provenance,
                                    const AssetUris& modelUris);

}  // namespace husk::writers
