#pragma once

#include <string>

namespace husk {

// Resolves husk's own XDG-style config file path: $XDG_CONFIG_HOME/husk/config.toml,
// falling back to $HOME/.config/husk/config.toml. Returns "" if neither
// XDG_CONFIG_HOME nor HOME is set (CLI11's own set_config() treats an empty default
// path as "no default config" and just skips it -- same "unset is the no-flag state"
// convention every other opt-in sidecar in this project follows).
//
// Existence is deliberately not checked here -- CLI11 itself treats a missing file at
// this path as "no config", not an error (config_required=false at the set_config
// call site). Shared (not duplicated per-command) so every command CLI11 migrates to
// config support later (TODO/CLEANUP_TODO.md) resolves the same path the same way.
std::string defaultConfigPath();

}  // namespace husk
