// canon::assembleModel (canon_model.hpp) -- pure composition, no m2::/
// skin:: type in its own signature (see that header's own doc comment for
// why: the M2-specific orchestration this function used to do itself moved
// to m2input::buildCanonModel, tests/test_m2_canon_input.cpp). This file
// checks ONLY the one structural invariant assembleModel itself owns:
// `primitiveMaterials` must have one entry per `mesh.primitives` entry, and
// any `RecordIndex` in it must be in range for `materials`. Every piece
// this function packs together (Skeleton/Mesh/Material/AnimationClip) is
// hand-built here, never derived from real M2/skin data -- that's
// deliberately test_m2_canon_input.cpp's job, not this file's.

#include <doctest/doctest.h>

#include "canon_model.hpp"

using namespace husk;

TEST_CASE("canon::assembleModel packs already-built pieces into a Model unchanged") {
    canon::Skeleton skeleton;
    canon::Joint root;
    root.parent = -1;
    skeleton.joints = {root};

    canon::Mesh mesh;
    canon::PrimitiveGeoset prim;
    prim.indexCount = 3;
    mesh.primitives = {prim};

    canon::Material mat;
    mat.ref.id = canon::RecordIndex{0};
    std::vector<canon::Material> materials = {mat};
    std::vector<canon::Identity> primitiveMaterials = {canon::Identity{canon::RecordIndex{0}}};

    canon::AnimationClip clip;
    clip.sequence = canon::SequenceRef::sequence(7);
    std::vector<canon::AnimationClip> animations = {clip};

    canon::Model result =
        canon::assembleModel(skeleton, mesh, materials, primitiveMaterials, animations);

    CHECK(result.skeleton.joints.size() == 1);
    CHECK(result.mesh.primitives.size() == 1);
    REQUIRE(result.materials.size() == 1);
    CHECK(std::get<canon::RecordIndex>(result.materials[0].ref.id).value == 0);
    REQUIRE(result.primitiveMaterials.size() == 1);
    REQUIRE(result.animations.size() == 1);
    CHECK(result.animations[0].sequence.index == 7);
}

TEST_CASE("canon::assembleModel throws when primitiveMaterials doesn't have one entry per "
          "mesh.primitives entry") {
    canon::Mesh mesh;
    mesh.primitives.resize(2);  // two primitives, but only one primitiveMaterials entry below

    std::vector<canon::Material> materials(1);
    materials[0].ref.id = canon::RecordIndex{0};
    std::vector<canon::Identity> primitiveMaterials = {canon::Identity{canon::RecordIndex{0}}};

    CHECK_THROWS_AS(
        canon::assembleModel(canon::Skeleton{}, mesh, materials, primitiveMaterials, {}),
        std::runtime_error);
}

TEST_CASE("canon::assembleModel throws when a primitiveMaterials RecordIndex is out of range "
          "for materials") {
    canon::Mesh mesh;
    mesh.primitives.resize(1);

    std::vector<canon::Material> materials(1);  // only index 0 exists
    std::vector<canon::Identity> primitiveMaterials = {
        canon::Identity{canon::RecordIndex{5}}};  // out of range

    CHECK_THROWS_AS(
        canon::assembleModel(canon::Skeleton{}, mesh, materials, primitiveMaterials, {}),
        std::runtime_error);
}

TEST_CASE("canon::assembleModel accepts a non-RecordIndex primitiveMaterials entry without "
          "validating it locally (a bundle-external material reference, not resolvable "
          "against a single local Model)") {
    canon::Mesh mesh;
    mesh.primitives.resize(1);

    std::vector<canon::Material> materials;  // deliberately empty -- the FileDataId entry
                                              // below doesn't name anything in it
    std::vector<canon::Identity> primitiveMaterials = {canon::Identity{canon::FileDataId{12345}}};

    canon::Model result =
        canon::assembleModel(canon::Skeleton{}, mesh, materials, primitiveMaterials, {});
    REQUIRE(result.primitiveMaterials.size() == 1);
    CHECK(std::get<canon::FileDataId>(result.primitiveMaterials[0]).value == 12345);
}

