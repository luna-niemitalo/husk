// CLI tier: argument-grammar tests -- exercises husk's top-level argv
// parsing (CLI11-based `export`, hand-rolled `info`/`dump-chunks`) by
// spawning the real compiled binary (see run_husk.hpp). Always run, no real
// game files or HUSK_TEST_* env vars needed. See TEST_DESIGN.md#Four-tier-
// architecture for how this tier relates to the others.
//
// Split out of tests/test_cli.cpp (FILE_SPLIT_TODO.md's post-completion
// audit -- the original file was still over the 1000-line hard limit after
// Item 5's own split): every case here is about *how flags are spelled and
// combined on the command line*, success or failure alike -- named vs.
// positional -i/-o, every flag accepted named in arbitrary order, --help/
// -h/--version/-V, the no-command/unknown-command/no-arguments usage paths,
// and CLI11's own RequiredError/ExtrasError/missing-value parse errors plus
// info/dump-chunks' hand-written argc guards -- not about what a well-formed
// or corrupted model *file*'s contents do once argv parsing succeeds.
// Corrupted/adversarial *file-content* cases (bad chunk data, out-of-range
// indices, bone cycles, non-finite vertices) live in
// tests/test_cli_errors.cpp instead -- see that file's own doc comment.
// Shared byte-builder helpers live in tests/test_cli_fixtures.hpp, included
// by all test_cli*.cpp files.

#include <doctest/doctest.h>
#include <filesystem>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

// --skel/--textures/--anim/--skin-dir three(-plus)-state coverage
// (DESIGN.md's "CLI argument grammar for export") not already exercised above.

TEST_CASE("husk export: -i/--input and -o/--output work identically as named flags and as bare "
          "positionals") {
    auto dir = defaultsDir("namedflags");
    writeFile(dir / "flagged.m2", tinyValidM2());
    writeFile(dir / "flagged00.skin", tinyMatchingSkin());

    auto outPath = dir / "explicit-out.glb";
    auto result =
        runHusk("export -i " + (dir / "flagged.m2").string() + " -o " + outPath.string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outPath));

    auto outPath2 = dir / "explicit-out2.glb";
    auto result2 = runHusk("export --input " + (dir / "flagged.m2").string() + " --output " +
                            outPath2.string());
    CHECK(result2.exitCode == 0);
    CHECK(fs::exists(outPath2));

    // -o given *before* the model on the command line: proves the bare
    // positional (the model path) still binds to --input regardless of
    // where a named flag sits in argv, not just when named flags trail it.
    auto outPath3 = dir / "explicit-out3.glb";
    auto result3 =
        runHusk("export -o " + outPath3.string() + " " + (dir / "flagged.m2").string());
    CHECK(result3.exitCode == 0);
    CHECK(fs::exists(outPath3));

    fs::remove_all(dir);
}

TEST_CASE("husk export: every flag accepted named, in arbitrary order") {
    auto dir = defaultsDir("allflags");
    writeFile(dir / "m.m2", tinyValidM2());
    writeFile(dir / "m.skin", tinyMatchingSkin());
    auto outPath = dir / "out.glb";

    // Every flag but --skin-dir/--lod (both only meaningful alongside
    // --skin auto -- combining them with an explicit --skin path is its
    // own rejected case, covered by its own dedicated tests above),
    // scrambled out of the order addExportOptions declares them in, proving
    // none of them depend on argv position.
    auto result = runHusk("export --anim none --textures none --skel none -o " + outPath.string() +
                           " --skin " + (dir / "m.skin").string() + " -i " +
                           (dir / "m.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outPath));

    fs::remove_all(dir);
}

// `export`'s --help/-h no longer needs commands::isHelpFlag's hand-rolled
// pre-check (see commands.hpp's doc comment, and info/dump-chunks below,
// which still do) -- CLI11 recognizes -h/--help itself, anywhere in argv,
// and prints its own auto-generated help sourced from addExportOptions's
// real flag surface, not a hand-written usage block. These three tests
// check for real flag names in that output, not the old hand-written prose.
TEST_CASE("husk export --help prints CLI11's own generated help (real flag names) and exits 0, "
          "not a file-not-found error") {
    auto result = runHusk("export --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--skin") != std::string::npos);
    CHECK(result.output.find("--anim") != std::string::npos);
    CHECK(result.output.find("--lod") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk export -h (shorthand) prints the same CLI11-generated help and exits 0") {
    auto result = runHusk("export -h");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--skin") != std::string::npos);
    CHECK(result.output.find("--anim") != std::string::npos);
}

TEST_CASE("husk export <file.m2> --help (help after a positional) still prints CLI11's help, "
          "without ever trying to open the (nonexistent) model path") {
    auto result = runHusk("export nonexistent-model.m2 --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--skin") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk info --help prints usage and exits 0, not a file-not-found error") {
    auto result = runHusk("info --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk info") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk dump-chunks --help prints usage and exits 0, not a file-not-found error") {
    auto result = runHusk("dump-chunks --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk dump-chunks") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk --help prints the top-level command list and exits 0") {
    auto result = runHusk("--help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk <command>") != std::string::npos);
    CHECK(result.output.find("export") != std::string::npos);
    CHECK(result.output.find("dump-chunks") != std::string::npos);
}

TEST_CASE("husk --version prints a non-empty version string and exits 0") {
    // HUSK_VERSION is baked in at CMake configure time (a git describe,
    // see CMakeLists.txt) and varies build to build (e.g. a "-dirty"
    // suffix) -- this only checks the command works and says *something*
    // real, not any specific string.
    auto result = runHusk("--version");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("husk ") != std::string::npos);
    CHECK(result.output.find("husk unknown") == std::string::npos);
}

