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

namespace {

const char* alphaModeString(Material::AlphaMode mode) {
    switch (mode) {
        case Material::AlphaMode::Opaque: return "OPAQUE";
        case Material::AlphaMode::Mask: return "MASK";
        case Material::AlphaMode::Blend: return "BLEND";
    }
    return "OPAQUE";
}

}  // namespace

std::vector<uint8_t> writeGlbMulti(const std::vector<NamedMesh>& meshes, const Skeleton* skeleton,
                                    const std::vector<Animation>& animations) {
    if (meshes.empty()) {
        throw Error("writeGlbMulti: meshes must not be empty");
    }

    bool hasSkeleton = skeleton != nullptr && !skeleton->joints.empty();
    if (hasSkeleton) {
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            int parent = skeleton->joints[i].parent;
            if (parent == static_cast<int>(i)) {
                throw Error("writeGlbMulti: joint " + std::to_string(i) + " is its own parent");
            }
            if (parent != -1 &&
                (parent < 0 || static_cast<size_t>(parent) >= skeleton->joints.size())) {
                throw Error("writeGlbMulti: joint " + std::to_string(i) + "'s parent (" +
                            std::to_string(parent) + ") is out of range for " +
                            std::to_string(skeleton->joints.size()) + " joints");
            }
        }
    }

    if (!animations.empty() && !hasSkeleton) {
        throw Error("writeGlbMulti: animations were given without a skeleton");
    }
    for (size_t ai = 0; ai < animations.size(); ++ai) {
        for (size_t ji = 0; ji < animations[ai].joints.size(); ++ji) {
            const auto& ja = animations[ai].joints[ji];
            if (ja.joint < 0 || static_cast<size_t>(ja.joint) >= skeleton->joints.size()) {
                throw Error("writeGlbMulti: animation " + std::to_string(ai) + "'s joint entry " +
                            std::to_string(ji) + " (joint " + std::to_string(ja.joint) +
                            ") is out of range for " + std::to_string(skeleton->joints.size()) +
                            " joints");
            }
            if (ja.translationTimes.size() != ja.translationValues.size() ||
                ja.rotationTimes.size() != ja.rotationValues.size() ||
                ja.scaleTimes.size() != ja.scaleValues.size()) {
                throw Error("writeGlbMulti: animation " + std::to_string(ai) + "'s joint " +
                            std::to_string(ja.joint) +
                            " has mismatched keyframe time/value counts");
            }
        }
    }

    // Per-entry validation, same checks (and same reasons) as writeGlb's
    // single mesh/materials pair -- see gltf.hpp's doc comments.
    for (size_t mi = 0; mi < meshes.size(); ++mi) {
        const Mesh& mesh = meshes[mi].mesh;
        const auto& materials = meshes[mi].materials;
        size_t n = mesh.positions.size();
        if (mesh.normals.size() != n || mesh.texCoords.size() != n) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s positions (" +
                        std::to_string(n) + "), normals (" + std::to_string(mesh.normals.size()) +
                        "), and texCoords (" + std::to_string(mesh.texCoords.size()) +
                        ") must all be the same length");
        }
        if (!mesh.texCoords2.empty() && mesh.texCoords2.size() != n) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s texCoords2 (" +
                        std::to_string(mesh.texCoords2.size()) + ") was given but doesn't match "
                        "positions (" + std::to_string(n) + ")");
        }
        if (mesh.primitives.empty()) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitives must not be "
                        "empty");
        }
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& prim = mesh.primitives[pi];
            if (prim.indices.empty()) {
                throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                            std::to_string(pi) + " indices must not be empty");
            }
            if (prim.indices.size() % 3 != 0) {
                throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                            std::to_string(pi) + "'s indices count (" +
                            std::to_string(prim.indices.size()) +
                            ") must be a multiple of 3 (one triangle per 3 entries)");
            }
            for (uint32_t idx : prim.indices) {
                if (idx >= n) {
                    throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                                std::to_string(pi) + "'s indices reference " +
                                std::to_string(idx) + " but there are only " + std::to_string(n) +
                                " positions");
                }
            }
            if (prim.materialIndex != -1 &&
                (prim.materialIndex < 0 ||
                 static_cast<size_t>(prim.materialIndex) >= materials.size())) {
                throw Error("writeGlbMulti: mesh " + std::to_string(mi) + "'s primitive " +
                            std::to_string(pi) + "'s materialIndex (" +
                            std::to_string(prim.materialIndex) + ") is out of range for " +
                            std::to_string(materials.size()) + " materials");
            }
        }
        if (hasSkeleton && mesh.skinning.size() != n) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + " has a skeleton but its "
                        "skinning (" + std::to_string(mesh.skinning.size()) +
                        ") doesn't match positions (" + std::to_string(n) + ")");
        }
        if (!hasSkeleton && !mesh.skinning.empty()) {
            throw Error("writeGlbMulti: mesh " + std::to_string(mi) + " has skinning data without "
                        "a skeleton");
        }
    }

    tinygltf::Model model;
    model.asset.version = "2.0";
    model.asset.generator = "husk";

    tinygltf::Buffer buffer;
    std::vector<tinygltf::BufferView> views;
    std::vector<tinygltf::Accessor> accessors;

    // Skeleton/skin: built once, shared by every mesh node below. Joint
    // nodes are appended after every mesh node (see the node-index layout
    // comment near the bottom of this function), so their indices are
    // offset by meshes.size() -- known up front, unlike the single-mesh
    // writeGlb's hardcoded "1 +".
    size_t meshCount = meshes.size();
    int skinIdx = -1;
    std::vector<tinygltf::Node> jointNodes;
    std::vector<int> rootJointNodeIndices;
    if (hasSkeleton) {
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

        jointNodes.resize(skeleton->joints.size());
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            const auto& src = skeleton->joints[i];
            jointNodes[i].translation = {src.localTranslation.x, src.localTranslation.y,
                                          src.localTranslation.z};
        }
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            int parent = skeleton->joints[i].parent;
            int nodeIdx = static_cast<int>(meshCount + i);
            if (parent == -1) {
                rootJointNodeIndices.push_back(nodeIdx);
            } else {
                jointNodes[static_cast<size_t>(parent)].children.push_back(nodeIdx);
            }
        }

        tinygltf::Skin skin;
        skin.inverseBindMatrices = ibmAccIdx;
        for (size_t i = 0; i < skeleton->joints.size(); ++i) {
            skin.joints.push_back(static_cast<int>(meshCount + i));
        }
        model.skins = {skin};
        skinIdx = 0;
    }

    std::vector<tinygltf::Image> images;
    std::vector<tinygltf::Texture> textures;
    std::vector<tinygltf::Material> tinyMaterials;
    std::vector<tinygltf::Mesh> tinyMeshes;
    std::vector<tinygltf::Node> meshNodes;

    for (const auto& nm : meshes) {
        const Mesh& mesh = nm.mesh;
        size_t n = mesh.positions.size();
        bool hasTexCoords2 = !mesh.texCoords2.empty();

        int posView = appendBufferView(buffer, views, mesh.positions, TINYGLTF_TARGET_ARRAY_BUFFER);
        int normView = appendBufferView(buffer, views, mesh.normals, TINYGLTF_TARGET_ARRAY_BUFFER);
        int uvView = appendBufferView(buffer, views, mesh.texCoords, TINYGLTF_TARGET_ARRAY_BUFFER);

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

        int uv2AccIdx = -1;
        if (hasTexCoords2) {
            int uv2View =
                appendBufferView(buffer, views, mesh.texCoords2, TINYGLTF_TARGET_ARRAY_BUFFER);
            tinygltf::Accessor uv2Acc;
            uv2Acc.bufferView = uv2View;
            uv2Acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            uv2Acc.count = n;
            uv2Acc.type = TINYGLTF_TYPE_VEC2;
            uv2AccIdx = static_cast<int>(accessors.size());
            accessors.push_back(uv2Acc);
        }

        int jAccIdx = -1, wAccIdx = -1;
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
            int jointsView =
                appendBufferView(buffer, views, jointsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);
            int weightsView =
                appendBufferView(buffer, views, weightsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);

            tinygltf::Accessor jAcc;
            jAcc.bufferView = jointsView;
            jAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
            jAcc.count = n;
            jAcc.type = TINYGLTF_TYPE_VEC4;
            jAccIdx = static_cast<int>(accessors.size());
            accessors.push_back(jAcc);

            tinygltf::Accessor wAcc;
            wAcc.bufferView = weightsView;
            wAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            wAcc.count = n;
            wAcc.type = TINYGLTF_TYPE_VEC4;
            wAccIdx = static_cast<int>(accessors.size());
            accessors.push_back(wAcc);
        }

        // Materials/images/textures: appended into the shared, model-wide
        // lists, so a primitive's tinygltf material index has to be offset
        // by however many other meshes' materials came before this one's --
        // `nm.materials`/`mesh.primitives[*].materialIndex` are numbered
        // locally to this one NamedMesh (see gltf.hpp's doc comment).
        int materialBase = static_cast<int>(tinyMaterials.size());
        for (const auto& mat : nm.materials) {
            tinygltf::Material tm;
            tm.name = mat.name;
            tm.alphaMode = alphaModeString(mat.alphaMode);
            tm.doubleSided = mat.doubleSided;
            tm.pbrMetallicRoughness.baseColorFactor = {mat.baseColorFactor[0], mat.baseColorFactor[1],
                                                         mat.baseColorFactor[2], mat.baseColorFactor[3]};
            if (!mat.baseColorImagePng.empty()) {
                int imgView = appendBufferView(buffer, views, mat.baseColorImagePng, /*target=*/0);
                tinygltf::Image img;
                img.mimeType = "image/png";
                img.bufferView = imgView;
                int imgIdx = static_cast<int>(images.size());
                images.push_back(img);

                tinygltf::Texture tex;
                tex.source = imgIdx;
                int texIdx = static_cast<int>(textures.size());
                textures.push_back(tex);

                tm.pbrMetallicRoughness.baseColorTexture.index = texIdx;
                if (mat.baseColorTexCoord == 1 && uv2AccIdx >= 0) {
                    tm.pbrMetallicRoughness.baseColorTexture.texCoord = 1;
                }
            }
            tinyMaterials.push_back(tm);
        }

        std::vector<tinygltf::Primitive> tinyPrims;
        for (const auto& p : mesh.primitives) {
            int idxView =
                appendBufferView(buffer, views, p.indices, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);
            tinygltf::Accessor idxAcc;
            idxAcc.bufferView = idxView;
            idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
            idxAcc.count = p.indices.size();
            idxAcc.type = TINYGLTF_TYPE_SCALAR;
            int idxAccIdx = static_cast<int>(accessors.size());
            accessors.push_back(idxAcc);

            tinygltf::Primitive tp;
            tp.attributes["POSITION"] = posAccIdx;
            tp.attributes["NORMAL"] = normAccIdx;
            tp.attributes["TEXCOORD_0"] = uvAccIdx;
            if (uv2AccIdx >= 0) {
                tp.attributes["TEXCOORD_1"] = uv2AccIdx;
            }
            if (jAccIdx >= 0) {
                tp.attributes["JOINTS_0"] = jAccIdx;
                tp.attributes["WEIGHTS_0"] = wAccIdx;
            }
            tp.indices = idxAccIdx;
            tp.mode = TINYGLTF_MODE_TRIANGLES;
            if (p.materialIndex >= 0) {
                tp.material = materialBase + p.materialIndex;
            }
            tinyPrims.push_back(tp);
        }

        tinygltf::Mesh gltfMesh;
        gltfMesh.primitives = tinyPrims;
        tinyMeshes.push_back(gltfMesh);

        tinygltf::Node node;
        node.name = nm.name;
        node.mesh = static_cast<int>(tinyMeshes.size()) - 1;
        if (skinIdx >= 0) {
            node.skin = skinIdx;
        }
        meshNodes.push_back(node);
    }

    model.images = images;
    model.textures = textures;
    model.materials = tinyMaterials;
    model.meshes = tinyMeshes;

    // Node layout: mesh nodes first (indices 0..meshCount-1, in `meshes`
    // order), then the shared skeleton's joint nodes (meshCount..meshCount+
    // jointCount-1) -- see the skeleton/skin block above, which already
    // computed every joint/skin reference against this same offset.
    model.nodes = meshNodes;
    for (auto& jointNode : jointNodes) {
        model.nodes.push_back(jointNode);
    }

    tinygltf::Scene scene;
    for (size_t mi = 0; mi < meshCount; ++mi) {
        scene.nodes.push_back(static_cast<int>(mi));
    }
    for (int rootIdx : rootJointNodeIndices) {
        scene.nodes.push_back(rootIdx);
    }
    model.scenes = {scene};
    model.defaultScene = 0;

    // Animations: one glTF animation per husk::gltf::Animation, one
    // sampler+channel pair per non-empty TRS property per joint entry.
    // Joint i's data targets node (meshCount + i) -- the same joint-node
    // offset the skeleton/skin block above used. Input (time) accessors get
    // min/max per glTF's own requirement for animation sampler inputs;
    // rotation output values are laid out as (x, y, z, w) float arrays,
    // matching Quat's own field order.
    for (const auto& anim : animations) {
        tinygltf::Animation ga;
        ga.name = anim.name;

        auto addChannel = [&](int nodeIdx, const char* path, const std::vector<float>& times,
                               const void* valuesData, size_t valueCount, size_t valueStride,
                               int type) {
            int inView = appendBufferView(buffer, views, times, /*target=*/0);
            tinygltf::Accessor inAcc;
            inAcc.bufferView = inView;
            inAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            inAcc.count = times.size();
            inAcc.type = TINYGLTF_TYPE_SCALAR;
            inAcc.minValues = {static_cast<double>(times.front())};
            inAcc.maxValues = {static_cast<double>(times.back())};
            int inIdx = static_cast<int>(accessors.size());
            accessors.push_back(inAcc);

            tinygltf::BufferView outView;
            outView.buffer = 0;
            outView.byteOffset = buffer.data.size();
            outView.byteLength = valueCount * valueStride;
            const auto* bytes = reinterpret_cast<const unsigned char*>(valuesData);
            buffer.data.insert(buffer.data.end(), bytes, bytes + outView.byteLength);
            int outViewIdx = static_cast<int>(views.size());
            views.push_back(outView);

            tinygltf::Accessor outAcc;
            outAcc.bufferView = outViewIdx;
            outAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
            outAcc.count = valueCount;
            outAcc.type = type;
            int outIdx = static_cast<int>(accessors.size());
            accessors.push_back(outAcc);

            tinygltf::AnimationSampler samp;
            samp.input = inIdx;
            samp.output = outIdx;
            samp.interpolation = "LINEAR";
            int sampIdx = static_cast<int>(ga.samplers.size());
            ga.samplers.push_back(samp);

            tinygltf::AnimationChannel ch;
            ch.sampler = sampIdx;
            ch.target_node = nodeIdx;
            ch.target_path = path;
            ga.channels.push_back(ch);
        };

        for (const auto& ja : anim.joints) {
            int nodeIdx = static_cast<int>(meshCount) + ja.joint;
            if (!ja.translationTimes.empty()) {
                addChannel(nodeIdx, "translation", ja.translationTimes, ja.translationValues.data(),
                           ja.translationValues.size(), sizeof(Vec3), TINYGLTF_TYPE_VEC3);
            }
            if (!ja.rotationTimes.empty()) {
                std::vector<std::array<float, 4>> rot;
                rot.reserve(ja.rotationValues.size());
                for (const auto& q : ja.rotationValues) {
                    rot.push_back({q.x, q.y, q.z, q.w});
                }
                addChannel(nodeIdx, "rotation", ja.rotationTimes, rot.data(), rot.size(),
                           sizeof(std::array<float, 4>), TINYGLTF_TYPE_VEC4);
            }
            if (!ja.scaleTimes.empty()) {
                addChannel(nodeIdx, "scale", ja.scaleTimes, ja.scaleValues.data(),
                           ja.scaleValues.size(), sizeof(Vec3), TINYGLTF_TYPE_VEC3);
            }
        }

        model.animations.push_back(ga);
    }

    // Deferred until every appendBufferView/accessors.push_back above (mesh
    // data, skinning, materials' embedded images, and now animations) has
    // run -- `views`/`accessors` are plain local vectors, not references
    // into the model, so copying them into `model` any earlier would miss
    // whatever got appended after that point.
    model.bufferViews = views;
    model.accessors = accessors;

    model.buffers = {buffer};

    tinygltf::TinyGLTF writer;
    std::ostringstream out(std::ios::binary);
    if (!writer.WriteGltfSceneToStream(&model, out, /*prettyPrint=*/false, /*writeBinary=*/true)) {
        throw Error("writeGlbMulti: tinygltf failed to serialize the model");
    }

    std::string bytes = out.str();
    return std::vector<uint8_t>(bytes.begin(), bytes.end());
}

std::vector<uint8_t> writeGlb(const Mesh& mesh, const std::vector<Material>& materials,
                               const Skeleton* skeleton, const std::vector<Animation>& animations) {
    return writeGlbMulti({NamedMesh{"", mesh, materials}}, skeleton, animations);
}

}  // namespace husk::gltf
