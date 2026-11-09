// This is the Conformance tier (see TEST_DESIGN.md#Four-tier-architecture)
// -- real downstream consumers of what husk exports, as opposed to
// tests/test_integration.cpp's "does husk itself run correctly" role:
//
//   - The Khronos glTF-Validator over `husk export --glb`'s lean
//     projection: independent spec-conformance check. tinygltf being able
//     to load a file back doesn't prove the file is spec-valid -- tinygltf
//     is a fairly permissive reader.
//   - Blender, run headlessly: its own glTF importer over the lean
//     projection (tests/blender_import_check.py), and the native bundle
//     read straight into Blender with no glTF involved
//     (tests/bundle_import_check.py).
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

#include "m2.hpp"
#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_data_paths.hpp"
#include "writers/gltf_lean.hpp"
#include "writers/gltf_math.hpp"

namespace {

using husk::test::runCommand;
using husk::test::runHusk;
using husk::test::testFoxM2;
using husk::test::testFoxSkin;
using husk::test::testM2;
using husk::test::testQuadrupedM2;
using husk::test::testQuadrupedSkin;
using husk::test::testSkin;
using husk::test::testWeaponParticleB;

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

// Pulls a "x,y,z" triple out of a "HUSK_PROBE key=x,y,z" line -- same
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
// of the source-vs-export-vs-readback cross-checks below.
husk::m2::Header readM2Header(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    REQUIRE_MESSAGE(static_cast<bool>(f), "couldn't open '", path, "': ", std::strerror(errno));
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return husk::m2::parseHeader(bytes);
}

// Counts real M2 bone-array roots (parentBone == -1) by parsing the actual
// bone array -- the ground-truth check that a chosen fixture really is
// multi-root, so a test fails loudly rather than passing vacuously if a
// re-extraction ever swaps in a different file.
int countRealRootBones(const std::string& path) {
    std::vector<uint8_t> bytes = readFile(path);
    husk::m2::Header header = husk::m2::parseHeader(bytes);
    auto bones = husk::m2::parseBones(bytes, header.bones);
    int roots = 0;
    for (const auto& b : bones) {
        if (b.parentBone == -1) ++roots;
    }
    return roots;
}

struct Aabb {
    husk::gltf::Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::max()};
    husk::gltf::Vec3 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                         std::numeric_limits<float>::lowest()};
    void add(const husk::gltf::Vec3& p) {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
};

// Remaps an M2-space (Z-up) AABB to glTF-space (Y-up) via husk's own
// gltf::zUpToYUp, transforming all 8 corners rather than just `min`/`max`
// directly -- the axis permutation negates one component, which flips that
// axis's min/max, so pairing zUpToYUp(min) with zUpToYUp(max) would
// produce an inside-out box on that axis.
Aabb transformedM2BoundingBox(const husk::m2::BoundingBox& box) {
    Aabb result;
    for (float x : {box.min.x, box.max.x}) {
        for (float y : {box.min.y, box.max.y}) {
            for (float z : {box.min.z, box.max.z}) result.add(husk::gltf::zUpToYUp({x, y, z}));
        }
    }
    return result;
}

// `inner` sits inside `outer`, up to a small epsilon: the header box is
// itself a float value Blizzard's own exporter computed, and the axis
// remap rounds -- this isn't meant to catch that noise, only a real
// containment violation.
void checkContained(const Aabb& outer, const Aabb& inner) {
    constexpr float kEpsilon = 1e-3f;
    CHECK(outer.min.x <= inner.min.x + kEpsilon);
    CHECK(outer.min.y <= inner.min.y + kEpsilon);
    CHECK(outer.min.z <= inner.min.z + kEpsilon);
    CHECK(outer.max.x >= inner.max.x - kEpsilon);
    CHECK(outer.max.y >= inner.max.y - kEpsilon);
    CHECK(outer.max.z >= inner.max.z - kEpsilon);
}

// `husk export <m2> -o <bundle> [extra] --glb <glb>`, REQUIREd to succeed;
// any earlier run's outputs are removed first.
void exportWithGlb(const std::string& m2Path, const std::string& extra, const fs::path& bundleDir,
                   const fs::path& glbPath) {
    fs::remove_all(bundleDir);
    fs::remove(glbPath);
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + bundleDir.string() + "\" " + extra + " --glb \"" +
                          glbPath.string() + "\"");
    INFO("husk export output:\n", result.output);
    REQUIRE(result.exitCode == 0);
}

