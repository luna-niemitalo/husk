// Tests for husk::gltf's skeleton module (src/gltf_skeleton.hpp/.cpp):
// Skeleton/JointAnimation/Animation -- skinning, joint hierarchy, bone
// corrections, ribbon/particle/physics anchors, attachment/event/light
// nodes, and animation keyframes/sequence metadata.
// Split out of the former tests/test_gltf.cpp -- see FILE_SPLIT_TODO.md
// Item 5.

#include <algorithm>

#include "test_gltf_fixtures.hpp"

TEST_CASE("writeGlb: skinned mesh round-trips joints, weights, and the joint hierarchy") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.skins.size() == 1);
    const auto& skin = model.skins[0];
    REQUIRE(skin.joints.size() == 3);

    // Node hierarchy: joint 0 is root.parent-less, joint 1 is its child,
    // joint 2 is joint 1's child. Translations are local-to-parent.
    int rootNode = skin.joints[0];
    int midNode = skin.joints[1];
    int tipNode = skin.joints[2];
    REQUIRE(model.nodes[rootNode].children.size() == 1);
    CHECK(model.nodes[rootNode].children[0] == midNode);
    REQUIRE(model.nodes[midNode].children.size() == 1);
    CHECK(model.nodes[midNode].children[0] == tipNode);

    REQUIRE(model.nodes[midNode].translation.size() == 3);
    CHECK(model.nodes[midNode].translation[1] == doctest::Approx(2));
    REQUIRE(model.nodes[tipNode].translation.size() == 3);
    CHECK(model.nodes[tipNode].translation[2] == doctest::Approx(3));

    // Inverse bind matrices: pure translation by -globalPosition, column-major.
    REQUIRE(skin.inverseBindMatrices >= 0);
    const auto& ibmAcc = model.accessors[skin.inverseBindMatrices];
    REQUIRE(ibmAcc.count == 3);
    const auto& ibmView = model.bufferViews[ibmAcc.bufferView];
    const auto& ibmBuf = model.buffers[ibmView.buffer];
    std::vector<float> ibm(16 * 3);
    std::memcpy(ibm.data(), ibmBuf.data.data() + ibmView.byteOffset + ibmAcc.byteOffset,
                ibm.size() * sizeof(float));
    // Joint 2 (tip)'s global position is (0,2,3) -> translation column is
    // (-0,-2,-3), the last 4 floats of its 16-float column-major matrix.
    const float* tipMat = ibm.data() + 16 * 2;
    CHECK(tipMat[12] == doctest::Approx(0));
    CHECK(tipMat[13] == doctest::Approx(-2));
    CHECK(tipMat[14] == doctest::Approx(-3));
    CHECK(tipMat[15] == doctest::Approx(1));

    // Mesh node references the skin; primitive carries JOINTS_0/WEIGHTS_0.
    REQUIRE(model.nodes[0].mesh == 0);
    CHECK(model.nodes[0].skin == 0);
    const auto& prim = model.meshes[0].primitives[0];
    REQUIRE(prim.attributes.count("JOINTS_0") == 1);
    REQUIRE(prim.attributes.count("WEIGHTS_0") == 1);

    const auto& jAcc = model.accessors[prim.attributes.at("JOINTS_0")];
    REQUIRE(jAcc.count == 3);
    const auto& jView = model.bufferViews[jAcc.bufferView];
    const auto& jBuf = model.buffers[jView.buffer];
    const uint8_t* jData = jBuf.data.data() + jView.byteOffset + jAcc.byteOffset;
    CHECK(jData[0 * 4 + 0] == 0);  // vertex 0 -> joint 0
    CHECK(jData[1 * 4 + 0] == 1);  // vertex 1 -> joint 1
    CHECK(jData[2 * 4 + 0] == 2);  // vertex 2 -> joint 2

    const auto& wAcc = model.accessors[prim.attributes.at("WEIGHTS_0")];
    const auto& wView = model.bufferViews[wAcc.bufferView];
    const auto& wBuf = model.buffers[wView.buffer];
    std::vector<float> weights(4 * 3);
    std::memcpy(weights.data(), wBuf.data.data() + wView.byteOffset + wAcc.byteOffset,
                weights.size() * sizeof(float));
    CHECK(weights[0 * 4 + 0] == doctest::Approx(1));
    CHECK(weights[2 * 4 + 0] == doctest::Approx(1));
}

