// CLI tier: `husk info` output tests -- exercises husk::commands::info by
// spawning the real compiled binary (see run_husk.hpp) against small,
// synthetic, on-disk fixtures -- always run, no real game files or
// HUSK_TEST_* env vars needed. See TEST_DESIGN.md#Four-tier-architecture for
// how this tier relates to the others. Every fixture below targets one
// specific, previously-confirmed-broken behavior; if any of these start
// failing again, it's a real regression, not a flake.
//
// Split out of tests/test_cli.cpp (FILE_SPLIT_TODO.md's post-completion
// audit -- the original file was still over the 1000-line hard limit after
// Item 5's own split): every `husk info`-only case that prints header/
// chunk/texture/material/sidecar-FileDataID detail on a well-formed (or at
// least well-formed-enough-to-parse) file lives here. `husk info`/`husk
// export` cases that exist to prove a *crash or corrupted-data path* fails
// cleanly instead moved to tests/test_cli_errors.cpp -- see that file's own
// doc comment for the exact split rationale. Shared byte-builder helpers
// live in tests/test_cli_fixtures.hpp, included by all test_cli*.cpp files.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk info: prints collision_box/collision_sphere_radius/collision_indices/"
          "collision_face_normals, not just collision_positions") {
    auto path = tempPath("collision.m2");
    writeFile(path, tinyValidM2());

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("collision_box: min=") != std::string::npos);
    CHECK(result.output.find("collision_sphere_radius: ") != std::string::npos);
    CHECK(result.output.find("collision_positions: ") != std::string::npos);
    CHECK(result.output.find("collision_indices: ") != std::string::npos);
    CHECK(result.output.find("collision_face_normals: ") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: global_flags prints named bits alongside the raw hex value") {
    auto m2 = minimalMd20();
    // tilt_x (0x1) | load_phys_data (0x20) | new_particle_record (0x200) --
    // three bits spanning the reserved-gap/version-gated boundaries in
    // GlobalFlag's own bit layout, not just the first bit.
    uint32_t globalFlags = 0x1 | 0x20 | 0x200;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);
    auto path = tempPath("global-flags.m2");
    writeFile(path, m2);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("global_flags: 0x221") != std::string::npos);
    CHECK(result.output.find("tilt_x") != std::string::npos);
    CHECK(result.output.find("load_phys_data") != std::string::npos);
    CHECK(result.output.find("new_particle_record") != std::string::npos);
    // A bit that isn't set must not show up in the name list.
    CHECK(result.output.find("camera_related") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: global_flags with no bits set prints \"(none set)\", not an empty parenthesis") {
    auto path = tempPath("no-global-flags.m2");
    writeFile(path, minimalMd20());  // globalFlags = 0

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("global_flags: 0x0 (none set)") != std::string::npos);

    fs::remove(path);
}

