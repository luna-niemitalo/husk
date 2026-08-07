// Shared mesh/skeleton-building helpers for tests/test_gltf_*.cpp --
// factored out here because buildTriangleMesh/loadBack/buildChainSkeleton/
// buildSkinnedTriangleMesh/buildTwoPrimitiveQuad are each used by 3+ of the
// split files (see FILE_SPLIT_TODO.md Item 5). Anonymous namespace: each
// including TU gets its own private copy, same as when these lived inline
// in the pre-split tests/test_gltf.cpp.
#pragma once

#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "../src/gltf.hpp"

namespace {

husk::gltf::Mesh buildTriangleMesh() {
    husk::gltf::Mesh mesh;
    mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    mesh.texCoords = {{0, 0}, {1, 0}, {0, 1}};
    husk::gltf::Primitive prim;
    prim.indices = {0, 1, 2};
    mesh.primitives = {prim};
    return mesh;
}

tinygltf::Model loadBack(const std::vector<uint8_t>& glb) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    bool ok = loader.LoadBinaryFromMemory(&model, &err, &warn, glb.data(),
                                           static_cast<unsigned int>(glb.size()));
    INFO("tinygltf error: ", err);
    REQUIRE(ok);
    return model;
}

// A 3-joint chain: root (0) -> mid (1) -> tip (2), at global positions
// (0,0,0), (0,2,0), (0,2,3). Every triangle vertex is fully weighted to a
// different joint, so the round-trip test can tell them apart.
husk::gltf::Skeleton buildChainSkeleton() {
    husk::gltf::Skeleton skel;
    skel.joints.push_back({-1, {0, 0, 0}, {0, 0, 0}});
    skel.joints.push_back({0, {0, 2, 0}, {0, 2, 0}});
    skel.joints.push_back({1, {0, 0, 3}, {0, 2, 3}});
    return skel;
}

husk::gltf::Mesh buildSkinnedTriangleMesh() {
    auto mesh = buildTriangleMesh();
    husk::gltf::JointWeights w0, w1, w2;
    w0.joints[0] = 0;
    w0.weights[0] = 1.0f;
    w1.joints[0] = 1;
    w1.weights[0] = 1.0f;
    w2.joints[0] = 2;
    w2.weights[0] = 1.0f;
    mesh.skinning = {w0, w1, w2};
    return mesh;
}

// A 2-triangle quad (0,1,2 and 1,3,2), each triangle its own primitive so a
// test can prove they get independent index buffers and materials -- roadmap
// stage 5 (see README.md), where one M2 submesh/batch becomes one primitive.
husk::gltf::Mesh buildTwoPrimitiveQuad() {
    husk::gltf::Mesh mesh;
    mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    mesh.texCoords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

    husk::gltf::Primitive p0;
    p0.indices = {0, 1, 2};
    p0.materialIndex = 0;
    husk::gltf::Primitive p1;
    p1.indices = {1, 3, 2};
    p1.materialIndex = 1;
    mesh.primitives = {p0, p1};

    return mesh;
}

}  // namespace
