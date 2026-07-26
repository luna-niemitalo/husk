#pragma once

#include <string>

// One function per subcommand, matching casc-tool's layout so the two
// tools stay easy to jump between. `args` excludes the program name and
// the subcommand word itself. Returns the process exit code.
namespace husk::commands {

// Shared by every subcommand's own `--help`/`-h` check (main.cpp only
// catches this before a subcommand name is read, e.g. `husk --help`;
// `husk export --help` needs the same check again once `args[0]` is a
// subcommand's own first positional, or it gets treated as a literal
// filename -- see each cmd_*.cpp's entry point).
inline bool isHelpFlag(const std::string& arg) { return arg == "--help" || arg == "-h"; }

int info(int argc, char** args);

// `export` is a reserved word, hence the name mismatch with the CLI verb.
int exportGlb(int argc, char** args);

// `dump-chunks` -- extracts M2 chunks that don't feed into `export`'s glTF
// output into readable JSON (see cmd_dump.cpp's own doc comment).
int dumpChunks(int argc, char** args);

}  // namespace husk::commands
