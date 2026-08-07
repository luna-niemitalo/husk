// `husk dump-chunks` tests for cmd_dump.cpp's own remaining code (the CLI
// entry point / top-level file-type dispatch) -- everything that doesn't
// belong to one specific dumper module, see tests/test_dump_emitters.cpp
// (dump_emitters.{hpp,cpp}), tests/test_dump_phys.cpp (dump_phys.{hpp,cpp}
// and the .phys dumping path), and tests/test_dump_chunks_misc.cpp
// (dump_chunks_misc.{hpp,cpp}, the ~20 small per-tag dumpers) for those.
// .bone's own BIDA/BOMT dumping is inline in cmd_dump.cpp's own top-level
// dispatch (never extracted to a dump_* module -- it's the .bone-file
// counterpart to the .phys-file/nonexistent-path branches right next to
// it), so it stays here rather than moving. Exercises the real compiled
// binary (see run_husk.hpp) -- the per-chunk parsing logic lives entirely
// inside cmd_dump.cpp's anonymous namespace, so this is the only way to
// reach it (see TEST_DESIGN.md#Four-tier-architecture).

#include <doctest/doctest.h>
#include <filesystem>

#include "run_husk.hpp"
#include "test_dump_fixtures.hpp"

namespace {
using husk::test::runHusk;
namespace fs = std::filesystem;
}  // namespace

TEST_CASE("husk dump-chunks: a .bone file (no MD20/MD21 magic) dumps its BIDA/BOMT "
          "bone-correction pairs instead of failing as 'bad magic'") {
    std::vector<uint8_t> file;
    putU32(file, 1);  // leading version field, see src/bone.hpp
    std::vector<uint8_t> bida;
    uint16_t boneIndex = 58;
    bida.push_back(static_cast<uint8_t>(boneIndex));
    bida.push_back(static_cast<uint8_t>(boneIndex >> 8));
    std::vector<uint8_t> bomt;
    float row[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.001f, 0, 0, 1};
    for (float f : row) putF32(bomt, f);
    putTag(file, "BIDA");
    putU32(file, static_cast<uint32_t>(bida.size()));
    file.insert(file.end(), bida.begin(), bida.end());
    putTag(file, "BOMT");
    putU32(file, static_cast<uint32_t>(bomt.size()));
    file.insert(file.end(), bomt.begin(), bomt.end());

    auto path = tempPath("test.bone");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"bone_index\"") != std::string::npos);
    CHECK(result.output.find("58") != std::string::npos);
    CHECK(result.output.find("0.001") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: nonexistent path fails cleanly, not a crash") {
    auto result = runHusk("dump-chunks /nonexistent/path.m2");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: dump-chunks failed") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);
}
