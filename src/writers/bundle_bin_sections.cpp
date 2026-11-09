#include "writers/bundle_bin_sections.hpp"

#include <array>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "writers/writer_common.hpp"

namespace husk::writers {

namespace {

std::string billboardName(canon::BillboardMode mode) {
    switch (mode) {
        case canon::BillboardMode::None: return "none";
        case canon::BillboardMode::Spherical: return "spherical";
        case canon::BillboardMode::CylindricalLockX: return "cylindrical_lock_x";
        case canon::BillboardMode::CylindricalLockY: return "cylindrical_lock_y";
        case canon::BillboardMode::CylindricalLockZ: return "cylindrical_lock_z";
    }
    return "none";
}

template <typename T>
const char* componentTypeName() {
    if constexpr (std::is_same_v<T, uint8_t>) return "u8";
    if constexpr (std::is_same_v<T, uint16_t>) return "u16";
    if constexpr (std::is_same_v<T, uint32_t>) return "u32";
    if constexpr (std::is_same_v<T, int32_t>) return "i32";
    if constexpr (std::is_same_v<T, float>) return "f32";
}

const char* domainName(canon::AttributeDomain domain) {
    switch (domain) {
        case canon::AttributeDomain::Vertex: return "vertex";
        case canon::AttributeDomain::Face: return "face";
        case canon::AttributeDomain::Part: return "part";
    }
    throw std::logic_error("unreachable AttributeDomain");
}

std::vector<BufferSlice> appendAttributes(const std::vector<canon::AttributeChannel>& attributes,
                                          std::vector<uint8_t>& meshBin) {
    std::vector<BufferSlice> out;
    for (const canon::AttributeChannel& a : attributes) {
        out.push_back(std::visit(
            [&](const auto& values) {
                using T = typename std::decay_t<decltype(values)>::value_type;
                return appendComponents(meshBin, "mesh.bin", values, a.components, componentTypeName<T>());
            },
            a.values));
    }
    return out;
}

// `slices[i]` holds attributes[i]'s values (appendAttributes).
void writeAttributes(json::Writer& w, const std::vector<canon::AttributeChannel>& attributes,
                     const std::vector<BufferSlice>& slices) {
    w.beginArray();
    for (size_t i = 0; i < attributes.size(); ++i) {
        w.beginObject();
        w.key("name");
        w.value(attributes[i].name);
        w.key("domain");
        w.value(domainName(attributes[i].domain));
        w.key("data");
        writeBufferSlice(w, slices[i]);
        w.endObject();
    }
    w.endArray();
}

VertexSlices appendVertexStreams(const canon::Mesh& mesh, std::vector<uint8_t>& meshBin) {
    VertexSlices out;
    out.positions = appendArray(meshBin, "mesh.bin", mesh.positions, 3, "f32");
    out.normals = appendArray(meshBin, "mesh.bin", mesh.normals, 3, "f32");
    for (const auto& uv : mesh.uvSets) out.uvSets.push_back(appendArray(meshBin, "mesh.bin", uv, 2, "f32"));
    for (const auto& colors : mesh.colorSets) out.colorSets.push_back(appendArray(meshBin, "mesh.bin", colors, 4, "u8"));
    if (!mesh.skinning.empty()) {
        std::vector<std::array<uint8_t, 4>> joints;
        std::vector<std::array<float, 4>> weights;
        joints.reserve(mesh.skinning.size());
        weights.reserve(mesh.skinning.size());
        for (const auto& s : mesh.skinning) {
            joints.push_back(s.joints);
            weights.push_back(s.weights);
        }
        out.joints = appendArray(meshBin, "mesh.bin", joints, 4, "u8");
        out.weights = appendArray(meshBin, "mesh.bin", weights, 4, "f32");
    }
    return out;
}

void writeVertexSlices(json::Writer& w, const VertexSlices& v) {
    w.key("positions");
    writeBufferSlice(w, v.positions, "POSITION");
    w.key("normals");
    writeBufferSlice(w, v.normals, "NORMAL");
    w.key("uv_sets");
    w.beginArray();
    for (size_t k = 0; k < v.uvSets.size(); ++k) {
        std::string semantic = "TEXCOORD_" + std::to_string(k);
        writeBufferSlice(w, v.uvSets[k], semantic.c_str());
    }
    w.endArray();
    if (!v.colorSets.empty()) {
        w.key("colors");
        w.beginArray();
        for (size_t k = 0; k < v.colorSets.size(); ++k) {
            std::string semantic = "COLOR_" + std::to_string(k);
            writeBufferSlice(w, v.colorSets[k], semantic.c_str());
        }
        w.endArray();
    }
    if (v.joints) {
        w.key("joints0");
        writeBufferSlice(w, *v.joints, "JOINTS_0");
        w.key("weights0");
        writeBufferSlice(w, *v.weights, "WEIGHTS_0");
    }
}

// `materialOf(i)`: primitive i's index into model.materials, resolved
// through canon::resolveMaterialIndex or its level twin -- model.materials
// is deduped (canon_model.hpp), so it is never assumed to be `i`.
void writePrimitives(json::Writer& w, const std::vector<canon::PrimitiveGeoset>& primitives,
                     const std::function<std::optional<size_t>(size_t)>& materialOf) {
    w.beginArray();
    for (size_t i = 0; i < primitives.size(); ++i) {
        const canon::PrimitiveGeoset& prim = primitives[i];
        w.beginObject();
        w.key("geoset_id");
        w.value(static_cast<int64_t>(prim.geoset.group * 100 + prim.geoset.variant));
        w.key("geoset_group");
        w.value(static_cast<int64_t>(prim.geoset.group));
        w.key("geoset_variant");
        w.value(static_cast<int64_t>(prim.geoset.variant));
        w.key("index_start");
        w.value(static_cast<int64_t>(prim.indexStart));
        w.key("index_count");
        w.value(static_cast<int64_t>(prim.indexCount));
        // A nullopt here means the model holds a primitive material this
        // bundle format has no way to represent (a bundle-external material
        // reference, not implemented anywhere yet) -- an interior invariant
        // violation for this writer's own current scope, so it throws
        // rather than silently writing a wrong index (FOREIGN_DATA policy:
        // the interior trusts an already-validated canon::Model, it doesn't
        // limp past a shape it can't handle).
        auto matIdx = materialOf(i);
        if (!matIdx) {
            throw std::runtime_error("writeBundle: primitive " + std::to_string(i) +
                                      "'s material reference isn't a local RecordIndex -- bundle writer has no "
                                      "way to represent a bundle-external material reference yet");
        }
        w.key("material_index");
        w.value(static_cast<int64_t>(*matIdx));
        if (prim.part) {
            w.key("part");
            w.value(static_cast<int64_t>(*prim.part));
        }
        w.endObject();
    }
    w.endArray();
}

}  // namespace

MeshSlices writeMeshSection(json::Writer& w, const canon::Model& model, std::vector<uint8_t>& meshBin) {
    const canon::Mesh& mesh = model.mesh;
    w.beginObject();

    VertexSlices vertices = appendVertexStreams(mesh, meshBin);
    writeVertexSlices(w, vertices);

    BufferSlice indices = appendArray(meshBin, "mesh.bin", mesh.indices, 1, "u32");
    w.key("indices");
    writeBufferSlice(w, indices, "INDICES");

    w.key("primitives");
    writePrimitives(w, mesh.primitives, [&](size_t i) { return canon::resolveMaterialIndex(model, i); });

    w.key("parts");
    w.beginArray();
    for (const canon::MeshPart& part : mesh.parts) {
        w.beginObject();
        w.key("ref");
        writeRef(w, part.ref);
        w.key("bounds_min");
        writeVec3Value(w, part.boundsMin);
        w.key("bounds_max");
        writeVec3Value(w, part.boundsMax);
        for (auto [key, refs] : {std::pair{"fog_volumes", &part.fogs}, std::pair{"ambient_color_volumes", &part.ambientVolumes},
                                 std::pair{"ambient_particle_volumes", &part.particulateVolumes}}) {
            if (refs->empty()) continue;
            w.key(key);
            w.beginArray();
            for (const canon::InstanceRef& ref : *refs) writeInstanceRef(w, ref);
            w.endArray();
        }
        if (!part.lights.empty()) {
            w.key("lights");
            w.beginArray();
            for (uint32_t light : part.lights) w.value(static_cast<int64_t>(light));
            w.endArray();
        }
        if (!part.lightSets.empty()) {
            w.key("light_sets");
            w.beginArray();
            for (const std::vector<uint32_t>& set : part.lightSets) {
                w.beginArray();
                for (uint32_t light : set) w.value(static_cast<int64_t>(light));
                w.endArray();
            }
            w.endArray();
        }
        if (!part.portals.empty()) {
            w.key("portals");
            w.beginArray();
            for (const canon::PortalLink& link : part.portals) {
                w.beginObject();
                w.key("portal");
                w.value(static_cast<int64_t>(link.portal));
                w.key("to_part");
                w.value(static_cast<int64_t>(link.toPart));
                w.key("unresolved");
                w.beginObject();
                w.key("side");
                w.value(static_cast<int64_t>(link.side));
                w.endObject();
                w.endObject();
            }
            w.endArray();
        }
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(part.flags));
        w.endObject();
        w.endObject();
    }
    w.endArray();

