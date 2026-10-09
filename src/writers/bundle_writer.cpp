#include "writers/bundle_writer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <variant>

#include "json_writer.hpp"
#include "writers/bundle_common.hpp"
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

std::string sequenceKindName(canon::SequenceRef::Kind kind) {
    return kind == canon::SequenceRef::Kind::GlobalSequence ? "global_sequence" : "sequence";
}

std::string interpolationName(canon::Interpolation interp) {
    return interp == canon::Interpolation::Step ? "step" : "linear";
}

std::string framebufferBlendName(canon::FramebufferBlend blend) {
    switch (blend) {
        case canon::FramebufferBlend::Opaque: return "opaque";
        case canon::FramebufferBlend::AlphaKey: return "alpha_key";
        case canon::FramebufferBlend::Alpha: return "alpha";
        case canon::FramebufferBlend::NoAlphaAdd: return "no_alpha_add";
        case canon::FramebufferBlend::Add: return "add";
        case canon::FramebufferBlend::Mod: return "mod";
        case canon::FramebufferBlend::Mod2x: return "mod2x";
        case canon::FramebufferBlend::BlendAdd: return "blend_add";
    }
    return "opaque";
}

std::string blendOpName(canon::BlendOp op) {
    switch (op) {
        case canon::BlendOp::Modulate: return "modulate";
        case canon::BlendOp::Modulate2x: return "modulate_2x";
        case canon::BlendOp::Add: return "add";
        case canon::BlendOp::Replace: return "replace";
        case canon::BlendOp::Decal: return "decal";
        case canon::BlendOp::Fade: return "fade";
    }
    return "modulate";
}

std::string knownRoleName(canon::KnownRole role) {
    switch (role) {
        case canon::KnownRole::Unknown: return "unknown";
        case canon::KnownRole::Diffuse: return "diffuse";
        case canon::KnownRole::Specular: return "specular";
        case canon::KnownRole::Emission: return "emission";
        case canon::KnownRole::Alpha: return "alpha";
        case canon::KnownRole::Detail: return "detail";
        case canon::KnownRole::Env: return "env";
    }
    return "unknown";
}

void writeLayerRole(json::Writer& w, const canon::LayerRole& role) {
    std::visit(
        [&w](const auto& r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, canon::KnownRole>) {
                w.value(knownRoleName(r));
            } else {
                w.value(r);
            }
        },
        role);
}

void writeUvRef(json::Writer& w, const canon::UvRef& uv) {
    std::visit(
        [&w](const auto& u) {
            using T = std::decay_t<decltype(u)>;
            if constexpr (std::is_same_v<T, canon::UvSetIndex>) {
                w.value("uv_set_" + std::to_string(u.index));
            } else {
                w.value("environment_mapped");
            }
        },
        uv);
}

void writeVec3Value(json::Writer& w, const canon::Vec3& v) {
    w.beginArray();
    w.value(static_cast<double>(v.x));
    w.value(static_cast<double>(v.y));
    w.value(static_cast<double>(v.z));
    w.endArray();
}

void writeQuatValue(json::Writer& w, const canon::Quat& q) {
    w.beginArray();
    w.value(static_cast<double>(q.x));
    w.value(static_cast<double>(q.y));
    w.value(static_cast<double>(q.z));
    w.value(static_cast<double>(q.w));
    w.endArray();
}

void writeScalarValue(json::Writer& w, float v) { w.value(static_cast<double>(v)); }

