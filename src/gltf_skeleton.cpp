#include "gltf_skeleton.hpp"

#include <array>
#include <string>

#include "gltf_buffer_utils.hpp"
#include "gltf_skeleton_internal.hpp"
#include "gltf.hpp"

namespace husk::gltf {

void validateSkeletonTopology(const Skeleton* skeleton, bool hasSkeleton) {
    if (!hasSkeleton) return;
    for (size_t i = 0; i < skeleton->joints.size(); ++i) {
        int parent = skeleton->joints[i].parent;
        if (parent == static_cast<int>(i)) {
            throw Error("writeGlbMulti: joint " + std::to_string(i) + " is its own parent");
        }
        if (parent != -1 && (parent < 0 || static_cast<size_t>(parent) >= skeleton->joints.size())) {
            throw Error("writeGlbMulti: joint " + std::to_string(i) + "'s parent (" +
                        std::to_string(parent) + ") is out of range for " +
                        std::to_string(skeleton->joints.size()) + " joints");
        }
    }
}

void validateSkeletonAnchors(const Skeleton* skeleton) {
    if (!skeleton) return;

    for (size_t si = 0; si < skeleton->correctionSets.size(); ++si) {
        const auto& cs = skeleton->correctionSets[si];
        for (size_t ci = 0; ci < cs.corrections.size(); ++ci) {
            int joint = cs.corrections[ci].joint;
            if (joint < 0 || static_cast<size_t>(joint) >= skeleton->joints.size()) {
                throw Error("writeGlbMulti: correction set " + std::to_string(si) + "'s entry " +
                            std::to_string(ci) + " (joint " + std::to_string(joint) +
                            ") is out of range for " + std::to_string(skeleton->joints.size()) +
                            " joints");
            }
        }
    }

    // Generic over any "has a .joint field" anchor shape (EmitterAnchor,
    // Attachment, Event, Light -- PhysicsBody too, though its own check
    // below predates this and is left as is) so ribbon/particle/
    // attachment/event/light entries all share one bounds-check.
    auto checkAnchors = [&](const auto& anchors, const char* what) {
        for (size_t i = 0; i < anchors.size(); ++i) {
            int joint = anchors[i].joint;
            if (joint < 0 || static_cast<size_t>(joint) >= skeleton->joints.size()) {
                throw Error(std::string("writeGlbMulti: ") + what + " " + std::to_string(i) +
                            " (joint " + std::to_string(joint) + ") is out of range for " +
                            std::to_string(skeleton->joints.size()) + " joints");
            }
        }
    };
    checkAnchors(skeleton->ribbonAnchors, "ribbon anchor");
    checkAnchors(skeleton->particleAnchors, "particle anchor");
    checkAnchors(skeleton->attachments, "attachment");
    checkAnchors(skeleton->events, "event");
    checkAnchors(skeleton->lights, "light");
    for (size_t i = 0; i < skeleton->physicsBodies.size(); ++i) {
        int joint = skeleton->physicsBodies[i].joint;
        if (joint < 0 || static_cast<size_t>(joint) >= skeleton->joints.size()) {
            throw Error("writeGlbMulti: physics body " + std::to_string(i) + " (joint " +
                        std::to_string(joint) + ") is out of range for " +
                        std::to_string(skeleton->joints.size()) + " joints");
        }
    }
}

void validateAnimations(const std::vector<Animation>& animations, const Skeleton* skeleton,
                         bool hasSkeleton) {
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
                            std::to_string(ja.joint) + " has mismatched keyframe time/value counts");
            }
        }
    }
}

