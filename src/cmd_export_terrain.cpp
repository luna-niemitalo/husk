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
#include "db2table.hpp"
#include "listfile_cache.hpp"
#include "listfile_index.hpp"
#include "writers/bundle_common.hpp"
#include "writers/terrain_bundle_writer.hpp"

// `export-terrain` / `export-world` -- WIP: ADT tiles (root + _obj0 + _tex0 +
// the map's WDT) to canonical terrain bundles. All file and DB2 access
// happens here; adtinput::buildCanonTerrain stays pure. See
// TODO/WORLD/ADT_EXPORT_FINDINGS.md.
namespace husk::commands {

void addExportTerrainOptions(CLI::App& app, ExportTerrainOptions& opts) {
    app.add_option("input", opts.input, "root .adt tile (<map>_<x>_<y>.adt)")->required();
    app.add_option("output", opts.output, "bundle directory to write")->required();
    app.add_option("--obj", opts.objArg, "placements: 'auto' (sibling <stem>_obj0.adt), 'none', or a path")
        ->default_val("auto");
    app.add_option("--tex", opts.texArg, "texture layers: 'auto' (sibling <stem>_tex0.adt), 'none', or a path")
        ->default_val("auto");
    app.add_option("--wdt", opts.wdtArg, "map WDT: 'auto' (<map>.wdt beside the tile) or a path")->default_val("auto");
    app.add_option("--listfile", opts.listfile, "community listfile CSV, to resolve texture/model FileDataIDs");
    app.add_option("--listfile-root", opts.listfileRoot, "directory the listfile's paths are relative to");
    app.add_option("--db2-dir", opts.db2Dir, "DB2 directory, for GroundEffectTexture/GroundEffectDoodad");
    app.add_option("--dbd-dir", opts.dbdDir, "WoWDBDefs definitions directory");
    app.add_option("--models-dir", opts.modelsDir,
                   "set placement/ground-effect uris to <dir>/<fdid>.canon.bundle where that bundle exists");
    app.add_flag("--list-models", opts.listModels,
                 "print '<fdid>\\t<listfile path>' for every model the tile references, then exit");
}

void addExportWorldOptions(CLI::App& app, ExportWorldOptions& opts) {
    app.add_option("maps-root", opts.mapsRoot, "directory holding one subdirectory per map (world/maps)")->required();
    app.add_option("output", opts.output, "scene directory to write (maps/, models/, textures/, errors.log)")
        ->required();
    app.add_option("--map", opts.maps, "only these map directories (repeatable); default every map");
    app.add_option("--listfile", opts.listfile, "community listfile CSV")->required();
    app.add_option("--listfile-root", opts.listfileRoot, "directory the listfile's paths are relative to")->required();
    app.add_option("--db2-dir", opts.db2Dir, "DB2 directory, for GroundEffectTexture/GroundEffectDoodad");
    app.add_option("--dbd-dir", opts.dbdDir, "WoWDBDefs definitions directory");
    app.add_option("--jobs", opts.jobs, "worker threads (default: hardware threads)");
    app.add_flag("--skip-models", opts.skipModels, "don't export model bundles; placements stay identity-only");
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

std::optional<std::filesystem::path> resolveSibling(const std::filesystem::path& root, const std::string& arg,
                                                    const std::string& suffix, bool quiet = false) {
    if (arg == "none") return std::nullopt;
    if (arg != "auto") return std::filesystem::path(arg);
    std::filesystem::path sibling = root.parent_path() / (root.stem().string() + suffix);
    if (std::filesystem::exists(sibling)) return sibling;
    if (!quiet) std::cerr << "note: no " << sibling.filename().string() << " beside the tile; skipping it\n";
    return std::nullopt;
}

// FDID -> real BLP on disk -> DDS-housed source blocks, PNG when the BLP's
// encoding can't be rehoused (blp::extractRawPayload declines palettized).
canon::TextureRef resolveTexture(uint32_t fdid, const ListfileIndex& listfile, const std::filesystem::path& root) {
    canon::TextureRef ref;
    std::optional<std::string_view> listed = listfile.lookup(fdid);
    if (!listed) {
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.unresolvedReason = "FileDataID " + std::to_string(fdid) + " not in listfile";
        return ref;
    }
    std::string relative(*listed);
    std::filesystem::path path = root / relative;
    if (!std::filesystem::exists(path)) {
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.unresolvedReason = "listfile path not extracted locally: " + relative;
        return ref;
    }
    std::vector<uint8_t> bytes = adt::readFile(path);
    ref.state = canon::TextureRef::State::Resolved;
    ref.resolved = canon::Ref{canon::FileDataId{fdid}, std::filesystem::path(relative).stem().string(),
                              canon::NameSource::Listfile};
    try {
        blp::RawPayload raw = blp::extractRawPayload(bytes);
        canon::TextureRef::Payload payload;
        payload.bytes = blp::encodeDds(raw);
        switch (raw.encoding) {
            case blp::RawEncoding::Bc1: payload.encoding = canon::TextureEncoding::Bc1; break;
            case blp::RawEncoding::Bc2: payload.encoding = canon::TextureEncoding::Bc2; break;
            case blp::RawEncoding::Bc3: payload.encoding = canon::TextureEncoding::Bc3; break;
            case blp::RawEncoding::Bgra: payload.encoding = canon::TextureEncoding::Bgra; break;
        }
        ref.rawPayload = std::move(payload);
    } catch (const blp::ParseError&) {
        ref.payload = canon::TextureRef::Payload{blp::encodePng(blp::decode(bytes)), canon::TextureEncoding::Png};
    }
    return ref;
}

std::vector<uint32_t> textureIds(const adt::TexFile& tex) {
    std::vector<uint32_t> out = tex.diffuseIds;
    for (uint32_t fdid : tex.heightIds) {
        if (fdid != 0) out.push_back(fdid);
    }
    return out;
}

uint32_t valueOr(const std::optional<uint32_t>& v, const char* what, uint32_t row) {
    if (!v) throw std::runtime_error(std::string(what) + ": expected a value in row " + std::to_string(row) + ", got none");
    return *v;
}

// Every GroundEffectTexture row (density + 4 weighted doodad slots) joined
// to GroundEffectDoodad (model FileDataID). The table is small; the tile's
// layers decide which rows reach the manifest.
adtinput::GroundEffectDefinitions loadGroundEffects(const std::string& db2Dir, const std::string& dbdDir) {
    std::ostringstream err;
    std::string texturePath = (std::filesystem::path(db2Dir) / "groundeffecttexture.db2").string();
    std::string doodadPath = (std::filesystem::path(db2Dir) / "groundeffectdoodad.db2").string();
    auto textures = db2table::readNamedColumns(texturePath, dbdDir, {"ID", "Density"}, err);
    auto textureArrays = db2table::readNamedArrayColumns(texturePath, dbdDir, {"DoodadID", "DoodadWeight"}, err);
    auto doodads = db2table::readNamedColumns(doodadPath, dbdDir, {"ID", "ModelFileID", "Flags"}, err);
    if (!textures || !textureArrays || !doodads || textures->size() != textureArrays->size()) {
        throw std::runtime_error("ground effect DB2s unreadable: " + err.str());
    }
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> doodadById;
    for (uint32_t r = 0; r < doodads->size(); ++r) {
        const auto& row = (*doodads)[r];
        doodadById[valueOr(row[0], "GroundEffectDoodad.ID", r)] = {row[1].value_or(0), row[2].value_or(0)};
    }

    adtinput::GroundEffectDefinitions out;
    for (uint32_t r = 0; r < textures->size(); ++r) {
        uint32_t id = valueOr((*textures)[r][0], "GroundEffectTexture.ID", r);
        canon::GroundEffect effect;
        effect.ref = canon::Ref{canon::Db2Row{"GroundEffectTexture", id}, "", canon::NameSource::None};
        effect.density = (*textures)[r][1];
        const auto& ids = (*textureArrays)[r][0];
        const auto& weights = (*textureArrays)[r][1];
        if (!ids || !weights || ids->size() != weights->size()) {
            throw std::runtime_error("GroundEffectTexture row " + std::to_string(id) +
                                     ": expected DoodadID and DoodadWeight arrays of equal length");
        }
        for (size_t k = 0; k < ids->size(); ++k) {
            uint32_t doodadId = (*ids)[k];
            if (doodadId == 0) continue;
            canon::GroundEffectDoodad d;
            d.doodad = canon::Ref{canon::Db2Row{"GroundEffectDoodad", doodadId}, "", canon::NameSource::None};
            d.weight = (*weights)[k];
            auto found = doodadById.find(doodadId);
            if (found != doodadById.end()) {
                d.model = canon::Ref{canon::FileDataId{found->second.first}, "", canon::NameSource::None};
                d.flags = found->second.second;
            }
            effect.doodads.push_back(std::move(d));
        }
        out.emplace(id, std::move(effect));
    }
    if (!err.str().empty()) std::cerr << err.str();
    return out;
}

std::set<uint32_t> referencedModels(const canon::Terrain& terrain) {
    std::set<uint32_t> out;
    for (const canon::Placement& p : terrain.placements) {
        if (p.kind != canon::Placement::Kind::Model) continue;
        out.insert(std::get<canon::FileDataId>(p.asset.id).value);
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
    CLI::App app{"husk export-terrain -- WIP: one ADT tile to a canonical terrain bundle", "husk export-terrain"};
    ExportTerrainOptions opts;
    addExportTerrainOptions(app, opts);
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;

    try {
        std::filesystem::path rootPath(opts.input);
        TileName tile = parseTileName(rootPath);
        adt::RootFile root = adt::parseRoot(adt::readFile(rootPath));
        std::optional<adt::ObjFile> obj;
        if (auto p = resolveSibling(rootPath, opts.objArg, "_obj0.adt")) obj = adt::parseObj(adt::readFile(*p));
        std::optional<adt::TexFile> tex;
        if (auto p = resolveSibling(rootPath, opts.texArg, "_tex0.adt")) tex = adt::parseTex(adt::readFile(*p));
        std::optional<uint32_t> wdtFlags;
        if (tex) {
            std::filesystem::path wdtPath =
                opts.wdtArg == "auto" ? rootPath.parent_path() / (tile.map + ".wdt") : std::filesystem::path(opts.wdtArg);
            wdtFlags = adt::parseWdtFlags(adt::readFile(wdtPath));
        }

        std::unique_ptr<ListfileIndex> listfile;
        if (!opts.listfile.empty()) listfile = loadListfileCached(opts.listfile);

        adtinput::TextureResolutions textures;
        adtinput::GroundEffectDefinitions groundEffects;
        if (tex) {
            if (listfile) {
                for (uint32_t fdid : textureIds(*tex)) textures.emplace(fdid, resolveTexture(fdid, *listfile, opts.listfileRoot));
            }
            if (!opts.db2Dir.empty() && !opts.dbdDir.empty()) groundEffects = loadGroundEffects(opts.db2Dir, opts.dbdDir);
        }

        adtinput::TileSources sources{root, obj, tex, wdtFlags, tile.map, tile.x, tile.y};
        canon::Terrain terrain = adtinput::buildCanonTerrain(sources, textures, groundEffects);
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
//   1. collect model FileDataIDs (every tile's _obj0 MDDF + every
//      GroundEffectDoodad model),
//   2. export each model once to models/<fdid>.canon.bundle via
//      exportOneModel --bundle-only (one shared listfile, no DB2 enrichment),
//   3. export each tile to maps/<map>/<tile>.bundle, terrain textures shared
//      under textures/, uris only to outputs that exist.
// Failures are logged to errors.log and never stop the run.
int exportWorld(int argc, char** args) {
    CLI::App app{"husk export-world -- WIP: every ADT tile of every map to a canonical scene", "husk export-world"};
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
        adtinput::GroundEffectDefinitions groundEffects;
        if (!opts.db2Dir.empty() && !opts.dbdDir.empty()) groundEffects = loadGroundEffects(opts.db2Dir, opts.dbdDir);

        // Pass 1: model references.
        std::set<uint32_t> modelIds;
        for (const auto& [id, effect] : groundEffects) {
            for (const canon::GroundEffectDoodad& d : effect.doodads) {
                if (auto fdid = std::get_if<canon::FileDataId>(&d.model.id)) modelIds.insert(fdid->value);
            }
        }
        std::mutex modelIdsMutex;
        if (!opts.skipModels) {
            Progress progress("collect", tiles.size());
            parallelFor(tiles.size(), jobs, [&](size_t i) {
                try {
                    if (auto p = resolveSibling(tiles[i].root, "auto", "_obj0.adt", true)) {
                        adt::ObjFile obj = adt::parseObj(adt::readFile(*p));
                        std::lock_guard<std::mutex> lock(modelIdsMutex);
                        for (const adt::DoodadPlacement& d : obj.doodads) {
                            if (d.flags & adt::kMddfNameIsFileDataId) modelIds.insert(d.nameId);
                        }
                    }
                } catch (...) {
                    errors.add("collect", tiles[i].root.string(), exceptionMessage(std::current_exception()));
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

        std::unordered_set<uint32_t> exportedModels;
        for (const auto& entry : std::filesystem::directory_iterator(modelsDir)) {
            if (!std::filesystem::exists(entry.path() / "manifest.json")) continue;
            std::string name = entry.path().filename().string();
            exportedModels.insert(static_cast<uint32_t>(std::stoul(name.substr(0, name.find('.')))));
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
                errors.add("wdt", wdt.string(), exceptionMessage(std::current_exception()));
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
                    std::optional<adt::ObjFile> obj;
                    if (auto p = resolveSibling(t.root, "auto", "_obj0.adt", true)) obj = adt::parseObj(adt::readFile(*p));
                    std::optional<adt::TexFile> tex;
                    const std::optional<uint32_t>& flags = wdtFlags.at(t.map);
                    if (flags) {
                        if (auto p = resolveSibling(t.root, "auto", "_tex0.adt", true)) tex = adt::parseTex(adt::readFile(*p));
                    }

                    std::filesystem::path relToOut = std::filesystem::relative(out, bundle);
                    adtinput::TextureResolutions textures;
                    writers::AssetUris textureUris;
                    if (tex) {
                        for (uint32_t fdid : textureIds(*tex)) {
                            auto [ref, filename] = sharedTextures.get(fdid);
                            textures.emplace(fdid, ref);
                            if (!filename.empty()) textureUris.emplace(fdid, (relToOut / "textures" / filename).generic_string());
                        }
                    }
                    adtinput::TileSources sources{root, obj, tex, flags, name.map, name.x, name.y};
                    canon::Terrain terrain = adtinput::buildCanonTerrain(sources, textures, groundEffects);
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
