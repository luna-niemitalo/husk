#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace husk::commands {

// The whole file. Throws std::runtime_error naming the path and the OS
// reason when it can't be opened or read.
std::vector<uint8_t> readFileBytes(const std::string& path);

}  // namespace husk::commands
