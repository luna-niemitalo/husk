#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "commands.hpp"

// Hidden `--print-completion=<bash|zsh>` support (see DESIGN.md's "Shell
// completion generation"). No C++
// arg-parsing library (CLI11 included) generates shell completions, so this
// builds its own throwaway CLI11 App tree -- the exact same shape
// exportGlb's real parse uses, via husk::commands::addExportOptions, so the
// flag surface can never drift between the two -- and walks it with CLI11's
// own introspection API, never `.parse(...)`.
namespace {

struct FlagSpec {
    std::string longName;   // e.g. "--skin", empty if this option has none
    std::string shortName;  // e.g. "-s", empty if this option has none
    bool takesValue = false;
};

// A pure positional (CLI11's `pname_` set, no `-x`/`--xxx` forms at all --
// info/dump-chunks' single "model" argument) has nothing for `--<TAB>` to
// complete; skip it. `get_options()` (no filter) walks CLI11's own
// `options_` unconditionally, unlike the parallel-named `get_subcommands()`
// below -- confirmed by reading CLI/impl/App_inl.hpp rather than assumed.
std::vector<FlagSpec> collectFlags(CLI::App& sub) {
    std::vector<FlagSpec> flags;
    for (CLI::Option* opt : sub.get_options()) {
        if (opt->get_lnames().empty() && opt->get_snames().empty()) continue;
        FlagSpec fs;
        if (!opt->get_lnames().empty()) fs.longName = "--" + opt->get_lnames().front();
        if (!opt->get_snames().empty()) fs.shortName = "-" + opt->get_snames().front();
        fs.takesValue = opt->get_expected_max() != 0;
        flags.push_back(fs);
    }
    return flags;
}

// Unlike get_options(), CLI11's no-arg `get_subcommands()` returns only
// *parsed* subcommands (`parsed_subcommands_`) -- always empty here, since
// this never calls `.parse(...)`. The filtered overload walks the real
// `subcommands_` list instead (verified in CLI/impl/App_inl.hpp), so a
// pass-everything filter is how "all registered subcommands" is obtained.
std::vector<CLI::App*> allSubcommands(CLI::App& root) {
    return root.get_subcommands([](CLI::App*) { return true; });
}

std::vector<std::string> flagTokens(const FlagSpec& f) {
    std::vector<std::string> tokens;
    if (!f.longName.empty()) tokens.push_back(f.longName);
    if (!f.shortName.empty()) tokens.push_back(f.shortName);
    return tokens;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

// CLI11's Option carries no "these are the only accepted values" metadata
// for a free-form string option -- get_expected_max()/get_lnames() are all
// it exposes (checked, not assumed). The auto/inline/none/directory/file
// taxonomy below is real domain knowledge from DESIGN.md's per-flag
// state-machine table, added on top of introspection and keyed only by flag
// identity -- matches every other case in this codebase where CLI11 alone
// can't carry the full contract (e.g. `--skin none`'s rejection).
//
// `--db2-dir`/`--dbd-dir`/`--listfile`/`--listfile-root` are also registered
// on db2-build/db2-info, which don't accept 'none' -- db2-build's own three
// are `->required()`, so an off-state doesn't apply, and db2-info never got
// --config wiring in the first place, so it has no config-supplied default
// to opt back out of. Every *other* subcommand these flags appear on has
// both --config wiring and an optional (non-required) flag, so 'none' is
// real there too (DESIGN.md's "Three-state resolution, not two").
bool noneOptOutSupported(const std::string& subName, const std::string& longName) {
    if (subName == "export") {
        return longName == "--db2-dir" || longName == "--dbd-dir" || longName == "--listfile" ||
               longName == "--listfile-root";
    }
    if (subName == "resolve") return longName == "--listfile" || longName == "--listfile-root";
    if (subName == "db2-export") return longName == "--dbd-dir";
    if (subName == "appearance-string") return longName == "--db2-dir" || longName == "--dbd-dir";
    if (subName == "export-terrain") {
        return longName == "--db2-dir" || longName == "--dbd-dir" || longName == "--listfile" ||
               longName == "--listfile-root";
    }
    // export-world's --listfile/--listfile-root are required: no off-state.
    if (subName == "export-world") return longName == "--db2-dir" || longName == "--dbd-dir";
    return false;
}

std::string bashValueCompletion(const std::string& longName, const std::string& subName) {
    if (longName == "--skin") {
        return "COMPREPLY=($(compgen -W \"auto\" -- \"$cur\")); compopt -o filenames "
               "2>/dev/null; COMPREPLY+=($(compgen -f -- \"$cur\"))";
    }
    if (longName == "--anim") {
        return "COMPREPLY=($(compgen -W \"auto inline none\" -- \"$cur\")); compopt -o "
               "filenames 2>/dev/null; COMPREPLY+=($(compgen -d -- \"$cur\"))";
    }
    bool dirFlag = longName == "--db2-dir" || longName == "--dbd-dir" || longName == "--listfile-root";
    bool fileFlag = longName == "--listfile";
    bool noneOk = noneOptOutSupported(subName, longName);
    if (longName == "--textures" || longName == "--skin-dir" || longName == "--bones-dir" ||
        (dirFlag && noneOk)) {
        return "COMPREPLY=($(compgen -W \"none\" -- \"$cur\")); compopt -o filenames "
               "2>/dev/null; COMPREPLY+=($(compgen -d -- \"$cur\"))";
    }
    if (longName == "--textures-out" || (dirFlag && !noneOk)) {
        return "compopt -o filenames 2>/dev/null; COMPREPLY=($(compgen -d -- \"$cur\"))";
    }
    if (longName == "--skel" || longName == "--phys" || (fileFlag && noneOk)) {
        return "COMPREPLY=($(compgen -W \"none\" -- \"$cur\")); compopt -o filenames "
               "2>/dev/null; COMPREPLY+=($(compgen -f -- \"$cur\"))";
    }
    if (longName == "--lod") {
        return R"(COMPREPLY=($(compgen -W "all" -- "$cur")))";
    }
    return "COMPREPLY=($(compgen -f -- \"$cur\"))";  // --input/--output/listfile-without-none: plain filenames
}

std::string generateBashCompletion(CLI::App& root) {
    std::ostringstream out;
    out << "# generated by `husk --print-completion=bash` -- do not hand-edit; "
           "regenerate with that command whenever export's flag table changes.\n"
        << "_husk_completions() {\n"
        << "    local cur prev\n"
        << "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
        << "    prev=\"${COMP_WORDS[COMP_CWORD-1]}\"\n"
        << "\n";

    auto subs = allSubcommands(root);
    std::vector<std::string> subNames;
    subNames.reserve(subs.size());
    for (CLI::App* sub : subs) subNames.push_back(sub->get_name());
    out << "    local subcommands=\"" << join(subNames, " ") << "\"\n"
        << "\n"
        << "    if [[ $COMP_CWORD -eq 1 ]]; then\n"
        << "        COMPREPLY=($(compgen -W \"$subcommands\" -- \"$cur\"))\n"
        << "        return\n"
        << "    fi\n"
        << "\n"
        << "    case \"${COMP_WORDS[1]}\" in\n";

    for (CLI::App* sub : subs) {
        auto flags = collectFlags(*sub);
        out << "        " << sub->get_name() << ")\n";

        bool anyValueFlag = false;
        for (const auto& f : flags) anyValueFlag |= f.takesValue;
        if (anyValueFlag) {
            out << "            case \"$prev\" in\n";
            for (const auto& f : flags) {
                if (!f.takesValue) continue;
                out << "                " << join(flagTokens(f), "|") << ")\n"
                    << "                    " << bashValueCompletion(f.longName, sub->get_name()) << "\n"
                    << "                    return\n"
                    << "                    ;;\n";
            }
            out << "            esac\n";
        }

        std::vector<std::string> allTokens;
        for (const auto& f : flags) {
            for (auto& t : flagTokens(f)) allTokens.push_back(t);
        }
        if (!allTokens.empty()) {
            out << "            if [[ \"$cur\" == -* ]]; then\n"
                << "                COMPREPLY=($(compgen -W \"" << join(allTokens, " ")
                << "\" -- \"$cur\"))\n"
                << "                return\n"
                << "            fi\n";
        }
        out << "            COMPREPLY=($(compgen -f -- \"$cur\"))\n"
            << "            ;;\n";
    }

    out << "    esac\n"
        << "}\n"
        << "complete -F _husk_completions husk\n";
    return out.str();
}

// Each entry pairs a helper function's body (an `_alternative` call
// combining a literal word list with real file/dir completion) with the
// function name `_arguments` should reference for that flag. Named
// functions, not an inlined `_alternative '...' '...'` call, because
// `_arguments` spec strings are themselves single-quoted -- the same
// content nested directly inside would prematurely close/reopen that outer
// quote (zsh silently concatenates the resulting adjacent quoted/unquoted
// fragments into one malformed word rather than raising a parse error, so
// `zsh -n` alone can't catch this class of bug; only actually reasoning
// through the tokenization does). A bare name has no quotes to nest.
struct ZshHelper {
    std::string name;
    std::string body;
};

const std::vector<ZshHelper>& zshHelpers() {
    static const std::vector<ZshHelper> helpers = {
        {"_husk_skin_value", "_alternative 'value:value:(auto)' 'files:file:_files'"},
        {"_husk_anim_value",
         "_alternative 'value:value:(auto inline none)' 'dirs:directory:_files -/'"},
        {"_husk_dir_or_none_value", "_alternative 'value:value:(none)' 'dirs:directory:_files -/'"},
        {"_husk_dir_value", "_alternative 'dirs:directory:_files -/'"},
        {"_husk_file_or_none_value", "_alternative 'value:value:(none)' 'files:file:_files'"},
    };
    return helpers;
}

// See noneOptOutSupported's own comment for why `subName` matters here:
// --db2-dir/--dbd-dir/--listfile/--listfile-root only accept 'none' on some
// of the subcommands they're registered on.
std::string zshValueAction(const std::string& longName, const std::string& subName) {
    if (longName == "--skin") return "_husk_skin_value";
    if (longName == "--anim") return "_husk_anim_value";
    bool dirFlag = longName == "--db2-dir" || longName == "--dbd-dir" || longName == "--listfile-root";
    bool fileFlag = longName == "--listfile";
    bool noneOk = noneOptOutSupported(subName, longName);
    if (longName == "--textures" || longName == "--skin-dir" || longName == "--bones-dir" || (dirFlag && noneOk))
        return "_husk_dir_or_none_value";
    if (longName == "--textures-out" || (dirFlag && !noneOk)) return "_husk_dir_value";
    if (longName == "--skel" || longName == "--phys" || (fileFlag && noneOk)) return "_husk_file_or_none_value";
    if (longName == "--lod") return "(all)";
    return "_files";
}

// Short, quote-free labels -- not CLI11's own Option::get_description(),
// which freely embeds apostrophes ("'auto'", "'none'", ...) that would
// break the single-quoted `_arguments` spec strings below. The real flag
// names/short-forms/value taxonomy below all come from introspection or
// DESIGN.md's table; only these help strings are hand-written, to
// stay inside zsh's quoting rules rather than fighting them.
std::string zshFlagLabel(const std::string& longName) {
    if (longName == "--input") return "the .m2 file to export";
    if (longName == "--output") return "output .glb path";
    if (longName == "--skin") return "a .skin path, or auto";
    if (longName == "--textures") return "texture directory, or none";
    if (longName == "--textures-out") return "directory to also write decoded .png copies to";
    if (longName == "--slim-textures") return "write textures as external files instead of embedding";
    if (longName == "--skin-dir") return "skin-search directory, or none";
    if (longName == "--anim") return "auto, inline, none, or a directory";
    if (longName == "--skel") return "external .skel path, or none";
    if (longName == "--lod") return "LOD index, or all";
    if (longName == "--bones-dir") return "bone-correction directory, or none";
    if (longName == "--phys") return "external .phys path, or none";
    if (longName == "--collision") return "include the collision mesh (off by default)";
    if (longName == "--db2-dir") return "character DB2 directory (texture-layout or customization)";
    if (longName == "--dbd-dir") return "WoWDBDefs checkout, for --db2-dir column names";
    if (longName == "--char-layout-id") return "a real CharComponentTextureLayoutsID";
    if (longName == "--customization-choice-ids") return "comma-separated ChrCustomizationChoiceID(s)";
    if (longName == "--listfile") return "community-listfile.csv snapshot, for FileDataID names";
    if (longName == "--listfile-root") return "corpus root the listfile paths are relative to";
    if (longName == "--json") return "print structured JSON instead of prose";
    if (longName == "--help") return "print help and exit";
    return longName;
}

std::string generateZshCompletion(CLI::App& root) {
    std::ostringstream out;
    out << "#compdef husk\n"
        << "# generated by `husk --print-completion=zsh` -- do not hand-edit; "
           "regenerate with that command whenever export's flag table changes.\n"
        << "\n";
    for (const ZshHelper& helper : zshHelpers()) {
        out << helper.name << "() { " << helper.body << " }\n";
    }
    out << "\n"
        << "_husk() {\n"
        << "    local -a subcommands\n"
        << "    subcommands=(\n";

    auto subs = allSubcommands(root);
    for (CLI::App* sub : subs) {
        out << "        '" << sub->get_name() << ":" << sub->get_description() << "'\n";
    }
    out << "    )\n"
        << "\n"
        << "    if (( CURRENT == 2 )); then\n"
        << "        _describe 'command' subcommands\n"
        << "        return\n"
        << "    fi\n"
        << "\n"
        << "    case ${words[2]} in\n";

    for (CLI::App* sub : subs) {
        auto flags = collectFlags(*sub);
        out << "        " << sub->get_name() << ")\n";
        if (!flags.empty()) {
            out << "            _arguments \\\n";
            for (const auto& f : flags) {
                std::string spec;
                if (!f.longName.empty() && !f.shortName.empty()) {
                    spec = "'(" + f.shortName + " " + f.longName + ")'{" + f.shortName + "," +
                           f.longName + "}'";
                } else if (!f.longName.empty()) {
                    spec = "'" + f.longName;
                } else {
                    spec = "'" + f.shortName;
                }
                spec += "[" + zshFlagLabel(f.longName.empty() ? f.shortName : f.longName) + "]";
                if (f.takesValue) {
                    spec += ":value:" + zshValueAction(f.longName, sub->get_name());
                }
                spec += "'";
                out << "                " << spec << " \\\n";
            }
            out << "                '1:model:_files'\n";
        } else {
            out << "            _arguments '1:model:_files'\n";
        }
        out << "            ;;\n";
    }

    out << "    esac\n"
        << "}\n"
        << "\n"
        << "_husk \"$@\"\n";
    return out.str();
}

// Every option struct below must outlive whatever introspects `root` (CLI11
// binds by reference to the address passed at add_option time) -- bundled
// into one struct, rather than a pile of function-local variables, so both
// generateCompletionScript and generateFlagDocsMarkdown below can share one
// registration function instead of keeping two copies of this subcommand
// list in sync by hand.
struct AllSubcommandOpts {
    husk::commands::ExportOptions exportOpts;
    husk::commands::InfoOptions infoOpts;
    husk::commands::DumpChunksOptions dumpOpts;
    husk::commands::Db2InfoOptions db2InfoOpts;
    husk::commands::Db2ExportOptions db2ExportOpts;
    husk::commands::Db2BuildOptions db2BuildOpts;
    husk::commands::BlpExportOptions blpExportOpts;
    husk::commands::AppearanceStringOptions appearanceOpts;
    husk::commands::ResolveOptions resolveOpts;
    husk::commands::ExportTerrainOptions exportTerrainOpts;
    husk::commands::ExportWorldOptions exportWorldOpts;
};

// Builds a throwaway CLI11 App tree matching every subcommand's real flag
// surface. Never calls `.parse(...)` -- only introspects (see collectFlags/
// allSubcommands above). Every subcommand here registers via its own
// shared `addXOptions` (commands.hpp) -- the same flag declarations the
// real command's own parse uses, so this tree can never drift out of sync
// with what CLI11 actually parses against.
void registerAllSubcommands(CLI::App& root, AllSubcommandOpts& opts) {
    CLI::App* exportSub =
        root.add_subcommand("export", "export a mesh (+ skin/animation) to glTF");
    husk::commands::addExportOptions(*exportSub, opts.exportOpts);

    CLI::App* infoSub = root.add_subcommand("info", "parse and print an M2 header");
    husk::commands::addInfoOptions(*infoSub, opts.infoOpts);

    CLI::App* dumpSub = root.add_subcommand("dump-chunks", "extract misc chunks to JSON");
    husk::commands::addDumpChunksOptions(*dumpSub, opts.dumpOpts);

    CLI::App* db2InfoSub = root.add_subcommand("db2-info", "parse and print a WDC5 DB2 file");
    husk::commands::addDb2InfoOptions(*db2InfoSub, opts.db2InfoOpts);

    CLI::App* db2ExportSub =
        root.add_subcommand("db2-export", "convert WDC5 DB2 file(s) to a real SQLite database");
    husk::commands::addDb2ExportOptions(*db2ExportSub, opts.db2ExportOpts);

    CLI::App* db2BuildSub =
        root.add_subcommand("db2-build", "build husk's own verified knowledge-base DB");
    husk::commands::addDb2BuildOptions(*db2BuildSub, opts.db2BuildOpts);

    CLI::App* blpExportSub = root.add_subcommand("blp-export", "convert BLP2 texture(s) to PNG");
    husk::commands::addBlpExportOptions(*blpExportSub, opts.blpExportOpts);

    CLI::App* appearanceSub =
        root.add_subcommand("appearance-string", "validate/normalize a husk-appearance/1 string");
    husk::commands::addAppearanceStringOptions(*appearanceSub, opts.appearanceOpts);

    CLI::App* resolveSub =
        root.add_subcommand("resolve", "print sources::Catalog's texture-resolution ledger as JSON");
    husk::commands::addResolveOptions(*resolveSub, opts.resolveOpts);

    CLI::App* exportTerrainSub =
        root.add_subcommand("export-terrain", "export one ADT tile to a canonical terrain bundle");
    husk::commands::addExportTerrainOptions(*exportTerrainSub, opts.exportTerrainOpts);

    CLI::App* exportWorldSub =
        root.add_subcommand("export-world", "export every ADT tile of every map to a canonical scene");
    husk::commands::addExportWorldOptions(*exportWorldSub, opts.exportWorldOpts);
}

std::string generateCompletionScript(const std::string& shell) {
    CLI::App root{"husk"};
    AllSubcommandOpts opts;
    registerAllSubcommands(root, opts);

    if (shell == "bash") return generateBashCompletion(root);
    if (shell == "zsh") return generateZshCompletion(root);
    throw std::runtime_error("husk: --print-completion: unsupported shell '" + shell +
                              "' -- supported: bash, zsh");
}

// `--config`/`--help` are plumbing, not part of a subcommand's own
// documented flag surface (README.md's flag tables never listed either --
// --config gets its own "Config file" prose section, --help is implied).
bool isDocExcludedFlag(const std::string& longName) {
    return longName == "--config" || longName == "--help";
}

// Markdown-table cell text can't contain a literal `|` or a bare newline --
// CLI11 descriptions are hand-written prose (see addExportOptions in
// cmd_export.cpp) that never uses either, but this guards the generator
// itself against silently emitting a broken table if that ever changes.
std::string escapeTableCell(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '|') {
            out += "&#124;";
        } else if (c == '\n') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

// One row per real `add_option`/`add_flag` registration, taken directly
// from CLI11's own introspection -- `opt->get_description()` is the exact
// same string `--help` prints, so there is no second, hand-copied
// explanation of what a flag does left to drift out of sync (see
// README.md's "Flags" table under `export`, spliced in verbatim between
// the `<!-- BEGIN/END GENERATED FLAG TABLE -->` markers this emits).
std::string generateFlagDocsMarkdown(const std::string& subName) {
    CLI::App root{"husk"};
    AllSubcommandOpts opts;
    registerAllSubcommands(root, opts);

    CLI::App* sub = nullptr;
    for (CLI::App* s : allSubcommands(root)) {
        if (s->get_name() == subName) sub = s;
    }
    if (sub == nullptr) {
        throw std::runtime_error("husk: --print-flag-docs: unknown subcommand '" + subName + "'");
    }

    std::ostringstream out;
    out << "<!-- BEGIN GENERATED FLAG TABLE: " << subName << " -->\n"
        << "<!-- generated by `husk --print-flag-docs=" << subName
        << "` -- do not hand-edit; regenerate with that command whenever " << subName
        << "'s flags change. -->\n"
        << "\n"
        << "| Flag | Short | Description | Default |\n"
        << "|---|---|---|---|\n";

    for (CLI::Option* opt : sub->get_options()) {
        if (opt->get_lnames().empty() && opt->get_snames().empty()) continue;
        std::string longName = opt->get_lnames().empty() ? "" : "--" + opt->get_lnames().front();
        if (isDocExcludedFlag(longName)) continue;

        std::string flagCell = longName.empty() ? "" : "`" + longName + "`";
        std::string shortCell = opt->get_snames().empty() ? "--" : "`-" + opt->get_snames().front() + "`";
        std::string descCell = escapeTableCell(opt->get_description());
        std::string defaultCell;
        if (opt->get_required()) {
            defaultCell = "-- (required)";
        } else {
            std::string def = opt->get_default_str();
            defaultCell = def.empty() ? "unset" : "`" + def + "`";
        }

        out << "| " << flagCell << " | " << shortCell << " | " << descCell << " | " << defaultCell
            << " |\n";
    }

    out << "\n<!-- END GENERATED FLAG TABLE: " << subName << " -->\n";
    return out.str();
}

// `--print-completion=<shell>` has no human reader (its only consumers are
// this build's own `completions/` capture step and, transitively, the
// installed completion script) -- deliberately absent from `usage` below,
// per DESIGN.md's "Shell completion generation" section. Scanned across
// every top-level argv position, not just argv[1], since it's plumbing
// rather than a real subcommand competing with info/export/dump-chunks for
// that slot.
bool tryPrintCompletion(int argc, char** argv, int& exitCode) {
    static const std::string kPrefix = "--print-completion=";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.compare(0, kPrefix.size(), kPrefix) != 0) continue;
        std::string shell = arg.substr(kPrefix.size());
        try {
            std::cout << generateCompletionScript(shell);
            exitCode = 0;
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            exitCode = 1;
        }
        return true;
    }
    return false;
}

