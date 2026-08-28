#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <CLI/CLI.hpp>

#include "commands.hpp"
#include "export_extras.hpp"  // readFileBytes
#include "export_materials.hpp"
#include "export_skin_resolution.hpp"
#include "husk_config.hpp"
#include "json_writer.hpp"
#include "listfile.hpp"
#include "m2.hpp"
#include "skin.hpp"
#include "sources/catalog.hpp"

// `husk resolve` -- REFACTOR/CLI_AND_TOOLING.md §3's structured-output
// answer to RESOURCE_CATALOG.md's excavation-escape-hatch table: four
// corpus tasks (`unfillable_texture_task.py`, `texture_dedup_collision_
// task.py`, `texture_type_collisions_task.py`, `m2_full_validation_
// task.py`) are meant to consume husk's own texture resolution instead of
// re-deriving it, and `unfillable_texture_task.py` is the one that already
// broke once from doing exactly that re-derivation (a dropped tier, a real
// 18,742-file re-extraction that silently moved nothing -- see that file's
// own doc comment, and REFACTOR/RESOURCE_CATALOG.md's "excavation escape
// hatch" table).
//
// Deliberately a separate verb, not `export --explain-textures=json`: the
// decisive fact is that two of the four consuming tasks
// (`unfillable_texture_task.py`, `texture_dedup_collision_task.py`)
// currently shell out to the cheap, header-only `husk info` per file, on
// purpose, at 132k-file corpus scale -- `unfillable_texture_task.py`'s own
// doc comment records an earlier version that *did* shell out to a real
// `husk export` per file instead and had to be reverted, because a real
// mesh/skin build + image embed + .glb write turned a ~10-minute scan into
// a multi-hour one for no accuracy those tasks need. Extending
// `--explain-textures` would force exactly that cost back onto them to get
// a machine-readable answer. `husk resolve` instead runs only as much of
// `export`'s own pipeline as answers "where did every batch's texture
// resolve": M2 header + material/texture arrays, each exported .skin's
// submeshes/batches, and `sources::Catalog::texture()` -- via the *same*
// `buildMaterialsAndPrimitives`/`resolveSkinsToExport` functions `export`
// itself calls (I2: one implementation), never a second, simplified mirror
// of which slots are "used." What it skips, on purpose: `m2::parseVertices`
// (materials/textures never need vertex data), the skeleton/bone/animation/
// DB2-character pipeline, mesh-accessor/glTF assembly, and the final .glb
// write -- none of those affect which tier answers a texture slot, and
// they're the actual expense the tasks above are avoiding.
//
// Output is JSON only (stdout), one array entry per (skin, texture slot)
// resolved -- same shape as `dump-chunks`, no prose twin to keep in sync,
// since this command's only reason to exist is to be machine-parsed.
namespace husk::commands {

void addResolveOptions(CLI::App& app, ResolveOptions& opts) {
    // Same config wiring as `export`'s addExportOptions, and it is not
    // optional here: `resolve` exists to report the *same* answer `export`
    // reaches, so any input `export` picks up and `resolve` doesn't makes the
    // two disagree by construction -- the exact drift this ledger is meant to
    // expose. Without it a configured `listfile`/`listfile-root` reached
    // `export` and not `resolve`, and four real slots on a real character
    // model reported `fuzzy-same-basename-pool` here while `export` resolved
    // them via `listfile` (see REFACTOR_LOG.md's verification entry).
    app.set_config("--config", husk::defaultConfigPath(),
                    "TOML file of default flag values (see README.md's config-file "
                    "section) -- explicit CLI flags always override a config value")
        ->envname("HUSK_CONFIG")
        ->group("Diagnostics");

    app.add_option("-i,--input,input", opts.model, "the .m2 file to resolve texture slots for");
    app.add_option("-s,--skin", opts.skinArg,
                    "a .skin path, or 'auto' to resolve via the model's own SFID chunk, falling "
                    "back to a same-basename numbered scan next to the model if that doesn't "
                    "resolve -- same grammar as `export`'s own --skin")
        ->capture_default_str()
        ->group("Model sidecars");
    app.add_option("--skin-dir", opts.skinDirArg,
                    "directory 'auto' searches for the SFID-declared '<FileDataID>.skin' file, or "
                    "'none' to skip that stage (default: the model's own directory)")
        ->group("Model sidecars");
    app.add_option("--lod", opts.lodArg,
                    "'<n>' or 'all' -- only meaningful when --skin resolves via 'auto' (default: "
                    "entry 0)")
        ->group("Model sidecars");
    app.add_option("-t,--textures", opts.texturesArg,
                    "directory of already-converted '<FileDataID>.png' files, raw '<FileDataID>.blp' "
                    "files, or 'none' to skip literal/fuzzy resolution entirely (default: the "
                    "model's own directory) -- same grammar as `export`'s own --textures")
        ->group("Game data");
    app.add_option("--textures-out", opts.texturesOutArg,
                    "directory to also write each resolved '.blp''s decoded '.png' to (default: "
                    "unset -- nothing written to disk, same as `export`'s own --textures-out)")
        ->group("Game data");
    app.add_option("--listfile", opts.listfileArg,
                    "a local community-listfile.csv-style snapshot -- same tier-2 fallback "
                    "`export`'s own --listfile drives (see that flag's help text)")
        ->group("Game data");
    app.add_option("--listfile-root", opts.listfileRootArg,
                    "the corpus root --listfile's paths are relative to (default: --textures "
                    "itself) -- same meaning as `export`'s own --listfile-root")
        ->group("Game data");
    app.add_option("--object-skin-texture-id", opts.objectSkinTextureIdArg,
                    "a real texture FileDataID to fill into any type=2 (object_skin) texture slot "
                    "that has no FileDataID of its own -- same meaning as `export`'s own "
                    "--object-skin-texture-id. --knowledge-db's own KB-driven auto-derivation of "
                    "this value is deliberately not wired into `resolve` -- out of this command's "
                    "scope, not a silent gap: pass the resolved FileDataID directly")
        ->group("Game data");
}

namespace {

void writeLedgerEntryJson(json::Writer& w, const husk::sources::Catalog::LedgerEntry& e) {
    w.beginObject();
    w.key("model_path");
    w.value(e.modelPath);
    w.key("texture_slot_index");
    w.value(static_cast<int64_t>(e.textureSlotIndex));
    w.key("file_data_id");
    w.value(static_cast<int64_t>(e.fileDataId));
    w.key("texture_type");
    w.value(static_cast<int64_t>(e.textureType));
    w.key("found");
    w.value(e.found);
    w.key("tier");
    w.value(std::string(husk::sources::tierName(e.tier)));
    w.key("resolved_name");
    w.value(e.resolvedName);
    w.key("byte_count");
    w.value(static_cast<int64_t>(e.byteCount));
    w.key("alternate_count");
    w.value(static_cast<int64_t>(e.alternateCount));
    // `reason` doubles as the miss reason on a miss (FOREIGN_DATA.md §2's
    // expected-vs-actual) and free-text provenance detail on a hit (which
    // directory/fallback/pool-size fired) -- one field either way, same
    // convention `Resolved<T>::reason` itself already uses; a consumer
    // only needs to branch on `found` to know which reading applies.
    w.key("reason");
    w.value(e.reason);
    w.endObject();
}

// Redirects std::cout into an internal buffer for its own scope, restoring
// the real buffer on destruction -- including via an exception unwinding
// through it, which a bare rdbuf()-swap-and-restore pair wouldn't survive.
// See its own call site's doc comment for why this exists at all:
// buildMaterialsAndPrimitives prints one real diagnostic straight to
// std::cout, which would otherwise land ahead of `resolve`'s own JSON on
// the one stream a machine consumer reads.
class CoutToStderrGuard {
public:
    CoutToStderrGuard() : realBuf_(std::cout.rdbuf(captured_.rdbuf())) {}
    ~CoutToStderrGuard() {
        std::cout.rdbuf(realBuf_);
        if (!captured_.str().empty()) std::cerr << captured_.str();
    }
    CoutToStderrGuard(const CoutToStderrGuard&) = delete;
    CoutToStderrGuard& operator=(const CoutToStderrGuard&) = delete;

private:
    std::ostringstream captured_;
    std::streambuf* realBuf_;
};

}  // namespace

int resolve(int argc, char** args) {
    CLI::App app{"husk resolve -- print sources::Catalog's texture-resolution ledger as JSON",
                 "husk resolve"};
    ResolveOptions opts;
    addResolveOptions(app, opts);
    try {
        // App::parse(vector<string>&) processes tokens back-to-front (see
        // cmd_export.cpp's identical comment on its own call) -- reverse
        // `args` into that expected order, or every flag's value binds to
        // the wrong neighbor. Deliberately NOT app.parse(argc, args): that
        // overload treats argv[0] as the program name and skips it, but
        // `args` here already excludes both the program name and the
        // subcommand word (commands.hpp's own doc comment on every
        // int(int argc, char** args) command entry point) -- calling it
        // silently ate the model path as a fake "program name" (found via
        // a real CLI-tier test failure, not assumed).
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    if (opts.model.empty()) {
        std::cerr << "husk: resolve: the .m2 file is required\n";
        return 1;
    }

    bool skinDirGiven = app.count("--skin-dir") > 0;
    bool lodGiven = !opts.lodArg.empty();
    if (opts.skinArg != "auto") {
        if (skinDirGiven) {
            std::cerr << "husk: --skin-dir only does anything when --skin is 'auto'\n";
            return 1;
        }
        if (lodGiven) {
            std::cerr << "husk: --lod only does anything when --skin is 'auto'\n";
            return 1;
        }
    }
    if (lodGiven && skinDirGiven && opts.skinDirArg == "none") {
        std::cerr << "husk: --lod needs the SFID-based resolution --skin-dir 'none' disables\n";
        return 1;
    }

    std::filesystem::path modelDir = std::filesystem::path(opts.model).parent_path();
    std::string modelDirStr = modelDir.empty() ? "." : modelDir.string();

    // Same three-state --textures/--skin-dir/--listfile-root resolution as
    // `export` (cmd_export.cpp's exportOneModel) -- deliberately
    // reimplemented here rather than shared, since it's flag-parsing glue
    // (a handful of `app.count()`/`== "none"` checks), not a resolution
    // *policy* I2 would otherwise be duplicating; the policy itself
    // (`sources::Catalog`, `resolveSkinsToExport`) is shared, below.
    std::string texturesDir = app.count("--textures") ? opts.texturesArg : modelDirStr;
    if (texturesDir == "none") texturesDir.clear();
    std::string listfileRoot = app.count("--listfile-root") ? opts.listfileRootArg : texturesDir;
    if (listfileRoot == "none") listfileRoot.clear();
    std::string texturesOutDir = app.count("--textures-out") ? opts.texturesOutArg : "";
    bool skinDirNone = skinDirGiven && opts.skinDirArg == "none";
    std::string skinDir = skinDirNone ? "" : (skinDirGiven ? opts.skinDirArg : modelDirStr);

    std::unordered_map<uint32_t, std::string> listfile;
    if (app.count("--listfile") && opts.listfileArg != "none") {
        try {
            listfile = husk::loadListfile(opts.listfileArg);
        } catch (const std::exception& e) {
            std::cerr << "husk: resolve failed: " << e.what() << "\n";
            return 1;
        }
        if (listfile.empty()) {
            std::cerr << "husk: warning: --listfile '" << opts.listfileArg
                      << "' loaded but produced zero usable entries -- falling back to "
                         "local-only resolution\n";
        }
    }

    uint32_t objectSkinTextureFileDataId = 0;
    if (app.count("--object-skin-texture-id")) {
        try {
            objectSkinTextureFileDataId = static_cast<uint32_t>(std::stoul(opts.objectSkinTextureIdArg));
        } catch (const std::exception&) {
            std::cerr << "husk: note: --object-skin-texture-id '" << opts.objectSkinTextureIdArg
                      << "' isn't a valid integer -- ignoring\n";
        }
    }

    try {
        auto modelBytes = readFileBytes(opts.model);
        auto header = m2::parseHeader(modelBytes);
        auto blob = m2::extractBlob(modelBytes);

        M2MaterialInputs m2Inputs;
        m2Inputs.materials = m2::parseMaterials(blob, header.materials);
        m2Inputs.textures = m2::parseTextures(blob, header.textures);
        m2Inputs.textureCombos = m2::parseUint16Array(blob, header.textureCombos);
        m2Inputs.textureCoordCombos = m2::parseUint16Array(blob, header.textureCoordCombos);
        m2Inputs.colors = m2::parseColors(blob, header.colors);
        m2Inputs.textureWeights = m2::parseTextureWeights(blob, header.textureWeights);
        m2Inputs.textureWeightCombos = m2::parseUint16Array(blob, header.textureWeightCombos);
        m2Inputs.textureTransforms = m2::parseTextureTransforms(blob, header.textureTransforms);
        m2Inputs.textureTransformCombos = m2::parseUint16Array(blob, header.textureTransformCombos);
        m2Inputs.textureFileDataIds = header.textureFileDataIds;
        m2Inputs.blob = &blob;
        m2Inputs.sequenceCount = header.sequences.count;

        auto skinsToExport = resolveSkinsToExport(header, opts.model, skinDir, skinDirNone, lodGiven,
                                                    opts.lodArg, opts.skinArg);

        husk::sources::Catalog catalog(texturesDir, listfile, listfileRoot, texturesOutDir);

        for (const auto& [lodName, path] : skinsToExport) {
            (void)lodName;  // LOD label, not needed here -- the ledger keys by modelPath+slot only
            auto skinBytes = readFileBytes(path);
            auto skinHeader = skin::parseHeader(skinBytes);
            auto triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);
            auto submeshes = skin::parseSubmeshes(skinBytes, skinHeader.submeshes);
            auto batches = skin::parseBatches(skinBytes, skinHeader.batches);

            // Runs the real per-batch resolution (same function `export`
            // calls, buildLodTierMeshes) purely for its catalog.texture()
            // side effects on the shared ledger -- the returned
            // BuiltMaterials (gltf::Material/Primitive objects, never
            // otherwise emitted here) is intentionally discarded.
            //
            // buildMaterialsAndPrimitives itself prints one real diagnostic
            // straight to std::cout on a non-empty leftover fuzzy pool (see
            // its own trailing "N texture file(s) ... share this model's
            // basename" note, export_materials.cpp) -- fine for `export`,
            // whose stdout has no other contract, but fatal here: `resolve`'s
            // entire reason to exist is a clean, single-JSON-document
            // stdout stream a real parser can trust (found live, not
            // assumed -- a real 218-candidate corpus run prepended this
            // exact note before the JSON and broke `jq .` outright). Rather
            // than touching the shared function's own behavior (which
            // `export` still depends on unchanged), CoutToStderrGuard
            // redirects std::cout for the duration of this one call and
            // re-emits whatever it captured on stderr instead -- the
            // information isn't lost, it just never reaches the stream a
            // machine consumer reads.
            {
                CoutToStderrGuard coutGuard;
                (void)buildMaterialsAndPrimitives(triangleIndices, submeshes, batches, m2Inputs, catalog,
                                                   texturesDir, opts.model, texturesOutDir, listfile,
                                                   listfileRoot.empty() ? texturesDir : listfileRoot,
                                                   objectSkinTextureFileDataId);
            }
        }

        json::Writer w(std::cout);
        w.beginObject();
        w.key("model_path");
        w.value(opts.model);
        w.key("textures_dir");
        w.value(texturesDir);
        w.key("listfile_root");
        w.value(listfileRoot);
        w.key("listfile_row_count");
        w.value(static_cast<int64_t>(listfile.size()));
        w.key("slots");
        w.beginArray();
        for (const auto& entry : catalog.ledger()) {
            writeLedgerEntryJson(w, entry);
        }
        w.endArray();
        w.endObject();
        std::cout << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: resolve failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
