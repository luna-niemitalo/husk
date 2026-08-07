// CLI tier: inline-M2 animation-sequence resolution -- exercises
// husk::commands::exportGlb by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures. Split out of the
// original tests/test_cli.cpp (FILE_SPLIT_TODO.md Item 5), then split
// further out of tests/test_cli_anim.cpp itself (FILE_SPLIT_TODO.md's
// post-completion audit -- that file was still over the 1000-line hard
// limit after Item 5's own split): this file covers a model's own inline
// (flags&0x20) sequences, pure-alias (flags&0x40) chain resolution, and
// --anim <dir>'s FileDataID-vs-basename-convention resolution for a model
// with no .skel. .skel-sourced (SKS1/AFSB) external-sequence resolution and
// keyframe-data validation (NaN, non-monotonic, duplicate timestamps) moved
// to tests/test_cli_anim_skel.cpp -- see that file's own doc comment. See
// TEST_DESIGN.md#Four-tier-architecture for how this tier relates to the
// others.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export: an inline bone with a flags&0x20 sequence produces a real glTF "
          "animation, end to end") {
    auto m2Path = tempPath("animated.m2");
    writeFile(m2Path, tinyAnimatedM2());
    auto skinPath = tempPath("animated.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("animated.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --anim <dir> resolves an external (flags without 0x20/0x40) sequence's "
          "bone keyframes from a real .anim file, via AFID, end to end") {
    auto md20 = tinyExternalAnimM2();

    // Wrap in MD21 + an AFID chunk mapping (animId=200, subAnimId=0) ->
    // fileId=777 -- AFID only exists in the chunked container (wowdev.wiki:
    // "This section only applies to versions >= 7.0.1.20740").
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);  // anim_id
    putU16(file, 0);    // sub_anim_id
    putU32(file, 777);  // file_id

    auto m2Path = tempPath("external-anim.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("external-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("external-anim.glb").string() + " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: an external sequence with no matching --anim <dir> file produces no "
          "animation clip, not an error") {
    auto md20 = tinyExternalAnimM2();
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);
    putU16(file, 0);
    putU32(file, 777);

    auto m2Path = tempPath("external-anim-missing.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("external-anim-missing.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-dir-empty";
    fs::create_directories(animDir);  // no 777.anim inside

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("external-anim-missing.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

// A "pure alias" sequence (flags & 0x40 set, flags & 0x20 NOT set) has no
// keyframe data of its own -- buildAnimations resolves aliasNext to the
// terminal non-alias sequence and reuses *that* sequence's own keyframe
// data, registered under the alias's own id. `spec` gives each sequence's
// own (id, variationIndex, flags, aliasNext); only sequence array index 0
// gets real inline bone keyframe data (fillTrack's own single-outer-sub-
// array convention, same as tinyAnimatedM2), so any *other* sequence that
// produces a clip at all can only be getting there via alias-chain
// resolution borrowing index 0's data, not its own.
// TODO: Remove: former M2_GAPS_TODO.md Item 1 / `WIKI_FINDINGS/M2.md`.
struct AliasSeqSpec {
    uint16_t id;
    uint16_t variationIndex;
    uint32_t flags;
    uint16_t aliasNext;
};

std::vector<uint8_t> aliasChainM2(const std::vector<AliasSeqSpec>& specs) {
    auto b = tinyValidM2();

    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = static_cast<uint32_t>(specs.size());
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40 * specs.size(), 0);
    for (size_t i = 0; i < specs.size(); ++i) {
        size_t off = seqOff + i * 0x40;
        uint16_t id = specs[i].id;
        uint16_t variationIndex = specs[i].variationIndex;
        uint32_t flags = specs[i].flags;
        uint16_t aliasNext = specs[i].aliasNext;
        std::memcpy(b.data() + off + 0x00, &id, 2);
        std::memcpy(b.data() + off + 0x02, &variationIndex, 2);
        std::memcpy(b.data() + off + 0x0C, &flags, 4);
        std::memcpy(b.data() + off + 0x3E, &aliasNext, 2);
    }

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    fillTrack(b, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    fillTrack(b, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    return b;
}

TEST_CASE("husk export: a pure-alias sequence (flags 0x40, no 0x20) resolves via aliasNext to a "
          "sibling sequence's real inline data, producing a real clip instead of none") {
    auto m2Path = tempPath("alias-2hop.m2");
    writeFile(m2Path, aliasChainM2({{100, 0, 0x20, 0}, {200, 0, 0x40, 0}}));
    auto skinPath = tempPath("alias-2hop.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-2hop.glb").string());
    CHECK(result.exitCode == 0);
    // Both sequences producing a clip is the real signal here -- see
    // TEST_DESIGN.md#Mutation-tested-regressions for how this was verified
    // to actually catch the bug it's named after.
    CHECK(result.output.find("2 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a multi-hop alias chain (pure alias -> pure alias -> real inline data) "
          "resolves all the way to the terminal sequence") {
    auto m2Path = tempPath("alias-3hop.m2");
    // seq2 (id=300) -> aliasNext=1 -> seq1 (id=200, also pure alias) ->
    // aliasNext=0 -> seq0 (id=100, real inline data).
    writeFile(m2Path,
              aliasChainM2({{100, 0, 0x20, 0}, {200, 0, 0x40, 0}, {300, 0, 0x40, 1}}));
    auto skinPath = tempPath("alias-3hop.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-3hop.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("3 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a sequence flagged both inline (0x20) AND alias (0x40) resolves against "
          "its own sequence index, not the (possibly invalid) alias chain") {
    // TODO: Remove: this priority rule was forced by real data -- 31/38 real
    // alias sequences in bloodelffemale_hd.skel also carry 0x20
    // (`WIKI_FINDINGS/M2.md`'s follow-up).
    auto m2Path = tempPath("alias-both-flags.m2");
    // seq1 has both 0x20 and 0x40 set, and a deliberately out-of-range
    // aliasNext (99, only 2 sequences exist) that would throw immediately
    // if the alias-resolution branch were ever taken for it -- since this
    // fixture's fillTrack only supplies real keyframe data at sequence-
    // array index 0 (seq0's own), seq1 produces no clip of its own either
    // way (no data at its own index 1); the real proof here is exit code 0
    // (no crash from the invalid aliasNext), which a broken priority order
    // would turn into a hard failure, not a silently-wrong clip count.
    writeFile(m2Path, aliasChainM2({{100, 0, 0x20, 0}, {200, 0, 0x20 | 0x40, 99}}));
    auto skinPath = tempPath("alias-both-flags.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-both-flags.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a self-referencing pure-alias sequence (aliasNext points to itself) "
          "fails cleanly with a cycle error, not an infinite loop") {
    auto m2Path = tempPath("alias-cycle.m2");
    writeFile(m2Path, aliasChainM2({{100, 0, 0x40, 0}}));  // seq 0 aliases to itself
    auto skinPath = tempPath("alias-cycle.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-cycle.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: export failed") != std::string::npos);
    CHECK(result.output.find("cycle") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a pure-alias sequence's out-of-range aliasNext fails cleanly, not an "
          "out-of-bounds read") {
    auto m2Path = tempPath("alias-oob.m2");
    writeFile(m2Path, aliasChainM2({{100, 0, 0x40, 5}}));  // only 1 sequence, aliasNext=5
    auto skinPath = tempPath("alias-oob.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-oob.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: export failed") != std::string::npos);
    CHECK(result.output.find("aliasNext") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// A real wow.export-style extraction names external .anim files
// '<model-basename><animId:04d>-<subAnimId:02d>.anim' rather than
// '<FileDataID>.anim' (see findAnimFileByBasename, DESIGN.md's AFSB design
// note) -- these four cases cover its three-way priority (FileDataID file,
// basename file, neither) against the same tinyExternalAnimM2 fixture the
// two tests above already use.
// TODO: Remove: `WIKI_FINDINGS/M2/anim.md`.

TEST_CASE("husk export: --anim <dir> resolves via the basename convention when the model has no "
          "AFID chunk at all") {
    // Unwrapped (no MD21/AFID chunk) -- tinyExternalAnimM2's seqId=200,
    // subAnimId=0 has no FileDataID mapping to try in the first place, so
    // this exercises the animFileIds==nullopt path straight into the
    // basename fallback.
    auto m2 = tinyExternalAnimM2();

    auto m2Path = tempPath("anim-basename-no-afid.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("anim-basename-no-afid.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-no-afid-dir";
    fs::create_directories(animDir);
    writeFile(animDir / (m2Path.stem().string() + "0200-00.anim"), tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-no-afid.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim <dir> falls back to the basename convention when an AFID entry "
          "exists but its FileDataID-named file is missing") {
    auto md20 = tinyExternalAnimM2();
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);
    putU16(file, 0);
    putU32(file, 777);  // maps to 777.anim -- deliberately never written below

    auto m2Path = tempPath("anim-basename-afid-file-missing.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-basename-afid-file-missing.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-afid-file-missing-dir";
    fs::create_directories(animDir);
    writeFile(animDir / (m2Path.stem().string() + "0200-00.anim"), tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-afid-file-missing.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim <dir> prefers the FileDataID-named file over the basename-named "
          "one when both exist") {
    auto md20 = tinyExternalAnimM2();
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);
    putU16(file, 0);
    putU32(file, 777);

    auto m2Path = tempPath("anim-basename-priority.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-basename-priority.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-priority-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());  // real, resolvable data
    // Deliberately too small to hold the 1 timestamp (4 bytes) + 1 C3Vector
    // (12 bytes) tinyExternalAnimM2's track descriptors claim -- if the
    // basename file were read instead of 777.anim, resolving it would throw
    // a bounds error (a non-zero exit), not silently produce a wrong value.
    writeFile(animDir / (m2Path.stem().string() + "0200-00.anim"), {0, 0});

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-priority.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim <dir> produces no animation clip, not an error, when neither the "
          "FileDataID-named nor the basename-named file exists") {
    auto md20 = tinyExternalAnimM2();
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);
    putU16(file, 0);
    putU32(file, 777);

    auto m2Path = tempPath("anim-basename-neither.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-basename-neither.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-neither-dir";
    fs::create_directories(animDir);  // neither 777.anim nor the basename file exists

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-neither.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a sequence without flags&0x20 (external .anim data) produces no "
          "animation clip, even with real inline bone track data") {
    auto m2 = tinyAnimatedM2();
    // Clear the inline-storage flag -- same shape as a genuinely low-
    // priority sequence whose real keyframes live in a .anim file husk
    // doesn't parse the content of; the M2's own inline track data (if
    // any) shouldn't be trusted for a sequence that claims it isn't here.
    uint32_t seqOff = 0;
    std::memcpy(&seqOff, m2.data() + 0x020, 4);
    uint32_t noFlags = 0;
    std::memcpy(m2.data() + seqOff + 0x0C, &noFlags, 4);

    auto m2Path = tempPath("not-inline.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("not-inline.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("not-inline.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

