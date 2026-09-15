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
#include "writers/writer_common.hpp"

namespace husk::writers {

namespace {

// One named byte range inside a .bin file -- the manifest's BufferSlice
// shape (bundle_writer.hpp's own doc comment is the canonical description;
// this struct is that shape's in-memory twin, not a second definition of
// it).
struct BufferSlice {
    std::string file;
    size_t byteOffset = 0;
    size_t byteLength = 0;
    std::string componentType;
    uint32_t componentCount = 0;
    size_t count = 0;
};

// The one place that appends a typed array to a byte buffer and hands back
// the {offset, length, type, count} facts needed to describe it -- every
// BufferSlice below is produced by this, never by hand-computed offsets,
// so a slice's recorded range and its actual bytes cannot drift apart.
template <typename Elem>
BufferSlice appendArray(std::vector<uint8_t>& buf, std::string filename, const Elem* data, size_t count,
                         uint32_t componentCount, std::string componentType) {
    size_t byteOffset = buf.size();
    size_t byteLength = count * sizeof(Elem);
    if (byteLength > 0) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(data);
        buf.insert(buf.end(), bytes, bytes + byteLength);
    }
    return BufferSlice{std::move(filename), byteOffset, byteLength, std::move(componentType), componentCount,
                        count};
}

template <typename Elem>
BufferSlice appendArray(std::vector<uint8_t>& buf, std::string filename, const std::vector<Elem>& data,
                         uint32_t componentCount, std::string componentType) {
    return appendArray(buf, std::move(filename), data.data(), data.size(), componentCount,
                        std::move(componentType));
}

void writeBufferSlice(json::Writer& w, const BufferSlice& slice, const char* semantic = nullptr) {
    w.beginObject();
    w.key("file");
    w.value(slice.file);
    w.key("byte_offset");
    w.value(static_cast<int64_t>(slice.byteOffset));
    w.key("byte_length");
    w.value(static_cast<int64_t>(slice.byteLength));
    w.key("component_type");
    w.value(slice.componentType);
    w.key("component_count");
    w.value(static_cast<int64_t>(slice.componentCount));
    w.key("count");
    w.value(static_cast<int64_t>(slice.count));
    if (semantic != nullptr) {
        w.key("semantic");
        w.value(semantic);
    }
    w.endObject();
}

std::string nameSourceName(canon::NameSource source) {
    switch (source) {
        case canon::NameSource::M2Embedded: return "m2_embedded";
        case canon::NameSource::Listfile: return "listfile";
        case canon::NameSource::Db2: return "db2";
        case canon::NameSource::Synthesized: return "synthesized";
        case canon::NameSource::None: return "none";
    }
    return "none";
}

// One shared serializer for canon::Ref's Identity variant -- every call
// site below (bone/geoset/material-layer/texture identities) shares this
// rather than re-visiting the variant per site (this file's own brief).
// `uri`, when non-empty, adds BUNDLE_FORMAT.md's "Embed or reference"
// field -- only the texture case populates it today (writeTextureRef
// below); every other Ref user passes the default (identity only, no
// payload location to name yet).
void writeRef(json::Writer& w, const canon::Ref& ref, const std::string& uri = "") {
    w.beginObject();
    w.key("id");
    w.beginObject();
    std::visit(
        [&w](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, canon::None>) {
                w.key("kind");
                w.value("none");
            } else if constexpr (std::is_same_v<T, canon::FileDataId>) {
                w.key("kind");
                w.value("file_data_id");
                w.key("value");
                w.value(static_cast<int64_t>(id.value));
            } else if constexpr (std::is_same_v<T, canon::Db2Row>) {
                w.key("kind");
                w.value("db2_row");
                w.key("table");
                w.value(id.table);
                w.key("row");
                w.value(static_cast<int64_t>(id.row));
            } else if constexpr (std::is_same_v<T, canon::RecordIndex>) {
                w.key("kind");
                w.value("record_index");
                w.key("value");
                w.value(static_cast<int64_t>(id.value));
            }
        },
        ref.id);
    w.endObject();
    w.key("name");
    w.value(ref.name);
    w.key("name_source");
    w.value(nameSourceName(ref.source));
    if (!uri.empty()) {
        w.key("uri");
        w.value(uri);
    }
    w.endObject();
}

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

