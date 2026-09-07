#include "writers/gltf_lean.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "gltf_buffer_utils.hpp"
#include "gltf_math.hpp"
#include "writers/writer_common.hpp"

// See gltf_lean.hpp's own doc comment for this file's scope (no extras,
// ever) and REFACTOR/README.md stage 4 for why this exists alongside the
// production writer rather than replacing it.
namespace husk::writers {

namespace {

gltf::Vec3 toGltfPoint(const m2::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }
gltf::Vec3 toGltfScale(const m2::Vec3& v) { return gltf::scaleZUpToYUp({v.x, v.y, v.z}); }
gltf::Quat toGltfQuat(const m2::Quat& q) { return gltf::rotationZUpToYUp({q.x, q.y, q.z, q.w}); }

// One joint's world-space bind position, in raw M2 space -- canon::Joint
// already gives this for free (Joint::globalPosition is absolute, not
// parent-relative -- see canon_skeleton.hpp's own doc comment), so unlike
// localBindTranslation there's no hierarchy walk needed here.
gltf::Vec3 worldBindPositionGltf(const canon::Joint& joint) { return toGltfPoint(joint.globalPosition); }

// Pure-translation inverse-bind matrix: identity rotation/scale, -p
// translation (the exact inverse of a pure-translation bind matrix).
// Mirrors gltf_skeleton.cpp's own real inverse-bind-matrix construction
// (same formula, re-derived here rather than called, since that file is
// off-limits per this writer's own scope).
std::array<float, 16> inverseBindMatrix(const gltf::Vec3& worldBindPos) {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -worldBindPos.x, -worldBindPos.y, -worldBindPos.z, 1};
}

// A real, un-collapsed M2 texture-combine op (canon::BlendOp) is a
// different axis from glTF's own alphaMode (a framebuffer-blend state,
// M2's separate BLEND_MODE field -- not modeled in canon::Material at all
// yet, see canon_material.hpp's own doc comment on BlendOp). Since there's
// no source M2 alpha-blend-mode field to reuse export_texture_resolution.cpp's
// alphaModeForBlend against, this is a deliberately reasoned mapping of the
// material's own *last* layer's combine op (the one that determines what
// finally reaches the framebuffer): Replace/Modulate/Modulate2x describe a
// fully-covering combine (Opaque); Decal's whole point is a hard cutout
// (Mask); Add/Fade both describe a translucent contribution (Blend).
std::string alphaModeFor(const canon::Material& mat) {
    if (mat.layers.empty()) return "OPAQUE";
    switch (mat.layers.back().blendIntoPrevious) {
        case canon::BlendOp::Replace:
        case canon::BlendOp::Modulate:
        case canon::BlendOp::Modulate2x:
            return "OPAQUE";
        case canon::BlendOp::Decal:
            return "MASK";
        case canon::BlendOp::Add:
        case canon::BlendOp::Fade:
            return "BLEND";
    }
    return "OPAQUE";
}

// canon::SequenceRef carries no display name of its own (canon_curve.hpp's
// own doc comment: neither array entry names itself anywhere in this
// codebase) -- this writer needs *some* real Animation.name for glTF/
// Blender's Action-picker UX, so it synthesizes one from the same
// (kind, index) pair production's own "global_seq_<n>" naming already
// establishes for the GlobalSequence half; "sequence_<n>" is this writer's
// own, equally mechanical, choice for the ordinary case.
std::string animationName(const canon::SequenceRef& ref) {
    const char* prefix = ref.kind == canon::SequenceRef::Kind::GlobalSequence ? "global_sequence_" : "sequence_";
    return prefix + std::to_string(ref.index);
}

}  // namespace

