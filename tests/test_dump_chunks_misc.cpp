// `husk dump-chunks` tests for the ~20 small per-tag dumpers
// (src/dump_chunks_misc.{hpp,cpp}): dumpTxac/dumpExpt/dumpU16ArrayChunk
// (PABC)/dumpPadc/dumpPsbc/dumpPedc/dumpFileDataIdArrayChunk (RPID)/
// dumpWfv1/dumpWfv2/dumpWfv3/dumpNerf/dumpEdgf/dumpDboc/dumpTexl/dumpAfra/
// dumpDpiv/dumpDetl/dumpPfdc (EXP2 side only -- see test_dump_phys.cpp for
// its physics-body half)/dumpExp2/dumpPcol. Exercises the real compiled
// binary (see run_husk.hpp) -- see tests/test_dump.cpp's own top comment
// and TEST_DESIGN.md#Four-tier-architecture for why. Every documented
// dumper (TXAC/EXPT/PADC/PSBC/PEDC/RPID/GPID/PGD1/WFV3/NERF/EDGF/DBOC/TEXL/
// DETL/PFDC/EXP2) gets its own round-trip test except GPID/PGD1, which call
// the *exact same function pointer* as RPID/PABC (dumpFileDataIdArrayChunk/
// dumpU16ArrayChunk, see cmd_dump.cpp's kDocumented table) rather than
// merely sharing a similar shape -- a second test there would exercise
// identical code, not additional coverage. WFV3 in particular has ~20
// sequential hand-transcribed field offsets, exactly the shape of a silent
// transcription bug that's easy to introduce and hard to notice without a
// real offset round-trip.

#include <doctest/doctest.h>
#include <filesystem>

#include "run_husk.hpp"
#include "test_data_paths.hpp"
#include "test_dump_fixtures.hpp"

namespace {
using husk::test::runHusk;
using husk::test::testExp2VerificationM2;
namespace fs = std::filesystem;
}  // namespace

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

// ~20 sequential hand-transcribed float/int fields at fixed offsets --
// exactly the shape of a silent transcription bug. Every field gets a
// distinct value so a field landing at the wrong offset shows up as a
// specific wrong number, not a coincidental pass.
// TODO: Remove: WFV3 was the highest-risk untested dumper before this test
// existed (FINDINGS.md §4.4); precedent for this transcription-bug shape is
// WIKI_FINDINGS.md's M2Sequence-is-64-not-36-bytes investigation.
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

// Regression test: real corpus files (Shadowlands waterfall doodads) carry
// a 64-byte WFV3 chunk, missing exactly the trailing unk1-unk4 floats the
// wiki's own struct listing always includes. dumpWfv3 used to assume 80
// bytes unconditionally, so every field read past 0x40 threw a bounds
// error and failed the whole dump-chunks run for these files.
// TODO: Remove: see `WIKI_FINDINGS/M2.md`'s WFV3 entry (9 real files).
TEST_CASE("husk dump-chunks: WFV3's real 64-byte short variant reads every documented field and "
          "emits null for the missing unk1-unk4, instead of throwing") {
    std::vector<uint8_t> wfv3;
    for (int i = 0; i < 11; ++i) putF32(wfv3, static_cast<float>(i + 1));  // 0x00-0x28
    wfv3.push_back(0xAA);
    wfv3.push_back(0xBB);
    wfv3.push_back(0xCC);
    wfv3.push_back(0xDD);  // 0x2C basecolor_rgba
    uint16_t flags = 0x1234, unk0 = 0x5678;
    wfv3.push_back(static_cast<uint8_t>(flags));
    wfv3.push_back(static_cast<uint8_t>(flags >> 8));
    wfv3.push_back(static_cast<uint8_t>(unk0));
    wfv3.push_back(static_cast<uint8_t>(unk0 >> 8));
    putF32(wfv3, 12.0f);  // 0x34 values3_w
    putF32(wfv3, 13.0f);  // 0x38 values3_z
    putF32(wfv3, 14.0f);  // 0x3C values4_y
    REQUIRE(wfv3.size() == 0x40);  // the short variant -- no unk1-unk4 at all

    auto file = wrapChunked(minimalMd20(), {{"WFV3", wfv3}});
    auto path = tempPath("wfv3-short.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("\"bumpScale\": 1") != std::string::npos);
    CHECK(result.output.find("\"values4_y\": 14") != std::string::npos);
    CHECK(result.output.find("\"unk1\": null") != std::string::npos);
    CHECK(result.output.find("\"unk2\": null") != std::string::npos);
    CHECK(result.output.find("\"unk3\": null") != std::string::npos);
    CHECK(result.output.find("\"unk4\": null") != std::string::npos);

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

