#pragma once

#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "canon_policy.hpp"

// One function per subcommand, matching casc-tool's layout so the two
// tools stay easy to jump between. `args` excludes the program name and
// the subcommand word itself. Returns the process exit code.
namespace husk::commands {

// husk export's declared canon::PartialFailurePolicy (see canon_policy.hpp):
// a malformed field it's about to consume must throw, never silently read
// as empty. Every attachX()/exportOneModel call site in cmd_export.cpp/
// export_extras.cpp passes this, rather than each implying strictness by
// calling a strict-only-named function.
inline constexpr auto kExportPartialFailurePolicy = canon::PartialFailurePolicy::Strict;

int info(int argc, char** args);

// `export` is a reserved word, hence the name mismatch with the CLI verb.
int exportGlb(int argc, char** args);

// `dump-chunks` -- extracts M2 chunks that don't feed into `export`'s glTF
// output into readable JSON (see cmd_dump.cpp's own doc comment).
int dumpChunks(int argc, char** args);

// `db2-info` -- proof-of-concept WDC5 DB2 inspection, the `info` analogue
// for the new format (see src/db2.hpp). Not yet consumed by
// `export`/`dump-chunks` -- purely an inspection tool for now.
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
// (design history: git log -p TODO/KNOWLEDGE_BASE_DESIGN.md, deleted
// 2026-09-16; live follow-up in TODO/TEXTURE_POOL_RECALL_TODO.md) from
// --db2-dir + --dbd-dir + --listfile:
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

// `resolve` -- structured JSON dump of `sources::Catalog`'s texture-
// resolution ledger (see cmd_resolve.cpp's own doc comment, and
// `ResolveOptions` below).
int resolve(int argc, char** args);

// Every struct/addXOptions pair below is the same single-source-of-truth
// split `ExportOptions`/`addExportOptions` established: the flag surface is
// declared once here (names, defaults, descriptions), used both by the real
// command's own parse and by main.cpp's `--print-completion` generator,
// which never calls `app.parse(...)` on its copy -- only introspects
// `get_options()`. Kept in `commands.hpp` rather than local to each
// cmd_*.cpp for the same reason `ExportOptions` is (~/docs/READABILITY.md's
// single-source-of-truth rule): a second, hand-maintained copy of any of
// these flag lists would drift out of sync with the one CLI11 actually
// parses against.

struct InfoOptions {
    std::string model;
    // REFACTOR/CLI_AND_TOOLING.md §3: a structured twin of the prose output
    // below, so corpus-scan tasks (tools/corpus_scan_tasks/*.py) can parse
    // husk's own answer instead of regexing text husk never promised to
    // keep stable. Off by default -- prose stays byte-identical either way.
    bool json = false;
};
void addInfoOptions(CLI::App& app, InfoOptions& opts);

// Shared between cmd_info.cpp's prose path and cmd_info_json.cpp's --json
// path -- husk's own curated list of every wowdev.wiki-documented M2 chunk
// tag (see cmd_info.cpp's own doc comment on the list itself), and the
// check for a real file carrying a tag outside it (a signal the format grew
// a new chunk since this list was last updated). Single source of truth so
// the two output paths can't silently drift apart on what counts as
// "known".
const std::vector<std::string>& documentedM2ChunkTags();
bool isUndocumentedChunkTag(const std::string& tag);

struct DumpChunksOptions {
    std::string model;
};
void addDumpChunksOptions(CLI::App& app, DumpChunksOptions& opts);

struct Db2InfoOptions {
    std::string model;
    std::string rowsArg = "5";
};
void addDb2InfoOptions(CLI::App& app, Db2InfoOptions& opts);

// `pos1`/`pos2` are deliberately generic, not `input`/`output` -- --dir
// mode's grammar ("--dir <dir> <out>") only leaves one positional token
// after `--dir` consumes its own value, which binds to `pos1` and is
// reinterpreted as the output path there; single-file mode ("<in> <out>")
// uses both positionals for their more obvious meaning. See db2Export's own
// doc comment in cmd_db2.cpp for why this shape was kept instead of a
// redesign into new flag names.
struct Db2ExportOptions {
    std::string dirArg;
    std::string dbdDir;
    std::string pos1;
    std::string pos2;
};
void addDb2ExportOptions(CLI::App& app, Db2ExportOptions& opts);

struct Db2BuildOptions {
    std::string db2Dir;
    std::string dbdDir;
    std::string listfilePath;
    std::string outputPath;
};
void addDb2BuildOptions(CLI::App& app, Db2BuildOptions& opts);

// Same `pos1`/`pos2` reinterpreted-by-mode shape as `Db2ExportOptions`
// above.
struct BlpExportOptions {
    std::string dirArg;
    std::string pos1;
    std::string pos2;
};
void addBlpExportOptions(CLI::App& app, BlpExportOptions& opts);

struct AppearanceStringOptions {
    std::string value;
    // Optional -- when both given, `gear` entries resolve to real equipped-
    // item DB2 data instead of staying opaque IDs. Same role/naming as
    // `export`'s own --db2-dir/--dbd-dir.
    std::string db2DirArg;
    std::string dbdDirArg;
};
void addAppearanceStringOptions(CLI::App& app, AppearanceStringOptions& opts);

// `resolve` -- REFACTOR/CLI_AND_TOOLING.md §3's `husk resolve` verb: prints
// `sources::Catalog`'s own texture-resolution ledger as JSON, driving the
// exact same tier-order code `export` uses (`buildMaterialsAndPrimitives`,
// `resolveSkinsToExport`) but stopping once every batch's texture slot has
// been resolved -- no skeleton/animation/mesh-accessor build, no glTF
// serialization, no file write. See cmd_resolve.cpp's own doc comment for
// why this is a separate command rather than an `--explain-textures=json`
// extension: two of the four corpus tasks this unblocks
// (`unfillable_texture_task.py`, `texture_dedup_collision_task.py`) already
// deliberately avoid a real `husk export` per file, at 132k-file corpus
// scale, for exactly the cost a full export/glb-write pays (see those
// tasks' own module doc comments) -- forcing them onto `export` to get a
// structured answer would reintroduce the cost their own history already
// rejected.
struct ResolveOptions {
    std::string model;
    std::string skinArg = "auto";
    std::string skinDirArg;
    std::string lodArg;
    std::string texturesArg;
    std::string texturesOutArg;
    std::string listfileArg;
    std::string listfileRootArg;
    std::string objectSkinTextureIdArg;
};
void addResolveOptions(CLI::App& app, ResolveOptions& opts);

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
    std::string appearanceArg;
    std::string chrModelIdArg;
    std::string creatureDisplayIdArg;
    std::string objectSkinTextureIdArg;
    std::string knowledgeDbArg;
    std::string listfileArg;
    std::string listfileRootArg;
    std::string fromListArg;
    std::string outputDirArg;
    bool slimTextures = false;
    // REFACTOR/README.md's I4 -- see the flag's own --help text.
    bool explainTextures = false;
    // Opts back into the old full per-batch, full-filename-list
    // fuzzy/ambiguous-texture warning dump -- see the flag's own --help text.
    bool debugTextureWarnings = false;
    // REFACTOR/README.md stage 3's runtime convergence gate -- see the
    // flag's own --help text and cmd_export_canon.hpp's doc comment.
    bool exportCanon = false;
};

// Declares every export flag (names, defaults, descriptions, the `--skin
// none` rejection) onto `app` and binds them into `opts`. Used both by
// exportGlb's own real parse and by main.cpp's completion generator, which
// never calls `app.parse(...)` on its copy -- only introspects
// `get_options()`.
void addExportOptions(CLI::App& app, ExportOptions& opts);

}  // namespace husk::commands
