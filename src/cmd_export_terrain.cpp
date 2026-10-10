#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include "bundle_schema.hpp"
#include "version.hpp"
#include "commands.hpp"
#include "husk_config.hpp"
#include "groundeffect_db2.hpp"
#include "listfile_cache.hpp"
#include "listfile_index.hpp"
#include "map_scene_input.hpp"
#include "sources/listfile_catalog.hpp"
#include "sources/texture_payload.hpp"
#include "wdt_volumes.hpp"
#include "wmo.hpp"
#include "wmo_canon_input.hpp"
#include "writers/bundle_common.hpp"
#include "writers/bundle_writer.hpp"
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

void addExportMapSceneOptions(CLI::App& app, ExportMapSceneOptions& opts) {
    addConfigOption(app);
    app.add_option("input", opts.input, "map directory holding <map>_fogs.wdt and/or <map>_mpv.wdt")->required();
    app.add_option("output", opts.output, "bundle directory to write")->required();
    app.add_option("--listfile", opts.listfile,
                   "community listfile CSV, or 'none': resolves particle textures, names fog models")
        ->group("Resolution");
    app.add_option("--listfile-root", opts.listfileRoot, "directory the listfile's paths are relative to, or 'none'")
        ->group("Resolution");
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
    app.add_option("--models-dir", opts.modelsDir,
                   "set doodad uris to <dir>/<fdid>.canon.bundle where that bundle exists")
        ->group("Output");
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

// M2s only: what `husk export --bundle-only` can export.
std::set<uint32_t> referencedModels(const canon::Terrain& terrain) {
    std::set<uint32_t> out;
    for (const canon::PlacedInstance& instance : terrain.doodads.instances) {
        if (auto fdid = std::get_if<canon::FileDataId>(&instance.asset.id)) out.insert(fdid->value);
    }
    for (const canon::GroundEffect& e : terrain.groundEffects) {
        for (const canon::GroundEffectDoodad& d : e.doodads) {
            if (auto fdid = std::get_if<canon::FileDataId>(&d.model.id)) out.insert(fdid->value);
        }
    }
    return out;
}

// Models and WMOs: every bundle a tile can link to.
std::set<uint32_t> referencedAssets(const canon::Terrain& terrain) {
    std::set<uint32_t> out = referencedModels(terrain);
    for (const canon::PlacedInstance& instance : terrain.mapObjects.instances) {
        if (auto fdid = std::get_if<canon::FileDataId>(&instance.asset.id)) out.insert(fdid->value);
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
    for (canon::PlacementSet* set : {&terrain.doodads, &terrain.mapObjects}) {
        for (canon::PlacedInstance& instance : set->instances) nameFromListfile(instance.asset, listfile);
    }
    for (canon::GroundEffect& e : terrain.groundEffects) {
        for (canon::GroundEffectDoodad& d : e.doodads) nameFromListfile(d.model, listfile);
    }
}

std::string modelBundleName(uint32_t fdid) { return std::to_string(fdid) + ".canon.bundle"; }

// IN, critical when found: a WMO group file, by its GFID FileDataID through
// the listfile, else by the <root stem>_NNN.wmo name beside the root (the
// local extraction's naming). nullopt when neither exists.
std::optional<std::filesystem::path> findWmoGroupFile(const std::filesystem::path& rootPath, const wmo::RootFile& root,
                                                      uint32_t group, const ListfileIndex* listfile,
                                                      const std::string& listfileRoot) {
    uint32_t fdid = wmo::groupFileDataId(root, 0, group);
    if (listfile && fdid != 0) {
        std::optional<std::filesystem::path> listed = sources::pathForFileDataId(*listfile, listfileRoot, fdid);
        if (listed && std::filesystem::exists(*listed)) return listed;
    }
    std::string suffix = std::to_string(group);
    suffix.insert(0, suffix.size() < 3 ? 3 - suffix.size() : 0, '0');
    std::filesystem::path sibling = rootPath.parent_path() / (rootPath.stem().string() + "_" + suffix + ".wmo");
    if (std::filesystem::exists(sibling)) return sibling;
    return std::nullopt;
}

// A WMO root and its base-LOD group files as a canon::Model. Missing group
// files are reported through `warn` and leave that part empty; a group file
// that exists but doesn't parse fails the export.
canon::Model loadWmoModel(const std::filesystem::path& rootPath, std::optional<uint32_t> fileDataId,
                          const ListfileIndex* listfile, const std::string& listfileRoot,
                          const std::function<void(const std::string&)>& warn) {
    wmo::RootFile root = wmo::parseRoot(adt::readFile(rootPath));
    wmoinput::WmoInputs inputs;
    inputs.root = &root;
    for (uint32_t g = 0; g < root.groups.size(); ++g) {
        std::optional<std::filesystem::path> path = findWmoGroupFile(rootPath, root, g, listfile, listfileRoot);
        if (!path) {
            warn("expected group " + std::to_string(g) + " of " + rootPath.filename().string() + " (FileDataID " +
                 std::to_string(wmo::groupFileDataId(root, 0, g)) + "), found no file -- its part has no geometry");
            inputs.groups.emplace_back();
            continue;
        }
        try {
            inputs.groups.emplace_back(wmo::parseGroup(adt::readFile(*path)));
        } catch (const std::exception& e) {
            throw std::runtime_error(path->string() + ": " + e.what());
        }
    }
    if (listfile) {
        for (const wmo::Material& m : root.materials) {
            for (uint32_t fdid : m.textures) {
                if (fdid != 0 && root.textureNames.empty() && !inputs.textures.count(fdid)) {
                    inputs.textures.emplace(fdid, resolveTexture(fdid, *listfile, listfileRoot));
                }
            }
        }
        for (const wmo::ParticulateVolume& v : root.particulateVolumes) {
            for (const wmo::ParticulateEmitter& e : v.emitters) {
                if (!inputs.textures.count(e.textureFileDataId)) {
                    inputs.textures.emplace(e.textureFileDataId, resolveTexture(e.textureFileDataId, *listfile, listfileRoot));
                }
            }
        }
        std::vector<uint32_t> named = root.doodadFileDataIds;
        for (const wmo::NewLight& light : root.newLights) named.push_back(light.cookieFileDataId);
        for (uint32_t fdid : named) {
            std::optional<std::string_view> listed = fdid != 0 ? listfile->lookup(fdid) : std::nullopt;
            if (listed) inputs.assetNames.emplace(fdid, std::filesystem::path(std::string(*listed)).stem().string());
        }
    }
    canon::Model model = wmoinput::buildCanonWmo(inputs);
    model.subject = writers::subjectRef(rootPath, fileDataId);
    return model;
}

writers::Provenance wmoProvenance(const ListfileIndex* listfile, const std::string& listfilePath) {
    writers::Provenance out{std::string("husk ") + husk::version(), "wmo", {}};
    out.sources.push_back({"listfile", "group files, textures and doodad names by FileDataID", listfile != nullptr,
                           listfile ? listfilePath : "not given"});
    return out;
}

// A map directory's _fogs.wdt and _mpv.wdt as a canon::MapScene; nullopt
// when the map has neither file. A file that exists but doesn't parse
// fails the export.
std::optional<canon::MapScene> loadMapScene(const std::filesystem::path& mapDir, const ListfileIndex* listfile,
                                            const std::string& listfileRoot) {
    std::string map = mapDir.filename().string();
    std::filesystem::path fogsPath = mapDir / (map + "_fogs.wdt");
    std::filesystem::path mpvPath = mapDir / (map + "_mpv.wdt");
    std::optional<wdt::FogsFile> fogs;
    std::optional<wdt::MpvFile> mpv;
    try {
        if (std::filesystem::exists(fogsPath)) fogs = wdt::parseFogs(adt::readFile(fogsPath));
        if (std::filesystem::exists(mpvPath)) mpv = wdt::parseMpv(adt::readFile(mpvPath));
    } catch (const std::exception& e) {
        throw std::runtime_error(map + ": " + e.what());
    }
    if (!fogs && !mpv) return std::nullopt;

    mapinput::MapSceneInputs inputs;
    inputs.map = map;
    inputs.fogs = fogs ? &*fogs : nullptr;
    inputs.mpv = mpv ? &*mpv : nullptr;
    if (listfile) {
        if (mpv) {
            for (const wdt::MapParticulateVolume& v : mpv->volumes) {
                for (const wmo::ParticulateEmitter& e : v.volume.emitters) {
                    if (!inputs.textures.count(e.textureFileDataId)) {
                        inputs.textures.emplace(e.textureFileDataId,
                                                resolveTexture(e.textureFileDataId, *listfile, listfileRoot));
                    }
                }
            }
        }
        if (fogs) {
            for (const wdt::Fog& f : fogs->fogs) {
                std::optional<std::string_view> listed = f.modelFileDataId ? listfile->lookup(f.modelFileDataId) : std::nullopt;
                if (listed) {
                    inputs.assetNames.emplace(f.modelFileDataId, std::filesystem::path(std::string(*listed)).stem().string());
                }
            }
        }
    }
    return mapinput::buildMapScene(inputs);
}

std::set<uint32_t> placedAssets(const canon::Model& model) {
    std::set<uint32_t> out;
    for (const canon::Model::OwnedSet& owned : model.placementSets) {
        for (const canon::PlacedInstance& instance : owned.set.instances) {
            if (auto fdid = std::get_if<canon::FileDataId>(&instance.asset.id)) out.insert(fdid->value);
        }
    }
    return out;
}

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
            for (uint32_t fdid : referencedAssets(terrain)) {
                std::filesystem::path manifest =
                    std::filesystem::absolute(opts.modelsDir) / modelBundleName(fdid) / "manifest.json";
                if (!std::filesystem::exists(manifest)) continue;
                modelUris.emplace(fdid, std::filesystem::relative(manifest, bundleDir).generic_string());
            }
        }
        writers::writeTerrainBundle(terrain, opts.output, modelUris);
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
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;

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
            listfile ? sources::fileDataIdForPath(*listfile, opts.input, listfileRoot) : std::nullopt;
        canon::Model model = loadWmoModel(opts.input, fileDataId, listfile.get(), listfileRoot, warn);

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
        writers::writeBundle(model, opts.output, wmoProvenance(listfile.get(), listfilePath), modelUris);
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
}

