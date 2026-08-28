// CLI tier: `husk info --json` -- the structured twin of `husk info`'s
// prose output (REFACTOR/CLI_AND_TOOLING.md §3, src/cmd_info_json.cpp).
// Exercises the real compiled binary (see run_husk.hpp), same convention as
// every other test_cli*.cpp file. Two kinds of coverage:
//
// - Synthetic, always-run fixtures (test_cli_fixtures.hpp's minimalMd20/
//   tinyValidM2/putU16/putU32/putTag) for the flag's own plumbing: JSON is
//   emitted, is syntactically well-formed, prose is unaffected when the
//   flag is absent.
// - Real, optional fixtures (tests/test_data_paths.hpp's testM2()/
//   testWeaponParticleA(), same doctest::skip(...().empty()) convention
//   test_cli_db2.cpp already uses) for the three fields three real
//   consumers already scrape out of prose today -- particle_emitters count
//   (`black_additive_task.py`, `particle_only_task.py`), vertices count
//   (`particle_only_task.py`), and materials[].blend_mode (both) -- so
//   there's a real cross-check that this schema genuinely covers what they
//   need, not just what looks plausible.
//
// Validity is checked with a real parser, not a balance check. No new
// dependency is added to do it: `json.hpp` (nlohmann) already ships inside
// tinygltf's own include directory, and husk-lib links tinygltf PUBLIC, so
// husk-tests already has it on the include path. This matters more here
// than for json_writer.hpp's other consumer (`husk dump-chunks`, checked by
// substring in tests/test_dump_emitters.cpp): the entire point of `--json`
// is that *other* parsers consume it, so "a real parser accepts this" is
// the actual contract, and a bracket-balance check would pass a document
// with a trailing comma or a malformed number that every real consumer
// rejects.

#include <cstdint>
#include <cstring>
#include <json.hpp>
#include <optional>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_data_paths.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

namespace {

// Full JSON grammar check via nlohmann (see the header comment for why a
// real parser rather than a balance check, and why it costs no new
// dependency). Returns the parsed document so callers can assert on real
// typed values instead of substring-matching the raw text.
std::optional<nlohmann::json> parseJson(const std::string& s) {
    auto parsed = nlohmann::json::parse(s, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) return std::nullopt;
    return parsed;
}

}  // namespace

TEST_CASE("husk info --json: without the flag, prose output is unaffected") {
    auto path = tempPath("json-flag-absent.m2");
    writeFile(path, tinyValidM2());

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    // Prose's own first two lines -- see cmd_info.cpp's untouched printing.
    CHECK(result.output.find(path.string() + "\n") == 0);
    CHECK(result.output.find("  format: pre-Legion (flat MD20)") != std::string::npos);
    // Never emits a JSON object when --json isn't given.
    CHECK(result.output.find('{') == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info --json: emits a single well-formed JSON object, not prose") {
    auto path = tempPath("json-flag-present.m2");
    writeFile(path, tinyValidM2());

    auto result = runHusk("info --json " + path.string());
    CHECK(result.exitCode == 0);
    auto doc = parseJson(result.output);
    REQUIRE(doc.has_value());
    // Prose-only markers must be absent.
    CHECK(result.output.find("  format: ") == std::string::npos);
    CHECK(doc->at("path").is_string());
    CHECK(doc->at("format") == "pre_legion");
    CHECK(doc->at("version") == 274);

    fs::remove(path);
}

TEST_CASE("husk info --json: reports vertices/particle_emitters counts and per-material "
          "blend_mode -- the three fields real corpus-scan tasks already scrape from prose") {
    auto md20 = minimalMd20();

    // One vertex.
    uint32_t vertCount = 1;
    uint32_t vertOff = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x03C, &vertCount, 4);
    std::memcpy(md20.data() + 0x040, &vertOff, 4);
    md20.resize(md20.size() + 0x30, 0);

    // One material, blend_mode=4 (additive-family -- see
    // black_additive_task.py's own ADDITIVE_FAMILY_THRESHOLD).
    uint32_t matCount = 1;
    uint32_t matOff = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x070, &matCount, 4);
    std::memcpy(md20.data() + 0x074, &matOff, 4);
    putU16(md20, 0);  // flags
    putU16(md20, 4);  // blendMode

    auto path = tempPath("json-consumer-fields.m2");
    writeFile(path, md20);

    auto result = runHusk("info --json " + path.string());
    CHECK(result.exitCode == 0);
    auto doc = parseJson(result.output);
    REQUIRE(doc.has_value());
    CHECK(doc->at("vertices").at("count") == 1);
    CHECK(doc->at("particle_emitters").at("count") == 0);
    CHECK(doc->at("materials").at("entries").at(0).at("blend_mode") == 4);

    fs::remove(path);
}

