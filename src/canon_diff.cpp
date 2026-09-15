#include "canon_diff.hpp"

#include <cmath>
#include <optional>
#include <sstream>
#include <unordered_map>

#include "writers/writer_common.hpp"   // writers::composeJointCurves

namespace husk::canon_diff {

namespace {

// Independent re-derivation of m2_material_input.cpp's private
// blendModeToBlendOp -- same reasoning tests/test_canon_material_convergence.cpp's
// own expectedBlendOp gives: a comparison must check against a fact worked
// out separately, not by calling the very function under test.
canon::BlendOp expectedBlendOp(uint16_t blendMode) {
    switch (blendMode) {
        case 0: return canon::BlendOp::Replace;
        case 1: return canon::BlendOp::Replace;
        case 2: return canon::BlendOp::Fade;
        case 3: return canon::BlendOp::Add;
        case 4: return canon::BlendOp::Add;
        case 5: return canon::BlendOp::Modulate;
        case 6: return canon::BlendOp::Modulate2x;
        default: return canon::BlendOp::Fade;
    }
}

bool near(float a, float b, float epsilon) { return std::fabs(a - b) <= epsilon; }

bool near(const gltf::Vec3& a, const gltf::Vec3& b, float epsilon) {
    return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
}

// canon::Vec2 -> gltf::Vec2 comparison (raw M2-space, no conversion) --
// uv0/uv1 stay in the same 2D coordinate system on both sides.
bool near(const canon::Vec2& a, const gltf::Vec2& b, float epsilon) {
    return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon);
}

// canon:: owns its own Vec3/Quat (canon_primitives.hpp), distinct from
// m2::Vec3/m2::Quat even though the layout is identical -- so
// commands::toGltf/toGltfScale (export_transform.hpp, m2::-typed
// overloads only) can't be called on a canon:: value directly. Rather
// than add a canon::-accepting overload to that legacy file (off-limits,
// see this project's own "never touch the legacy pipeline to share code
// with the canon-comparison path" rule), these wrap the same real,
// already-shared gltf_math.hpp functions legacy's own toGltf wraps --
// zUpToYUp/rotationZUpToYUp/scaleZUpToYUp aren't legacy-private, they're
// the one real axis-conversion implementation both sides already use.
gltf::Vec3 toGltf(const canon::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }
gltf::Quat toGltf(const canon::Quat& q) { return gltf::rotationZUpToYUp({q.x, q.y, q.z, q.w}); }
gltf::Vec3 toGltfScale(const canon::Vec3& v) { return gltf::scaleZUpToYUp({v.x, v.y, v.z}); }

std::string billboardModeString(canon::BillboardMode mode) {
    switch (mode) {
        case canon::BillboardMode::None: return "";
        case canon::BillboardMode::Spherical: return "spherical";
        case canon::BillboardMode::CylindricalLockX: return "cylindrical_lock_x";
        case canon::BillboardMode::CylindricalLockY: return "cylindrical_lock_y";
        case canon::BillboardMode::CylindricalLockZ: return "cylindrical_lock_z";
    }
    return "";
}

}  // namespace

void Report::append(Report&& other) {
    deviations.insert(deviations.end(), std::make_move_iterator(other.deviations.begin()),
                       std::make_move_iterator(other.deviations.end()));
    notes.insert(notes.end(), std::make_move_iterator(other.notes.begin()),
                 std::make_move_iterator(other.notes.end()));
}