// textureCombinerCombos (wowdev.wiki M2#Header) only exists in the wire
// header at all when flag_use_texture_combiner_combos (0x8) is set. Builds
// a real header past minimalMd20()'s own 0x130-byte end: the array
// descriptor at 0x130 pointing at 3 real uint16 values appended right
// after it.
// TODO: Remove: former RO_COMPLETENESS_TODO.md Item 2b.
TEST_CASE("husk info: textureCombinerCombos is read and printed when "
          "flag_use_texture_combiner_combos is set") {
    auto b = minimalMd20();
    uint32_t globalFlags = 0x8;  // flag_use_texture_combiner_combos
    std::memcpy(b.data() + 0x010, &globalFlags, 4);
    uint32_t count = 3;
    uint32_t offset = static_cast<uint32_t>(b.size() + 8);
    b.resize(b.size() + 8);
    std::memcpy(b.data() + 0x130, &count, 4);
    std::memcpy(b.data() + 0x134, &offset, 4);
    putU16(b, 5);
    putU16(b, 6);
    putU16(b, 7);
    auto path = tempPath("texture-combiner-combos.m2");
    writeFile(path, b);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("textureCombinerCombos: 3") != std::string::npos);
    CHECK(result.output.find("5 6 7") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: textureCombinerCombos is absent (not printed) when the flag isn't set, "
          "even though the header is otherwise unchanged") {
    auto path = tempPath("no-texture-combiner-combos.m2");
    writeFile(path, minimalMd20());  // globalFlags = 0 -- flag unset

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("textureCombinerCombos") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: flag_use_texture_combiner_combos set but the blob too short for the array "
          "fails cleanly, not a silent misread") {
    auto b = minimalMd20();
    uint32_t globalFlags = 0x8;
    std::memcpy(b.data() + 0x010, &globalFlags, 4);
    // No bytes appended past 0x130 -- the array descriptor itself doesn't fit.
    auto path = tempPath("truncated-texture-combiner-combos.m2");
    writeFile(path, b);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("textureCombinerCombos") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: flags a chunk tag that isn't in husk's known M2 chunk list") {
    // "ZZZZ" stands in for whatever chunk a future client build adds that
    // isn't yet in cmd_info.cpp's documentedM2ChunkTags -- see that file's
    // comment and README.md's Design notes for why this format keeps
    // growing new top-level chunks. The point of this test isn't "ZZZZ"
    // itself, it's proving the diagnostic path actually fires end-to-end
    // through the real CLI, not just at the parser level (see
    // tests/test_m2.cpp's chunkTags tests for that half).
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "ZZZZ");
    putU32(bytes, 4);
    putU32(bytes, 0xDEADBEEF);

    auto path = tempPath("undocumented-chunk.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("ZZZZ") != std::string::npos);
    CHECK(result.output.find("not in husk's known M2 chunk list") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: a real, fully-documented chunk set gets no undocumented-chunk note") {
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "SFID");  // a real, documented tag -- see cmd_info.cpp
    putU32(bytes, 0);

    auto path = tempPath("documented-chunk.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("SFID") != std::string::npos);  // still listed under "chunks:"
    CHECK(result.output.find("not in husk's known M2 chunk list") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: prints skin_file_data_ids/lod_count/bone_file_data_ids/anim_file_ids when "
          "their chunks are present") {
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());

    putTag(bytes, "SFID");
    putU32(bytes, 8);
    putU32(bytes, 469824);
    putU32(bytes, 469830);

    putTag(bytes, "LDV1");
    putU32(bytes, 4);
    putU16(bytes, 8);  // unk0
    putU16(bytes, 3);  // lodCount

    putTag(bytes, "BFID");
    putU32(bytes, 4);
    putU32(bytes, 777001);

    putTag(bytes, "AFID");
    putU32(bytes, 8);
    putU16(bytes, 120);      // anim_id
    putU16(bytes, 0);        // sub_anim_id
    putU32(bytes, 469839);   // file_id

    auto path = tempPath("full-sidecars.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("skin_file_data_ids: 469824, 469830") != std::string::npos);
    CHECK(result.output.find("lod_count: 3") != std::string::npos);
    CHECK(result.output.find("bone_file_data_ids: 1") != std::string::npos);
    CHECK(result.output.find("anim_file_ids: 1") != std::string::npos);

    fs::remove(path);
}

// parseBones/parseSequences/parseRibbons' fixed record strides are only
// documented/verified for Wrath+ (version 264); expansionForVersion already
// recognizes and labels Classic (256-257)/TBC (260-263), so a version below
// Wrath gets a loud warning rather than silently trusting an unverified
// stride.
// TODO: Remove: regression tests for FAILURES2.md #3.
TEST_CASE("husk info: a version below Wrath (264) prints a loud warning") {
    auto path = tempPath("pre-wrath.m2");
    writeFile(path, minimalMd20(/*version=*/260));  // The Burning Crusade

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Wrath") != std::string::npos);
    CHECK(result.output.find("260") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: a Wrath+ version prints no such warning") {
    auto path = tempPath("post-wrath.m2");
    writeFile(path, minimalMd20(/*version=*/274));

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Wrath") == std::string::npos);

    fs::remove(path);
}

// `husk info` prints per-texture/per-material detail (parseTextures/
// parseMaterials), matching attachments/events/lights/ribbons, which all
// get per-record detail; Header::textureFileDataIds (the TXID chunk,
// already resolved and used internally by `husk export --textures`) is
// printed too, matching every other sidecar FileDataID list.
// TODO: Remove: regression test for FAILURES2.md #4.
TEST_CASE("husk info: prints per-texture type/flags/filename, per-material flags/blend_mode, and "
          "texture_file_data_ids when TXID is present") {
    auto md20 = minimalMd20();

    // textures[0]: type=0 (real embedded filename), flags=0, name "foo.blp".
    // textures[1]: type=1 (runtime-substituted -- filename shouldn't print).
    uint32_t nameOff = static_cast<uint32_t>(md20.size());
    std::string name = "foo.blp";
    md20.insert(md20.end(), name.begin(), name.end());
    md20.push_back(0);  // trailing NUL, trimmed by readName

    uint32_t texOff = static_cast<uint32_t>(md20.size());
    putU32(md20, 0);                                     // textures[0].type
    putU32(md20, 0);                                      // textures[0].flags
    putU32(md20, static_cast<uint32_t>(name.size() + 1));  // textures[0].filename.count
    putU32(md20, nameOff);                                 // textures[0].filename.offset
    putU32(md20, 1);                                       // textures[1].type
    putU32(md20, 0);                                       // textures[1].flags
    putU32(md20, 0);                                       // textures[1].filename.count
    putU32(md20, 0);                                       // textures[1].filename.offset
    uint32_t two = 2;
    std::memcpy(md20.data() + 0x050, &two, 4);   // textures.count
    std::memcpy(md20.data() + 0x054, &texOff, 4);  // textures.offset

    // materials[0]: flags=0x04 (two-sided), blendMode=2 (Alpha).
    uint32_t matOff = static_cast<uint32_t>(md20.size());
    putU16(md20, 0x04);
    putU16(md20, 2);
    uint32_t one = 1;
    std::memcpy(md20.data() + 0x070, &one, 4);
    std::memcpy(md20.data() + 0x074, &matOff, 4);

    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "TXID");
    putU32(bytes, 8);
    putU32(bytes, 1034713);  // textures[0]'s FileDataID
    putU32(bytes, 0);        // textures[1]: not file-based (type != 0)

    auto path = tempPath("textures-materials.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("texture 0: type=0 flags=0x0 filename=foo.blp file_data_id=1034713") !=
          std::string::npos);
    // texture 1 has type != 0 -- filename must NOT print (it's a runtime
    // substitution slot, not a real path -- see m2::Texture's doc comment),
    // and file_data_id must not print either (its TXID entry is 0, "none").
    CHECK(result.output.find("texture 1: type=1 flags=0x0") != std::string::npos);
    CHECK(result.output.find("texture 1: type=1 flags=0x0 filename") == std::string::npos);
    CHECK(result.output.find("texture 1: type=1 flags=0x0 file_data_id") == std::string::npos);
    CHECK(result.output.find("material 0: flags=0x4 blend_mode=2") != std::string::npos);
    CHECK(result.output.find("texture_file_data_ids: 1034713, 0") != std::string::npos);

    fs::remove(path);
}

// The five uint16 lookup arrays (sequence/bone/texture/attachment/camera)
// used to be parsed into Array descriptors and never dereferenced anywhere
// (TODO_correctness.md's former item 2). Builds a real header with all five
// populated -- each with one resolvable entry and one 0xFFFF ("-1", "not
// used") sentinel -- past minimalMd20()'s 0x130-byte end, and points each
// Array descriptor at its own two-entry payload.
TEST_CASE("husk info: dereferences sequence_lookup/bone_lookup/texture_lookup/attachment_lookup/"
          "camera_lookup, resolving names and skipping 0xFFFF sentinels") {
    auto b = minimalMd20();

    auto appendLookup = [&b](size_t descriptorOffset, uint16_t resolvedValue) {
        uint32_t count = 2;
        uint32_t offset = static_cast<uint32_t>(b.size());
        std::memcpy(b.data() + descriptorOffset, &count, 4);
        std::memcpy(b.data() + descriptorOffset + 4, &offset, 4);
        putU16(b, resolvedValue);
        putU16(b, 0xFFFF);  // "-1": no entry for bucket/id/type 1
    };

    appendLookup(0x024, 1);   // sequence_lookup: bucket 0 -> sequence[1]
    appendLookup(0x034, 4);   // bone_lookup: key bone 0 (ArmL) -> bone 4
    appendLookup(0x0F8, 3);   // attachment_lookup: id 0 (Shield) -> attachment 3
    appendLookup(0x118, 0);   // camera_lookup: type 0 -> camera 0

    // texture_lookup (0x068): textureTypeName(0) is deliberately nullptr (a
    // real embedded filename, not a named replaceable slot) -- put the
    // sentinel at type 0 and the resolvable entry at type 1 ("skin")
    // instead, so this test also exercises a real name resolving.
    {
        uint32_t count = 2;
        uint32_t offset = static_cast<uint32_t>(b.size());
        std::memcpy(b.data() + 0x068, &count, 4);
        std::memcpy(b.data() + 0x06C, &offset, 4);
        putU16(b, 0xFFFF);
        putU16(b, 2);
    }

    auto path = tempPath("lookup-tables.m2");
    writeFile(path, b);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("sequence_lookup: 2") != std::string::npos);
    CHECK(result.output.find("bucket 0 -> sequence[1]") != std::string::npos);
    CHECK(result.output.find("bucket 1") == std::string::npos);
    CHECK(result.output.find("bone_lookup: 2") != std::string::npos);
    CHECK(result.output.find("key bone 0 (ArmL) -> bone 4") != std::string::npos);
    CHECK(result.output.find("key bone 1") == std::string::npos);
    CHECK(result.output.find("texture_lookup: 2") != std::string::npos);
    CHECK(result.output.find("texture type 1 (skin) -> texture 2") != std::string::npos);
    CHECK(result.output.find("texture type 0 ") == std::string::npos);
    CHECK(result.output.find("attachment_lookup: 2") != std::string::npos);
    CHECK(result.output.find("attachment type 0 (Shield) -> attachment 3") != std::string::npos);
    CHECK(result.output.find("attachment type 1") == std::string::npos);
    CHECK(result.output.find("camera_lookup: 2") != std::string::npos);
    CHECK(result.output.find("camera type 0 -> camera 0") != std::string::npos);
    CHECK(result.output.find("camera type 1") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: prints attachments/events/lights/cameras/ribbon_emitters/particle_emitters "
          "counts") {
    auto md20 = minimalMd20();
    auto path = tempPath("counts-only.m2");
    writeFile(path, md20);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attachments: 0") != std::string::npos);
    CHECK(result.output.find("events: 0") != std::string::npos);
    CHECK(result.output.find("lights: 0") != std::string::npos);
    CHECK(result.output.find("cameras: 0") != std::string::npos);
    CHECK(result.output.find("ribbon_emitters: 0") != std::string::npos);
    CHECK(result.output.find("particle_emitters: 0") != std::string::npos);

    fs::remove(path);
}

// Regression coverage for kMinVerifiedParticleVersion: M2Particle's 0x1EC
// byte shape is only real-data-verified for Cataclysm+ (272) -- see
// m2::ParticleEmitter's doc comment. A file below that version with real
// particle_emitters must warn and stay count-only, same shape as the
// existing "below Wrath" bones/sequences/ribbons regression test above.
TEST_CASE("husk info: a particle_emitters array below Cataclysm (272) prints a loud warning and "
          "stays count-only") {
    auto md20 = minimalMd20(/*version=*/264);  // Wrath -- above the bones/ribbons floor, below
                                                 // the particle one
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x128, &count, 4);
    std::memcpy(md20.data() + 0x12C, &off, 4);
    md20.resize(md20.size() + 0x1EC, 0);

    auto path = tempPath("pre-cata-particles.m2");
    writeFile(path, md20);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Cataclysm") != std::string::npos);
    CHECK(result.output.find("particle_emitters: 1") != std::string::npos);
    CHECK(result.output.find("particleId=") == std::string::npos);  // never parsed structurally

    fs::remove(path);
}

TEST_CASE("husk info: a Cata+ particle_emitters array prints no version warning and resolves "
          "real fields") {
    auto md20 = minimalMd20(/*version=*/274);
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x128, &count, 4);
    std::memcpy(md20.data() + 0x12C, &off, 4);
    md20.resize(md20.size() + 0x1EC, 0);
    md20[off + 0x28] = 4;  // blendingType
    md20[off + 0x29] = 1;  // emitterType

    auto path = tempPath("cata-particles.m2");
    writeFile(path, md20);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Cataclysm") == std::string::npos);
    CHECK(result.output.find("particleId=") != std::string::npos);
    CHECK(result.output.find("blendingType=4 emitterType=1") != std::string::npos);

    fs::remove(path);
}

