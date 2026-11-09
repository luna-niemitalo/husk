#include "dir_listing.hpp"

#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace husk::sources {

const std::vector<std::filesystem::directory_entry>& scanDirOrWarn(const std::string& dir, const char* purpose) {
    static const std::vector<std::filesystem::directory_entry> none;
    std::error_code statEc;
    auto st = std::filesystem::status(dir, statEc);
    if (statEc) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': " << statEc.message() << "\n";
        return none;
    }
    if (st.type() == std::filesystem::file_type::not_found) {
        std::error_code linkEc;
        auto lst = std::filesystem::symlink_status(dir, linkEc);
        bool brokenSymlink = !linkEc && lst.type() == std::filesystem::file_type::symlink;
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': "
                  << (brokenSymlink ? "broken symlink (target doesn't exist)" : "no such directory")
                  << "\n";
        return none;
    }
    if (st.type() != std::filesystem::file_type::directory) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': not a directory\n";
        return none;
    }
    // A changed directory gets a new key, never a replaced listing, so a
    // returned reference stays valid; node-based for the same reason.
    std::error_code timeEc;
    std::string key = dir + '\0' + std::to_string(std::filesystem::last_write_time(dir, timeEc).time_since_epoch().count());
    static std::unordered_map<std::string, std::vector<std::filesystem::directory_entry>> listings;
    static std::shared_mutex mutex;
    if (!timeEc) {
        std::shared_lock<std::shared_mutex> lock(mutex);
        auto it = listings.find(key);
        if (it != listings.end()) return it->second;
    }

    std::vector<std::filesystem::directory_entry> entries;
    std::error_code iterEc;
    for (std::filesystem::directory_iterator it(dir, iterEc), end; !iterEc && it != end; it.increment(iterEc)) {
        entries.push_back(*it);
    }
    if (iterEc) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': " << iterEc.message() << "\n";
        return none;
    }
    std::unique_lock<std::shared_mutex> lock(mutex);
    return listings.try_emplace(key, std::move(entries)).first->second;
}

}  // namespace husk::sources
