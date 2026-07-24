#include "gltf.hpp"

#include <algorithm>
#include <sstream>

#include <tiny_gltf.h>

namespace husk::gltf {

namespace {

// Appends `data`'s raw bytes to `buffer` and returns a BufferView covering
// exactly that span. `target` is TINYGLTF_TARGET_ARRAY_BUFFER for vertex
// attributes or TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER for indices.
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

std::vector<uint8_t> writeGlb(const Mesh& mesh) {
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

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "husk";

    tinygltf::Buffer buffer;
    std::vector<tinygltf::BufferView> views;

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

    tinygltf::Accessor normAcc;
    normAcc.bufferView = normView;
    normAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    normAcc.count = n;
    normAcc.type = TINYGLTF_TYPE_VEC3;

    tinygltf::Accessor uvAcc;
    uvAcc.bufferView = uvView;
    uvAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    uvAcc.count = n;
    uvAcc.type = TINYGLTF_TYPE_VEC2;

    tinygltf::Accessor idxAcc;
    idxAcc.bufferView = idxView;
    idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    idxAcc.count = mesh.indices.size();
    idxAcc.type = TINYGLTF_TYPE_SCALAR;

    model.bufferViews = views;
    model.accessors = {posAcc, normAcc, uvAcc, idxAcc};

    tinygltf::Primitive prim;
    prim.attributes["POSITION"] = 0;
    prim.attributes["NORMAL"] = 1;
    prim.attributes["TEXCOORD_0"] = 2;
    prim.indices = 3;
    prim.mode = TINYGLTF_MODE_TRIANGLES;

    tinygltf::Mesh gltfMesh;
    gltfMesh.primitives = {prim};
    model.meshes = {gltfMesh};

    tinygltf::Node node;
    node.mesh = 0;
    model.nodes = {node};

    tinygltf::Scene scene;
    scene.nodes = {0};
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