void writeLeanGlb(const canon::Model& model, const std::filesystem::path& outputPath) {
    const canon::Skeleton& skeleton = model.skeleton;
    const canon::Mesh& mesh = model.mesh;
    bool hasSkeleton = !skeleton.joints.empty();
    bool meshIsSkinned = hasSkeleton && !mesh.skinning.empty();
    size_t n = mesh.positions.size();

    if (mesh.normals.size() != n || mesh.uv0.size() != n) {
        throw std::runtime_error("writeLeanGlb: mesh positions (" + std::to_string(n) +
                                  "), normals (" + std::to_string(mesh.normals.size()) + "), and uv0 (" +
                                  std::to_string(mesh.uv0.size()) + ") sizes don't match");
    }
    if (meshIsSkinned && mesh.skinning.size() != n) {
        throw std::runtime_error("writeLeanGlb: mesh skinning (" + std::to_string(mesh.skinning.size()) +
                                  ") doesn't match position count (" + std::to_string(n) + ")");
    }
    if (model.materials.size() != mesh.primitives.size()) {
        throw std::runtime_error("writeLeanGlb: materials (" + std::to_string(model.materials.size()) +
                                  ") doesn't match primitives (" + std::to_string(mesh.primitives.size()) +
                                  ") -- canon::Model's own stated 1:1 correspondence is violated");
    }

    tinygltf::Model out;
    out.asset.version = "2.0";
    out.asset.generator = "husk (lean writer)";

    tinygltf::Buffer buffer;
    std::vector<tinygltf::BufferView> views;
    std::vector<tinygltf::Accessor> accessors;

    // --- Mesh geometry --------------------------------------------------
    std::vector<gltf::Vec3> positionsGltf, normalsGltf;
    positionsGltf.reserve(n);
    normalsGltf.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        positionsGltf.push_back(toGltfPoint(mesh.positions[i]));
        normalsGltf.push_back(toGltfPoint(mesh.normals[i]));
    }

    int posView = gltf::appendBufferView(buffer, views, positionsGltf, TINYGLTF_TARGET_ARRAY_BUFFER);
    int normView = gltf::appendBufferView(buffer, views, normalsGltf, TINYGLTF_TARGET_ARRAY_BUFFER);
    int uvView = gltf::appendBufferView(buffer, views, mesh.uv0, TINYGLTF_TARGET_ARRAY_BUFFER);

    gltf::Vec3 posMin = positionsGltf.empty() ? gltf::Vec3{} : positionsGltf[0];
    gltf::Vec3 posMax = posMin;
    for (const auto& p : positionsGltf) {
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

    int uv1AccIdx = -1;
    if (mesh.uv1) {
        if (mesh.uv1->size() != n) {
            throw std::runtime_error("writeLeanGlb: mesh uv1 (" + std::to_string(mesh.uv1->size()) +
                                      ") doesn't match position count (" + std::to_string(n) + ")");
        }
        int uv1View = gltf::appendBufferView(buffer, views, *mesh.uv1, TINYGLTF_TARGET_ARRAY_BUFFER);
        tinygltf::Accessor uv1Acc;
        uv1Acc.bufferView = uv1View;
        uv1Acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        uv1Acc.count = n;
        uv1Acc.type = TINYGLTF_TYPE_VEC2;
        uv1AccIdx = static_cast<int>(accessors.size());
        accessors.push_back(uv1Acc);
    }

    int jointsAccIdx = -1, weightsAccIdx = -1;
    if (meshIsSkinned) {
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

        int jointsView = gltf::appendBufferView(buffer, views, jointsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);
        int weightsView = gltf::appendBufferView(buffer, views, weightsFlat, TINYGLTF_TARGET_ARRAY_BUFFER);

        tinygltf::Accessor jAcc;
        jAcc.bufferView = jointsView;
        jAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;  // matches Skinning::joints' own uint8_t width
        jAcc.count = n;
        jAcc.type = TINYGLTF_TYPE_VEC4;
        jointsAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(jAcc);

        tinygltf::Accessor wAcc;
        wAcc.bufferView = weightsView;
        wAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        wAcc.count = n;
        wAcc.type = TINYGLTF_TYPE_VEC4;
        weightsAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(wAcc);
    }

    // Shared index buffer -- one bufferView, every primitive's accessor
    // slices a byteOffset/count range out of it (mirrors canon::Mesh's own
    // "one shared buffer, PrimitiveGeoset slices a range" shape exactly,
    // rather than re-copying each primitive's own index subrange into a
    // separate bufferView the way the production writer does).
    int idxView = gltf::appendBufferView(buffer, views, mesh.indices, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);

    // --- Materials --------------------------------------------------------
    for (const auto& mat : model.materials) {
        tinygltf::Material tm;
        tm.pbrMetallicRoughness.baseColorFactor = {1.0, 1.0, 1.0, 1.0};
        tm.alphaMode = alphaModeFor(mat);
        out.materials.push_back(tm);
    }

    // --- Mesh primitives ----------------------------------------------
    tinygltf::Mesh tinyMesh;
    for (const auto& prim : mesh.primitives) {
        if (prim.indexStart + prim.indexCount > mesh.indices.size()) {
            throw std::runtime_error("writeLeanGlb: primitive index range [" +
                                      std::to_string(prim.indexStart) + ", " +
                                      std::to_string(prim.indexStart + prim.indexCount) +
                                      ") runs past the shared index buffer (" +
                                      std::to_string(mesh.indices.size()) + " entries)");
        }
        tinygltf::Accessor idxAcc;
        idxAcc.bufferView = idxView;
        idxAcc.byteOffset = static_cast<size_t>(prim.indexStart) * sizeof(uint32_t);
        idxAcc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        idxAcc.count = prim.indexCount;
        idxAcc.type = TINYGLTF_TYPE_SCALAR;
        int idxAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(idxAcc);

        tinygltf::Primitive tp;
        tp.attributes["POSITION"] = posAccIdx;
        tp.attributes["NORMAL"] = normAccIdx;
        tp.attributes["TEXCOORD_0"] = uvAccIdx;
        if (uv1AccIdx >= 0) tp.attributes["TEXCOORD_1"] = uv1AccIdx;
        if (jointsAccIdx >= 0) {
            tp.attributes["JOINTS_0"] = jointsAccIdx;
            tp.attributes["WEIGHTS_0"] = weightsAccIdx;
        }
        tp.indices = idxAccIdx;
        tp.mode = TINYGLTF_MODE_TRIANGLES;
        size_t primIndex = tinyMesh.primitives.size();
        if (primIndex < model.materials.size()) {
            tp.material = static_cast<int>(primIndex);
        }
        tinyMesh.primitives.push_back(tp);
    }
    out.meshes.push_back(tinyMesh);

    // --- Nodes: mesh node (0) + one per joint ---------------------------
    tinygltf::Node meshNode;
    meshNode.mesh = 0;
    int meshNodeIdx = 0;
    out.nodes.push_back(meshNode);

    int jointNodeBase = static_cast<int>(out.nodes.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const canon::Joint& joint = skeleton.joints[i];
        gltf::Vec3 t = toGltfPoint(localBindTranslation(skeleton, i));
        tinygltf::Node node;
        node.translation = {t.x, t.y, t.z};
        node.name = !joint.structuralLabel.empty() ? joint.structuralLabel : "joint_" + std::to_string(i);
        out.nodes.push_back(node);
    }
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        int parent = skeleton.joints[i].parent;
        int nodeIdx = jointNodeBase + static_cast<int>(i);
        if (parent >= 0) {
            out.nodes[static_cast<size_t>(jointNodeBase + parent)].children.push_back(nodeIdx);
        }
    }

    out.scenes.emplace_back();
    out.scenes[0].nodes.push_back(meshNodeIdx);
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        if (skeleton.joints[i].parent == -1) {
            out.scenes[0].nodes.push_back(jointNodeBase + static_cast<int>(i));
        }
    }
    out.defaultScene = 0;

    // --- Skin + inverse bind matrices -----------------------------------
    if (hasSkeleton) {
        std::vector<float> ibmFlat;
        ibmFlat.reserve(skeleton.joints.size() * 16);
        for (const auto& joint : skeleton.joints) {
            auto m = inverseBindMatrix(worldBindPositionGltf(joint));
            ibmFlat.insert(ibmFlat.end(), m.begin(), m.end());
        }
        int ibmView = gltf::appendBufferView(buffer, views, ibmFlat, /*target=*/0);
        tinygltf::Accessor ibmAcc;
        ibmAcc.bufferView = ibmView;
        ibmAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
        ibmAcc.count = skeleton.joints.size();
        ibmAcc.type = TINYGLTF_TYPE_MAT4;
        int ibmAccIdx = static_cast<int>(accessors.size());
        accessors.push_back(ibmAcc);

        tinygltf::Skin skin;
        skin.inverseBindMatrices = ibmAccIdx;
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            skin.joints.push_back(jointNodeBase + static_cast<int>(i));
        }
        out.skins.push_back(skin);
        out.nodes[static_cast<size_t>(meshNodeIdx)].skin = 0;
    }

    // --- Animations -------------------------------------------------------
    for (const auto& clip : model.animations) {
        tinygltf::Animation anim;
        anim.name = animationName(clip.sequence);

        auto addChannel = [&](int nodeIdx, const char* path, const std::vector<float>& times,
                               const void* valuesData, size_t valueCount, size_t valueStride, int type,
                               bool step) {
            int inView = gltf::appendBufferView(buffer, views, times, /*target=*/0);
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
            gltf::padTo4(buffer);
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
            samp.interpolation = step ? "STEP" : "LINEAR";
            int sampIdx = static_cast<int>(anim.samplers.size());
            anim.samplers.push_back(samp);

            tinygltf::AnimationChannel ch;
            ch.sampler = sampIdx;
            ch.target_node = nodeIdx;
            ch.target_path = path;
            anim.channels.push_back(ch);
        };

        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            auto composed = composeJointCurves(skeleton, clip, i);
            if (!composed) continue;
            int nodeIdx = jointNodeBase + static_cast<int>(i);

            if (!composed->translation.keyframes.empty()) {
                std::vector<float> times;
                std::vector<gltf::Vec3> values;
                times.reserve(composed->translation.keyframes.size());
                values.reserve(composed->translation.keyframes.size());
                for (const auto& [t, v] : composed->translation.keyframes) {
                    times.push_back(t);
                    values.push_back(toGltfPoint(v));
                }
                addChannel(nodeIdx, "translation", times, values.data(), values.size(), sizeof(gltf::Vec3),
                           TINYGLTF_TYPE_VEC3, composed->translation.interpolation == canon::Interpolation::Step);
            }
            if (!composed->rotation.keyframes.empty()) {
                std::vector<float> times;
                std::vector<std::array<float, 4>> values;
                times.reserve(composed->rotation.keyframes.size());
                values.reserve(composed->rotation.keyframes.size());
                std::optional<gltf::Quat> prev;
                for (const auto& [t, q] : composed->rotation.keyframes) {
                    times.push_back(t);
                    gltf::Quat gq = toGltfQuat(q);
                    if (prev) gq = gltf::enforceHemisphereContinuity(*prev, gq);
                    prev = gq;
                    values.push_back({gq.x, gq.y, gq.z, gq.w});
                }
                addChannel(nodeIdx, "rotation", times, values.data(), values.size(),
                           sizeof(std::array<float, 4>), TINYGLTF_TYPE_VEC4,
                           composed->rotation.interpolation == canon::Interpolation::Step);
            }
            if (!composed->scale.keyframes.empty()) {
                std::vector<float> times;
                std::vector<gltf::Vec3> values;
                times.reserve(composed->scale.keyframes.size());
                values.reserve(composed->scale.keyframes.size());
                for (const auto& [t, v] : composed->scale.keyframes) {
                    times.push_back(t);
                    values.push_back(toGltfScale(v));
                }
                addChannel(nodeIdx, "scale", times, values.data(), values.size(), sizeof(gltf::Vec3),
                           TINYGLTF_TYPE_VEC3, composed->scale.interpolation == canon::Interpolation::Step);
            }
        }

        if (!anim.channels.empty()) {
            out.animations.push_back(anim);
        }
    }

    out.buffers.push_back(buffer);
    out.bufferViews = views;
    out.accessors = accessors;

    tinygltf::TinyGLTF writer;
    if (!writer.WriteGltfSceneToFile(&out, outputPath.string(), /*embedImages=*/true, /*embedBuffers=*/true,
                                      /*prettyPrint=*/false, /*writeBinary=*/true)) {
        throw std::runtime_error("writeLeanGlb: tinygltf failed to write '" + outputPath.string() + "'");
    }
}

}  // namespace husk::writers
