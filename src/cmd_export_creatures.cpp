#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <CLI/CLI.hpp>

#include "cli_args.hpp"
#include "cmd_world_shared.hpp"
#include "commands.hpp"
#include "corpus_index.hpp"
#include "corpus_store.hpp"
#include "creature_catalog_input.hpp"
#include "creature_db2.hpp"
#include "listfile_cache.hpp"
#include "version.hpp"
#include "writers/creatures_writer.hpp"

namespace fs = std::filesystem;

namespace husk::commands {

namespace {

std::string modelManifest(uint32_t fdid) { return "models/" + world::modelBundleName(fdid) + "/manifest.json"; }

// Each display's preset is what its model document chose: the index row
// naming it. The row's model must be the one the DB2 chain names, or the
// tree was exported from other tables than these.
void applyPresets(canon::CreatureCatalog& catalog, const std::vector<corpus::CreatureDisplayRow>& rows) {
    std::unordered_map<uint32_t, size_t> displayAt;
    for (size_t i = 0; i < catalog.displays.size(); ++i) {
        displayAt.emplace(std::get<canon::Db2Row>(catalog.displays[i].ref.id).row, i);
    }
    for (const corpus::CreatureDisplayRow& row : rows) {
        auto at = displayAt.find(row.display);
        if (at == displayAt.end()) continue;  // a display no creature row names
        canon::CreatureDisplay& display = catalog.displays[at->second];
        std::string expected = display.model ? modelManifest(*display.model) : "no model (the DB2 chain names none)";
        if (row.model != expected) {
            throw std::runtime_error("index/creature_displays.tsv: display " + std::to_string(row.display) +
                                     ": expected " + expected + ", got " + row.model);
        }
        display.preset = row.preset;
    }
}

}  // namespace

void addExportCreaturesOptions(CLI::App& app, ExportCreaturesOptions& opts) {
    addConfigOption(app);
    app.add_option("tree", opts.tree,
                   "an export tree with index/creature_displays.tsv (export-corpus, or export-world then index-tree); "
                   "creatures.json is written there")
        ->required();
    app.add_option("--db2-dir", opts.db2Dir,
                   "DB2 directory: Creature, CreatureType, CreatureFamily, CreatureDisplayInfo, CreatureModelData")
        ->required()
        ->group("Resolution");
    app.add_option("--dbd-dir", opts.dbdDir, "WoWDBDefs definitions directory")->required()->group("Resolution");
    app.add_option("--listfile", opts.listfile, "community listfile CSV: names the model references; without it they carry no name")
        ->group("Resolution");
}

int exportCreatures(int argc, char** args) {
    CLI::App app{"husk export-creatures -- write an export tree's creature catalog (creatures.json)",
                 "husk export-creatures"};
    ExportCreaturesOptions opts;
    addExportCreaturesOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;
    try {
        fs::path tree = opts.tree;
        corpus::stampProducer(tree, std::string("husk ") + husk::version());
        std::ostringstream err;
        std::optional<creaturedb::Data> data = creaturedb::load(opts.db2Dir, opts.dbdDir, err);
        if (!data) throw std::runtime_error("expected readable creature tables in " + opts.db2Dir + ", got:\n" + err.str());
        std::cerr << err.str();

        canon::CreatureCatalog catalog = creaturecatalog::assemble(*data);
        applyPresets(catalog, corpus::readCreatureDisplays(tree));
        std::unique_ptr<ListfileIndex> listfile;
        if (!opts.listfile.empty() && opts.listfile != "none") {
            listfile = loadListfileCached(opts.listfile);
            for (canon::CreatureModel& model : catalog.models) world::nameFromListfile(model.ref, *listfile);
        }
        writers::AssetUris uris;
        size_t withPreset = 0;
        for (const canon::CreatureDisplay& display : catalog.displays) {
            if (display.preset) ++withPreset;
            if (display.model && fs::exists(tree / modelManifest(*display.model))) {
                uris.emplace(*display.model, modelManifest(*display.model));
            }
        }

        writers::Provenance provenance{"db2", {}};
        provenance.sources.push_back({"creature db2", "creatures: names, titles, types, families, displays (Creature)", true, ""});
        provenance.sources.push_back({"creature type db2", "type names (CreatureType)", true, ""});
        provenance.sources.push_back({"creature family db2", "family names (CreatureFamily)", true, ""});
        provenance.sources.push_back({"creature display db2", "each display's model and scales (CreatureDisplayInfo, "
                                      "CreatureModelData)", true, ""});
        provenance.sources.push_back({"index/creature_displays.tsv", "each display's preset in its model document", true, ""});
        provenance.sources.push_back({"listfile", "model reference names", listfile != nullptr, listfile ? "" : "no --listfile"});
        corpus::writeTextAtomic(tree / "creatures.json", writers::creatureCatalogDocument(catalog, provenance, uris));

        std::cout << "creatures.json: " << catalog.creatures.size() << " creatures, " << catalog.displays.size()
                  << " displays (" << withPreset << " with a preset), " << uris.size()
                  << " models in the tree" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: export-creatures: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
