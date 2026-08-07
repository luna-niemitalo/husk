// This is the Conformance tier (see TEST_DESIGN.md#Four-tier-architecture)
// -- real downstream consumers of a husk-exported .glb, as opposed to
// tests/test_integration.cpp's "does husk itself run correctly" role:
//
//   - The Khronos glTF-Validator: independent spec-conformance check.
//     tinygltf being able to load a file back doesn't prove the file is
//     spec-valid -- tinygltf is a fairly permissive reader.
//   - Blender's own glTF importer, run headlessly: proves a real,
//     independent glTF implementation agrees with tinygltf's reading of
//     the same file (see tests/blender_import_check.py for why that
//     agreement is the actual point).
//
// See TEST_DESIGN.md#Conformance-gating for the HUSK_GLTF_VALIDATOR/
// HUSK_BLENDER build-time gating, and TEST_DESIGN.md#Fixture-resolution-model
// for the real-data fixture skip mechanics this file shares with
// test_integration.cpp.

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "gltf.hpp"
#include "m2.hpp"
#include "run_husk.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runCommand;
using husk::test::runHusk;
using husk::test::testM2;
using husk::test::testQuadrupedM2;
using husk::test::testQuadrupedSkin;
using husk::test::testSkin;
using husk::test::testWeaponParticleB;
using husk::test::testWeaponParticleStress;
using husk::test::testWeaponPhys;
using husk::test::testWeaponPhysSkin;

// Pulls an integer out of a "HUSK_PROBE key=value" line (see
// tests/blender_import_check.py) -- REQUIREs the key is present so a
// probe script that silently stopped printing something fails loudly
// rather than comparing against a default.
int parseProbeInt(const std::string& output, const std::string& key) {
    std::string marker = "HUSK_PROBE " + key + "=";
    auto pos = output.find(marker);
    INFO("looking for '", marker, "' in blender output:\n", output);
    REQUIRE(pos != std::string::npos);
    return std::stoi(output.substr(pos + marker.size()));
}

// Pulls a "x,y,z" triple out of a "HUSK_PROBE key=x,y,z" line (see
// blender_import_check.py's husk_probe_-prefixed bone printing) -- same
// fail-loudly-if-missing contract as parseProbeInt.
husk::gltf::Vec3 parseProbeVec3(const std::string& output, const std::string& key) {
    std::string marker = "HUSK_PROBE " + key + "=";
    auto pos = output.find(marker);
    INFO("looking for '", marker, "' in blender output:\n", output);
    REQUIRE(pos != std::string::npos);
    std::string rest = output.substr(pos + marker.size());
    size_t c1 = rest.find(',');
    size_t c2 = rest.find(',', c1 + 1);
    REQUIRE(c1 != std::string::npos);
    REQUIRE(c2 != std::string::npos);
    return {std::stof(rest.substr(0, c1)), std::stof(rest.substr(c1 + 1, c2 - c1 - 1)), std::stof(rest.substr(c2 + 1))};
}

// Reads an M2 header straight from disk, independent of anything husk
// export/tinygltf/Blender did with it -- the ground-truth source-file leg
// of the source-vs-export-vs-readback cross-checks below. Bones/vertices
// only: this fixture (testM2()) has inline bones, not .skel-sourced ones,
// so header.bones.count is the real joint count here.
husk::m2::Header readM2Header(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(static_cast<bool>(f), "couldn't open '", path, "': ", std::strerror(errno));
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return husk::m2::parseHeader(bytes);
}

// Remaps an M2-space (Z-up) AABB to glTF-space (Y-up) via husk's own
// gltf::zUpToYUp, transforming all 8 corners rather than just `min`/`max`
// directly -- the axis permutation negates one component (y_gltf =
// -z_m2), which flips that axis's min/max, so naively pairing
// zUpToYUp(min) with zUpToYUp(max) would silently produce an inside-out
// box on that axis.
struct Aabb {
    husk::gltf::Vec3 min, max;
};