std::string isoTimestampUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("bundle writer: could not open " + path.string() + " for writing");
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
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

// BUNDLE_FORMAT.md's settled texture-encoding vocabulary, extension half --
// only Png is reachable today (canon::TextureEncoding's own doc comment:
// every real producer still decodes BLP -> PNG before canon:: ever sees
// the bytes), the rest exist so this switch doesn't need revisiting once
// that changes.
std::string textureFileExtension(canon::TextureEncoding encoding) {
    switch (encoding) {
        case canon::TextureEncoding::Png: return "png";
        case canon::TextureEncoding::Bc1:
        case canon::TextureEncoding::Bc2:
        case canon::TextureEncoding::Bc3:
        case canon::TextureEncoding::Bgra:
        case canon::TextureEncoding::Palettized:
            return "dds";  // BUNDLE_FORMAT.md's "Texture encoding -- settled": DDS houses the compressed blocks verbatim
    }
    return "png";
}

// Writes `texture`'s resolved payload (when it carries one, AUDIT.md §7.4)
// to `bundleDir/textures/<name>.<ext>` and records a real `uri` alongside
// its identity -- BUNDLE_FORMAT.md's "Embed or reference" shape. A
// Resolved identity with no payload (a future reference-only producer)
// writes `id`/`name`/`name_source` with no `uri`, the documented "husk
// knows what this is" case, not an error. `writtenTextures` dedupes by
// filename across the whole bundle (more than one material layer can
// resolve to the same real texture) so repeated identical bytes are
// written to disk once, not once per referencing layer.
//
// `rawPayload` (the DDS-housed source blocks, BUNDLE_FORMAT.md's "Texture
// encoding -- settled") wins over `payload` (the PNG projection) when both
// are present: this bundle is exactly the archival/engine-facing format
// that section says should carry the lossless source payload, not the
// glTF-facing PNG. `payload` stays as the fallback so a texture whose
// source was never a compressed BLP in the first place (already a `.png`
// on disk, or DDS extraction declined for a Palette-encoded source) still
// gets a real file in the bundle instead of being silently dropped just
// because the DDS variant wasn't available for it.
void writeTextureRef(json::Writer& w, const canon::TextureRef& texture, const std::filesystem::path& bundleDir,
                      std::unordered_set<std::string>& writtenTextures) {
    switch (texture.state) {
        case canon::TextureRef::State::Resolved: {
            w.key("texture_state");
            w.value("resolved");
            w.key("texture");
            std::string uri;
            const canon::TextureRef::Payload* chosen =
                texture.rawPayload ? &*texture.rawPayload : (texture.payload ? &*texture.payload : nullptr);
            if (chosen) {
                std::string stem = texture.resolved.name;
                if (stem.empty()) {
                    if (auto fdid = std::get_if<canon::FileDataId>(&texture.resolved.id)) {
                        stem = std::to_string(fdid->value);
                    } else {
                        stem = "texture";
                    }
                }
                std::string filename = stem + "." + textureFileExtension(chosen->encoding);
                uri = "textures/" + filename;
                if (writtenTextures.insert(filename).second) {
                    std::filesystem::path texturesDir = bundleDir / "textures";
                    std::error_code ec;
                    std::filesystem::create_directories(texturesDir, ec);
                    if (ec) {
                        throw std::runtime_error("bundle writer: could not create directory " +
                                                  texturesDir.string() + ": " + ec.message());
                    }
                    writeFile(texturesDir / filename, chosen->bytes);
                }
            }
            writeRef(w, texture.resolved, uri);
            break;
        }
        case canon::TextureRef::State::KnownUnresolved:
            w.key("texture_state");
            w.value("known_unresolved");
            w.key("unresolved_reason");
            w.value(texture.unresolvedReason);
            break;
        case canon::TextureRef::State::Ambiguous:
            w.key("texture_state");
            w.value("ambiguous");
            w.key("candidates");
            w.beginArray();
            for (const auto& candidate : texture.candidates) {
                w.beginObject();
                w.key("identity");
                writeRef(w, candidate.identity);
                w.key("category");
                w.value(candidate.category);
                w.key("width");
                w.value(static_cast<int64_t>(candidate.width));
                w.key("height");
                w.value(static_cast<int64_t>(candidate.height));
                w.endObject();
            }
            w.endArray();
            break;
    }
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