TEST_CASE("writeGlb: a joint's billboardMode becomes a \"billboard\" key in its node's extras") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.joints[1].billboardMode = "spherical";
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    int midNode = model.skins[0].joints[1];
    REQUIRE(model.nodes[midNode].extras.Has("billboard"));
    CHECK(model.nodes[midNode].extras.Get("billboard").Get<std::string>() == "spherical");

    // Joints without a billboardMode get no extras at all.
    int rootNode = model.skins[0].joints[0];
    CHECK_FALSE(model.nodes[rootNode].extras.Has("billboard"));
}

TEST_CASE("writeGlb: a skeleton's correctionSets round-trip as bone_correction_sets on the "
          "skin's extras") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 1103216;
    husk::gltf::Skeleton::CorrectionSet::Correction c;
    c.joint = 1;
    c.matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.01f, 0.02f, 0.03f, 1};
    set.corrections = {c};
    skel.correctionSets = {set};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    const auto& sets = extras.Get("bone_correction_sets");
    REQUIRE(sets.IsArray());
    REQUIRE(sets.ArrayLen() == 1);

    const auto& set0 = sets.Get(0);
    CHECK(set0.Get("file_data_id").GetNumberAsInt() == 1103216);
    const auto& corrections = set0.Get("corrections");
    REQUIRE(corrections.IsArray());
    REQUIRE(corrections.ArrayLen() == 1);
    const auto& c0 = corrections.Get(0);
    CHECK(c0.Get("joint").GetNumberAsInt() == 1);
    const auto& matrix = c0.Get("matrix");
    REQUIRE(matrix.IsArray());
    REQUIRE(matrix.ArrayLen() == 16);
    CHECK(matrix.Get(12).GetNumberAsDouble() == doctest::Approx(0.01));
    CHECK(matrix.Get(13).GetNumberAsDouble() == doctest::Approx(0.02));
    CHECK(matrix.Get(14).GetNumberAsDouble() == doctest::Approx(0.03));
    CHECK(matrix.Get(15).GetNumberAsDouble() == doctest::Approx(1));
}

TEST_CASE("writeGlb: a skeleton with no correctionSets gets no bone_correction_sets extras key") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    CHECK_FALSE(model.skins[0].extras.IsObject());
}

TEST_CASE("writeGlb: a correctionSet entry with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 1;
    husk::gltf::Skeleton::CorrectionSet::Correction c;
    c.joint = 99;
    set.corrections = {c};
    skel.correctionSets = {set};

    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: a skeleton's ribbonAnchors/particleAnchors round-trip as ribbon_emitters/"
          "particle_emitters on the skin's extras -- minimal placement data only") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.ribbonAnchors = {{0xFFFFFFFFu, 1, {0.1f, 0.2f, 0.3f}}};
    skel.particleAnchors = {{42, 0, {1.0f, -1.0f, 0.5f}}, {43, 1, {0, 0, 0}}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());

    const auto& ribbons = extras.Get("ribbon_emitters");
    REQUIRE(ribbons.IsArray());
    REQUIRE(ribbons.ArrayLen() == 1);
    CHECK(ribbons.Get(0).Get("id").GetNumberAsInt() == -1);
    CHECK(ribbons.Get(0).Get("joint").GetNumberAsInt() == 1);
    const auto& rPos = ribbons.Get(0).Get("position");
    CHECK(rPos.Get("x").GetNumberAsDouble() == doctest::Approx(0.1));
    CHECK(rPos.Get("z").GetNumberAsDouble() == doctest::Approx(0.3));

    const auto& particles = extras.Get("particle_emitters");
    REQUIRE(particles.IsArray());
    REQUIRE(particles.ArrayLen() == 2);
    CHECK(particles.Get(0).Get("id").GetNumberAsInt() == 42);
    CHECK(particles.Get(1).Get("joint").GetNumberAsInt() == 1);
}

TEST_CASE("writeGlb: a skeleton with no ribbon/particle anchors gets no such extras keys") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    CHECK_FALSE(model.skins[0].extras.IsObject());
}

TEST_CASE("writeGlb: a ribbon anchor with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.ribbonAnchors = {{0, 99, {0, 0, 0}}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: a particle anchor with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.particleAnchors = {{0, -1, {0, 0, 0}}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: ribbon/particle anchors coexist with bone_correction_sets without "
          "clobbering each other") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 7;
    skel.correctionSets = {set};
    skel.ribbonAnchors = {{0, 0, {0, 0, 0}}};
    skel.particleAnchors = {{0, 0, {0, 0, 0}}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("bone_correction_sets").IsArray());
    CHECK(extras.Get("ribbon_emitters").IsArray());
    CHECK(extras.Get("particle_emitters").IsArray());
}