// Shared shape for tint/alpha_fade/uv_animation's three sub-curves -- small,
// per-material data, deliberately inline rather than BufferSlice (see
// bundle_writer.hpp's "inline curve" doc). `writeValue` writes one
// keyframe's value in whatever shape T needs (plain number for float, a
// JSON array for Vec3/Quat).
template <typename T, typename ValueWriter>
void writeInlineCurve(json::Writer& w, const canon::Curve<T>& curve, ValueWriter writeValue) {
    w.beginObject();
    w.key("sequence_index");
    w.value(static_cast<int64_t>(curve.sequence.index));
    w.key("sequence_kind");
    w.value(sequenceKindName(curve.sequence.kind));
    w.key("interpolation");
    w.value(interpolationName(curve.interpolation));
    w.key("keyframes");
    w.beginArray();
    for (const auto& [t, v] : curve.keyframes) {
        w.beginArray();
        w.value(static_cast<double>(t));
        writeValue(w, v);
        w.endArray();
    }
    w.endArray();
    w.endObject();
}

// resources.mesh -- appends every mesh BufferSlice to `meshBin` and writes
// the section (including the "primitives" inline array) to `w`. Takes the
// whole `model`, not just `model.mesh`, so each primitive's own
// "material_index" can be resolved via canon::resolveMaterialIndex (I2) --
// `model.materials` is deduped and no longer 1:1 with `model.mesh
// .primitives`, so a primitive's material must be looked up through
// `model.primitiveMaterials`, not assumed to share its own index.
void writeMeshSection(json::Writer& w, const canon::Model& model, std::vector<uint8_t>& meshBin) {
    const canon::Mesh& mesh = model.mesh;
    w.beginObject();

    w.key("positions");
    writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.positions, 3, "f32"), "POSITION");

    w.key("normals");
    writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.normals, 3, "f32"), "NORMAL");

    w.key("uv0");
    writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.uv0, 2, "f32"), "TEXCOORD_0");

    if (mesh.uv1) {
        w.key("uv1");
        writeBufferSlice(w, appendArray(meshBin, "mesh.bin", *mesh.uv1, 2, "f32"), "TEXCOORD_1");
    }

    if (!mesh.skinning.empty()) {
        std::vector<std::array<uint8_t, 4>> joints;
        std::vector<std::array<float, 4>> weights;
        joints.reserve(mesh.skinning.size());
        weights.reserve(mesh.skinning.size());
        for (const auto& s : mesh.skinning) {
            joints.push_back(s.joints);
            weights.push_back(s.weights);
        }
        w.key("joints0");
        writeBufferSlice(w, appendArray(meshBin, "mesh.bin", joints, 4, "u8"), "JOINTS_0");
        w.key("weights0");
        writeBufferSlice(w, appendArray(meshBin, "mesh.bin", weights, 4, "f32"), "WEIGHTS_0");
    }

    w.key("indices");
    writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.indices, 1, "u32"), "INDICES");

    w.key("primitives");
    w.beginArray();
    for (size_t i = 0; i < mesh.primitives.size(); ++i) {
        const canon::PrimitiveGeoset& prim = mesh.primitives[i];
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
        // Resolved via canon::resolveMaterialIndex, not assumed to equal
        // `i` -- model.materials is deduped (canon_model.hpp's own doc
        // comment); see this function's own doc comment above and
        // tests/test_writers_bundle.cpp's explicit regression test. A
        // nullopt here means `model` holds a `primitiveMaterials` entry
        // this bundle format has no way to represent (a bundle-external
        // material reference, not implemented anywhere yet) -- an interior
        // invariant violation for this writer's own current scope, so it
        // throws rather than silently writing a wrong index (FOREIGN_DATA
        // policy: the interior trusts an already-validated canon::Model,
        // it doesn't limp past a shape it can't handle).
        auto matIdx = canon::resolveMaterialIndex(model, i);
        if (!matIdx) {
            throw std::runtime_error("writeBundle: primitive " + std::to_string(i) +
                                      "'s material reference isn't a local RecordIndex -- bundle writer has no "
                                      "way to represent a bundle-external material reference yet");
        }
        w.key("material_index");
        w.value(static_cast<int64_t>(*matIdx));
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

void writeMaterialLayer(json::Writer& w, const canon::MaterialLayer& layer, const std::filesystem::path& bundleDir,
                         std::unordered_set<std::string>& writtenTextures) {
    w.beginObject();
    w.key("identity");
    writeRef(w, layer.identity);
    w.key("role");
    writeLayerRole(w, layer.role);
    w.key("uv");
    writeUvRef(w, layer.uv);
    writeTextureRef(w, layer.texture, bundleDir, writtenTextures);
    w.key("blend");
    w.value(blendOpName(layer.blendIntoPrevious));

    if (layer.tint) {
        w.key("tint");
        writeInlineCurve(w, *layer.tint, writeVec3Value);
    }
    if (layer.alphaFade) {
        w.key("alpha_fade");
        writeInlineCurve(w, *layer.alphaFade, writeScalarValue);
    }
    if (layer.uvAnimation) {
        w.key("uv_animation");
        w.beginObject();
        if (layer.uvAnimation->translation) {
            w.key("translation");
            writeInlineCurve(w, *layer.uvAnimation->translation, writeVec3Value);
        }
        if (layer.uvAnimation->rotation) {
            w.key("rotation");
            writeInlineCurve(w, *layer.uvAnimation->rotation, writeQuatValue);
        }
        if (layer.uvAnimation->scaling) {
            w.key("scaling");
            writeInlineCurve(w, *layer.uvAnimation->scaling, writeVec3Value);
        }
        w.endObject();
    }

    w.endObject();
}

void writeMaterialsSection(json::Writer& w, const std::vector<canon::Material>& materials,
                            const std::filesystem::path& bundleDir,
                            std::unordered_set<std::string>& writtenTextures) {
    w.beginArray();
    for (const canon::Material& material : materials) {
        w.beginObject();

        w.key("layers");
        w.beginArray();
        for (const canon::MaterialLayer& layer : material.layers) {
            writeMaterialLayer(w, layer, bundleDir, writtenTextures);
        }
        w.endArray();

        if (material.framebufferBlend) {
            w.key("framebuffer_blend");
            w.value(framebufferBlendName(*material.framebufferBlend));
        }
        if (material.diffuseLayer) {
            w.key("diffuse_layer");
            writeRef(w, *material.diffuseLayer);
        }
        if (material.specularLayer) {
            w.key("specular_layer");
            writeRef(w, *material.specularLayer);
        }
        if (material.emissionLayer) {
            w.key("emission_layer");
            writeRef(w, *material.emissionLayer);
        }
        if (material.alphaLayer) {
            w.key("alpha_layer");
            writeRef(w, *material.alphaLayer);
        }

        w.endObject();
    }
    w.endArray();
}

void writeVec2Value(json::Writer& w, const canon::Vec2& v) {
    w.beginArray();
    w.value(static_cast<double>(v.x));
    w.value(static_cast<double>(v.y));
    w.endArray();
}

template <typename T, typename ValueWriter>
void writeAnimated(json::Writer& w, const char* key, const canon::Animated<T>& curves, ValueWriter writeValue) {
    w.key(key);
    w.beginArray();
    for (const canon::Curve<T>& curve : curves) writeInlineCurve(w, curve, writeValue);
    w.endArray();
}

template <typename T, typename ValueWriter>
void writeLifetimeCurve(json::Writer& w, const char* key, const canon::LifetimeCurve<T>& curve,
                        ValueWriter writeValue) {
    w.key(key);
    w.beginArray();
    for (const auto& [timestamp, value] : curve.keyframes) {
        w.beginArray();
        w.value(static_cast<int64_t>(timestamp));
        writeValue(w, value);
        w.endArray();
    }
    w.endArray();
}

void writeFloatField(json::Writer& w, const char* key, float v) {
    w.key(key);
    w.value(static_cast<double>(v));
}

void writeIntField(json::Writer& w, const char* key, int64_t v) {
    w.key(key);
    w.value(v);
}

void writeVec3Field(json::Writer& w, const char* key, const canon::Vec3& v) {
    w.key(key);
    writeVec3Value(w, v);
}

void writeFloatArrayField(json::Writer& w, const char* key, const float* values, size_t count) {
    w.key(key);
    w.beginArray();
    for (size_t i = 0; i < count; ++i) w.value(static_cast<double>(values[i]));
    w.endArray();
}

void writeTextureList(json::Writer& w, const std::vector<canon::TextureRef>& textures,
                      const std::filesystem::path& bundleDir, std::unordered_set<std::string>& writtenTextures) {
    w.key("textures");
    w.beginArray();
    for (const canon::TextureRef& texture : textures) {
        w.beginObject();
        writeTextureRef(w, texture, bundleDir, writtenTextures);
        w.endObject();
    }
    w.endArray();
}

void writeAttachments(json::Writer& w, const std::vector<canon::Attachment>& attachments) {
    w.beginArray();
    for (const canon::Attachment& a : attachments) {
        w.beginObject();
        w.key("ref");
        writeRef(w, a.ref);
        writeIntField(w, "point_id", a.pointId);
        w.key("bone");
        writeRef(w, a.bone);
        writeVec3Field(w, "position", a.position);
        writeAnimated(w, "animate_attached", a.animateAttached, writeScalarValue);
        w.endObject();
    }
    w.endArray();
}

void writeEvents(json::Writer& w, const std::vector<canon::Event>& events) {
    w.beginArray();
    for (const canon::Event& e : events) {
        w.beginObject();
        w.key("identifier");
        w.value(e.identifier);
        writeIntField(w, "data", e.data);
        w.key("bone");
        writeRef(w, e.bone);
        writeVec3Field(w, "position", e.position);
        w.endObject();
    }
    w.endArray();
}

void writeLights(json::Writer& w, const std::vector<canon::Light>& lights) {
    w.beginArray();
    for (const canon::Light& l : lights) {
        w.beginObject();
        writeIntField(w, "type", l.type);
        if (l.bone) {
            w.key("bone");
            writeRef(w, *l.bone);
        }
        writeVec3Field(w, "position", l.position);
        writeAnimated(w, "ambient_color", l.ambientColor, writeVec3Value);
        writeAnimated(w, "ambient_intensity", l.ambientIntensity, writeScalarValue);
        writeAnimated(w, "diffuse_color", l.diffuseColor, writeVec3Value);
        writeAnimated(w, "diffuse_intensity", l.diffuseIntensity, writeScalarValue);
        writeAnimated(w, "attenuation_start", l.attenuationStart, writeScalarValue);
        writeAnimated(w, "attenuation_end", l.attenuationEnd, writeScalarValue);
        writeAnimated(w, "visibility", l.visibility, writeScalarValue);
        w.endObject();
    }
    w.endArray();
}

void writeRibbons(json::Writer& w, const std::vector<canon::RibbonEmitter>& ribbons,
                  const std::filesystem::path& bundleDir, std::unordered_set<std::string>& writtenTextures) {
    w.beginArray();
    for (const canon::RibbonEmitter& r : ribbons) {
        w.beginObject();
        writeIntField(w, "ribbon_id", r.ribbonId);
        w.key("bone");
        writeRef(w, r.bone);
        writeVec3Field(w, "position", r.position);
        writeTextureList(w, r.textures, bundleDir, writtenTextures);
        w.key("materials");
        w.beginArray();
        for (const canon::RenderState& state : r.materials) {
            w.beginObject();
            writeIntField(w, "flags", state.flags);
            if (state.blend) {
                w.key("framebuffer_blend");
                w.value(framebufferBlendName(*state.blend));
            }
            w.endObject();
        }
        w.endArray();
        writeAnimated(w, "color", r.color, writeVec3Value);
        writeAnimated(w, "alpha", r.alpha, writeScalarValue);
        writeAnimated(w, "height_above", r.heightAbove, writeScalarValue);
        writeAnimated(w, "height_below", r.heightBelow, writeScalarValue);
        writeAnimated(w, "texture_slot", r.textureSlot, writeScalarValue);
        writeAnimated(w, "visibility", r.visibility, writeScalarValue);
        writeFloatField(w, "edges_per_second", r.edgesPerSecond);
        writeFloatField(w, "edge_lifetime", r.edgeLifetime);
        writeFloatField(w, "gravity", r.gravity);
        writeIntField(w, "texture_rows", r.textureRows);
        writeIntField(w, "texture_columns", r.textureColumns);
        writeIntField(w, "priority_plane", r.priorityPlane);
        writeIntField(w, "ribbon_color_index", r.ribbonColorIndex);
        writeIntField(w, "texture_transform_lookup_index", r.textureTransformLookupIndex);
        w.endObject();
    }
    w.endArray();
}

void writeParticles(json::Writer& w, const std::vector<canon::ParticleEmitter>& particles,
                    const std::filesystem::path& bundleDir, std::unordered_set<std::string>& writtenTextures) {
    auto writeUint16 = [](json::Writer& w2, uint16_t v) { w2.value(static_cast<int64_t>(v)); };
    w.beginArray();
    for (const canon::ParticleEmitter& p : particles) {
        w.beginObject();
        writeIntField(w, "particle_id", p.particleId);
        writeIntField(w, "flags", p.flags);
        w.key("bone");
        writeRef(w, p.bone);
        writeVec3Field(w, "position", p.position);
        writeTextureList(w, p.textures, bundleDir, writtenTextures);
        if (p.particleModel) {
            w.key("particle_model");
            writeRef(w, *p.particleModel);
        }
        if (p.childEmittersModel) {
            w.key("child_emitters_model");
            writeRef(w, *p.childEmittersModel);
        }
        writeIntField(w, "blending_type", p.blendingType);
        writeIntField(w, "emitter_type", p.emitterType);
        writeIntField(w, "particle_color_index", p.particleColorIndex);
        writeFloatArrayField(w, "multi_texture_scale", p.multiTextureScale, 2);
        writeIntField(w, "priority_plane", p.priorityPlane);
        writeIntField(w, "texture_rows", p.textureRows);
        writeIntField(w, "texture_columns", p.textureColumns);

        writeAnimated(w, "emission_speed", p.emissionSpeed, writeScalarValue);
        writeAnimated(w, "speed_variation", p.speedVariation, writeScalarValue);
        writeAnimated(w, "vertical_range", p.verticalRange, writeScalarValue);
        writeAnimated(w, "horizontal_range", p.horizontalRange, writeScalarValue);
        writeAnimated(w, "gravity", p.gravity, writeScalarValue);
        writeAnimated(w, "lifespan", p.lifespan, writeScalarValue);
        writeAnimated(w, "emission_rate", p.emissionRate, writeScalarValue);
        writeAnimated(w, "emission_area_length", p.emissionAreaLength, writeScalarValue);
        writeAnimated(w, "emission_area_width", p.emissionAreaWidth, writeScalarValue);
        writeAnimated(w, "z_source", p.zSource, writeScalarValue);
        writeAnimated(w, "enabled_in", p.enabledIn, writeScalarValue);
        writeFloatField(w, "lifespan_variation", p.lifespanVariation);
        writeFloatField(w, "emission_rate_variation", p.emissionRateVariation);

        writeLifetimeCurve(w, "color", p.color, writeVec3Value);
        writeLifetimeCurve(w, "alpha", p.alpha, writeScalarValue);
        writeLifetimeCurve(w, "scale", p.scale, writeVec2Value);
        writeLifetimeCurve(w, "head_cell", p.headCell, writeUint16);
        writeLifetimeCurve(w, "tail_cell", p.tailCell, writeUint16);
        w.key("scale_variation");
        writeVec2Value(w, p.scaleVariation);

        writeFloatField(w, "tail_length", p.tailLength);
        writeFloatField(w, "twinkle_speed", p.twinkleSpeed);
        writeFloatField(w, "twinkle_percent", p.twinklePercent);
        writeFloatField(w, "twinkle_scale_min", p.twinkleScaleMin);
        writeFloatField(w, "twinkle_scale_max", p.twinkleScaleMax);
        writeFloatField(w, "inherit_velocity_scale", p.inheritVelocityScale);
        writeFloatField(w, "drag", p.drag);
        writeFloatField(w, "base_spin", p.baseSpin);
        writeFloatField(w, "base_spin_variation", p.baseSpinVariation);
        writeFloatField(w, "spin_speed", p.spinSpeed);
        writeFloatField(w, "spin_speed_variation", p.spinSpeedVariation);
        writeVec3Field(w, "tumble_min", p.tumbleMin);
        writeVec3Field(w, "tumble_max", p.tumbleMax);
        writeVec3Field(w, "wind_vector", p.windVector);
        writeFloatField(w, "wind_time", p.windTime);
        writeFloatField(w, "follow_speed1", p.followSpeed1);
        writeFloatField(w, "follow_scale1", p.followScale1);
        writeFloatField(w, "follow_speed2", p.followSpeed2);
        writeFloatField(w, "follow_scale2", p.followScale2);
        w.key("spline_points");
        w.beginArray();
        for (const canon::Vec3& point : p.splinePoints) writeVec3Value(w, point);
        w.endArray();
        writeFloatArrayField(w, "multi_texture_scroll_mid", p.multiTextureScrollMid, 4);
        writeFloatArrayField(w, "multi_texture_scroll_range", p.multiTextureScrollRange, 4);
        w.endObject();
    }
    w.endArray();
}

}  // namespace

