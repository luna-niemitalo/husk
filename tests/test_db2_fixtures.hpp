// Shared byte-writing helpers and WDC5 layout sizes for tests/test_db2.cpp
// and tests/test_db2_relations.cpp. Anonymous namespace: each including TU
// gets its own private copy (CMakeLists.txt's -Wno-unused-function note).
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

void putU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) {
    std::memcpy(buf.data() + offset, &v, 2);
}
void putU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) {
    std::memcpy(buf.data() + offset, &v, 4);
}
void putU64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) {
    std::memcpy(buf.data() + offset, &v, 8);
}

constexpr size_t kHeaderSize = 204;  // magic(4) + version(4) + schema(128) + 17 header fields (68)
constexpr size_t kSectionHeaderSize = 40;
constexpr size_t kFieldStructureSize = 4;
constexpr size_t kFieldStorageInfoSize = 24;

}  // namespace
