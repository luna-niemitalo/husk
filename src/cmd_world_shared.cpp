#include "cmd_world_shared.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "blp.hpp"
#include "liquid_canon_input.hpp"
#include "map_scene_input.hpp"
#include "parallel.hpp"
#include "sources/listfile_catalog.hpp"
#include "sources/texture_payload.hpp"
#include "version.hpp"

namespace husk::commands::world {

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

// FileDataID -> the BLP on disk -> its DDS-housed source blocks, or decoded
// PNG when the encoding can't be rehoused (palettized). Terrain has no
// model directory to search, so of RESOURCE_CATALOG.md's texture tiers only
// the listfile and store ones apply.
canon::TextureRef resolveTexture(uint32_t fdid, const ListfileIndex& listfile, const std::string& listfileRoot) {
    canon::TextureRef ref;
    std::optional<std::filesystem::path> path = sources::localFileForFileDataId(listfile, listfileRoot, fdid);
    if (!path) {
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.unresolvedReason = "FileDataID " + std::to_string(fdid) + " has no local file (listfile or _by_fdid)";
        return ref;
    }
    std::vector<uint8_t> bytes = adt::readFile(*path);
    ref.state = canon::TextureRef::State::Resolved;
    ref.resolved = canon::Ref{canon::FileDataId{fdid}, std::to_string(fdid), canon::NameSource::Synthesized};
    if (auto name = sources::contentNameForFileDataId(listfile, fdid)) {
        ref.resolved.name = *name;
        ref.resolved.source = canon::NameSource::Listfile;
    }
    try {
        ref.rawPayload = sources::ddsPayloadFromBlp(bytes);
        return ref;
    } catch (const blp::ParseError&) {
    }
    // One texture husk can't read leaves that texture unresolved, never the tile.
    try {
        ref.payload = sources::pngPayloadFromBlp(bytes);
    } catch (const blp::ParseError& e) {
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.unresolvedReason = path->string() + ": " + e.what();
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

std::optional<lightdb::Data> loadLights(const std::string& db2Dir, const std::string& dbdDir, std::string& note) {
    if (db2Dir.empty() || dbdDir.empty()) {
        note = "zone lights: none (needs --db2-dir and --dbd-dir)";
        return std::nullopt;
    }
    std::ostringstream err;
    std::optional<lightdb::Data> data = lightdb::load(db2Dir, dbdDir, err);
    if (!data) {
        note = "zone lights: none (light DB2s unreadable: " + err.str() + ")";
        return std::nullopt;
    }
    note = "zone lights: " + std::to_string(data->zones.size()) + " zones, " + std::to_string(data->keysByParams.size()) +
           " light params from " + db2Dir;
    return data;
}

std::optional<creaturedisplay::Data> loadCreatures(const std::string& db2Dir, const std::string& dbdDir,
                                                  std::string& note) {
    if (db2Dir.empty() || dbdDir.empty()) {
        note = "creature options: none (needs --db2-dir and --dbd-dir)";
        return std::nullopt;
    }
    std::ostringstream err;
    std::optional<creaturedisplay::Data> data = creaturedisplay::load(db2Dir, dbdDir, err);
    if (!data) {
        note = "creature options: none (creature display DB2s unreadable: " + err.str() + ")";
        return std::nullopt;
    }
    note = "creature options: " + std::to_string(data->modelDataByFile.size()) + " creature models from " + db2Dir;
    return data;
}

std::optional<liquiddb::Data> loadLiquids(const std::string& db2Dir, const std::string& dbdDir, std::string& note) {
    if (db2Dir.empty() || dbdDir.empty()) {
        note = "liquid types: identities only (needs --db2-dir and --dbd-dir)";
        return std::nullopt;
    }
    std::ostringstream err;
    std::optional<liquiddb::Data> data = liquiddb::load(db2Dir, dbdDir, err);
    if (!data) {
        note = "liquid types: identities only (liquid DB2s unreadable: " + err.str() + ")";
        return std::nullopt;
    }
    note = "liquid types: " + std::to_string(data->types.size()) + " types, " + std::to_string(data->objects.size()) +
           " objects from " + db2Dir;
    return data;
}

std::optional<surfacedb::Data> loadSurfaces(const std::string& db2Dir, const std::string& dbdDir, std::string& note) {
    if (db2Dir.empty() || dbdDir.empty()) {
        note = "surface types: TerrainType ids only (needs --db2-dir and --dbd-dir)";
        return std::nullopt;
    }
    std::ostringstream err;
    std::optional<surfacedb::Data> data = surfacedb::load(db2Dir, dbdDir, err);
    if (!data) {
        note = "surface types: TerrainType ids only (surface DB2s unreadable: " + err.str() + ")";
        return std::nullopt;
    }
    note = "surface types: " + std::to_string(data->types.size()) + " TerrainType rows from " + db2Dir;
    return data;
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

// IN, critical when found: a WMO group file, by its GFID FileDataID (listfile
// path or the store), else by the <root stem>_NNN.wmo name beside the root
// (the local extraction's naming). nullopt when neither exists.
std::optional<std::filesystem::path> findWmoGroupFile(const std::filesystem::path& rootPath, const wmo::RootFile& root,
                                                      uint32_t group, const ListfileIndex* listfile,
                                                      const std::string& listfileRoot) {
    uint32_t fdid = wmo::groupFileDataId(root, 0, group);
    if (listfile && fdid != 0) {
        if (auto found = sources::localFileForFileDataId(*listfile, listfileRoot, fdid)) return found;
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
                          const liquiddb::Data* liquids, const surfacedb::Data* surfaces, const std::function<void(const std::string&)>& warn) {
    wmo::RootFile root = wmo::parseRoot(adt::readFile(rootPath));
    wmoinput::WmoInputs inputs;
    inputs.root = &root;
    inputs.liquids = liquids;
    inputs.surfaces = surfaces;
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
        if (liquids) {
            for (uint32_t fdid : liquidinput::textureFileDataIds(wmoinput::liquidTypeRows(inputs), *liquids)) {
                if (!inputs.textures.count(fdid)) inputs.textures.emplace(fdid, resolveTexture(fdid, *listfile, listfileRoot));
            }
        }
        for (const wmo::NewLight& light : root.newLights) {
            if (light.cookieFileDataId != 0 && !inputs.textures.count(light.cookieFileDataId)) {
                inputs.textures.emplace(light.cookieFileDataId, resolveTexture(light.cookieFileDataId, *listfile, listfileRoot));
            }
        }
        std::vector<uint32_t> named = root.doodadFileDataIds;
        for (uint32_t fdid : named) {
            std::optional<std::string_view> listed = fdid != 0 ? listfile->lookup(fdid) : std::nullopt;
            if (listed) inputs.assetNames.emplace(fdid, std::filesystem::path(std::string(*listed)).stem().string());
        }
    }
    for (const canon::SourceRecord& light : wmoinput::droppedLights(inputs)) {
        std::cerr << "husk: note: " << rootPath.filename().string() << ": " << light.table << "[" << light.index
                  << "] left out: a light with non-finite numbers can't be placed\n";
    }
    canon::Model model = wmoinput::buildCanonWmo(inputs);
    model.subject = writers::subjectRef(rootPath, fileDataId);
    return model;
}

writers::Provenance wmoProvenance(const ListfileIndex* listfile, const std::string& listfilePath, bool haveLiquids,
                                  bool haveSurfaces) {
    writers::Provenance out{std::string("husk ") + husk::version(), "wmo", {}};
    out.sources.push_back({"listfile", "group files, textures and doodad names by FileDataID", listfile != nullptr,
                           listfile ? listfilePath : "not given"});
    out.sources.push_back({"liquid db2", "liquid type names, materials and textures", haveLiquids, ""});
    out.sources.push_back({"surface db2", "surface type names, footstep sounds and sprays", haveSurfaces, ""});
    return out;
}

// What a tile was built from. A sidecar marked unavailable was missing, or
// (tex0 without a readable WDT) unusable.
writers::Provenance terrainProvenance(bool haveObj, bool haveTex, bool haveWdt, const ListfileIndex* listfile,
                                      const std::string& listfilePath, bool haveGroundEffects, bool haveLiquids,
                                      bool haveSurfaces) {
    writers::Provenance out{std::string("husk ") + husk::version(), "adt", {}};
    out.sources.push_back({"obj0", "doodad and map-object placements", haveObj, ""});
    out.sources.push_back({"tex0", "texture layers and alpha maps", haveTex, ""});
    out.sources.push_back({"wdt", "MPHD flags: the alpha-map format", haveWdt, ""});
    out.sources.push_back({"listfile", "textures and name-table paths by FileDataID", listfile != nullptr,
                           listfile ? listfilePath : "not given"});
    out.sources.push_back({"groundeffect db2", "ground-cover rules", haveGroundEffects, ""});
    out.sources.push_back({"liquid db2", "liquid type names, materials, textures and objects", haveLiquids, ""});
    out.sources.push_back({"surface db2", "surface type names, footstep sounds and sprays", haveSurfaces, ""});
    return out;
}

// Root ADT FileDataIDs for `roots`, by listfile path under `listfileRoot`:
// one listfile pass for all of them.
std::unordered_map<std::string, uint32_t> tileFileDataIds(const ListfileIndex& listfile,
                                                         const std::string& listfileRoot,
                                                         const std::vector<std::filesystem::path>& roots) {
    std::vector<std::string> gamePaths;
    for (const std::filesystem::path& root : roots) {
        gamePaths.push_back(std::filesystem::relative(root, listfileRoot).generic_string());
    }
    return sources::fileDataIdsForGamePaths(listfile, gamePaths);
}

std::optional<uint32_t> tileFileDataId(const std::unordered_map<std::string, uint32_t>& ids,
                                       const std::string& listfileRoot, const std::filesystem::path& root) {
    auto it = ids.find(std::filesystem::relative(root, listfileRoot).generic_string());
    if (it == ids.end()) return std::nullopt;
    return it->second;
}

bool placesModels(const std::vector<canon::PlacementSet>& sets) {
    return std::any_of(sets.begin(), sets.end(), [](const canon::PlacementSet& set) {
        return std::any_of(set.instances.begin(), set.instances.end(), [](const canon::PlacedInstance& i) { return !i.effect; });
    });
}

// Every effect husk writes so far is a placeholder (REFACTOR/EFFECTS.md):
// the summary counts their instances so they stay visible until classified.
void printPlaceholderEffects(const std::vector<canon::PlacementSet>& sets, const char* command) {
    std::map<std::string, size_t> counts;
    for (const canon::PlacementSet& set : sets) {
        for (const canon::PlacedInstance& i : set.instances) {
            if (i.effect) ++counts[i.asset.name];
        }
    }
    for (const auto& [effect, count] : counts) {
        std::cerr << "husk: " << command << ": warning: " << count << " instances of placeholder effect " << effect << "\n";
    }
}

// A map directory's _fogs.wdt and _mpv.wdt, and `lights`' zones for the
// map, as world placement sets of effect instances; none when the map has
// none of them. A file that exists but doesn't parse fails the export.
std::vector<canon::PlacementSet> loadMapEffects(const std::filesystem::path& mapDir, const ListfileIndex* listfile,
                                                const std::string& listfileRoot, const lightdb::Data* lights) {
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
    mapinput::MapSceneInputs inputs;
    inputs.map = map;
    inputs.fogs = fogs ? &*fogs : nullptr;
    inputs.mpv = mpv ? &*mpv : nullptr;
    inputs.lights = lights;
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
        std::set<uint32_t> models;
        if (fogs) {
            for (const wdt::Fog& f : fogs->fogs) models.insert(f.modelFileDataId ? f.modelFileDataId : mapinput::kFallbackFogModel);
        }
        mapinput::ZoneLightAssets zoneAssets = mapinput::zoneLightAssets(inputs);
        models.insert(zoneAssets.models.begin(), zoneAssets.models.end());
        for (uint32_t model : models) {
            if (std::optional<std::string_view> listed = listfile->lookup(model)) {
                inputs.assetNames.emplace(model, std::filesystem::path(std::string(*listed)).stem().string());
            }
        }
        for (uint32_t texture : zoneAssets.textures) {
            if (!inputs.textures.count(texture)) inputs.textures.emplace(texture, resolveTexture(texture, *listfile, listfileRoot));
        }
    }
    return mapinput::buildMapEffects(inputs);
}

void dropMissingDoodadSets(canon::PlacementSet& set, const std::unordered_map<uint32_t, size_t>& setCounts,
                           const std::function<void(const std::string&)>& warn) {
    for (canon::PlacedInstance& instance : set.instances) {
        auto fdid = std::get_if<canon::FileDataId>(&instance.asset.id);
        if (!fdid || !instance.activeSets) continue;
        auto count = setCounts.find(fdid->value);
        if (count == setCounts.end()) continue;
        std::erase_if(*instance.activeSets, [&](const canon::Ref& ref) {
            uint32_t index = std::get<canon::RecordIndex>(ref.id).value;
            if (index < count->second) return false;
            warn("placement " + std::to_string(instance.id) + " selects doodad set " + std::to_string(index) +
                 " of WMO " + std::to_string(fdid->value) + ", which has " + std::to_string(count->second) +
                 " sets: dropped from the selection");
            return true;
        });
    }
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

// The world-frame box a tile's terrain surface and liquids occupy: chunk
// footprints from their origins, heights from the vertices and liquids.
std::pair<canon::Vec3, canon::Vec3> terrainBounds(const canon::Terrain& terrain) {
    canon::Vec2 corner = canon::terrainTileCorner(terrain.tileX, terrain.tileY);
    constexpr float kFar = std::numeric_limits<float>::max();
    canon::Vec3 lo{kFar, kFar, kFar};
    canon::Vec3 hi{-kFar, -kFar, -kFar};
    auto includeZ = [&](float z) {
        lo.z = std::min(lo.z, z);
        hi.z = std::max(hi.z, z);
    };
    for (const canon::TerrainChunk& c : terrain.chunks) {
        // Rows step -X, columns -Y: a chunk spans one chunk_size below its origin.
        lo.x = std::min(lo.x, corner.x + c.origin.x - canon::kTerrainChunkSize);
        hi.x = std::max(hi.x, corner.x + c.origin.x);
        lo.y = std::min(lo.y, corner.y + c.origin.y - canon::kTerrainChunkSize);
        hi.y = std::max(hi.y, corner.y + c.origin.y);
        for (float h : c.heights) includeZ(h);
    }
    for (const canon::LiquidSurface& l : terrain.liquids) {
        for (float h : l.heights) includeZ(h);
    }
    return {lo, hi};
}

// IN, critical: a map's world document from its parsed WDT, its root ADTs
// and its _fogs.wdt/_mpv.wdt. `tiles` holds only roots the WDT's MAIN lists
// (the store keeps stale tiles the WDT dropped); MAID names each tile's
// FileDataID (the listfile only when the WDT has no MAID). Each root is read
// once more, alone (no sidecars), on `jobs` threads, for its bounds; a root
// that doesn't parse fails the world, a missing volume file leaves the scene
// empty.
canon::World loadWorld(const std::filesystem::path& mapDir, const adt::WdtFile& wdt, const std::vector<WorldTile>& tiles,
                       const ListfileIndex* listfile, const std::string& listfileRoot, const lightdb::Data* lights,
                       unsigned jobs) {
    std::string map = mapDir.filename().string();
    std::filesystem::path wdtPath = mapDir / (map + ".wdt");
    std::vector<std::filesystem::path> named{wdtPath};
    for (const WorldTile& t : tiles) named.push_back(t.root);
    std::unordered_map<std::string, uint32_t> ids;
    if (listfile && !listfileRoot.empty()) ids = tileFileDataIds(*listfile, listfileRoot, named);
    auto rootId = [&](const WorldTile& t, const TileName& name) -> std::optional<uint32_t> {
        if (wdt.tileFileDataIds) {
            uint32_t id = (*wdt.tileFileDataIds)[name.y * 64 + name.x].rootAdt;
            return id != 0 ? std::optional<uint32_t>(id) : std::nullopt;
        }
        return tileFileDataId(ids, listfileRoot, t.root);
    };

    canon::World world;
    world.subject = writers::subjectRef(wdtPath, tileFileDataId(ids, listfileRoot, wdtPath));
    adtinput::GamePathResolutions mapObjectPaths;
    if (listfile && !wdt.mapObjectName.empty()) {
        mapObjectPaths = sources::fileDataIdsForGamePaths(*listfile, {wdt.mapObjectName});
    }
    canon::PlacementSet mapObjects = adtinput::buildGlobalMapObjects(wdt, mapObjectPaths);
    if (listfile) {
        for (canon::PlacedInstance& instance : mapObjects.instances) nameFromListfile(instance.asset, *listfile);
    }
    if (!mapObjects.instances.empty()) world.placements.push_back(std::move(mapObjects));
    const std::optional<adt::ObjFile> noObj;
    const std::optional<adt::TexFile> noTex;
    const adtinput::TextureResolutions noTextures;
    const adtinput::GroundEffectDefinitions noEffects;
    const adtinput::GamePathResolutions noPaths;
    world.cells.resize(tiles.size());
    std::exception_ptr failure;
    std::mutex failureMutex;
    parallelFor(tiles.size(), jobs, [&](size_t i) {
        const WorldTile& t = tiles[i];
        try {
            TileName name = parseTileName(t.root);
            adt::RootFile root = adt::parseRoot(adt::readFile(t.root));
            adtinput::TileSources sources{root, noObj, noTex, std::nullopt, name.map, name.x, name.y};
            canon::Terrain terrain = adtinput::buildCanonTerrain(sources, {noTextures, noEffects, noPaths});
            auto [lo, hi] = terrainBounds(terrain);
            world.cells[i] = {name.x, name.y, writers::subjectRef(t.root, rootId(t, name)), lo, hi};
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(failureMutex);
            if (!failure) failure = std::make_exception_ptr(std::runtime_error(t.root.string() + ": " + e.what()));
        }
    });
    if (failure) std::rethrow_exception(failure);
    for (canon::PlacementSet& set : loadMapEffects(mapDir, listfile, listfileRoot, lights)) world.placements.push_back(std::move(set));
    return world;
}

writers::Provenance worldProvenance(const std::filesystem::path& mapDir, const ListfileIndex* listfile,
                                    const std::string& listfilePath, bool haveLights) {
    std::string map = mapDir.filename().string();
    writers::Provenance out{std::string("husk ") + husk::version(), "wdt", {}};
    out.sources.push_back({"root adt", "the tile grid and each tile's bounds", true, ""});
    out.sources.push_back({"_fogs.wdt", "map-wide fog volumes", std::filesystem::exists(mapDir / (map + "_fogs.wdt")), ""});
    out.sources.push_back({"_mpv.wdt", "map-wide particulate volumes", std::filesystem::exists(mapDir / (map + "_mpv.wdt")),
                           ""});
    out.sources.push_back({"listfile", "tile and map FileDataIDs, particle textures, fog shape models",
                           listfile != nullptr, listfile ? listfilePath : "not given"});
    out.sources.push_back({"light db2", "the map's zone lights and their fog by time of day", haveLights, ""});
    return out;
}

}  // namespace husk::commands::world
