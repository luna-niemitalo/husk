#!/usr/bin/env nu
# Launches playwright-mcp with the current flake's browser bundle instead of
# a hardcoded /nix/store/<hash>-playwright-browsers path -- that hash
# changes on every flake.lock/nixpkgs bump, so a registered `claude mcp add`
# command with a literal path silently rots (real incident: 2026-08-22,
# husk-ed session, registered path no longer existed at all). Resolving via
# `direnv exec` at launch time instead means this always matches whatever
# the dev shell itself would use, no re-registration needed after a rebuild.
#
# Registered once via:
#   claude mcp add playwright /home/luna/dev/husk/tools/playwright_mcp_launch.nu
# (no -e/-- args needed -- this script resolves everything itself.)

def main [] {
    # direnv exec's own shellHook prints a banner to stdout on every
    # invocation -- fine to swallow here (this call is only for path
    # resolution), but fatal if it ever reached the actual MCP process:
    # playwright-mcp speaks JSON-RPC over stdio, and banner noise ahead of
    # the first response would corrupt that stream. So resolve paths in
    # this throwaway subshell, then `exec` the real binary directly
    # (bypassing direnv/the shellHook entirely) for the actual server run.
    let env_lines = (direnv exec /home/luna/dev/husk env | lines)
    let browsers_path = (
        $env_lines
        | where { |l| $l | str starts-with "PLAYWRIGHT_BROWSERS_PATH=" }
        | first
        | str replace "PLAYWRIGHT_BROWSERS_PATH=" ""
    )
    let chrome = (
        glob $"($browsers_path)/chromium-*/chrome-linux64/chrome"
        | first
    )
    let mcp_bin = (
        $env_lines
        | where { |l| $l | str starts-with "PATH=" }
        | first
        | str replace "PATH=" ""
        | split row ":"
        | each { |p| $"($p)/playwright-mcp" }
        | where { |p| $p | path exists }
        | first
    )
    let profile_dir = "/media/luna/work/cache/husk/playwright_profile"
    mkdir $profile_dir

    exec $mcp_bin $"--executable-path=($chrome)" $"--user-data-dir=($profile_dir)" --no-sandbox
}
