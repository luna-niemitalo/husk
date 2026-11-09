#include "uuid.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "corpus_store.hpp"

namespace husk {

namespace {

constexpr std::array<uint8_t, 16> kNamespace = {0x6f, 0x0f, 0x3a, 0x52, 0x8d, 0x1c, 0x4b, 0x7e,
                                                0x9a, 0x42, 0x5c, 0x3e, 0x1d, 0x7b, 0x2f, 0x10};

}  // namespace

std::string nameUuid(std::string_view name) {
    std::vector<uint8_t> bytes(kNamespace.begin(), kNamespace.end());
    bytes.insert(bytes.end(), name.begin(), name.end());
    corpus::Md5 digest = corpus::md5Bytes(bytes);
    digest[6] = static_cast<uint8_t>((digest[6] & 0x0f) | 0x30);
    digest[8] = static_cast<uint8_t>((digest[8] & 0x3f) | 0x80);
    std::string text = corpus::hex(digest);
    for (size_t at : {20, 16, 12, 8}) text.insert(at, 1, '-');
    return text;
}

}  // namespace husk
