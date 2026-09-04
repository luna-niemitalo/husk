#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "m2.hpp"

// `husk info --json`: a structured twin of cmd_info.cpp's own prose output
// (REFACTOR/CLI_AND_TOOLING.md §3) -- split into its own file rather than
// growing cmd_info.cpp further past its existing ~420 lines (FILE_SIZE.md),
// since the two output paths share only the already-parsed m2::Model, not
// any printing logic (json::Writer vs. std::cout <<, see json_writer.hpp).
namespace husk::commands {

// Writes the full structured JSON document for `model` to `out`, followed
// by a trailing newline to match every other JSON-emitting command here.
// Mirrors every field cmd_info.cpp's prose path prints -- see
// cmd_info_json.cpp's own doc comment for the field-by-field schema. Never
// throws: every field read here is a plain struct member already
// populated (or left empty, with the reason in `model.parseFailures`) by
// loadModel -- no parsing happens in this function at all.
void printInfoJson(std::ostream& out, const std::string& path, const m2::Model& model);

}  // namespace husk::commands
