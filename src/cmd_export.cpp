// Over FILE_SIZE.md's ceiling: §3's "flat independent helpers" exception
// (addExportOptions, resolveBones, ...) plus exportOneModel's own "one
// sequential process" exception, in one file -- REFACTOR/README.md's
// canon/writers split is expected to eventually separate the two.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <CLI/CLI.hpp>
#include <sqlite3.h>

#include "animationdata_db2.hpp"
#include "appearance_string.hpp"
#include "canon_item_builder.hpp"
#include "character_texture_context.hpp"
#include "chunk.hpp"
#include "cli_args.hpp"
#include "cmd_export_canon.hpp"
#include "commands.hpp"
#include "skin_resolution.hpp"
#include "husk_config.hpp"
#include "itemappearance_db2.hpp"
#include "listfile.hpp"
#include "read_file.hpp"
#include "listfile_cache.hpp"
#include "listfile_index.hpp"
#include "m2.hpp"
#include "modelfiledata_db2.hpp"
#include "skel.hpp"
#include "skin.hpp"
#include "texturefiledata_db2.hpp"
#include "sources/listfile_catalog.hpp"
#include "sources/resolved.hpp"

// `husk export`: one M2 (+ .skin/.skel/.anim/.bone/.phys sidecars and
// optional DB2 data) -> one model bundle (REFACTOR/BUNDLE_FORMAT.md).
// This file is the orchestrator: flag registration (addExportOptions),
// sidecar resolution (skin, skeleton, physics, bone corrections, gear),
// and the --from-list batch loop; canon assembly and the bundle write are
// cmd_export_canon.cpp's.
namespace husk::commands {

namespace {

// Resolves this model's bones -- inline first, falling back to an external
// .skel per the three-state --skel resolution (~/docs/CLI.md §2.11):
// 'none' means never look, even if a same-basename .skel exists; an
// explicit path overrides; unset auto-detects a same-basename '.skel' next
// to the model, else the one SKID names (listfile path, then the store's
// `_by_fdid/`, where a model's own name is its FileDataID). `bonesAreInline`/`haveSkel`/`skelBytes` are out-parameters
// since buildAnimations (below) needs to know which source's track offsets
// `bones` carries, and haveSkel/skelBytes are reused by --bones-dir
// resolution too.
//
// model.bones is already parsed; canon::enforcePartialFailurePolicy(model,
// "bones", kExportPartialFailurePolicy) must run first. A malformed bones
// array parses to an empty vector rather than throwing, and bonesAreInline
// below is derived from exactly that emptiness -- skipping the check would
// silently misread "parse failed" as "no inline bones," falling back to an
// external .skel or an unskinned mesh instead of erroring: a wrong data
// source, not just an empty field.
std::vector<m2::Bone> resolveBones(const std::string& modelPath, const m2::Model& model, bool skelGiven,
                                    bool skelNone, std::string skelPath, bool& bonesAreInline,
                                    bool& haveSkel, std::vector<uint8_t>& skelBytes,
                                    const husk::ListfileIndex& listfile, const std::string& listfileRoot) {
    canon::enforcePartialFailurePolicy(model, "bones", kExportPartialFailurePolicy);
    auto bones = model.bones;
    bonesAreInline = !bones.empty();
    haveSkel = false;

    if (bonesAreInline) {
        if (skelGiven && !skelNone) {
            std::cerr << "husk: note: '" << modelPath << "' has its own inline bones; ignoring '"
                      << skelPath << "'\n";
        }
    } else if (skelNone) {
        // Deliberately skip -- forces an unskinned mesh regardless of
        // whether a same-basename '.skel' actually exists.
    } else if (skelGiven) {
        skelBytes = readFileBytes(skelPath);
        haveSkel = true;
        bones = skel::parseBones(skelBytes);
    } else {
        // Not finding one isn't an error: plenty of 0-bone models
        // genuinely have no skeleton at all, and this model falls back to
        // the same unskinned-mesh output it always did.
        std::filesystem::path defaultSkel = std::filesystem::path(modelPath).replace_extension(".skel");
        std::error_code ec;
        bool found = std::filesystem::exists(defaultSkel, ec) && !ec;
        if (!found && model.header.skeletonFileId) {
            if (auto local = sources::localFileForFileDataId(listfile, listfileRoot, *model.header.skeletonFileId)) {
                defaultSkel = *local;
                found = true;
            }
        }
        if (found) {
            skelPath = defaultSkel.string();
            std::cerr << "husk: note: '" << modelPath << "' has 0 inline bones -- found and using '"
                      << skelPath << "'\n";
            skelBytes = readFileBytes(skelPath);
            haveSkel = true;
            bones = skel::parseBones(skelBytes);
        }
    }
    return bones;
}

// --knowledge-db: queries husk's own pre-built knowledge base
// (`husk db2-build`; design history in git, see
// TODO/TEXTURE_POOL_RECALL_TODO.md's cross-validation open item) for this model's
// resolved object-skin texture, via its own FileDataID (looked up in the
// same database's `models` table by relative path against
// `--listfile-root`), and that texture's own real path (via the
// database's own `textures` table -- lets --knowledge-db alone resolve
// texture_id -> path cleanly, without also requiring a separate
// --listfile load just for this one texture). Never a live DB2/CASC read
// -- the knowledge base is itself a local, pre-built artifact, same
// "locally-extracted files only" tier as every other sidecar. Returns 0/
// empty (unresolved) rather than throwing on any lookup miss -- a model
// or texture outside the knowledge base's coverage is a real, expected
// case, not an error.
struct KbObjectSkinResolution {
    uint32_t textureFileDataId = 0;
    std::string texturePath;
};

// Tier 5 ("knowledge base") of RESOURCE_CATALOG.md's tier order --
// documented-known-wrong (TODO/TEXTURE_POOL_RECALL_TODO.md: same-slot
// cross-item collisions), so its `Resolved<T>::reason` on a hit carries
// that caveat as real provenance rather than a separate ad-hoc warning at
// the call site (REFACTOR/CLI_AND_TOOLING.md §5's original fix, now folded
// into the same envelope every other tier reports through -- see
// REFACTOR_LOG.md's 2026-08-28 entry). A miss (kb path empty, db won't
// open, model path not under listfileRoot, no matching row at any of the
// three joins) is a real, expected case, not an error -- see this
// function's own callers, which treat 0/empty as "nothing resolved,
// fall back to whatever else is available."
husk::sources::Resolved<KbObjectSkinResolution> resolveObjectSkinTextureFromKb(const std::string& kbPath,
                                                                                 const std::string& modelPath,
                                                                                 const std::string& listfileRoot) {
    using husk::sources::Resolved;
    using husk::sources::ResolutionTier;
    if (kbPath.empty()) {
        return Resolved<KbObjectSkinResolution>::miss(ResolutionTier::KnowledgeBase, "--knowledge-db not given");
    }
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(kbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "husk: note: --knowledge-db '" << kbPath << "' couldn't be opened -- skipping\n";
        sqlite3_close(db);
        return Resolved<KbObjectSkinResolution>::miss(ResolutionTier::KnowledgeBase,
                                                         "couldn't open '" + kbPath + "'");
    }

    std::error_code ec;
    std::string relPath;
    if (!listfileRoot.empty()) {
        auto rel = std::filesystem::relative(std::filesystem::path(modelPath), listfileRoot, ec);
        if (!ec) relPath = rel.generic_string();
    }
    if (relPath.empty()) {
        sqlite3_close(db);
        return Resolved<KbObjectSkinResolution>::miss(
            ResolutionTier::KnowledgeBase, "'" + modelPath + "' is not relative to listfileRoot '" + listfileRoot + "'");
    }
    std::transform(relPath.begin(), relPath.end(), relPath.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    uint32_t modelFdid = 0;
    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT file_data_id FROM models WHERE lower(path) = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, relPath.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) modelFdid = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
        sqlite3_finalize(stmt);
    }
    if (modelFdid == 0) {
        sqlite3_close(db);
        return Resolved<KbObjectSkinResolution>::miss(ResolutionTier::KnowledgeBase,
                                                         "no 'models' row for '" + relPath + "'");
    }

