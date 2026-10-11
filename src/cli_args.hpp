#pragma once

#include <optional>

#include <CLI/CLI.hpp>

namespace husk::commands {

// Parses a subcommand's own arguments: `args` excludes the program name and
// the subcommand word (commands.hpp's entry points). Returns the exit code
// when parsing ends the command (--help, a parse error), else nullopt.
std::optional<int> parseCommandArgs(CLI::App& app, int argc, char** args);

}  // namespace husk::commands
