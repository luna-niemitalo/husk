#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "appearance_string.hpp"
#include "commands.hpp"

// `husk appearance-string` -- validates/normalizes a husk-appearance/1
// string (src/appearance_string.hpp's own doc comment covers the grammar
// and why it isn't an addon export format). Deliberately does NOT resolve
// `gear` entries to real textures yet -- tracked as
// TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 6, not started -- this
// command only proves the string round-trips, same "current vs target"
// honesty as every other still-partial feature in this project (per-slot
// appearance data flows through as opaque IDs, not silently dropped or
// claimed-resolved).
namespace husk::commands {

void addAppearanceStringOptions(CLI::App& app, AppearanceStringOptions& opts) {
    app.add_option("--validate", opts.value, "the husk-appearance/1 string to validate")->required();
}

int appearanceString(int argc, char** args) {
    AppearanceStringOptions opts;
    CLI::App app{
        "Parses a husk-appearance/1 string, re-serializes it in canonical form (sorted cust/gear "
        "lists), and prints a summary. `gear` entries are carried through as opaque (slot, "
        "ItemModifiedAppearanceID) pairs only -- husk does not yet resolve them to textures.",
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
        if (!parsed.gear.empty()) {
            std::cout << "husk: appearance-string: note: gear entries are not yet resolved to "
                          "textures -- see this command's --help\n";
        }
        return 0;
    } catch (const husk::appearance::ParseError& e) {
        std::cerr << "husk: appearance-string: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
