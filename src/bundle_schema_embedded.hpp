#pragma once

#include <string_view>
#include <vector>

// The schema/ files, embedded at build time (cmake/embed_schemas.cmake).
namespace husk::bundle_schema {

struct EmbeddedSchema {
    std::string_view name;  // file stem: "common", "model", ...
    std::string_view text;
};

const std::vector<EmbeddedSchema>& embeddedSchemas();

}  // namespace husk::bundle_schema