    KbObjectSkinResolution result;
    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT texture_file_data_id FROM model_object_skin_texture WHERE model_file_data_id = ?",
                            -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, modelFdid);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result.textureFileDataId = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    if (result.textureFileDataId == 0) {
        sqlite3_close(db);
        return Resolved<KbObjectSkinResolution>::miss(
            ResolutionTier::KnowledgeBase,
            "no model_object_skin_texture row for model FileDataID " + std::to_string(modelFdid));
    }

    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT path FROM textures WHERE file_data_id = ?", -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, result.textureFileDataId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) result.texturePath = reinterpret_cast<const char*>(text);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    // Verbatim text of the pre-Resolved<T> ad-hoc warning
    // (REFACTOR/CLI_AND_TOOLING.md §5's original fix) -- kept exact so it
    // still carries the caller's own message unchanged, just sourced from
    // provenance now instead of constructed again at the call site.
    std::string reason = "--knowledge-db resolved texture FileDataID " + std::to_string(result.textureFileDataId) +
                          " for this model's object-skin slot -- this resolution is known to produce wrong "
                          "same-slot matches often enough not to trust as primary (see "
                          "TODO/TEXTURE_POOL_RECALL_TODO.md's cross-validation open item); verify before relying on it";
    return Resolved<KbObjectSkinResolution>::hit(result, ResolutionTier::KnowledgeBase, std::move(reason));
}

}  // namespace

