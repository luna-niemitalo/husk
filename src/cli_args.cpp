#include "cli_args.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
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
    // Every command's --listfile, attached here once rather than at each
    // of its nine registrations.
    if (CLI::Option* listfile = app.get_option_no_throw("--listfile")) {
        listfile->transform(latestListfileInDirectory());
    }
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

CLI::Validator latestListfileInDirectory() {
    // Empty description: the flag tables in README.md are generated from
    // --help, and this adds no user-facing constraint worth a line there.
    return CLI::Validator(
        [](std::string& value) -> std::string {
            std::error_code ec;
            if (!std::filesystem::is_directory(value, ec)) return {};
            std::optional<std::filesystem::path> latest;
            for (const auto& entry : std::filesystem::directory_iterator(value, ec)) {
                if (entry.path().extension() != ".csv") continue;
                if (!latest || entry.path().filename() > latest->filename()) latest = entry.path();
            }
            if (!latest) return "expected a listfile *.csv in directory " + value + ", got none";
            value = latest->string();
            return {};
        },
        "", "latest listfile");
}

}  // namespace husk::commands
