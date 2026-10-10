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

void requireOwnedSet(const std::optional<Ref>& set, size_t setCount, const std::string& what) {
    if (!set) return;
    const auto* ri = std::get_if<RecordIndex>(&set->id);
    if (!ri || ri->value >= setCount) {
        throw std::runtime_error(what + "'s placement set: expected a RecordIndex < " + std::to_string(setCount) +
                                  " owned sets, got " + (ri ? std::to_string(ri->value) : "another Identity kind"));
    }
}

void requireSceneSets(const Scene& scene, size_t setCount) {
    for (size_t i = 0; i < scene.localLights.size(); ++i) {
        requireOwnedSet(scene.localLights[i].placementSet, setCount, "local light " + std::to_string(i));
    }
    for (size_t i = 0; i < scene.ambientVolumes.size(); ++i) {
        requireOwnedSet(scene.ambientVolumes[i].placementSet, setCount, "ambient volume " + std::to_string(i));
    }
}

void requireLiquids(const std::vector<LiquidGrid>& liquids, size_t partCount, size_t materialCount) {
    for (size_t i = 0; i < liquids.size(); ++i) {
        const LiquidGrid& l = liquids[i];
        std::string what = "liquid " + std::to_string(i);
        if (l.part >= partCount) {
            throw std::runtime_error(what + "'s part: expected an index < " + std::to_string(partCount) +
                                      " parts, got " + std::to_string(l.part));
        }
        const auto* ri = std::get_if<RecordIndex>(&l.material);
        if (ri && ri->value >= materialCount) {
            throw std::runtime_error(what + "'s material: expected an index < " + std::to_string(materialCount) +
                                      " materials, got " + std::to_string(ri->value));
        }
        size_t vertexCount = static_cast<size_t>(l.vertsX) * l.vertsY;
        size_t tileCount = l.vertsX == 0 || l.vertsY == 0 ? 0 : static_cast<size_t>(l.vertsX - 1) * (l.vertsY - 1);
        if (l.heights.size() != vertexCount || l.vertexData.size() != vertexCount ||
            l.tileExists.size() != tileCount || l.tileFlags.size() != tileCount) {
            throw std::runtime_error(what + ": expected " + std::to_string(vertexCount) + " heights/vertex data and " +
                                      std::to_string(tileCount) + " tiles, got " + std::to_string(l.heights.size()) +
                                      "/" + std::to_string(l.vertexData.size()) + " and " +
                                      std::to_string(l.tileExists.size()) + "/" + std::to_string(l.tileFlags.size()));
        }
    }
}

}  // namespace

Model assembleModel(Skeleton skeleton, Mesh mesh, std::vector<Material> materials,
                     std::vector<Identity> primitiveMaterials, std::vector<AnimationClip> animations,
                     Scene scene, std::vector<Model::OwnedSet> placementSets,
                     std::vector<LiquidGrid> liquids) {
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
    requireSceneSets(scene, placementSets.size());
    requireLiquids(liquids, mesh.parts.size(), materials.size());

    Model result;
    result.skeleton = std::move(skeleton);
    result.mesh = std::move(mesh);
    result.materials = std::move(materials);
    result.primitiveMaterials = std::move(primitiveMaterials);
    result.animations = std::move(animations);
    result.scene = std::move(scene);
    result.placementSets = std::move(placementSets);
    result.liquids = std::move(liquids);
    return result;
}

std::optional<size_t> resolveMaterialIndex(const Model& model, size_t primitiveIndex) {
    if (primitiveIndex >= model.primitiveMaterials.size()) return std::nullopt;
    const auto* ri = std::get_if<RecordIndex>(&model.primitiveMaterials[primitiveIndex]);
    if (!ri || ri->value >= model.materials.size()) return std::nullopt;
    return ri->value;
}

}  // namespace husk::canon
