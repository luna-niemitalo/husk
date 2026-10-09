#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "adt.hpp"
#include "adt_canon_input.hpp"
#include "blp.hpp"
#include "commands.hpp"
#include "husk_config.hpp"
#include "groundeffect_db2.hpp"
#include "listfile_cache.hpp"
#include "listfile_index.hpp"
#include "sources/listfile_catalog.hpp"
#include "sources/texture_payload.hpp"
#include "writers/bundle_common.hpp"
#include "writers/terrain_bundle_writer.hpp"

// `export-terrain` / `export-world`: ADT tiles (root + _obj0 + _tex0 + the
// map's WDT) to canonical terrain bundles (REFACTOR/BUNDLE_FORMAT.md's
// "Terrain tile bundles"). Every file, listfile and DB2 access happens here,
// assembled into one adtinput::Resolutions per tile; adtinput::
// buildCanonTerrain stays pure.
namespace husk::commands {

namespace {

// Same --config wiring as `export`/`resolve`: --listfile/--listfile-root/
// --db2-dir/--dbd-dir are per-machine facts (CLI.md §2.5), so a config file
// supplies them and 'none' opts one back out (README.md's "Config file").
void addConfigOption(CLI::App& app) {
    app.set_config("--config", husk::defaultConfigPath(),
                   "TOML file of default flag values (see README.md's config-file section) -- explicit CLI "
                   "flags always override a config value")
        ->envname("HUSK_CONFIG")
        ->group("Diagnostics");
}

}  // namespace

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
}

