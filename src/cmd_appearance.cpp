#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "appearance_string.hpp"
#include "commands.hpp"
#include "husk_config.hpp"
#include "itemappearance_db2.hpp"
#include "modelfiledata_db2.hpp"
#include "texturefiledata_db2.hpp"

// `husk appearance-string` -- validates/normalizes a husk-appearance/1
// string (src/appearance_string.hpp's own doc comment covers the grammar
// and why it isn't an addon export format). `gear` entries resolve to real
// equipped-item DB2 data when --db2-dir/--dbd-dir are given (TODO/
// CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 6, src/itemappearance_db2.hpp) --
// otherwise they stay opaque (slot, ItemModifiedAppearanceID) pairs, same
// "current vs target" honesty as every other still-partial feature here.
namespace husk::commands {

void addAppearanceStringOptions(CLI::App& app, AppearanceStringOptions& opts) {
    app.set_config("--config", husk::defaultConfigPath(),
                    "TOML file of default flag values -- explicit CLI flags always override a "
                    "config value")
        ->envname("HUSK_CONFIG");
    app.add_option("--validate", opts.value, "the husk-appearance/1 string to validate")->required();
    app.add_option("--db2-dir", opts.db2DirArg,
                    "a real, local, already-extracted DB2 directory -- with --dbd-dir, resolves `gear` "
                    "entries to real equipped-item model/texture FileDataIDs instead of leaving them opaque");
    app.add_option("--dbd-dir", opts.dbdDirArg,
                    "a local WoWDBDefs checkout, resolving --db2-dir's real column names -- required "
                    "alongside --db2-dir");
}

int appearanceString(int argc, char** args) {
    AppearanceStringOptions opts;
    CLI::App app{
        "Parses a husk-appearance/1 string, re-serializes it in canonical form (sorted cust/gear "
        "lists), and prints a summary. `gear` entries resolve to real equipped-item model/texture "
        "FileDataIDs when --db2-dir/--dbd-dir are given, otherwise stay opaque (slot, "
        "ItemModifiedAppearanceID) pairs.",
        "husk appearance-string"};
    addAppearanceStringOptions(app, opts);

    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    try {
        husk::appearance::AppearanceString parsed = husk::appearance::parse(opts.value);
        std::cout << "husk: appearance-string: valid\n"
                   << "husk: appearance-string: canonical: " << husk::appearance::serialize(parsed) << "\n"
                   << "husk: appearance-string: race=" << parsed.raceId << " sex=" << parsed.sexId
                   << " cust=" << parsed.customizationChoiceIds.size() << " gear=" << parsed.gear.size() << "\n";

        if (parsed.gear.empty()) return 0;

        if (opts.db2DirArg.empty() || opts.dbdDirArg.empty()) {
            std::cout << "husk: appearance-string: note: gear entries not resolved -- pass --db2-dir "
                          "and --dbd-dir to resolve them to real model/texture FileDataIDs\n";
            return 0;
        }

        std::optional<husk::itemappearance::Data> itemAppearanceData =
            husk::itemappearance::load(opts.db2DirArg, opts.dbdDirArg, std::cerr);
        std::optional<husk::modelfiledata::Data> modelFileData =
            husk::modelfiledata::load(opts.db2DirArg, opts.dbdDirArg, std::cerr);
        std::optional<husk::texturefiledata::Data> textureFileData =
            husk::texturefiledata::load(opts.db2DirArg, opts.dbdDirArg, std::cerr);
        if (!itemAppearanceData) {
            std::cerr << "husk: appearance-string: couldn't load the item-appearance DB2 chain from '"
                       << opts.db2DirArg << "' -- gear entries left unresolved\n";
            return 0;
        }

        for (const husk::appearance::GearEntry& entry : parsed.gear) {
            husk::itemappearance::Resolution resolution =
                husk::itemappearance::resolve(*itemAppearanceData, entry.itemModifiedAppearanceId, std::cerr);
            std::cout << "husk: appearance-string: gear " << entry.slot << ":" << entry.itemModifiedAppearanceId;
            if (!resolution.itemDisplayInfoId) {
                std::cout << " -- unresolved\n";
                continue;
            }
            std::cout << " -> ItemDisplayInfoID=" << *resolution.itemDisplayInfoId;
            if (resolution.modelResourcesId && modelFileData) {
                auto it = modelFileData->find(*resolution.modelResourcesId);
                if (it != modelFileData->end()) {
                    std::cout << " model=";
                    for (size_t i = 0; i < it->second.size(); ++i) {
                        if (i) std::cout << ",";
                        std::cout << it->second[i];
                    }
                }
            }
            if (textureFileData) {
                for (const husk::itemappearance::ModelMatRes& m : resolution.materials) {
                    auto it = textureFileData->find(m.materialResourcesId);
                    if (it != textureFileData->end()) {
                        std::cout << " texture(type=" << m.textureType << ")=" << it->second;
                    }
                }
            }
            std::cout << "\n";
        }
        return 0;
    } catch (const husk::appearance::ParseError& e) {
        std::cerr << "husk: appearance-string: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