TEST_CASE("canon::assembleModel: an empty model (no primitives, no materials) composes cleanly") {
    canon::Model result = canon::assembleModel(canon::Skeleton{}, canon::Mesh{}, {}, {}, {});
    CHECK(result.mesh.primitives.empty());
    CHECK(result.materials.empty());
    CHECK(result.primitiveMaterials.empty());
    CHECK(result.animations.empty());
}

TEST_CASE("canon::assembleModel checks collision indices, per-triangle arrays and parts") {
    canon::Mesh mesh;
    mesh.parts.resize(1);
    canon::CollisionMesh c;
    c.positions.resize(3);
    c.indices = {0, 1, 2};
    c.triangleParts = {0};
    CHECK(canon::assembleModel({}, mesh, {}, {}, {}, {}, {}, {}, c).collision.indices.size() == 3);

    canon::CollisionMesh badIndex = c;
    badIndex.indices[2] = 3;
    CHECK_THROWS_WITH_AS(canon::assembleModel({}, mesh, {}, {}, {}, {}, {}, {}, badIndex),
                         "collision indices: expected < 3 positions, got 3", std::runtime_error);
    canon::CollisionMesh partial = c;
    partial.indices.pop_back();
    CHECK_THROWS_WITH_AS(canon::assembleModel({}, mesh, {}, {}, {}, {}, {}, {}, partial),
                         "collision indices: expected a multiple of 3, got 2", std::runtime_error);
    canon::CollisionMesh badFlags = c;
    badFlags.triangleSourceFlags = {1, 2};
    CHECK_THROWS_WITH_AS(canon::assembleModel({}, mesh, {}, {}, {}, {}, {}, {}, badFlags),
                         "collision triangleSourceFlags: expected none or one per triangle (1), got 2",
                         std::runtime_error);
    canon::CollisionMesh badPart = c;
    badPart.triangleParts = {1};
    CHECK_THROWS_WITH_AS(canon::assembleModel({}, mesh, {}, {}, {}, {}, {}, {}, badPart),
                         "collision triangleParts: expected < 1 parts, got 1", std::runtime_error);
}

TEST_CASE("canon::assembleModel checks every portal link's portal and part") {
    canon::Mesh mesh;
    mesh.parts.resize(2);
    mesh.portals.resize(1);
    mesh.parts[0].portals = {{0, 1, 1}};
    CHECK(canon::assembleModel({}, mesh, {}, {}, {}).mesh.parts[0].portals.size() == 1);
    mesh.parts[1].portals = {{0, 2, -1}};
    CHECK_THROWS_WITH_AS(canon::assembleModel({}, mesh, {}, {}, {}),
                         "part 1's portal link: expected portal < 1 and part < 2, got portal 0 to part 2",
                         std::runtime_error);
}

TEST_CASE("canon::assembleModel keeps a scene whose bone references are all in range") {
    canon::Skeleton skeleton;
    skeleton.joints.resize(2);
    canon::Scene scene;
    canon::Attachment attachment;
    attachment.bone = canon::boneRef(1);
    scene.attachments.push_back(attachment);
    scene.lights.push_back(canon::Light{});  // no bone: not attached

    canon::Model result = canon::assembleModel(skeleton, canon::Mesh{}, {}, {}, {}, scene);
    CHECK(result.scene.attachments.size() == 1);
    CHECK(result.scene.lights.size() == 1);
}

TEST_CASE("canon::assembleModel throws when a scene bone reference is out of range for the skeleton") {
    canon::Skeleton skeleton;
    skeleton.joints.resize(2);
    canon::Scene scene;
    canon::ParticleEmitter particle;
    particle.bone = canon::boneRef(2);
    scene.particles.push_back(particle);

    CHECK_THROWS_WITH_AS(canon::assembleModel(skeleton, canon::Mesh{}, {}, {}, {}, scene),
                         "particle emitter 0's bone: expected an index < 2 joints, got 2", std::runtime_error);
}
