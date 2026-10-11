#pragma once

#include <string_view>
#include <vector>

// The schema/ and effects/ files, embedded at build time
// (cmake/embed_schemas.cmake).
namespace husk::bundle_schema {

struct EmbeddedSchema {
    std::string_view name;  // file stem: "common", "model", "classic_local_fog", ...
    std::string_view text;
};

const std::vector<EmbeddedSchema>& embeddedSchemas();

// Each effect document's body: every key after the contract header
// (definition, selection, references, sources). The writer adds the header.
const std::vector<EmbeddedSchema>& embeddedEffects();

}  // namespace husk::bundle_schema
