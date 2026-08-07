// `husk dump-chunks` tests for dumpEmitters (src/dump_emitters.{hpp,cpp}) --
// ribbon_emitters/particle_emitters output, unconditional and independent
// of any Legion+ chunk tags (they read core M2Ribbon/M2Particle header
// arrays present in every version). Exercises the real compiled binary (see
// run_husk.hpp) -- see tests/test_dump.cpp's own top comment and
// TEST_DESIGN.md#Four-tier-architecture for why.

#include <doctest/doctest.h>
#include <filesystem>

#include "run_husk.hpp"
#include "test_dump_fixtures.hpp"

namespace {
using husk::test::runHusk;
namespace fs = std::filesystem;
}  // namespace

TEST_CASE("husk dump-chunks: a pre-Legion flat MD20 file still gets ribbon_emitters/"
          "particle_emitters (core header arrays, present in every version), just no Legion+ "
          "chunk tags, not an error") {
    auto path = tempPath("flat.m2");
    writeFile(path, minimalMd20());

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("pre-Legion flat MD20") != std::string::npos);
    CHECK(result.output.find("\"ribbon_emitters\"") != std::string::npos);
    CHECK(result.output.find("\"particle_emitters\"") != std::string::npos);
    CHECK(result.output.find("\"TXAC\"") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: a chunked file with none of the dumpable chunks and no ribbon/"
          "particle emitters prints ribbon_emitters/particle_emitters as empty arrays, not a "
          "crash or a stray chunk entry") {
    auto file = wrapChunked(minimalMd20(), {});
    auto path = tempPath("empty.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find('{') != std::string::npos);
    CHECK(result.output.find('}') != std::string::npos);
    // Both keys present (as empty arrays -- the writer pretty-prints, so
    // "[]" isn't adjacent text; absence of any entry's own fields is what
    // proves they're empty) but no chunk tags and no emitter content.
    CHECK(result.output.find("\"ribbon_emitters\": [") != std::string::npos);
    CHECK(result.output.find("\"particle_emitters\": [") != std::string::npos);
    CHECK(result.output.find("\"ribbon_id\"") == std::string::npos);
    CHECK(result.output.find("\"particle_id\"") == std::string::npos);
    CHECK(result.output.find("\"TXAC\"") == std::string::npos);
    CHECK(result.output.find("\"NERF\"") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: a real ribbon_emitters/particle_emitters entry surfaces its static "
          "fields, on a flat pre-Legion file (proving these aren't chunk-gated)") {
    auto md20 = minimalMd20();  // version 274, already Cata+
    // One M2Ribbon (0xB0 bytes) appended right after the header, static
    // fields only -- the M2Track/M2Array regions in between are left
    // zeroed (same convention tests/test_m2.cpp's putRibbon uses), which
    // resolves harmlessly to an empty global-sequence-0 curve rather than
    // throwing (see writeTrackCurve's global_sequence branch).
    size_t ribbonOff = md20.size();
    md20.resize(ribbonOff + 0xB0, 0);
    putU32At(md20, ribbonOff + 0x00, 0xFFFFFFFFu);  // ribbonId
    putU32At(md20, ribbonOff + 0x04, 5);            // boneIndex
    putU32At(md20, /*offset::ribbonEmitters=*/0x120, 1);  // Array::count
    putU32At(md20, 0x124, static_cast<uint32_t>(ribbonOff));  // Array::offset

    // One M2Particle (0x1EC bytes, Cata+ shape), static fields only.
    size_t particleOff = md20.size();
    md20.resize(particleOff + 0x1EC, 0);
    putU32At(md20, particleOff + 0x00, 0xFFFFFFFFu);  // particleId
    putU16At(md20, particleOff + 0x14, 9);            // boneId
    md20[particleOff + 0x28] = 4;                     // blendingType
    md20[particleOff + 0x29] = 1;                     // emitterType
    putU32At(md20, 0x128, 1);  // Array::count
    putU32At(md20, 0x12C, static_cast<uint32_t>(particleOff));  // Array::offset

    auto path = tempPath("real_emitters.m2");
    writeFile(path, md20);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"ribbon_id\": 4294967295") != std::string::npos);
    CHECK(result.output.find("\"bone\": 5") != std::string::npos);
    CHECK(result.output.find("\"particle_id\": 4294967295") != std::string::npos);
    CHECK(result.output.find("\"bone\": 9") != std::string::npos);
    CHECK(result.output.find("\"blending_type\": 4") != std::string::npos);

    fs::remove(path);
}
