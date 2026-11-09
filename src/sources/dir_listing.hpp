#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace husk::sources {

// `dir`'s entries, or none with a warning on stderr naming `purpose` and
// why: missing, a broken symlink, not a directory, or unreadable. A
// silent empty result would look the same as "no matches". Listings are
// kept per directory and modification time: a corpus export asks for the
// same large directories once per model.
const std::vector<std::filesystem::directory_entry>& scanDirOrWarn(const std::string& dir, const char* purpose);

}  // namespace husk::sources
