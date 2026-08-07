// CLI tier: --anim/.skel-sourced animation-sequence resolution -- exercises
// husk::commands::exportGlb by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures. Split out of the
// original tests/test_cli.cpp (FILE_SPLIT_TODO.md Item 5) -- covers inline/
// external/alias sequence resolution, --anim <dir>/none/auto/inline,
// .skel-sourced (SKS1/AFSB) external sequences, and keyframe-data validation
// (NaN, non-monotonic, duplicate timestamps). See TEST_DESIGN.md#Four-tier-
// architecture for how this tier relates to the others.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>
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

TEST_CASE("husk export: a .skel with no SKS1 chunk at all gets no animation clips, not an "
          "error (a .skel isn't required to carry sequences, same as skel::findAnimFileIds "
          "already tolerating a missing AFID)") {
    // tinyValidM2() (not tinyAnimatedM2()) -- no inline bones at all, so
    // bones come entirely from the .skel file below, same as a real
    // Legion+ SKID-linked model. The M2's *own* sequences array is
    // irrelevant here regardless -- a .skel-sourced skeleton's animations
    // come from the .skel's own SKS1/AFID, never the owning M2's.
    auto m2 = tinyValidM2();

    auto m2Path = tempPath("skel-no-sks1.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-no-sks1.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-no-sks1.skel");
    writeFile(skelPath, buildSkel({{-1, -1}}));  // SKB1 only, no SKS1

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("skel-no-sks1.glb").string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a .skel with an inline (flags&0x20) SKS1 sequence and real SKB1 track "
          "data produces a real glTF animation, end to end") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    fillTrack(skb1Payload, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    fillTrack(skb1Payload, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(skb1Payload, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(300, 0x20));

    auto m2Path = tempPath("skel-inline-anim.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("skel-inline-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-inline-anim.skel");
    writeFile(skelPath, skel);

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("skel-inline-anim.glb").string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a .skel external (flags without 0x20/0x40) SKS1 sequence resolves via "
          "the .skel's own AFID + --anim <dir>, cross-blob (own AFID table, not the owning M2's)") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    size_t transOff = boneOff + 0x10;
    uint16_t noGlobalSeq = 0xFFFF;  // see fillTrack's identical comment
    std::memcpy(skb1Payload.data() + transOff + 0x02, &noGlobalSeq, 2);
    uint32_t tsOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(tsOuterOff + 8, 0);
    putArrayAt(skb1Payload, tsOuterOff, 1, 0);  // seq 0: 1 timestamp at *anim-blob* offset 0
    uint32_t valOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(valOuterOff + 8, 0);
    putArrayAt(skb1Payload, valOuterOff, 1, 4);  // seq 0: 1 C3Vector at *anim-blob* offset 4
    putArrayAt(skb1Payload, transOff + 0x04, 1, tsOuterOff);
    putArrayAt(skb1Payload, transOff + 0x0C, 1, valOuterOff);

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(400, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 400);  // anim_id
    putU16(afid, 0);    // sub_anim_id
    putU32(afid, 777);  // file_id -- this .skel's own AFID, unrelated to the M2's
    appendChunkTo(skel, "AFID", afid);

    auto m2Path = tempPath("skel-external-anim.m2");
    writeFile(m2Path, tinyValidM2());  // globalFlags=0 -- .anim is flat, matching tinyAnimFile()
    auto skinPath = tempPath("skel-external-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-external-anim.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-anim-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());

    auto result =
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-external-anim.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence whose --anim <dir> file is AFSB-tagged "
          "resolves a real animation clip, end to end -- SKB1's own per-sequence (count,offset) "
          "descriptors point directly into the AFSB payload, same mechanism as an AFM2-shaped "
          "external file just pointed at a different blob") {
    // TODO: Remove: `WIKI_FINDINGS/M2/anim.md`'s follow-up.
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    size_t transOff = boneOff + 0x10;
    uint16_t noGlobalSeq = 0xFFFF;  // see fillTrack's identical comment
    std::memcpy(skb1Payload.data() + transOff + 0x02, &noGlobalSeq, 2);
    uint32_t tsOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(tsOuterOff + 8, 0);
    putArrayAt(skb1Payload, tsOuterOff, 1, 0);  // seq 0: 1 timestamp at *AFSB-blob* offset 0
    uint32_t valOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(valOuterOff + 8, 0);
    putArrayAt(skb1Payload, valOuterOff, 1, 4);  // seq 0: 1 C3Vector at *AFSB-blob* offset 4
    putArrayAt(skb1Payload, transOff + 0x04, 1, tsOuterOff);
    putArrayAt(skb1Payload, transOff + 0x0C, 1, valOuterOff);

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(500, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 500);
    putU16(afid, 0);
    putU32(afid, 888);
    appendChunkTo(skel, "AFID", afid);

    // globalFlags |= 0x200000 -- "chunked .anim files" (wowdev.wiki's
    // flag_unk_0x200000) -- needed for the AFM2-vs-AFSB sniff to run at
    // all (see buildAnimations's doc comment); a flat-.anim model has no
    // AFSB shape to begin with.
    auto m2 = tinyValidM2();
    uint32_t globalFlags = 0x200000;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);

    auto m2Path = tempPath("skel-afsb-anim.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-afsb-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-afsb-anim.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-afsb-dir";
    fs::create_directories(animDir);
    std::vector<uint8_t> afsbFile;
    appendChunkTo(afsbFile, "AFSB", tinyAnimFile());  // real [timestamp][Vec3] payload
    writeFile(animDir / "888.anim", afsbFile);

    auto result =
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-afsb-anim.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence's --anim <dir> file with BOTH a small AFM2 "
          "stub chunk and a real AFSB chunk resolves the AFSB data, not the AFM2 stub -- real "
          "bloodelffemale_hd .anim files have exactly this shape (a tiny, all-near-zero AFM2 "
          "stub alongside the real AFSB data); using the stub's payload as if it were the full "
          "flat-format content throws a real 'claims more keyframes than this blob holds' error "
          "instead, so AFSB has to take priority whenever both are present") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    size_t transOff = boneOff + 0x10;
    uint16_t noGlobalSeq = 0xFFFF;
    std::memcpy(skb1Payload.data() + transOff + 0x02, &noGlobalSeq, 2);
    uint32_t tsOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(tsOuterOff + 8, 0);
    putArrayAt(skb1Payload, tsOuterOff, 1, 0);
    uint32_t valOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(valOuterOff + 8, 0);
    putArrayAt(skb1Payload, valOuterOff, 1, 4);
    putArrayAt(skb1Payload, transOff + 0x04, 1, tsOuterOff);
    putArrayAt(skb1Payload, transOff + 0x0C, 1, valOuterOff);

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(600, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 600);
    putU16(afid, 0);
    putU32(afid, 999);
    appendChunkTo(skel, "AFID", afid);

    auto m2 = tinyValidM2();
    uint32_t globalFlags = 0x200000;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);

    auto m2Path = tempPath("skel-afm2-stub-afsb-anim.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-afm2-stub-afsb-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-afm2-stub-afsb-anim.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-afm2-stub-dir";
    fs::create_directories(animDir);
    std::vector<uint8_t> mixedFile;
    appendChunkTo(mixedFile, "AFM2", {0, 0, 0, 0});    // tiny stub, not real track data
    appendChunkTo(mixedFile, "AFSB", tinyAnimFile());  // the real data
    writeFile(animDir / "999.anim", mixedFile);

    auto result =
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-afm2-stub-afsb-anim.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence's chunked --anim <dir> file with neither an "
          "AFM2 nor an AFSB chunk produces no animation clip, not an error -- an unrecognized "
          "future .anim shape, same skip policy as a missing file") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);  // tracks left empty -- never reached

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(700, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 700);
    putU16(afid, 0);
    putU32(afid, 1111);
    appendChunkTo(skel, "AFID", afid);

    auto m2 = tinyValidM2();
    uint32_t globalFlags = 0x200000;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);

    auto m2Path = tempPath("skel-neither-afm2-afsb.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-neither-afm2-afsb.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-neither-afm2-afsb.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-neither-dir";
    fs::create_directories(animDir);
    std::vector<uint8_t> unknownFile;
    appendChunkTo(unknownFile, "ZZZZ", {1, 2, 3, 4});  // some future chunk shape, not AFM2/AFSB
    writeFile(animDir / "1111.anim", unknownFile);

    auto result =
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-neither-afm2-afsb.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim defaults to the model's own directory -- an external sequence's "
          ".anim file already sitting there resolves without passing the flag") {
    auto dir = defaultsDir("animdir");
    auto md20 = tinyExternalAnimM2();

    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);  // anim_id, matches tinyExternalAnimM2's sequence id
    putU16(file, 0);    // sub_anim_id
    putU32(file, 999);  // file_id

    writeFile(dir / "extanim.m2", file);
    writeFile(dir / "extanim00.skin", tinyMatchingSkin());
    writeFile(dir / "999.anim", tinyAnimFile());

    auto result = runHusk("export " + (dir / "extanim.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

// oneTextureOneMaterialM2()/oneBatchTwoTexturesSkin() (used only by
// tests/test_cli.cpp's multi-texture-batch regression tests, not by
// anything in this file) now live in tests/test_cli_fixtures.hpp.

// Non-finite (NaN/Inf) values and non-monotonic timestamps are checked for
// animation keyframe data the same way they are for vertex positions/
// normals. Both fixtures below are otherwise identical to tinyAnimatedM2()
// (see its own doc comment), just with the translation track's second
// keyframe corrupted one way at a time.
// TODO: Remove: regression tests for FAILURES2.md #9/FAILURES.md #4.
TEST_CASE("husk export: a non-finite (NaN) translation keyframe value fails with a real message, "
          "not a silently-invalid .glb") {
    auto b = tinyValidM2();
    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 100;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    uint32_t seqFlags = 0x20;
    std::memcpy(b.data() + seqOff + 0x0C, &seqFlags, 4);

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    float nan = std::numeric_limits<float>::quiet_NaN();
    fillTrack(b, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(nan, 2, 3)});
    fillTrack(b, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("nan-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("nan-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("nan-keyframe.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("non-finite (NaN/Inf) value") != std::string::npos);
    CHECK(result.output.find("bone 0's translation keyframe 1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a non-monotonic (out-of-order) translation keyframe timestamp fails with "
          "a real message") {
    auto b = tinyValidM2();
    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 100;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    uint32_t seqFlags = 0x20;
    std::memcpy(b.data() + seqOff + 0x0C, &seqFlags, 4);

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    // Keyframe 1's timestamp (500) is *before* keyframe 0's (1000) -- a
    // corrupted/truncated read, not a valid ascending keyframe sequence.
    fillTrack(b, boneOff + 0x10, {1000, 500}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    fillTrack(b, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("nonmonotonic-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("nonmonotonic-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("nonmonotonic-keyframe.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("isn't strictly greater than") != std::string::npos);
    CHECK(result.output.find("bone 0's translation keyframe 1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// The ordinary (finite, ascending) case -- tinyAnimatedM2() itself -- must
// keep working; already covered by other tests in this file (e.g. "husk
// export: end-to-end animated model produces a real glTF animation clip"),
// not repeated here.

// Regression test: real shipped Blizzard data (5 real
// files -- world bosses, base character rigs, one world doodad, all on
// `rotation`) has an exact-duplicate keyframe timestamp -- a genuinely-
// authored "hard cut" pose (two values meant to apply at the same instant),
// not corruption. This used to be rejected identically to real disorder (a
// timestamp genuinely *decreasing*, still covered by the test above); it's
// now repaired instead (the later duplicate nudged forward 1ms) so both
// authored values survive, rather than collapsing one away.
TEST_CASE("husk export: an exact-duplicate keyframe timestamp is repaired (nudged forward 1ms), "
          "not rejected like genuine disorder") {
    auto b = tinyValidM2();
    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 100;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    uint32_t seqFlags = 0x20;
    std::memcpy(b.data() + seqOff + 0x0C, &seqFlags, 4);

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    fillTrack(b, boneOff + 0x10, {0}, {vec3Bytes(0, 0, 0)});
    // Rotation keyframes 1 and 2 share timestamp 500 -- the real shape found
    // on yoggsaronbrain.m2/maldraxxusskeleton.m2/mechagnomemale.m2/etc.
    fillTrack(b, boneOff + 0x24, {0, 500, 500},
              {identityQuatBytes(), identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("duplicate-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("duplicate-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("duplicate-keyframe.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("isn't strictly greater than") == std::string::npos);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// A genuine 3-way cascading duplicate run (T, T, T) must repair cleanly
// into strictly-increasing timestamps (T, T+1, T+2), not misfire the
// disorder check on the second duplicate -- see
// repairDuplicateTimestampsAndValidate's own doc comment for why comparing
// against the *original* (not already-nudged) previous timestamp matters.
TEST_CASE("husk export: a 3-way cascading duplicate keyframe timestamp run repairs cleanly, not "
          "just a single pair") {
    auto b = tinyValidM2();
    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 100;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    uint32_t seqFlags = 0x20;
    std::memcpy(b.data() + seqOff + 0x0C, &seqFlags, 4);

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    fillTrack(b, boneOff + 0x10, {0}, {vec3Bytes(0, 0, 0)});
    fillTrack(b, boneOff + 0x24, {0, 500, 500, 500},
              {identityQuatBytes(), identityQuatBytes(), identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("cascading-duplicate-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("cascading-duplicate-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("cascading-duplicate-keyframe.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("isn't strictly greater than") == std::string::npos);
    CHECK(result.output.find("couldn't be repaired") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// A .skin file whose submeshes carry different skinSectionId ("geoset ID")
// values -- the normal shape for a real character model bundling multiple
// selectable hairstyles/gear geosets in one file. husk doesn't filter
// geosets (that's a separate, bigger feature), but it must say so loudly.
// TODO: Remove: regression test for FAILURES2.md #1.
TEST_CASE("husk export: --anim auto (explicit) produces the identical clip count as the "
          "default (omitted) case -- 'auto' is genuinely the default, not merely documented as "
          "one") {
    auto m2Path = tempPath("animated-anim-auto.m2");
    writeFile(m2Path, tinyAnimatedM2());
    auto skinPath = tempPath("animated-anim-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("animated-anim-auto.glb").string() + " --anim auto");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --anim inline still resolves a model's own inline + global-sequence "
          "clips (only external-directory resolution is what --anim inline turns off)") {
    auto m2Path = tempPath("anim-inline-still-inline.m2");
    writeFile(m2Path, tinyAnimatedM2());
    auto skinPath = tempPath("anim-inline-still-inline.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-inline-still-inline.glb").string() + " --anim inline");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --anim inline skips external-directory resolution entirely -- an "
          "external sequence's otherwise-resolvable --anim <dir> file is ignored, even sitting "
          "right there") {
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

    auto m2Path = tempPath("anim-inline-ignores-external.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-inline-ignores-external.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-inline-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-inline-ignores-external.glb").string() +
                           " --anim inline");
    CHECK(result.exitCode == 0);
    // Same model/skin/AFID/anim-dir fixture as the "--anim <dir> resolves
    // an external sequence" test above, which asserts "1 animation(s)" for
    // the exact same file layout with --anim <dir> instead -- the only
    // difference here is the flag value, proving --anim inline really does
    // suppress the external lookup rather than happening to find nothing.
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim none produces zero animation clips, but JOINTS_0/WEIGHTS_0 (the "
          "bind-pose skin) are still present when the model has bones") {
    auto m2Path = tempPath("anim-none.m2");
    writeFile(m2Path, tinyAnimatedM2());  // has 1 bone + 1 real inline sequence
    auto skinPath = tempPath("anim-none.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-none.glb").string() + " --anim none");
    CHECK(result.exitCode == 0);
    // Bones (and thus JOINTS_0/WEIGHTS_0 -- buildSkinning runs whenever
    // bones aren't empty, unconditionally on --anim) still print; only the
    // animation-clip resolution that --anim none suppresses is gone.
    CHECK(result.output.find("1 bones") != std::string::npos);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