    w.key("portals");
    w.beginArray();
    for (const canon::Portal& portal : mesh.portals) {
        w.beginObject();
        w.key("vertices");
        w.beginArray();
        for (const canon::Vec3& v : portal.vertices) writeVec3Value(w, v);
        w.endArray();
        w.key("normal");
        writeVec3Value(w, portal.normal);
        w.key("distance");
        w.value(static_cast<double>(portal.distance));
        if (!portal.sourceExtra.empty()) {
            w.key("unresolved");
            w.beginObject();
            w.key("extra_record");
            w.beginArray();
            for (uint8_t b : portal.sourceExtra) w.value(static_cast<int64_t>(b));
            w.endArray();
            w.endObject();
        }
        w.endObject();
    }
    w.endArray();

    w.key("attributes");
    std::vector<BufferSlice> attributes = appendAttributes(mesh.attributes, meshBin);
    writeAttributes(w, mesh.attributes, attributes);

    w.endObject();
    return {std::move(vertices), indices, std::move(attributes)};
}

void writeLodSection(json::Writer& w, const canon::Model& model, const MeshSlices& mesh0, std::vector<uint8_t>& meshBin) {
    w.beginObject();
    w.key("max_level");
    w.value(static_cast<int64_t>(model.meshLevels.size()));
    w.key("levels");
    w.beginArray();

    w.beginObject();
    writeVertexSlices(w, mesh0.vertices);
    w.key("indices");
    writeBufferSlice(w, mesh0.indices, "INDICES");
    w.key("primitives");
    writePrimitives(w, model.mesh.primitives, [&](size_t i) { return canon::resolveMaterialIndex(model, i); });
    w.key("attributes");
    writeAttributes(w, model.mesh.attributes, mesh0.attributes);
    w.endObject();

    for (size_t l = 0; l < model.meshLevels.size(); ++l) {
        const canon::MeshLevel& level = model.meshLevels[l];
        w.beginObject();
        writeVertexSlices(w, level.sharesVertices ? mesh0.vertices : appendVertexStreams(level.mesh, meshBin));
        w.key("indices");
        writeBufferSlice(w, appendArray(meshBin, "mesh.bin", level.mesh.indices, 1, "u32"), "INDICES");
        w.key("primitives");
        writePrimitives(w, level.mesh.primitives,
                        [&](size_t i) { return canon::resolveLevelMaterialIndex(model, l, i); });
        w.key("attributes");
        writeAttributes(w, level.mesh.attributes, appendAttributes(level.mesh.attributes, meshBin));
        w.key("source");
        w.beginObject();
        if (level.sourceFile) {
            w.key("file");
            writeRef(w, *level.sourceFile);
        }
        if (!level.partLevels.empty()) {
            w.key("part_levels");
            w.beginArray();
            for (const canon::PartLevel& p : level.partLevels) {
                w.beginObject();
                w.key("part");
                w.value(static_cast<int64_t>(p.part));
                w.key("level");
                w.value(static_cast<int64_t>(p.sourceLevel));
                w.endObject();
            }
            w.endArray();
        }
        w.endObject();
        w.endObject();
    }

    w.endArray();
    w.endObject();
}

