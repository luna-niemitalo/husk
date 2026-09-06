#pragma once

#include "m2.hpp"

// husk::canon: the semantic layer REFACTOR/CANONICAL_MODEL.md designs
// (Stage 3). This file is a minimal seed of that layer -- just the
// parse-failure contract every m2::Model consumer must declare against --
// not a home for anything else CANONICAL_MODEL.md describes.
namespace husk::canon {

// m2::loadModel isolates a malformed array's parse failure onto
// model.parseFailures rather than throwing, so one bad section doesn't
// blank out an otherwise-readable file (see m2_model.hpp). What a consumer
// does once it actually reads that field is a policy choice, not a fact
// about the field itself -- two real, deliberately different answers exist
// today:
//
// Tolerant: read model.parseFailures as diagnostic data and keep going
// (husk info/dump-chunks) -- a diagnostic tool should show as much of a
// broken file as it can. There is no enforcePartialFailurePolicy call for
// this case: it is a no-op by definition, so tolerant consumers just read
// model.parseFailures directly (see cmd_info.cpp/cmd_dump.cpp/
// cmd_info_json.cpp).
//
// Strict: throw before consuming a field that failed to parse (husk
// export) -- an empty vector can't be told apart from "genuinely zero
// records," so silently treating a malformed array as empty risks a wrong
// export, not just a missing one.
enum class PartialFailurePolicy {
    Tolerant,
    Strict,
};

// No-op under Tolerant. Under Strict, throws std::runtime_error(the
// matching m2::FieldParseFailure::what) if `field` is in
// model.parseFailures. Callers place one call per field, at the point that
// field is actually read -- not once for the whole model -- since a real
// export's own consumption order does not match m2::loadModel's parse
// order (vertices is read before bones/sequences despite parsing after
// them), so which of several simultaneous failures gets reported depends
// on call-site placement, not just which failed first.
void enforcePartialFailurePolicy(const m2::Model& model, const char* field, PartialFailurePolicy policy);

}  // namespace husk::canon