SkeletonEmission emitSkeletonAndSkin(const Skeleton* skeleton, bool hasSkeleton, size_t meshCount,
                                      tinygltf::Buffer& buffer, std::vector<tinygltf::BufferView>& views,
                                      std::vector<tinygltf::Accessor>& accessors) {
    SkeletonEmission out;
    if (!hasSkeleton) return out;

    std::vector<float> ibmFlat;
    ibmFlat.reserve((skeleton->joints.size() + skeleton->geosetTags.size()) * 16);
    for (const auto& j : skeleton->joints) {
        const Vec3& p = j.globalPosition;
        float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -p.x, -p.y, -p.z, 1};
        ibmFlat.insert(ibmFlat.end(), std::begin(m), std::end(m));
    }
    // Geoset tag joints (Skeleton::GeosetTag) sit at the identity bind pose
    // -- they're never posed/animated, so a plain identity inverse bind
    // matrix is correct (not an approximation): a joint that never moves
    // from its bind pose contributes weight * identity * vertex, a verified
    // no-op on real skin deformation (GEOSET_MASK_TODO.md).
    static constexpr float kIdentityIbm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    for (size_t i = 0; i < skeleton->geosetTags.size(); ++i) {
        ibmFlat.insert(ibmFlat.end(), std::begin(kIdentityIbm), std::end(kIdentityIbm));
    }
    int ibmView = appendBufferView(buffer, views, ibmFlat, /*target=*/0);
    tinygltf::Accessor ibmAcc;
    ibmAcc.bufferView = ibmView;
    ibmAcc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    ibmAcc.count = skeleton->joints.size() + skeleton->geosetTags.size();
    ibmAcc.type = TINYGLTF_TYPE_MAT4;
    int ibmAccIdx = static_cast<int>(accessors.size());
    accessors.push_back(ibmAcc);

    out.jointNodes.resize(skeleton->joints.size());
    for (size_t i = 0; i < skeleton->joints.size(); ++i) {
        const auto& src = skeleton->joints[i];
        out.jointNodes[i].translation = {src.localTranslation.x, src.localTranslation.y,
                                          src.localTranslation.z};
        // A real semantic name when known (see Skeleton::Joint::name's doc
        // comment), else an index-correlatable fallback -- either beats
        // Blender's own generic "Bone"/"Node" numbering, which isn't
        // guaranteed to match husk's own bone-index order.
        out.jointNodes[i].name = !src.name.empty() ? src.name : "bone_" + std::to_string(i);
        if (!src.billboardMode.empty()) {
            tinygltf::Value::Object extras;
            extras["billboard"] = tinygltf::Value(src.billboardMode);
            out.jointNodes[i].extras = tinygltf::Value(extras);
        }
    }
    for (size_t i = 0; i < skeleton->joints.size(); ++i) {
        int parent = skeleton->joints[i].parent;
        int nodeIdx = static_cast<int>(meshCount + i);
        if (parent == -1) {
            out.rootJointNodeIndices.push_back(nodeIdx);
        } else {
            out.jointNodes[static_cast<size_t>(parent)].children.push_back(nodeIdx);
        }
    }
    if (out.rootJointNodeIndices.size() > 1) {
        out.hasSyntheticRoot = true;
        // Past the joint-node range *and* past the geoset-tag-node range
        // (gltf.cpp pushes mesh, then joint, then tag, then this node, in
        // that order) -- geosetTags.size() is already known here even
        // though the tag nodes themselves aren't built until just below.
        out.syntheticRootNodeIndex =
            static_cast<int>(meshCount + skeleton->joints.size() + skeleton->geosetTags.size());
        out.syntheticRootNode.children = out.rootJointNodeIndices;
    }

    // Geoset tag joints (Skeleton::GeosetTag): one identity-pose node each,
    // appended right after the real joint-node range. Parented under
    // whatever node is (or would be) the skin's own "closest common root"
    // -- the synthesized multi-root parent when one exists, else the
    // single real root joint directly -- so adding these never breaks that
    // glTF-required property of a skin's joint hierarchy. Also pushed onto
    // `skin.joints` itself below, unlike Attachment/Event/Light nodes.
    int tagParentLocalIdx = -1;  // index into skeleton->joints, single-root case only
    if (!out.hasSyntheticRoot && !out.rootJointNodeIndices.empty()) {
        tagParentLocalIdx = out.rootJointNodeIndices.front() - static_cast<int>(meshCount);
    }
    out.geosetTagNodes.resize(skeleton->geosetTags.size());
    for (size_t i = 0; i < skeleton->geosetTags.size(); ++i) {
        int nodeIdx = static_cast<int>(meshCount + skeleton->joints.size() + i);
        int geosetId = skeleton->geosetTags[i].geosetId;
        // "group_<n>,variant_<n>" rather than a single "geoset_<id>" token
        // -- a future Blender-side script (GEOSET_MASK_TODO.md's geometry-
        // nodes follow-up) can recover the raw group/variant integers with
        // a plain comma-split + prefix-strip, no `id / 100` / `id % 100`
        // math needed at the consuming end.
        out.geosetTagNodes[i].name =
            "group_" + std::to_string(geosetId / 100) + ",variant_" + std::to_string(geosetId % 100);
        out.geosetTagNodes[i].translation = {0, 0, 0};
        out.geosetTagJointIndex[geosetId] = static_cast<uint32_t>(skeleton->joints.size() + i);
        if (out.hasSyntheticRoot) {
            out.syntheticRootNode.children.push_back(nodeIdx);
        } else if (tagParentLocalIdx >= 0) {
            out.jointNodes[static_cast<size_t>(tagParentLocalIdx)].children.push_back(nodeIdx);
        }
    }

    tinygltf::Skin skin;
    skin.inverseBindMatrices = ibmAccIdx;
    for (size_t i = 0; i < skeleton->joints.size(); ++i) {
        skin.joints.push_back(static_cast<int>(meshCount + i));
    }
    for (size_t i = 0; i < skeleton->geosetTags.size(); ++i) {
        skin.joints.push_back(static_cast<int>(meshCount + skeleton->joints.size() + i));
    }
    // A skin's implicit skeleton root (when `skeleton` is unset) is the
    // common parent of every joint, which doesn't exist for a real
    // multi-root M2 -- set it explicitly to the synthesized node so
    // consumers don't have to guess. Single-root models: left unset,
    // unchanged behavior.
    // TODO: Remove: github.com/KhronosGroup/glTF/issues/1270 citation
    // (external tracker link for this discussion).
    if (out.hasSyntheticRoot) {
        skin.skeleton = out.syntheticRootNodeIndex;
    }

    // .bone correction data (Skeleton::CorrectionSet's doc comment), plus
    // ribbon/particle emitter placement anchors (Skeleton::EmitterAnchor's
    // doc comment) -- inert extras only, same nested Value construction as
    // a material's additional_textures/texture_transform extras
    // (gltf_mesh.cpp's emitMaterial). All three are independent (any subset
    // may be present) and share one skinExtras object the same way
    // materialExtras does there.
    tinygltf::Value::Object skinExtras;
    if (!skeleton->correctionSets.empty()) {
        tinygltf::Value::Array sets;
        for (const auto& cs : skeleton->correctionSets) {
            tinygltf::Value::Object setObj;
            setObj["file_data_id"] = tinygltf::Value(static_cast<int>(cs.fileDataId));
            tinygltf::Value::Array corrections;
            for (const auto& c : cs.corrections) {
                tinygltf::Value::Object co;
                co["joint"] = tinygltf::Value(c.joint);
                tinygltf::Value::Array mat;
                for (float f : c.matrix) mat.push_back(tinygltf::Value(static_cast<double>(f)));
                co["matrix"] = tinygltf::Value(mat);
                corrections.push_back(tinygltf::Value(co));
            }
            setObj["corrections"] = tinygltf::Value(corrections);
            sets.push_back(tinygltf::Value(setObj));
        }
        skinExtras["bone_correction_sets"] = tinygltf::Value(sets);
    }
    auto writeAnchors = [](const std::vector<Skeleton::EmitterAnchor>& anchors) {
        tinygltf::Value::Array arr;
        for (const auto& a : anchors) {
            tinygltf::Value::Object obj;
            obj["id"] = tinygltf::Value(static_cast<int>(a.id));
            obj["joint"] = tinygltf::Value(a.joint);
            tinygltf::Value::Object pos;
            pos["x"] = tinygltf::Value(static_cast<double>(a.position.x));
            pos["y"] = tinygltf::Value(static_cast<double>(a.position.y));
            pos["z"] = tinygltf::Value(static_cast<double>(a.position.z));
            obj["position"] = tinygltf::Value(pos);
            arr.push_back(tinygltf::Value(obj));
        }
        return arr;
    };
    if (!skeleton->ribbonAnchors.empty()) {
        skinExtras["ribbon_emitters"] = tinygltf::Value(writeAnchors(skeleton->ribbonAnchors));
    }
    if (!skeleton->particleAnchors.empty()) {
        skinExtras["particle_emitters"] = tinygltf::Value(writeAnchors(skeleton->particleAnchors));
    }
    if (!skeleton->physicsBodies.empty()) {
        tinygltf::Value::Array arr;
        for (const auto& b : skeleton->physicsBodies) {
            tinygltf::Value::Object obj;
            obj["id"] = tinygltf::Value(static_cast<int>(b.id));
            obj["joint"] = tinygltf::Value(b.joint);
            tinygltf::Value::Object pos;
            pos["x"] = tinygltf::Value(static_cast<double>(b.position.x));
            pos["y"] = tinygltf::Value(static_cast<double>(b.position.y));
            pos["z"] = tinygltf::Value(static_cast<double>(b.position.z));
            obj["position"] = tinygltf::Value(pos);
            obj["body_type"] = tinygltf::Value(static_cast<int>(b.bodyType));
            arr.push_back(tinygltf::Value(obj));
        }
        skinExtras["physics_bodies"] = tinygltf::Value(arr);
    }
    if (!skinExtras.empty()) {
        skin.extras = tinygltf::Value(skinExtras);
    }

    out.skin = skin;
    out.skinIndex = 0;

    // Attachments/Events/Lights: real child nodes, not skin extras (see
    // gltf_skeleton.hpp's Skeleton::Attachment/Event/Light doc comments for
    // why these differ from every anchor list above) -- translation-only,
    // same convention as a joint node's own `.translation`, parented as a
    // `.children` entry of the owning joint node. Node index is computed
    // relative to this call's own running `out.anchorNodes.size()` since
    // these are appended past the joint-node range, past geosetTagNodes,
    // and past the synthesized multi-root node (if present) -- see
    // gltf.hpp's writeGlbMulti doc comment for the full node-index layout
    // this reproduces.
    auto appendAnchorNode = [&](const std::string& name, int joint, const Vec3& position) {
        int nodeIdx = static_cast<int>(meshCount + skeleton->joints.size() + skeleton->geosetTags.size() +
                                        (out.hasSyntheticRoot ? 1 : 0) + out.anchorNodes.size());
        tinygltf::Node node;
        node.name = name;
        node.translation = {position.x, position.y, position.z};
        out.anchorNodes.push_back(node);
        out.jointNodes[static_cast<size_t>(joint)].children.push_back(nodeIdx);
    };
    for (const auto& a : skeleton->attachments) {
        appendAnchorNode("attachment_" + std::to_string(a.id), a.joint, a.position);
    }
    for (const auto& e : skeleton->events) {
        appendAnchorNode("event_" + e.identifier, e.joint, e.position);
    }
    for (size_t i = 0; i < skeleton->lights.size(); ++i) {
        appendAnchorNode("light_" + std::to_string(i), skeleton->lights[i].joint,
                          skeleton->lights[i].position);
    }

    return out;
}