void writeSkeletonSection(json::Writer& w, const canon::Skeleton& skeleton, std::vector<uint8_t>& skeletonBin) {
    w.beginObject();

    w.key("joint_count");
    w.value(static_cast<int64_t>(skeleton.joints.size()));

    std::vector<int32_t> parents;
    std::vector<canon::Vec3> bindTranslations;
    parents.reserve(skeleton.joints.size());
    bindTranslations.reserve(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        parents.push_back(skeleton.joints[i].parent);
        bindTranslations.push_back(localBindTranslation(skeleton, i));
    }

    w.key("parents");
    writeBufferSlice(w, appendArray(skeletonBin, "skeleton.bin", parents, 1, "i32"));
    w.key("bind_translation");
    writeBufferSlice(w, appendArray(skeletonBin, "skeleton.bin", bindTranslations, 3, "f32"));

    w.key("joints");
    w.beginArray();
    for (const canon::Joint& joint : skeleton.joints) {
        w.beginObject();
        w.key("billboard");
        w.value(billboardName(joint.billboard));
        w.key("ref");
        writeRef(w, joint.ref);
        w.key("structural_label");
        w.value(joint.structuralLabel);
        w.endObject();
    }
    w.endArray();

    w.endObject();
}

namespace {

// One channel (translation/rotation/scale) of one joint's ComposedJointCurves.
template <typename T>
void writeAnimChannel(json::Writer& w, const canon::Curve<T>& curve, std::vector<uint8_t>& animBin) {
    std::vector<float> times;
    std::vector<T> values;
    times.reserve(curve.keyframes.size());
    values.reserve(curve.keyframes.size());
    for (const auto& [t, v] : curve.keyframes) {
        times.push_back(t);
        values.push_back(v);
    }

    uint32_t componentCount;
    if constexpr (std::is_same_v<T, canon::Vec3>) {
        componentCount = 3;
    } else if constexpr (std::is_same_v<T, canon::Quat>) {
        componentCount = 4;
    } else {
        componentCount = 1;
    }

    w.beginObject();
    w.key("times");
    writeBufferSlice(w, appendArray(animBin, "animation.bin", times, 1, "f32"));
    w.key("values");
    writeBufferSlice(w, appendArray(animBin, "animation.bin", values, componentCount, "f32"));
    w.key("interpolation");
    w.value(interpolationName(curve.interpolation));
    w.endObject();
}

}  // namespace