Report compareMesh(const canon::Mesh& canonMesh, const gltf::Mesh& legacyMesh, float epsilon) {
    Report r;

    if (canonMesh.positions.size() != legacyMesh.positions.size()) {
        std::ostringstream os;
        os << "vertex count differs: canon " << canonMesh.positions.size() << " vs legacy "
           << legacyMesh.positions.size();
        r.deviations.push_back(os.str());
        return r;  // nothing further is safely comparable
    }
    size_t n = canonMesh.positions.size();

    std::optional<size_t> posMismatch, normalMismatch, uvMismatch;
    for (size_t i = 0; i < n; ++i) {
        if (!posMismatch && !near(toGltf(canonMesh.positions[i]), legacyMesh.positions[i], epsilon)) {
            posMismatch = i;
        }
        if (!normalMismatch && !near(toGltf(canonMesh.normals[i]), legacyMesh.normals[i], epsilon)) {
            normalMismatch = i;
        }
        if (!uvMismatch && !near(canonMesh.uv0[i], legacyMesh.texCoords[i], epsilon)) {
            uvMismatch = i;
        }
    }
    if (posMismatch) r.deviations.push_back("position mismatch at vertex " + std::to_string(*posMismatch));
    if (normalMismatch) r.deviations.push_back("normal mismatch at vertex " + std::to_string(*normalMismatch));
    if (uvMismatch) r.deviations.push_back("uv0 mismatch at vertex " + std::to_string(*uvMismatch));

    // uv1: canon omits it entirely for an all-origin second UV set
    // (m2_mesh_input.hpp's own doc comment); legacy always emits
    // texCoords2, fabricated-all-zero in that same case. Only a real
    // comparison when canon actually populated one.
    if (canonMesh.uv1) {
        if (legacyMesh.texCoords2.size() != n) {
            r.deviations.push_back("canon populated uv1 but legacy texCoords2 size doesn't match vertex count");
        } else {
            std::optional<size_t> uv1Mismatch;
            for (size_t i = 0; i < n; ++i) {
                if (!near((*canonMesh.uv1)[i], legacyMesh.texCoords2[i], epsilon)) {
                    uv1Mismatch = i;
                    break;
                }
            }
            if (uv1Mismatch) r.deviations.push_back("uv1 mismatch at vertex " + std::to_string(*uv1Mismatch));
        }
    } else {
        r.notes.push_back("canon omitted uv1 (every vertex's second UV coordinate is the origin) -- "
                           "legacy still emits a fabricated all-zero texCoords2, a documented, "
                           "deliberate divergence (m2_mesh_input.hpp)");
    }

    // Skinning.
    if (canonMesh.skinning.empty()) {
        if (!legacyMesh.skinning.empty()) {
            bool legacyAllZero = true;
            for (const auto& jw : legacyMesh.skinning) {
                for (float w : jw.weights) {
                    if (w != 0.0f) legacyAllZero = false;
                }
            }
            if (legacyAllZero) {
                r.notes.push_back("canon left skinning empty for this all-zero-weight (unskinned) model; "
                                   "legacy still fills boneCount-many meaningless zero-weight entries -- "
                                   "documented, deliberate divergence (m2_mesh_input.hpp)");
            } else {
                r.deviations.push_back("canon reports this model as unskinned (empty skinning), but legacy's "
                                        "skinning has real nonzero weights");
            }
        }
    } else if (legacyMesh.skinning.size() != n) {
        r.deviations.push_back("canon populated skinning but legacy skinning size doesn't match vertex count");
    } else {
        std::optional<size_t> jointMismatch, weightMismatch;
        for (size_t i = 0; i < n; ++i) {
            const auto& cs = canonMesh.skinning[i];
            const auto& ls = legacyMesh.skinning[i];
            for (int j = 0; j < 4; ++j) {
                if (!jointMismatch && cs.joints[static_cast<size_t>(j)] != ls.joints[j]) jointMismatch = i;
                if (!weightMismatch && !near(cs.weights[static_cast<size_t>(j)], ls.weights[j], epsilon)) {
                    weightMismatch = i;
                }
            }
        }
        if (jointMismatch) r.deviations.push_back("skinning joint mismatch at vertex " + std::to_string(*jointMismatch));
        if (weightMismatch) {
            r.deviations.push_back("skinning weight mismatch at vertex " + std::to_string(*weightMismatch));
        }
    }

    // Primitives: shared-index-space slices, mirroring
    // assemblePrimitiveGeosets/buildMaterialsAndPrimitives's own proven
    // batch<->primitive alignment (test_canon_mesh_convergence.cpp).
    if (canonMesh.primitives.size() != legacyMesh.primitives.size()) {
        std::ostringstream os;
        os << "primitive count differs: canon " << canonMesh.primitives.size() << " vs legacy "
           << legacyMesh.primitives.size();
        r.deviations.push_back(os.str());
    } else {
        std::optional<size_t> geosetIdMismatch, indicesMismatch;
        for (size_t i = 0; i < canonMesh.primitives.size(); ++i) {
            const auto& cp = canonMesh.primitives[i];
            const auto& lp = legacyMesh.primitives[i];

            if (!geosetIdMismatch && std::holds_alternative<canon::RecordIndex>(cp.geoset.ref.id)) {
                uint32_t rawId = std::get<canon::RecordIndex>(cp.geoset.ref.id).value;
                if (lp.skinSectionId >= 0 && static_cast<uint32_t>(lp.skinSectionId) != rawId) {
                    geosetIdMismatch = i;
                }
            }

            if (!indicesMismatch) {
                if (cp.indexStart + cp.indexCount > canonMesh.indices.size()) {
                    indicesMismatch = i;
                } else {
                    std::vector<uint32_t> slice(canonMesh.indices.begin() + static_cast<long>(cp.indexStart),
                                                 canonMesh.indices.begin() +
                                                     static_cast<long>(cp.indexStart + cp.indexCount));
                    if (slice != lp.indices) indicesMismatch = i;
                }
            }
        }
        if (geosetIdMismatch) {
            r.deviations.push_back("primitive geoset id mismatch at primitive " + std::to_string(*geosetIdMismatch));
        }
        if (indicesMismatch) {
            r.deviations.push_back("primitive index-range contents mismatch at primitive " +
                                    std::to_string(*indicesMismatch));
        }
    }

    return r;
}