// TEXL has a real, unambiguous struct (unlike DETL's inconsistent
// offsets), so it gets real field-level parsing, same shape as DBOC
// (16-byte records sized by chunk byte length).
// TODO: Remove: regression test for FAILURES2.md #5 -- TEXL was recognized
// by cmd_info.cpp's chunk-tag list but absent from this file's own dump
// tables, so real TEXL data was invisible everywhere.
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

// WFV1/WFV2/DPIV/AFRA have no wowdev.wiki struct at all -- their layout
// was byte-decoded from real corpus files instead, and they get real
// structural parsing, not a raw-hex-dump fallback.
// TODO: Remove: this was former RO_COMPLETENESS_TODO.md Item 3, see
// WIKI_FINDINGS.md's WFV1/WFV2/DPIV/AFRA section.
TEST_CASE("husk dump-chunks: AFRA (a single fixed 16-byte struct) round-trips its float field") {
    std::vector<uint8_t> afra;
    putF32(afra, 0.6f);
    putF32(afra, 0.0f);
    putF32(afra, 0.0f);
    putF32(afra, 0.0f);
    auto file = wrapChunked(minimalMd20(), {{"AFRA", afra}});
    auto path = tempPath("afra.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"AFRA\"") != std::string::npos);
    CHECK(result.output.find("\"value\"") != std::string::npos);
    CHECK(result.output.find("0.6") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: WFV1 (a single fixed 16-byte struct) round-trips its float field") {
    std::vector<uint8_t> wfv1;
    putF32(wfv1, 10.25f);
    putF32(wfv1, 0.0f);
    putF32(wfv1, 0.0f);
    putF32(wfv1, 0.0f);
    auto file = wrapChunked(minimalMd20(), {{"WFV1", wfv1}});
    auto path = tempPath("wfv1.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"WFV1\"") != std::string::npos);
    CHECK(result.output.find("\"value\"") != std::string::npos);
    CHECK(result.output.find("10.25") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: WFV2 (a flat 16x float32 array, no wowdev.wiki struct) reads all 16 "
          "fields at the right offsets") {
    std::vector<uint8_t> wfv2;
    for (int i = 0; i < 16; ++i) {
        putF32(wfv2, static_cast<float>(i) + 0.5f);
    }
    auto file = wrapChunked(minimalMd20(), {{"WFV2", wfv2}});
    auto path = tempPath("wfv2.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"WFV2\"") != std::string::npos);
    for (int i = 0; i < 16; ++i) {
        CHECK(result.output.find(std::to_string(i) + ".5") != std::string::npos);
    }

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: DPIV (a real record array, chunk.size/32 records, not a single fixed "
          "struct) reads multiple records' fields") {
    std::vector<uint8_t> dpiv;
    // Record 0: all zero (the dominant real-corpus shape).
    for (int i = 0; i < 8; ++i) putF32(dpiv, 0.0f);
    // Record 1: distinct nonzero values in every field, proving each
    // record is read at its own 32-byte-stride offset, not just record 0.
    for (int i = 0; i < 8; ++i) putF32(dpiv, static_cast<float>(i) + 1.25f);
    auto file = wrapChunked(minimalMd20(), {{"DPIV", dpiv}});
    auto path = tempPath("dpiv.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"DPIV\"") != std::string::npos);
    CHECK(result.output.find("\"field_0\"") != std::string::npos);
    for (int i = 1; i <= 8; ++i) {
        CHECK(result.output.find(std::to_string(i) + ".25") != std::string::npos);
    }

    fs::remove(path);
}