namespace {

struct TileName {
    std::string map;
    uint32_t x = 0;
    uint32_t y = 0;
};

const std::regex& tileNamePattern() {
    static const std::regex pattern(R"(^(.+)_(\d{1,2})_(\d{1,2})$)");
    return pattern;
}

// IN, critical: the file name is the only source of map name and tile
// coordinates (no WDT/Map.db2 lookup). Coordinates are cross-checked
// against MCNK positions in buildCanonTerrain.
TileName parseTileName(const std::filesystem::path& path) {
    std::string stem = path.stem().string();
    std::smatch m;
    if (!std::regex_match(stem, m, tileNamePattern())) {
        throw std::runtime_error("expected a root tile name <map>_<x>_<y>.adt, got '" + path.filename().string() + "'");
    }
    TileName out{m[1], static_cast<uint32_t>(std::stoul(m[2])), static_cast<uint32_t>(std::stoul(m[3]))};
    if (out.x > 63 || out.y > 63) {
        throw std::runtime_error("expected tile coordinates in 0..63, got (" + std::to_string(out.x) + "," +
                                 std::to_string(out.y) + ")");
    }
    return out;
}

// 'none' on a per-machine flag means "as if never set", beating a config value.
std::string unlessNone(const std::string& value) { return value == "none" ? std::string() : value; }

// One tile sidecar (_obj0/_tex0), resolved by CLI.md §2.11's three states,
// with what happened written out for the caller to print or log.
template <typename Parsed>
struct Sidecar {
    std::optional<Parsed> value;
    std::string note;     // what was resolved, always set
    std::string warning;  // set when an auto-found file was unreadable and skipped
};

// IN, non-critical when auto-found: the sibling is the tile's own implied
// reference, so a broken one is warned about (expected vs. actual) and
// skipped, and the rest of the tile still exports (FOREIGN_DATA.md §4).
// IN, critical when explicit: a path the user named that doesn't parse
// fails the export, since they asked for exactly that file.
template <typename Parsed>
Sidecar<Parsed> loadSidecar(const std::filesystem::path& root, const std::string& arg, const std::string& suffix,
                            const char* flag, Parsed (*parse)(const std::vector<uint8_t>&)) {
    Sidecar<Parsed> out;
    if (arg == "none") {
        out.note = std::string(flag) + ": skipped (" + flag + " none)";
        return out;
    }
    if (arg != "auto") {
        out.value = parse(adt::readFile(arg));
        out.note = std::string(flag) + ": " + arg + " (explicit)";
        return out;
    }
    std::filesystem::path sibling = root.parent_path() / (root.stem().string() + suffix);
    if (!std::filesystem::exists(sibling)) {
        out.note = std::string(flag) + ": none (no " + sibling.filename().string() + " beside the tile)";
        return out;
    }
    try {
        out.value = parse(adt::readFile(sibling));
        out.note = std::string(flag) + ": " + sibling.filename().string() + " (beside the tile)";
    } catch (const std::exception& e) {
        out.note = std::string(flag) + ": skipped (unreadable)";
        out.warning = "expected a readable " + sibling.filename().string() + ", got: " + e.what() +
                      " -- exporting the tile without it (" + flag + " none silences this)";
    }
    return out;
}

// FileDataID -> listfile path -> the BLP on disk -> its DDS-housed source
// blocks, or decoded PNG when the encoding can't be rehoused (palettized).
// Terrain has no model directory to search, so of RESOURCE_CATALOG.md's
// texture tiers only the listfile one applies.
canon::TextureRef resolveTexture(uint32_t fdid, const ListfileIndex& listfile, const std::string& listfileRoot) {
    canon::TextureRef ref;
    std::optional<std::filesystem::path> path = sources::pathForFileDataId(listfile, listfileRoot, fdid);
    if (!path) {
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.unresolvedReason = "FileDataID " + std::to_string(fdid) + " not in listfile";
        return ref;
    }
    if (!std::filesystem::exists(*path)) {
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.unresolvedReason = "listfile path not extracted locally: " + path->string();
        return ref;
    }
    std::vector<uint8_t> bytes = adt::readFile(*path);
    ref.state = canon::TextureRef::State::Resolved;
    ref.resolved = canon::Ref{canon::FileDataId{fdid}, path->stem().string(), canon::NameSource::Listfile};
    try {
        ref.rawPayload = sources::ddsPayloadFromBlp(bytes);
    } catch (const blp::ParseError&) {
        ref.payload = sources::pngPayloadFromBlp(bytes);
    }
    return ref;
}

// Every ground-effect rule the DB2s define, or none when they aren't
// given or can't be read -- ground cover is optional enrichment (CLI.md
// §2.10: fall back, and say so in `note`), never a reason to fail a tile.
adtinput::GroundEffectDefinitions loadGroundEffects(const std::string& db2Dir, const std::string& dbdDir,
                                                    std::string& note) {
    if (db2Dir.empty() || dbdDir.empty()) {
        note = "ground effects: none (needs --db2-dir and --dbd-dir)";
        return {};
    }
    std::ostringstream err;
    std::optional<groundeffect::Data> data = groundeffect::load(db2Dir, dbdDir, err);
    if (!data) {
        note = "ground effects: none (GroundEffect DB2s unreadable: " + err.str() + ")";
        return {};
    }
    adtinput::GroundEffectDefinitions out = adtinput::groundEffectDefinitions(*data);
    note = "ground effects: " + std::to_string(out.size()) + " rules from " + db2Dir;
    return out;
}

std::set<uint32_t> referencedModels(const canon::Terrain& terrain) {
    std::set<uint32_t> out;
    for (const canon::Placement& p : terrain.placements) {
        if (p.kind != canon::Placement::Kind::Model) continue;
        if (auto fdid = std::get_if<canon::FileDataId>(&p.asset.id)) out.insert(fdid->value);
    }
    for (const canon::GroundEffect& e : terrain.groundEffects) {
        for (const canon::GroundEffectDoodad& d : e.doodads) {
            if (auto fdid = std::get_if<canon::FileDataId>(&d.model.id)) out.insert(fdid->value);
        }
    }
    return out;
}

// Same naming rule as resolveTexture: the listfile path's stem, tagged as a listfile name.
void nameFromListfile(canon::Ref& ref, const ListfileIndex& listfile) {
    auto fdid = std::get_if<canon::FileDataId>(&ref.id);
    if (!fdid) return;
    std::optional<std::string_view> listed = listfile.lookup(fdid->value);
    if (!listed) return;
    ref.name = std::filesystem::path(std::string(*listed)).stem().string();
    ref.source = canon::NameSource::Listfile;
}

void nameModelRefs(canon::Terrain& terrain, const ListfileIndex& listfile) {
    for (canon::Placement& p : terrain.placements) nameFromListfile(p.asset, listfile);
    for (canon::GroundEffect& e : terrain.groundEffects) {
        for (canon::GroundEffectDoodad& d : e.doodads) nameFromListfile(d.model, listfile);
    }
}

std::string modelBundleName(uint32_t fdid) { return std::to_string(fdid) + ".canon.bundle"; }

// Reversed for the same reason as resolve()'s identical call (cmd_resolve.cpp).
bool parseArgs(CLI::App& app, int argc, char** args, int& exitCode) {
    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        exitCode = app.exit(e);
        return false;
    }
    return true;
}