Report compareSkeleton(const canon::Skeleton& canonSkeleton, const gltf::Skeleton& legacySkeleton, float epsilon) {
    Report r;

    if (canonSkeleton.joints.size() != legacySkeleton.joints.size()) {
        std::ostringstream os;
        os << "joint count differs: canon " << canonSkeleton.joints.size() << " vs legacy "
           << legacySkeleton.joints.size();
        r.deviations.push_back(os.str());
        return r;
    }

    std::optional<size_t> parentMismatch, posMismatch, billboardMismatch;
    for (size_t i = 0; i < canonSkeleton.joints.size(); ++i) {
        const auto& cj = canonSkeleton.joints[i];
        const auto& lj = legacySkeleton.joints[i];

        if (!parentMismatch && cj.parent != lj.parent) parentMismatch = i;
        if (!posMismatch && !near(toGltf(cj.globalPosition), lj.globalPosition, epsilon)) {
            posMismatch = i;
        }
        if (!billboardMismatch && billboardModeString(cj.billboard) != lj.billboardMode) {
            billboardMismatch = i;
        }
    }
    if (parentMismatch) r.deviations.push_back("joint parent mismatch at joint " + std::to_string(*parentMismatch));
    if (posMismatch) r.deviations.push_back("joint bind-pose position mismatch at joint " + std::to_string(*posMismatch));
    if (billboardMismatch) {
        r.deviations.push_back("joint billboard mode mismatch at joint " + std::to_string(*billboardMismatch));
    }

    r.notes.push_back("joint display names (canon::Joint::ref.name/structuralLabel vs "
                       "gltf::Skeleton::Joint::name) aren't compared -- canon's structural-label naming "
                       "tier is a new fact with no legacy equivalent, not a mirrored one "
                       "(REFACTOR/AUDIT.md, DESIGN.md's canon bone-naming section)");

    return r;
}

