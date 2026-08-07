// CLI tier: general flags/error paths -- exercises husk::commands::info/
// exportGlb by spawning the real compiled binary (see run_husk.hpp) against
// small, synthetic, on-disk fixtures -- always run, no real game files or
// HUSK_TEST_* env vars needed. See TEST_DESIGN.md#Four-tier-architecture for
// how this tier relates to the others. Every fixture below targets one
// specific, previously-confirmed-broken behavior; if any of these start
// failing again, it's a real regression, not a flake.
//
// TODO: Remove: this file exists because of a real, confirmed gap
// (FAILURES.md #5) -- cmd_info.cpp/cmd_export.cpp, the only place several
// bugs actually lived (FAILURES.md #1-#4), had zero committed test
// coverage that didn't require a personal WoW install.
//
// --skin/--skin-dir/--lod, --anim/.skel-sourced animation, --skel/
// --bones-dir, and --phys cases moved to tests/test_cli_skin.cpp/
// test_cli_anim.cpp/test_cli_skel.cpp/test_cli_phys.cpp respectively
// (FILE_SPLIT_TODO.md Item 5, CLI-flag-shaped, not module-shaped -- this
// file keeps general flags/error-path cases plus `husk info` output tests
// that aren't specific to one of those four families). Shared byte-builder
// helpers live in tests/test_cli_fixtures.hpp, included by all five files.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk info: directory as path fails cleanly, not a crash") {
    auto result = runHusk("info " + fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);
}