int validateBundle(int argc, char** args) {
    CLI::App app{"husk validate-bundle -- check documents against the bundle format's schema", "husk validate-bundle"};
    ValidateBundleOptions opts;
    addValidateBundleOptions(app, opts);
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;
    if (opts.paths.empty() && !opts.lintSchema) {
        std::cerr << "husk: validate-bundle: expected a bundle path or --lint-schema\n";
        return 2;
    }

    std::vector<bundle_schema::Problem> problems;
    if (opts.lintSchema) problems = bundle_schema::lintSchemas();
    size_t documents = 0;
    for (const std::string& path : opts.paths) {
        std::vector<std::filesystem::path> checked;
        for (bundle_schema::Problem& p : bundle_schema::validateDocument(path, &checked)) problems.push_back(std::move(p));
        documents += checked.size();
    }
    for (const bundle_schema::Problem& p : problems) std::cout << p.where << ": " << p.message << "\n";
    std::cout << documents << " documents checked, " << problems.size() << " problems\n";
    return problems.empty() ? 0 : 1;
}

int exportMapScene(int argc, char** args) {
    CLI::App app{"husk export-map-scene -- a map's fog and particulate volumes to a map-scene bundle",
                 "husk export-map-scene"};
    ExportMapSceneOptions opts;
    addExportMapSceneOptions(app, opts);
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;

    try {
        std::unique_ptr<ListfileIndex> listfile;
        std::string listfilePath = unlessNone(opts.listfile);
        if (!listfilePath.empty()) {
            listfile = loadListfileCached(listfilePath);
            std::cerr << "husk: note: listfile: " << listfile->size() << " rows from " << listfilePath << "\n";
        } else {
            std::cerr << "husk: note: listfile: none (particle textures stay unresolved, fog models unnamed)\n";
        }
        std::filesystem::path mapDir = std::filesystem::path(opts.input).lexically_normal();
        if (mapDir.filename().empty()) mapDir = mapDir.parent_path();
        std::optional<canon::MapScene> scene = loadMapScene(mapDir, listfile.get(), unlessNone(opts.listfileRoot));
        if (!scene) {
            std::string map = mapDir.filename().string();
            throw std::runtime_error("expected " + map + "_fogs.wdt or " + map + "_mpv.wdt in " + mapDir.string() +
                                     ", found neither");
        }
        writers::writeMapSceneBundle(*scene, opts.output, "husk dev");
        std::cout << "wrote " << opts.output << ": " << scene->fogs.size() << " fog volumes, "
                  << scene->particulateVolumes.size() << " particulate volumes\n";
    } catch (const std::exception& e) {
        std::cerr << "husk: export-map-scene: " << e.what() << "\n";
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
//      exportOneModel --bundle-only (one shared listfile, no DB2 enrichment),
//   3. export each tile to maps/<map>/<tile>.bundle, terrain textures shared
//      under textures/, uris only to outputs that exist.
//   4. export each map's fog and particulate volumes to
//      maps/<map>/<map>.scene.bundle, for maps whose sidecars have any.
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
        std::set<uint32_t> wmoIds;
        std::set<std::string> namedWmoPaths;
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

        // The WMOs' own doodads join the model pass. Only roots are read
        // here; a WMO that can't be found is logged now and skipped silently
        // when its bundle would be written.
        auto wmoSource = [&](uint32_t fdid) -> std::optional<std::filesystem::path> {
            std::optional<std::string_view> listed = listfile->lookup(fdid);
            if (!listed) return std::nullopt;
            std::filesystem::path src = std::filesystem::path(opts.listfileRoot) / std::string(*listed);
            if (!std::filesystem::exists(src)) return std::nullopt;
            return src;
        };
        std::vector<uint32_t> wmos(wmoIds.begin(), wmoIds.end());
        if (!opts.skipModels) {
            std::mutex doodadsMutex;
            Progress progress("wmo roots", wmos.size());
            parallelFor(wmos.size(), jobs, [&](size_t i) {
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
                        auto warn = [&](const std::string& w) { errors.add("wmo", std::to_string(fdid), w); };
                        canon::Model model = loadWmoModel(*src, fdid, listfile.get(), opts.listfileRoot, warn);
                        writers::AssetUris doodadUris;
                        for (uint32_t doodad : placedAssets(model)) {
                            if (exportedModels.count(doodad)) {
                                doodadUris.emplace(doodad, "../" + modelBundleName(doodad) + "/manifest.json");
                            }
                        }
                        writers::writeBundle(model, bundle, wmoProvenance(listfile.get(), opts.listfile), doodadUris);
                    }
                } catch (...) {
                    errors.add("wmo", std::to_string(fdid), exceptionMessage(std::current_exception()));
                }
                progress.tick();
            });
            exportedModels = scanExportedModels();
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
                    for (uint32_t fdid : referencedAssets(terrain)) {
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

        // Pass 4: map scenes.
        std::vector<std::filesystem::path> mapDirs;
        for (const WorldTile& t : tiles) {
            if (mapDirs.empty() || mapDirs.back() != t.root.parent_path()) mapDirs.push_back(t.root.parent_path());
        }
        std::sort(mapDirs.begin(), mapDirs.end());
        mapDirs.erase(std::unique(mapDirs.begin(), mapDirs.end()), mapDirs.end());
        std::atomic<size_t> scenes{0};
        {
            Progress sceneProgress("map scenes", mapDirs.size());
            parallelFor(mapDirs.size(), jobs, [&](size_t i) {
                std::string map = mapDirs[i].filename().string();
                std::filesystem::path bundle = out / "maps" / map / (map + ".scene.bundle");
                try {
                    if (std::filesystem::exists(bundle / "manifest.json")) {
                        ++scenes;
                    } else if (std::optional<canon::MapScene> scene =
                                   loadMapScene(mapDirs[i], listfile.get(), opts.listfileRoot)) {
                        if (!scene->fogs.empty() || !scene->particulateVolumes.empty()) {
                            writers::writeMapSceneBundle(*scene, bundle, "husk dev");
                            ++scenes;
                        }
                    }
                } catch (...) {
                    errors.add("map-scene", mapDirs[i].string(), exceptionMessage(std::current_exception()));
                }
                sceneProgress.tick();
            });
        }

        std::cout << "done: " << tiles.size() << " tiles, " << scenes << " map scenes, " << exportedModels.size()
                  << " model bundles, "
                  << errors.count() << " errors this run (" << (out / "errors.log").string() << ")" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "husk: export-world: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

}  // namespace husk::commands
