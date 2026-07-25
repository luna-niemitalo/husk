#pragma once

// One function per subcommand, matching casc-tool's layout so the two
// tools stay easy to jump between. `args` excludes the program name and
// the subcommand word itself. Returns the process exit code.
namespace husk::commands {

int info(int argc, char** args);

// `export` is a reserved word, hence the name mismatch with the CLI verb.
int exportGlb(int argc, char** args);

// `dump-chunks` -- extracts M2 chunks that don't feed into `export`'s glTF
// output into readable JSON (see cmd_dump.cpp's own doc comment).
int dumpChunks(int argc, char** args);

}  // namespace husk::commands
