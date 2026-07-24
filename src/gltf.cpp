#include "gltf.hpp"

#include <algorithm>
#include <array>
#include <sstream>

#include <tiny_gltf.h>

namespace husk::gltf {

namespace {

// Appends `data`'s raw bytes to `buffer` and returns a BufferView covering
// exactly that span. `target` is TINYGLTF_TARGET_ARRAY_BUFFER for vertex
// attributes, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER for indices, or 0 for
// data that isn't a vertex/index buffer at all (e.g. inverse bind matrices).
template <typename T>
int appendBufferView(tinygltf::Buffer& buffer, std::vector<tinygltf::BufferView>& views,
                      const std::vector<T>& data, int target) {
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = buffer.data.size();
    view.byteLength = data.size() * sizeof(T);
    view.target = target;

    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    buffer.data.insert(buffer.data.end(), bytes, bytes + view.byteLength);

    views.push_back(view);
    return static_cast<int>(views.size()) - 1;
}

}  // namespace

Vec3 zUpToYUp(const Vec3& v) { return {v.x, -v.z, v.y}; }

std::vector<uint8_t> writeGlb(const Mesh& mesh, const Skeleton* skeleton) {
    size_t n = mesh.positions.size();
    if (mesh.normals.size() != n || mesh.texCoords.size() != n) {
        throw Error("writeGlb: positions (" + std::to_string(n) + "), normals (" +
                    std::to_string(mesh.normals.size()) + "), and texCoords (" +
                    std::to_string(mesh.texCoords.size()) + ") must all be the same length");
    }
    if (mesh.indices.empty()) {
        throw Error("writeGlb: indices must not be empty");
    }
    if (mesh.indices.size() % 3 != 0) {
        throw Error("writeGlb: indices count (" + std::to_string(mesh.indices.size()) +
                    ") must be a multiple of 3 (one triangle per 3 entries)");
    }

    bool hasSkeleton = skeleton != nullptr && !skeleton->joints.empty();
    if (hasSkeleton && mesh.skinning.size() != n) {
        throw Error("writeGlb: a skeleton was given but mesh.skinning (" +
                    std::to_string(mesh.skinning.size()) + ") doesn't match positions (" +
                    std::to_string(n) + ") -- both or neither");
    }
    if (!hasSkeleton && !mesh.skinning.empty()) {
        throw Error(
            "writeGlb: mesh.skinning was given without a skeleton -- both or neither");
    }
    if (hasSkeleton) {
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            int parent = skeleton->joints[i].parent;
            if (parent == static_cast<int>(i)) {
                throw Error("writeGlb: joint " + std::to_string(i) + " is its own parent");
            }
            if (parent != -1 &&
                (parent < 0 || static_cast<size_t>(parent) >= skeleton->joints.size())) {
                throw Error("writeGlb: joint " + std::to_string(i) + "'s parent (" +
                            std::to_string(parent) + ") is out of range for " +
                            std::to_string(skeleton->joints.size()) + " joints");
            }
        }
    }

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "husk";

    tinygltf::Buffer buffer;
    std::vector<tinygltf::BufferView> views;
    std::vector<tinygltf::Accessor> accessors;

    int posView = appendBufferView(buffer, views, mesh.positions, TINYGLTF_TARGET_ARRAY_BUFFER);
    int normView = appendBufferView(buffer, views, mesh.normals, TINYGLTF_TARGET_ARRAY_BUFFER);
    int uvView = appendBufferView(buffer, views, mesh.texCoords, TINYGLTF_TARGET_ARRAY_BUFFER);
    int idxView =
        appendBufferView(buffer, views, mesh.indices, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);

    Vec3 posMin = mesh.positions[0], posMax = mesh.positions[0];
    for (const auto& p : mesh.positions) {
        posMin.x = std::min(posMin.x, p.x);
        posMin.y = std::min(posMin.y, p.y);
        posMin.z = std::min(posMin.z, p.z);
        posMax.x = std::max(posMax.x, p.x);
        posMax.y = std::max(posMax.y, p.y);
        posMax.z = std::max(posMax.z, p.z);
    }

    tinygltf::Accessor posAcc;
    posAcc.bufferView = posView;
    posAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    posAcc.count = n;
    posAcc.type = TINYGLTF_TYPE_VEC3;
    posAcc.minValues = {posMin.x, posMin.y, posMin.z};
    posAcc.maxValues = {posMax.x, posMax.y, posMax.z};
    int posAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(posAcc);

    tinygltf::Accessor normAcc;
    normAcc.bufferView = normView;
    normAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    normAcc.count = n;
    normAcc.type = TINYGLTF_TYPE_VEC3;
    int normAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(normAcc);

    tinygltf::Accessor uvAcc;
    uvAcc.bufferView = uvView;
    uvAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    uvAcc.count = n;
    uvAcc.type = TINYGLTF_TYPE_VEC2;
    int uvAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(uvAcc);

