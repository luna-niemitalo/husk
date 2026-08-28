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
// No JSON parsing library is pulled in for this (per the task's own "don't
// add a JSON library" constraint) -- looksLikeValidJson below is a
// lightweight bracket/string-aware balance check, not a full parser; exact
// field/value assertions still go through substring matching on the raw
// text, same as tests/test_dump_emitters.cpp already does against
// json_writer.hpp's other real consumer (`husk dump-chunks`).

#include <cstdint>
#include <cstring>
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

// Balanced-bracket/string-aware sanity check: every '{'/'[' outside a
// string is matched by a later '}'/']', with proper backslash-escape and
// quote handling inside strings. Doesn't validate full JSON grammar (number
// syntax, comma placement, duplicate keys, ...) -- json_writer.hpp's own
// Writer already owns getting that part right; this just confirms the
// overall document is well-formed, not silently truncated or malformed.
bool looksLikeValidJson(const std::string& s) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (char c : s) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            --depth;
            if (depth < 0) return false;
        }
    }
    return !inString && depth == 0;
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
    CHECK(looksLikeValidJson(result.output));
    // Prose-only markers must be absent.
    CHECK(result.output.find("  format: ") == std::string::npos);
    CHECK(result.output.find("\"path\": ") != std::string::npos);
    CHECK(result.output.find("\"format\": \"pre_legion\"") != std::string::npos);
    CHECK(result.output.find("\"version\": 274") != std::string::npos);

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
    CHECK(looksLikeValidJson(result.output));
    CHECK(result.output.find("\"vertices\": {\n    \"count\": 1,") != std::string::npos);
    CHECK(result.output.find("\"particle_emitters\": {\n    \"count\": 0,") != std::string::npos);
    CHECK(result.output.find("\"blend_mode\": 4") != std::string::npos);

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
    CHECK(looksLikeValidJson(result.output));
    CHECK(result.output.find("\"format\": \"legion_chunked\"") != std::string::npos);
    CHECK(result.output.find("\"SFID\"") != std::string::npos);
    CHECK(result.output.find("\"undocumented_tags\": [\n      \"ZZZZ\"") != std::string::npos);

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
    CHECK(looksLikeValidJson(result.output));
    CHECK(result.output.find("\"vertices\": {\n    \"count\": 8061,") != std::string::npos);
    CHECK(result.output.find("\"particle_emitters\": {\n    \"count\": 0,") != std::string::npos);

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
    CHECK(looksLikeValidJson(result.output));
    // Cross-checked against `husk info` (no --json) on the same file before
    // writing this test: ribbon_emitters: 1, particle_emitters: 2.
    CHECK(result.output.find("\"ribbon_emitters\": {\n    \"count\": 1,") != std::string::npos);
    CHECK(result.output.find("\"particle_emitters\": {\n    \"count\": 2,") != std::string::npos);
    CHECK(result.output.find("\"particle_id\": 4294967295") != std::string::npos);
    CHECK(result.output.find("\"version_verified\": true") != std::string::npos);
}