// `--print-flag-docs=<subcommand>` -- same "hidden, no human ever types
// this by hand, --print-completion's own doc comment applies verbatim"
// tier as that flag; the only consumer is README.md's own regeneration
// step (see generateFlagDocsMarkdown's doc comment) and, transitively,
// tests/test_cli_flag_docs.cpp's drift check.
bool tryPrintFlagDocs(int argc, char** argv, int& exitCode) {
    static const std::string kPrefix = "--print-flag-docs=";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.compare(0, kPrefix.size(), kPrefix) != 0) continue;
        std::string subName = arg.substr(kPrefix.size());
        try {
            std::cout << generateFlagDocsMarkdown(subName);
            exitCode = 0;
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            exitCode = 1;
        }
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    int completionExitCode = 0;
    if (tryPrintCompletion(argc, argv, completionExitCode)) {
        return completionExitCode;
    }
    int flagDocsExitCode = 0;
    if (tryPrintFlagDocs(argc, argv, flagDocsExitCode)) {
        return flagDocsExitCode;
    }

    static const char* usage =
        "usage: husk <command> [args...]\n"
        "\n"
        "commands:\n"
        "  info <file.m2>              parse and print an M2 header\n"
        "  export <file.m2> [args...]  export a mesh (+ skin/animation) to glTF (see --help)\n"
        "  dump-chunks <file.m2|.bone> extract misc chunks to JSON (see --help)\n"
        "  db2-info <file.db2>          parse and print a WDC5 DB2 file (proof of concept, see --help)\n"
        "  db2-export <file.db2>|--dir <dir> <out.sqlite> [--dbd-dir DIR]\n"
        "                               convert WDC5 DB2 file(s) to a real SQLite database (see --help)\n"
        "  db2-build --db2-dir DIR --dbd-dir DIR --listfile FILE -o <out.sqlite>\n"
        "                               build husk's own verified knowledge-base DB (see --help)\n"
        "  blp-export <file.blp> <out.png>\n"
        "  blp-export --dir <blp-dir> <out-dir>\n"
        "                               convert BLP2 texture(s) to PNG (see --help)\n"
        "  appearance-string --validate <string>\n"
        "                               validate/normalize a husk-appearance/1 string (see --help)\n"
        "  resolve <file.m2> [args...]  print texture-resolution ledger as JSON (see --help)\n"
        "  export-terrain <tile.adt> <out-dir>\n"
        "                               export one ADT tile to a canonical terrain bundle\n"
        "  export-world <maps-root> <out-dir> --listfile F --listfile-root D [--map NAME]...\n"
        "                               every tile of every map, parallel, resumable\n"
        "  --version, -V                print the build version and exit\n"
        "\n"
        "run `husk <command> --help` for a command's full usage and defaults.\n";

    if (argc < 2) {
        std::cerr << usage;
        return 1;
    }

    std::string command = argv[1];
    char** rest = argv + 2;
    int restArgc = argc - 2;

    if (command == "info") {
        return husk::commands::info(restArgc, rest);
    }
    if (command == "export") {
        return husk::commands::exportGlb(restArgc, rest);
    }
    if (command == "dump-chunks") {
        return husk::commands::dumpChunks(restArgc, rest);
    }
    if (command == "db2-info") {
        return husk::commands::db2Info(restArgc, rest);
    }
    if (command == "db2-export") {
        return husk::commands::db2Export(restArgc, rest);
    }
    if (command == "db2-build") {
        return husk::commands::db2Build(restArgc, rest);
    }
    if (command == "blp-export") {
        return husk::commands::blpExport(restArgc, rest);
    }
    if (command == "appearance-string") {
        return husk::commands::appearanceString(restArgc, rest);
    }
    if (command == "resolve") {
        return husk::commands::resolve(restArgc, rest);
    }
    if (command == "export-terrain") {
        return husk::commands::exportTerrain(restArgc, rest);
    }
    if (command == "export-world") {
        return husk::commands::exportWorld(restArgc, rest);
    }
    if (command == "--help" || command == "-h") {
        std::cout << usage;
        return 0;
    }
    // A durable fact, not a per-invocation flag choice (~/docs/CLI.md
    // §2.5) -- HUSK_VERSION is baked in at CMake configure time
    // (CMakeLists.txt), never recomputed here.
    if (command == "--version" || command == "-V") {
        std::cout << "husk " << HUSK_VERSION << "\n";
        return 0;
    }

    std::cerr << "husk: unknown command '" << command << "'\n";
    return 1;
}
