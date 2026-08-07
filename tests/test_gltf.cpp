// writeGlb()/writeGlbMulti() delegate the actual glTF/.glb framing to
// tinygltf (see nix/flake.nix, src/gltf.cpp) rather than reimplementing the
// format here. So instead of re-deriving glTF's binary chunk layout by hand
// (the test_m2.cpp/test_skin.cpp style), these tests round-trip writeGlb()'s
// output back through tinygltf's own loader and check the data survived --
// that exercises real glTF-spec compliance (tinygltf's loader enforces the
// spec independently of how husk built the Model), not just "husk agrees
// with itself."
//
// This file holds what's left of the pre-split tests/test_gltf.cpp after
// FILE_SPLIT_TODO.md Item 5: the document-assembler-level tests (overall
// .glb structural correctness) plus every writeGlbMulti test, since
// writeGlbMulti's own multi-node/multi-skeleton assembly logic doesn't
// cleanly belong to gltf_math.hpp/gltf_mesh.hpp/gltf_skeleton.hpp's data
// types -- gltf.cpp itself still holds real orchestrator code (Error,
// writeGlb, writeGlbMulti), same as this file's role among the test_gltf_*
// modules. See tests/test_gltf_math.cpp/test_gltf_mesh.cpp/
// test_gltf_skeleton.cpp for the rest.

#include "test_gltf_fixtures.hpp"

TEST_CASE("writeGlb: output starts with the glTF binary magic and version 2") {
    auto glb = husk::gltf::writeGlb(buildTriangleMesh());
    REQUIRE(glb.size() >= 12);
    CHECK(glb[0] == 'g');
    CHECK(glb[1] == 'l');
    CHECK(glb[2] == 'T');
    CHECK(glb[3] == 'F');
    uint32_t version;
    std::memcpy(&version, glb.data() + 4, 4);
    CHECK(version == 2);
}

// writeGlbMulti (husk export --lod all's underlying primitive, see
// README.md/src/cmd_export.cpp): multiple named meshes -- one node per LOD
// tier -- in a single .glb, optionally sharing one skeleton/animation set.
// writeGlb(mesh, materials, skeleton, animations) is defined in terms of
// this (see gltf.cpp) as the one-entry, unnamed case -- already exercised
// by every test above -- so these focus on what's genuinely new: multiple
// mesh nodes, per-entry material numbering, and joint-node index offsets
// when mesh nodes occupy the low indices instead of just node 0.

TEST_CASE("writeGlbMulti: one node (with its own mesh) per entry, named and scened") {
    husk::gltf::NamedMesh a{"lod0", buildTriangleMesh(), {}};
    husk::gltf::NamedMesh b{"lod1", buildTriangleMesh(), {}};
    auto glb = husk::gltf::writeGlbMulti({a, b});
    auto model = loadBack(glb);

    REQUIRE(model.meshes.size() == 2);
    REQUIRE(model.nodes.size() == 2);
    CHECK(model.nodes[0].name == "lod0");
    CHECK(model.nodes[0].mesh == 0);
    CHECK(model.nodes[1].name == "lod1");
    CHECK(model.nodes[1].mesh == 1);
    REQUIRE(model.scenes[model.defaultScene].nodes.size() == 2);
    CHECK(model.scenes[model.defaultScene].nodes[0] == 0);
    CHECK(model.scenes[model.defaultScene].nodes[1] == 1);
}

TEST_CASE("writeGlbMulti: empty meshes throws") {
    CHECK_THROWS_AS(husk::gltf::writeGlbMulti({}), husk::gltf::Error);
}

TEST_CASE("writeGlbMulti: empty meshes with a skeleton succeeds -- no mesh node, but joints and "
          "emitter anchors still export (the real geometry-less-VFX-model shape, see DESIGN.md)") {
    auto skel = buildChainSkeleton();
    skel.particleAnchors = {{7, 1, {0.1f, 0.2f, 0.3f}}};
    auto glb = husk::gltf::writeGlbMulti({}, &skel);
    auto model = loadBack(glb);

    CHECK(model.meshes.empty());
    REQUIRE(model.nodes.size() == 3);  // just the 3 joint nodes, no mesh node
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == 3);
    REQUIRE(model.scenes[model.defaultScene].nodes.size() == 1);  // root joint only
    CHECK(model.scenes[model.defaultScene].nodes[0] == 0);
}

TEST_CASE("writeGlbMulti: empty meshes with a skeleton that has no joints throws (same as no "
          "skeleton at all -- nothing to fall back to)") {
    husk::gltf::Skeleton emptySkel;
    CHECK_THROWS_AS(husk::gltf::writeGlbMulti({}, &emptySkel), husk::gltf::Error);
}