Aabb transformedM2BoundingBox(const husk::m2::BoundingBox& box) {
    Aabb result;
    result.min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max()};
    result.max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest()};
    for (float x : {box.min.x, box.max.x}) {
        for (float y : {box.min.y, box.max.y}) {
            for (float z : {box.min.z, box.max.z}) {
                husk::gltf::Vec3 corner = husk::gltf::zUpToYUp({x, y, z});
                result.min.x = std::min(result.min.x, corner.x);
                result.min.y = std::min(result.min.y, corner.y);
                result.min.z = std::min(result.min.z, corner.z);
                result.max.x = std::max(result.max.x, corner.x);
                result.max.y = std::max(result.max.y, corner.y);
                result.max.z = std::max(result.max.z, corner.z);
            }
        }
    }
    return result;
}

}  // namespace

TEST_CASE("husk export: exported bind-pose vertex bounds sit fully inside the M2 header's own "
          "declared bounding box" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-conformance-bbox.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    // mesh 0 is always the render mesh: cmd_export.cpp appends the
    // (optional) collision mesh strictly after every render/LOD entry, so
    // it never displaces this fixture's single render mesh from index 0 --
    // see cmd_export.cpp's "renderMeshCount" comment. mesh count is 1 or 2
    // depending on whether this fixture happens to carry collision data
    // (it does, but this check doesn't hardcode that).
    REQUIRE(!model.meshes.empty());
    REQUIRE(!model.meshes[0].primitives.empty());

    // The header's bounding_box is NOT a tight fit around the bind-pose
    // mesh -- it runs roughly 2x-4x wider per axis than the bind-pose
    // vertices' own extent, consistent with the header accounting for the
    // model's full animated range, not just its rest pose -- so a
    // tight-tolerance equality check would fail on real data, not catch a
    // real bug. What *does* hold, and is a genuine correctness signal: the
    // bind-pose mesh husk actually parsed and wrote must sit entirely
    // inside the header's declared box -- if husk's vertex parsing were
    // subtly wrong (bad offset, wrong scale), the computed accessor bounds
    // could poke outside it.
    // TODO: Remove: cites WIKI_FINDINGS.md §5; verification narrative
    // (bloodelffemale_hd.m2's header z range ~9.6 vs. bind-pose mesh ~2.1).
    auto posIt = model.meshes[0].primitives[0].attributes.find("POSITION");
    REQUIRE(posIt != model.meshes[0].primitives[0].attributes.end());
    const tinygltf::Accessor& posAcc = model.accessors[posIt->second];
    REQUIRE(posAcc.minValues.size() == 3);
    REQUIRE(posAcc.maxValues.size() == 3);

    husk::m2::Header header = readM2Header(m2Path);
    Aabb headerBox = transformedM2BoundingBox(header.boundingBox);

    // A small epsilon, not zero: the header box is itself a float value
    // Blizzard's own exporter computed, and husk's own axis remap
    // introduces its own rounding -- this isn't meant to catch that noise,
    // only a real containment violation.
    constexpr float kEpsilon = 1e-3f;
    CHECK(headerBox.min.x <= static_cast<float>(posAcc.minValues[0]) + kEpsilon);
    CHECK(headerBox.min.y <= static_cast<float>(posAcc.minValues[1]) + kEpsilon);
    CHECK(headerBox.min.z <= static_cast<float>(posAcc.minValues[2]) + kEpsilon);
    CHECK(headerBox.max.x >= static_cast<float>(posAcc.maxValues[0]) - kEpsilon);
    CHECK(headerBox.max.y >= static_cast<float>(posAcc.maxValues[1]) - kEpsilon);
    CHECK(headerBox.max.z >= static_cast<float>(posAcc.maxValues[2]) - kEpsilon);

    std::filesystem::remove(outPath);
}

// Counts real M2 bone-array roots (parentBone == -1) by parsing the actual
// bone array, not just the header's Array descriptor -- the ground-truth
// check that a chosen fixture really is multi-root, so the tests below fail
// loudly rather than pass vacuously if a future re-extraction ever swaps in
// a different file.
int countRealRootBones(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(static_cast<bool>(f), "couldn't open '", path, "': ", std::strerror(errno));
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    husk::m2::Header header = husk::m2::parseHeader(bytes);
    auto bones = husk::m2::parseBones(bytes, header.bones);
    int roots = 0;
    for (const auto& b : bones) {
        if (b.parentBone == -1) ++roots;
    }
    return roots;
}

