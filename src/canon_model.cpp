#include "canon_model.hpp"

#include <set>
#include <stdexcept>

namespace husk::canon {

namespace {

void requireJoint(const Ref& bone, size_t jointCount, const std::string& what) {
    const auto* ri = std::get_if<RecordIndex>(&bone.id);
    if (!ri) {
        throw std::runtime_error(what + "'s bone: expected a RecordIndex identity, got another Identity kind");
    }
    if (ri->value >= jointCount) {
        throw std::runtime_error(what + "'s bone: expected an index < " + std::to_string(jointCount) +
                                  " joints, got " + std::to_string(ri->value));
    }
}

void requireJoints(const Scene& scene, size_t jointCount) {
    for (size_t i = 0; i < scene.attachments.size(); ++i) {
        requireJoint(scene.attachments[i].bone, jointCount, "attachment " + std::to_string(i));
    }
    for (size_t i = 0; i < scene.events.size(); ++i) {
        requireJoint(scene.events[i].bone, jointCount, "event " + std::to_string(i));
    }
    for (size_t i = 0; i < scene.lights.size(); ++i) {
        if (scene.lights[i].bone) requireJoint(*scene.lights[i].bone, jointCount, "light " + std::to_string(i));
    }
    for (size_t i = 0; i < scene.ribbons.size(); ++i) {
        requireJoint(scene.ribbons[i].bone, jointCount, "ribbon emitter " + std::to_string(i));
    }
    for (size_t i = 0; i < scene.particles.size(); ++i) {
        requireJoint(scene.particles[i].bone, jointCount, "particle emitter " + std::to_string(i));
    }
}

void requireParts(const Mesh& mesh) {
    for (size_t i = 0; i < mesh.primitives.size(); ++i) {
        const auto& part = mesh.primitives[i].part;
        if (part && *part >= mesh.parts.size()) {
            throw std::runtime_error("primitive " + std::to_string(i) + "'s part: expected an index < " +
                                      std::to_string(mesh.parts.size()) + " parts, got " + std::to_string(*part));
        }
    }
}

void requireUniqueInstanceIds(const std::vector<Model::OwnedSet>& sets) {
    for (const Model::OwnedSet& owned : sets) {
        std::set<uint32_t> seen;
        for (const PlacedInstance& instance : owned.set.instances) {
            if (!seen.insert(instance.id).second) {
                throw std::runtime_error("placement set '" + owned.set.ref.name + "': instance id " +
                                          std::to_string(instance.id) + " appears more than once");
            }
        }
    }
}

}  // namespace

Model assembleModel(Skeleton skeleton, Mesh mesh, std::vector<Material> materials,
                     std::vector<Identity> primitiveMaterials, std::vector<AnimationClip> animations,
                     Scene scene, std::vector<Model::OwnedSet> placementSets) {
    if (primitiveMaterials.size() != mesh.primitives.size()) {
        throw std::runtime_error("primitiveMaterials size (" + std::to_string(primitiveMaterials.size()) +
                                  ") must match mesh.primitives size (" + std::to_string(mesh.primitives.size()) +
                                  ")");
    }
    // Only RecordIndex is locally resolvable/validatable here -- a
    // FileDataId/Db2Row identity names a bundle-external material this
    // Model doesn't hold, so it's accepted unchecked (see Model's own doc
    // comment).
    for (const auto& id : primitiveMaterials) {
        if (const auto* ri = std::get_if<RecordIndex>(&id)) {
            if (ri->value >= materials.size()) {
                throw std::runtime_error("primitiveMaterials references RecordIndex " +
                                          std::to_string(ri->value) + ", out of range for " +
                                          std::to_string(materials.size()) + " materials");
            }
        }
    }
    requireJoints(scene, skeleton.joints.size());
    requireParts(mesh);
    requireUniqueInstanceIds(placementSets);

    Model result;
    result.skeleton = std::move(skeleton);
    result.mesh = std::move(mesh);
    result.materials = std::move(materials);
    result.primitiveMaterials = std::move(primitiveMaterials);
    result.animations = std::move(animations);
    result.scene = std::move(scene);
    result.placementSets = std::move(placementSets);
    return result;
}

std::optional<size_t> resolveMaterialIndex(const Model& model, size_t primitiveIndex) {
    if (primitiveIndex >= model.primitiveMaterials.size()) return std::nullopt;
    const auto* ri = std::get_if<RecordIndex>(&model.primitiveMaterials[primitiveIndex]);
    if (!ri || ri->value >= model.materials.size()) return std::nullopt;
    return ri->value;
}

}  // namespace husk::canon