// See commands.hpp's doc comment on ExportOptions: this is the one place
// export's flag surface is declared, shared by exportBundle's own real parse
// and main.cpp's `--print-completion` introspection.
void addExportOptions(CLI::App& app, ExportOptions& opts) {
    addConfigOption(app);

    // Not ->required() here even though single-file mode needs it -- --from-list
    // mode (below) takes its model paths from a file instead, so requiredness is
    // enforced by hand after parsing (see exportBundle), once it's known which mode
    // this invocation is actually in.
    app.add_option("-i,--input,input", opts.modelPath,
                    "the .m2 file to export -- required for single-file mode (--from-list "
                    "supplies model paths instead); also usable as the first bare positional "
                    "argument")
        ->group("Input / output");
    app.add_option("-o,--output,output", opts.outputPath,
                    "the bundle directory to write (default: '<model-basename>.bundle' beside the "
                    "model); an existing one is written over -- mutually exclusive with "
                    "--from-list, which always writes into --output-dir instead; also usable as "
                    "the last bare positional argument")
        ->group("Input / output");
    app.add_option("--from-list", opts.fromListArg,
                    "batch mode: a plain text file of .m2 paths, one per line (blank lines and "
                    "'#'-prefixed comment lines skipped) -- exports every one of them with the "
                    "same options given on this command line, opening --listfile once and reusing "
                    "it across the whole batch rather than reloading it per file, the same "
                    "'--from-list <ids-file> <out-dir>' shape casc-tool's own 'extract-batch' has. "
                    "Requires --output-dir instead of --output; one file failing is reported and "
                    "skipped, not fatal to the rest of the batch -- see the run's own final "
                    "'N succeeded, M failed' summary line")
        ->group("Batch");
    app.add_option("--output-dir", opts.outputDirArg,
                    "directory to write each --from-list entry's bundle into (named "
                    "'<model-basename>.bundle', or '<parent-dir-name>_<model-basename>.bundle' if "
                    "two entries in the same batch share a basename) -- required alongside "
                    "--from-list, meaningless without it")
        ->group("Batch");
    app.add_option("-s,--skin", opts.skinArg,
                    "a .skin path, or 'auto' to resolve via the model's own SFID chunk, falling "
                    "back to a same-basename numbered scan next to the model if that doesn't "
                    "resolve -- never 'none': a .skin file is the sole source of triangle/"
                    "submesh/batch data, not optional enrichment")
        ->capture_default_str()
        ->check(
            [](const std::string& v) -> std::string {
                if (v == "none") {
                    return "--skin takes a .skin path or 'auto', never 'none' -- a .skin file is "
                           "the sole source of triangle/submesh/batch data, not optional "
                           "enrichment (pass an explicit path instead)";
                }
                return "";
            },
            "SKIN")
        ->group("Model sidecars");
    app.add_option("-t,--textures", opts.texturesArg,
                    "directory of '<FileDataID>.png' or '<FileDataID>.blp' texture files, or "
                    "'none' to resolve no textures from a directory (default: the model's own "
                    "directory)")
        ->group("Input / output");
    app.add_option("--textures-out", opts.texturesOutArg,
                    "directory to also write each --textures .blp's decoded .png to, mirroring its "
                    "location under --textures (default: unset -- nothing extra written)")
        ->group("Input / output");
    app.add_option("--glb", opts.glbArg,
                    "also write the bundle's lean glTF projection (best effort: no options, presets or "
                    "effects) to this .glb path -- single-file mode only")
        ->group("Input / output");
    app.add_flag("--explain-textures", opts.explainTextures,
                 "after exporting, print the texture-resolution ledger: for every texture slot, "
                 "which tier answered it (literal / listfile / fuzzy same-basename pool / "
                 "knowledge base), which file it resolved to, whether the pool was genuinely "
                 "ambiguous, and on a miss the reason it failed -- off by default. Use this to "
                 "diff resolution behavior before vs. after a change, rather than inferring it "
                 "from the export summary")
        ->group("Diagnostics");
    app.add_option("--skin-dir", opts.skinDirArg,
                    "directory 'auto' searches for the SFID-declared '<FileDataID>.skin' file, or "
                    "'none' to skip that stage (default: the model's own directory)")
        ->group("Model sidecars");
    app.add_option("-a,--anim", opts.animArg,
                    "'auto': inline + global-sequence + best-effort external directory search; "
                    "'inline': inline + global-sequence only, no external search; 'none': no "
                    "animation clips at all (bind pose only); or a directory of "
                    "'<FileDataID>.anim' files, falling back to "
                    "'<model-basename><animId>-<subId>.anim' when a FileDataID-named file isn't found")
        ->capture_default_str()
        ->group("Model sidecars");
    app.add_option("--skel", opts.skelArg,
                    "external .skel path (only relevant for a model with 0 inline bones), or "
                    "'none' to never look for one (default: a same-basename '.skel' next to the "
                    "model, if any)")
        ->group("Model sidecars");
    app.add_option("--bones-dir", opts.bonesDirArg,
                    "directory of already-extracted '<FileDataID>.bone' files (per the model's/"
                    "'.skel's BFID array), tried before the listfile and store, or 'none' to look "
                    "only there (default: the model's own directory) -- written as the bundle's "
                    "correction sets, data only")
        ->group("Model sidecars");
    app.add_option("--phys", opts.physArg,
                    "external .phys path (per the model's own PFID scalar), or 'none' to never "
                    "look for one (default: a same-basename '.phys' next to the model, if any) -- "
                    "written as the bundle's physics: bodies, shapes and joints, data only")
        ->group("Model sidecars");
    app.add_option("--db2-dir", opts.db2DirArg,
                    "directory of extracted DB2 files (lowercase casc-tool names) -- with --dbd-dir, "
                    "fills the bundle's character customization, creature options and presets, "
                    "character textures, and --appearance gear; unset (default) skips them. 'none' "
                    "opts out even when --config/$HUSK_CONFIG supplies a value")
        ->group("Game data");
    app.add_option("--dbd-dir", opts.dbdDirArg,
                    "a local WoWDBDefs checkout (github.com/wowdev/WoWDBDefs), used to resolve "
                    "--db2-dir's real column names -- required alongside --db2-dir/"
                    "--char-layout-id or --db2-dir/--customization-choice-ids, same role as "
                    "`husk db2-export`'s own --dbd-dir. 'none' explicitly opts out even when "
                    "--config/$HUSK_CONFIG supplies a value")
        ->group("Game data");
    app.add_option("--char-layout-id", opts.charLayoutIdArg,
                    "a real CharComponentTextureLayoutsID (see `husk db2-export`) to filter "
                    "--db2-dir's data down to. Optional -- unset (default) auto-derives it from "
                    "the resolved --chr-model-id's own real ChrModel.CharComponentTextureLayoutID "
                    "column (same auto|none|<id> derivation --chr-model-id itself already uses; "
                    "--chr-model-id none disables this too); an explicit value always overrides. "
                    "Requires --db2-dir/--dbd-dir either way")
        ->group("Character");
    app.add_option("--customization-choice-ids", opts.customizationChoiceIdsArg,
                    "comma-separated ChrCustomizationChoiceID(s) whose textures the character "
                    "texture slots resolve to, instead of the default choice per option; requires "
                    "--db2-dir/--dbd-dir")
        ->group("Character");
    app.add_option("--appearance", opts.appearanceArg,
                    "a husk-appearance/1 string (src/appearance_string.hpp): written as one preset "
                    "-- its 'cust' choices (also used as --customization-choice-ids) and its 'gear' "
                    "items, each (SLOT, ItemModifiedAppearanceID) resolved to the item's models "
                    "(exported as bundles under 'items/'), textures and body-section overlays. "
                    "Mutually exclusive with --customization-choice-ids; requires --db2-dir/"
                    "--dbd-dir. 'race'/'sex' are informational only -- --chr-model-id's derivation "
                    "decides the character")
        ->group("Appearance / gear");
    app.add_option("--chr-model-id", opts.chrModelIdArg,
                    "a real ChrModelID (see ChrModel.db2 / `husk db2-export`), the literal 'auto' to "
                    "derive one automatically, or 'none' to explicitly disable derivation. Same "
                    "auto|none|<id> three-state convention as --textures/--skin-dir/--skel -- "
                    "**unset defaults to 'auto'**: whenever --db2-dir/--dbd-dir are given at all, "
                    "husk tries to derive the character model whose texture layout and choice "
                    "textures the texture slots resolve through. Two derivation paths, tried in "
                    "order: (1) primary, "
                    "if --listfile/--listfile-root resolve this .m2's own real FileDataID -- an exact "
                    "identity match via CreatureModelData/CreatureDisplayInfo/ChrModel, never "
                    "ambiguous for a real file (e.g. correctly tells dracthyrmale.m2 apart from "
                    "dracthyrdragon.m2 even though both are valid 'Dracthyr, male' answers); "
                    "(2) fallback, only when no FileDataID was found: this .m2's own filename against "
                    "the real client-side <ClientFileString><\"male\"|\"female\">[_hd].m2 convention "
                    "(exact, case-insensitive match only, never fuzzy) -- can be genuinely ambiguous "
                    "when a race+sex has more than one real model (e.g. Dracthyr's dragon form). "
                    "Either path reports and skips rather than guessing when it can't resolve to "
                    "exactly one real ChrModelID. Either way, once resolved, auto-selects a "
                    "*sensible default* customization choice per option: the choice "
                    "with the lowest real OrderIndex under each ChrCustomizationOption belonging to "
                    "this model; NOT a client-verified default (no DB2 table states a player "
                    "default), husk's own heuristic. "
                    "Ignored when --customization-choice-ids is "
                    "also given (an explicit pick always wins); requires --db2-dir/--dbd-dir too")
        ->group("Character");
    app.add_option("--object-skin-texture-id", opts.objectSkinTextureIdArg,
                    "a real texture FileDataID to fill into any type=2 (object_skin) texture "
                    "slot that has no FileDataID of its own -- husk can't derive this on its "
                    "own (it needs the real ItemDisplayInfo/TextureFileData DB2 chain resolved "
                    "externally), so it must be supplied directly; unset (default) leaves those "
                    "slots unresolved, same as before this flag existed")
        ->group("Appearance / gear");
    app.add_option("--knowledge-db", opts.knowledgeDbArg,
                    "husk's own pre-built knowledge-base SQLite database (`husk db2-build`, "
                    "TODO/TEXTURE_POOL_RECALL_TODO.md) -- when given, resolves this model's own "
                    "object-skin texture automatically (via the database's own model->texture "
                    "mapping, keyed by the model's FileDataID under --listfile-root), instead of "
                    "requiring --object-skin-texture-id per invocation; --object-skin-texture-id "
                    "still wins if both are given")
        ->group("Diagnostics");
    app.add_option("--listfile", opts.listfileArg,
                    "a local community-listfile.csv-style snapshot (FileDataID;path per line, "
                    "github.com/wowdev/wow-listfile) -- last-resort fallback when a "
                    "FileDataID-named texture slot isn't found as '<FileDataID>.{blp,png}' next "
                    "to --textures: looks up its real name and tries '<listfile-root>/<real-path>' "
                    "instead, before falling back to the same-basename fuzzy pool. Optional; unset "
                    "(default) skips this tier entirely, same as every other opt-in sidecar -- "
                    "never fetched by husk itself, same tier as --dbd-dir's WoWDBDefs checkout. "
                    "'none' explicitly opts out even when --config/$HUSK_CONFIG supplies a value")
        ->group("Game data");
    app.add_option("--listfile-root", opts.listfileRootArg,
                    "the corpus root --listfile's paths are relative to -- deliberately separate "
                    "from --textures, since --textures also drives the *directory-local* "
                    "embedded-filename/same-basename matching tried first, and a real corpus root "
                    "is typically many directories away from any one model. Only meaningful "
                    "alongside --listfile; ignored otherwise. Default: --textures itself, for the "
                    "case where a single directory happens to serve both roles. 'none' explicitly "
                    "opts out even when --config/$HUSK_CONFIG supplies a value")
        ->group("Game data");
}