std::vector<tinygltf::Animation> buildAnimationClips(const std::vector<Animation>& animations,
                                                       size_t meshCount, tinygltf::Buffer& buffer,
                                                       std::vector<tinygltf::BufferView>& views,
                                                       std::vector<tinygltf::Accessor>& accessors) {
    // Animations: one glTF animation per husk::gltf::Animation, one
    // sampler+channel pair per non-empty TRS property per joint entry.
    // Joint i's data targets node (meshCount + i) -- the same joint-node
    // offset emitSkeletonAndSkin's own output uses. Input (time) accessors
    // get min/max per glTF's own requirement for animation sampler inputs;
    // rotation output values are laid out as (x, y, z, w) float arrays,
    // matching Quat's own field order.
    std::vector<tinygltf::Animation> out;
    for (const auto& anim : animations) {
        tinygltf::Animation ga;
        ga.name = anim.name;

        // M2Sequence's own per-sequence metadata (see
        // Animation::SequenceMetadata's doc comment) -- inert extras, same
        // "tag it, don't guess at semantics" treatment as skinSectionId/
        // correctionSets elsewhere.
        if (anim.sequenceMetadata) {
            const auto& sm = *anim.sequenceMetadata;
            auto vec3Array = [](const Vec3& v) {
                return tinygltf::Value(tinygltf::Value::Array{tinygltf::Value(static_cast<double>(v.x)),
                                                                tinygltf::Value(static_cast<double>(v.y)),
                                                                tinygltf::Value(static_cast<double>(v.z))});
            };
            tinygltf::Value::Object meta;
            meta["movespeed"] = tinygltf::Value(static_cast<double>(sm.movespeed));
            meta["frequency"] = tinygltf::Value(static_cast<int>(sm.frequency));
            meta["replay_min"] = tinygltf::Value(static_cast<int>(sm.replayMin));
            meta["replay_max"] = tinygltf::Value(static_cast<int>(sm.replayMax));
            meta["blend_time_in"] = tinygltf::Value(static_cast<int>(sm.blendTimeIn));
            meta["blend_time_out"] = tinygltf::Value(static_cast<int>(sm.blendTimeOut));
            meta["bounds_min"] = vec3Array(sm.boundsMin);
            meta["bounds_max"] = vec3Array(sm.boundsMax);
            meta["bounds_radius"] = tinygltf::Value(static_cast<double>(sm.boundsRadius));
            meta["variation_next"] = tinygltf::Value(static_cast<int>(sm.variationNext));
            meta["alias_next"] = tinygltf::Value(static_cast<int>(sm.aliasNext));
            meta["is_alias"] = tinygltf::Value(sm.isAlias);
            tinygltf::Value::Object animExtras;
            animExtras["sequence_metadata"] = tinygltf::Value(meta);
            ga.extras = tinygltf::Value(animExtras);
        }

        auto addChannel = [&](int nodeIdx, const char* path, const std::vector<float>& times,
                               const void* valuesData, size_t valueCount, size_t valueStride, int type,
                               bool step) {
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
            padTo4(buffer);
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
                           ja.translationValues.size(), sizeof(Vec3), TINYGLTF_TYPE_VEC3,
                           ja.translationStep);
            }
            if (!ja.rotationTimes.empty()) {
                std::vector<std::array<float, 4>> rot;
                rot.reserve(ja.rotationValues.size());
                for (const auto& q : ja.rotationValues) {
                    rot.push_back({q.x, q.y, q.z, q.w});
                }
                addChannel(nodeIdx, "rotation", ja.rotationTimes, rot.data(), rot.size(),
                           sizeof(std::array<float, 4>), TINYGLTF_TYPE_VEC4, ja.rotationStep);
            }
            if (!ja.scaleTimes.empty()) {
                addChannel(nodeIdx, "scale", ja.scaleTimes, ja.scaleValues.data(), ja.scaleValues.size(),
                           sizeof(Vec3), TINYGLTF_TYPE_VEC3, ja.scaleStep);
            }
        }

        out.push_back(ga);
    }
    return out;
}

}  // namespace husk::gltf
