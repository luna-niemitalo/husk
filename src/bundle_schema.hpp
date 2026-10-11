#pragma once

#include <filesystem>
#include <string>
#include <vector>

// husk::bundle_schema: the bundle format as structure, not prose. schema/
// holds one JSON Schema per document kind plus shared definitions
// (REFACTOR/BUNDLE_FORMAT.md "Document kinds and the contract header");
// they are embedded in the binary. Two checks run against them:
//
// - validateDocument: is this document a well-formed instance of its kind?
//   The JSON Schema first, then what a schema can't say: BufferSlice
//   arithmetic and file bounds, that linked files exist, x-index-into
//   ranges, and the placement_set documents it references, recursively.
// - lintSchemas: is the format itself free of known antipatterns? Storage
//   opinions (container or API names), source-format jargon in keys, raw
//   source data outside `source`/`unresolved`, undocumented or open
//   objects, real numbers without x-unit, unresolvable x-index-into.
//
// This header deliberately exposes no JSON type: the validator is built on
// nlohmann::json 3.12 while tinygltf ships its own older copy, and the two
// must never meet in one translation unit.
namespace husk::bundle_schema {

struct Problem {
    std::string where;  // "<file>#<json pointer>" for documents, "<schema>#<pointer>" for lint
    std::string message;
};

// The document kinds schema/ defines.
std::vector<std::string> kinds();

std::vector<Problem> lintSchemas();

// Lints one kind schema given as text, against the embedded common
// definitions; how the lint rules themselves are tested.
std::vector<Problem> lintSchemaText(const std::string& stem, const std::string& text);

// `path` is a document file, or a bundle directory (its manifest.json).
// `checked` receives every document file validated, the referenced
// placement sets and effects included. Returns the errors; `warnings`
// receives what a release build must not contain but a development build
// may (an instance of a placeholder effect, REFACTOR/EFFECTS.md).
std::vector<Problem> validateDocument(const std::filesystem::path& path,
                                      std::vector<std::filesystem::path>* checked = nullptr,
                                      std::vector<Problem>* warnings = nullptr);

}  // namespace husk::bundle_schema