TEST_CASE("writeGlb: a skeleton's physicsBodies round-trip as physics_bodies on the skin's "
          "extras -- minimal placement data only (DESIGN.md's anchor/dump-chunks split)") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.physicsBodies = {{0, 1, {0.1f, 0.2f, 0.3f}, 0}, {1, 0, {1.0f, -1.0f, 0.5f}, 2}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    const auto& bodies = extras.Get("physics_bodies");
    REQUIRE(bodies.IsArray());
    REQUIRE(bodies.ArrayLen() == 2);
    CHECK(bodies.Get(0).Get("id").GetNumberAsInt() == 0);
    CHECK(bodies.Get(0).Get("joint").GetNumberAsInt() == 1);
    CHECK(bodies.Get(0).Get("body_type").GetNumberAsInt() == 0);
    const auto& pos = bodies.Get(0).Get("position");
    CHECK(pos.Get("x").GetNumberAsDouble() == doctest::Approx(0.1));
    CHECK(pos.Get("z").GetNumberAsDouble() == doctest::Approx(0.3));
    CHECK(bodies.Get(1).Get("id").GetNumberAsInt() == 1);
    CHECK(bodies.Get(1).Get("body_type").GetNumberAsInt() == 2);
}

TEST_CASE("writeGlb: a skeleton with no physics bodies gets no physics_bodies extras key") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    CHECK_FALSE(model.skins[0].extras.IsObject());
}

TEST_CASE("writeGlb: a physics body with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.physicsBodies = {{0, 99, {0, 0, 0}, 0}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: physics bodies coexist with bone_correction_sets/ribbon_emitters/"
          "particle_emitters without clobbering each other") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 7;
    skel.correctionSets = {set};
    skel.ribbonAnchors = {{0, 0, {0, 0, 0}}};
    skel.particleAnchors = {{0, 0, {0, 0, 0}}};
    skel.physicsBodies = {{0, 0, {0, 0, 0}, 0}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("bone_correction_sets").IsArray());
    CHECK(extras.Get("ribbon_emitters").IsArray());
    CHECK(extras.Get("particle_emitters").IsArray());
    CHECK(extras.Get("physics_bodies").IsArray());
}

TEST_CASE("writeGlb: a skeleton's attachments/events/lights become real child nodes, parented "
          "under their joint, not skin extras") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.attachments = {{5, 1, {0.1f, 0.2f, 0.3f}}};
    skel.events = {{"$DTH", 0, {1, 2, 3}}};
    skel.lights = {{2, {4, 5, 6}}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    // No pollution of the skin's extras object -- these are real nodes,
    // not another anchor list like ribbonAnchors/particleAnchors/physicsBodies.
    CHECK_FALSE(model.skins[0].extras.IsObject());

    auto findNamed = [&](const std::string& name) -> const tinygltf::Node* {
        for (const auto& n : model.nodes) {
            if (n.name == name) return &n;
        }
        return nullptr;
    };
    const tinygltf::Node* attachment = findNamed("attachment_5");
    const tinygltf::Node* event = findNamed("event_$DTH");
    const tinygltf::Node* light = findNamed("light_0");
    REQUIRE(attachment != nullptr);
    REQUIRE(event != nullptr);
    REQUIRE(light != nullptr);

    REQUIRE(attachment->translation.size() == 3);
    CHECK(attachment->translation[0] == doctest::Approx(0.1));
    CHECK(attachment->translation[1] == doctest::Approx(0.2));
    CHECK(attachment->translation[2] == doctest::Approx(0.3));
    REQUIRE(event->translation.size() == 3);
    CHECK(event->translation[0] == doctest::Approx(1));
    REQUIRE(light->translation.size() == 3);
    CHECK(light->translation[2] == doctest::Approx(6));

    auto nodeIndex = [&](const tinygltf::Node* n) {
        return static_cast<int>(n - model.nodes.data());
    };
    int attachmentIdx = nodeIndex(attachment);
    int eventIdx = nodeIndex(event);
    int lightIdx = nodeIndex(light);

    // Parented under their owning joint node (joint 1/0/2 respectively) --
    // reached only via that node's `.children`, not listed as a scene root.
    int joint1Node = model.skins[0].joints[1];
    int joint0Node = model.skins[0].joints[0];
    int joint2Node = model.skins[0].joints[2];
    CHECK(std::find(model.nodes[joint1Node].children.begin(),
                     model.nodes[joint1Node].children.end(),
                     attachmentIdx) != model.nodes[joint1Node].children.end());
    CHECK(std::find(model.nodes[joint0Node].children.begin(),
                     model.nodes[joint0Node].children.end(),
                     eventIdx) != model.nodes[joint0Node].children.end());
    CHECK(std::find(model.nodes[joint2Node].children.begin(),
                     model.nodes[joint2Node].children.end(),
                     lightIdx) != model.nodes[joint2Node].children.end());

    // Never added to skin.joints -- same invariant as the multi-root
    // synthesized parent node.
    CHECK(model.skins[0].joints.size() == 3);
    for (int j : model.skins[0].joints) {
        CHECK(j != attachmentIdx);
        CHECK(j != eventIdx);
        CHECK(j != lightIdx);
    }
}

