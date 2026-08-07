// Shared byte-builder helpers for tests/test_dump*.cpp -- factored out here
// because tempPath/writeFile/putU32/putU16/putF32/putU32At/putU16At/putTag/
// minimalMd20/wrapChunked/countOccurrences are each used by 3+ of the split
// files (see FILE_SPLIT_TODO.md Item 5). Anonymous namespace: each including
// TU gets its own private copy, same as when these lived inline in the
// pre-split tests/test_dump.cpp.
#pragma once

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path tempPath(const std::string& name) {
    return fs::temp_directory_path() / ("husk-dump-test-" + name);
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 24));
}

void putF32(std::vector<uint8_t>& b, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    putU32(b, bits);
}

void putU16At(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    if (b.size() < off + 2) b.resize(off + 2, 0);
    b[off] = static_cast<uint8_t>(v);
    b[off + 1] = static_cast<uint8_t>(v >> 8);
}

void putU32At(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    if (b.size() < off + 4) b.resize(off + 4, 0);
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(v >> (8 * i));
}

void putTag(std::vector<uint8_t>& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}

// Non-overlapping substring count -- used by several regression tests where
// "does the key appear the right *number* of times" is the actual assertion,
// not just "does it appear at all."
size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// Same minimal fully-zeroed MD20 blob as test_cli.cpp's minimalMd20 (through
// particleEmitters, 0x130 bytes) -- duplicated locally rather than shared,
// since these test files exercise a different subcommand and don't
// otherwise need a shared fixture header with test_cli.cpp.
std::vector<uint8_t> minimalMd20() {
    std::vector<uint8_t> b;
    putTag(b, "MD20");
    putU32(b, 274);  // version
    for (int i = 0; i < 74; ++i) putU32(b, 0);
    REQUIRE(b.size() == 0x130);
    return b;
}

std::vector<uint8_t> wrapChunked(const std::vector<uint8_t>& md20,
                                  const std::vector<std::pair<std::string, std::vector<uint8_t>>>& extra) {
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    for (const auto& [tag, payload] : extra) {
        putTag(file, tag.c_str());
        putU32(file, static_cast<uint32_t>(payload.size()));
        file.insert(file.end(), payload.begin(), payload.end());
    }
    return file;
}

}  // namespace
