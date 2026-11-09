// Reading back a bundle on disk: the schema-checked manifest and its binary
// slices. Shared by the writer tests (test_bundle_fixtures.hpp) and the CLI
// tests that run `husk export` and inspect what it wrote.
// Anonymous namespace: each including TU gets its own private copy.
#pragma once

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <string>
#include <vector>

#include "bundle_schema.hpp"

namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Every manifest a test reads is also checked against schema/, so the
// writer can't drift from the format's definition unnoticed.
nlohmann::json parseManifest(const std::filesystem::path& bundleDir) {
    std::vector<husk::bundle_schema::Problem> problems = husk::bundle_schema::validateDocument(bundleDir);
    std::string report;
    for (const husk::bundle_schema::Problem& p : problems) report += p.where + ": " + p.message + "\n";
    INFO(report);
    CHECK(problems.empty());
    std::string text = readTextFile(bundleDir / "manifest.json");
    auto parsed = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    REQUIRE(!parsed.is_discarded());
    return parsed;
}

// The components of an f32 BufferSlice, flattened, read from the bundle's
// own bin file.
std::vector<float> readF32Slice(const std::filesystem::path& bundleDir, const nlohmann::json& slice) {
    REQUIRE(slice["component_type"] == "f32");
    size_t byteOffset = slice["byte_offset"].get<size_t>();
    size_t byteLength = slice["byte_length"].get<size_t>();
    std::ifstream in(bundleDir / slice["file"].get<std::string>(), std::ios::binary);
    REQUIRE(in.good());
    in.seekg(static_cast<std::streamoff>(byteOffset));
    std::vector<uint8_t> bytes(byteLength);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byteLength));
    REQUIRE(in.good());
    std::vector<float> values(byteLength / sizeof(float));
    std::memcpy(values.data(), bytes.data(), values.size() * sizeof(float));
    return values;
}

void checkF32Slice(const std::filesystem::path& bundleDir, const nlohmann::json& slice,
                   const std::vector<float>& expected) {
    std::vector<float> actual = readF32Slice(bundleDir, slice);
    REQUIRE(actual.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        INFO("component ", i);
        CHECK(actual[i] == doctest::Approx(expected[i]));
    }
}

// The sequence_index of every clip in resources.rig.animation, sorted.
std::vector<int64_t> clipSequenceIndices(const nlohmann::json& clips) {
    std::vector<int64_t> indices;
    for (const auto& clip : clips) indices.push_back(clip["sequence_index"].get<int64_t>());
    std::sort(indices.begin(), indices.end());
    return indices;
}

}  // namespace