// M2Attachment::animate_attached round-trips as an "animate_attached"
// extras key on the attachment's own node -- same shape/rationale as
// Light's tracks below (no core-glTF slot for this boolean-flag track).
TEST_CASE("writeGlb: an attachment's animateAttached round-trips as 'animate_attached' extras on "
          "its own node") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::Skeleton::Attachment attachment;
    attachment.id = 5;
    attachment.joint = 1;
    attachment.position = {0.1f, 0.2f, 0.3f};
    husk::gltf::Material::AnimatedScalarCurve curve;
    curve.sequenceIndex = 0;
    curve.keyframes = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    attachment.animateAttached = {curve};
    skel.attachments = {attachment};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const tinygltf::Node* node = nullptr;
    for (const auto& n : model.nodes) {
        if (n.name == "attachment_5") node = &n;
    }
    REQUIRE(node != nullptr);
    REQUIRE(node->extras.IsObject());
    const auto& curves = node->extras.Get("animate_attached");
    REQUIRE(curves.IsArray());
    REQUIRE(curves.ArrayLen() == 1);
    REQUIRE(curves.Get(0).Get("keyframes").ArrayLen() == 2);
    CHECK(curves.Get(0).Get("keyframes").Get(1).Get("value").GetNumberAsDouble() == doctest::Approx(0.0));
}

TEST_CASE("writeGlb: an attachment with no animateAttached data gets no 'animate_attached' extras") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.attachments = {{5, 1, {0.1f, 0.2f, 0.3f}}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    for (const auto& n : model.nodes) {
        if (n.name == "attachment_5") {
            CHECK_FALSE(n.extras.IsObject());
        }
    }
}

// M2Light::type plus its 7 animated tracks (ambient/diffuse color+intensity,
// attenuation start/end, visibility) round-trip as a "type"/"light_animation"
// extras pair on the light's own node -- same shape/rationale as Material's
// tint_animation/fade_animation (no core-glTF animation-channel target for a
// light property exists), see gltf_skeleton.hpp's Skeleton::Light doc comment.
TEST_CASE("writeGlb: a light's type/animated tracks round-trip as extras on its own node") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::Skeleton::Light light;
    light.joint = 2;
    light.position = {4, 5, 6};
    light.type = 1;  // point light
    husk::gltf::Material::AnimatedColorCurve ambient;
    ambient.sequenceIndex = 0;
    ambient.keyframes = {{0.0f, {0.9f, 0.7f, 0.6f}}};
    light.ambientColor = {ambient};
    husk::gltf::Material::AnimatedScalarCurve visibility;
    visibility.sequenceIndex = 0;
    visibility.keyframes = {{0.0f, 1.0f}, {2.5f, 0.0f}};
    light.visibility = {visibility};
    skel.lights = {light};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const tinygltf::Node* lightNode = nullptr;
    for (const auto& n : model.nodes) {
        if (n.name == "light_0") lightNode = &n;
    }
    REQUIRE(lightNode != nullptr);
    const auto& extras = lightNode->extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("type").GetNumberAsInt() == 1);

    const auto& anim = extras.Get("light_animation");
    REQUIRE(anim.IsObject());
    const auto& ambientOut = anim.Get("ambient_color");
    REQUIRE(ambientOut.IsArray());
    REQUIRE(ambientOut.ArrayLen() == 1);
    CHECK(ambientOut.Get(0).Get("sequence_index").GetNumberAsInt() == 0);
    CHECK(ambientOut.Get(0).Get("keyframes").Get(0).Get("value").Get(0).GetNumberAsDouble() ==
          doctest::Approx(0.9));

    const auto& visOut = anim.Get("visibility");
    REQUIRE(visOut.IsArray());
    REQUIRE(visOut.ArrayLen() == 1);
    REQUIRE(visOut.Get(0).Get("keyframes").ArrayLen() == 2);
    CHECK(visOut.Get(0).Get("keyframes").Get(1).Get("time").GetNumberAsDouble() == doctest::Approx(2.5));
    CHECK(visOut.Get(0).Get("keyframes").Get(1).Get("value").GetNumberAsDouble() == doctest::Approx(0.0));

    // No data for these tracks -- key must be absent, not an empty array.
    CHECK_FALSE(anim.Get("diffuse_color").IsArray());
    CHECK_FALSE(anim.Get("attenuation_start").IsArray());
}