// --- export-world ---------------------------------------------------------

// Thread-safe append-only failure log: one "phase<TAB>item<TAB>message" line
// per failure, so a rerun (which skips finished outputs) can be checked
// against it.
class ErrorLog {
public:
    explicit ErrorLog(const std::filesystem::path& path) : out_(path, std::ios::app) {
        if (!out_) throw std::runtime_error("cannot open " + path.string());
    }
    void add(const std::string& phase, const std::string& item, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        out_ << phase << '\t' << item << '\t' << message << '\n';
        out_.flush();
        ++count_;
    }
    size_t count() const { return count_; }

private:
    std::mutex mutex_;
    std::ofstream out_;
    std::atomic<size_t> count_{0};
};

class Progress {
public:
    Progress(std::string phase, size_t total) : phase_(std::move(phase)), total_(total) {}
    void tick() {
        size_t done = ++done_;
        if (done % 500 == 0 || done == total_) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << phase_ << ": " << done << "/" << total_ << std::endl;
        }
    }

private:
    std::string phase_;
    size_t total_;
    std::atomic<size_t> done_{0};
    std::mutex mutex_;
};

// Runs work(i) for i in [0, n) on `jobs` threads. `work` handles its own errors.
template <typename Work>
void parallelFor(size_t n, unsigned jobs, Work work) {
    std::atomic<size_t> next{0};
    std::vector<std::thread> threads;
    for (unsigned j = 0; j < jobs; ++j) {
        threads.emplace_back([&] {
            for (size_t i = next++; i < n; i = next++) work(i);
        });
    }
    for (std::thread& t : threads) t.join();
}

struct WorldTile {
    std::filesystem::path root;
    std::string map;
};

std::vector<WorldTile> findTiles(const std::filesystem::path& mapsRoot, const std::vector<std::string>& onlyMaps) {
    std::vector<WorldTile> tiles;
    std::set<std::string> wanted(onlyMaps.begin(), onlyMaps.end());
    for (const auto& mapDir : std::filesystem::directory_iterator(mapsRoot)) {
        if (!mapDir.is_directory()) continue;
        std::string map = mapDir.path().filename().string();
        if (!wanted.empty() && !wanted.count(map)) continue;
        for (const auto& f : std::filesystem::directory_iterator(mapDir.path())) {
            std::smatch m;
            std::string stem = f.path().stem().string();
            if (f.path().extension() == ".adt" && std::regex_match(stem, m, tileNamePattern()) && m[1] == map) {
                tiles.push_back({f.path(), map});
            }
        }
    }
    std::sort(tiles.begin(), tiles.end(), [](const WorldTile& a, const WorldTile& b) { return a.root < b.root; });
    return tiles;
}

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
    SharedTextures(std::filesystem::path dir, const ListfileIndex& listfile, std::filesystem::path listfileRoot)
        : dir_(std::move(dir)), listfile_(listfile), listfileRoot_(std::move(listfileRoot)) {}

    // The texture without payload plus its filename under textures/ (empty
    // when unresolved). Two threads may resolve the same new FDID at once;
    // both write identical bytes, so that race is harmless.
    std::pair<canon::TextureRef, std::string> get(uint32_t fdid) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = done_.find(fdid);
            if (it != done_.end()) return it->second;
        }
        canon::TextureRef ref = resolveTexture(fdid, listfile_, listfileRoot_);
        std::string filename;
        const canon::TextureRef::Payload* chosen = ref.rawPayload ? &*ref.rawPayload : ref.payload ? &*ref.payload : nullptr;
        if (chosen) {
            filename = std::to_string(fdid) + "." + writers::textureFileExtension(chosen->encoding);
            writers::writeFile(dir_ / filename, chosen->bytes);
            ref.payload.reset();
            ref.rawPayload.reset();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return done_.emplace(fdid, std::make_pair(std::move(ref), filename)).first->second;
    }

private:
    std::filesystem::path dir_;
    const ListfileIndex& listfile_;
    std::filesystem::path listfileRoot_;
    std::mutex mutex_;
    std::unordered_map<uint32_t, std::pair<canon::TextureRef, std::string>> done_;
};

}  // namespace

