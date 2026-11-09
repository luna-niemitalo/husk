#include "listfile.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "input_log.hpp"

namespace husk {

// One pass over an in-memory buffer (fread + memchr/digit scanning), not
// getline()+stoul: a real listfile is ~2.2M lines, re-parsed on every
// uncached invocation, and the line-by-line form cost ~1.1s per call
// (DESIGN.md "Listfile cache"). Every entry is kept: filtering to known-
// relevant FileDataIDs would silently drop coverage for anything not yet
// known.
//
// IN, critical: the file itself (an unopenable --listfile throws).
// IN, non-critical per line: a line without a "<digits>;<path>" shape, a
// FileDataID of 0 or over 10 digits, or an empty path is skipped
// (DESIGN.md: "a malformed line is still silently skipped").
std::unordered_map<uint32_t, std::string> loadListfile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("husk: couldn't open --listfile '" + path + "': " + std::strerror(errno));
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<char> buf(size > 0 ? static_cast<size_t>(size) : 0);
    size_t nread = buf.empty() ? 0 : std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    buf.resize(nread);
    inputs::use(path, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(buf.data()), buf.size()));

    std::unordered_map<uint32_t, std::string> result;
    result.reserve(2'300'000);  // real listfile scale -- avoids dozens of incremental rehashes

    const char* p = buf.data();
    const char* end = p + buf.size();
    while (p < end) {
        const char* lineStart = p;
        const auto* nl = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
        const char* lineEnd = nl ? nl : end;
        p = nl ? nl + 1 : end;

        const char* le = lineEnd;
        if (le > lineStart && le[-1] == '\r') --le;  // tolerate CRLF-saved copies

        const auto* sep = static_cast<const char*>(memchr(lineStart, ';', static_cast<size_t>(le - lineStart)));
        if (sep == nullptr || sep == lineStart) continue;

        uint32_t fdid = 0;
        bool validDigits = (sep - lineStart) <= 10;  // real FileDataIDs never need more; guards uint32_t overflow
        for (const char* d = lineStart; validDigits && d < sep; ++d) {
            if (*d < '0' || *d > '9') { validDigits = false; break; }
            fdid = fdid * 10 + static_cast<uint32_t>(*d - '0');
        }
        if (!validDigits || fdid == 0) continue;

        const char* pathStart = sep + 1;
        if (pathStart >= le) continue;  // empty path
        result.emplace(fdid, std::string(pathStart, static_cast<size_t>(le - pathStart)));
    }
    return result;
}

}  // namespace husk