void writeBundle(const canon::Model& model, const std::filesystem::path& bundleDir,
                  const std::string& producer) {
    std::error_code ec;
    std::filesystem::create_directories(bundleDir, ec);
    if (ec) {
        throw std::runtime_error("bundle writer: could not create directory " + bundleDir.string() + ": " +
                                  ec.message());
    }

    std::vector<uint8_t> meshBin;
    std::vector<uint8_t> skeletonBin;
    std::vector<uint8_t> animBin;

    std::ostringstream manifestStream;
    json::Writer w(manifestStream);

    w.beginObject();
    w.key("schema_version");
    w.value("0.1.0");
    w.key("exported_at");
    w.value(isoTimestampUtc());
    w.key("producer");
    w.value(producer);
    w.key("endianness");
    w.value("little");

    w.key("resources");
    w.beginObject();

    w.key("mesh");
    writeMeshSection(w, model, meshBin);

    w.key("skeleton");
    writeSkeletonSection(w, model.skeleton, skeletonBin);

    w.key("animation");
    writeAnimationSection(w, model.skeleton, model.animations, animBin);

    w.key("materials");
    std::unordered_set<std::string> writtenTextures;
    writeMaterialsSection(w, model.materials, bundleDir, writtenTextures);

    w.key("attachments");
    writeAttachments(w, model.scene.attachments);
    w.key("events");
    writeEvents(w, model.scene.events);
    w.key("lights");
    writeLights(w, model.scene.lights);
    w.key("emitters");
    w.beginObject();
    w.key("ribbons");
    writeRibbons(w, model.scene.ribbons, bundleDir, writtenTextures);
    w.key("particles");
    writeParticles(w, model.scene.particles, bundleDir, writtenTextures);
    w.endObject();

    w.endObject();  // resources
    w.endObject();  // root

    writeFile(bundleDir / "mesh.bin", meshBin);
    writeFile(bundleDir / "skeleton.bin", skeletonBin);
    writeFile(bundleDir / "animation.bin", animBin);

    std::ofstream manifestFile(bundleDir / "manifest.json", std::ios::binary);
    if (!manifestFile) {
        throw std::runtime_error("bundle writer: could not open manifest.json for writing in " +
                                  bundleDir.string());
    }
    manifestFile << manifestStream.str();
}

}  // namespace husk::writers
