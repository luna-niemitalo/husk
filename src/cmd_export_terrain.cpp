#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "bundle_schema.hpp"
#include "cli_args.hpp"
#include "cmd_world_shared.hpp"
#include "commands.hpp"
#include "liquid_canon_input.hpp"
#include "listfile_cache.hpp"
#include "sources/listfile_catalog.hpp"
#include "writers/bundle_writer.hpp"
#include "writers/terrain_bundle_writer.hpp"

// `export-terrain`, `export-wmo`, `export-map` and `validate-bundle`; what
// they share with `export-world` is cmd_world_shared.hpp.
namespace husk::commands {

using namespace world;

void addExportTerrainOptions(CLI::App& app, ExportTerrainOptions& opts) {
    addConfigOption(app);
    app.add_option("input", opts.input, "root .adt tile (<map>_<x>_<y>.adt)")->required();
    app.add_option("output", opts.output, "bundle directory to write")->required();
    app.add_option("--obj", opts.objArg,
                   "placements file: 'auto' (the tile's sibling <stem>_obj0.adt), 'none', or a path")
        ->default_val("auto")
        ->group("Tile sidecars");
    app.add_option("--tex", opts.texArg,
                   "texture-layer file: 'auto' (the tile's sibling <stem>_tex0.adt), 'none', or a path")
        ->default_val("auto")
        ->group("Tile sidecars");
    app.add_option("--wdt", opts.wdtArg,
                   "map WDT, whose MPHD flags pick the alpha-map format: 'auto' (<map>.wdt beside the tile) "
                   "or a path. No 'none': without a WDT, texture layers can't be read -- use --tex none")
        ->default_val("auto")
        ->group("Tile sidecars");
    app.add_option("--listfile", opts.listfile,
                   "community listfile CSV, or 'none': finds texture files by FileDataID, resolves "
                   "name-table (MMDX/MWMO/MTEX) paths to FileDataIDs, and names placed models")
        ->group("Resolution");
    app.add_option("--listfile-root", opts.listfileRoot,
                   "directory the listfile's paths are relative to, or 'none'")
        ->group("Resolution");
    app.add_option("--db2-dir", opts.db2Dir,
                   "DB2 directory for GroundEffectTexture/GroundEffectDoodad (ground cover), or 'none'")
        ->group("Resolution");
    app.add_option("--dbd-dir", opts.dbdDir, "WoWDBDefs definitions directory, or 'none'")->group("Resolution");
    app.add_option("--models-dir", opts.modelsDir,
                   "set placement/ground-effect uris to <dir>/<fdid>.canon.bundle where that bundle exists")
        ->group("Output");
    app.add_flag("--list-models", opts.listModels,
                 "print '<fdid>\\t<listfile path>' for every model the tile references, then exit")
        ->group("Output");
}

void addExportMapOptions(CLI::App& app, ExportMapOptions& opts) {
    addConfigOption(app);
    app.add_option("input", opts.input, "map directory: its root <map>_<x>_<y>.adt tiles, <map>_fogs.wdt, <map>_mpv.wdt")
        ->required();
    app.add_option("output", opts.output, "bundle directory to write")->required();
    app.add_option("--listfile", opts.listfile,
                   "community listfile CSV, or 'none': tile and map FileDataIDs, particle textures, fog models")
        ->group("Resolution");
    app.add_option("--listfile-root", opts.listfileRoot, "directory the listfile's paths are relative to, or 'none'")
        ->group("Resolution");
    app.add_option("--db2-dir", opts.db2Dir, "DB2 directory for the zone lights (Light, LightData, Map), or 'none'")
        ->group("Resolution");
    app.add_option("--dbd-dir", opts.dbdDir, "WoWDBDefs definitions directory, or 'none'")->group("Resolution");
}

void addExportWmoOptions(CLI::App& app, ExportWmoOptions& opts) {
    addConfigOption(app);
    app.add_option("input", opts.input, "root .wmo file (not a _NNN group file)")->required();
    app.add_option("output", opts.output, "bundle directory to write")->required();
    app.add_option("--listfile", opts.listfile,
                   "community listfile CSV, or 'none': finds group files and textures by FileDataID, "
                   "names doodad models")
        ->group("Resolution");
    app.add_option("--listfile-root", opts.listfileRoot, "directory the listfile's paths are relative to, or 'none'")
        ->group("Resolution");
    app.add_option("--db2-dir", opts.db2Dir, "DB2 directory for the liquid tables (liquid type names, textures), or 'none'")
        ->group("Resolution");
    app.add_option("--dbd-dir", opts.dbdDir, "WoWDBDefs definitions directory, or 'none'")->group("Resolution");
    app.add_option("--models-dir", opts.modelsDir,
                   "set doodad uris to <dir>/<fdid>.canon.bundle where that bundle exists")
        ->group("Output");
}

int exportTerrain(int argc, char** args) {
    CLI::App app{"husk export-terrain -- one ADT tile to a canonical terrain bundle", "husk export-terrain"};
    ExportTerrainOptions opts;
    addExportTerrainOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;

    try {
        std::filesystem::path rootPath(opts.input);
        TileName tile = parseTileName(rootPath);
        adt::RootFile root = adt::parseRoot(adt::readFile(rootPath));
        auto announce = [](const std::string& note) { std::cerr << "husk: note: " << note << "\n"; };
        auto warn = [](const std::string& warning) { std::cerr << "husk: warning: " << warning << "\n"; };

        Sidecar<adt::ObjFile> objSidecar = loadSidecar(rootPath, opts.objArg, "_obj0.adt", "--obj", &adt::parseObj);
        Sidecar<adt::TexFile> texSidecar = loadSidecar(rootPath, opts.texArg, "_tex0.adt", "--tex", &adt::parseTex);
        announce(objSidecar.note);
        announce(texSidecar.note);
        if (!objSidecar.warning.empty()) warn(objSidecar.warning);
        if (!texSidecar.warning.empty()) warn(texSidecar.warning);
        std::optional<adt::ObjFile>& obj = objSidecar.value;
        std::optional<adt::TexFile>& tex = texSidecar.value;

        // The WDT is only needed to read texture layers. An auto-located one
        // that's missing drops the layers with a warning; an explicit --wdt
        // that doesn't parse fails, like any other explicit path.
        std::optional<uint32_t> wdtFlags;
        if (tex) {
            bool explicitWdt = opts.wdtArg != "auto";
            std::filesystem::path wdtPath =
                explicitWdt ? std::filesystem::path(opts.wdtArg) : rootPath.parent_path() / (tile.map + ".wdt");
            if (explicitWdt) {
                wdtFlags = adt::parseWdtFlags(adt::readFile(wdtPath));
                announce("--wdt: " + wdtPath.string() + " (explicit)");
            } else {
                std::string problem = "not found";
                if (std::filesystem::exists(wdtPath)) {
                    try {
                        wdtFlags = adt::parseWdtFlags(adt::readFile(wdtPath));
                    } catch (const std::exception& e) {
                        problem = std::string("unreadable: ") + e.what();
                    }
                }
                if (wdtFlags) {
                    announce("--wdt: " + wdtPath.string() + " (beside the tile)");
                } else {
                    warn("expected the map WDT at " + wdtPath.string() +
                         " (its MPHD flags pick the alpha-map format), " + problem +
                         " -- exporting without texture layers (pass --wdt <path>, or --tex none to silence this)");
                    tex.reset();
                }
            }
        }

        std::unique_ptr<ListfileIndex> listfile;
        std::string listfilePath = unlessNone(opts.listfile);
        std::string listfileRoot = unlessNone(opts.listfileRoot);
        if (!listfilePath.empty()) {
            listfile = loadListfileCached(listfilePath);
            announce("listfile: " + std::to_string(listfile->size()) + " rows from " + listfilePath);
        } else {
            announce("listfile: none (textures stay unresolved, name-table paths stay names)");
        }

        adtinput::GamePathResolutions gamePaths;
        if (listfile) gamePaths = sources::fileDataIdsForGamePaths(*listfile, adtinput::gamePaths(obj, tex));
        adtinput::TextureResolutions textures;
        adtinput::GroundEffectDefinitions groundEffects;
        if (tex) {
            if (listfile) {
                for (uint32_t fdid : adtinput::textureFileDataIds(*tex, gamePaths)) {
                    textures.emplace(fdid, resolveTexture(fdid, *listfile, listfileRoot));
                }
            }
            std::string note;
            groundEffects = loadGroundEffects(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), note);
            announce(note);
        }
        std::optional<liquiddb::Data> liquids;
        if (root.liquid) {
            std::string note;
            liquids = loadLiquids(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), note);
            announce(note);
            if (liquids && listfile) {
                for (uint32_t fdid : liquidinput::textureFileDataIds(adtinput::liquidTypeRows(root), *liquids)) {
                    textures.emplace(fdid, resolveTexture(fdid, *listfile, listfileRoot));
                }
            }
        }

