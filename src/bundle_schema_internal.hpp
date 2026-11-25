#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "bundle_schema.hpp"

// The validator's own nlohmann::json (3.12): include this header only where
// bundle_schema.cpp may be included, never next to tinygltf
// (bundle_schema.hpp).
#include <nlohmann/json.hpp>

// The parsed embedded schemas, shared by bundle_schema.cpp (validation,
// lint) and schema_paths.cpp (document paths: x-source, schema-doc).
namespace husk::bundle_schema::internal {

using json = nlohmann::json;

inline constexpr const char* kSchemaSuffix = ".schema.json";

// Every embedded schema by stem ("common", "model", ...).
const std::map<std::string, json>& schemas();

const json& schemaFor(const std::string& stem);

// "common.schema.json" or a full https://husk.invalid/schema/... uri -> "common".
std::string schemaStem(const std::string& location);

// Leaves without x-source: schema location -> the document paths reaching
// it ("<kind> document path <path>"), in walk order.
using SourceGapPaths = std::map<std::string, std::vector<std::string>>;

// Adds one kind schema's gaps, embedded or not (lintSchemaText).
void collectSourceGaps(const std::string& kind, const json& schema, SourceGapPaths& gaps);

// One problem per schema location, sorted by it, naming every path.
std::vector<Problem> sourceGapProblems(const SourceGapPaths& gaps);

// Kinds of the anatomy family (REFACTOR/ANATOMY_EXCHANGE.md).
bool isAnatomyKind(const std::string& kind);

using Fail = std::function<void(const std::string& pointer, const std::string& message)>;

// What an anatomy-family document needs beyond its schema
// (REFACTOR/ANATOMY_EXCHANGE.md "What validate-bundle checks"), the schema
// already holding. `referenced` is each reference's validated document,
// null when unreadable; `idChecked` are the arrays whose ids the x-id-in
// walk has already checked for uniqueness.
void checkAnatomyFamily(const std::string& kind, const json& document, const std::vector<const json*>& referenced,
                        const std::set<std::string>& idChecked, const Fail& fail);

}  // namespace husk::bundle_schema::internal
