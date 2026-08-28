#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "m2.hpp"

// `husk info --json`: a structured twin of cmd_info.cpp's own prose output
// (REFACTOR/CLI_AND_TOOLING.md §3) -- split into its own file rather than
// growing cmd_info.cpp further past its existing ~420 lines (FILE_SIZE.md),
// since the two output paths share only the already-parsed Header/blob, not
// any printing logic (json::Writer vs. std::cout <<, see json_writer.hpp).
namespace husk::commands {

// Writes the full structured JSON document for `h`/`blob` (already parsed
// from `path` by the caller) to `out`, followed by a trailing newline to
// match every other JSON-emitting command here (`husk dump-chunks`,
// cmd_dump.cpp). Mirrors every field cmd_info.cpp's prose path prints --
// see cmd_info_json.cpp's own doc comment for the field-by-field schema.
// Never throws on its own (every field read here was already successfully
// parsed by the caller before this is called); any m2::parse* call that
// could throw runs against the same `blob`/array descriptors the prose path
// already trusts.
void printInfoJson(std::ostream& out, const std::string& path, const m2::Header& h,
                    const std::vector<uint8_t>& blob);

}  // namespace husk::commands
