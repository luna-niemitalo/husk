// `husk dump-chunks` tests for writePhysFile (src/dump_phys.{hpp,cpp}) and
// the .phys dumping path -- both a standalone .phys file and PFDC's inline
// physics (same byte-for-byte chunk container, reusing husk's own .phys
// parser rather than a second one, see PFDC's own dumper in
// dump_chunks_misc.cpp). Exercises the real compiled binary (see
// run_husk.hpp) -- see tests/test_dump.cpp's own top comment and
// TEST_DESIGN.md#Four-tier-architecture for why.

#include <doctest/doctest.h>
#include <filesystem>

#include "run_husk.hpp"
#include "test_data_paths.hpp"
#include "test_dump_fixtures.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testPfdcVerificationM2;
namespace fs = std::filesystem;

// .phys's own chunk tags are byte-reversed on disk (WMO/ADT convention,
// opposite of every other sidecar husk reads -- see src/phys.hpp's doc
// comment) -- `tag` is given in wowdev.wiki's own un-reversed spelling and
// reversed here before writing.
void appendPhysChunk(std::vector<uint8_t>& file, const char tag[4], const std::vector<uint8_t>& payload) {
    file.push_back(tag[3]);
    file.push_back(tag[2]);
    file.push_back(tag[1]);
    file.push_back(tag[0]);
    putU32(file, static_cast<uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
}

// A minimal but real .phys byte layout (PHYS/BODY/SHAP/CAPS chunks) -- used
// both by the standalone ".phys file" test below and by the PFDC tests,
// since PFDC's own documented payload (wowdev.wiki M2#PFDC: "PHYS physics;
// char PADDING[6];") is byte-for-byte the same container a standalone
// .phys file's bytes are.
std::vector<uint8_t> buildMinimalPhysBytes() {
    std::vector<uint8_t> file;
    std::vector<uint8_t> physPayload;
    putU16(physPayload, 5);  // version
    appendPhysChunk(file, "PHYS", physPayload);

    std::vector<uint8_t> body;
    putU16(body, 1);   // type
    putU16(body, 0);   // padding
    putF32(body, 0);
    putF32(body, 0);
    putF32(body, 0);   // position
    putU16(body, 3);   // boneIndex -- the distinguishing value this test looks for
    putU16(body, 0);   // padding
    putU32(body, 0);   // shapes_base
    putU32(body, 1);   // shapes_count
    appendPhysChunk(file, "BODY", body);

    std::vector<uint8_t> shap;
    putU16(shap, 1);  // type: capsule
    putU16(shap, 0);  // index
    putU32(shap, 0);  // unk[4]
    putF32(shap, 0);  // friction
    putF32(shap, 0);  // restitution
    putF32(shap, 0);  // density
    appendPhysChunk(file, "SHAP", shap);

    std::vector<uint8_t> caps;
    putF32(caps, 0);
    putF32(caps, 0);
    putF32(caps, -1);      // localPosition1
    putF32(caps, 0);
    putF32(caps, 0);
    putF32(caps, 1);       // localPosition2
    putF32(caps, 0.4242f);  // radius -- the other distinguishing value
    appendPhysChunk(file, "CAPS", caps);

    return file;
}

}  // namespace

TEST_CASE("husk dump-chunks: a .phys file (byte-reversed tags, no MD20/MD21 magic) dumps its "
          "full body/shape/joint record set, each shape/joint resolved to its real type-specific "
          "data inline") {
    std::vector<uint8_t> file;
    std::vector<uint8_t> physPayload;
    putU16(physPayload, 5);  // version
    appendPhysChunk(file, "PHYS", physPayload);
    std::vector<uint8_t> phyt;
    putU32(phyt, 4);
    appendPhysChunk(file, "PHYT", phyt);

    std::vector<uint8_t> body;
    putU16(body, 1);   // type
    putU16(body, 0);   // padding
    putF32(body, 0);   // position.x
    putF32(body, 0);   // position.y
    putF32(body, 0);   // position.z
    putU16(body, 3);   // boneIndex
    putU16(body, 0);   // padding
    putU32(body, 0);   // shapes_base
    putU32(body, 1);   // shapes_count
    appendPhysChunk(file, "BODY", body);

    std::vector<uint8_t> shap;
    putU16(shap, 1);  // type: capsule
    putU16(shap, 0);  // index
    putU32(shap, 0);  // unk[4]
    putF32(shap, 0);  // friction
    putF32(shap, 0);  // restitution
    putF32(shap, 0);  // density
    appendPhysChunk(file, "SHAP", shap);

    std::vector<uint8_t> caps;
    putF32(caps, 0);
    putF32(caps, 0);
    putF32(caps, -1);  // localPosition1
    putF32(caps, 0);
    putF32(caps, 0);
    putF32(caps, 1);       // localPosition2
    putF32(caps, 0.4242f);  // radius -- the distinguishing value this test looks for
    appendPhysChunk(file, "CAPS", caps);

    std::vector<uint8_t> join;
    putU32(join, 0);  // bodyA
    putU32(join, 0);  // bodyB
    putU32(join, 0);  // unk[4]
    putU16(join, 2);  // type: weld
    putU16(join, 0);  // index
    appendPhysChunk(file, "JOIN", join);

    std::vector<uint8_t> welj;
    for (int i = 0; i < 12; ++i) putF32(welj, 0);  // frameA
    for (int i = 0; i < 12; ++i) putF32(welj, 0);  // frameB
    putF32(welj, 7.5f);  // angularFrequencyHz -- the other distinguishing value
    putF32(welj, 0);     // angularDampingRatio
    appendPhysChunk(file, "WELJ", welj);

    auto path = tempPath("test.phys");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"version\"") != std::string::npos);
    CHECK(result.output.find("\"bodies\"") != std::string::npos);
    CHECK(result.output.find("\"bone\": 3") != std::string::npos);  // the body's boneIndex
    CHECK(result.output.find("\"kind\"") != std::string::npos);
    CHECK(result.output.find("capsule") != std::string::npos);
    CHECK(result.output.find("0.4242") != std::string::npos);  // capsule radius, resolved inline
    CHECK(result.output.find("weld") != std::string::npos);
    CHECK(result.output.find("7.5") != std::string::npos);  // weld joint's angularFrequencyHz

    fs::remove(path);
}