TEST_CASE("writeGlbMulti: each entry's materials are numbered locally, remapped into one shared "
          "glTF materials array") {
    auto quadA = buildTwoPrimitiveQuad();  // primitives reference materialIndex 0 and 1
    std::vector<husk::gltf::Material> materialsA(2);
    materialsA[0].name = "a_mat0";
    materialsA[1].name = "a_mat1";

    auto meshB = buildTriangleMesh();
    meshB.primitives[0].materialIndex = 0;  // local index 0 -- should NOT collide with a_mat0/1
    std::vector<husk::gltf::Material> materialsB(1);
    materialsB[0].name = "b_mat0";

    husk::gltf::NamedMesh a{"a", quadA, materialsA};
    husk::gltf::NamedMesh b{"b", meshB, materialsB};
    auto glb = husk::gltf::writeGlbMulti({a, b});
    auto model = loadBack(glb);

    REQUIRE(model.materials.size() == 3);
    CHECK(model.materials[0].name == "a_mat0");
    CHECK(model.materials[1].name == "a_mat1");
    CHECK(model.materials[2].name == "b_mat0");

    REQUIRE(model.meshes[0].primitives.size() == 2);
    CHECK(model.meshes[0].primitives[0].material == 0);
    CHECK(model.meshes[0].primitives[1].material == 1);
    REQUIRE(model.meshes[1].primitives.size() == 1);
    // meshB's local materialIndex 0 must resolve to the *global* index of
    // b_mat0 (2), not be reinterpreted as a_mat0 (0) -- the actual bug this
    // test exists to catch.
    CHECK(model.meshes[1].primitives[0].material == 2);
}

TEST_CASE("writeGlbMulti: a shared skeleton's joint nodes come after every mesh node, and every "
          "mesh node references the one shared skin") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    husk::gltf::NamedMesh b{"lod1", buildSkinnedTriangleMesh(), {}};
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlbMulti({a, b}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() == 2 /* mesh nodes */ + 3 /* joints */);
    CHECK(model.nodes[0].mesh == 0);
    CHECK(model.nodes[1].mesh == 1);
    REQUIRE(model.skins.size() == 1);
    CHECK(model.nodes[0].skin == 0);
    CHECK(model.nodes[1].skin == 0);

    // Joint node indices are offset by meshCount (2) -- root joint is node
    // 2, its child node 3, that one's child node 4 (see buildChainSkeleton).
    const auto& skin = model.skins[0];
    REQUIRE(skin.joints.size() == 3);
    CHECK(skin.joints[0] == 2);
    CHECK(skin.joints[1] == 3);
    CHECK(skin.joints[2] == 4);
    REQUIRE(model.nodes[2].children.size() == 1);
    CHECK(model.nodes[2].children[0] == 3);
    REQUIRE(model.nodes[3].children.size() == 1);
    CHECK(model.nodes[3].children[0] == 4);

    // Scene roots: both mesh nodes plus the one root joint node -- not the
    // whole joint chain (children are reached via the hierarchy, same as
    // writeGlb's single-mesh case).
    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 3);
    CHECK(sceneNodes[0] == 0);
    CHECK(sceneNodes[1] == 1);
    CHECK(sceneNodes[2] == 2);
}

TEST_CASE("writeGlbMulti: an animation's joint target node is offset by meshCount, same as the "
          "skeleton/skin's own joint nodes") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    husk::gltf::NamedMesh b{"lod1", buildSkinnedTriangleMesh(), {}};
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 1;  // "mid" joint -> node (meshCount=2) + 1 = 3
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 2, 0}, {0, 5, 0}};
    husk::gltf::Animation anim;
    anim.name = "anim0";
    anim.joints = {ja};

    auto glb = husk::gltf::writeGlbMulti({a, b}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    REQUIRE(model.animations[0].channels.size() == 1);
    CHECK(model.animations[0].channels[0].target_node == 3);
}

TEST_CASE("writeGlbMulti: one entry with skinning present but the wrong length throws, naming "
          "that entry") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    husk::gltf::Mesh badSkinning = buildTriangleMesh();
    badSkinning.skinning = {husk::gltf::JointWeights{}};  // 1 entry, but 3 positions
    husk::gltf::NamedMesh b{"lod1", badSkinning, {}};
    auto skel = buildChainSkeleton();
    CHECK_THROWS_AS(husk::gltf::writeGlbMulti({a, b}, &skel), husk::gltf::Error);
}

