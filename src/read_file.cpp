#include "read_file.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "input_log.hpp"

namespace husk::commands {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw std::runtime_error("error reading '" + path + "': " + std::strerror(errno));
    }
    // Every model-side input passes through here, so export-world's
    // inputs.log sees each one (input_log.hpp).
    inputs::use(path, bytes);
    return bytes;
}

}  // namespace husk::commands