        adtinput::TileSources sources{root, obj, tex, wdtFlags, tile.map, tile.x, tile.y};
        canon::Terrain terrain = adtinput::buildCanonTerrain(
            sources, {textures, groundEffects, gamePaths, liquids ? &*liquids : nullptr});
        if (listfile) nameModelRefs(terrain, *listfile);

        if (opts.listModels) {
            for (uint32_t fdid : referencedModels(terrain)) {
                std::optional<std::string_view> path = listfile ? listfile->lookup(fdid) : std::nullopt;
                std::cout << fdid << "\t" << (path ? std::string(*path) : "") << "\n";
            }
            return 0;
        }

        writers::AssetUris modelUris;
        if (!opts.modelsDir.empty()) {
            std::filesystem::path bundleDir = std::filesystem::absolute(opts.output);
            for (uint32_t fdid : referencedAssets(terrain)) {
                std::filesystem::path manifest =
                    std::filesystem::absolute(opts.modelsDir) / modelBundleName(fdid) / "manifest.json";
                if (!std::filesystem::exists(manifest)) continue;
                modelUris.emplace(fdid, std::filesystem::relative(manifest, bundleDir).generic_string());
            }
        }
        std::unordered_map<std::string, uint32_t> rootIds;
        if (listfile && !listfileRoot.empty()) rootIds = tileFileDataIds(*listfile, listfileRoot, {rootPath});
        terrain.subject = writers::subjectRef(rootPath, tileFileDataId(rootIds, listfileRoot, rootPath));
        writers::writeTerrainBundle(terrain, opts.output,
                                    terrainProvenance(obj.has_value(), tex.has_value(), wdtFlags.has_value(),
                                                      listfile.get(), listfilePath, !groundEffects.empty(),
                                                      liquids.has_value()),
                                    modelUris);
        std::cout << "wrote " << opts.output << ": " << terrain.chunks.size() << " chunks, " << terrain.textures.size()
                  << " textures, " << terrain.groundEffects.size() << " ground effects, " << terrain.liquids.size()
                  << " liquid surfaces, " << terrain.doodads.instances.size() << " doodads, "
                  << terrain.mapObjects.instances.size() << " map objects, " << modelUris.size()
                  << " linked models\n";
    } catch (const std::exception& e) {
        std::cerr << "husk: export-terrain: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int exportWmo(int argc, char** args) {
    CLI::App app{"husk export-wmo -- one WMO building to a canonical object bundle", "husk export-wmo"};
    ExportWmoOptions opts;
    addExportWmoOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;

    try {
        auto announce = [](const std::string& note) { std::cerr << "husk: note: " << note << "\n"; };
        auto warn = [](const std::string& warning) { std::cerr << "husk: warning: " << warning << "\n"; };
        std::unique_ptr<ListfileIndex> listfile;
        std::string listfilePath = unlessNone(opts.listfile);
        std::string listfileRoot = unlessNone(opts.listfileRoot);
        if (!listfilePath.empty()) {
            listfile = loadListfileCached(listfilePath);
            announce("listfile: " + std::to_string(listfile->size()) + " rows from " + listfilePath);
        } else {
            announce("listfile: none (group files found by name only, textures stay unresolved)");
        }

        std::optional<uint32_t> fileDataId =
            listfile ? sources::fileDataIdForLocalFile(*listfile, opts.input, listfileRoot)
                     : sources::fileDataIdForStorePath(opts.input);
        std::string liquidNote;
        std::optional<liquiddb::Data> liquids = loadLiquids(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), liquidNote);
        announce(liquidNote);
        canon::Model model =
            loadWmoModel(opts.input, fileDataId, listfile.get(), listfileRoot, liquids ? &*liquids : nullptr, warn);

        writers::AssetUris modelUris;
        if (!opts.modelsDir.empty()) {
            std::filesystem::path bundleDir = std::filesystem::absolute(opts.output);
            for (uint32_t fdid : placedAssets(model)) {
                std::filesystem::path manifest =
                    std::filesystem::absolute(opts.modelsDir) / modelBundleName(fdid) / "manifest.json";
                if (std::filesystem::exists(manifest)) {
                    modelUris.emplace(fdid, std::filesystem::relative(manifest, bundleDir).generic_string());
                }
            }
        }
        writers::writeBundle(model, opts.output, wmoProvenance(listfile.get(), listfilePath, liquids.has_value()), modelUris);
        size_t instances = 0;
        for (const auto& owned : model.placementSets) instances += owned.set.instances.size();
        std::cout << "wrote " << opts.output << ": " << model.mesh.parts.size() << " parts, "
                  << model.mesh.primitives.size() << " primitives, " << model.materials.size() << " materials, "
                  << model.placementSets.size() << " placement sets (" << instances << " instances), "
                  << modelUris.size() << " linked models\n";
    } catch (const std::exception& e) {
        std::cerr << "husk: export-wmo: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

void addValidateBundleOptions(CLI::App& app, ValidateBundleOptions& opts) {
    app.add_option("paths", opts.paths, "bundle directories or document files to check");
    app.add_flag("--lint-schema", opts.lintSchema, "also check the embedded schema/ for format antipatterns");
    app.add_flag("--release", opts.release, "fail on warnings too: a release build places no placeholder effect");
}

int validateBundle(int argc, char** args) {
    CLI::App app{"husk validate-bundle -- check documents against the bundle format's schema", "husk validate-bundle"};
    ValidateBundleOptions opts;
    addValidateBundleOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;
    if (opts.paths.empty() && !opts.lintSchema) {
        std::cerr << "husk: validate-bundle: expected a bundle path or --lint-schema\n";
        return 2;
    }

    std::vector<bundle_schema::Problem> problems;
    std::vector<bundle_schema::Problem> warnings;
    if (opts.lintSchema) problems = bundle_schema::lintSchemas();
    size_t documents = 0;
    for (const std::string& path : opts.paths) {
        std::vector<std::filesystem::path> checked;
        for (bundle_schema::Problem& p : bundle_schema::validateDocument(path, &checked, &warnings)) problems.push_back(std::move(p));
        documents += checked.size();
    }
    for (const bundle_schema::Problem& p : problems) std::cout << p.where << ": " << p.message << "\n";
    for (const bundle_schema::Problem& p : warnings) std::cout << p.where << ": warning: " << p.message << "\n";
    std::cout << documents << " documents checked, " << problems.size() << " problems, " << warnings.size()
              << " warnings" << (opts.release && !warnings.empty() ? " (--release: warnings fail)" : "") << "\n";
    return problems.empty() && (!opts.release || warnings.empty()) ? 0 : 1;
}

int exportMap(int argc, char** args) {
    CLI::App app{"husk export-map -- one map's world document: tile grid and map-wide volumes", "husk export-map"};
    ExportMapOptions opts;
    addExportMapOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;

    try {
        std::unique_ptr<ListfileIndex> listfile;
        std::string listfilePath = unlessNone(opts.listfile);
        if (!listfilePath.empty()) {
            listfile = loadListfileCached(listfilePath);
            std::cerr << "husk: note: listfile: " << listfile->size() << " rows from " << listfilePath << "\n";
        } else {
            std::cerr << "husk: note: listfile: none (tiles and map unidentified, particle textures unresolved)\n";
        }
        std::filesystem::path mapDir = std::filesystem::path(opts.input).lexically_normal();
        if (mapDir.filename().empty()) mapDir = mapDir.parent_path();
        std::string map = mapDir.filename().string();
        std::vector<WorldTile> tiles = findTiles(mapDir.parent_path(), {map});
        adt::WdtFile wdt = adt::parseWdt(adt::readFile(mapDir / (map + ".wdt")));
        size_t unlisted = std::erase_if(tiles, [&](const WorldTile& t) {
            TileName name = parseTileName(t.root);
            return !wdt.hasTile(name.x, name.y);
        });
        if (unlisted > 0) std::cerr << "husk: note: " << unlisted << " root tiles the WDT's MAIN doesn't list, skipped\n";
        std::string lightNote;
        std::optional<lightdb::Data> lights = loadLights(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), lightNote);
        std::cerr << "husk: note: " << lightNote << "\n";
        canon::World world = loadWorld(mapDir, wdt, tiles, listfile.get(), unlessNone(opts.listfileRoot),
                                       lights ? &*lights : nullptr, std::max(1u, std::thread::hardware_concurrency()));
        if (world.cells.empty() && !placesModels(world.placements)) {
            throw std::runtime_error("expected root tiles " + map + "_<x>_<y>.adt in " + mapDir.string() +
                                     " or a map-wide WMO in " + map + ".wdt, found neither");
        }
        writers::writeWorldBundle(world, opts.output, worldProvenance(mapDir, listfile.get(), listfilePath, lights.has_value()),
                                  std::vector<std::string>(world.cells.size()));
        std::cout << "wrote " << opts.output << ": " << world.cells.size() << " tiles, " << world.placements.size()
                  << " off-grid placement sets\n";
        printPlaceholderEffects(world.placements, "export-map");
    } catch (const std::exception& e) {
        std::cerr << "husk: export-map: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

// Four passes over the world, each parallel, each resumable (an output
// whose manifest.json exists is skipped -- every writer writes it last):
//   1. read every tile's _obj0/_tex0 once for what it references: model
//      FileDataIDs and name-table paths (resolved against the listfile in
//      one pass for the whole world), plus every GroundEffectDoodad model,
//   2. export each model once to models/<fdid>.canon.bundle via
//      exportOneModel --bundle-only (one shared listfile, no per-model DB2
//      loads; creature options from tables loaded once),
//   3. export each tile to maps/<map>/<tile>.bundle, terrain textures shared
//      under textures/, uris only to outputs that exist.
//   4. write each map's world document to maps/<map>/<map>.world.bundle:
//      its tile grid, linking the tile bundles that exist, its fog and
//      particulate volumes, and for a map without terrain the one WMO its
//      WDT places (exported with the others in pass 2).
// Failures are logged to errors.log and never stop the run.

}  // namespace husk::commands