TEST_CASE("husk -V (shorthand) prints the same thing as --version") {
    auto result = runHusk("-V");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("husk ") != std::string::npos);
}

TEST_CASE("husk with no command prints usage and exits 1") {
    auto result = runHusk("");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk <command>") != std::string::npos);
}

TEST_CASE("husk with an unknown command fails cleanly, not a crash") {
    auto result = runHusk("frobnicate");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("unknown command") != std::string::npos);
}

// Remaining CLI argv edge cases: each subcommand's argc guard. export's own
// guard is CLI11's own machinery -- --input is ->required() (addExportOptions),
// and a flag given with no value is a real CLI11 parse-time error with
// CLI11's own named exit code (see CLI::ExitCodes in
// /nix/store/*-cli11-*/include/CLI/Error.hpp). info/dump-chunks's argc
// guards below are hand-written and unrelated.
// TODO: Remove: FINDINGS.md §4.3.

TEST_CASE("husk export with no arguments at all fails via CLI11's RequiredError (--input is "
          "required), not the old hand-written usage text") {
    auto result = runHusk("export");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--input") != std::string::npos);
    CHECK(result.output.find("required") != std::string::npos);
}

TEST_CASE("husk export --textures with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --textures");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--textures") != std::string::npos);
}

TEST_CASE("husk export --skin-dir with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --skin-dir");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--skin-dir") != std::string::npos);
}

TEST_CASE("husk export --anim with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --anim");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--anim") != std::string::npos);
}

TEST_CASE("husk export --skel with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --skel");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--skel") != std::string::npos);
}

TEST_CASE("husk export --lod with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --lod");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--lod") != std::string::npos);
}

TEST_CASE("husk export with a 3rd bare positional fails via CLI11's ExtrasError -- only -i/-o "
          "have a positional fallback (see addExportOptions's 'input'/'output' names), so a 3rd "
          "bare word is always rejected, regardless of how many named flags exist (replaces the "
          "old 'more than 4 positionals' test -- there's no 4th positional slot left to overflow "
          "into anymore)") {
    auto result = runHusk("export some.m2 a.glb extra-positional");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("not expected") != std::string::npos);
}

TEST_CASE("husk info with no arguments at all prints usage and exits 1") {
    auto result = runHusk("info");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk info") != std::string::npos);
}

TEST_CASE("husk info with more than one argument prints usage and exits 1") {
    auto result = runHusk("info a.m2 b.m2");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk info") != std::string::npos);
}

TEST_CASE("husk dump-chunks with no arguments at all prints usage and exits 1") {
    auto result = runHusk("dump-chunks");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk dump-chunks") != std::string::npos);
}

TEST_CASE("husk dump-chunks with more than one argument prints usage and exits 1") {
    auto result = runHusk("dump-chunks a.m2 b.m2");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk dump-chunks") != std::string::npos);
}

// Adversarial/out-of-range coverage for buildMaterialsAndPrimitives
// (cmd_export.cpp): six real bounds checks chaining batch -> submesh ->
// material -> color/textureWeight/texture/textureCoord. A real mismatched
// .skin/.m2 pairing hits exactly these paths.
// TODO: Remove: FINDINGS.md §4.2.

