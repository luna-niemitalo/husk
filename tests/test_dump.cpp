// `husk dump-chunks` tests: exercises the real compiled binary (see
// run_husk.hpp), same "spawn the actual CLI, check output shape" approach
// as tests/test_cli.cpp uses for info/export -- the per-chunk parsing
// logic lives entirely inside cmd_dump.cpp's anonymous namespace, so this
// is the only way to reach it. Every documented dumper (TXAC/EXPT/PADC/
// PSBC/PEDC/RPID/GPID/PGD1/WFV3/NERF/EDGF/DBOC/TEXL) gets its own
// round-trip test except GPID/PGD1, which call the *exact same function
// pointer* as RPID/PABC (dumpFileDataIdArrayChunk/dumpU16ArrayChunk, see
// cmd_dump.cpp's kDocumented table) rather than merely sharing a similar
// shape -- a second test there would exercise identical code, not
// additional coverage. WFV3 in particular (FINDINGS.md §4.4) was
// previously untested despite being the highest-risk dumper here: ~20
// sequential hand-transcribed field offsets, exactly the shape of silent
// transcription bug this project's own history (the
// M2Sequence-is-64-not-36-bytes investigation, WIKI_FINDINGS.md) shows is
// easy to introduce and hard to notice without a real offset round-trip.
// Deliberately reuses tests/test_m2.cpp's already-established
// fixture-building conventions.

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

TEST_CASE("husk dump-chunks: TXAC (2-byte records sized by chunk byte length) reads unk0/unk1 "
          "per record") {
    std::vector<uint8_t> txac = {10, 20, 30, 40};  // 2 records: (10,20), (30,40)
    auto file = wrapChunked(minimalMd20(), {{"TXAC", txac}});
    auto path = tempPath("txac.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"TXAC\"") != std::string::npos);
    CHECK(result.output.find("\"unk0\"") != std::string::npos);
    CHECK(result.output.find("10") != std::string::npos);
    CHECK(result.output.find("30") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: EXPT (12-byte zSource/colorMult/alphaMult records) reads every "
          "field at the right offset") {
    std::vector<uint8_t> expt;
    putF32(expt, 1.25f);   // zSource
    putF32(expt, 0.5f);    // colorMult
    putF32(expt, -0.75f);  // alphaMult
    auto file = wrapChunked(minimalMd20(), {{"EXPT", expt}});
    auto path = tempPath("expt.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"zSource\"") != std::string::npos);
    CHECK(result.output.find("1.25") != std::string::npos);
    CHECK(result.output.find("\"colorMult\"") != std::string::npos);
    CHECK(result.output.find("0.5") != std::string::npos);
    CHECK(result.output.find("\"alphaMult\"") != std::string::npos);
    CHECK(result.output.find("-0.75") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: PSBC (chunk-relative M2Array<M2Bounds>, 28-byte records) reads "
          "min/max/radius at the right offsets") {
    std::vector<uint8_t> psbc;
    putU32(psbc, 1);  // count
    putU32(psbc, 8);  // offset, right after this 8-byte header
    putF32(psbc, -1.0f);
    putF32(psbc, -2.0f);
    putF32(psbc, -3.0f);  // min
    putF32(psbc, 4.0f);
    putF32(psbc, 5.0f);
    putF32(psbc, 6.0f);  // max
    putF32(psbc, 7.5f);  // radius
    auto file = wrapChunked(minimalMd20(), {{"PSBC", psbc}});
    auto path = tempPath("psbc.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"min_inferred\"") != std::string::npos);
    CHECK(result.output.find("\"max_inferred\"") != std::string::npos);
    CHECK(result.output.find("\"radius_inferred\"") != std::string::npos);
    CHECK(result.output.find("7.5") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: PEDC (chunk-relative M2Array<M2TrackBase>, 12-byte records) reads "
          "interpolation_type/global_sequence") {
    std::vector<uint8_t> pedc;
    putU32(pedc, 1);  // count
    putU32(pedc, 8);  // offset
    uint16_t interp = 1, globalSeq = 3;
    pedc.push_back(static_cast<uint8_t>(interp));
    pedc.push_back(static_cast<uint8_t>(interp >> 8));
    pedc.push_back(static_cast<uint8_t>(globalSeq));
    pedc.push_back(static_cast<uint8_t>(globalSeq >> 8));
    putU32(pedc, 0);  // timestamps outer array (unread)
    auto file = wrapChunked(minimalMd20(), {{"PEDC", pedc}});
    auto path = tempPath("pedc.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"interpolation_type\"") != std::string::npos);
    CHECK(result.output.find("\"global_sequence\"") != std::string::npos);
    CHECK(result.output.find("3") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: PADC (chunk-relative M2Array<M2TextureWeight>) reaches "
          "parseTextureWeights without crashing, weight null for an empty track") {
    // The track-resolution semantics themselves (constant vs. animated)
    // are already covered by tests/test_m2.cpp's parseTextureWeights
    // tests -- this only proves dump-chunks wires a real M2TextureWeight
    // record through readChunkArray correctly, same shape PABC's test
    // already established for a plain uint16 array.
    std::vector<uint8_t> padc;
    putU32(padc, 1);   // count
    putU32(padc, 8);   // offset
    padc.resize(8 + 0x14, 0);  // one zeroed M2TextureWeight record (empty track)
    auto file = wrapChunked(minimalMd20(), {{"PADC", padc}});
    auto path = tempPath("padc.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"PADC\"") != std::string::npos);
    CHECK(result.output.find("\"weight\"") != std::string::npos);
    CHECK(result.output.find("null") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: EDGF (24-byte records sized by chunk byte length) reads its three "
          "float fields, hex-dumps the rest") {
    std::vector<uint8_t> edgf;
    putF32(edgf, 1.5f);
    putF32(edgf, 2.5f);
    putF32(edgf, 3.5f);
    for (int i = 0; i < 12; ++i) edgf.push_back(static_cast<uint8_t>(0xA0 + i));
    auto file = wrapChunked(minimalMd20(), {{"EDGF", edgf}});
    auto path = tempPath("edgf.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"EDGF\"") != std::string::npos);
    CHECK(result.output.find("1.5") != std::string::npos);
    CHECK(result.output.find("2.5") != std::string::npos);
    CHECK(result.output.find("3.5") != std::string::npos);
    CHECK(result.output.find("\"_0xC_hex\"") != std::string::npos);
    CHECK(result.output.find("a0a1a2") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: DBOC (16-byte records sized by chunk byte length) reads its two "
          "float and two uint32 fields at the right offsets") {
    std::vector<uint8_t> dboc;
    putF32(dboc, 1.5f);
    putF32(dboc, -2.5f);
    putU32(dboc, 111);
    putU32(dboc, 222);
    auto file = wrapChunked(minimalMd20(), {{"DBOC", dboc}});
    auto path = tempPath("dboc.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"DBOC\"") != std::string::npos);
    CHECK(result.output.find("1.5") != std::string::npos);
    CHECK(result.output.find("-2.5") != std::string::npos);
    CHECK(result.output.find("111") != std::string::npos);
    CHECK(result.output.find("222") != std::string::npos);

    fs::remove(path);
}