tinygltf::Model loadGlb(const fs::path& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, path.string());
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    return model;
}

}  // namespace

TEST_CASE("husk export: bind-pose vertex bounds, in the bundle and in the lean projection, sit fully "
          "inside the M2 header's own declared bounding box" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-conformance-bbox.bundle";
    fs::path glbPath = fs::temp_directory_path() / "husk-test-conformance-bbox.glb";
    exportWithGlb(m2Path, "--skin \"" + testSkin() + "\"", bundleDir, glbPath);

    // The header's bounding_box is NOT a tight fit around the bind-pose
    // mesh -- it runs roughly 2x-4x wider per axis, consistent with the
    // header accounting for the model's full animated range -- so equality
    // would fail on real data without catching a real bug. What does hold,
    // and is a genuine correctness signal: the bind-pose mesh husk parsed
    // and wrote sits entirely inside the declared box. A subtly wrong
    // vertex parse (bad offset, wrong scale) could poke outside it.
    husk::m2::Header header = readM2Header(m2Path);

    // The bundle keeps the source's Z-up frame: compare untransformed.
    nlohmann::json manifest = parseManifest(bundleDir);
    const auto& positions = manifest["resources"]["geometry"]["mesh"]["positions"];
    REQUIRE(positions["component_type"] == "f32");
    REQUIRE(positions["component_count"] == 3);
    std::vector<uint8_t> meshBin = readFile((bundleDir / positions["file"].get<std::string>()).string());
    REQUIRE(positions["byte_offset"].get<size_t>() + positions["byte_length"].get<size_t>() <= meshBin.size());
    std::vector<float> xyz(positions["count"].get<size_t>() * 3);
    REQUIRE(!xyz.empty());
    std::memcpy(xyz.data(), meshBin.data() + positions["byte_offset"].get<size_t>(), xyz.size() * sizeof(float));
    Aabb bundleBounds;
    for (size_t i = 0; i < xyz.size(); i += 3) bundleBounds.add({xyz[i], xyz[i + 1], xyz[i + 2]});
    Aabb headerBox;
    headerBox.add({header.boundingBox.min.x, header.boundingBox.min.y, header.boundingBox.min.z});
    headerBox.add({header.boundingBox.max.x, header.boundingBox.max.y, header.boundingBox.max.z});
    checkContained(headerBox, bundleBounds);

    // The lean projection is Y-up: compare against the remapped box.
    tinygltf::Model model = loadGlb(glbPath);
    REQUIRE(!model.meshes.empty());
    REQUIRE(!model.meshes[0].primitives.empty());
    auto posIt = model.meshes[0].primitives[0].attributes.find("POSITION");
    REQUIRE(posIt != model.meshes[0].primitives[0].attributes.end());
    const tinygltf::Accessor& posAcc = model.accessors[static_cast<size_t>(posIt->second)];
    REQUIRE(posAcc.minValues.size() == 3);
    REQUIRE(posAcc.maxValues.size() == 3);
    Aabb glbBounds;
    glbBounds.add({static_cast<float>(posAcc.minValues[0]), static_cast<float>(posAcc.minValues[1]),
                   static_cast<float>(posAcc.minValues[2])});
    glbBounds.add({static_cast<float>(posAcc.maxValues[0]), static_cast<float>(posAcc.maxValues[1]),
                   static_cast<float>(posAcc.maxValues[2])});
    checkContained(transformedM2BoundingBox(header.boundingBox), glbBounds);

    fs::remove_all(bundleDir);
    fs::remove(glbPath);
}

