#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "cli_args.hpp"
#include "cmd_world_shared.hpp"
#include "commands.hpp"
#include "input_log.hpp"
#include "liquid_canon_input.hpp"
#include "listfile_cache.hpp"
#include "parallel.hpp"
#include "progress.hpp"
#include "sources/listfile_catalog.hpp"
#include "wmo_canon_input.hpp"
#include "writers/bundle_writer.hpp"
#include "writers/terrain_bundle_writer.hpp"

// `export-world`: every tile of every map, the models and WMOs they place,
// and each map's world document, in parallel passes (README.md).
namespace husk::commands {

using namespace world;

void addExportWorldOptions(CLI::App& app, ExportWorldOptions& opts) {
    addConfigOption(app);
    app.add_option("maps-root", opts.mapsRoot, "directory holding one subdirectory per map (world/maps)")->required();
    app.add_option("output", opts.output, "scene directory to write (maps/, models/, textures/, errors.log)")
        ->required();
    app.add_option("--map", opts.maps, "only these map directories (repeatable); default every map")
        ->group("Selection");
    app.add_option("--listfile", opts.listfile,
                   "community listfile CSV: finds texture and model files by FileDataID, resolves "
                   "name-table (MMDX/MWMO/MTEX) paths to FileDataIDs")
        ->required()
        ->group("Resolution");
    app.add_option("--listfile-root", opts.listfileRoot, "directory the listfile's paths are relative to")
        ->required()
        ->group("Resolution");
    app.add_option("--db2-dir", opts.db2Dir,
                   "DB2 directory for GroundEffectTexture/GroundEffectDoodad (ground cover), or 'none'")
        ->group("Resolution");
    app.add_option("--dbd-dir", opts.dbdDir, "WoWDBDefs definitions directory, or 'none'")->group("Resolution");
    app.add_option("--jobs", opts.jobs, "worker threads (default: hardware threads)")->group("Run");
    app.add_flag("--skip-models", opts.skipModels,
                 "don't export model bundles (pass 2); placements link only to bundles already under "
                 "<output>/models/")
        ->group("Run");
    app.add_flag("--all-assets", opts.allAssets,
                 "also export every M2 and root WMO the listfile names, placed or not (export-corpus)")
        ->group("Selection");
    app.add_option("--sample", opts.sample,
                   "with --all-assets: only this many of them, a fixed seeded random choice (size and time "
                   "projections)")
        ->group("Selection");
}

namespace {

// A WMO's groups are <root>_NNN.wmo, their LODs <root>_NNN_lodN.wmo.
bool isRootWmoPath(std::string_view path) {
    static const std::regex group(R"(_\d{3}(_lod\d)?\.wmo$)");
    return path.ends_with(".wmo") && !std::regex_search(path.begin(), path.end(), group);
}

struct SourceGone : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Thread-safe append-only failure log: one "phase<TAB>item<TAB>message" line
// per failure, so a rerun (which skips finished outputs) can be checked
// against it.
class ErrorLog {
public:
    ErrorLog(const std::filesystem::path& path, std::filesystem::path sourceRoot)
        : sourceRoot_(std::move(sourceRoot)), out_(path, std::ios::app) {
        if (!out_) throw std::runtime_error("cannot open " + path.string());
    }
    // A failure is the item's only while the source tree is still there: a
    // vanished one (an unmounted store) would otherwise log every remaining
    // file as missing and finish a useless run, so it stops the run.
    void add(const std::string& phase, const std::string& item, const std::string& message) {
        std::error_code ec;
        if (!std::filesystem::is_directory(sourceRoot_, ec)) {
            throw SourceGone("expected the source tree " + sourceRoot_.string() +
                             ", got none (unmounted?): stopping at " + phase + " " + item);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        out_ << phase << '\t' << item << '\t' << message << '\n';
        out_.flush();
        ++count_;
    }
    size_t count() const { return count_; }

private:
    std::filesystem::path sourceRoot_;
    std::mutex mutex_;
    std::ofstream out_;
    std::atomic<size_t> count_{0};
};

std::string exceptionMessage(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (const std::exception& ex) {
        return ex.what();
    } catch (...) {
        return "unknown exception";
    }
}

// Shared terrain textures: each FileDataID's payload is written once to
// <out>/textures/, and tiles reference it by uri.
class SharedTextures {
public:
    SharedTextures(writers::TextureStore& store, const ListfileIndex& listfile, std::filesystem::path listfileRoot)
        : store_(store), listfile_(listfile), listfileRoot_(std::move(listfileRoot)) {}