Report compareAnimations(const canon::Model& canonModel, const std::vector<m2::Sequence>& modelSequences,
                         const std::vector<gltf::Animation>& legacyAnimations, float translationEpsilon) {
    Report r;

    std::unordered_map<std::string, const gltf::Animation*> byName;
    for (const auto& anim : legacyAnimations) byName.emplace(anim.name, &anim);

    for (const auto& clip : canonModel.animations) {
        std::string expectedName;
        if (clip.sequence.kind == canon::SequenceRef::Kind::Sequence) {
            uint32_t si = clip.sequence.index;
            if (si >= modelSequences.size()) {
                r.deviations.push_back("canon animation clip references out-of-range sequence index " +
                                        std::to_string(si));
                continue;
            }
            const auto& seq = modelSequences[si];
            // Matches both a real-inline sequence's own name AND a
            // pure-alias sequence's name (canon_model.cpp's pass 3 tags an
            // alias clip with the ALIAS's own sequence index, not its
            // terminal's -- the same "clip identity comes from the
            // original alias" convention buildAnimations's own `anim.name`
            // construction uses), so this one expression covers both.
            expectedName = "anim_" + std::to_string(seq.id) + "_" + std::to_string(seq.variationIndex);
        } else {
            // canon_model.cpp's pass 2 -- mirrors
            // buildGlobalSequenceAnimations's own `anim.name` convention
            // (export_animation.cpp) exactly.
            expectedName = "global_seq_" + std::to_string(clip.sequence.index);
        }

        auto it = byName.find(expectedName);
        if (it == byName.end()) {
            r.deviations.push_back("canon emitted a clip (" + expectedName +
                                    ") but no legacy animation of that name exists -- a real gap (canon "
                                    "and legacy disagree on whether this clip has any real keyframe "
                                    "data at all, or on how it should be named)");
            continue;
        }
        const gltf::Animation& legacyAnim = *it->second;

        std::unordered_map<int, const gltf::JointAnimation*> legacyByJoint;
        for (const auto& ja : legacyAnim.joints) legacyByJoint.emplace(ja.joint, &ja);

        std::optional<size_t> missingJoint, translationCountMismatch, translationValueMismatch,
            rotationCountMismatch, rotationValueMismatch, scaleCountMismatch, scaleValueMismatch;
        for (size_t bi = 0; bi < clip.boneCurves.size(); ++bi) {
            if (!clip.boneCurves[bi].has_value()) continue;
            auto composed = writers::composeJointCurves(canonModel.skeleton, clip, bi);
            if (!composed) continue;  // can't happen given the has_value() check above

            auto lit = legacyByJoint.find(static_cast<int>(bi));
            if (lit == legacyByJoint.end()) {
                if (!missingJoint) missingJoint = bi;
                continue;
            }
            const gltf::JointAnimation& lj = *lit->second;

            if (composed->translation.keyframes.size() != lj.translationTimes.size()) {
                if (!translationCountMismatch) translationCountMismatch = bi;
            } else {
                for (size_t k = 0; k < composed->translation.keyframes.size(); ++k) {
                    gltf::Vec3 cv = toGltf(composed->translation.keyframes[k].second);
                    if (!near(cv, lj.translationValues[k], translationEpsilon)) {
                        if (!translationValueMismatch) translationValueMismatch = bi;
                        break;
                    }
                }
            }

            if (composed->rotation.keyframes.size() != lj.rotationTimes.size()) {
                if (!rotationCountMismatch) rotationCountMismatch = bi;
            } else {
                for (size_t k = 0; k < composed->rotation.keyframes.size(); ++k) {
                    gltf::Quat cq = toGltf(composed->rotation.keyframes[k].second);
                    const gltf::Quat& lq = lj.rotationValues[k];
                    // Sign/hemisphere-invariant: a flipped-sign quaternion
                    // represents the identical rotation, and legacy applies
                    // gltf::enforceHemisphereContinuity after this same
                    // conversion (m2_animation_input.hpp's own doc
                    // comment) -- a per-keyframe sign difference here is
                    // exactly that fix at work, not a real mismatch.
                    float dot = cq.x * lq.x + cq.y * lq.y + cq.z * lq.z + cq.w * lq.w;
                    if (std::fabs(dot) < 1.0f - 1e-3f) {
                        if (!rotationValueMismatch) rotationValueMismatch = bi;
                        break;
                    }
                }
            }

            if (composed->scale.keyframes.size() != lj.scaleTimes.size()) {
                if (!scaleCountMismatch) scaleCountMismatch = bi;
            } else {
                for (size_t k = 0; k < composed->scale.keyframes.size(); ++k) {
                    gltf::Vec3 cv = toGltfScale(composed->scale.keyframes[k].second);
                    if (!near(cv, lj.scaleValues[k], translationEpsilon)) {
                        if (!scaleValueMismatch) scaleValueMismatch = bi;
                        break;
                    }
                }
            }
        }

        std::string clipTag = " (clip '" + expectedName + "')";
        if (missingJoint) {
            r.deviations.push_back("canon animates joint " + std::to_string(*missingJoint) +
                                    " but legacy has no JointAnimation entry for it" + clipTag);
        }
        if (translationCountMismatch) {
            r.deviations.push_back("translation keyframe count mismatch at joint " +
                                    std::to_string(*translationCountMismatch) + clipTag);
        }
        if (translationValueMismatch) {
            r.deviations.push_back("translation value mismatch at joint " +
                                    std::to_string(*translationValueMismatch) + clipTag);
        }
        if (rotationCountMismatch) {
            r.deviations.push_back("rotation keyframe count mismatch at joint " +
                                    std::to_string(*rotationCountMismatch) + clipTag);
        }
        if (rotationValueMismatch) {
            r.deviations.push_back("rotation value mismatch at joint " + std::to_string(*rotationValueMismatch) +
                                    clipTag);
        }
        if (scaleCountMismatch) {
            r.deviations.push_back("scale keyframe count mismatch at joint " + std::to_string(*scaleCountMismatch) +
                                    clipTag);
        }
        if (scaleValueMismatch) {
            r.deviations.push_back("scale value mismatch at joint " + std::to_string(*scaleValueMismatch) + clipTag);
        }
    }

    r.notes.push_back("external-.anim-resolved clips ARE compared above like any other clip when the "
                       "caller supplied m2input::ExternalAnimBlobs (AUDIT.md §7.2's API shape, closed "
                       "2026-09-15) -- this note only flags that no separate distinction is drawn "
                       "between an inline-sourced and an external-blob-sourced clip's own deviations; a "
                       "caller that passed no external blobs at all simply sees canon emit fewer clips, "
                       "the same 'absent, not approximated' case every unresolvable sequence already "
                       "gets, not a scope limitation of this comparator");

    return r;
}