TEST_CASE("writeGlb: a light with no animated tracks gets no 'light_animation' extras key, just "
          "'type'") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::Skeleton::Light light;
    light.joint = 0;
    light.position = {0, 0, 0};
    light.type = 0;
    skel.lights = {light};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const tinygltf::Node* lightNode = nullptr;
    for (const auto& n : model.nodes) {
        if (n.name == "light_0") lightNode = &n;
    }
    REQUIRE(lightNode != nullptr);
    REQUIRE(lightNode->extras.IsObject());
    CHECK(lightNode->extras.Get("type").GetNumberAsInt() == 0);
    CHECK_FALSE(lightNode->extras.Get("light_animation").IsObject());
}

TEST_CASE("writeGlb: a skeleton with no attachments/events/lights adds no anchor nodes") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    // 1 mesh node + 3 joint nodes, nothing else (single-root chain, no
    // synthesized parent node either).
    CHECK(model.nodes.size() == 4);
}

TEST_CASE("writeGlb: an attachment with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.attachments = {{0, 99, {0, 0, 0}}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: an event with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.events = {{"$DTH", 99, {0, 0, 0}}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: a light with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.lights = {{-1, {0, 0, 0}}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: attachment/event/light nodes coexist with bone_correction_sets/"
          "ribbon_emitters/particle_emitters/physics_bodies extras without clobbering "
          "each other") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 7;
    skel.correctionSets = {set};
    skel.ribbonAnchors = {{0, 0, {0, 0, 0}}};
    skel.particleAnchors = {{0, 0, {0, 0, 0}}};
    skel.physicsBodies = {{0, 0, {0, 0, 0}, 0}};
    skel.attachments = {{0, 1, {0, 0, 0}}};
    skel.events = {{"$DTH", 0, {0, 0, 0}}};
    skel.lights = {{2, {0, 0, 0}}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("bone_correction_sets").IsArray());
    CHECK(extras.Get("ribbon_emitters").IsArray());
    CHECK(extras.Get("particle_emitters").IsArray());
    CHECK(extras.Get("physics_bodies").IsArray());

    // 1 mesh + 3 joints + 1 attachment + 1 event + 1 light.
    CHECK(model.nodes.size() == 7);
    int found = 0;
    for (const auto& n : model.nodes) {
        if (n.name == "attachment_0" || n.name == "event_$DTH" || n.name == "light_0") {
            ++found;
        }
    }
    CHECK(found == 3);
}

