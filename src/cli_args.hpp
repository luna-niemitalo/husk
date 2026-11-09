#pragma once

#include <optional>

#include <CLI/CLI.hpp>

namespace husk::commands {

// Parses a subcommand's own arguments: `args` excludes the program name and
// the subcommand word (commands.hpp's entry points). Returns the exit code
// when parsing ends the command (--help, a parse error), else nullopt.
std::optional<int> parseCommandArgs(CLI::App& app, int argc, char** args);

// --config: a TOML file of default flag values. Paths like --listfile,
// --db2-dir or --store are per-machine facts (CLI.md §2.5), so a config file
// supplies them and 'none' opts one back out (README.md's "Config file").
void addConfigOption(CLI::App& app);

// --listfile's transform: a directory stands for the alphabetically last
// *.csv in it, so a directory of community-listfile-YYYY-MM-DD.csv
// snapshots always means the newest. A file or 'none' passes through.
// Runs on config-file values too, so the config can name the directory.
CLI::Validator latestListfileInDirectory();

}  // namespace husk::commands
