#include "canon_model.hpp"

#include <algorithm>
#include <set>
#include <variant>
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
}

void requireAnchors(const std::vector<Model::OwnedSet>& sets, size_t jointCount) {
    for (const Model::OwnedSet& owned : sets) {
        for (const PlacedInstance& instance : owned.set.instances) {
            if (!instance.anchor) continue;
            requireJoint(*instance.anchor, jointCount,
                         "placement set '" + owned.set.ref.name + "' instance " + std::to_string(instance.id) + "'s anchor");
        }
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
    for (size_t p = 0; p < mesh.parts.size(); ++p) {
        for (const PortalLink& link : mesh.parts[p].portals) {
            if (link.portal >= mesh.portals.size() || link.toPart >= mesh.parts.size()) {
                throw std::runtime_error("part " + std::to_string(p) + "'s portal link: expected portal < " +
                                          std::to_string(mesh.portals.size()) + " and part < " +
                                          std::to_string(mesh.parts.size()) + ", got portal " +
                                          std::to_string(link.portal) + " to part " + std::to_string(link.toPart));
            }
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
}

void requireEffectInstance(const InstanceRef& ref, const std::vector<Model::OwnedSet>& sets, const std::string& what) {
    if (ref.set >= sets.size()) {
        throw std::runtime_error(what + ": expected a placement set < " + std::to_string(sets.size()) + ", got " +
                                  std::to_string(ref.set));
    }
    const std::vector<PlacedInstance>& instances = sets[ref.set].set.instances;
    auto it = std::find_if(instances.begin(), instances.end(), [&](const PlacedInstance& i) { return i.id == ref.id; });
    if (it == instances.end() || !it->effect) {
        throw std::runtime_error(what + ": expected an effect instance with id " + std::to_string(ref.id) +
                                  " in placement set " + std::to_string(ref.set) + ", got " +
                                  (it == instances.end() ? "no instance" : "a model instance"));
    }
}

void requireCollision(const CollisionMesh& c) {
    if (c.indices.size() % 3 != 0) {
        throw std::runtime_error("collision indices: expected a multiple of 3, got " + std::to_string(c.indices.size()));
    }
    for (uint32_t i : c.indices) {
        if (i >= c.positions.size()) {
            throw std::runtime_error("collision indices: expected < " + std::to_string(c.positions.size()) +
                                      " positions, got " + std::to_string(i));
        }
    }
    size_t triangles = c.indices.size() / 3;
    if (!c.triangleNormals.empty() && c.triangleNormals.size() != triangles) {
        throw std::runtime_error("collision triangleNormals: expected none or one per triangle (" +
                                  std::to_string(triangles) + "), got " + std::to_string(c.triangleNormals.size()));
    }
}

// `vertexCount`/`partCount`: the streams and parts the mesh's attributes
// cover, which a mesh level takes from Model::mesh.
void requireAttributes(const Mesh& mesh, size_t vertexCount, size_t partCount) {
    std::set<std::string> names;
    for (const AttributeChannel& a : mesh.attributes) {
        if (!names.insert(a.name).second) throw std::runtime_error("attribute '" + a.name + "': expected a unique name");
        size_t elements = 0;
        switch (a.domain) {
            case AttributeDomain::Vertex: elements = vertexCount; break;
            case AttributeDomain::Face: elements = mesh.indices.size() / 3; break;
            case AttributeDomain::Part: elements = partCount; break;
        }
        size_t values = std::visit([](const auto& v) { return v.size(); }, a.values);
        if (a.components == 0 || values != elements * a.components) {
            throw std::runtime_error("attribute '" + a.name + "': expected " + std::to_string(elements) + " elements x " +
                                      std::to_string(a.components) + " components, got " + std::to_string(values) +
                                      " values");
        }
    }
}

void requirePartEffects(const std::vector<MeshPart>& parts, const std::vector<Model::OwnedSet>& sets) {
    for (size_t p = 0; p < parts.size(); ++p) {
        for (const InstanceRef& fog : parts[p].fogs) requireEffectInstance(fog, sets, "part " + std::to_string(p) + "'s fog");
        for (const InstanceRef& volume : parts[p].ambientVolumes) {
            requireEffectInstance(volume, sets, "part " + std::to_string(p) + "'s ambient volume");
        }
        for (const InstanceRef& volume : parts[p].particulateVolumes) {
            requireEffectInstance(volume, sets, "part " + std::to_string(p) + "'s particulate volume");
        }
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
                     std::vector<LiquidGrid> liquids, CollisionMesh collision, std::optional<InstanceRef> defaultFog) {
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
    requireAnchors(placementSets, skeleton.joints.size());
    requireParts(mesh);
    requireUniqueInstanceIds(placementSets);
    requireSceneSets(scene, placementSets.size());
    requirePartEffects(mesh.parts, placementSets);
    if (defaultFog) requireEffectInstance(*defaultFog, placementSets, "default fog");
    requireLiquids(liquids, mesh.parts.size(), materials.size());
    requireCollision(collision);
    requireAttributes(mesh, mesh.positions.size(), mesh.parts.size());

    Model result;
    result.skeleton = std::move(skeleton);
    result.mesh = std::move(mesh);
    result.materials = std::move(materials);
    result.primitiveMaterials = std::move(primitiveMaterials);
    result.animations = std::move(animations);
    result.scene = std::move(scene);
    result.placementSets = std::move(placementSets);
    result.defaultFog = defaultFog;
    result.liquids = std::move(liquids);
    result.collision = std::move(collision);
    return result;
}

namespace {

std::optional<size_t> localMaterial(const std::vector<Identity>& primitiveMaterials, size_t materialCount,
                                    size_t primitiveIndex) {
    if (primitiveIndex >= primitiveMaterials.size()) return std::nullopt;
    const auto* ri = std::get_if<RecordIndex>(&primitiveMaterials[primitiveIndex]);
    if (!ri || ri->value >= materialCount) return std::nullopt;
    return ri->value;
}

}  // namespace

std::optional<size_t> resolveMaterialIndex(const Model& model, size_t primitiveIndex) {
    return localMaterial(model.primitiveMaterials, model.materials.size(), primitiveIndex);
}

std::optional<size_t> resolveLevelMaterialIndex(const Model& model, size_t level, size_t primitiveIndex) {
    return localMaterial(model.meshLevels.at(level).primitiveMaterials, model.materials.size(), primitiveIndex);
}

void setMeshLevels(Model& model, std::vector<MeshLevel> levels) {
    for (size_t l = 0; l < levels.size(); ++l) {
        const MeshLevel& level = levels[l];
        const Mesh& mesh = level.mesh;
        std::string what = "mesh level " + std::to_string(l + 1);
        if (level.sharesVertices && !mesh.positions.empty()) {
            throw std::runtime_error(what + ": expected no vertex streams of its own when it shares the mesh's, got " +
                                      std::to_string(mesh.positions.size()) + " positions");
        }
        size_t vertexCount = level.sharesVertices ? model.mesh.positions.size() : mesh.positions.size();
        size_t partCount = model.mesh.parts.size();
        if (level.primitiveMaterials.size() != mesh.primitives.size()) {
            throw std::runtime_error(what + ": expected one primitive material per primitive (" +
                                      std::to_string(mesh.primitives.size()) + "), got " +
                                      std::to_string(level.primitiveMaterials.size()));
        }
        for (size_t i = 0; i < mesh.primitives.size(); ++i) {
            const PrimitiveGeoset& prim = mesh.primitives[i];
            const auto* ri = std::get_if<RecordIndex>(&level.primitiveMaterials[i]);
            if (ri && ri->value >= model.materials.size()) {
                throw std::runtime_error(what + " primitive " + std::to_string(i) + "'s material: expected an index < " +
                                          std::to_string(model.materials.size()) + ", got " + std::to_string(ri->value));
            }
            if (prim.part && *prim.part >= partCount) {
                throw std::runtime_error(what + " primitive " + std::to_string(i) + "'s part: expected an index < " +
                                          std::to_string(partCount) + ", got " + std::to_string(*prim.part));
            }
            if (static_cast<size_t>(prim.indexStart) + prim.indexCount > mesh.indices.size()) {
                throw std::runtime_error(what + " primitive " + std::to_string(i) + ": expected indices [" +
                                          std::to_string(prim.indexStart) + ", +" + std::to_string(prim.indexCount) +
                                          ") within " + std::to_string(mesh.indices.size()));
            }
        }
        if (mesh.indices.size() % 3 != 0) {
            throw std::runtime_error(what + ": expected a multiple of 3 indices, got " + std::to_string(mesh.indices.size()));
        }
        auto highest = std::max_element(mesh.indices.begin(), mesh.indices.end());
        if (highest != mesh.indices.end() && *highest >= vertexCount) {
            throw std::runtime_error(what + ": expected indices < " + std::to_string(vertexCount) + " vertices, got " +
                                      std::to_string(*highest));
        }
        for (const PartLevel& p : level.partLevels) {
            if (p.part >= partCount) {
                throw std::runtime_error(what + "'s part level: expected a part < " + std::to_string(partCount) +
                                          ", got " + std::to_string(p.part));
            }
        }
        requireAttributes(mesh, vertexCount, partCount);
    }
    model.meshLevels = std::move(levels);
}

}  // namespace husk::canon
