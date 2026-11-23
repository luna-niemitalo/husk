#include <algorithm>
#include <iostream>
#include <optional>
#include <sstream>

#include <CLI/CLI.hpp>

#include "appearance_string.hpp"
#include "canon_item_builder.hpp"
#include "cli_args.hpp"
#include "commands.hpp"
#include "husk_config.hpp"
#include "itemappearance_db2.hpp"

// `husk appearance-string` -- validates/normalizes a husk-appearance/1
// string (src/appearance_string.hpp's own doc comment covers the grammar
// and why it isn't an addon export format). `gear` entries resolve to real
// equipped-item DB2 data when --db2-dir/--dbd-dir are given
// (src/itemappearance_db2.hpp) -- otherwise they stay opaque (slot,
// ItemModifiedAppearanceID) pairs, same
// "current vs target" honesty as every other still-partial feature here.
// Output carries both real gear cases (canon_item.hpp):
// `texture(type=N)=<fdid>` is case 1 (the item's own standalone geometry's
// texture), `section(N)=<fdid>` is case 2 (a base-character-mesh section
// overlay, N a real CharComponentTextureSections.SectionType-space value).
namespace husk::commands {

void addAppearanceStringOptions(CLI::App& app, AppearanceStringOptions& opts) {
    addConfigOption(app);
    app.add_option("--validate", opts.value, "the husk-appearance/1 string to validate")->required();
    app.add_option("--db2-dir", opts.db2DirArg,
                    "a real, local, already-extracted DB2 directory -- with --dbd-dir, resolves `gear` "
                    "entries to real equipped-item model/texture FileDataIDs instead of leaving them "
                    "opaque. 'none' explicitly opts out even when --config/$HUSK_CONFIG supplies a "
                    "value (DESIGN.md's \"Three-state resolution, not two\")");
    app.add_option("--dbd-dir", opts.dbdDirArg,
                    "a local WoWDBDefs checkout, resolving --db2-dir's real column names -- required "
                    "alongside --db2-dir. 'none' explicitly opts out even when --config/$HUSK_CONFIG "
                    "supplies a value");
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

    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;

    // 'none' opts out of a --config/$HUSK_CONFIG-supplied value for this one
    // invocation -- same convention as export's own --db2-dir/--dbd-dir
    // (DESIGN.md's "Three-state resolution, not two").
    if (opts.db2DirArg == "none") opts.db2DirArg.clear();
    if (opts.dbdDirArg == "none") opts.dbdDirArg.clear();

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
        if (!itemAppearanceData) {
            std::cerr << "husk: appearance-string: couldn't load the item-appearance DB2 chain from '"
                       << opts.db2DirArg << "' -- gear entries left unresolved\n";
            return 0;
        }
        std::optional<husk::modelfiledata::Data> models =
            husk::modelfiledata::load(opts.db2DirArg, opts.dbdDirArg, std::cerr);
        std::optional<husk::texturefiledata::Candidates> textures =
            husk::texturefiledata::loadCandidates(opts.db2DirArg, opts.dbdDirArg, std::cerr);
        std::optional<husk::componentfile::Data> components =
            husk::componentfile::load(opts.db2DirArg, opts.dbdDirArg, std::cerr);
        husk::canon::GearSources sources;
        sources.models = models ? &*models : nullptr;
        sources.textures = textures ? &*textures : nullptr;
        sources.components = components ? &*components : nullptr;
        if (parsed.raceId != 0) {
            sources.wearers.push_back({static_cast<uint32_t>(parsed.raceId), static_cast<uint32_t>(parsed.sexId)});
        }

        auto pickText = [](std::optional<uint32_t> fileDataId, const std::string& unresolved) {
            return fileDataId && *fileDataId != 0 ? std::to_string(*fileDataId) : "(" + unresolved + ")";
        };
        std::vector<husk::canon::Item> items =
            husk::canon::assembleEquippedItems(*itemAppearanceData, sources, parsed.gear, std::cerr);
        for (const husk::appearance::GearEntry& entry : parsed.gear) {
            // assembleEquippedItems already reported this entry's dangling hops.
            std::ostringstream alreadyReported;
            husk::itemappearance::Resolution resolution =
                husk::itemappearance::resolve(*itemAppearanceData, entry.itemModifiedAppearanceId, alreadyReported);
            std::cout << "husk: appearance-string: gear " << entry.slot << ":" << entry.itemModifiedAppearanceId;
            if (!resolution.itemDisplayInfoId) {
                std::cout << " -- unresolved\n";
                continue;
            }
            std::cout << " -> ItemDisplayInfoID=" << *resolution.itemDisplayInfoId;
            auto item = std::find_if(items.begin(), items.end(), [&](const husk::canon::Item& i) {
                return std::get<husk::canon::Db2Row>(i.identity.id).row ==
                       static_cast<uint32_t>(entry.itemModifiedAppearanceId);
            });
            for (const husk::canon::Component& component : item->components) {
                if (const auto* geometry = std::get_if<husk::canon::GeometryComponent>(&component)) {
                    for (const auto& m : geometry->models) {
                        std::cout << " model[" << m.position << "]=" << pickText(m.fileDataId, m.unresolved);
                    }
                    for (const auto& m : geometry->materials) {
                        std::cout << " texture(type=" << m.textureType << ",model=" << m.modelIndex
                                  << ")=" << pickText(m.fileDataId, m.unresolved);
                    }
                    continue;
                }
                for (const auto& s : std::get<husk::canon::SectionOverlayComponent>(component).sections) {
                    std::cout << " section(" << s.componentSection << ")=" << pickText(s.fileDataId, s.unresolved);
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
