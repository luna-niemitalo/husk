// `husk dump-chunks` tests: exercises the real compiled binary (see
// run_husk.hpp), same "spawn the actual CLI, check output shape" approach
// as tests/test_cli.cpp uses for info/export -- the per-chunk parsing
// logic lives entirely inside cmd_dump.cpp's anonymous namespace, so this
// is the only way to reach it. Not every documented chunk gets its own
// test here (TXAC/EXPT/PSBC/PEDC/GPID/PGD1/EDGF/DBOC/WFV3 all share one of
// two simple shapes already covered below -- a fixed-size record array
// sized by chunk byte length, or a chunk-relative M2Array<T> descriptor);
// this covers each *distinct* code path once; deliberately reuses
// tests/test_m2.cpp's already-established fixture-building conventions.

#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"

namespace {

using husk::test::runHusk;
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

void putTag(std::vector<uint8_t>& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

// Same minimal fully-zeroed MD20 blob as test_cli.cpp's minimalMd20 (through
// particleEmitters, 0x130 bytes) -- duplicated locally rather than shared,
// since these two test files exercise different subcommands and don't
// otherwise need a shared fixture header.
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

TEST_CASE("husk dump-chunks: a pre-Legion flat MD20 file has nothing to dump, not an error") {
    auto path = tempPath("flat.m2");
    writeFile(path, minimalMd20());

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("pre-Legion flat MD20") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: a chunked file with none of the dumpable chunks prints an empty "
          "JSON object, not a crash or a stray chunk entry") {
    auto file = wrapChunked(minimalMd20(), {});
    auto path = tempPath("empty.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find('{') != std::string::npos);
    CHECK(result.output.find('}') != std::string::npos);
    CHECK(result.output.find("\"TXAC\"") == std::string::npos);
    CHECK(result.output.find("\"NERF\"") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: NERF (a single fixed struct) round-trips its two float fields") {
    std::vector<uint8_t> nerf;
    putF32(nerf, 12.5f);
    putF32(nerf, -3.25f);
    auto file = wrapChunked(minimalMd20(), {{"NERF", nerf}});
    auto path = tempPath("nerf.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"NERF\"") != std::string::npos);
    CHECK(result.output.find("12.5") != std::string::npos);
    CHECK(result.output.find("-3.25") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: PABC (a chunk-relative M2Array<uint16_t>) reads its entries via the "
          "self-describing (count, offset) header") {
    std::vector<uint8_t> pabc;
    putU32(pabc, 3);   // count
    putU32(pabc, 8);   // offset, right after this 8-byte header
    pabc.push_back(100);
    pabc.push_back(0);  // entry 0 = 100
    pabc.push_back(200);
    pabc.push_back(0);  // entry 1 = 200
    pabc.push_back(255);
    pabc.push_back(255);  // entry 2 = 65535
    auto file = wrapChunked(minimalMd20(), {{"PABC", pabc}});
    auto path = tempPath("pabc.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"PABC\"") != std::string::npos);
    CHECK(result.output.find("100") != std::string::npos);
    CHECK(result.output.find("200") != std::string::npos);
    CHECK(result.output.find("65535") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: RPID (a flat FileDataID array sized by chunk byte length) reads "
          "every entry") {
    std::vector<uint8_t> rpid;
    putU32(rpid, 469824);
    putU32(rpid, 469830);
    auto file = wrapChunked(minimalMd20(), {{"RPID", rpid}});
    auto path = tempPath("rpid.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("469824") != std::string::npos);
    CHECK(result.output.find("469830") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: an undocumented chunk (e.g. WFV1) is included as a raw hex dump "
          "plus a note, not silently dropped") {
    std::vector<uint8_t> wfv1 = {0xDE, 0xAD, 0xBE, 0xEF};
    auto file = wrapChunked(minimalMd20(), {{"WFV1", wfv1}});
    auto path = tempPath("wfv1.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"WFV1\"") != std::string::npos);
    CHECK(result.output.find("deadbeef") != std::string::npos);
    CHECK(result.output.find("not documented") != std::string::npos);

    fs::remove(path);
}

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