#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export --glb: real M2 + .skin produces a lean glb the Khronos glTF-Validator "
          "accepts with zero errors" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-conformance.bundle";
    fs::path glbPath = fs::temp_directory_path() / "husk-test-conformance.glb";
    exportWithGlb(testM2(), "--skin \"" + testSkin() + "\"", bundleDir, glbPath);

    // -a: print every issue message (not just errors), so a failure shows
    // exactly what the validator objected to. The validator's own
    // documented contract (--help): nonzero exit iff at least one error
    // was found -- warnings/infos/hints don't fail this.
    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + glbPath.string() + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);
    CHECK(validation.output.find("Errors: 0,") != std::string::npos);

    fs::remove_all(bundleDir);
    fs::remove(glbPath);
}
#else
TEST_CASE("husk export --glb: real M2 + .skin produces a lean glb the Khronos glTF-Validator "
          "accepts with zero errors" *
          doctest::skip(true)) {
    // see TEST_DESIGN.md#Conformance-gating
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export --glb: real M2 + .skin imports into Blender (headless) with bone/vertex/"
          "animation counts matching tinygltf's reading and the M2 header; the bundle carries the "
          "header's collision mesh" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-blender.bundle";
    fs::path glbPath = fs::temp_directory_path() / "husk-test-blender.glb";
    exportWithGlb(m2Path, "--skin \"" + testSkin() + "\"", bundleDir, glbPath);
    tinygltf::Model model = loadGlb(glbPath);
    REQUIRE(model.skins.size() == 1);

    // --factory-startup: skip whatever addons/preferences happen to be
    // installed in the invoking user's own Blender config.
    // --python-exit-code 1: an unhandled exception in the import script
    // must actually fail this process -- Blender's own --background
    // default is to exit 0 regardless.
    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                    " --background --factory-startup --python-exit-code 1 --python \"" +
                                    std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + glbPath.string() + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    CHECK(parseProbeInt(blenderResult.output, "bone_count") == static_cast<int>(model.skins[0].joints.size()));
    CHECK(parseProbeInt(blenderResult.output, "action_count") == static_cast<int>(model.animations.size()));

    // The M2 source's own header counts, a third leg alongside tinygltf's
    // and Blender's readings. The lean projection writes the render mesh
    // alone, built once from the entire vertex array (a .skin's batches
    // only select a subset for drawing, never re-slice it), and no joint
    // beyond the M2's own bones.
    husk::m2::Header header = readM2Header(m2Path);
    CHECK(parseProbeInt(blenderResult.output, "mesh_object_count") == 1);
    CHECK(static_cast<uint32_t>(parseProbeInt(blenderResult.output, "total_vertex_count")) == header.vertices.count);
    CHECK(model.skins[0].joints.size() == header.bones.count);

    // Collision is bundle-only: its counts match the header exactly (pure
    // count/topology, carried through unchanged).
    nlohmann::json manifest = parseManifest(bundleDir);
    const auto& geometry = manifest["resources"]["geometry"];
    if (header.collisionPositions.count > 0 && header.collisionIndices.count > 0) {
        REQUIRE(geometry.contains("collision"));
        CHECK(geometry["collision"]["positions"]["count"] == header.collisionPositions.count);
        CHECK(geometry["collision"]["indices"]["count"] == header.collisionIndices.count);
    } else {
        CHECK_FALSE(geometry.contains("collision"));
    }

    fs::remove_all(bundleDir);
    fs::remove(glbPath);
}
#else
TEST_CASE("husk export --glb: real M2 + .skin imports into Blender (headless) with bone/vertex/"
          "animation counts matching tinygltf's reading and the M2 header; the bundle carries the "
          "header's collision mesh" *
          doctest::skip(true)) {
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export --glb: a real multi-root-bone-forest weapon imports into Blender as one "
          "armature with bone_count matching skin.joints.size() and the M2's bone count exactly" *
          doctest::skip(testWeaponParticleB().empty())) {
    std::string m2Path = testWeaponParticleB();
    // Sanity: this fixture really is multi-root (10 of 15 real bones have
    // parentBone == -1).
    REQUIRE(countRealRootBones(m2Path) > 1);

    fs::path bundleDir = fs::temp_directory_path() / "husk-test-blender-multiroot.bundle";
    fs::path glbPath = fs::temp_directory_path() / "husk-test-blender-multiroot.glb";
    exportWithGlb(m2Path, "", bundleDir, glbPath);
    tinygltf::Model model = loadGlb(glbPath);
    REQUIRE(model.skins.size() == 1);
    husk::m2::Header header = readM2Header(m2Path);
    CHECK(model.skins[0].joints.size() == header.bones.count);

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                    " --background --factory-startup --python-exit-code 1 --python \"" +
                                    std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + glbPath.string() + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    // Several root bones must still import as one armature, not one per
    // root, and nothing beyond the real bones is counted as a bone.
    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    CHECK(parseProbeInt(blenderResult.output, "bone_count") == static_cast<int>(model.skins[0].joints.size()));

    fs::remove_all(bundleDir);
    fs::remove(glbPath);
}
#else
TEST_CASE("husk export --glb: a real multi-root-bone-forest weapon imports into Blender as one "
          "armature with bone_count matching skin.joints.size() and the M2's bone count exactly" *
          doctest::skip(true)) {
}
#endif