// --appearance gear as canon items (ItemModifiedAppearance -> display ->
// models, model textures, body-section overlays). Empty, with a note,
// without the DB2 tables.
std::vector<canon::Item> resolveGearItems(const std::string& db2Dir, const std::string& dbdDir,
                                          const std::vector<appearance::GearEntry>& gear) {
    if (gear.empty()) return {};
    if (db2Dir.empty() || dbdDir.empty()) {
        std::cerr << "husk: note: --appearance gear needs --db2-dir and --dbd-dir -- no items\n";
        return {};
    }
    std::optional<itemappearance::Data> items = itemappearance::load(db2Dir, dbdDir, std::cerr);
    if (!items) return {};
    return canon::assembleEquippedItems(*items, modelfiledata::load(db2Dir, dbdDir, std::cerr),
                                        texturefiledata::load(db2Dir, dbdDir, std::cerr), gear, std::cerr);
}

// Each gear item's model as a bundle of its own, `items/<fdid>.bundle/`
// inside the character's bundle, textured with the item's object-skin
// texture (type 2) when it has one. Returns each item's manifest uri
// relative to the character bundle; empty where the model has no local
// file or its export failed (both reported).
std::vector<std::string> exportGearItemBundles(const std::vector<canon::Item>& gear, const ListfileIndex& listfile,
                                               const std::string& listfileRoot, const std::string& bundleDir,
                                               const writers::StoreLink* textureStore) {
    std::vector<std::string> uris(gear.size());
    for (size_t i = 0; i < gear.size(); ++i) {
        const canon::GeometryComponent* geometry = nullptr;
        for (const canon::Component& c : gear[i].components) {
            if (const auto* g = std::get_if<canon::GeometryComponent>(&c)) geometry = g;
        }
        if (!geometry || geometry->modelFileDataIds.empty()) continue;
        uint32_t fdid = geometry->modelFileDataIds.front();
        std::optional<std::filesystem::path> path = sources::localFileForFileDataId(listfile, listfileRoot, fdid);
        if (!path) {
            std::cerr << "husk: note: gear model FileDataID " << fdid << " has no local file -- not exported\n";
            continue;
        }

        // CLI11 takes a vector argv last-argument-first.
        std::vector<std::string> reversedArgv;
        for (const auto& m : geometry->materials) {
            if (m.textureType == 2 && m.fileDataId != 0) {
                reversedArgv = {std::to_string(m.fileDataId), "--object-skin-texture-id"};
            }
        }
        // The item's own defaults, never the character's flags: its
        // --appearance would recurse, and its textures sit beside it.
        ExportOptions itemOpts;
        CLI::App itemApp{"husk export (gear item model)", "husk export"};
        addExportOptions(itemApp, itemOpts);
        try {
            itemApp.parse(reversedArgv);
        } catch (const CLI::ParseError& e) {
            std::cerr << "husk: note: gear model FileDataID " << fdid << ": " << e.what() << " -- not exported\n";
            continue;
        }


        std::string uri = "items/" + std::to_string(fdid) + ".bundle";
        std::string itemDir = (std::filesystem::path(bundleDir) / uri).string();
        if (exportOneModel(itemOpts, itemApp, path->string(), itemDir, true, listfile, fdid, nullptr, textureStore) == 0) {
            uris[i] = uri + "/manifest.json";
        } else {
            std::cerr << "husk: note: gear model FileDataID " << fdid << " failed to export\n";
        }
    }
    return uris;
}

