#include "cli_args.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "husk_config.hpp"

namespace husk::commands {

std::optional<int> parseCommandArgs(CLI::App& app, int argc, char** args) {
    // App::parse(vector<string>&) takes tokens back to front (its (argc,
    // argv) sibling reverses argv itself), so `args` is reversed, or every
    // flag's value binds to the wrong neighbour. Not app.parse(argc, args):
    // that overload skips argv[0] as the program name, and here argv[0] is
    // already the first real argument.
    std::vector<std::string> argVec(args, args + argc);
    std::reverse(argVec.begin(), argVec.end());
    try {
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    return std::nullopt;
}

// CLI11 maps config keys onto the app's own add_option registrations, so
// each flag's validator and the flag > config > default precedence hold
// with no second parser. The file: --config, else $HUSK_CONFIG, else the
// XDG default; a missing file is no config, not an error.
void addConfigOption(CLI::App& app) {
    app.set_config("--config", husk::defaultConfigPath(),
                   "TOML file of default flag values (see README.md's config-file section) -- explicit CLI "
                   "flags always override a config value")
        ->envname("HUSK_CONFIG")
        ->group("Diagnostics");
}

}  // namespace husk::commands