TEST_CASE("husk info: chunked file with a truncated trailing chunk header fails cleanly, not a "
          "crash") {
    // Valid MD21 wrapper around a minimal MD20 blob, followed by a
    // truncated second chunk header (a tag with no size field) -- used to
    // throw husk::ChunkError straight through info()'s then-too-narrow
    // catch and abort the whole process.
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "SKID");
    bytes.push_back(0xAA);  // 1 of 4 size bytes -- truncated

    auto path = tempPath("chunked-truncated.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: generic non-M2 garbage fails cleanly, not a crash") {
    // Not MD20, and not a well-formed chunk stream either -- exercises
    // "falls through to 'maybe this is chunked', then runs out of buffer
    // mid-header", using nothing more exotic than a wrong magic and
    // zero-filled padding. This is the realistic case: pointing husk at the
    // wrong file entirely, not a hand-crafted one.
    // TODO: Remove: FAILURES.md #1.
    std::vector<uint8_t> bytes;
    putTag(bytes, "XXXX");
    bytes.resize(300, 0);

    auto path = tempPath("garbage.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk export: corrupted huge vertex count fails with a real message, not "
          "std::bad_alloc") {
    auto m2 = minimalMd20();
    uint32_t count = 0xFFFFFFF0;
    uint32_t off = 0;
    std::memcpy(m2.data() + 0x03C, &count, 4);
    std::memcpy(m2.data() + 0x040, &off, 4);
    auto m2Path = tempPath("huge-vertices.m2");
    writeFile(m2Path, m2);

    // m2::parseVertices runs (and, pre-fix, would have OOM'd) before
    // cmd_export.cpp ever opens the .skin file, so this path doesn't need
    // to exist or be valid.
    auto result = runHusk("export " + m2Path.string() + " --skin /nonexistent.skin -o " +
                           tempPath("huge-vertices.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("vertices array claims") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: corrupted huge bone count fails with a real message, not "
          "std::bad_alloc") {
    auto m2 = tinyValidM2();
    uint32_t count = 0xFFFFFFF0;
    uint32_t off = 0;
    std::memcpy(m2.data() + 0x02C, &count, 4);
    std::memcpy(m2.data() + 0x030, &off, 4);
    auto m2Path = tempPath("huge-bones.m2");
    writeFile(m2Path, m2);

    auto skinPath = tempPath("huge-bones.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("huge-bones.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("bones array claims") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: .skin file with a corrupted huge indices count fails with a real "
          "message, not std::bad_alloc") {
    auto m2Path = tempPath("for-huge-skin-indices.m2");
    writeFile(m2Path, minimalMd20());  // 0 vertices -- never reached, indices fails first

    std::vector<uint8_t> skin;
    putTag(skin, "SKIN");
    putU32(skin, 0);
    putU32(skin, 0);           // vertices: count=0, offset=0
    putU32(skin, 0xFFFFFFF0);  // indices: corrupted huge count
    putU32(skin, 8);
    putU32(skin, 0);
    putU32(skin, 0);  // bones: count=0, offset=0
    putU32(skin, 0);
    putU32(skin, 0);  // submeshes: count=0, offset=0
    putU32(skin, 0);
    putU32(skin, 0);  // batches: count=0, offset=0
    auto skinPath = tempPath("huge-indices.skin");
    writeFile(skinPath, skin);

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("huge-skin-indices.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("uint16 entries") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a 2-cycle in the bones' parent chain is rejected, not silently "
          "exported") {
    auto m2Path = tempPath("cycle.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("cycle.skin");
    writeFile(skinPath, tinyMatchingSkin());
    // Bone 0's parent is bone 1, bone 1's parent is bone 0: every
    // individual parentBone value is in-range and non-self-referential, so
    // only chain-walking (not a plain range check) can catch this.
    auto skelPath = tempPath("cycle.skel");
    writeFile(skelPath, buildSkel({{-1, 1}, {-1, 0}}));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("cycle.glb").string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("loops back") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a bone that is its own parent (a 1-node cycle) is still rejected, "
          "guarded against regressing while fixing the longer-cycle case above") {
    // A self-parent is the degenerate 1-node case of a bone-parent-chain
    // cycle. It was already caught before this fix, by a dedicated check
    // in gltf::writeGlb -- cmd_export.cpp's new checkNoBoneCycles() (in
    // buildSkeleton(), which runs first) now catches it too and throws
    // its own "loops back on itself" first, so writeGlb's check never
    // gets a chance to fire for this particular caller. That's fine --
    // writeGlb's check is still live for any caller that skips
    // buildSkeleton() -- but it does mean this test should expect the
    // newer message, not the older one.
    auto m2Path = tempPath("self-parent.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("self-parent.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("self-parent.skel");
    writeFile(skelPath, buildSkel({{-1, 0}}));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("self-parent.glb").string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("loops back") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

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

TEST_CASE("husk export: non-finite (NaN/Inf) vertex position is rejected, not silently baked "
          "into the glb") {
    auto m2 = tinyValidM2();
    size_t vertexOff = m2.size() - 0x30;
    uint32_t nanBits = 0x7FC00000;  // quiet NaN
    uint32_t infBits = 0x7F800000;  // +Infinity
    std::memcpy(m2.data() + vertexOff + 0x00, &nanBits, 4);
    std::memcpy(m2.data() + vertexOff + 0x04, &infBits, 4);
    auto m2Path = tempPath("nan-vertex.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("nan-vertex.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("nan-vertex.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("non-finite") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
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

// CLI default-resolution tests: `husk export <file.m2>` alone (no .skin,
// output, .skel, or --textures/--skin-dir/--anim) should resolve
// everything it reasonably can from what's already sitting next to the
// model, rather than requiring every argument spelled out even when it's
// exactly where the tool could have found it itself. Each fixture below
// gets its own dedicated subdirectory (not the shared system temp root
// tempPath() otherwise writes into) so directory-scan-based defaulting has
// a clean, isolated view -- no risk of an unrelated file from a different
// test case being picked up.

// Export-side version of the two `husk info` cases above.
// TODO: Remove: regression test for FAILURES2.md #3.
TEST_CASE("husk export: a version below Wrath (264) prints a loud warning") {
    auto dir = defaultsDir("prewrathexport");
    auto md20 = minimalMd20(/*version=*/256);  // Classic
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x03C, &count, 4);
    std::memcpy(md20.data() + 0x040, &off, 4);
    md20.resize(md20.size() + 0x30, 0);
    writeFile(dir / "classic.m2", md20);
    writeFile(dir / "classic00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "classic.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Wrath") != std::string::npos);
    CHECK(result.output.find("256") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: model path alone resolves a same-basename .skin and defaults the output "
          "path, end to end") {
    auto dir = defaultsDir("basic");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "basic.m2").string());
    CHECK(result.exitCode == 0);
    // --skin's default is "auto" (not a separate "no .skin path given" note
    // the old positional grammar printed) -- resolveSkin's own success note
    // covers this instead.
    CHECK(result.output.find("'auto' resolved") != std::string::npos);
    CHECK(result.output.find("same-basename numbered scan") != std::string::npos);
    CHECK(result.output.find("no output path given") != std::string::npos);
    CHECK(fs::exists(dir / "basic.glb"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: default .skin resolution never matches a different model's file that "
          "merely extends this one's name as a string (the _hd-variant trap)") {
    auto dir = defaultsDir("hdtrap");
    writeFile(dir / "hero.m2", tinyValidM2());
    writeFile(dir / "hero00.skin", tinyMatchingSkin());
    // A real, unrelated, much-higher-poly model -- "hero_hd" starts with
    // "hero" as a plain string, but the character right after the basename
    // is '_', not a digit, so it must never be picked for hero.m2.
    writeFile(dir / "hero_hd00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "hero.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("hero00.skin") != std::string::npos);
    CHECK(result.output.find("hero_hd00.skin") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: multiple same-basename .skin candidates resolves to the lowest-numbered "
          "one, and says so") {
    auto dir = defaultsDir("ambiguous");
    writeFile(dir / "multi.m2", tinyValidM2());
    writeFile(dir / "multi01.skin", tinyMatchingSkin());
    writeFile(dir / "multi00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "multi.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("multi00.skin") != std::string::npos);
    CHECK(result.output.find("2 same-basename .skin files") != std::string::npos);

    fs::remove_all(dir);
}

// Regression test: a real corpus scan found
// findSameBasenameSkins silently pairing "mogu_library_crate_10.m2" with
// "mogu_library_crate_100.skin" -- which actually belongs to the shorter
// sibling model "mogu_library_crate_1.m2" ("...crate_1" + "00" LOD suffix),
// not "...crate_10" ("...crate_10" + "0", a spurious 1-digit match that
// used to tie with the real "...crate_1000.skin" ("...crate_10" + "00")
// candidate and lose the std::sort tie-break (lexicographically, "0" <
// "00.skin"'s leading "0" but ".skin" < "0" in ASCII, so the *wrong* file
// sorted first). Reproduced here with a minimal same-shape pair: "crate_1"/
// "crate_10" siblings, each with its own real 2-digit-suffix .skin.
TEST_CASE("husk export: a model basename that's a numeric-suffix prefix of a sibling model's own "
          "basename resolves its own 2-digit-suffix .skin, not the sibling's colliding shorter/"
          "longer match") {
    auto dir = defaultsDir("basenamecollision");
    writeFile(dir / "crate_1.m2", tinyValidM2());
    writeFile(dir / "crate_10.m2", tinyValidM2());
    writeFile(dir / "crate_100.skin", tinyMatchingSkin());   // crate_1's real skin: "crate_1"+"00"
    writeFile(dir / "crate_1000.skin", tinyMatchingSkin());  // crate_10's real skin: "crate_10"+"00"

    auto shortResult = runHusk("export " + (dir / "crate_1.m2").string());
    CHECK(shortResult.exitCode == 0);
    CHECK(shortResult.output.find("crate_100.skin") != std::string::npos);

    auto longResult = runHusk("export " + (dir / "crate_10.m2").string());
    CHECK(longResult.exitCode == 0);
    CHECK(longResult.output.find("crate_1000.skin") != std::string::npos);

    fs::remove_all(dir);
}

// Regression test for the second fix above: a wrong-
// .skin pairing references hundreds of out-of-range vertex indices in real
// corpus files, not one -- the error message now names how many and the
// worst offender, not just the first index iteration happened to hit.
TEST_CASE("husk export: a .skin referencing multiple out-of-range M2 vertices reports the count "
          "and the worst offender, not just the first") {
    auto dir = defaultsDir("outofrangevtx");
    writeFile(dir / "mismatch.m2", tinyValidM2());
    writeFile(dir / "mismatch00.skin", outOfRangeVertexSkin());

    auto result = runHusk("export " + (dir / "mismatch.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("references 6 out-of-range M2 vertex index(es)") != std::string::npos);
    CHECK(result.output.find("up to 6") != std::string::npos);
    CHECK(result.output.find("only has 1 vertices") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: no .skin path given and none found next to the model fails cleanly, "
          "naming what was expected") {
    auto dir = defaultsDir("noskin");
    writeFile(dir / "lonely.m2", tinyValidM2());

    auto result = runHusk("export " + (dir / "lonely.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("no same-named") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a collision mesh is attached by default when present") {
    auto m2Path = tempPath("collision-default.m2");
    writeFile(m2Path, tinyValidM2WithCollision());
    auto skinPath = tempPath("collision-default.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("collision-default.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached a 3-position/1-triangle collision mesh") !=
          std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --collision none omits the collision mesh even when the model has "
          "one") {
    auto m2Path = tempPath("collision-none.m2");
    writeFile(m2Path, tinyValidM2WithCollision());
    auto skinPath = tempPath("collision-none.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("collision-none.glb").string() + " --collision none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("collision mesh") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --collision only accepts 'none', rejecting any other value with a clear "
          "message") {
    auto m2Path = tempPath("collision-bad.m2");
    writeFile(m2Path, tinyValidM2WithCollision());
    auto skinPath = tempPath("collision-bad.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("collision-bad.glb").string() + " --collision bogus");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--collision") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --textures defaults to the model's own directory -- a FileDataID-named "
          "file already sitting there is embedded without passing the flag") {
    auto dir = defaultsDir("textures");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});  // fake PNG bytes -- husk embeds raw, doesn't decode

    auto result = runHusk("export " + (dir / "textured.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --textures none never embeds an image, even when a matching "
          "<FileDataID>.png sits in the default (model's own) directory") {
    auto dir = defaultsDir("texturesnone");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "textured.m2").string() + " --textures none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("0 with an embedded texture") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a hardcoded texture slot's material name includes its real semantic "
          "type name") {
    auto m2Path = tempPath("typename.m2");
    writeFile(m2Path, oneTexturedModelWithType(6));  // 6 = TEX_COMPONENT_CHAR_HAIR
    auto skinPath = tempPath("typename.skin");
    writeFile(skinPath, oneTexturedModelSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("typename.glb").string());
    CHECK(result.exitCode == 0);

    fs::path glbPath = tempPath("typename.glb");
    std::ifstream glbFile(glbPath, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("char_hair") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(glbPath);
}

TEST_CASE("husk export: a hardcoded texture slot embeds the sole basename-matching file in "
          "--textures when husk can't resolve a FileDataID, and warns that the match is "
          "non-deterministic and has no FileDataID to cross-reference") {
    auto dir = defaultsDir("fuzzytex-sole");
    writeFile(dir / "fuzzytex.m2", oneTexturedModelWithType(1));  // 1 = TEX_COMPONENT_SKIN
    writeFile(dir / "fuzzytex00.skin", oneTexturedModelSkin());
    writeFile(dir / "fuzzytexfaceupper00_00_hd.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "fuzzytex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);
    CHECK(result.output.find("husk: warning:") != std::string::npos);
    CHECK(result.output.find("fuzzytexfaceupper00_00_hd.png") != std::string::npos);
    CHECK(result.output.find("non-deterministic basename matching") != std::string::npos);
    CHECK(result.output.find("no FileDataID at all for this hardcoded slot") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a FileDataID-exact texture match prints no fuzzy-match warning "
          "(deterministic, nothing to double-check)") {
    auto dir = defaultsDir("fdid-no-warning");
    writeFile(dir / "fdidtex.m2", oneTexturedModel(1034713));
    writeFile(dir / "fdidtex00.skin", oneTexturedModelSkin());
    writeFile(dir / "1034713.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "fdidtex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);
    CHECK(result.output.find("husk: warning:") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: two basename-matching candidates for one hardcoded slot embed neither "
          "and are reported, not guessed at") {
    auto dir = defaultsDir("fuzzytex-ambiguous");
    writeFile(dir / "fuzzytex.m2", oneTexturedModelWithType(1));
    writeFile(dir / "fuzzytex00.skin", oneTexturedModelSkin());
    writeFile(dir / "fuzzytexfaceupper00_00_hd.png", {1, 2, 3, 4});
    writeFile(dir / "fuzzytexhair00_00.png", {5, 6, 7, 8});

    auto result = runHusk("export " + (dir / "fuzzytex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("0 with an embedded texture") != std::string::npos);
    CHECK(result.output.find("2 texture file(s)") != std::string::npos);
    CHECK(result.output.find("can't tell which hardcoded texture slot") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a fdid-resolvable slot keeps its own FileDataID-named file even when a "
          "hardcoded slot in the same model claims a real-named file from the shared fuzzy pool "
          "(regression: an earlier version of the name-priority fix let this cross-contaminate)") {
    auto dir = defaultsDir("mixedtex");
    writeFile(dir / "mixedtex.m2", twoTexturedModel(1034713));
    writeFile(dir / "mixedtex00.skin", twoBatchSkin());
    // texture[1]'s own FileDataID-named file (deterministic match).
    writeFile(dir / "1034713.png", {'F', 'D', 'I', 'D'});
    // A real-named file sharing the model's basename -- the only thing
    // texture[0]'s hardcoded slot can ever resolve to, and the only real
    // candidate in the fuzzy pool.
    writeFile(dir / "mixedtexfaceupper00_00_hd.png", {'N', 'A', 'M', 'E'});

    auto result = runHusk("export " + (dir / "mixedtex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 with an embedded texture") != std::string::npos);

    std::ifstream glbFile(dir / "mixedtex.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    // Both real images present, each exactly once -- neither slot's file
    // got embedded into the other's material instead.
    CHECK(text.find("FDID") != std::string::npos);
    CHECK(text.find("NAME") != std::string::npos);
    // The fdid-resolvable slot's own material name still carries its real
    // FileDataID -- it wasn't renamed to the fuzzy-matched file's name.
    CHECK(text.find("_fdid1034713") != std::string::npos);
    CHECK(text.find("_mixedtexfaceupper00_00_hd") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a fdid-resolvable slot with no matching \"<fdid>.png\" file falls through "
          "to a real basename-matching file instead of embedding nothing (previously: silent gap)") {
    auto dir = defaultsDir("fallbacktex");
    writeFile(dir / "fallbacktex.m2", oneTexturedModel(9999999));  // no 9999999.png on disk
    writeFile(dir / "fallbacktex00.skin", oneTexturedModelSkin());
    writeFile(dir / "fallbacktexfaceupper00_00_hd.png", {'N', 'A', 'M', 'E'});

    auto result = runHusk("export " + (dir / "fallbacktex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);
    // Fell through to the fuzzy match, so it gets the same warning as any
    // other non-deterministic match -- this time naming the resolved
    // FileDataID it couldn't be cross-checked against, since one exists
    // (unlike a genuinely hardcoded slot).
    CHECK(result.output.find("husk: warning:") != std::string::npos);
    CHECK(result.output.find("resolved FileDataID 9999999") != std::string::npos);

    std::ifstream glbFile(dir / "fallbacktex.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("NAME") != std::string::npos);
    // The real FileDataID is still recorded (material name + extras),
    // even though the embedded bytes came from the named file, not
    // "9999999.png" -- see gltf.hpp's Material::baseColorTextureFileDataId
    // doc comment.
    CHECK(text.find("_fdid9999999") != std::string::npos);
    CHECK(text.find("texture_file_data_id") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batches spanning more than one distinct skinSectionId (geoset ID) print a "
          "loud note naming them") {
    auto dir = defaultsDir("geosets");
    writeFile(dir / "geoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "geoset00.skin", twoGeosetSkin());

    auto result = runHusk("export " + (dir / "geoset.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 distinct geoset IDs") != std::string::npos);
    CHECK(result.output.find("skinSectionId: 0, 401") != std::string::npos);
    CHECK(result.output.find("doesn't filter geosets yet") != std::string::npos);

    fs::remove_all(dir);
}

// The same-skinSectionId case (the ordinary shape -- every submesh really is
// part of one consistent mesh) must stay quiet: no note at all.
TEST_CASE("husk export: batches that all share one skinSectionId print no geoset note") {
    auto dir = defaultsDir("samegeoset");
    auto skin = twoGeosetSkin();
    // Patch submesh 1's skinSectionId (offset 60 + 0x30 + 0x00) from 401 to
    // 0, matching submesh 0 -- same fixture, single-geoset shape.
    uint16_t zero16 = 0;
    std::memcpy(skin.data() + 60 + 0x30 + 0x00, &zero16, 2);
    writeFile(dir / "onegeoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "onegeoset00.skin", skin);

    auto result = runHusk("export " + (dir / "onegeoset.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("distinct geoset IDs") == std::string::npos);

    fs::remove_all(dir);
}

// Regression tests: a genuinely geometry-less M2 (0
// vertices, an empty .skin) used to make buildMaterialsAndPrimitives
// manufacture one primitive with empty `indices`, which writeGlbMulti's
// hard "primitive indices must not be empty" check then rejected outright
// -- 3,807 real corpus files (particle/ribbon-only VFX models) failed this
// way. Fixed by skipping mesh output for a geometry-less LOD tier entirely
// (see cmd_export.cpp/gltf.cpp) rather than trying to represent "zero
// triangles" as a mesh at all.
TEST_CASE("husk export: a genuinely geometry-less model (0 vertices, empty .skin) exports "
          "successfully with no mesh, keeping its skeleton") {
    auto dir = defaultsDir("novtx");
    writeFile(dir / "vfx.m2", zeroVertexOneBoneM2());
    writeFile(dir / "vfx00.skin", emptySkin());

    auto result = runHusk("export " + (dir / "vfx.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("no renderable geometry") != std::string::npos);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(fs::exists(dir / "vfx.glb"));
    CHECK(fs::file_size(dir / "vfx.glb") > 0);

    fs::remove_all(dir);
}

// A model with genuinely nothing to export at all -- 0 vertices, 0 bones --
// still fails loudly (writeGlbMulti's "meshes must not be empty without a
// skeleton to fall back to" case): there's no mesh *and* no skeleton for the
// zero-mesh path above to fall back to, so this isn't the real corpus shape
// (every real particle-only file found in the corpus had at least one bone)
// and should stay a hard error rather than silently emitting an empty glTF.
TEST_CASE("husk export: a model with 0 vertices and 0 bones (nothing at all to export) still "
          "fails cleanly") {
    auto dir = defaultsDir("nothingatall");
    writeFile(dir / "nothing.m2", minimalMd20());
    writeFile(dir / "nothing00.skin", emptySkin());

    auto result = runHusk("export " + (dir / "nothing.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);

    fs::remove_all(dir);
}

// TODO: Remove: regression test for FAILURES2.md #6 -- a batch with
// textureCount > 1 used to be silently reduced to a single texture with
// zero indication anything was dropped.
TEST_CASE("husk export: a batch with textureCount > 1 prints a note that extra texture layers "
          "are dropped") {
    auto dir = defaultsDir("multitex");
    writeFile(dir / "shiny.m2", oneTextureOneMaterialM2());
    writeFile(dir / "shiny00.skin", oneBatchTwoTexturesSkin());

    auto result = runHusk("export " + (dir / "shiny.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) with more than one texture") != std::string::npos);
    CHECK(result.output.find("additional layers are exported as inert 'extras' metadata") !=
          std::string::npos);

    fs::remove_all(dir);
}

// The ordinary (single-texture) case must stay quiet.
TEST_CASE("husk export: a batch with textureCount == 1 prints no multi-texture note") {
    auto dir = defaultsDir("singletex");
    writeFile(dir / "plain.m2", oneTextureOneMaterialM2());
    auto skin = oneBatchTwoTexturesSkin();
    uint16_t one16 = 1;
    // textureCount lives at submesh-block-end + batch offset 0x0E (see
    // oneBatchTwoTexturesSkin's own layout comment: 52 + 0x30 is the batch's
    // start).
    std::memcpy(skin.data() + 52 + 0x30 + 0x0E, &one16, 2);
    writeFile(dir / "plain00.skin", skin);

    auto result = runHusk("export " + (dir / "plain.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("more than one texture") == std::string::npos);

    fs::remove_all(dir);
}

// A bone track whose global_sequence field is set (continuous,
// M2Sequence-independent looping animation -- glow pulses, idle sway)
// resolves to a real global-sequence clip via
// `m2::resolveVec3GlobalSequenceTrack`/`buildGlobalSequenceAnimations`
// (src/m2.cpp, src/cmd_export.cpp) -- see m2::TrackMeta's doc comment for
// why it must not be misattributed to whichever M2Sequence happens to
// occupy outer-array position 0. This checks it end to end through the
// real CLI, not just the underlying parser (see tests/test_m2.cpp for
// that).
// TODO: Remove: regression test for FAILURES2.md #7.
TEST_CASE("husk export: a global-sequence-driven bone track resolves to a real animation clip") {
    auto m2 = tinyValidM2();
    uint32_t boneOff = static_cast<uint32_t>(m2.size());
    uint32_t boneCount = 1;
    std::memcpy(m2.data() + 0x02C, &boneCount, 4);
    std::memcpy(m2.data() + 0x030, &boneOff, 4);
    m2.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(m2.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(m2.data() + boneOff + 0x08, &parentBone, 2);

    fillTrack(m2, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    // fillTrack always writes global_sequence = 0xFFFF ("none") at the
    // track's own header -- patch it to a real global-sequence index (3),
    // marking this specific track as global-sequence-driven instead of
    // per-M2Sequence (see fillTrack's own doc comment).
    uint16_t globalSeq = 3;
    std::memcpy(m2.data() + boneOff + 0x10 + 0x02, &globalSeq, 2);
    // Rotation/scale get a trivial single keyframe each, left "none" (
    // fillTrack's default) -- isolates this test to exactly the translation
    // track actually under test.
    fillTrack(m2, boneOff + 0x24, {0}, {identityQuatBytes()});
    fillTrack(m2, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto dir = defaultsDir("globalseq");
    writeFile(dir / "glow.m2", m2);
    writeFile(dir / "glow00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "glow.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

// The ordinary case (tinyAnimatedM2's per-M2Sequence-only bone -- fillTrack's
// default global_sequence is "none") must not gain a phantom extra clip.
TEST_CASE("husk export: a model with no global-sequence-driven tracks gains no extra clip") {
    auto dir = defaultsDir("noglobalseq");
    writeFile(dir / "normal.m2", tinyAnimatedM2());
    writeFile(dir / "normal00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "normal.m2").string());
    CHECK(result.exitCode == 0);
    // tinyAnimatedM2 resolves to exactly 1 real (per-sequence) clip already
    // (see its own doc comment/other tests using it) -- must stay exactly 1,
    // not 2, confirming no spurious global-sequence clip appears when no
    // track is actually global-sequence-driven.
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

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

TEST_CASE("husk export: batch skinSectionIndex out of range for submeshes fails cleanly") {
    auto dir = defaultsDir("badskinsection");
    writeFile(dir / "m.m2", tinyValidM2());
    writeFile(dir / "m.skin", oneBatchSkin({.skinSectionIndex = 1}));  // only submesh 0 exists

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("skinSectionIndex (1) is out of range for 1 submeshes") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: submesh index range past the resolved triangle-index buffer fails "
          "cleanly (corrupted .skin?)") {
    auto dir = defaultsDir("badindexrange");
    writeFile(dir / "m.m2", tinyValidM2());
    // Submesh claims 10 indices; the .skin's own indices array (and thus
    // the resolved triangle-index buffer) only has 3.
    writeFile(dir / "m.skin", oneBatchSkin({}, /*submeshIndexCount=*/10));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("corrupted .skin?") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch materialIndex out of range for materials fails cleanly") {
    auto dir = defaultsDir("badmaterial");
    writeFile(dir / "m.m2", tinyValidM2());       // 0 materials
    writeFile(dir / "m.skin", oneBatchSkin({}));  // materialIndex defaults to 0

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("materialIndex (0) is out of range for 0 materials") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch colorIndex out of range for colors fails cleanly") {
    auto dir = defaultsDir("badcolor");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 1 material, 0 colors
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("colorIndex (0) is out of range for 0 colors") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureWeightComboIndex out of range for textureWeightCombos "
          "fails cleanly") {
    auto dir = defaultsDir("badweightcombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 1, 0, 0, 0));  // 1 textureWeightCombos entry
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 5}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureWeightComboIndex (5) is out of range for 1 "
                              "textureWeightCombos entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch texture weight, resolved via textureWeightCombos, out of range "
          "for textureWeights fails cleanly") {
    auto dir = defaultsDir("badweightresolved");
    // textureWeightCombos[0] = 99, but there are 0 real textureWeights.
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 1, 0, 0, 0, /*textureWeightCombo0=*/99));
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("texture weight (index 99 via textureWeightCombos[0]) is out of "
                              "range for 0 textureWeights entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureComboIndex out of range for textureCombos fails cleanly") {
    auto dir = defaultsDir("badtexturecombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 0 textureCombos
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureComboIndex (0) is out of range for 0 textureCombos "
                              "entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch texture, resolved via textureCombos, out of range for textures "
          "fails cleanly") {
    auto dir = defaultsDir("badtextureresolved");
    // textureCombos[0] = 99, but there are 0 real textures.
    writeFile(dir / "m.m2",
              materialsFixtureM2(1, 0, 0, 0, 0, 1, 0, std::nullopt, /*textureCombo0=*/99));
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("texture (index 99 via textureCombos[0]) is out of range for 0 "
                              "textures") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureCoordComboIndex out of range for textureCoordCombos "
          "fails cleanly") {
    auto dir = defaultsDir("badtexcoordcombo");
    // 1 real texture, textureCombos[0]=0 (valid, points at it), but only
    // 1 textureCoordCombos entry even though the batch claims index 5.
    writeFile(dir / "m.m2",
              materialsFixtureM2(1, 0, 0, 0, 1, 1, 1, std::nullopt, /*textureCombo0=*/0));
    writeFile(dir / "m.skin",
              oneBatchSkin({.textureCount = 1, .textureComboIndex = 0, .textureCoordComboIndex = 5}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureCoordComboIndex (5) is out of range for 1 "
                              "textureCoordCombos entries") != std::string::npos);

    fs::remove_all(dir);
}

// A batch's M2Color/M2TextureWeight can be genuinely animated (per-sequence
// or global-sequence keyframes), the same track shape a bone's
// translation/rotation/scale can be -- but unlike a bone track (a real,
// animatable glTF node property), core glTF has no way to animate a
// material's baseColorFactor at all, so there's no real clip to build.
// These tests confirm husk says so instead of silently exporting the batch
// as if the track were the ordinary constant-value case.
// TODO: Remove: FINDINGS.md §3.2/FAILURES2.md #7.

TEST_CASE("husk export: an animated (non-constant) M2Color track is dropped with a note, not "
          "silently treated as constant") {
    auto dir = defaultsDir("animatedcolor");
    auto m2 = materialsFixtureM2(1, 1, 0, 0, 0, 0, 0);  // 1 material, 1 color
    patchColorTrackAnimated(m2);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) whose color tint (M2Color) or transparency fade") !=
          std::string::npos);
    CHECK(result.output.find("core glTF has no way to animate a material's baseColorFactor") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: an animated (non-constant) M2TextureWeight track is dropped with a "
          "note, not silently treated as constant") {
    auto dir = defaultsDir("animatedweight");
    // 1 material, 1 textureWeight, 1 textureWeightCombos entry (its
    // zeroed default value, 0, already points at textureWeights[0]).
    auto m2 = materialsFixtureM2(1, 0, 1, 1, 0, 0, 0);
    patchWeightTrackAnimated(m2);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) whose color tint (M2Color) or transparency fade") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a constant (non-animated) M2Color track gets no animated-tint note") {
    auto dir = defaultsDir("constantcolor");
    // materialsFixtureM2's zeroed color record has outer.count=0 (empty,
    // not animated) -- the ordinary, already-well-tested case; this just
    // confirms it doesn't spuriously trigger the new note.
    writeFile(dir / "m.m2", materialsFixtureM2(1, 1, 0, 0, 0, 0, 0));
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("color tint") == std::string::npos);

    fs::remove_all(dir);
}

// A batch's textureTransformComboIndex resolving to a real
// M2TextureTransform gets noted and exported as inert extras, not silently
// dropped -- see gltf.hpp's TextureTransform doc comment for why it's
// never applied to the actual render.
// TODO: Remove: FINDINGS.md §3.1.

TEST_CASE("husk export: a batch referencing a texture transform gets a note, and husk info "
          "counts texture_transforms") {
    auto dir = defaultsDir("texturetransform");
    writeFile(dir / "m.m2", oneTextureTransformM2());
    writeFile(dir / "m.skin", oneBatchSkin({.textureTransformComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) with a UV transform (M2TextureTransform)") !=
          std::string::npos);

    auto infoResult = runHusk("info " + (dir / "m.m2").string());
    CHECK(infoResult.exitCode == 0);
    CHECK(infoResult.output.find("texture_transforms: 1 ") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a batch with textureTransformComboIndex = 0xFFFF (none) gets no "
          "UV-transform note") {
    auto dir = defaultsDir("notexturetransform");
    writeFile(dir / "m.m2", oneTextureTransformM2());
    // Default BatchFields::textureTransformComboIndex is 0xFFFF -- the
    // model has a real texture_transforms entry, but this batch doesn't
    // reference it.
    writeFile(dir / "m.skin", oneBatchSkin({}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("UV transform") == std::string::npos);

    fs::remove_all(dir);
}