std::optional<creaturedisplay::Data> loadCreatureDisplays(const ExportOptions& opts, CLI::App& app) {
    if (!app.count("--db2-dir") || !app.count("--dbd-dir")) return std::nullopt;
    if (opts.db2DirArg == "none" || opts.dbdDirArg == "none") return std::nullopt;
    std::optional<creaturedisplay::Data> data = creaturedisplay::load(opts.db2DirArg, opts.dbdDirArg, std::cerr);
    if (!data) {
        std::cerr << "husk: warning: creature display tables unreadable under --db2-dir '" << opts.db2DirArg
                  << "'; bundles get no creature options\n";
    }
    return data;
}

// The single-model export pipeline, unchanged in substance from the old
// monolithic exportBundle -- factored out so --from-list (below) can call it
// once per entry, sharing one already-parsed `opts`/`app` and one
// already-loaded `listfile` across the whole batch instead of re-parsing
// argv or re-reading a multi-million-line CSV per file. `modelPath`/
// `outputPath`/`outputGiven` are passed explicitly rather than read off
// `opts` directly, since those three are the only fields that vary per
// batch entry -- everything else (--textures, --db2-dir, ...) applies
// identically to every file in the batch, same as a real casc-tool
// `extract-batch` run shares its own storage/listfile handle across every
// extracted entry.
// `listfile` is taken by const reference, never copied or mutated:
// --knowledge-db's (below) per-model object-skin override goes through
// sources::Catalog::registerPathOverride instead, which keeps its own
// separate map -- so a --from-list batch sharing one loaded ListfileIndex
// across every entry never risks one model's override leaking into the
// next (DESIGN.md's "Listfile index" section has the measured case for why
// this is a reference now: a ListfileIndex may be an mmap-backed object
// whose entire point is avoiding a per-process copy).
int exportOneModel(const ExportOptions& opts, CLI::App& app, const std::string& modelPath,
                    const std::string& outputPathIn, bool outputGiven, const husk::ListfileIndex& listfile,
                    std::optional<uint32_t> modelFileDataId, const creaturedisplay::Data* creatures,
                    const writers::StoreLink* textureStore) {
    std::string outputPath = outputPathIn;
    bool skinDirGiven = app.count("--skin-dir") > 0;

    // An explicit .skin path has no SFID-resolution stage for --skin-dir
    // to modify.
    if (opts.skinArg != "auto" && skinDirGiven) {
        std::cerr << "husk: --skin-dir only does anything when --skin is 'auto'\n";
        return 1;
    }
    bool appearanceGiven = app.count("--appearance") > 0;
    if (appearanceGiven && app.count("--customization-choice-ids") > 0) {
        std::cerr << "husk: --appearance and --customization-choice-ids are mutually exclusive -- "
                     "--appearance's own 'cust' field already supplies the same choice IDs\n";
        return 1;
    }

    // The output is the bundle directory itself; an existing one is
    // written over.
    if (!outputGiven) {
        outputPath = std::filesystem::path(modelPath).replace_extension(".bundle").string();
        std::cerr << "husk: note: no output path given -- writing to '" << outputPath << "'\n";
    }

    // -o is where the user *wants* the file to end up, not an assertion that
    // the directory structure already exists -- create any missing parent
    // directories up front (same TODO finding as above), and do it here,
    // before the real parse/export pipeline below, so a bad -o path fails
    // fast instead of costing a multi-second re-parse to discover.
    {
        auto parent = std::filesystem::path(outputPath).parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "husk: couldn't create output directory '" << parent.string()
                          << "': " << ec.message() << "\n";
                return 1;
            }
        }
    }

    std::filesystem::path modelDir = std::filesystem::path(modelPath).parent_path();
    std::string modelDirStr = modelDir.empty() ? "." : modelDir.string();

    // --textures/--skin-dir: three-state resolution (~/docs/CLI.md §2.11)
    // -- unset defaults to the model's own directory (a real casc-tool-
    // style extraction drops the .m2 and every sidecar it needs into one
    // directory together, see README.md's Usage section); an explicit
    // 'none' means deliberately skip, never attempted; anything else
    // overrides. (--skel gets the identical three states below, but its
    // "unset" default -- a same-basename '.skel' next to the model -- is a
    // filesystem *check*, not a fixed directory, so it's resolved later,
    // once bones are known to actually be needed.)
    std::string texturesDir = app.count("--textures") ? opts.texturesArg : modelDirStr;
    if (texturesDir == "none") texturesDir.clear();

    // `listfile` itself is a parameter now, not loaded here -- see this
    // function's own doc comment: exportBundle loads it once, before either
    // the single-file call or the --from-list loop, never per model.
    // --listfile-root is deliberately its own directory, not texturesDir --
    // see that flag's own help text for why reusing texturesDir would
    // silently break the directory-local matching tried before it.
    std::string listfileRoot = app.count("--listfile-root") ? opts.listfileRootArg : texturesDir;
    if (listfileRoot == "none") listfileRoot.clear();

    // --textures-out: unset (the default) means "no disk copy at all" --
    // unlike --textures/--skin-dir/--bones-dir, there's no directory this
    // should silently default to, since writing files unasked is a much
    // bigger deal than reading them.
    std::string texturesOutDir = app.count("--textures-out") ? opts.texturesOutArg : "";

    std::string bonesDir = app.count("--bones-dir") ? opts.bonesDirArg : modelDirStr;
    if (bonesDir == "none") bonesDir.clear();

    bool skinDirNone = skinDirGiven && opts.skinDirArg == "none";
    std::string skinDir = skinDirNone ? "" : (skinDirGiven ? opts.skinDirArg : modelDirStr);

    // --anim: four states, not three (see DESIGN.md's "Does inline
    // generalize past --anim?") -- inline M2Sequence/global-sequence bone
    // tracks and external-.anim-directory resolution are independent axes.
    // 'inline' deliberately leaves `animDir` empty rather than setting a
    // separate flag: buildAnimations already treats an empty animDir as
    // "skip external resolution," which is exactly what 'inline' means.
    std::string animDir;
    bool animNone = false;  // --anim none: the bundle gets no clips (bind pose only)
    if (opts.animArg == "auto") {
        animDir = modelDirStr;
    } else if (opts.animArg == "none") {
        animNone = true;
    } else if (opts.animArg != "inline") {
        animDir = opts.animArg;
    }

    bool skelGiven = app.count("--skel") > 0;
    bool skelNone = skelGiven && opts.skelArg == "none";
    std::string skelPath = (skelGiven && !skelNone) ? opts.skelArg : "";

    bool physGiven = app.count("--phys") > 0;
    bool physNone = physGiven && opts.physArg == "none";
    std::string physPath = (physGiven && !physNone) ? opts.physArg : "";

    // --db2-dir/--dbd-dir get no 'auto' (full rationale: DESIGN.md's
    // "Three-state resolution, not two"); 'none' opts out of a
    // --config/$HUSK_CONFIG-supplied value for this one invocation.
    std::string db2Dir = app.count("--db2-dir") ? opts.db2DirArg : "";
    if (db2Dir == "none") db2Dir.clear();
    std::string dbdDirForChr = app.count("--dbd-dir") ? opts.dbdDirArg : "";
    if (dbdDirForChr == "none") dbdDirForChr.clear();
    std::string charLayoutIdArg = app.count("--char-layout-id") ? opts.charLayoutIdArg : "";
    std::string customizationChoiceIdsArg =
        app.count("--customization-choice-ids") ? opts.customizationChoiceIdsArg : "";
    std::vector<appearance::GearEntry> gearEntries;
    if (appearanceGiven) {
        // Mutual exclusivity with --customization-choice-ids already
        // enforced above -- customizationChoiceIdsArg is still empty here,
        // safe to fill from --appearance's own 'cust' field. A malformed
        // --appearance string is a direct user mistake, same "bad flag
        // value fails fast" treatment husk-appearance/1's own parser
        // already gives `husk appearance-string`.
        appearance::AppearanceString parsed;
        try {
            parsed = appearance::parse(opts.appearanceArg);
        } catch (const appearance::ParseError& e) {
            std::cerr << "husk: --appearance: " << e.what() << "\n";
            return 1;
        }
        if (!parsed.customizationChoiceIds.empty()) {
            std::ostringstream ids;
            for (size_t i = 0; i < parsed.customizationChoiceIds.size(); ++i) {
                if (i) ids << ",";
                ids << parsed.customizationChoiceIds[i];
            }
            customizationChoiceIdsArg = ids.str();
        }
        gearEntries = parsed.gear;
    }
    std::string chrModelIdArg = app.count("--chr-model-id") ? opts.chrModelIdArg : "";
    // One catalog per model export: every texture slot and LOD level shares
    // its resolution state (REFACTOR/RESOURCE_CATALOG.md).
    husk::sources::Catalog catalog(texturesDir, listfile, listfileRoot, texturesOutDir);

    uint32_t objectSkinTextureFileDataId = 0;
    if (app.count("--object-skin-texture-id")) {
        try {
            objectSkinTextureFileDataId = static_cast<uint32_t>(std::stoul(opts.objectSkinTextureIdArg));
        } catch (const std::exception&) {
            std::cerr << "husk: note: --object-skin-texture-id '" << opts.objectSkinTextureIdArg
                      << "' isn't a valid integer -- ignoring\n";
        }
    } else if (app.count("--knowledge-db")) {
        auto kbResolved = resolveObjectSkinTextureFromKb(opts.knowledgeDbArg, modelPath, listfileRoot);
        objectSkinTextureFileDataId = kbResolved.found() ? kbResolved.value->textureFileDataId : 0;
        // Fills the embed path's --listfile fallback tier
        // (sources::Catalog::texture(), tier 2) straight from the knowledge
        // base's own 'textures' table, without requiring a separate
        // --listfile load just for this one texture -- only when
        // --listfile didn't already resolve this fdid itself. Registered
        // on `catalog` instead of injecting into `listfile` itself, so this
        // model's own KB override can never leak into any other caller that
        // also holds a reference to the same map.
        // Known-wrongness (I4, REFACTOR/CLI_AND_TOOLING.md §5) still rides
        // kbResolved.reason -- resolveObjectSkinTextureFromKb's own doc
        // comment -- instead of being reconstructed here.
        if (kbResolved.found() && !kbResolved.value->texturePath.empty()) {
            catalog.registerPathOverride(objectSkinTextureFileDataId, kbResolved.value->texturePath);
            std::cerr << "husk: warning: " << kbResolved.reason << "\n";
        }
    }

    try {
        auto modelBytes = readFileBytes(modelPath);
        m2::Model model = m2::loadModel(modelBytes);
        const m2::Header& header = model.header;

        // export fails fast on a malformed field it reads (DESIGN.md's
        // "Two parse-failure contracts" paragraph, Key design decisions) --
        // canon::enforcePartialFailurePolicy enforces that here, in the
        // same order the fields below are read. bones/sequences are
        // excluded here: resolveBones and the canon assembly enforce it
        // themselves, at the point each is actually read.
        canon::enforcePartialFailurePolicy(model, "vertices", kExportPartialFailurePolicy);

        if (header.version < m2::kMinVerifiedRecordStrideVersion) {
            std::cerr << "husk: warning: '" << modelPath << "' is version " << header.version
                      << ", below Wrath (264) -- this parser's bones/sequences/ribbon_emitters "
                         "record sizes are only documented and verified for Wrath+; the "
                         "exported bind pose/animation/ribbon data may be silently wrong rather "
                         "than failing loudly\n";
        }

        canon::enforcePartialFailurePolicy(model, "materials", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "textures", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "texture_combos", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "texture_coord_combos", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "colors", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "texture_weights", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "texture_weight_combos", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "texture_transforms", kExportPartialFailurePolicy);
        canon::enforcePartialFailurePolicy(model, "texture_transform_combos", kExportPartialFailurePolicy);

        auto skinsToExport = resolveSkinsToExport(header, modelPath, skinDir, listfileRoot, skinDirNone,
                                                  /*lodGiven=*/false, "", opts.skinArg);

        bool bonesAreInline = false;
        bool haveSkel = false;
        std::vector<uint8_t> skelBytes;
        auto bones = resolveBones(modelPath, model, skelGiven, skelNone, skelPath, bonesAreInline,
                                   haveSkel, skelBytes, listfile, listfileRoot);
        // A malformed inline sequences array parses empty; unchecked, that
        // reads as "no animations" instead of an error.
        if (bonesAreInline) canon::enforcePartialFailurePolicy(model, "sequences", kExportPartialFailurePolicy);

        if (!bones.empty()) {
            chartexture::Args charArgs{db2Dir, dbdDirForChr, charLayoutIdArg, chrModelIdArg, customizationChoiceIdsArg};
            if (auto ctx = chartexture::build(charArgs, modelPath, listfile, listfileRoot, std::cerr)) {
                catalog.setCharacterTextureContext(std::move(*ctx));
            }
        }
        // A tier without geometry still exports: a model that only emits
        // (steam, a spell impact) is its effects and skeleton.
        std::vector<LodSkinPath> lodSkins = opts.skinArg == "auto"
                                                ? resolveLodSkinPaths(header, modelPath, skinDir, listfileRoot)
                                                : std::vector<LodSkinPath>{};
        BundleSidecars sidecars;
        sidecars.leanGlbPath = opts.glbArg;
        sidecars.noAnimation = animNone;
        if (!db2Dir.empty() && !dbdDirForChr.empty()) {
            if (auto animData = animationdata::load(db2Dir, dbdDirForChr, std::cerr)) {
                sidecars.animationNames = animationdata::toNameMap(*animData);
            }
        }
        sidecars.listfile = &listfile;
        sidecars.bonesDir = bonesDir;
        if (physGiven && !physNone) {
            sidecars.physPath = physPath;
        } else if (!physGiven) {
            std::filesystem::path beside = std::filesystem::path(modelPath).replace_extension(".phys");
            std::error_code ec;
            if (std::filesystem::exists(beside, ec)) sidecars.physPath = beside.string();
        }
        if (appearanceGiven) {
            sidecars.appearance = opts.appearanceArg;
            std::stringstream ids(customizationChoiceIdsArg);
            for (std::string id; std::getline(ids, id, ',');) {
                sidecars.appearanceChoiceIds.push_back(static_cast<uint32_t>(std::stoul(id)));
            }
            sidecars.gear = resolveGearItems(db2Dir, dbdDirForChr, gearEntries);
            sidecars.gearModelUris = exportGearItemBundles(sidecars.gear, listfile, listfileRoot, outputPath, textureStore);
        }
        writeCanonBundleOnly(model, skinsToExport.front().second, lodSkins, catalog, modelPath,
                             modelFileDataId ? modelFileDataId
                                             : sources::fileDataIdForLocalFile(listfile, modelPath, listfileRoot),
                             objectSkinTextureFileDataId, animDir, listfileRoot, bonesAreInline, haveSkel, skelBytes,
                             outputPath, creatures, textureStore, sidecars);
        std::cerr << "husk: wrote bundle '" << outputPath << "/'\n";
        // I4: which tier answered each texture slot is a read operation.
        if (opts.explainTextures) std::cout << catalog.describe();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: export failed: " << e.what() << "\n";
        return 1;
    }
}