void writeAnimationSection(json::Writer& w, const canon::Skeleton& skeleton,
                            const std::vector<canon::AnimationClip>& clips, std::vector<uint8_t>& animBin) {
    w.beginArray();
    for (const canon::AnimationClip& clip : clips) {
        w.beginObject();
        w.key("sequence_index");
        w.value(static_cast<int64_t>(clip.sequence.index));
        w.key("sequence_kind");
        w.value(sequenceKindName(clip.sequence.kind));

        w.key("joints");
        w.beginArray();
        for (size_t jointIndex = 0; jointIndex < clip.boneCurves.size(); ++jointIndex) {
            std::optional<ComposedJointCurves> composed = composeJointCurves(skeleton, clip, jointIndex);
            if (!composed) continue;  // sparse: only real curve data gets an entry

            w.beginObject();
            w.key("joint_index");
            w.value(static_cast<int64_t>(jointIndex));
            w.key("translation");
            writeAnimChannel(w, composed->translation, animBin);
            w.key("rotation");
            writeAnimChannel(w, composed->rotation, animBin);
            w.key("scale");
            writeAnimChannel(w, composed->scale, animBin);
            w.endObject();
        }
        w.endArray();

        w.endObject();
    }
    w.endArray();
}

void writeLiquids(json::Writer& w, const canon::Model& model, std::vector<uint8_t>& liquidBin) {
    w.beginArray();
    for (const canon::LiquidGrid& l : model.liquids) {
        w.beginObject();
        writeIntField(w, "part", l.part);
        w.key("material_index");
        w.value(static_cast<int64_t>(std::get<canon::RecordIndex>(l.material).value));
        w.key("liquid_type");
        writeRef(w, l.liquidType);
        writeVec3Field(w, "corner", l.corner);
        writeFloatField(w, "tile_size", l.tileSize);
        writeIntField(w, "verts_x", l.vertsX);
        writeIntField(w, "verts_y", l.vertsY);
        w.key("heights");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.heights, 1, "f32"));
        w.key("tile_exists");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.tileExists, 1, "u8"));
        w.key("source");
        w.beginObject();
        writeIntField(w, "liquid_type", l.sourceLiquidType);
        w.key("vertex_data");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.vertexData, 4, "u8"));
        w.key("tile_flags");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.tileFlags, 1, "u8"));
        w.endObject();
        w.endObject();
    }
    w.endArray();
}

void writeCollision(json::Writer& w, const canon::CollisionMesh& c, std::vector<uint8_t>& collisionBin) {
    constexpr const char* kBin = "collision.bin";
    w.beginObject();
    w.key("positions");
    writeBufferSlice(w, appendArray(collisionBin, kBin, c.positions, 3, "f32"));
    w.key("indices");
    writeBufferSlice(w, appendArray(collisionBin, kBin, c.indices, 1, "u32"));
    if (!c.triangleNormals.empty()) {
        w.key("triangle_normals");
        writeBufferSlice(w, appendArray(collisionBin, kBin, c.triangleNormals, 3, "f32"));
    }
    w.endObject();
}

}  // namespace husk::writers
