#include "husk_config.hpp"

#include <cstdlib>

namespace husk {

std::string defaultConfigPath() {
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfigHome && *xdgConfigHome) {
        return std::string(xdgConfigHome) + "/husk/config.toml";
    }
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return "";
    }
    return std::string(home) + "/.config/husk/config.toml";
}

}  // namespace husk