// Reads --from-list's plain-text worklist: one .m2 path per line, blank
// lines and '#'-prefixed comments skipped -- same convention casc-tool's
// own --from-list uses for its FileDataID worklists. A missing/unreadable
// --from-list is a direct user mistake (bad path), thrown loudly, same
// "bad flag value fails fast, a malformed *line* doesn't" split
// loadListfile already draws for --listfile.
std::vector<std::string> readModelListFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("couldn't open --from-list '" + path + "'");
    }
    std::vector<std::string> result;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF-saved copies
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;
        size_t end = line.find_last_not_of(" \t");
        result.push_back(line.substr(start, end - start + 1));
    }
    return result;
}

// Picks each batch entry's output filename: '<model-basename>.bundle', or,
// when that collides with an earlier entry in the same batch (real corpora
// commonly reuse basenames across expansion/race subdirectories),
// '<parent-dir-name>_<model-basename>.bundle', falling back to a numeric
// suffix if even that still collides. `usedNames` accumulates across the
// whole batch so collisions are detected regardless of how far apart the
// colliding entries are in the list.
std::string pickBatchOutputName(const std::string& modelPath, size_t indexInBatch,
                                 std::unordered_map<std::string, int>& usedNames) {
    std::string stem = std::filesystem::path(modelPath).stem().string();
    std::string name = stem + ".bundle";
    if (usedNames.find(name) == usedNames.end()) {
        usedNames.emplace(name, 1);
        return name;
    }
    std::string parentName = std::filesystem::path(modelPath).parent_path().filename().string();
    std::string disambiguated = parentName.empty() ? (stem + "_" + std::to_string(indexInBatch) + ".bundle")
                                                     : (parentName + "_" + stem + ".bundle");
    if (usedNames.find(disambiguated) == usedNames.end()) {
        usedNames.emplace(disambiguated, 1);
        return disambiguated;
    }
    disambiguated = stem + "_" + std::to_string(indexInBatch) + ".bundle";
    usedNames.emplace(disambiguated, 1);
    return disambiguated;
}

