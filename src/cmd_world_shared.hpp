#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "adt.hpp"
#include "adt_canon_input.hpp"
#include "canon_map.hpp"
#include "canon_model.hpp"
#include "creature_display_db2.hpp"
#include "light_db2.hpp"
#include "liquid_db2.hpp"
#include "listfile_index.hpp"
#include "writers/bundle_common.hpp"

// What export-terrain, export-wmo, export-map and export-world share:
// reading tiles, WMOs and maps and the DB2 tables they draw on into canon::,
// and naming what they wrote. Every file, listfile and DB2 access of those
// commands happens here or in them; adtinput::buildCanonTerrain stays pure.
namespace husk::commands::world {

struct TileName {
    std::string map;
    uint32_t x = 0;
    uint32_t y = 0;
};

TileName parseTileName(const std::filesystem::path& path);

// 'none' on a per-machine flag means "as if never set", beating a config value.
std::string unlessNone(const std::string& value);

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

canon::TextureRef resolveTexture(uint32_t fdid, const ListfileIndex& listfile, const std::string& listfileRoot);

adtinput::GroundEffectDefinitions loadGroundEffects(const std::string& db2Dir, const std::string& dbdDir,
                                                    std::string& note);
std::optional<lightdb::Data> loadLights(const std::string& db2Dir, const std::string& dbdDir, std::string& note);
std::optional<creaturedisplay::Data> loadCreatures(const std::string& db2Dir, const std::string& dbdDir,
                                                   std::string& note);
std::optional<liquiddb::Data> loadLiquids(const std::string& db2Dir, const std::string& dbdDir, std::string& note);

std::set<uint32_t> referencedModels(const canon::Terrain& terrain);
std::set<uint32_t> referencedAssets(const canon::Terrain& terrain);
void nameModelRefs(canon::Terrain& terrain, const ListfileIndex& listfile);
std::string modelBundleName(uint32_t fdid);

canon::Model loadWmoModel(const std::filesystem::path& rootPath, std::optional<uint32_t> fileDataId,
                          const ListfileIndex* listfile, const std::string& listfileRoot, const liquiddb::Data* liquids,
                          const std::function<void(const std::string&)>& warn);
std::set<uint32_t> placedAssets(const canon::Model& model);
// A placement can select a doodad set its WMO doesn't have (azeroth_39_16,
// placement 53492465: set 8 of a WMO with sets 0..7). What the client shows
// then is not known; the selection keeps the sets that exist and each
// dropped one is reported through `warn`. `setCounts`: doodad sets per WMO
// FileDataID; a WMO not in it is left alone.
void dropMissingDoodadSets(canon::PlacementSet& set, const std::unordered_map<uint32_t, size_t>& setCounts,
                           const std::function<void(const std::string&)>& warn);

writers::Provenance wmoProvenance(const ListfileIndex* listfile, const std::string& listfilePath, bool haveLiquids);
writers::Provenance terrainProvenance(bool haveObj, bool haveTex, bool haveWdt, const ListfileIndex* listfile,
                                      const std::string& listfilePath, bool haveGroundEffects, bool haveLiquids);
writers::Provenance worldProvenance(const std::filesystem::path& mapDir, const ListfileIndex* listfile,
                                    const std::string& listfilePath, bool haveLights);

std::unordered_map<std::string, uint32_t> tileFileDataIds(const ListfileIndex& listfile, const std::string& listfileRoot,
                                                          const std::vector<std::filesystem::path>& roots);
std::optional<uint32_t> tileFileDataId(const std::unordered_map<std::string, uint32_t>& ids,
                                       const std::string& listfileRoot, const std::filesystem::path& root);

bool placesModels(const std::vector<canon::PlacementSet>& sets);
void printPlaceholderEffects(const std::vector<canon::PlacementSet>& sets, const char* command);

struct WorldTile {
    std::filesystem::path root;
    std::string map;
};

std::vector<WorldTile> findTiles(const std::filesystem::path& mapsRoot, const std::vector<std::string>& onlyMaps);
canon::World loadWorld(const std::filesystem::path& mapDir, const adt::WdtFile& wdt, const std::vector<WorldTile>& tiles,
                       const ListfileIndex* listfile, const std::string& listfileRoot, const lightdb::Data* lights,
                       unsigned jobs);

}  // namespace husk::commands::world