// A skeleton in scope doesn't force every mesh entry to be skinned -- an
// entry can opt out by leaving `mesh.skinning` empty (see gltf.hpp's
// writeGlbMulti doc comment; the concrete use case is an unskinned
// collision-mesh node alongside a skinned render mesh, cmd_export.cpp).
TEST_CASE("writeGlbMulti: a mesh entry with no skinning data, alongside a skinned one sharing the "
          "same skeleton, gets no glTF skin reference") {
    husk::gltf::NamedMesh skinned{"render", buildSkinnedTriangleMesh(), {}};
    husk::gltf::NamedMesh unskinned{"aux", buildTriangleMesh(), {}};  // no skinning at all
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlbMulti({skinned, unskinned}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() >= 2);
    CHECK(model.nodes[0].skin == 0);
    CHECK(model.nodes[1].skin == -1);
    REQUIRE(model.meshes.size() == 2);
    for (const auto& attr : model.meshes[1].primitives[0].attributes) {
        CHECK(attr.first != "JOINTS_0");
        CHECK(attr.first != "WEIGHTS_0");
    }
}

namespace {

// 3 independent roots, no shared parent -- a real M2 shape (a bone forest,
// not a tree; see gltf.hpp's Skeleton doc comment). Distinct
// from buildChainSkeleton, which is single-root by construction.
husk::gltf::Skeleton buildMultiRootSkeleton() {
    husk::gltf::Skeleton skel;
    skel.joints.push_back({-1, {0, 0, 0}, {0, 0, 0}});
    skel.joints.push_back({-1, {5, 0, 0}, {5, 0, 0}});
    skel.joints.push_back({-1, {0, 5, 0}, {0, 5, 0}});
    return skel;
}

}  // namespace

TEST_CASE("writeGlbMulti: a multi-root skeleton gets one synthesized non-joint parent node") {
    auto skel = buildMultiRootSkeleton();
    auto glb = husk::gltf::writeGlbMulti({}, &skel);
    auto model = loadBack(glb);

    // No mesh nodes -- 3 joint nodes plus exactly one synthetic node.
    REQUIRE(model.nodes.size() == 4);
    int syntheticIdx = 3;  // meshCount(0) + jointCount(3)

    REQUIRE(model.skins.size() == 1);
    const auto& skin = model.skins[0];
    // The concrete difference from Option 2 (a fake extra joint): joints
    // stays exactly skeleton->joints.size(), no bogus extra entry.
    CHECK(skin.joints.size() == 3);
    CHECK(skin.skeleton == syntheticIdx);

    const auto& synth = model.nodes[syntheticIdx];
    REQUIRE(synth.children.size() == 3);
    CHECK(synth.children[0] == skin.joints[0]);
    CHECK(synth.children[1] == skin.joints[1]);
    CHECK(synth.children[2] == skin.joints[2]);
    // Untouched/default transform -- a stray translation/rotation/scale
    // here would silently shift every former-root joint's whole subtree.
    CHECK(synth.translation.empty());
    CHECK(synth.rotation.empty());
    CHECK(synth.scale.empty());

    // Scene has exactly one root entry: the synthetic node, standing in for
    // all 3 real roots (reached via its own .children, not listed
    // individually).
    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 1);
    CHECK(sceneNodes[0] == syntheticIdx);
}

TEST_CASE("writeGlbMulti: a single-root skeleton's output is unaffected by the multi-root "
          "synthesis path -- no synthetic node, skin.skeleton left unset") {
    auto skel = buildChainSkeleton();  // single root by construction
    auto glb = husk::gltf::writeGlbMulti({}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() == 3);  // exactly the 3 joint nodes, no synthetic node
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == 3);
    CHECK(model.skins[0].skeleton == -1);  // unset, same as before this feature existed

    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 1);
    CHECK(sceneNodes[0] == 0);  // the one real root joint node, not a synthetic one
}

TEST_CASE("writeGlbMulti: a multi-root skeleton alongside real mesh nodes -- synthetic node comes "
          "after every joint node, mesh nodes/skinning are unaffected") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    auto skel = buildMultiRootSkeleton();
    // Weight the mesh to a couple of the (now-root) joints directly, to
    // prove vertex joint indices stay raw/unremapped M2 bone indices even
    // though rootJointNodeIndices.size() > 1.
    auto& mesh = a.mesh;
    mesh.skinning[0].joints[0] = 0;
    mesh.skinning[1].joints[0] = 2;

    auto glb = husk::gltf::writeGlbMulti({a}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() == 1 /* mesh */ + 3 /* joints */ + 1 /* synthetic */);
    int syntheticIdx = 4;  // meshCount(1) + jointCount(3)
    CHECK(model.nodes[0].mesh == 0);
    CHECK(model.nodes[0].skin == 0);

    const auto& skin = model.skins[0];
    CHECK(skin.joints.size() == 3);
    CHECK(skin.joints[0] == 1);  // meshCount + 0
    CHECK(skin.joints[2] == 3);  // meshCount + 2
    CHECK(skin.skeleton == syntheticIdx);
    CHECK(model.nodes[syntheticIdx].children.size() == 3);

    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 2);  // mesh node + the one synthetic root
    CHECK(sceneNodes[0] == 0);
    CHECK(sceneNodes[1] == syntheticIdx);
}