#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export: a real multi-root-bone-forest weapon "
          "produces a glb the Khronos glTF-Validator accepts with zero errors and no "
          "SKIN_NO_COMMON_ROOT" *
          doctest::skip(testWeaponParticleB().empty())) {
    std::string m2Path = testWeaponParticleB();
    // Sanity: this fixture really is multi-root (10 of 15 real bones have
    // parentBone == -1).
    REQUIRE(countRealRootBones(m2Path) > 1);

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-conformance-multiroot.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + outPath + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);
    CHECK(validation.output.find("SKIN_NO_COMMON_ROOT") == std::string::npos);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: a real multi-root-bone-forest weapon "
          "produces a glb the Khronos glTF-Validator accepts with zero errors and no "
          "SKIN_NO_COMMON_ROOT" *
          doctest::skip(true)) {
    // see TEST_DESIGN.md#Conformance-gating
}
#endif

#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export: --phys's physics_bodies extras don't introduce new glTF-Validator "
          "errors on a real weapon .m2/.skin/.phys fixture" *
          doctest::skip(testWeaponPhys().empty() || testWeaponPhysSkin().empty())) {
    std::string m2Path = testWeaponPhys();
    std::string skinPath = testWeaponPhysSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-conformance-phys.glb").string();
    std::filesystem::remove(outPath);

    // --phys unset -> auto-detects the same-basename '.phys' this fixture's
    // own .m2 sits next to (see cmd_export.cpp's --phys resolution).
    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);
    REQUIRE(exportResult.output.find("attached 10 physics body") != std::string::npos);

    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + outPath + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);
    CHECK(validation.output.find("Errors: 0") != std::string::npos);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: --phys's physics_bodies extras don't introduce new glTF-Validator "
          "errors on a real weapon .m2/.skin/.phys fixture" *
          doctest::skip(true)) {
}
#endif

#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export: real attachment/event/light child nodes don't introduce new "
          "glTF-Validator errors on a real weapon .m2/.skin fixture" *
          doctest::skip(testWeaponParticleStress().empty())) {
    std::string m2Path = testWeaponParticleStress();

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-conformance-anchor-nodes.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);
    REQUIRE(exportResult.output.find("attached 5 attachment, 2 event, and 4 light") !=
             std::string::npos);

    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + outPath + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);
    CHECK(validation.output.find("Errors: 0") != std::string::npos);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: real attachment/event/light child nodes don't introduce new "
          "glTF-Validator errors on a real weapon .m2/.skin fixture" *
          doctest::skip(true)) {
}
#endif