int exportBundle(int argc, char** args) {
    ExportOptions opts;
    CLI::App app{"husk export: WoW M2 (+ sidecars) -> model bundle",
                 "husk export"};
    addExportOptions(app, opts);

    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;

    bool batchMode = app.count("--from-list") > 0;

    // -i/--input is required in single-file mode, meaningless (and never
    // required) in --from-list mode -- see addExportOptions' comment for
    // why this can't just be CLI11's own ->required() on the option.
    if (!batchMode && opts.modelPath.empty()) {
        std::cerr << "husk: the following arguments are required: --input,input\n";
        return 1;
    }
    if (batchMode && !opts.modelPath.empty()) {
        std::cerr << "husk: --input/positional input and --from-list are mutually exclusive -- "
                     "--from-list supplies every model path itself\n";
        return 1;
    }
    if (batchMode && app.count("--output") > 0) {
        std::cerr << "husk: --output and --from-list are mutually exclusive -- pass --output-dir "
                     "instead, --from-list always names its own per-file outputs\n";
        return 1;
    }
    if (batchMode && app.count("--output-dir") == 0) {
        std::cerr << "husk: --from-list requires --output-dir\n";
        return 1;
    }
    if (!batchMode && app.count("--output-dir") > 0) {
        std::cerr << "husk: --output-dir only does anything alongside --from-list\n";
        return 1;
    }

    // --listfile: optional, unset by default (a real EmptyListfileIndex,
    // same "skip this tier entirely" behavior as before this flag
    // existed). Loaded once up front, not per-batch-entry -- a real
    // community-listfile.csv is millions of lines, and --from-list can
    // drive thousands of exports from one invocation. `listfileIndex` owns
    // whichever backing (an mmap'd cache, or a plain in-memory map on a
    // cache-write failure) loadListfileCached hands back; every
    // exportOneModel call below just borrows a reference to it -- see
    // DESIGN.md's "Listfile index" section for why this is no longer a
    // std::unordered_map passed (and, in a batch, re-copied) by value.
    std::unique_ptr<husk::ListfileIndex> ownedListfileIndex;
    husk::EmptyListfileIndex emptyListfileIndex;
    const husk::ListfileIndex* listfilePtr = &emptyListfileIndex;
    if (app.count("--listfile") && opts.listfileArg != "none") {
        // loadListfileCached: identical result to husk::loadListfile, just
        // backed by a persistent on-disk cache so repeated invocations
        // against the same --listfile within a session skip re-parsing
        // ~148MB of CSV (see listfile_cache.hpp/DESIGN.md).
        ownedListfileIndex = husk::loadListfileCached(opts.listfileArg);
        listfilePtr = ownedListfileIndex.get();
        // A bad --listfile *path* already throws (loadListfile itself, a
        // direct user mistake worth failing loudly on) -- but a listfile
        // that opens fine and parses to zero usable entries (genuinely
        // empty, or every line malformed -- e.g. the wrong file entirely,
        // or corrupted mid-download) previously degraded *silently*: every
        // consumer already treats an empty index the same as "no --listfile
        // given" and falls back to local-only resolution correctly, but
        // that fallback was invisible, which is the wrong failure mode for
        // something the caller explicitly asked for. Recoverable (every
        // downstream feature still has a real local fallback tier), so
        // this warns rather than aborting the export -- never silent.
        if (listfilePtr->empty()) {
            std::cerr << "husk: warning: --listfile '" << opts.listfileArg
                      << "' loaded but contained no usable entries (empty file, or every line "
                         "failed to parse) -- every listfile-dependent feature will fall back to "
                         "local-only resolution for this run, same as if --listfile had never "
                         "been given\n";
        }
    }
    const husk::ListfileIndex& listfile = *listfilePtr;
    std::optional<creaturedisplay::Data> creatures = loadCreatureDisplays(opts, app);
    const creaturedisplay::Data* creaturesPtr = creatures ? &*creatures : nullptr;

    if (!batchMode) {
        return exportOneModel(opts, app, opts.modelPath, opts.outputPath, app.count("--output") > 0, listfile,
                              std::nullopt, creaturesPtr);
    }

    std::vector<std::string> modelPaths;
    try {
        modelPaths = readModelListFile(opts.fromListArg);
    } catch (const std::exception& e) {
        std::cerr << "husk: " << e.what() << "\n";
        return 1;
    }
    if (modelPaths.empty()) {
        std::cerr << "husk: note: --from-list '" << opts.fromListArg << "' has no entries -- nothing to do\n";
        return 0;
    }

    std::error_code ec;
    std::filesystem::create_directories(opts.outputDirArg, ec);
    if (ec) {
        std::cerr << "husk: couldn't create --output-dir '" << opts.outputDirArg << "': " << ec.message() << "\n";
        return 1;
    }

    std::unordered_map<std::string, int> usedNames;
    size_t succeeded = 0;
    size_t failed = 0;
    for (size_t i = 0; i < modelPaths.size(); ++i) {
        const std::string& modelPath = modelPaths[i];
        std::string outName = pickBatchOutputName(modelPath, i, usedNames);
        std::string outputPath = (std::filesystem::path(opts.outputDirArg) / outName).string();

        std::cerr << "husk: [" << (i + 1) << "/" << modelPaths.size() << "] " << modelPath << " -> "
                  << outputPath << "\n";
        // Deliberately not fatal: a single bad file in a multi-thousand-entry
        // worklist shouldn't abort hours of unrelated corpus work -- each
        // entry's own real error is already printed by exportOneModel itself
        // (or, for anything thrown before it gets that far, caught here).
        int rc = 1;
        try {
            rc = exportOneModel(opts, app, modelPath, outputPath, /*outputGiven=*/true, listfile, std::nullopt,
                                creaturesPtr);
        } catch (const std::exception& e) {
            std::cerr << "husk: export failed: " << e.what() << "\n";
        }
        if (rc == 0) {
            ++succeeded;
        } else {
            ++failed;
        }
    }

    std::cerr << "husk: batch export done: " << succeeded << " succeeded, " << failed << " failed (of "
              << modelPaths.size() << ")\n";
    return failed > 0 ? 1 : 0;
}

}  // namespace husk::commands