// An asset-agnostic coordinate-frame probe, not a check on any real M2
// fixture's own semantics -- deliberately not built from a real
// character/weapon/creature, since it needs no body plan or "up"
// convention to assert anything about.
//
// The property under test: a root joint at the M2-local origin, with three
// children offset by one unit along the M2-local +X/+Y/+Z axes, should --
// once written through the lean writer's real Z-up->Y-up conversion and
// reimported through Blender's independent glTF importer -- land at those
// exact same coordinates in Blender's world space (WoW Z-up -> zUpToYUp ->
// glTF Y-up -> Blender's own Y-up->Z-up import -> Blender Z-up nets to the
// identity, the same physical up-axis on both ends). Mutation-tested (see
// TEST_DESIGN.md#Mutation-tested-regressions): a wrong zUpToYUp shows up
// here as a net 180-degree flip instead of identity.
#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("writers::writeLeanGlb: a synthetic axis-probe skeleton's M2-local +X/+Y/+Z offsets survive a "
          "real lean-glb -> Blender-import round trip as the identical coordinate") {
    canon::Model model;
    auto probeJoint = [](int parent, canon::Vec3 position, std::string label) {
        canon::Joint joint;
        joint.parent = parent;
        joint.globalPosition = position;
        joint.structuralLabel = std::move(label);
        return joint;
    };
    model.skeleton.joints = {probeJoint(-1, {0, 0, 0}, "husk_probe_root"), probeJoint(0, {1, 0, 0}, "husk_probe_x"),
                             probeJoint(0, {0, 1, 0}, "husk_probe_y"), probeJoint(0, {0, 0, 1}, "husk_probe_z")};
    // One triangle skinned to the root: a skin needs a mesh to bind.
    model.mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    model.mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    model.mesh.uvSets[0] = {{0, 0}, {1, 0}, {0, 1}};
    canon::Mesh::Skinning rootOnly;
    rootOnly.joints = {0, 0, 0, 0};
    rootOnly.weights = {1, 0, 0, 0};
    model.mesh.skinning = {rootOnly, rootOnly, rootOnly};
    model.mesh.indices = {0, 1, 2};
    canon::PrimitiveGeoset prim;
    prim.indexStart = 0;
    prim.indexCount = 3;
    model.mesh.primitives = {prim};
    canon::Material material;
    material.ref.id = canon::RecordIndex{0};
    model.materials = {material};
    model.primitiveMaterials = {canon::Identity{canon::RecordIndex{0}}};

    fs::path glbPath = fs::temp_directory_path() / "husk-test-axis-probe.glb";
    fs::remove(glbPath);
    writers::writeLeanGlb(model, glbPath);

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                    " --background --factory-startup --python-exit-code 1 --python \"" +
                                    std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + glbPath.string() + "\"");
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

    fs::remove(glbPath);
}
#else
TEST_CASE("writers::writeLeanGlb: a synthetic axis-probe skeleton's M2-local +X/+Y/+Z offsets survive a "
          "real lean-glb -> Blender-import round trip as the identical coordinate" *
          doctest::skip(true)) {
}
#endif