// DETL's real 12-byte stride and its own 16-byte alignment padding. Real
// corpus half-float constants (0x231C -> 0.013885498046875, 0x3C00 ->
// exactly 1.0) used directly rather than made-up numbers, so this test
// also proves readHalfFloat decodes the exact bit patterns real files
// carry, not just some arbitrary half-float.
// TODO: Remove: stride/padding confirmed against 1,043 real corpus files,
// see `WIKI_FINDINGS/M2.md`.
TEST_CASE("husk dump-chunks: DETL decodes real corpus half-float constants and ignores its own "
          "16-byte alignment padding") {
    auto md20 = minimalMd20();
    putU32At(md20, /*offset::lights=*/0x108, 1);  // Array::count -- lights.count

    std::vector<uint8_t> detl;
    putU16(detl, 0x0008);  // flags -- one of the two real corpus values
    putU16(detl, 0x231C);  // scale, half-float bits -- real corpus constant 0.013885498046875
    putU16(detl, 0x3C00);  // diffuseColorMultiplier, half-float bits -- real corpus constant 1.0
    putU16(detl, 0);       // unk0
    putU32(detl, 0);       // unk1
    REQUIRE(detl.size() == 12);
    detl.resize(16, 0);  // alignment padding to the next 16-byte boundary

    auto file = wrapChunked(md20, {{"DETL", detl}});
    auto path = tempPath("detl.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"DETL\"") != std::string::npos);
    CHECK(result.output.find("\"flags\": 8") != std::string::npos);
    CHECK(result.output.find("\"scale\": 0.0138855") != std::string::npos);
    CHECK(result.output.find("\"diffuse_color_multiplier\": 1") != std::string::npos);
    CHECK(countOccurrences(result.output, "\"flags\"") == 1);  // padding not misread as a 2nd record

    fs::remove(path);
}

// Regression test for the defensive-floor logic itself: 3 real 12-byte
// records (36 bytes) pad up to 48 bytes (the next 16-byte boundary).
// 48 / 12 == 4, so a dumpDetl that trusted chunk.size / kSize alone
// (without lights.count as the authority) would silently report 4 records
// instead of 3, misreading 4 bytes of the alignment padding as if it were
// a real trailing record.
TEST_CASE("husk dump-chunks: DETL's record count is lights.count, not chunk.size / 12 -- 3 real "
          "records padded to 48 bytes must not decode as 4") {
    auto md20 = minimalMd20();
    putU32At(md20, /*offset::lights=*/0x108, 3);  // Array::count

    std::vector<uint8_t> detl;
    for (int i = 0; i < 3; ++i) {
        putU16(detl, 0);       // flags
        putU16(detl, 0x3C00);  // scale (half-float 1.0, arbitrary but valid)
        putU16(detl, 0x3C00);  // diffuseColorMultiplier
        putU16(detl, 0);       // unk0
        putU32(detl, 0);       // unk1
    }
    REQUIRE(detl.size() == 36);
    detl.resize(48, 0);  // alignment padding -- 48 / 12 == 4, the overcount this test guards against

    auto file = wrapChunked(md20, {{"DETL", detl}});
    auto path = tempPath("detl-3lights.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(countOccurrences(result.output, "\"flags\"") == 3);

    fs::remove(path);
}