// TODO/GEOSET_MASK_TODO.md's core mechanism: one placeholder joint per distinct
// geoset ID, appended to skin.joints after every real bone, woven into a
// second JOINTS_1/WEIGHTS_1 set for whichever vertices its primitive
// touches -- with both weight sets rescaled so the combined per-vertex
// total is still 1.0 (a real gltf-validator run caught the unscaled
// version emitting a "non-normalized sum: 2.0" error on both sets).
TEST_CASE("writeGlb: a geoset tag becomes an extra skin joint with real JOINTS_1/WEIGHTS_1 data, "
          "real bone weights rescaled to keep the combined per-vertex total at 1.0") {
    auto mesh = buildSkinnedTriangleMesh();
    mesh.primitives[0].skinSectionId = 401;
    auto skel = buildChainSkeleton();
    skel.geosetTags = {{401}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.skins.size() == 1);
    const auto& skin = model.skins[0];
    // 3 real joints + 1 geoset tag joint -- the real joints' own indices
    // (0..2) must stay exactly as they were before this feature existed.
    REQUIRE(skin.joints.size() == 4);
    int tagNode = skin.joints[3];
    // "group_<n>,variant_<n>" (comma-separated, prefix-tagged fields), not
    // a single "geoset_<id>" token -- so a future Blender-side script can
    // recover the raw integers with a plain comma-split + prefix-strip
    // (TODO/GEOSET_MASK_TODO.md). 401 -> group 4, variant 1.
    CHECK(model.nodes[tagNode].name == "group_4,variant_1");
    // Never posed: identity translation, no rotation/scale override.
    REQUIRE(model.nodes[tagNode].translation.size() == 3);
    CHECK(model.nodes[tagNode].translation[0] == doctest::Approx(0));
    CHECK(model.nodes[tagNode].translation[1] == doctest::Approx(0));
    CHECK(model.nodes[tagNode].translation[2] == doctest::Approx(0));
    // Parented under the single real root joint (skin.joints[0]) -- a
    // genuine descendant, preserving the "closest common root" property a
    // skin's joint hierarchy needs, not a bare, unreachable extra node.
    int rootNode = skin.joints[0];
    CHECK(std::find(model.nodes[rootNode].children.begin(), model.nodes[rootNode].children.end(),
                     tagNode) != model.nodes[rootNode].children.end());

    const auto& prim = model.meshes[0].primitives[0];
    REQUIRE(prim.attributes.count("JOINTS_1") == 1);
    REQUIRE(prim.attributes.count("WEIGHTS_1") == 1);

    const auto& j1Acc = model.accessors[prim.attributes.at("JOINTS_1")];
    REQUIRE(j1Acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
    REQUIRE(j1Acc.count == 3);
    const auto& j1View = model.bufferViews[j1Acc.bufferView];
    const auto& j1Buf = model.buffers[j1View.buffer];
    std::vector<uint16_t> joints1(4 * 3);
    std::memcpy(joints1.data(), j1Buf.data.data() + j1View.byteOffset + j1Acc.byteOffset,
                joints1.size() * sizeof(uint16_t));
    const auto& w1Acc = model.accessors[prim.attributes.at("WEIGHTS_1")];
    const auto& w1View = model.bufferViews[w1Acc.bufferView];
    const auto& w1Buf = model.buffers[w1View.buffer];
    std::vector<float> weights1(4 * 3);
    std::memcpy(weights1.data(), w1Buf.data.data() + w1View.byteOffset + w1Acc.byteOffset,
                weights1.size() * sizeof(float));

    const auto& w0Acc = model.accessors[prim.attributes.at("WEIGHTS_0")];
    const auto& w0View = model.bufferViews[w0Acc.bufferView];
    const auto& w0Buf = model.buffers[w0View.buffer];
    std::vector<float> weights0(4 * 3);
    std::memcpy(weights0.data(), w0Buf.data.data() + w0View.byteOffset + w0Acc.byteOffset,
                weights0.size() * sizeof(float));

    // Every vertex is touched by this primitive (its only one), so every
    // vertex is tagged with skin-relative joint index 3 (the tag) at 0.5
    // -- and its real-bone WEIGHTS_0 (originally a full 1.0 to a single
    // joint) rescaled down to 0.5 too, keeping the combined total at 1.0.
    for (size_t v = 0; v < 3; ++v) {
        CHECK(joints1[v * 4 + 0] == 3);
        CHECK(weights1[v * 4 + 0] == doctest::Approx(0.5));
        CHECK(weights0[v * 4 + 0] == doctest::Approx(0.5));
        float total = weights0[v * 4 + 0] + weights1[v * 4 + 0];
        CHECK(total == doctest::Approx(1.0));
    }
}

// A model with no geosets at all (every Primitive::skinSectionId left at
// -1, the .skin-less fallback case) must be completely unaffected -- no
// tag joints, no JOINTS_1/WEIGHTS_1 attributes, output identical to before
// this feature existed.
TEST_CASE("writeGlb: no geosetTags means no tag joints and no JOINTS_1/WEIGHTS_1 at all") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == 3);
    const auto& prim = model.meshes[0].primitives[0];
    CHECK(prim.attributes.count("JOINTS_1") == 0);
    CHECK(prim.attributes.count("WEIGHTS_1") == 0);
}

// A single writeGlb mesh may legitimately share a skeleton without being
// skinned by it (empty mesh.skinning opts out -- see writeGlbMulti's doc
// comment); the still-real error case is skinning data that's *present*
// but the wrong length.
TEST_CASE("writeGlb: skeleton given with no mesh.skinning succeeds, mesh node gets no skin") {
    auto mesh = buildTriangleMesh();  // no skinning data
    auto skel = buildChainSkeleton();
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);
    REQUIRE(!model.nodes.empty());
    CHECK(model.nodes[0].skin == -1);
}