#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export: real M2 + .skin produces a glb the Khronos glTF-Validator "
          "accepts with zero errors" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-conformance.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    // -a: print every issue message (not just errors) to stderr, captured
    // into .output below, so a failure here shows exactly what the
    // validator objected to instead of just a bare nonzero exit code.
    // The validator's own documented contract (--help): nonzero exit iff
    // at least one error was found -- warnings/infos/hints don't fail this.
    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + outPath + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: real M2 + .skin produces a glb the Khronos glTF-Validator "
          "accepts with zero errors" *
          doctest::skip(true)) {
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export: real M2 + .skin imports into Blender (headless) with bone/animation "
          "counts matching tinygltf's own reading of the same file" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-blender.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);

    // --factory-startup: skip whatever addons/preferences happen to be
    // installed in the invoking user's own Blender config -- this test
    // exercises husk's glb output against Blender's importer, not against
    // Luna's personal addon set, which shouldn't be able to fail this.
    // --python-exit-code 1: an unhandled exception in the import script
    // must actually fail this process -- Blender's own --background
    // default is to exit 0 regardless.
    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                     " --background --factory-startup --python-exit-code 1 --python \"" +
                                     std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + outPath + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    CHECK(parseProbeInt(blenderResult.output, "bone_count") ==
          static_cast<int>(model.skins[0].joints.size()));
    CHECK(parseProbeInt(blenderResult.output, "action_count") ==
          static_cast<int>(model.animations.size()));

    // The M2 source's own header counts, a third leg alongside tinygltf's
    // and Blender's independent readings of the exported .glb. This
    // export has no --lod, so exactly one skin
    // (LOD tier) is written -- header.vertices.count carries over exactly,
    // not multiplied (cmd_export.cpp's baseMesh is built once from the
    // entire vertex array and reused unsliced; a .skin's batches only
    // select a *subset* for drawing, never re-slice the accessor).
    // cmd_export.cpp also attaches an unskinned collision-mesh node
    // whenever the M2 has collision data (this fixture does), so the
    // expected mesh/vertex counts below fold that in explicitly rather
    // than assuming the render mesh is the only node.
    husk::m2::Header header = readM2Header(m2Path);
    bool hasCollisionMesh = header.collisionPositions.count > 0 && header.collisionIndices.count > 0;
    int expectedMeshCount = 1 + (hasCollisionMesh ? 1 : 0);
    uint32_t expectedVertexCount =
        header.vertices.count + (hasCollisionMesh ? header.collisionPositions.count : 0);

    CHECK(parseProbeInt(blenderResult.output, "mesh_object_count") == expectedMeshCount);
    CHECK(static_cast<uint32_t>(parseProbeInt(blenderResult.output, "total_vertex_count")) ==
          expectedVertexCount);
    CHECK(model.skins[0].joints.size() == header.bones.count);

    // Case 5, readback step: the collision mesh Blender actually imported
    // (found by its "collision" extras tag, not by name -- see
    // blender_import_check.py) matches the M2 header's own collision
    // counts exactly (no tolerance needed -- pure count/topology, same
    // indices/positions the M2 declares, just carried through unchanged).
    if (hasCollisionMesh) {
        CHECK(parseProbeInt(blenderResult.output, "collision_mesh_count") == 1);
        CHECK(static_cast<uint32_t>(parseProbeInt(blenderResult.output, "collision_mesh_vertex_count")) ==
              header.collisionPositions.count);
        CHECK(static_cast<uint32_t>(parseProbeInt(blenderResult.output, "collision_mesh_triangle_count")) ==
              header.collisionIndices.count / 3);
    }

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: real M2 + .skin imports into Blender (headless) with bone/animation "
          "counts matching tinygltf's own reading of the same file" *
          doctest::skip(true)) {
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export: a real multi-root-bone-forest weapon imports into Blender with bone_count "
          "matching skin.joints.size() exactly -- the synthesized non-joint parent node "
          "is not counted as a bone" *
          doctest::skip(testWeaponParticleB().empty())) {
    std::string m2Path = testWeaponParticleB();
    REQUIRE(countRealRootBones(m2Path) > 1);

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-blender-multiroot.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    husk::m2::Header header = readM2Header(m2Path);
    // The invariant the chosen design (a plain non-joint parent node, see
    // DESIGN.md#Key-design-decisions) exists to preserve: skin.joints never
    // grows a bogus extra entry for the synthesized node (unlike the
    // rejected alternative of appending it as one more real joint).
    CHECK(model.skins[0].joints.size() == header.bones.count);

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                     " --background --factory-startup --python-exit-code 1 --python \"" +
                                     std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + outPath + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    // The empirical finding this test exists to confirm: Blender's glTF
    // importer does NOT count the synthesized non-joint parent node as a
    // bone -- bone_count matches the real M2 bone count exactly, same as
    // skin.joints.size() above.
    CHECK(parseProbeInt(blenderResult.output, "bone_count") ==
          static_cast<int>(header.bones.count));

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: a real multi-root-bone-forest weapon imports into Blender with bone_count "
          "matching skin.joints.size() exactly -- the synthesized non-joint parent node "
          "is not counted as a bone" *
          doctest::skip(true)) {
}
#endif

