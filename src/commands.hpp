#pragma once

#include <string>

#include <CLI/CLI.hpp>

// One function per subcommand, matching casc-tool's layout so the two
// tools stay easy to jump between. `args` excludes the program name and
// the subcommand word itself. Returns the process exit code.
namespace husk::commands {

// Shared by every subcommand's own `--help`/`-h` check (main.cpp only
// catches this before a subcommand name is read, e.g. `husk --help`;
// `husk info --help`/`husk dump-chunks --help` need the same check again
// once `args[0]` is a subcommand's own first positional, or it gets
// treated as a literal filename -- see each cmd_*.cpp's entry point).
// `export` no longer needs this: CLI11 recognizes -h/--help itself,
// anywhere in the argument list, without a hand-rolled pre-pass -- see
// cmd_export.cpp/DESIGN.md's "CLI argument grammar for export".
inline bool isHelpFlag(const std::string& arg) { return arg == "--help" || arg == "-h"; }

int info(int argc, char** args);

// `export` is a reserved word, hence the name mismatch with the CLI verb.
int exportGlb(int argc, char** args);

// `dump-chunks` -- extracts M2 chunks that don't feed into `export`'s glTF
// output into readable JSON (see cmd_dump.cpp's own doc comment).
int dumpChunks(int argc, char** args);

// `db2-info` -- proof-of-concept WDC5 DB2 inspection, the `info` analogue
// for the new format (see src/db2.hpp, CHAR_TEXTURE_COMPOSITING_TODO.md
// Stage 1). Not yet consumed by `export`/`dump-chunks` -- purely an
// inspection tool for now.
int db2Info(int argc, char** args);

// `export`'s real flag surface (see DESIGN.md's "CLI argument grammar for
// export"), captured here (rather than as a local in cmd_export.cpp) so
// main.cpp's `--print-completion` can register the exact same options onto
// a throwaway App and introspect them, instead of a second, hand-maintained
// copy of the flag list drifting out of sync with the one CLI11 actually
// parses against (~/docs/READABILITY.md's single-source-of-truth rule).
struct ExportOptions {
    std::string modelPath;
    std::string outputPath;
    std::string skinArg = "auto";
    std::string texturesArg;
    std::string texturesOutArg;
    std::string skinDirArg;
    std::string animArg = "auto";
    std::string skelArg;
    std::string lodArg;
    std::string bonesDirArg;
    std::string physArg;
    bool collisionRequested = false;
};

// Declares every export flag (names, defaults, descriptions, the `--skin
// none` rejection) onto `app` and binds them into `opts`. Used both by
// exportGlb's own real parse and by main.cpp's completion generator, which
// never calls `app.parse(...)` on its copy -- only introspects
// `get_options()`.
void addExportOptions(CLI::App& app, ExportOptions& opts);

}  // namespace husk::commands