int exportTerrain(int argc, char** args) {
    CLI::App app{"husk export-terrain -- one ADT tile to a canonical terrain bundle", "husk export-terrain"};
    ExportTerrainOptions opts;
    addExportTerrainOptions(app, opts);
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;

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

        adtinput::TileSources sources{root, obj, tex, wdtFlags, tile.map, tile.x, tile.y};
        canon::Terrain terrain = adtinput::buildCanonTerrain(sources, {textures, groundEffects, gamePaths});
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
            for (uint32_t fdid : referencedModels(terrain)) {
                std::filesystem::path manifest =
                    std::filesystem::absolute(opts.modelsDir) / modelBundleName(fdid) / "manifest.json";
                if (!std::filesystem::exists(manifest)) continue;
                modelUris.emplace(fdid, std::filesystem::relative(manifest, bundleDir).generic_string());
            }
        }
        writers::writeTerrainBundle(terrain, opts.output, modelUris);
        std::cout << "wrote " << opts.output << ": " << terrain.chunks.size() << " chunks, " << terrain.textures.size()
                  << " textures, " << terrain.groundEffects.size() << " ground effects, " << terrain.liquids.size()
                  << " liquid surfaces, " << terrain.placements.size() << " placements, " << modelUris.size()
                  << " linked models\n";
    } catch (const std::exception& e) {
        std::cerr << "husk: export-terrain: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

// Three passes over the world, each parallel, each resumable (an output
// whose manifest.json exists is skipped -- every writer writes it last):
//   1. read every tile's _obj0/_tex0 once for what it references: model
//      FileDataIDs and name-table paths (resolved against the listfile in
//      one pass for the whole world), plus every GroundEffectDoodad model,
//   2. export each model once to models/<fdid>.canon.bundle via
//      exportOneModel --bundle-only (one shared listfile, no DB2 enrichment),
//   3. export each tile to maps/<map>/<tile>.bundle, terrain textures shared
//      under textures/, uris only to outputs that exist.
// Failures are logged to errors.log and never stop the run.
int exportWorld(int argc, char** args) {
    CLI::App app{"husk export-world -- every ADT tile of every map to a canonical scene", "husk export-world"};
    ExportWorldOptions opts;
    addExportWorldOptions(app, opts);
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;

    try {
        unsigned jobs = opts.jobs > 0 ? opts.jobs : std::max(1u, std::thread::hardware_concurrency());
        std::filesystem::path out = std::filesystem::absolute(opts.output);
        std::filesystem::path modelsDir = out / "models";
        std::filesystem::path texturesDir = out / "textures";
        std::filesystem::create_directories(modelsDir);
        std::filesystem::create_directories(texturesDir);
        ErrorLog errors(out / "errors.log");

        std::vector<WorldTile> tiles = findTiles(opts.mapsRoot, opts.maps);
        std::cout << tiles.size() << " tiles, " << jobs << " threads" << std::endl;

        std::unique_ptr<ListfileIndex> listfile = loadListfileCached(opts.listfile);
        std::string groundEffectNote;
        const adtinput::GroundEffectDefinitions groundEffects =
            loadGroundEffects(unlessNone(opts.db2Dir), unlessNone(opts.dbdDir), groundEffectNote);
        std::cout << groundEffectNote << std::endl;

        // Pass 1: references.
        std::set<uint32_t> modelIds;
        for (const auto& [id, effect] : groundEffects) {
            for (const canon::GroundEffectDoodad& d : effect.doodads) {
                if (auto fdid = std::get_if<canon::FileDataId>(&d.model.id)) modelIds.insert(fdid->value);
            }
        }
        std::set<std::string> namedPaths;
        std::set<std::string> namedModelPaths;
        std::mutex referencesMutex;
        {
            Progress progress("collect", tiles.size());
            parallelFor(tiles.size(), jobs, [&](size_t i) {
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
                    }
                } catch (...) {
                    errors.add("collect", tiles[i].root.string(), exceptionMessage(std::current_exception()));
                }
                progress.tick();
            });
        }
        const adtinput::GamePathResolutions gamePaths =
            sources::fileDataIdsForGamePaths(*listfile, {namedPaths.begin(), namedPaths.end()});
        for (const std::string& path : namedModelPaths) {
            auto it = gamePaths.find(path);
            if (it != gamePaths.end()) modelIds.insert(it->second);
        }
        std::cout << "resolved " << gamePaths.size() << "/" << namedPaths.size()
                  << " name-table paths against the listfile" << std::endl;

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
            if (!parseArgs(modelApp, static_cast<int>(argv.size()), argv.data(), exitCode)) return exitCode;

            std::vector<uint32_t> ids(modelIds.begin(), modelIds.end());
            Progress progress("models", ids.size());
            parallelFor(ids.size(), jobs, [&](size_t i) {
                uint32_t fdid = ids[i];
                std::filesystem::path bundle = modelsDir / modelBundleName(fdid);
                try {
                    if (!std::filesystem::exists(bundle / "manifest.json")) {
                        std::optional<std::string_view> listed = listfile->lookup(fdid);
                        if (!listed) {
                            errors.add("model", std::to_string(fdid), "not in listfile");
                        } else {
                            std::filesystem::path src = std::filesystem::path(opts.listfileRoot) / std::string(*listed);
                            if (!std::filesystem::exists(src)) {
                                errors.add("model", std::to_string(fdid), "not extracted: " + src.string());
                            } else if (exportOneModel(modelOpts, modelApp, src.string(), bundle.string(), true,
                                                      *listfile) != 0) {
                                errors.add("model", std::to_string(fdid), "export failed (see stderr): " + src.string());
                            }
                        }
                    }
                } catch (...) {
                    errors.add("model", std::to_string(fdid), exceptionMessage(std::current_exception()));
                }
                progress.tick();
            });
        }

        std::unordered_set<uint32_t> exportedModels;
        for (const auto& entry : std::filesystem::directory_iterator(modelsDir)) {
            std::string name = entry.path().filename().string();
            size_t digits = name.find_first_not_of("0123456789");
            if (digits == 0 || name.compare(digits, std::string::npos, ".canon.bundle") != 0) continue;
            if (!std::filesystem::exists(entry.path() / "manifest.json")) continue;
            exportedModels.insert(static_cast<uint32_t>(std::stoul(name.substr(0, digits))));
        }

        // Pass 3: tiles.
        std::unordered_map<std::string, std::optional<uint32_t>> wdtFlags;
        for (const WorldTile& t : tiles) {
            if (wdtFlags.count(t.map)) continue;
            std::filesystem::path wdt = t.root.parent_path() / (t.map + ".wdt");
            try {
                wdtFlags[t.map] = adt::parseWdtFlags(adt::readFile(wdt));
            } catch (...) {
                wdtFlags[t.map] = std::nullopt;
                errors.add("wdt", wdt.string(), exceptionMessage(std::current_exception()) + " (tiles export untextured)");
            }
        }
        SharedTextures sharedTextures(texturesDir, *listfile, opts.listfileRoot);
        Progress progress("tiles", tiles.size());
        parallelFor(tiles.size(), jobs, [&](size_t i) {
            const WorldTile& t = tiles[i];
            std::filesystem::path bundle = out / "maps" / t.map / (t.root.stem().string() + ".bundle");
            try {
                if (!std::filesystem::exists(bundle / "manifest.json")) {
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
                    if (tex) {
                        for (uint32_t fdid : adtinput::textureFileDataIds(*tex, gamePaths)) {
                            auto [ref, filename] = sharedTextures.get(fdid);
                            textures.emplace(fdid, ref);
                            if (!filename.empty()) textureUris.emplace(fdid, (relToOut / "textures" / filename).generic_string());
                        }
                    }
                    adtinput::TileSources sources{root, obj, tex, flags, name.map, name.x, name.y};
                    canon::Terrain terrain = adtinput::buildCanonTerrain(sources, {textures, groundEffects, gamePaths});
                    nameModelRefs(terrain, *listfile);

                    writers::AssetUris modelUris;
                    for (uint32_t fdid : referencedModels(terrain)) {
                        if (exportedModels.count(fdid)) {
                            modelUris.emplace(fdid, (relToOut / "models" / modelBundleName(fdid) / "manifest.json").generic_string());
                        }
                    }
                    writers::writeTerrainBundle(terrain, bundle, modelUris, textureUris);
                }
            } catch (...) {
                errors.add("tile", t.root.string(), exceptionMessage(std::current_exception()));
            }
            progress.tick();
        });

        std::cout << "done: " << tiles.size() << " tiles, " << exportedModels.size() << " model bundles, "
                  << errors.count() << " errors this run (" << (out / "errors.log").string() << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "husk: export-world: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

}  // namespace husk::commands