TEST_CASE("writeGlb: skeleton given with mesh.skinning present but the wrong length throws") {
    auto mesh = buildTriangleMesh();  // 3 positions
    mesh.skinning = {husk::gltf::JointWeights{}};  // 1 entry
    auto skel = buildChainSkeleton();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: mesh.skinning given without a skeleton throws") {
    auto mesh = buildSkinnedTriangleMesh();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, nullptr), husk::gltf::Error);
}

TEST_CASE("writeGlb: mesh.skinning length mismatched with positions throws") {
    auto mesh = buildSkinnedTriangleMesh();
    mesh.skinning.pop_back();
    auto skel = buildChainSkeleton();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: joint parent index out of range throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.joints[2].parent = 99;
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: joint that is its own parent throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.joints[1].parent = 1;
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: an animation round-trips translation/rotation/scale keyframes through "
          "tinygltf's own loader") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 1;  // the "mid" joint
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 2, 0}, {0, 3, 0}};
    ja.rotationTimes = {0.0f, 1.0f};
    ja.rotationValues = {{0, 0, 0, 1}, {0.7071f, 0, 0, 0.7071f}};
    ja.scaleTimes = {0.0f};
    ja.scaleValues = {{2, 2, 2}};

    husk::gltf::Animation anim;
    anim.name = "anim_100_0";
    anim.joints = {ja};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    const auto& ga = model.animations[0];
    CHECK(ga.name == "anim_100_0");
    REQUIRE(ga.channels.size() == 3);

    // Joint 1 -> node index 2 (mesh node is 0, joint i is node 1+i).
    for (const auto& ch : ga.channels) {
        CHECK(ch.target_node == 2);
    }

    auto findChannel = [&](const std::string& path) -> const tinygltf::AnimationChannel& {
        for (const auto& ch : ga.channels) {
            if (ch.target_path == path) return ch;
        }
        FAIL("no channel for path " << path);
        return ga.channels[0];
    };

    const auto& transCh = findChannel("translation");
    const auto& transSamp = ga.samplers[transCh.sampler];
    CHECK(transSamp.interpolation == "LINEAR");
    const auto& transOutAcc = model.accessors[transSamp.output];
    REQUIRE(transOutAcc.count == 2);
    const auto& transOutView = model.bufferViews[transOutAcc.bufferView];
    const auto& transBuf = model.buffers[transOutView.buffer];
    std::vector<husk::gltf::Vec3> transValues(2);
    std::memcpy(transValues.data(), transBuf.data.data() + transOutView.byteOffset,
                2 * sizeof(husk::gltf::Vec3));
    CHECK(transValues[0].y == doctest::Approx(2));
    CHECK(transValues[1].y == doctest::Approx(3));

    const auto& transInAcc = model.accessors[transSamp.input];
    REQUIRE(transInAcc.minValues.size() == 1);
    CHECK(transInAcc.minValues[0] == doctest::Approx(0.0));
    CHECK(transInAcc.maxValues[0] == doctest::Approx(1.0));

    const auto& rotCh = findChannel("rotation");
    const auto& rotSamp = ga.samplers[rotCh.sampler];
    const auto& rotOutAcc = model.accessors[rotSamp.output];
    REQUIRE(rotOutAcc.count == 2);
    CHECK(rotOutAcc.type == TINYGLTF_TYPE_VEC4);
    const auto& rotOutView = model.bufferViews[rotOutAcc.bufferView];
    const auto& rotBuf = model.buffers[rotOutView.buffer];
    std::vector<float> rotValues(4 * 2);
    std::memcpy(rotValues.data(), rotBuf.data.data() + rotOutView.byteOffset,
                rotValues.size() * sizeof(float));
    // Keyframe 1: (0.7071, 0, 0, 0.7071) -- x/y/z/w order preserved.
    CHECK(rotValues[4 + 0] == doctest::Approx(0.7071f));
    CHECK(rotValues[4 + 3] == doctest::Approx(0.7071f));

    const auto& scaleCh = findChannel("scale");
    const auto& scaleSamp = ga.samplers[scaleCh.sampler];
    CHECK(model.accessors[scaleSamp.output].count == 1);
}

TEST_CASE("writeGlb: JointAnimation's per-property step flags become STEP samplers, "
          "independent of one another") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 0, 0}, {1, 1, 1}};
    ja.translationStep = true;
    ja.rotationTimes = {0.0f, 1.0f};
    ja.rotationValues = {{0, 0, 0, 1}, {0, 0, 0, 1}};
    // rotationStep left false (the default) -- proves the three flags are
    // read independently, not one shared setting for the whole joint.

    husk::gltf::Animation anim;
    anim.joints = {ja};
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    const auto& ga = model.animations[0];
    for (const auto& ch : ga.channels) {
        const auto& samp = ga.samplers[ch.sampler];
        if (ch.target_path == "translation") {
            CHECK(samp.interpolation == "STEP");
        } else if (ch.target_path == "rotation") {
            CHECK(samp.interpolation == "LINEAR");
        }
    }
}