// EXP2's M2Array<M2ExtendedParticle> content,
// chunk-local header (same readChunkArray shape PABC/PSBC/PADC/PEDC/PGD1
// already use) followed by one 28-byte record: zSource/colorMult/
// alphaMult (12 bytes, same as EXPT) then a 16-byte M2PartTrack<fixed16>
// alphaCutoff descriptor (times Array + values Array, both dereferenced
// against this same chunk's own payload bytes). times=[0, 32767] and
// values=[32767, 0] decode to clean 0.0/1.0 fixed16 fractions, avoiding
// any float-rounding ambiguity in the assertions.
TEST_CASE("husk dump-chunks: EXP2 (M2Array<M2ExtendedParticle>, chunk-local header) reads "
          "zSource/colorMult/alphaMult and resolves the alphaCutoff M2PartTrack<fixed16> curve") {
    std::vector<uint8_t> exp2;
    putU32(exp2, 1);  // content.count
    putU32(exp2, 8);  // content.offset -- right after this 8-byte header

    size_t recordOff = exp2.size();
    putF32(exp2, 1.5f);    // zSource
    putF32(exp2, 0.5f);    // colorMult
    putF32(exp2, -0.75f);  // alphaMult
    // alphaCutoff: M2Array<fixed16> times, M2Array<fixed16> values -- offsets
    // filled in below once the actual arrays' positions are known.
    size_t timesArrayFieldOff = exp2.size();
    putU32(exp2, 0);
    putU32(exp2, 0);
    size_t valuesArrayFieldOff = exp2.size();
    putU32(exp2, 0);
    putU32(exp2, 0);
    REQUIRE(exp2.size() - recordOff == 0x1C);

    size_t timesOff = exp2.size();
    putU16(exp2, 0);       // times[0] = 0.0 (fixed16 0x0000)
    putU16(exp2, 32767);   // times[1] = 1.0 (fixed16 0x7FFF)
    size_t valuesOff = exp2.size();
    putU16(exp2, 32767);   // values[0] = 1.0
    putU16(exp2, 0);       // values[1] = 0.0

    putU32At(exp2, timesArrayFieldOff, 2);
    putU32At(exp2, timesArrayFieldOff + 4, static_cast<uint32_t>(timesOff));
    putU32At(exp2, valuesArrayFieldOff, 2);
    putU32At(exp2, valuesArrayFieldOff + 4, static_cast<uint32_t>(valuesOff));

    auto file = wrapChunked(minimalMd20(), {{"EXP2", exp2}});
    auto path = tempPath("exp2.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"EXP2\"") != std::string::npos);
    CHECK(result.output.find("\"z_source\": 1.5") != std::string::npos);
    CHECK(result.output.find("\"color_mult\": 0.5") != std::string::npos);
    CHECK(result.output.find("\"alpha_mult\": -0.75") != std::string::npos);
    CHECK(result.output.find("\"alpha_cutoff_track\"") != std::string::npos);
    CHECK(result.output.find("\"life_fraction\"") != std::string::npos);
    CHECK(result.output.find("\"alpha_cutoff\"") != std::string::npos);
    // Both curve points present as clean 0/1 fixed16 fractions (life_fraction
    // 0.0 pairs with alpha_cutoff 1.0, and vice versa).
    CHECK(countOccurrences(result.output, "\"life_fraction\"") == 2);

    fs::remove(path);
}

// PCOL's four independent (count, offset) regions, deliberately laid out
// non-contiguous (real gaps between regions, matching the wiki's own
// "there can be extra bytes between the data, use the offsets" warning) to
// prove the dumper reads each region via its own offset rather than
// assuming they're packed back-to-back. indices/flags include a negative
// value each to lock in the signed int16 (not uint16) read.
// TODO: Remove: a real file was found with an 8-byte gap between regions.
TEST_CASE("husk dump-chunks: PCOL (four independent, non-contiguous M2Array-shaped regions) "
          "reads vertex_positions/face_normals/indices/flags via their own offsets") {
    std::vector<uint8_t> pcol(32, 0);  // header, filled in below

    size_t vertexPosOff = pcol.size();
    putF32(pcol, 1.0f);
    putF32(pcol, 2.0f);
    putF32(pcol, 3.0f);
    putF32(pcol, -1.5f);
    putF32(pcol, 0.5f);
    putF32(pcol, 4.25f);

    pcol.insert(pcol.end(), 8, 0xCC);  // 8-byte gap -- must not be misread as data

    size_t faceNormOff = pcol.size();
    putF32(pcol, 0.0f);
    putF32(pcol, 0.0f);
    putF32(pcol, 1.0f);

    pcol.insert(pcol.end(), 4, 0xCC);  // 4-byte gap

    size_t indexOff = pcol.size();
    putU16(pcol, 0);
    putU16(pcol, 1);
    putU16(pcol, static_cast<uint16_t>(static_cast<int16_t>(-5)));

    pcol.insert(pcol.end(), 2, 0xCC);  // 2-byte gap

    size_t flagsOff = pcol.size();
    putU16(pcol, 7);
    putU16(pcol, static_cast<uint16_t>(static_cast<int16_t>(-3)));

    putU32At(pcol, 0x00, 2);  // vertexPosCount
    putU32At(pcol, 0x04, static_cast<uint32_t>(vertexPosOff));
    putU32At(pcol, 0x08, 1);  // faceNormCount
    putU32At(pcol, 0x0C, static_cast<uint32_t>(faceNormOff));
    putU32At(pcol, 0x10, 3);  // indexCount
    putU32At(pcol, 0x14, static_cast<uint32_t>(indexOff));
    putU32At(pcol, 0x18, 2);  // flagsCount
    putU32At(pcol, 0x1C, static_cast<uint32_t>(flagsOff));

    auto file = wrapChunked(minimalMd20(), {{"PCOL", pcol}});
    auto path = tempPath("pcol.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"PCOL\"") != std::string::npos);
    CHECK(result.output.find("\"vertex_positions\"") != std::string::npos);
    CHECK(countOccurrences(result.output, "\"x\": 1,") == 1);
    CHECK(result.output.find("\"x\": -1.5") != std::string::npos);
    CHECK(result.output.find("\"face_normals\"") != std::string::npos);
    CHECK(result.output.find("\"indices\"") != std::string::npos);
    CHECK(result.output.find("-5") != std::string::npos);
    CHECK(result.output.find("\"flags\"") != std::string::npos);
    CHECK(result.output.find("-3") != std::string::npos);

    fs::remove(path);
}

