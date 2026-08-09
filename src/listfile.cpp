#include "listfile.hpp"

#include <fstream>
#include <stdexcept>

namespace husk {

std::unordered_map<uint32_t, std::string> loadListfile(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("husk: couldn't open --listfile '" + path + "'");
    }
    std::unordered_map<uint32_t, std::string> result;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF-saved copies
        auto sep = line.find(';');
        if (sep == std::string::npos || sep == 0) continue;
        uint32_t fdid = 0;
        try {
            size_t consumed = 0;
            unsigned long parsed = std::stoul(line.substr(0, sep), &consumed);
            if (consumed != sep) continue;  // trailing junk before ';' -- not a clean numeric ID
            fdid = static_cast<uint32_t>(parsed);
        } catch (const std::exception&) {
            continue;
        }
        if (fdid == 0) continue;
        std::string relPath = line.substr(sep + 1);
        if (relPath.empty()) continue;
        result.emplace(fdid, std::move(relPath));
    }
    return result;
}

}  // namespace husk