    tinygltf::Accessor idxAcc;
    idxAcc.bufferView = idxView;
    idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    idxAcc.count = mesh.indices.size();
    idxAcc.type = TINYGLTF_TYPE_SCALAR;
    int idxAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(idxAcc);

    tinygltf::Primitive prim;
    prim.attributes["POSITION"] = posAccIdx;
    prim.attributes["NORMAL"] = normAccIdx;
    prim.attributes["TEXCOORD_0"] = uvAccIdx;
    prim.indices = idxAccIdx;
    prim.mode = TINYGLTF_MODE_TRIANGLES;

    int skinIdx = -1;
    std::vector<tinygltf::Node> jointNodes;
    std::vector<int> rootJointNodeIndices;

    if (hasSkeleton) {
        std::vector<std::array<uint8_t, 4>> jointsFlat;
        std::vector<std::array<float, 4>> weightsFlat;
        jointsFlat.reserve(n);
        weightsFlat.reserve(n);
        for (const auto& jw : mesh.skinning) {
            std::array<uint8_t, 4> j;
            std::copy(std::begin(jw.joints), std::end(jw.joints), j.begin());
            jointsFlat.push_back(j);
            std::array<float, 4> w;
            std::copy(std::begin(jw.weights), std::end(jw.weights), w.begin());
            weightsFlat.push_back(w);
        }
        int jointsView = appendBufferView(buffer, views, jointsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);
        int weightsView =
            appendBufferView(buffer, views, weightsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);

        tinygltf::Accessor jAcc;
        jAcc.bufferView = jointsView;
        jAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        jAcc.count = n;
        jAcc.type = TINYGLTF_TYPE_VEC4;
        int jAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(jAcc);

        tinygltf::Accessor wAcc;
        wAcc.bufferView = weightsView;
        wAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        wAcc.count = n;
        wAcc.type = TINYGLTF_TYPE_VEC4;
        int wAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(wAcc);

        prim.attributes["JOINTS_0"] = jAccIdx;
        prim.attributes["WEIGHTS_0"] = wAccIdx;

        // Pure-translation inverse bind matrices (see Skeleton's doc
        // comment for why M2's bind pose never needs a rotation/scale
        // component here), 16 column-major floats per joint.
        std::vector<float> ibmFlat;
        ibmFlat.reserve(skeleton->joints.size() * 16);
        for (const auto& j : skeleton->joints) {
            const Vec3& p = j.globalPosition;
            float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p.x, -p.y, -p.z, 1};
            ibmFlat.insert(ibmFlat.end(), std::begin(m), std::end(m));
        }
        int ibmView = appendBufferView(buffer, views, ibmFlat, /*target=*/0);
        tinygltf::Accessor ibmAcc;
        ibmAcc.bufferView = ibmView;
        ibmAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        ibmAcc.count = skeleton->joints.size();
        ibmAcc.type = TINYGLTF_TYPE_MAT4;
        int ibmAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(ibmAcc);

        // Mesh node is index 0; joint i becomes node (1 + i).
        jointNodes.resize(skeleton->joints.size());
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            const auto& src = skeleton->joints[i];
            tinygltf::Node& node = jointNodes[i];
            node.translation = {src.localTranslation.x, src.localTranslation.y,
                                 src.localTranslation.z};
        }
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            int parent = skeleton->joints[i].parent;
            int nodeIdx = static_cast<int>(1 + i);
            if (parent == -1) {
                rootJointNodeIndices.push_back(nodeIdx);
            } else {
                jointNodes[static_cast<size_t>(parent)].children.push_back(nodeIdx);
            }
        }

        tinygltf::Skin skin;
        skin.inverseBindMatrices = ibmAccIdx;
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            skin.joints.push_back(static_cast<int>(1 + i));
        }
        model.skins = {skin};
        skinIdx = 0;
    }

    model.bufferViews = views;
    model.accessors = accessors;

    tinygltf::Mesh gltfMesh;
    gltfMesh.primitives = {prim};
    model.meshes = {gltfMesh};

    tinygltf::Node meshNode;
    meshNode.mesh = 0;
    if (skinIdx >= 0) {
        meshNode.skin = skinIdx;
    }
    model.nodes = {meshNode};
    for (auto& jointNode : jointNodes) {
        model.nodes.push_back(jointNode);
    }

    tinygltf::Scene scene;
    scene.nodes = {0};
    for (int rootIdx : rootJointNodeIndices) {
        scene.nodes.push_back(rootIdx);
    }
    model.scenes = {scene};
    model.defaultScene = 0;

    model.buffers = {buffer};

    tinygltf::TinyGLTF writer;
    std::ostringstream out(std::ios::binary);
    if (!writer.WriteGltfSceneToStream(&model, out, /*prettyPrint=*/false, /*writeBinary=*/true)) {
        throw Error("writeGlb: tinygltf failed to serialize the model");
    }

    std::string bytes = out.str();
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

}  // namespace husk::gltf