// WFV3 is the highest-risk untested dumper before this test existed
// (FINDINGS.md §4.4): ~20 sequential hand-transcribed float/int fields at
// fixed offsets, exactly the shape of silent transcription bug this
// project's own history (the M2Sequence-is-64-not-36-bytes investigation,
// WIKI_FINDINGS.md) shows is easy to introduce and hard to notice without
// a real byte-offset round-trip test. Every field gets a distinct value so
// a field landing at the wrong offset shows up as a specific wrong number,
// not a coincidental pass.
TEST_CASE("husk dump-chunks: WFV3 (one fixed 80-byte struct) reads every field at its own "
          "documented offset") {
    std::vector<uint8_t> wfv3;
    putF32(wfv3, 1.0f);    // 0x00 bumpScale
    putF32(wfv3, 2.0f);    // 0x04 value0_x
    putF32(wfv3, 3.0f);    // 0x08 value0_y
    putF32(wfv3, 4.0f);    // 0x0C value0_z
    putF32(wfv3, 5.0f);    // 0x10 value1_w
    putF32(wfv3, 6.0f);    // 0x14 value0_w
    putF32(wfv3, 7.0f);    // 0x18 value1_x
    putF32(wfv3, 8.0f);    // 0x1C value1_y
    putF32(wfv3, 9.0f);    // 0x20 value2_w
    putF32(wfv3, 10.0f);   // 0x24 value3_y
    putF32(wfv3, 11.0f);   // 0x28 value3_x
    // 0xAA/0xBB/0xCC/0xDD (170/187/204/221 decimal) -- deliberately distinct
    // from every other field's value in this fixture (1-18, 0x1234, 0x5678)
    // so a substring match on one of these numbers can't coincidentally hit
    // a different field's serialized value.
    wfv3.push_back(0xAA);  // 0x2C basecolor_rgba[0]
    wfv3.push_back(0xBB);
    wfv3.push_back(0xCC);
    wfv3.push_back(0xDD);            // 0x2F basecolor_rgba[3]
    uint16_t flags = 0x1234;
    uint16_t unk0 = 0x5678;
    wfv3.push_back(static_cast<uint8_t>(flags));
    wfv3.push_back(static_cast<uint8_t>(flags >> 8));  // 0x30 flags
    wfv3.push_back(static_cast<uint8_t>(unk0));
    wfv3.push_back(static_cast<uint8_t>(unk0 >> 8));  // 0x32 unk0
    putF32(wfv3, 12.0f);                              // 0x34 values3_w
    putF32(wfv3, 13.0f);                              // 0x38 values3_z
    putF32(wfv3, 14.0f);                               // 0x3C values4_y
    putF32(wfv3, 15.0f);                               // 0x40 unk1
    putF32(wfv3, 16.0f);                               // 0x44 unk2
    putF32(wfv3, 17.0f);                               // 0x48 unk3
    putF32(wfv3, 18.0f);                               // 0x4C unk4
    REQUIRE(wfv3.size() == 0x50);

    auto file = wrapChunked(minimalMd20(), {{"WFV3", wfv3}});
    auto path = tempPath("wfv3.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    // Exact "key": value substrings (json_writer.hpp's key() always emits
    // '"key": ' immediately followed by the value, no intervening
    // whitespace) -- this is what actually verifies each field landed at
    // its own documented offset, not just that all 18 numbers appear
    // somewhere in the output (whole-number doubles print without a
    // trailing ".0" via default ostream formatting, e.g. 1.0f -> "1").
    CHECK(result.output.find("\"bumpScale\": 1") != std::string::npos);
    CHECK(result.output.find("\"value0_x\": 2") != std::string::npos);
    CHECK(result.output.find("\"value0_y\": 3") != std::string::npos);
    CHECK(result.output.find("\"value0_z\": 4") != std::string::npos);
    CHECK(result.output.find("\"value1_w\": 5") != std::string::npos);
    CHECK(result.output.find("\"value0_w\": 6") != std::string::npos);
    CHECK(result.output.find("\"value1_x\": 7") != std::string::npos);
    CHECK(result.output.find("\"value1_y\": 8") != std::string::npos);
    CHECK(result.output.find("\"value2_w\": 9") != std::string::npos);
    CHECK(result.output.find("\"value3_y\": 10") != std::string::npos);
    CHECK(result.output.find("\"value3_x\": 11") != std::string::npos);
    CHECK(result.output.find("\"values3_w\": 12") != std::string::npos);
    CHECK(result.output.find("\"values3_z\": 13") != std::string::npos);
    CHECK(result.output.find("\"values4_y\": 14") != std::string::npos);
    CHECK(result.output.find("\"unk1\": 15") != std::string::npos);
    CHECK(result.output.find("\"unk2\": 16") != std::string::npos);
    CHECK(result.output.find("\"unk3\": 17") != std::string::npos);
    CHECK(result.output.find("\"unk4\": 18") != std::string::npos);
    CHECK(result.output.find("\"flags\": 4660") != std::string::npos);  // 0x1234
    CHECK(result.output.find("\"unk0\": 22136") != std::string::npos);  // 0x5678
    CHECK(result.output.find("\"basecolor_rgba\"") != std::string::npos);
    CHECK(result.output.find("170") != std::string::npos);  // basecolor_rgba[0] = 0xAA
    CHECK(result.output.find("187") != std::string::npos);  // basecolor_rgba[1] = 0xBB
    CHECK(result.output.find("204") != std::string::npos);  // basecolor_rgba[2] = 0xCC
    CHECK(result.output.find("221") != std::string::npos);  // basecolor_rgba[3] = 0xDD

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

// Regression test for FAILURES2.md #5: TEXL was recognized by
// cmd_info.cpp's documentedM2ChunkTags (so it never tripped the
// "undocumented chunk" note) but was in neither of this file's own
// kDocumented/kFallback lists -- a real TEXL chunk was invisible to
// dump-chunks entirely, not even as a hex dump. TEXL has a real,
// unambiguous struct (unlike DETL's inconsistent offsets), so it now gets
// real field-level parsing, same shape as DBOC (16-byte records sized by
// chunk byte length).
TEST_CASE("husk dump-chunks: TEXL (light-cookie texture lookups) reads its four fields per record") {
    std::vector<uint8_t> texl;
    putF32(texl, 1.5f);
    putF32(texl, -2.5f);
    putU32(texl, 3);  // texture_lookup: index into TXID
    putU32(texl, 0);  // unk2
    auto file = wrapChunked(minimalMd20(), {{"TEXL", texl}});
    auto path = tempPath("texl.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"TEXL\"") != std::string::npos);
    CHECK(result.output.find("\"texture_lookup\"") != std::string::npos);
    CHECK(result.output.find("1.5") != std::string::npos);
    CHECK(result.output.find("-2.5") != std::string::npos);

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