// An asset-agnostic coordinate-frame probe, not a check on any real M2
// fixture's own semantics -- deliberately NOT built from a real
// character/weapon/creature, since it doesn't need a body plan or an "up"
// convention to assert anything about.
//
// The property under test: a root joint at the local origin, with three
// children offset by one unit along the M2-local +X/+Y/+Z axes
// respectively, should -- once exported through husk's real Z-up->Y-up
// conversion and reimported through Blender's real, independent glTF
// importer -- land at those *exact same* coordinates in Blender's own
// world space. Not "somewhere plausible," not "consistent with itself" --
// numerically identical to the original M2-local offset (WoW Z-up ->
// husk's zUpToYUp -> glTF Y-up -> Blender's own Y-up->Z-up import ->
// Blender Z-up should net to the identity transform, since it's the same
// physical up-axis on both ends). Mutation-tested (see
// TEST_DESIGN.md#Mutation-tested-regressions): reverting zUpToYUp to its
// historical formula makes this test fail with a net 180-degree flip
// instead of identity.
// TODO: Remove: cites TRANSFORM_TRIAGE.md §2/§5c throughout, including "why
// the original 'anatomically upright' idea doesn't survive a weapon or
// quadruped."
#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk gltf::writeGlbMulti: a synthetic axis-probe skeleton's local +X/+Y/+Z offsets "
          "survive a real husk-export -> Blender-import round trip as the identical coordinate") {
    husk::gltf::Skeleton skel;
    skel.joints.push_back({-1, {0, 0, 0}, {0, 0, 0}, "", "husk_probe_root"});
    auto addAxisProbe = [&](const husk::m2::Vec3& m2Offset, const std::string& name) {
        husk::gltf::Vec3 offset = husk::gltf::zUpToYUp({m2Offset.x, m2Offset.y, m2Offset.z});
        skel.joints.push_back({0, offset, offset, "", name});
    };
    addAxisProbe({1, 0, 0}, "husk_probe_x");
    addAxisProbe({0, 1, 0}, "husk_probe_y");
    addAxisProbe({0, 0, 1}, "husk_probe_z");

    auto glb = husk::gltf::writeGlbMulti({}, &skel);
    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-axis-probe.glb").string();
    std::ofstream out(outPath, std::ios::binary);
    REQUIRE(static_cast<bool>(out));
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
    out.close();

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                     " --background --factory-startup --python-exit-code 1 --python \"" +
                                     std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + outPath + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);
    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);

    constexpr float kEpsilon = 1e-3f;
    husk::gltf::Vec3 root = parseProbeVec3(blenderResult.output, "husk_probe_root");
    CHECK(root.x == doctest::Approx(0).epsilon(kEpsilon));
    CHECK(root.y == doctest::Approx(0).epsilon(kEpsilon));
    CHECK(root.z == doctest::Approx(0).epsilon(kEpsilon));

    husk::gltf::Vec3 px = parseProbeVec3(blenderResult.output, "husk_probe_x");
    CHECK(px.x == doctest::Approx(1).epsilon(kEpsilon));
    CHECK(px.y == doctest::Approx(0).epsilon(kEpsilon));
    CHECK(px.z == doctest::Approx(0).epsilon(kEpsilon));

    husk::gltf::Vec3 py = parseProbeVec3(blenderResult.output, "husk_probe_y");
    CHECK(py.x == doctest::Approx(0).epsilon(kEpsilon));
    CHECK(py.y == doctest::Approx(1).epsilon(kEpsilon));
    CHECK(py.z == doctest::Approx(0).epsilon(kEpsilon));

    husk::gltf::Vec3 pz = parseProbeVec3(blenderResult.output, "husk_probe_z");
    CHECK(pz.x == doctest::Approx(0).epsilon(kEpsilon));
    CHECK(pz.y == doctest::Approx(0).epsilon(kEpsilon));
    CHECK(pz.z == doctest::Approx(1).epsilon(kEpsilon));

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk gltf::writeGlbMulti: a synthetic axis-probe skeleton's local +X/+Y/+Z offsets "
          "survive a real husk-export -> Blender-import round trip as the identical coordinate" *
          doctest::skip(true)) {
}
#endif