TEST_CASE("writeGlb: a JointAnimation with only some TRS properties populated emits only those "
          "channels") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{1, 1, 1}};
    // rotation/scale left empty.

    husk::gltf::Animation anim;
    anim.joints = {ja};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    REQUIRE(model.animations[0].channels.size() == 1);
    CHECK(model.animations[0].channels[0].target_path == "translation");
}

TEST_CASE("writeGlb: animations given without a skeleton throws") {
    auto mesh = buildTriangleMesh();
    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};
    husk::gltf::Animation anim;
    anim.joints = {ja};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, nullptr, {anim}), husk::gltf::Error);
}

TEST_CASE("writeGlb: an animation joint index out of range for the skeleton throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::JointAnimation ja;
    ja.joint = 99;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};
    husk::gltf::Animation anim;
    anim.joints = {ja};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel, {anim}), husk::gltf::Error);
}

TEST_CASE("writeGlb: an animation joint with mismatched time/value counts throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 0, 0}};  // only 1 value for 2 times
    husk::gltf::Animation anim;
    anim.joints = {ja};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel, {anim}), husk::gltf::Error);
}

TEST_CASE("writeGlb: an animation's sequenceMetadata round-trips as sequence_metadata on the clip's "
          "own extras") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};

    husk::gltf::Animation anim;
    anim.name = "anim_100_0";
    anim.joints = {ja};
    husk::gltf::Animation::SequenceMetadata sm;
    sm.movespeed = 4.5f;
    sm.frequency = -3;
    sm.replayMin = 10;
    sm.replayMax = 20;
    sm.blendTimeIn = 30;
    sm.blendTimeOut = 40;
    sm.boundsMin = {1, 2, 3};
    sm.boundsMax = {4, 5, 6};
    sm.boundsRadius = 7.5f;
    sm.variationNext = 5;
    sm.aliasNext = 9;
    sm.isAlias = true;
    anim.sequenceMetadata = sm;

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    const auto& extras = model.animations[0].extras;
    REQUIRE(extras.IsObject());
    const auto& meta = extras.Get("sequence_metadata");
    CHECK(meta.Get("movespeed").GetNumberAsDouble() == doctest::Approx(4.5));
    CHECK(meta.Get("frequency").GetNumberAsInt() == -3);
    CHECK(meta.Get("replay_min").GetNumberAsInt() == 10);
    CHECK(meta.Get("replay_max").GetNumberAsInt() == 20);
    CHECK(meta.Get("blend_time_in").GetNumberAsInt() == 30);
    CHECK(meta.Get("blend_time_out").GetNumberAsInt() == 40);
    const auto& boundsMin = meta.Get("bounds_min");
    REQUIRE(boundsMin.IsArray());
    CHECK(boundsMin.Get(0).GetNumberAsDouble() == doctest::Approx(1));
    CHECK(boundsMin.Get(1).GetNumberAsDouble() == doctest::Approx(2));
    CHECK(boundsMin.Get(2).GetNumberAsDouble() == doctest::Approx(3));
    const auto& boundsMax = meta.Get("bounds_max");
    REQUIRE(boundsMax.IsArray());
    CHECK(boundsMax.Get(0).GetNumberAsDouble() == doctest::Approx(4));
    CHECK(boundsMax.Get(1).GetNumberAsDouble() == doctest::Approx(5));
    CHECK(boundsMax.Get(2).GetNumberAsDouble() == doctest::Approx(6));
    CHECK(meta.Get("bounds_radius").GetNumberAsDouble() == doctest::Approx(7.5));
    CHECK(meta.Get("variation_next").GetNumberAsInt() == 5);
    CHECK(meta.Get("alias_next").GetNumberAsInt() == 9);
    CHECK(meta.Get("is_alias").Get<bool>() == true);
}

TEST_CASE("writeGlb: an animation with no sequenceMetadata gets no extras on the clip") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};
    husk::gltf::Animation anim;
    anim.joints = {ja};
    // sequenceMetadata left nullopt (default) -- e.g. a global_seq_<n> clip.

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    CHECK_FALSE(model.animations[0].extras.IsObject());
}
