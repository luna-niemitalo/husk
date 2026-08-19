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
// for the new format (see src/db2.hpp, TODO/CHAR_TEXTURE_COMPOSITING_TODO.md
// Stage 1). Not yet consumed by `export`/`dump-chunks` -- purely an
// inspection tool for now.
int db2Info(int argc, char** args);

// `db2-export` -- converts a WDC5 DB2 file (or, with --dir, every *.db2 file
// in a directory) to a real SQLite database, one table per file (see
// src/dbd.hpp, src/cmd_db2.cpp's own doc comment). Real column names/types
// when an optional --dbd-dir resolves them (via src/dbd.hpp's WoWDBDefs
// parser), generic `field_<N>` columns otherwise -- never a hard dependency
// on that data being available. In --dir mode, real WoWDBDefs foreign-key
// columns get a real SQLite FOREIGN KEY constraint whenever the target
// table is also part of the same export batch, and a header.flags & 0x04
// ("has non-inline IDs") table gets a real ID column of its own (see
// src/cmd_db2.cpp's own doc comment).
int db2Export(int argc, char** args);

// `db2-build` -- builds husk's own verified knowledge-base SQLite database
// (TODO/KNOWLEDGE_BASE_DESIGN.md) from --db2-dir + --dbd-dir + --listfile:
// the DB2 tables today's resolved joins need, a 'models' table (FileDataID
// -> real path), one resolved join table per known "model needs X"
// question (today: model_object_skin_texture), and a '_meta' staleness
// stamp. Consumed by `husk export --knowledge-db`.
int db2Build(int argc, char** args);

// `blp-export` -- converts a BLP2 texture (or, with --dir, every *.blp file
// in a directory) to a real PNG, reusing the exact blp::decode/
// blp::encodePng pipeline `husk export` already uses internally to embed
// textures (src/cmd_blp.cpp's own doc comment; README.md's "Texture
// conversion" section for why this exists alongside the separate Python
// blp/ tool).
int blpExport(int argc, char** args);

// `appearance-string` -- validates/normalizes a husk-appearance/1 string
// (see src/appearance_string.hpp, src/cmd_appearance.cpp's own doc comment).
int appearanceString(int argc, char** args);

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
    std::string db2DirArg;
    std::string dbdDirArg;
    std::string charLayoutIdArg;
    std::string customizationChoiceIdsArg;
    std::string creatureDisplayIdArg;
    std::string objectSkinTextureIdArg;
    std::string knowledgeDbArg;
    std::string listfileArg;
    std::string listfileRootArg;
};

// Declares every export flag (names, defaults, descriptions, the `--skin
// none` rejection) onto `app` and binds them into `opts`. Used both by
// exportGlb's own real parse and by main.cpp's completion generator, which
// never calls `app.parse(...)` on its copy -- only introspects
// `get_options()`.
void addExportOptions(CLI::App& app, ExportOptions& opts);

}  // namespace husk::commands