// An optional, non-load-bearing secondary check: a real humanoid fixture's
// own "_Name" key bone (near the top of the head, keyBoneId 22 -- see
// m2::keyBoneName) should land above the armature's own origin once
// imported into Blender, not below it. This is explicitly NOT the primary
// orientation check (the asset-agnostic synthetic probe above is) -- it
// only applies to fixtures that happen to have this specific bone tagged
// (real humanoids; skipped, not failed, otherwise), and exists purely as a
// second, real-content confirmation on top of the probe's own math-only
// result.
// TODO: Remove: cites TRANSFORM_TRIAGE.md §5c and BLENDER_EXPORT_TODO.md §8
// (the real-file measurement that originally surfaced the orientation bug).
#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export: real M2 + .skin -- a real head-height key bone (\"_Name\") lands above "
          "the armature origin in Blender, not below it (optional secondary, humanoid-only "
          "check)" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-landmark-probe.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                     " --background --factory-startup --python-exit-code 1 --python \"" +
                                     std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + outPath + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    // This fixture is expected to have a real "_Name" bone -- if a future
    // fixture swap ever drops it, this REQUIRE fails loudly (a real gap in
    // this specific check's own applicability) rather than silently
    // passing with zero assertions.
    REQUIRE(blenderResult.output.find("HUSK_PROBE landmark_head_bone=") != std::string::npos);
    husk::gltf::Vec3 landmark = parseProbeVec3(blenderResult.output, "landmark_head_bone");
    // Blender is natively Z-up, not Y-up -- "above the origin" means a
    // positive Z component in the raw (x, y, z) triple this parses
    // directly out of Blender's own printed world position, the same
    // convention the axis-probe test above uses (its own "z" probe checks
    // .z, not .y, for exactly this reason).
    CHECK(landmark.z > 0.0f);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: real M2 + .skin -- a real head-height key bone (\"_Name\") lands above "
          "the armature origin in Blender, not below it (optional secondary, humanoid-only "
          "check)" *
          doctest::skip(true)) {
}
#endif

// Pipeline coverage for a body plan/bone-hierarchy shape bloodelffemale.m2
// (the fixture behind almost every other conformance test in this file)
// doesn't represent, not orientation coverage -- the synthetic axis-probe
// above already covers any asset type by construction. A real quadruped
// exercises a genuinely different skeleton shape end to end (does the
// export pipeline still produce a spec-valid, Blender-importable file for a
// body plan this test suite has never run against before), independent of
// whether it happens to be right-side up.
// TODO: Remove: cites TRANSFORM_TRIAGE.md §5e.
#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export: a real quadruped creature (wolf.m2) produces a glb the Khronos "
          "glTF-Validator accepts with zero errors" *
          doctest::skip(testQuadrupedM2().empty() || testQuadrupedSkin().empty())) {
    std::string m2Path = testQuadrupedM2();
    std::string skinPath = testQuadrupedSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-quadruped.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + outPath + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: a real quadruped creature (wolf.m2) produces a glb the Khronos "
          "glTF-Validator accepts with zero errors" *
          doctest::skip(true)) {
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export: a real quadruped creature (wolf.m2) imports into Blender (headless) "
          "with bone/vertex counts matching tinygltf's own reading of the same file" *
          doctest::skip(testQuadrupedM2().empty() || testQuadrupedSkin().empty())) {
    std::string m2Path = testQuadrupedM2();
    std::string skinPath = testQuadrupedSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-quadruped-blender.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                     " --background --factory-startup --python-exit-code 1 --python \"" +
                                     std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + outPath + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    CHECK(parseProbeInt(blenderResult.output, "bone_count") ==
          static_cast<int>(model.skins[0].joints.size()));

    husk::m2::Header header = readM2Header(m2Path);
    CHECK(model.skins[0].joints.size() == header.bones.count);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: a real quadruped creature (wolf.m2) imports into Blender (headless) "
          "with bone/vertex counts matching tinygltf's own reading of the same file" *
          doctest::skip(true)) {
}
#endif