// PFDC embeds a real .phys file's own byte-for-byte chunk container
// (wowdev.wiki M2#PFDC), reusing husk's existing .phys parser rather than
// a new one. Trailing zero padding (up to 6 bytes per the wiki) must be
// trimmed before handing the bytes to phys::parse, or husk::readChunks
// throws on the short trailing header -- this proves both the happy path
// and that the padding-trim logic doesn't eat real data.
// TODO: Remove: reuses husk's real-file-verified .phys parser, see
// `WIKI_FINDINGS/PHYS.md`.
TEST_CASE("husk dump-chunks: PFDC (inline physics, same byte-for-byte .phys container a standalone "
          "file uses) dumps the same body/shape data, trailing zero padding tolerated") {
    auto pfdcPayload = buildMinimalPhysBytes();
    pfdcPayload.insert(pfdcPayload.end(), {0, 0, 0, 0});  // 4 bytes of trailing PADDING

    auto file = wrapChunked(minimalMd20(), {{"PFDC", pfdcPayload}});
    auto path = tempPath("pfdc.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"PFDC\"") != std::string::npos);
    CHECK(result.output.find("\"bodies\"") != std::string::npos);
    CHECK(result.output.find("\"bone\": 3") != std::string::npos);  // the body's boneIndex
    CHECK(result.output.find("capsule") != std::string::npos);
    CHECK(result.output.find("0.4242") != std::string::npos);  // capsule radius, resolved inline

    fs::remove(path);
}

TEST_CASE("husk dump-chunks: PFDC with no trailing padding at all (an exact chunk boundary) still "
          "dumps cleanly -- the padding-trim logic must not eat real data when there's nothing to "
          "trim") {
    auto pfdcPayload = buildMinimalPhysBytes();  // no trailing bytes appended

    auto file = wrapChunked(minimalMd20(), {{"PFDC", pfdcPayload}});
    auto path = tempPath("pfdc-no-padding.m2");
    writeFile(path, file);

    auto result = runHusk("dump-chunks " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"bone\": 3") != std::string::npos);
    CHECK(result.output.find("0.4242") != std::string::npos);

    fs::remove(path);
}

TEST_CASE(
    "husk dump-chunks: real EXP2+PFDC file (pfdc_1003471.m2, pulled from live CASC) decodes a "
    "real alphaCutoff curve and a real version-6/phyt-3 physics body/joint set" *
    doctest::skip(testPfdcVerificationM2().empty())) {
    auto result = runHusk("dump-chunks " + testPfdcVerificationM2());
    CHECK(result.exitCode == 0);

    // EXP2: 4 real particle emitters, each with a real, non-trivial
    // 3-keyframe alphaCutoff curve (life_fraction 0 / 0.500015 / 1, all
    // alpha_cutoff values 0 on this specific file).
    CHECK(countOccurrences(result.output, "\"particle_id\"") == 4);
    CHECK(countOccurrences(result.output, "\"life_fraction\": 0,") == 4);
    CHECK(countOccurrences(result.output, "\"life_fraction\": 0.500015") == 4);
    CHECK(countOccurrences(result.output, "\"life_fraction\": 1") == 4);
    CHECK(countOccurrences(result.output, "\"alpha_cutoff\": 0") == 12);  // 4 emitters x 3 keyframes

    // PFDC: version 6 / phyt 3, 5 real bodies (indices 0-4), 4 real
    // shoulder joints chaining body 0->1->2->3->4, every non-root body's
    // single shape resolving to a capsule.
    CHECK(result.output.find("\"version\": 6") != std::string::npos);
    CHECK(result.output.find("\"phyt\": 3") != std::string::npos);
    CHECK(countOccurrences(result.output, "\"index\": 0") >= 1);
    CHECK(countOccurrences(result.output, "\"kind\": \"capsule\"") == 4);
    CHECK(countOccurrences(result.output, "\"kind\": \"shoulder\"") == 4);
    CHECK(result.output.find("\"body_a\": 0") != std::string::npos);
    CHECK(result.output.find("\"body_b\": 1") != std::string::npos);
}
