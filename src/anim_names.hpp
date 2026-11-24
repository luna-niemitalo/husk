#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

// Community animation names (AnimationData id -> "Stand", "Walk", ...) from
// a local wow.export checkout (github.com/Kruithne/wow.export), read in
// place and never copied into this repo. 12.x AnimationData has no Name
// column (layout BBF66A3C), so this is the only name source there; a client
// Name (animationdata_db2.hpp, pre-12 layouts) wins over it.
namespace husk::animnames {

// The file inside the checkout, relative to its root. wow.export took the
// list from wow.tools.local's wwwroot/js/anims.js; both are index-based.
inline constexpr std::string_view kFile = "src/js/3D/AnimMapper.js";

struct Table {
    // AnimationData id -> name. An empty entry in the list is absent here.
    std::unordered_map<uint32_t, std::string> names;
    // Where the table came from, for provenance: repo, file, commit.
    std::string detail;
};

// IN, non-critical (decoration only; a clip's identity is its id): the text
// of AnimMapper.js. The list is `const ANIM_NAMES = [` .. `];` with one
// double-quoted name per line, its line position being the id. Throws
// std::runtime_error naming the line, expected and actual on anything else.
std::unordered_map<uint32_t, std::string> parseAnimMapper(std::string_view text, const std::string& fileForErrors);

// The checkout's HEAD commit (40 hex digits), or empty when .git doesn't
// say (no .git dir, a ref not found loose or packed).
std::string checkoutCommit(const std::filesystem::path& checkout);

// Reads `<checkout>/src/js/3D/AnimMapper.js`. Throws when it is missing or
// malformed: the caller asked for this source explicitly.
Table load(const std::filesystem::path& checkout);

}  // namespace husk::animnames