// A secondary, real-content check on top of the synthetic probe above: a
// real humanoid fixture's own "_Name" key bone (near the top of the head,
// keyBoneId 22 -- see m2::keyBoneName) should land above the armature's
// origin once imported into Blender, not below it. The lean projection
// names joints by structural label, so the bundle's skeleton is what maps
// "_Name" to the joint name Blender sees.
#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export --glb: real M2 + .skin -- a real head-height key bone (\"_Name\") lands above "
          "the armature origin in Blender, not below it" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-landmark-probe.bundle";
    fs::path glbPath = fs::temp_directory_path() / "husk-test-landmark-probe.glb";
    exportWithGlb(testM2(), "--skin \"" + testSkin() + "\"", bundleDir, glbPath);

    // This fixture is expected to have a real "_Name" bone -- if a fixture
    // swap ever drops it, this REQUIRE fails loudly rather than passing
    // with zero assertions.
    nlohmann::json manifest = parseManifest(bundleDir);
    std::string label;
    for (const auto& joint : manifest["resources"]["rig"]["skeleton"]["joints"]) {
        if (joint["ref"]["name"] == "_Name") label = joint["structural_label"].get<std::string>();
    }
    REQUIRE(!label.empty());

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                    " --background --factory-startup --python-exit-code 1 --python \"" +
                                    std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + glbPath.string() +
                                    "\" \"" + label + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    // Blender is natively Z-up: "above the origin" is a positive Z, the
    // same convention the axis-probe test above uses.
    husk::gltf::Vec3 landmark = parseProbeVec3(blenderResult.output, "bone:" + label);
    CHECK(landmark.z > 0.0f);

    fs::remove_all(bundleDir);
    fs::remove(glbPath);
}
#else
TEST_CASE("husk export --glb: real M2 + .skin -- a real head-height key bone (\"_Name\") lands above "
          "the armature origin in Blender, not below it" *
          doctest::skip(true)) {
}
#endif

// Pipeline coverage for a body plan/bone-hierarchy shape bloodelffemale.m2
// (the fixture behind most other conformance tests here) doesn't
// represent, not orientation coverage -- the synthetic axis-probe above
// already covers any asset type by construction.
#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export --glb: a real quadruped creature (wolf.m2) produces a lean glb the Khronos "
          "glTF-Validator accepts with zero errors" *
          doctest::skip(testQuadrupedM2().empty() || testQuadrupedSkin().empty())) {
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-quadruped.bundle";
    fs::path glbPath = fs::temp_directory_path() / "husk-test-quadruped.glb";
    exportWithGlb(testQuadrupedM2(), "--skin \"" + testQuadrupedSkin() + "\"", bundleDir, glbPath);

    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + glbPath.string() + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);
    CHECK(validation.output.find("Errors: 0,") != std::string::npos);

    fs::remove_all(bundleDir);
    fs::remove(glbPath);
}
#else
TEST_CASE("husk export --glb: a real quadruped creature (wolf.m2) produces a lean glb the Khronos "
          "glTF-Validator accepts with zero errors" *
          doctest::skip(true)) {
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export --glb: a real quadruped creature (wolf.m2) imports into Blender (headless) "
          "with bone/vertex counts matching tinygltf's reading and the M2 header" *
          doctest::skip(testQuadrupedM2().empty() || testQuadrupedSkin().empty())) {
    std::string m2Path = testQuadrupedM2();
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-quadruped-blender.bundle";
    fs::path glbPath = fs::temp_directory_path() / "husk-test-quadruped-blender.glb";
    exportWithGlb(m2Path, "--skin \"" + testQuadrupedSkin() + "\"", bundleDir, glbPath);
    tinygltf::Model model = loadGlb(glbPath);
    REQUIRE(model.skins.size() == 1);

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                    " --background --factory-startup --python-exit-code 1 --python \"" +
                                    std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + glbPath.string() + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    husk::m2::Header header = readM2Header(m2Path);
    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    CHECK(parseProbeInt(blenderResult.output, "bone_count") == static_cast<int>(model.skins[0].joints.size()));
    CHECK(model.skins[0].joints.size() == header.bones.count);
    CHECK(static_cast<uint32_t>(parseProbeInt(blenderResult.output, "total_vertex_count")) == header.vertices.count);

    fs::remove_all(bundleDir);
    fs::remove(glbPath);
}
#else
TEST_CASE("husk export --glb: a real quadruped creature (wolf.m2) imports into Blender (headless) "
          "with bone/vertex counts matching tinygltf's reading and the M2 header" *
          doctest::skip(true)) {
}
#endif