TEST_CASE("husk info --json: format/chunk fields reflect a Legion+ chunked file, including an "
          "undocumented chunk tag note") {
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "SFID");  // documented
    putU32(bytes, 0);
    putTag(bytes, "ZZZZ");  // not in husk's known M2 chunk list
    putU32(bytes, 4);
    putU32(bytes, 0xDEADBEEF);

    auto path = tempPath("json-chunked.m2");
    writeFile(path, bytes);

    auto result = runHusk("info --json " + path.string());
    CHECK(result.exitCode == 0);
    auto doc = parseJson(result.output);
    REQUIRE(doc.has_value());
    CHECK(doc->at("format") == "legion_chunked");
    CHECK(doc->at("chunks").at("tags").get<std::vector<std::string>>().size() > 0);
    CHECK(result.output.find("\"SFID\"") != std::string::npos);
    CHECK(doc->at("chunks").at("undocumented_tags") == nlohmann::json::array({"ZZZZ"}));

    fs::remove(path);
}

TEST_CASE("husk info --json: a malformed file fails cleanly (exit 1, no partial JSON on stdout), "
          "same as prose") {
    auto path = tempPath("json-truncated.m2");
    writeFile(path, {'M', 'D', '2', '0'});  // magic only, far too short to parse

    auto result = runHusk("info --json " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find('{') == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info --json: real fixture (bloodelffemale.m2) -- vertices/particle_emitters "
          "counts match husk's own parse" * doctest::skip(husk::test::testM2().empty())) {
    std::string path = husk::test::testM2();

    auto result = runHusk("info --json " + path);
    CHECK(result.exitCode == 0);
    auto doc = parseJson(result.output);
    REQUIRE(doc.has_value());
    CHECK(doc->at("vertices").at("count") == 8061);
    CHECK(doc->at("particle_emitters").at("count") == 0);

    // Same real file, no --json: still plain prose, not JSON.
    auto proseResult = runHusk("info " + path);
    CHECK(proseResult.exitCode == 0);
    CHECK(proseResult.output.find('{') == std::string::npos);
}

TEST_CASE("husk info --json: real fixture with both ribbon and particle emitters "
          "(sword_1h_artifactskywall_d_06.m2) resolves real per-emitter fields" *
          doctest::skip(husk::test::testWeaponParticleA().empty())) {
    std::string path = husk::test::testWeaponParticleA();

    auto result = runHusk("info --json " + path);
    CHECK(result.exitCode == 0);
    auto doc = parseJson(result.output);
    REQUIRE(doc.has_value());
    // Cross-checked against `husk info` (no --json) on the same file before
    // writing this test: ribbon_emitters: 1, particle_emitters: 2.
    CHECK(doc->at("ribbon_emitters").at("count") == 1);
    CHECK(doc->at("particle_emitters").at("count") == 2);
    CHECK(doc->at("particle_emitters").at("entries").at(0).at("particle_id") == 4294967295u);
    CHECK(doc->at("record_stride_version_verified") == true);
}