TEST_CASE(
    "husk dump-chunks: real PCOL file (pa_kite_lamp_creature.m2, from the local corpus) decodes "
    "40 vertices / 74 triangles with indexCount == faceNormCount * 3" *
    doctest::skip(husk::test::testPcolVerificationM2().empty())) {
    auto result = runHusk("dump-chunks " + husk::test::testPcolVerificationM2());
    CHECK(result.exitCode == 0);
    CHECK(countOccurrences(result.output, "\"x\":") >= 40 + 74);  // vertex_positions + face_normals
    // First vertex and last face normal, hand-checked against a fresh
    // dump-chunks run at test-writing time.
    CHECK(result.output.find("\"x\": 1.46941") != std::string::npos);
    CHECK(result.output.find("\"y\": 3.99775") != std::string::npos);
    CHECK(result.output.find("\"z\": 32.2569") != std::string::npos);
}

// The same real fixture PCOL's own regression test uses also happens to
// carry a real DPIV chunk -- one record, non-degenerate values in
// field_1/field_2, confirming dumpDpiv's record-array parsing against real
// bytes rather than only a synthetic fixture. Values hand-checked against
// a fresh dump-chunks run at test-writing time.
// TODO: Remove: this was former RO_COMPLETENESS_TODO.md Item 3, see
// WIKI_FINDINGS.md.
TEST_CASE(
    "husk dump-chunks: real DPIV data (same pa_kite_lamp_creature.m2 fixture as the PCOL "
    "regression test) decodes one record with real nonzero field_1/field_2 values" *
    doctest::skip(husk::test::testPcolVerificationM2().empty())) {
    auto result = runHusk("dump-chunks " + husk::test::testPcolVerificationM2());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"DPIV\"") != std::string::npos);
    CHECK(countOccurrences(result.output, "\"field_0\"") == 1);
    CHECK(result.output.find("2.79845") != std::string::npos);
    CHECK(result.output.find("22.0376") != std::string::npos);
}

// real-data regression coverage for EXP2/PFDC, pulled directly from a live
// CASC install rather than a synthetic fixture. exp2_126382.m2 carries
// EXP2 only, no PFDC.
// TODO: Remove: see `WIKI_FINDINGS/M2.md`.
TEST_CASE(
    "husk dump-chunks: real EXP2-only file (exp2_126382.m2, pulled from live CASC) decodes 9 "
    "particle emitters each with default zSource/colorMult/alphaMult and an empty alphaCutoff "
    "curve" *
    doctest::skip(testExp2VerificationM2().empty())) {
    auto result = runHusk("dump-chunks " + testExp2VerificationM2());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"EXP2\"") != std::string::npos);
    CHECK(result.output.find("\"PFDC\"") == std::string::npos);
    CHECK(countOccurrences(result.output, "\"particle_id\"") == 9);
    CHECK(countOccurrences(result.output, "\"z_source\": 0") == 9);
    CHECK(countOccurrences(result.output, "\"color_mult\": 1") == 9);
    CHECK(countOccurrences(result.output, "\"alpha_mult\": 1") == 9);
    // Every one of the 9 real EXP2 records has a genuinely empty
    // alphaCutoff curve -- no "life_fraction" keyframe anywhere in the
    // EXP2 section (the shared "particle_emitters" section has its own,
    // unrelated per-track keyframes, so this checks EXP2's own count is
    // zero rather than asserting an absolute zero across the whole file).
    auto exp2Pos = result.output.find("\"EXP2\"");
    REQUIRE(exp2Pos != std::string::npos);
    CHECK(result.output.find("\"life_fraction\"", exp2Pos) == std::string::npos);
}