// The concrete point of REFACTOR/BLENDER_ADDON.md's "point at a manifest,
// nothing else": bundle_import_check.py reads manifest.json + its .bin
// slices directly, with no glTF anywhere in the loop, and the result is
// checked against the M2 header's own raw counts.
#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_BUNDLE_IMPORT_SCRIPT)
TEST_CASE("husk export: the bundle's mesh/skeleton import into Blender -- with no glTF involved at "
          "all -- matching the M2 header's own vertex/bone counts" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-blender-bundle.bundle";
    fs::remove_all(bundleDir);

    auto exportResult =
        runHusk("export \"" + m2Path + "\" -o \"" + bundleDir.string() + "\" --skin \"" + testSkin() + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);
    REQUIRE(fs::exists(bundleDir / "manifest.json"));

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                    " --background --factory-startup --python-exit-code 1 --python \"" +
                                    std::string(HUSK_BLENDER_BUNDLE_IMPORT_SCRIPT) + "\" -- \"" +
                                    bundleDir.string() + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    husk::m2::Header header = readM2Header(m2Path);
    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    CHECK(static_cast<uint32_t>(parseProbeInt(blenderResult.output, "bone_count")) == header.bones.count);
    CHECK(parseProbeInt(blenderResult.output, "mesh_object_count") == 1);
    CHECK(static_cast<uint32_t>(parseProbeInt(blenderResult.output, "total_vertex_count")) == header.vertices.count);

    fs::remove_all(bundleDir);
}
#else
TEST_CASE("husk export: the bundle's mesh/skeleton import into Blender -- with no glTF involved at "
          "all -- matching the M2 header's own vertex/bone counts" *
          doctest::skip(true)) {
}
#endif

// Same idea, materials this time: bundle_import_check.py's build_materials
// loads each material's first resolved layer's texture file (payload "uri")
// straight off disk into a real Blender Image.
//
// testFoxM2()/testFoxSkin() (test_data/creature/fox/): a real, simple,
// non-character creature with real local-basename-resolvable textures --
// the wolf carries no local texture at all, and a character model would
// imply the customization/geoset surface is covered here, which it isn't.
#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_BUNDLE_IMPORT_SCRIPT)
TEST_CASE("husk export: the bundle's materials/textures import into Blender -- with no glTF "
          "involved at all -- matching the manifest's own material and resolved-texture counts, "
          "with real decoded pixels" *
          doctest::skip(testFoxM2().empty() || testFoxSkin().empty())) {
    fs::path bundleDir = fs::temp_directory_path() / "husk-test-blender-bundle-mat.bundle";
    fs::remove_all(bundleDir);

    auto exportResult =
        runHusk("export \"" + testFoxM2() + "\" -o \"" + bundleDir.string() + "\" --skin \"" + testFoxSkin() + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    // Ground truth from the manifest, read independently of Blender: every
    // material, and the materials whose layers resolved to a written
    // texture file (the probe loads one image per such material).
    nlohmann::json manifest = parseManifest(bundleDir);
    const auto& materials = manifest["resources"]["materials"];
    int texturedMaterials = 0;
    for (const auto& material : materials) {
        for (const auto& layer : material["layers"]) {
            const auto& texture = textureAt(manifest, layer["texture"]);
            if (texture["state"] == "resolved" && texture.contains("payload")) {
                ++texturedMaterials;
                break;
            }
        }
    }
    REQUIRE(!materials.empty());
    REQUIRE(texturedMaterials > 0);

    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                    " --background --factory-startup --python-exit-code 1 --python \"" +
                                    std::string(HUSK_BLENDER_BUNDLE_IMPORT_SCRIPT) + "\" -- \"" +
                                    bundleDir.string() + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    CHECK(parseProbeInt(blenderResult.output, "material_count") == static_cast<int>(materials.size()));
    CHECK(parseProbeInt(blenderResult.output, "loaded_image_count") == texturedMaterials);
    // A failed/unresolved image loads as a real Blender Image of size
    // (0, 0) -- > 0 is the proof Blender decoded real pixels.
    CHECK(parseProbeInt(blenderResult.output, "min_image_width") > 0);
    CHECK(parseProbeInt(blenderResult.output, "min_image_height") > 0);

    fs::remove_all(bundleDir);
}
#else
TEST_CASE("husk export: the bundle's materials/textures import into Blender -- with no glTF "
          "involved at all -- matching the manifest's own material and resolved-texture counts, "
          "with real decoded pixels" *
          doctest::skip(true)) {
}
#endif