Report compareMaterialBlendModes(const canon::Model& canonModel, const std::vector<skin::Batch>& batches,
                                 const std::vector<skin::Submesh>& submeshes,
                                 const std::vector<m2::Material>& materials,
                                 const std::vector<gltf::Material>& legacyMaterials,
                                 const std::vector<gltf::Primitive>& legacyPrimitives) {
    Report r;

    // Independently walk `batches` applying the exact same zero-indexCount
    // skip m2input::buildCanonModel/assemblePrimitiveGeosets apply, so
    // `expectedBlendMode[i]` lines up with `canonModel.mesh.primitives[i]`/
    // `canonModel.primitiveMaterials[i]` by construction, not by
    // re-trusting canon's own skip logic.
    std::vector<uint16_t> expectedBlendMode;
    for (const auto& b : batches) {
        if (b.skinSectionIndex >= submeshes.size()) continue;
        if (submeshes[b.skinSectionIndex].indexCount == 0) continue;
        expectedBlendMode.push_back(b.materialIndex < materials.size() ? materials[b.materialIndex].blendMode
                                                                        : uint16_t{0});
    }

    if (expectedBlendMode.size() != canonModel.primitiveMaterials.size()) {
        std::ostringstream os;
        os << "primitiveMaterials count differs from surviving-batch count: expected " << expectedBlendMode.size()
           << " vs canon " << canonModel.primitiveMaterials.size();
        r.deviations.push_back(os.str());
        return r;
    }

    // canon::resolveMaterialIndex is the one shared implementation of this
    // lookup (I2, canon_model.hpp's own doc comment) -- nullptr here just
    // adapts its std::optional<size_t> to a pointer for this function's
    // own loop convenience.
    auto resolveMaterial = [&](size_t primitiveIndex) -> const canon::Material* {
        auto idx = canon::resolveMaterialIndex(canonModel, primitiveIndex);
        return idx ? &canonModel.materials[*idx] : nullptr;
    };

    std::optional<size_t> blendOpMismatch;
    for (size_t i = 0; i < expectedBlendMode.size(); ++i) {
        const canon::Material* mat = resolveMaterial(i);
        if (!mat || mat->layers.empty()) continue;  // nothing to compare a blend op against
        if (mat->layers.front().blendIntoPrevious != expectedBlendOp(expectedBlendMode[i]) && !blendOpMismatch) {
            blendOpMismatch = i;
        }
    }
    if (blendOpMismatch) {
        r.deviations.push_back("first-layer blend op mismatch at primitive " + std::to_string(*blendOpMismatch));
    }

    // Texture-identity check: only when the caller actually supplied BOTH
    // legacy's own material list AND its own per-primitive materialIndex
    // (see this function's own doc comment for why either being empty
    // silently skips this, and why only the `Resolved` canon state is
    // checked at all). legacy's `gltf::Primitive::materialIndex` is its own
    // real indirection into its own deduped `legacyMaterials` -- the exact
    // counterpart to canon's `primitiveMaterials` -> `materials`, not
    // assumed to be 1:1 with the primitive/batch count either.
    if (!legacyMaterials.empty() && !legacyPrimitives.empty()) {
        if (legacyPrimitives.size() != expectedBlendMode.size()) {
            std::ostringstream os;
            os << "legacyPrimitives count (" << legacyPrimitives.size() << ") differs from surviving-batch count ("
               << expectedBlendMode.size() << ") -- skipping texture-identity check";
            r.notes.push_back(os.str());
        } else {
            std::optional<size_t> textureMismatch;
            for (size_t i = 0; i < expectedBlendMode.size(); ++i) {
                const canon::Material* mat = resolveMaterial(i);
                if (!mat || mat->layers.empty()) continue;
                const canon::TextureRef& ref = mat->layers.front().texture;
                if (ref.state != canon::TextureRef::State::Resolved) continue;  // no opinion to check
                const auto* fdid = std::get_if<canon::FileDataId>(&ref.resolved.id);
                if (!fdid) continue;  // Resolved but no real FileDataID identity -- nothing to compare

                int legacyMatIndex = legacyPrimitives[i].materialIndex;
                if (legacyMatIndex < 0 || static_cast<size_t>(legacyMatIndex) >= legacyMaterials.size()) continue;
                if (fdid->value != legacyMaterials[legacyMatIndex].baseColorTextureFileDataId && !textureMismatch) {
                    textureMismatch = i;
                }
            }
            if (textureMismatch) {
                const canon::Material* mat = resolveMaterial(*textureMismatch);
                uint32_t canonFdid = std::get<canon::FileDataId>(mat->layers.front().texture.resolved.id).value;
                int legacyMatIndex = legacyPrimitives[*textureMismatch].materialIndex;
                std::ostringstream os;
                os << "first-layer resolved texture FileDataID mismatch at primitive " << *textureMismatch
                   << ": canon " << canonFdid << " vs legacy "
                   << legacyMaterials[legacyMatIndex].baseColorTextureFileDataId;
                r.deviations.push_back(os.str());
            }
        }
    }

    r.notes.push_back("only each primitive's own resolved material's first-layer blendIntoPrevious op and (when "
                       "legacyMaterials/legacyPrimitives are supplied) resolved texture FileDataID are checked -- "
                       "tint/alphaFade/uvAnimation curve comparison, and any check at all for a canon "
                       "KnownUnresolved/Ambiguous state, remain unimplemented in this diagnostic");
    return r;
}

}  // namespace husk::canon_diff
