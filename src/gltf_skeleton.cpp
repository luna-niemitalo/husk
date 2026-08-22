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
    // no-op on real skin deformation.
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
        std::string name = !src.name.empty() ? src.name : "bone_" + std::to_string(i);
        if (!src.billboardMode.empty()) {
            name += "_billboard_" + src.billboardMode;
        }
        out.jointNodes[i].name = name;
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
        // -- tools/husk_blender_geoset_mask.py's geometry-nodes switch
        // recovers the raw group/variant integers with
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
    // Real joint index -> the exact same name each joint's own node got
    // above (billboard suffix included) -- index-aligned with
    // `skin.joints[0..joints.size()-1]` (real joints only, not geoset
    // tags). Serves two real, different purposes, both kept: (1)
    // `joint_names` below is always present, giving `_root_joint_extras`
    // (Python) and this codebase's own C++ tests an unambiguous marker for
    // which imported bone is the real carrier -- every *other* key here is
    // conditional, so without something unconditional there'd be no
    // reliable way to find it on a model whose only real extras happen to
    // be, say, one billboard-tagged joint elsewhere. (2) `resolveBoneName`
    // below additionally resolves every `joint` field to a real
    // `bone_name` directly in place, so a consumer reading e.g. a single
    // ribbon anchor doesn't have to separately fetch and join against this
    // whole table just to answer "which bone is that" -- confirmed
    // directly that Blender's own post-import bone order does not reliably
    // match this raw index order (242/358 mismatches on a real 245-bone
    // character), so a Blender-side consumer genuinely cannot derive a
    // bone name from a raw index any other way.
    std::vector<std::string> jointNameByIndex;
    jointNameByIndex.reserve(out.jointNodes.size());
    for (const auto& node : out.jointNodes) {
        jointNameByIndex.push_back(node.name);
    }
    tinygltf::Value::Array jointNames;
    for (const auto& name : jointNameByIndex) {
        jointNames.emplace_back(name);
    }
    skinExtras["joint_names"] = tinygltf::Value(jointNames);
    auto resolveBoneName = [&jointNameByIndex](int joint) -> std::string {
        return (joint >= 0 && static_cast<size_t>(joint) < jointNameByIndex.size()) ? jointNameByIndex[joint]
                                                                                     : std::string();
    };
    if (!skeleton->correctionSets.empty()) {
        tinygltf::Value::Array sets;
        for (const auto& cs : skeleton->correctionSets) {
            tinygltf::Value::Object setObj;
            setObj["file_data_id"] = tinygltf::Value(static_cast<int>(cs.fileDataId));
            tinygltf::Value::Array corrections;
            for (const auto& c : cs.corrections) {
                tinygltf::Value::Object co;
                co["joint"] = tinygltf::Value(c.joint);
                co["bone_name"] = tinygltf::Value(resolveBoneName(c.joint));
                tinygltf::Value::Array mat;
                for (float f : c.matrix) mat.emplace_back(static_cast<double>(f));
                co["matrix"] = tinygltf::Value(mat);
                corrections.emplace_back(co);
            }
            setObj["corrections"] = tinygltf::Value(corrections);
            if (!cs.selectedByChoiceIds.empty()) {
                tinygltf::Value::Array choiceIds;
                for (uint32_t id : cs.selectedByChoiceIds) choiceIds.emplace_back(static_cast<int>(id));
                setObj["selected_by_choice_ids"] = tinygltf::Value(choiceIds);
            }
            sets.emplace_back(setObj);
        }
        skinExtras["bone_correction_sets"] = tinygltf::Value(sets);
    }
    if (!skeleton->enabledGeosets.empty()) {
        tinygltf::Value::Array arr;
        for (const auto& g : skeleton->enabledGeosets) {
            tinygltf::Value::Object obj;
            obj["choice_id"] = tinygltf::Value(static_cast<int>(g.choiceId));
            obj["geoset_id"] = tinygltf::Value(static_cast<int>(g.geosetId));
            arr.emplace_back(obj);
        }
        skinExtras["enabled_geosets"] = tinygltf::Value(arr);
    }
    if (!skeleton->creatureEnabledGeosets.empty()) {
        tinygltf::Value::Array arr;
        for (const auto& g : skeleton->creatureEnabledGeosets) {
            tinygltf::Value::Object obj;
            obj["geoset_index"] = tinygltf::Value(static_cast<int>(g.geosetIndex));
            obj["geoset_value"] = tinygltf::Value(static_cast<int>(g.geosetValue));
            obj["geoset_id"] = tinygltf::Value(static_cast<int>(g.geosetId));
            arr.emplace_back(obj);
        }
        skinExtras["creature_enabled_geosets"] = tinygltf::Value(arr);
    }
    auto writeAnchors = [&resolveBoneName](const std::vector<Skeleton::EmitterAnchor>& anchors) {
        tinygltf::Value::Array arr;
        for (const auto& a : anchors) {
            tinygltf::Value::Object obj;
            obj["id"] = tinygltf::Value(static_cast<int>(a.id));
            obj["joint"] = tinygltf::Value(a.joint);
            obj["bone_name"] = tinygltf::Value(resolveBoneName(a.joint));
            tinygltf::Value::Object pos;
            pos["x"] = tinygltf::Value(static_cast<double>(a.position.x));
            pos["y"] = tinygltf::Value(static_cast<double>(a.position.y));
            pos["z"] = tinygltf::Value(static_cast<double>(a.position.z));
            obj["position"] = tinygltf::Value(pos);
            arr.emplace_back(obj);
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
            obj["bone_name"] = tinygltf::Value(resolveBoneName(b.joint));
            tinygltf::Value::Object pos;
            pos["x"] = tinygltf::Value(static_cast<double>(b.position.x));
            pos["y"] = tinygltf::Value(static_cast<double>(b.position.y));
            pos["z"] = tinygltf::Value(static_cast<double>(b.position.z));
            obj["position"] = tinygltf::Value(pos);
            obj["body_type"] = tinygltf::Value(static_cast<int>(b.bodyType));
            arr.emplace_back(obj);
        }
        skinExtras["physics_bodies"] = tinygltf::Value(arr);
    }
    if (!skeleton->physicsJoints.empty()) {
        tinygltf::Value::Array arr;
        for (const auto& j : skeleton->physicsJoints) {
            tinygltf::Value::Object obj;
            obj["body_a"] = tinygltf::Value(static_cast<int>(j.bodyA));
            obj["body_b"] = tinygltf::Value(static_cast<int>(j.bodyB));
            obj["frequency_hz"] = tinygltf::Value(static_cast<double>(j.frequencyHz));
            obj["damping_ratio"] = tinygltf::Value(static_cast<double>(j.dampingRatio));
            obj["swing_limit_deg"] = tinygltf::Value(static_cast<double>(j.swingLimitDeg));
            arr.emplace_back(obj);
        }
        skinExtras["physics_joints"] = tinygltf::Value(arr);
    }
    if (skeleton->charTextureLayout) {
        const Skeleton::CharTextureLayout& layout = *skeleton->charTextureLayout;
        tinygltf::Value::Object layoutObj;
        layoutObj["layout_id"] = tinygltf::Value(static_cast<int>(layout.layoutId));
        layoutObj["width"] = tinygltf::Value(static_cast<int>(layout.width));
        layoutObj["height"] = tinygltf::Value(static_cast<int>(layout.height));

        tinygltf::Value::Array materials;
        for (const auto& m : layout.materials) {
            tinygltf::Value::Object obj;
            obj["id"] = tinygltf::Value(static_cast<int>(m.id));
            obj["texture_type"] = tinygltf::Value(static_cast<int>(m.textureType));
            obj["width"] = tinygltf::Value(static_cast<int>(m.width));
            obj["height"] = tinygltf::Value(static_cast<int>(m.height));
            obj["flags"] = tinygltf::Value(static_cast<int>(m.flags));
            materials.emplace_back(obj);
        }
        layoutObj["materials"] = tinygltf::Value(materials);

        tinygltf::Value::Array sections;
        for (const auto& s : layout.sections) {
            tinygltf::Value::Object obj;
            obj["id"] = tinygltf::Value(static_cast<int>(s.id));
            obj["section_type"] = tinygltf::Value(static_cast<int>(s.sectionType));
            obj["x"] = tinygltf::Value(static_cast<int>(s.x));
            obj["y"] = tinygltf::Value(static_cast<int>(s.y));
            obj["width"] = tinygltf::Value(static_cast<int>(s.width));
            obj["height"] = tinygltf::Value(static_cast<int>(s.height));
            obj["overlap_section_mask"] = tinygltf::Value(static_cast<int>(s.overlapSectionMask));
            sections.emplace_back(obj);
        }
        layoutObj["sections"] = tinygltf::Value(sections);

        tinygltf::Value::Array textureLayers;
        for (const auto& t : layout.textureLayers) {
            tinygltf::Value::Object obj;
            obj["id"] = tinygltf::Value(static_cast<int>(t.id));
            obj["texture_type"] = tinygltf::Value(static_cast<int>(t.textureType));
            obj["layer"] = tinygltf::Value(static_cast<int>(t.layer));
            obj["flags"] = tinygltf::Value(static_cast<int>(t.flags));
            obj["blend_mode"] = tinygltf::Value(static_cast<int>(t.blendMode));
            obj["texture_section_type_bit_mask"] = tinygltf::Value(static_cast<int>(t.textureSectionTypeBitMask));
            obj["chr_model_texture_target_id"] = tinygltf::Value(static_cast<int>(t.chrModelTextureTargetId));
            textureLayers.emplace_back(obj);
        }
        layoutObj["texture_layers"] = tinygltf::Value(textureLayers);

        skinExtras["chr_texture_layout"] = tinygltf::Value(layoutObj);
    }
    if (!skeleton->enabledMaterials.empty()) {
        tinygltf::Value::Array arr;
        for (const auto& m : skeleton->enabledMaterials) {
            tinygltf::Value::Object obj;
            obj["choice_id"] = tinygltf::Value(static_cast<int>(m.choiceId));
            obj["chr_model_texture_target_id"] = tinygltf::Value(static_cast<int>(m.chrModelTextureTargetId));
            obj["material_resources_id"] = tinygltf::Value(static_cast<int>(m.materialResourcesId));
            obj["file_data_id"] = tinygltf::Value(static_cast<int>(m.fileDataId));
            arr.emplace_back(obj);
        }
        skinExtras["chr_enabled_materials"] = tinygltf::Value(arr);
    }
    if (!skeleton->customizationOptions.empty()) {
        tinygltf::Value::Array options;
        for (const auto& opt : skeleton->customizationOptions) {
            tinygltf::Value::Object optObj;
            optObj["option_id"] = tinygltf::Value(static_cast<int>(opt.optionId));
            optObj["option_name"] = tinygltf::Value(opt.optionName);
            optObj["option_order_index"] = tinygltf::Value(static_cast<int>(opt.optionOrderIndex));

            tinygltf::Value::Array choices;
            for (const auto& choice : opt.choices) {
                tinygltf::Value::Object choiceObj;
                choiceObj["choice_id"] = tinygltf::Value(static_cast<int>(choice.choiceId));
                choiceObj["choice_name"] = tinygltf::Value(choice.choiceName);
                choiceObj["choice_order_index"] = tinygltf::Value(static_cast<int>(choice.choiceOrderIndex));
                if (choice.geosetId) {
                    choiceObj["geoset_id"] = tinygltf::Value(static_cast<int>(*choice.geosetId));
                }
                tinygltf::Value::Array materials;
                for (const auto& mat : choice.materials) {
                    tinygltf::Value::Object matObj;
                    matObj["chr_model_texture_target_id"] =
                        tinygltf::Value(static_cast<int>(mat.chrModelTextureTargetId));
                    matObj["material_resources_id"] = tinygltf::Value(static_cast<int>(mat.materialResourcesId));
                    matObj["file_data_id"] = tinygltf::Value(static_cast<int>(mat.fileDataId));
                    // Present only when nonzero -- see
                    // Skeleton::CustomizationChoice::Material::relatedChoiceId's
                    // own doc comment. Absent means unconditional, same
                    // "absence means ordinary" convention every other extras
                    // field here uses.
                    if (mat.relatedChoiceId != 0) {
                        matObj["related_choice_id"] = tinygltf::Value(static_cast<int>(mat.relatedChoiceId));
                    }
                    // Real --listfile content name, when resolved -- see
                    // Skeleton::CustomizationChoice::Material::contentName's
                    // own doc comment. Absent (not empty-string) when
                    // unresolved, same "absence means ordinary" convention
                    // as related_choice_id above.
                    if (!mat.contentName.empty()) {
                        matObj["content_name"] = tinygltf::Value(mat.contentName);
                    }
                    materials.emplace_back(matObj);
                }
                choiceObj["materials"] = tinygltf::Value(materials);
                choices.emplace_back(choiceObj);
            }
            optObj["choices"] = tinygltf::Value(choices);
            options.emplace_back(optObj);
        }
        skinExtras["chr_customization_options"] = tinygltf::Value(options);
    }
    // Attached to the skin's own first real root joint's node extras, not
    // `skin.extras` -- Blender's own glTF importer has no supported target
    // for a *skin's* extras at all (confirmed empirically: node/mesh/
    // material/camera/light/scene extras all land as real Blender custom
    // properties post-import; skin extras land nowhere -- see
    // tools/husk_blender_geoset_mask.py's module docstring), but it DOES
    // keep node/bone extras. `out.rootJointNodeIndices.front()` is always a
    // real joint (unlike the synthesized multi-root parent node, which
    // isn't a joint at all and wouldn't become a real Blender Bone),
    // guaranteed non-empty whenever `skeleton->joints` is (every skinned
    // model has at least one root). Merged with, not overwriting, any
    // existing extras that joint already carries (e.g. `billboard`, above).
    if (!skinExtras.empty() && !out.rootJointNodeIndices.empty()) {
        size_t rootLocalIdx = static_cast<size_t>(out.rootJointNodeIndices.front()) - meshCount;
        tinygltf::Value::Object merged = out.jointNodes[rootLocalIdx].extras.IsObject()
                                              ? out.jointNodes[rootLocalIdx].extras.Get<tinygltf::Value::Object>()
                                              : tinygltf::Value::Object{};
        for (const auto& [key, value] : skinExtras) {
            merged[key] = value;
        }
        out.jointNodes[rootLocalIdx].extras = tinygltf::Value(merged);
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
    // Shared by every AnimatedColorCurve/AnimatedScalarCurve list below --
    // same JSON shape gltf_mesh.cpp's own tint/fade extras use (mirrored,
    // not reused, since that code lives in a different translation unit
    // building a *material's* extras object, not a node's).
    auto colorCurvesToValue = [](const std::vector<Material::AnimatedColorCurve>& curves) {
        tinygltf::Value::Array out;
        for (const auto& c : curves) {
            tinygltf::Value::Object co;
            if (c.sequenceIndex >= 0) co["sequence_index"] = tinygltf::Value(c.sequenceIndex);
            tinygltf::Value::Array kfs;
            for (const auto& [t, v] : c.keyframes) {
                tinygltf::Value::Object kf;
                kf["time"] = tinygltf::Value(static_cast<double>(t));
                kf["value"] = tinygltf::Value(tinygltf::Value::Array{
                    tinygltf::Value(static_cast<double>(v.x)), tinygltf::Value(static_cast<double>(v.y)),
                    tinygltf::Value(static_cast<double>(v.z))});
                kfs.emplace_back(kf);
            }
            co["keyframes"] = tinygltf::Value(kfs);
            out.emplace_back(co);
        }
        return out;
    };
    auto scalarCurvesToValue = [](const std::vector<Material::AnimatedScalarCurve>& curves) {
        tinygltf::Value::Array out;
        for (const auto& c : curves) {
            tinygltf::Value::Object co;
            if (c.sequenceIndex >= 0) co["sequence_index"] = tinygltf::Value(c.sequenceIndex);
            tinygltf::Value::Array kfs;
            for (const auto& [t, v] : c.keyframes) {
                tinygltf::Value::Object kf;
                kf["time"] = tinygltf::Value(static_cast<double>(t));
                kf["value"] = tinygltf::Value(static_cast<double>(v));
                kfs.emplace_back(kf);
            }
            co["keyframes"] = tinygltf::Value(kfs);
            out.emplace_back(co);
        }
        return out;
    };
    for (const auto& a : skeleton->attachments) {
        appendAnchorNode("attachment_" + std::to_string(a.id), a.joint, a.position);
        if (!a.animateAttached.empty()) {
            tinygltf::Value::Object attachmentExtras;
            attachmentExtras["animate_attached"] = tinygltf::Value(scalarCurvesToValue(a.animateAttached));
            out.anchorNodes.back().extras = tinygltf::Value(attachmentExtras);
        }
    }
    for (const auto& e : skeleton->events) {
        appendAnchorNode("event_" + e.identifier, e.joint, e.position);
        // M2Event::data -- opaque per-event payload, no core-glTF slot;
        // see Skeleton::Event::data's doc comment for why this is exposed
        // raw rather than decoded.
        tinygltf::Value::Object eventExtras;
        eventExtras["data"] = tinygltf::Value(static_cast<int>(e.data));
        out.anchorNodes.back().extras = tinygltf::Value(eventExtras);
    }
    for (size_t i = 0; i < skeleton->lights.size(); ++i) {
        const auto& l = skeleton->lights[i];
        appendAnchorNode("light_" + std::to_string(i), l.joint, l.position);

        // M2Light::type plus every animated curve, same "inert extras, no
        // core-glTF light-property animation-channel target exists" policy
        // as the material tint/fade curves -- see Skeleton::Light's doc
        // comment. Only written when there's real data to show.
        tinygltf::Value::Object lightExtras;
        lightExtras["type"] = tinygltf::Value(static_cast<int>(l.type));
        tinygltf::Value::Object anim;
        if (!l.ambientColor.empty()) anim["ambient_color"] = tinygltf::Value(colorCurvesToValue(l.ambientColor));
        if (!l.ambientIntensity.empty())
            anim["ambient_intensity"] = tinygltf::Value(scalarCurvesToValue(l.ambientIntensity));
        if (!l.diffuseColor.empty()) anim["diffuse_color"] = tinygltf::Value(colorCurvesToValue(l.diffuseColor));
        if (!l.diffuseIntensity.empty())
            anim["diffuse_intensity"] = tinygltf::Value(scalarCurvesToValue(l.diffuseIntensity));
        if (!l.attenuationStart.empty())
            anim["attenuation_start"] = tinygltf::Value(scalarCurvesToValue(l.attenuationStart));
        if (!l.attenuationEnd.empty())
            anim["attenuation_end"] = tinygltf::Value(scalarCurvesToValue(l.attenuationEnd));
        if (!l.visibility.empty()) anim["visibility"] = tinygltf::Value(scalarCurvesToValue(l.visibility));
        if (!anim.empty()) lightExtras["light_animation"] = tinygltf::Value(anim);
        out.anchorNodes.back().extras = tinygltf::Value(lightExtras);
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
            if (!sm.animationDataName.empty()) {
                meta["animation_data_name"] = tinygltf::Value(sm.animationDataName);
            }
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