    // The texture without payload plus its filename under textures/ (empty
    // when unresolved). Two threads may resolve the same new FDID at once;
    // both write identical bytes, so that race is harmless.
    std::pair<canon::TextureRef, std::string> get(uint32_t fdid) {
        // Every caller used the texture, also when an earlier one read it.
        if (auto path = sources::localFileForFileDataId(listfile_, listfileRoot_, fdid)) inputs::use(*path);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = done_.find(fdid);
            if (it != done_.end()) return it->second;
        }
        canon::TextureRef ref = resolveTexture(fdid, listfile_, listfileRoot_);
        std::string filename;
        const canon::TextureRef::Payload* chosen = ref.rawPayload ? &*ref.rawPayload : ref.payload ? &*ref.payload : nullptr;
        if (chosen) {
            filename = store_.put(fdid, *chosen);
            ref.payload.reset();
            ref.rawPayload.reset();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return done_.emplace(fdid, std::make_pair(std::move(ref), filename)).first->second;
    }

private:
    writers::TextureStore& store_;
    const ListfileIndex& listfile_;
    std::filesystem::path listfileRoot_;
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::pair<canon::TextureRef, std::string>> done_;
};

}  // namespace

int exportWorld(int argc, char** args) {
    CLI::App app{"husk export-world -- every ADT tile of every map to a canonical scene", "husk export-world"};
    ExportWorldOptions opts;
    addExportWorldOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;

    try {
        // The casc-tool mount answers every lookup through its `live` link
        // by re-reading its tag files; with every thread reading, that was
        // the whole cost of the run (12.1.0 export, 2026-10-12). Resolve the
        // links once and read by the real path.
        for (std::string* root : {&opts.listfileRoot, &opts.mapsRoot}) {
            std::error_code ec;
            std::filesystem::path real = std::filesystem::canonical(*root, ec);
            if (ec) throw SourceGone("expected the source tree " + *root + ", got none (unmounted?): " + ec.message());
            *root = real.string();
        }
        std::cout << "source: " << opts.listfileRoot << std::endl;
        unsigned jobs = opts.jobs > 0 ? opts.jobs : std::max(1u, std::thread::hardware_concurrency());
        std::filesystem::path out = std::filesystem::absolute(opts.output);
        std::filesystem::path modelsDir = out / "models";
        std::filesystem::path texturesDir = out / "textures";
        std::filesystem::create_directories(modelsDir);
        std::filesystem::create_directories(texturesDir);
        // Every texture payload of the export, tiles' and models' alike.
        writers::TextureStore textureStore(texturesDir);
        const writers::StoreLink modelTextures{&textureStore, "../../textures/"};
        ErrorLog errors(out / "errors.log", opts.listfileRoot);
        inputs::Log inputLog(out / "inputs.log", opts.listfileRoot);
        inputs::setRunLog(&inputLog);
        struct RunLogReset {
            ~RunLogReset() { inputs::setRunLog(nullptr); }
        } runLogReset;
        auto outputName = [&](const std::filesystem::path& bundle) { return bundle.lexically_relative(out).generic_string(); };
        auto wdtPathOf = [&](const std::string& map) {
            return std::filesystem::path(opts.mapsRoot) / map / (map + ".wdt");
        };

        std::map<std::string, adt::WdtFile> wdts;
        for (const auto& mapDir : std::filesystem::directory_iterator(opts.mapsRoot)) {
            std::string map = mapDir.path().filename().string();
            std::filesystem::path wdtPath = mapDir.path() / (map + ".wdt");
            if (!mapDir.is_directory()) continue;
            if (!opts.maps.empty() && std::find(opts.maps.begin(), opts.maps.end(), map) == opts.maps.end()) continue;
            try {
                // Each tile and world names its WDT as its own input.
                inputs::Scope discovery(nullptr, "");
                wdts.emplace(map, adt::parseWdt(adt::readFile(wdtPath)));
            } catch (...) {
                errors.add("wdt", wdtPath.string(), exceptionMessage(std::current_exception()) + " (tiles export untextured)");
            }
        }
        // The store keeps stale tiles a WDT no longer lists. A map without a
        // readable WDT keeps all its tiles: nothing says which are stale.
        std::vector<WorldTile> tiles = findTiles(opts.mapsRoot, opts.maps);
        size_t unlisted = std::erase_if(tiles, [&](const WorldTile& t) {
            auto wdt = wdts.find(t.map);
            if (wdt == wdts.end()) return false;
            TileName name = parseTileName(t.root);
            return !wdt->second.hasTile(name.x, name.y);
        });
        // Maps without terrain: their WDT places one map-wide WMO.
        std::vector<std::string> globalMaps;
        for (const auto& [map, wdt] : wdts) {
            if (wdt.mapObject) globalMaps.push_back(map);
        }
        std::cout << tiles.size() << " tiles (" << unlisted << " more their WDT doesn't list, skipped), "
                  << globalMaps.size() << " maps without terrain, " << jobs << " threads" << std::endl;

        std::unique_ptr<ListfileIndex> listfile = loadListfileCached(opts.listfile);
        std::string groundEffectNote;
        const adtinput::GroundEffectDefinitions groundEffects =
            loadGroundEffects(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), groundEffectNote);
        std::cout << groundEffectNote << std::endl;
        std::string liquidNote;
        const std::optional<liquiddb::Data> liquids =
            loadLiquids(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), liquidNote);
        std::cout << liquidNote << std::endl;
        std::string lightNote;
        const std::optional<lightdb::Data> lights = loadLights(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), lightNote);
        std::cout << lightNote << std::endl;
        std::string creatureNote;
        const std::optional<creaturedisplay::Data> creatures =
            loadCreatures(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), creatureNote);
        std::cout << creatureNote << std::endl;

        // Pass 1: references.
        std::set<uint32_t> modelIds;
        for (const auto& [id, effect] : groundEffects) {
            for (const canon::GroundEffectDoodad& d : effect.doodads) {
                if (auto fdid = std::get_if<canon::FileDataId>(&d.model.id)) modelIds.insert(fdid->value);
            }
        }
        std::set<std::string> namedPaths;
        std::set<std::string> namedModelPaths;
        std::set<uint32_t> wmoIds;
        std::set<std::string> namedWmoPaths;
        std::mutex referencesMutex;
        {
            Progress progress("collect", tiles.size());
            parallelFor(tiles.size(), jobs, [&](size_t i) {
                inputs::Scope discovery(nullptr, "");
                try {
                    // Unreadable sidecars are logged once, by pass 3, when the tile itself exports.
                    std::optional<adt::ObjFile> obj =
                        loadSidecar(tiles[i].root, "auto", "_obj0.adt", "--obj", &adt::parseObj).value;
                    std::optional<adt::TexFile> tex =
                        loadSidecar(tiles[i].root, "auto", "_tex0.adt", "--tex", &adt::parseTex).value;
                    std::lock_guard<std::mutex> lock(referencesMutex);
                    for (const std::string& path : adtinput::gamePaths(obj, tex)) namedPaths.insert(path);
                    if (obj) {
                        for (const adt::DoodadPlacement& d : obj->doodads) {
                            if (d.flags & adt::kMddfNameIsFileDataId) {
                                modelIds.insert(d.nameId);
                            } else {
                                namedModelPaths.insert(obj->doodadNames.at(d.nameId));
                            }
                        }
                        for (const adt::MapObjectPlacement& m : obj->mapObjects) {
                            if (m.flags & adt::kModfNameIsFileDataId) {
                                wmoIds.insert(m.nameId);
                            } else {
                                namedWmoPaths.insert(obj->mapObjectNames.at(m.nameId));
                            }
                        }
                    }
                } catch (...) {
                    errors.add("collect", tiles[i].root.string(), exceptionMessage(std::current_exception()));
                }
                progress.tick();
            });
        }
        for (const std::string& map : globalMaps) {
            const adt::WdtFile& wdt = wdts.at(map);
            if (wdt.mapObject->flags & adt::kModfNameIsFileDataId) {
                wmoIds.insert(wdt.mapObject->nameId);
            } else {
                namedPaths.insert(wdt.mapObjectName);
                namedWmoPaths.insert(wdt.mapObjectName);
            }
        }
        const adtinput::GamePathResolutions gamePaths =
            sources::fileDataIdsForGamePaths(*listfile, {namedPaths.begin(), namedPaths.end()});
        for (const std::string& path : namedModelPaths) {
            auto it = gamePaths.find(path);
            if (it != gamePaths.end()) modelIds.insert(it->second);
        }
        for (const std::string& path : namedWmoPaths) {
            auto it = gamePaths.find(path);
            if (it != gamePaths.end()) wmoIds.insert(it->second);
        }
        std::cout << "resolved " << gamePaths.size() << "/" << namedPaths.size()
                  << " name-table paths against the listfile" << std::endl;

        if (opts.allAssets) {
            std::vector<std::pair<uint32_t, bool>> assets;  // fdid, is a WMO
            listfile->forEach([&](uint32_t fdid, std::string_view path) {
                if (path.ends_with(".m2")) assets.push_back({fdid, false});
                if (isRootWmoPath(path)) assets.push_back({fdid, true});
                return true;
            });
            std::sort(assets.begin(), assets.end());
            if (opts.sample > 0 && opts.sample < assets.size()) {
                // Fixed seed: the same sample every run, so projections compare.
                std::mt19937 rng(1);
                std::shuffle(assets.begin(), assets.end(), rng);
                assets.resize(opts.sample);
            }
            size_t before = modelIds.size() + wmoIds.size();
            for (const auto& [fdid, isWmo] : assets) (isWmo ? wmoIds : modelIds).insert(fdid);
            std::cout << "--all-assets: " << assets.size() << " listfile models and WMOs, "
                      << modelIds.size() + wmoIds.size() - before << " of them placed nowhere" << std::endl;
        }

        // The WMOs' own doodads join the model pass. Only roots are read
        // here; a WMO that can't be found is logged now and skipped silently
        // when its bundle would be written.
        auto wmoSource = [&](uint32_t fdid) {
            return sources::localFileForFileDataId(*listfile, opts.listfileRoot, fdid);
        };
        std::vector<uint32_t> wmos(wmoIds.begin(), wmoIds.end());
        if (!opts.skipModels) {
            std::mutex doodadsMutex;
            Progress progress("wmo roots", wmos.size());
            parallelFor(wmos.size(), jobs, [&](size_t i) {
                inputs::Scope discovery(nullptr, "");
                try {
                    if (std::optional<std::filesystem::path> src = wmoSource(wmos[i])) {
                        wmo::RootFile root = wmo::parseRoot(adt::readFile(*src));
                        std::lock_guard<std::mutex> lock(doodadsMutex);
                        for (uint32_t fdid : root.doodadFileDataIds) {
                            if (fdid != 0) modelIds.insert(fdid);
                        }
                    } else {
                        errors.add("wmo", std::to_string(wmos[i]), "not in listfile or not extracted");
                    }
                } catch (...) {
                    errors.add("wmo", std::to_string(wmos[i]), exceptionMessage(std::current_exception()));
                }
                progress.tick();
            });
        }

        // Pass 2: models, in-process, one shared listfile. --db2-dir/--dbd-dir
        // none and an empty config keep per-model DB2 enrichment (legacy glTF
        // extras only) from running once per model.
        if (!opts.skipModels) {
            ExportOptions modelOpts;
            CLI::App modelApp;
            addExportOptions(modelApp, modelOpts);
            std::vector<std::string> modelArgs = {"--bundle-only", "--listfile", opts.listfile, "--listfile-root",
                                                  opts.listfileRoot, "--db2-dir", "none", "--dbd-dir", "none",
                                                  "--config", "/dev/null"};
            std::vector<char*> argv;
            for (std::string& a : modelArgs) argv.push_back(a.data());
            if (auto exit = parseCommandArgs(modelApp, static_cast<int>(argv.size()), argv.data())) return *exit;

            std::vector<uint32_t> ids(modelIds.begin(), modelIds.end());
            Progress progress("models", ids.size());
            parallelFor(ids.size(), jobs, [&](size_t i) {
                uint32_t fdid = ids[i];
                std::filesystem::path bundle = modelsDir / modelBundleName(fdid);
                try {
                    if (!std::filesystem::exists(bundle / "manifest.json")) {
                        inputs::Scope scope(&inputLog, outputName(bundle));
                        std::optional<std::filesystem::path> src =
                            sources::localFileForFileDataId(*listfile, opts.listfileRoot, fdid);
                        if (!src) {
                            errors.add("model", std::to_string(fdid), "no local file (listfile or _by_fdid)");
                        } else if (exportOneModel(modelOpts, modelApp, src->string(), bundle.string(), true,
                                                  *listfile, fdid, creatures ? &*creatures : nullptr, &modelTextures) != 0) {
                            errors.add("model", std::to_string(fdid), "export failed (see stderr): " + src->string());
                        }
                    }
                } catch (...) {
                    errors.add("model", std::to_string(fdid), exceptionMessage(std::current_exception()));
                }
                progress.tick();
            });
        }

        auto scanExportedModels = [&] {
            std::unordered_set<uint32_t> exported;
            for (const auto& entry : std::filesystem::directory_iterator(modelsDir)) {
                std::string name = entry.path().filename().string();
                size_t digits = name.find_first_not_of("0123456789");
                if (digits == 0 || name.compare(digits, std::string::npos, ".canon.bundle") != 0) continue;
                if (!std::filesystem::exists(entry.path() / "manifest.json")) continue;
                exported.insert(static_cast<uint32_t>(std::stoul(name.substr(0, digits))));
            }
            return exported;
        };
        std::unordered_set<uint32_t> exportedModels = scanExportedModels();

        // Pass 2b: WMO bundles, beside the model bundles (a WMO is an
        // ordinary object bundle), their doodad sets linked to pass 2's models.
        if (!opts.skipModels) {
            Progress progress("wmos", wmos.size());
            parallelFor(wmos.size(), jobs, [&](size_t i) {
                uint32_t fdid = wmos[i];
                std::filesystem::path bundle = modelsDir / modelBundleName(fdid);
                try {
                    std::optional<std::filesystem::path> src = wmoSource(fdid);
                    if (src && !std::filesystem::exists(bundle / "manifest.json")) {
                        inputs::Scope scope(&inputLog, outputName(bundle));
                        auto warn = [&](const std::string& w) { errors.add("wmo", std::to_string(fdid), w); };
                        canon::Model model = loadWmoModel(*src, fdid, listfile.get(), opts.listfileRoot,
                                                          liquids ? &*liquids : nullptr, warn);
                        writers::AssetUris doodadUris;
                        for (uint32_t doodad : placedAssets(model)) {
                            if (exportedModels.count(doodad)) {
                                doodadUris.emplace(doodad, "../" + modelBundleName(doodad) + "/manifest.json");
                            }
                        }
                        writers::AssetUris textureUris;
                        writers::externalizeTextures(model, textureStore, modelTextures.uriToStore, textureUris);
                        writers::writeBundle(model, bundle, wmoProvenance(listfile.get(), opts.listfile, liquids.has_value()),
                                             doodadUris, textureUris);
                    }
                } catch (...) {
                    errors.add("wmo", std::to_string(fdid), exceptionMessage(std::current_exception()));
                }
                progress.tick();
            });
            exportedModels = scanExportedModels();
        }

        // Pass 3: tiles.
        std::vector<std::filesystem::path> roots;
        for (const WorldTile& t : tiles) roots.push_back(t.root);
        const std::unordered_map<std::string, uint32_t> rootIds = tileFileDataIds(*listfile, opts.listfileRoot, roots);
        std::unordered_map<std::string, std::optional<uint32_t>> wdtFlags;
        for (const WorldTile& t : tiles) {
            auto wdt = wdts.find(t.map);
            wdtFlags[t.map] = wdt != wdts.end() ? std::optional<uint32_t>(wdt->second.flags) : std::nullopt;
        }
        SharedTextures sharedTextures(textureStore, *listfile, opts.listfileRoot);
        Progress progress("tiles", tiles.size());
        parallelFor(tiles.size(), jobs, [&](size_t i) {
            const WorldTile& t = tiles[i];
            std::filesystem::path bundle = out / "maps" / t.map / (t.root.stem().string() + ".bundle");
            try {
                if (!std::filesystem::exists(bundle / "manifest.json")) {
                    inputs::Scope scope(&inputLog, outputName(bundle));
                    if (wdts.count(t.map)) inputs::use(wdtPathOf(t.map));
                    TileName name = parseTileName(t.root);
                    adt::RootFile root = adt::parseRoot(adt::readFile(t.root));
                    Sidecar<adt::ObjFile> objSidecar = loadSidecar(t.root, "auto", "_obj0.adt", "--obj", &adt::parseObj);
                    if (!objSidecar.warning.empty()) errors.add("obj", t.root.string(), objSidecar.warning);
                    std::optional<adt::ObjFile>& obj = objSidecar.value;
                    std::optional<adt::TexFile> tex;
                    const std::optional<uint32_t>& flags = wdtFlags.at(t.map);
                    if (flags) {
                        Sidecar<adt::TexFile> texSidecar =
                            loadSidecar(t.root, "auto", "_tex0.adt", "--tex", &adt::parseTex);
                        if (!texSidecar.warning.empty()) errors.add("tex", t.root.string(), texSidecar.warning);
                        tex = std::move(texSidecar.value);
                    }

                    std::filesystem::path relToOut = std::filesystem::relative(out, bundle);
                    adtinput::TextureResolutions textures;
                    writers::AssetUris textureUris;
                    std::vector<uint32_t> textureIds;
                    if (tex) textureIds = adtinput::textureFileDataIds(*tex, gamePaths);
                    if (liquids) {
                        for (uint32_t fdid : liquidinput::textureFileDataIds(adtinput::liquidTypeRows(root), *liquids)) {
                            textureIds.push_back(fdid);
                        }
                    }
                    for (uint32_t fdid : textureIds) {
                        auto [ref, filename] = sharedTextures.get(fdid);
                        textures.emplace(fdid, ref);
                        if (!filename.empty()) textureUris.emplace(fdid, (relToOut / "textures" / filename).generic_string());
                    }
                    adtinput::TileSources sources{root, obj, tex, flags, name.map, name.x, name.y};
                    canon::Terrain terrain = adtinput::buildCanonTerrain(
                        sources, {textures, groundEffects, gamePaths, liquids ? &*liquids : nullptr});
                    nameModelRefs(terrain, *listfile);

                    writers::AssetUris modelUris;
                    for (uint32_t fdid : referencedAssets(terrain)) {
                        if (exportedModels.count(fdid)) {
                            modelUris.emplace(fdid, (relToOut / "models" / modelBundleName(fdid) / "manifest.json").generic_string());
                        }
                    }
                    terrain.subject = writers::subjectRef(t.root, tileFileDataId(rootIds, opts.listfileRoot, t.root));
                    writers::writeTerrainBundle(terrain, bundle,
                                                terrainProvenance(obj.has_value(), tex.has_value(), flags.has_value(),
                                                                  listfile.get(), opts.listfile, !groundEffects.empty(),
                                                                  liquids.has_value()),
                                                modelUris, textureUris);
                }
            } catch (...) {
                errors.add("tile", t.root.string(), exceptionMessage(std::current_exception()));
            }
            progress.tick();
        });

        // Pass 4: worlds.
        std::map<std::string, std::vector<WorldTile>> tilesByMap;
        for (const WorldTile& t : tiles) tilesByMap[t.map].push_back(t);
        std::set<std::string> mapSet;
        for (const auto& [map, _] : tilesByMap) mapSet.insert(map);
        for (const std::string& map : globalMaps) mapSet.insert(map);
        std::vector<std::string> maps(mapSet.begin(), mapSet.end());
        size_t worlds = 0;
        {
            // One map at a time; loadWorld spreads a map's tiles over the threads.
            Progress worldProgress("worlds", maps.size());
            for (size_t i = 0; i < maps.size(); ++i) {
                std::filesystem::path mapDir = std::filesystem::path(opts.mapsRoot) / maps[i];
                std::filesystem::path bundle = out / "maps" / maps[i] / (maps[i] + ".world.bundle");
                try {
                    if (!std::filesystem::exists(bundle / "manifest.json")) {
                        inputs::Scope scope(&inputLog, outputName(bundle));
                        if (wdts.count(maps[i])) inputs::use(wdtPathOf(maps[i]));
                        // The grid holds the tiles that exported; a failed one
                        // is a hole, already in errors.log from pass 3.
                        std::vector<WorldTile> mapTiles;
                        std::vector<std::string> tileUris;
                        for (const WorldTile& t : tilesByMap[maps[i]]) {
                            std::string tileBundle = t.root.stem().string() + ".bundle";
                            if (!std::filesystem::exists(out / "maps" / maps[i] / tileBundle / "manifest.json")) continue;
                            mapTiles.push_back(t);
                            tileUris.push_back("../" + tileBundle + "/manifest.json");
                        }
                        auto wdt = wdts.find(maps[i]);
                        if (wdt == wdts.end()) {
                            throw std::runtime_error("expected a readable " + (mapDir / (maps[i] + ".wdt")).string() +
                                                     " for the world document (see its wdt error, if any)");
                        }
                        canon::World world =
                            loadWorld(mapDir, wdt->second, mapTiles, listfile.get(), opts.listfileRoot,
                                      lights ? &*lights : nullptr, jobs);
                        writers::AssetUris assetUris;
                        auto link = [&](const canon::Ref& ref) {
                            auto fdid = std::get_if<canon::FileDataId>(&ref.id);
                            if (fdid && exportedModels.count(fdid->value)) {
                                assetUris.emplace(fdid->value, "../../../models/" + modelBundleName(fdid->value) + "/manifest.json");
                            }
                        };
                        for (const canon::PlacementSet& set : world.placements) {
                            for (const canon::PlacedInstance& instance : set.instances) {
                                link(instance.asset);
                                if (!instance.effect) continue;
                                // A fog's shape model is a mesh input.
                                for (const canon::NamedInput& input : instance.effect->inputs) {
                                    if (auto model = std::get_if<canon::Ref>(&input.value.value)) link(*model);
                                }
                            }
                        }
                        writers::writeWorldBundle(world, bundle, worldProvenance(mapDir, listfile.get(), opts.listfile, lights.has_value()),
                                                  tileUris, assetUris);
                    }
                    ++worlds;
                } catch (...) {
                    errors.add("world", mapDir.string(), exceptionMessage(std::current_exception()));
                }
                worldProgress.tick();
            }
        }

        std::cout << "done: " << tiles.size() << " tiles, " << worlds << " worlds, " << exportedModels.size()
                  << " model bundles, "
                  << errors.count() << " errors this run (" << (out / "errors.log").string() << ")" << std::endl;
    } catch (const SourceGone& e) {
        std::cerr << "husk: export-world: " << e.what() << "\n";
        return kExitSourceGone;
    } catch (const std::exception& e) {
        std::cerr << "husk: export-world: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

}  // namespace husk::commands
