#include "writers/bundle_common.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <nlohmann/json.hpp>

#include "bundle_schema_embedded.hpp"

namespace husk::writers {

namespace {

std::string nameSourceName(canon::NameSource source) {
    switch (source) {
        case canon::NameSource::M2Embedded: return "m2_embedded";
        case canon::NameSource::AdtEmbedded: return "adt_embedded";
        case canon::NameSource::WmoEmbedded: return "wmo_embedded";
        case canon::NameSource::Listfile: return "listfile";
        case canon::NameSource::Db2: return "db2";
        case canon::NameSource::Synthesized: return "synthesized";
        case canon::NameSource::None: return "none";
    }
    return "none";
}

}  // namespace

void writeBufferSlice(json::Writer& w, const BufferSlice& slice, const char* semantic) {
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

void writeIdentity(json::Writer& w, const canon::Identity& identity) {
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
        identity);
    w.endObject();
}

void writeName(json::Writer& w, const canon::Ref& ref) {
    if (ref.name.empty()) return;
    w.key("name");
    w.value(ref.name);
    w.key("name_source");
    w.value(nameSourceName(ref.source));
}

void writeRef(json::Writer& w, const canon::Ref& ref, const std::string& uri) {
    w.beginObject();
    w.key("id");
    writeIdentity(w, ref.id);
    writeName(w, ref);
    if (!uri.empty()) {
        w.key("uri");
        w.value(uri);
    }
    w.endObject();
}

void writeAssetRef(json::Writer& w, const canon::Ref& ref, const AssetUris& uris) {
    std::string uri;
    if (auto fdid = std::get_if<canon::FileDataId>(&ref.id)) {
        auto it = uris.find(fdid->value);
        if (it != uris.end()) uri = it->second;
    }
    writeRef(w, ref, uri);
}

canon::Ref subjectRef(const std::filesystem::path& file, std::optional<uint32_t> fileDataId) {
    canon::Identity id = fileDataId ? canon::Identity{canon::FileDataId{*fileDataId}} : canon::Identity{canon::None{}};
    std::string name = file.stem().string();
    bool nameIsId = fileDataId && name == std::to_string(*fileDataId);
    return canon::Ref{id, name, nameIsId ? canon::NameSource::Synthesized : canon::NameSource::Listfile};
}

void writeContractHeader(json::Writer& w, const char* kind, const canon::Ref& subject, const Provenance& provenance) {
    w.key("format");
    w.value("husk-bundle");
    w.key("kind");
    w.value(kind);
    w.key("schema_version");
    // Every schema/*.schema.json pins this as its schema_version const;
    // bump them together. Below 1.0, placeholder effects are allowed
    // (REFACTOR/EFFECTS.md).
    w.value("0.3.0");
    w.key("subject");
    writeRef(w, subject);
    w.key("space");
    w.beginObject();
    w.key("length_unit");
    w.value("yard");
    w.key("angle_unit");
    w.value("radian");
    w.key("time_unit");
    w.value("second");
    w.key("up_axis");
    w.value("+z");
    w.key("handedness");
    w.value("right");
    w.endObject();
    w.key("provenance");
    w.beginObject();
    w.key("producer");
    w.value(provenance.producer);
    w.key("input_format");
    w.value(provenance.inputFormat);
    w.key("sources");
    w.beginArray();
    for (const Provenance::Source& source : provenance.sources) {
        w.beginObject();
        w.key("name");
        w.value(source.name);
        w.key("role");
        w.value(source.role);
        w.key("available");
        w.value(source.available);
        if (!source.detail.empty()) {
            w.key("detail");
            w.value(source.detail);
        }
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

std::string hexFlags(uint32_t flags) {
    std::ostringstream out;
    out << "0x" << std::hex << flags;
    return out.str();
}

void writeSourceRecord(json::Writer& w, const canon::SourceRecord& record) {
    w.beginObject();
    w.key("table");
    w.value(record.table);
    w.key("index");
    w.value(static_cast<int64_t>(record.index));
    w.endObject();
}

void writeInstanceRef(json::Writer& w, const canon::InstanceRef& ref) {
    w.beginObject();
    w.key("set");
    w.value(static_cast<int64_t>(ref.set));
    w.key("id");
    w.value(static_cast<int64_t>(ref.id));
    w.endObject();
}

namespace {

const char* unresolvedKindName(canon::UnresolvedField::Kind kind) {
    switch (kind) {
        case canon::UnresolvedField::Kind::Scalar: return "scalar";
        case canon::UnresolvedField::Kind::Scalars: return "scalars";
        case canon::UnresolvedField::Kind::Bits: return "bits";
        case canon::UnresolvedField::Kind::Color: return "color";
        case canon::UnresolvedField::Kind::Count: return "count";
        case canon::UnresolvedField::Kind::Id: return "id";
        case canon::UnresolvedField::Kind::Angle: return "angle";
        case canon::UnresolvedField::Kind::UnitVector: return "unit_vector";
        case canon::UnresolvedField::Kind::Words: return "words";
    }
    return "scalar";
}

}  // namespace

void writeUnresolved(json::Writer& w, const std::vector<canon::UnresolvedField>& fields) {
    w.beginObject();
    w.key("fields");
    w.beginArray();
    for (const canon::UnresolvedField& f : fields) {
        w.beginObject();
        w.key("field");
        w.value(f.field);
        w.key("kind");
        w.value(unresolvedKindName(f.kind));
        w.key("value");
        std::visit(
            [&w](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, uint32_t>) {
                    w.value(hexFlags(v));
                } else if constexpr (std::is_same_v<T, std::vector<uint32_t>>) {
                    w.beginArray();
                    for (uint32_t word : v) w.value(hexFlags(word));
                    w.endArray();
                } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                    w.beginArray();
                    for (double d : v) w.value(d);
                    w.endArray();
                } else {
                    w.value(v);
                }
            },
            f.value);
        if (f.candidateRole) {
            w.key("candidate_role");
            w.value(*f.candidateRole);
        }
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

namespace {

const bundle_schema::EmbeddedSchema* embeddedTechnique(const std::string& name) {
    const auto& techniques = bundle_schema::embeddedTechniques();
    auto it = std::find_if(techniques.begin(), techniques.end(), [&](const auto& t) { return t.name == name; });
    return it == techniques.end() ? nullptr : &*it;
}

}  // namespace

std::string techniqueUri(const std::string& name) { return "techniques/" + name + ".json"; }

std::vector<std::string> materialTechniques(const canon::Material& material) {
    std::vector<std::string> candidates;
    if (material.pixelShader) {
        candidates.push_back("m2_pixel_shader/" + *material.pixelShader);
    } else if (material.shader) {
        const auto* id = std::get_if<canon::RecordIndex>(&material.shader->id);
        std::string name = !material.shader->name.empty() ? material.shader->name
                           : id                           ? "shader_" + std::to_string(id->value)
                                                          : "";
        if (!name.empty()) candidates.push_back("wmo_shader/" + name);
    }
    if (material.framebufferBlend) candidates.push_back("framebuffer_blend/" + framebufferBlendName(*material.framebufferBlend));
    std::vector<std::string> out;
    for (const std::string& name : candidates) {
        if (embeddedTechnique(name)) out.push_back(name);
    }
    return out;
}

void writeTechniqueDocuments(const std::vector<std::string>& names, const std::filesystem::path& bundleDir,
                             const Provenance& provenance) {
    std::set<std::string> written;
    std::vector<std::string> pending(names.begin(), names.end());
    while (!pending.empty()) {
        std::string name = pending.back();
        pending.pop_back();
        if (!written.insert(name).second) continue;
        const bundle_schema::EmbeddedSchema* body = embeddedTechnique(name);
        if (!body) throw std::runtime_error("bundle writer: no technique document '" + name + "' in techniques/");
        for (const nlohmann::json& ref : nlohmann::json::parse(body->text)["references"]) {
            if (ref["kind"] == "technique") pending.push_back(ref["asset"]["name"]);
        }
        std::ostringstream text;
        json::Writer w(text);
        w.beginObject();
        writeContractHeader(w, "technique", canon::Ref{canon::None{}, name, canon::NameSource::Synthesized}, provenance);
        w.rawMembers(std::string(body->text));
        w.endObject();
        std::filesystem::path path = bundleDir / techniqueUri(name);
        std::filesystem::create_directories(path.parent_path());
        std::string bytes = text.str();
        writeFile(path, std::vector<uint8_t>(bytes.begin(), bytes.end()));
    }
}

void writeSurfaceTypes(json::Writer& w, const std::vector<canon::SurfaceType>& types) {
    w.beginArray();
    for (const canon::SurfaceType& s : types) {
        w.beginObject();
        w.key("ref");
        writeRef(w, s.ref);
        if (s.soundClass) {
            w.key("sound_class");
            writeRef(w, *s.soundClass);
        }
        for (auto [key, spray] : {std::pair{"footstep_spray_run", &s.footstepSprayRun},
                                  std::pair{"footstep_spray_walk", &s.footstepSprayWalk}}) {
            if (!*spray) continue;
            w.key(key);
            writeRef(w, **spray);
        }
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(s.flags));
        w.endObject();
        w.endObject();
    }
    w.endArray();
}

void writeLiquidTypes(json::Writer& w, const std::vector<canon::LiquidType>& types,
                      const std::function<void(const canon::TextureRef&)>& writeTexture) {
    w.beginArray();
    for (const canon::LiquidType& t : types) {
        w.beginObject();
        w.key("identity");
        writeRef(w, t.ref);
        if (!std::holds_alternative<canon::None>(t.material.id)) {
            w.key("material");
            w.beginObject();
            w.key("identity");
            writeRef(w, t.material);
            if (t.materialVertexFormat) {
                w.key("vertex_format");
                w.value(static_cast<int64_t>(*t.materialVertexFormat));
                w.key("source");
                w.beginObject();
                w.key("flags");
                w.value(hexFlags(*t.materialFlags));
                w.endObject();
            }
            w.endObject();
        }
        if (!t.textures.empty()) {
            w.key("textures");
            w.beginArray();
            for (const canon::LiquidTextureSlot& slot : t.textures) {
                w.beginObject();
                w.key("frames");
                w.beginArray();
                for (const canon::TextureRef& frame : slot.frames) writeTexture(frame);
                w.endArray();
                w.key("source");
                w.beginObject();
                w.key("kind");
                w.value(static_cast<int64_t>(slot.sourceKind));
                w.endObject();
                w.endObject();
            }
            w.endArray();
        }
        if (!t.waves.empty()) {
            w.key("waves");
            w.beginArray();
            for (const canon::LiquidWaveSpectrum& spectrum : t.waves) {
                w.beginObject();
                w.key("identity");
                writeRef(w, spectrum.ref);
                w.key("source");
                w.beginObject();
                w.key("field_0");
                w.value(static_cast<int64_t>(spectrum.field0));
                w.key("values");
                w.beginArray();
                for (float v : spectrum.values) w.value(static_cast<double>(v));
                w.endArray();
                w.endObject();
                w.endObject();
            }
            w.endArray();
        }
        if (t.flags) {
            w.key("source");
            w.beginObject();
            w.key("flags");
            w.value(hexFlags(*t.flags));
            w.endObject();
        }
        w.endObject();
    }
    w.endArray();
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
        case canon::TextureEncoding::Bc5:
        case canon::TextureEncoding::Bgra:
        case canon::TextureEncoding::Palettized:
            return "dds";  // BUNDLE_FORMAT.md's "Texture encoding -- settled": DDS houses the compressed blocks verbatim
    }
    return "png";
}

std::string textureMediaType(const std::string& extension) {
    if (extension == "png") return "image/png";
    if (extension == "dds") return "image/vnd-ms.dds";
    throw std::logic_error("textureMediaType: no media type for extension '" + extension + "'");
}

void writeVec2Value(json::Writer& w, const canon::Vec2& v) {
    w.beginArray();
    w.value(static_cast<double>(v.x));
    w.value(static_cast<double>(v.y));
    w.endArray();
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
        case canon::FramebufferBlend::ModAdd: return "mod_add";
    }
    return "opaque";
}

uint32_t TextureTable::add(const std::function<void(json::Writer&)>& writeEntry) {
    std::ostringstream text;
    json::Writer w(text);
    writeEntry(w);
    auto [it, added] = index_.emplace(text.str(), static_cast<uint32_t>(entries_.size()));
    if (added) entries_.push_back(text.str());
    return it->second;
}

uint32_t TextureTable::add(const canon::TextureRef& texture) {
    auto fdid = std::get_if<canon::FileDataId>(&texture.resolved.id);
    bool hasPayload = texture.payload || texture.rawPayload;
    if (texture.state != canon::TextureRef::State::Resolved || hasPayload || !fdid || !shared_ || !shared_->count(fdid->value)) {
        return add([&](json::Writer& w) { writeTextureEntry(w, texture, bundleDir_, writtenTextures_, uriPrefix_); });
    }
    std::string uri = uriPrefix_ + shared_->at(fdid->value);
    return add([&](json::Writer& w) {
        w.beginObject();
        w.key("state");
        w.value("resolved");
        w.key("identity");
        writeRef(w, texture.resolved);
        w.key("payload");
        w.beginObject();
        w.key("uri");
        w.value(uri);
        w.key("media_type");
        w.value(textureMediaType(std::filesystem::path(uri).extension().string().substr(1)));
        w.endObject();
        w.endObject();
    });
}

void TextureTable::write(json::Writer& w) const {
    w.key("textures");
    w.beginArray();
    for (const std::string& entry : entries_) w.rawValue(entry);
    w.endArray();
}

// Writes `texture`'s resolved payload (when it carries one)
// to `bundleDir/textures/<name>.<ext>` and records it as the entry's
// `payload` {uri, media_type} beside its identity -- BUNDLE_FORMAT.md's
// "Embed or reference" shape. A Resolved identity with no payload (a
// future reference-only producer) writes the identity with no payload, the
// documented "husk knows what this is" case, not an error. `writtenTextures` dedupes by
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
void writeTextureEntry(json::Writer& w, const canon::TextureRef& texture, const std::filesystem::path& bundleDir,
                       std::unordered_set<std::string>& writtenTextures, const std::string& uriPrefix) {
    w.beginObject();
    switch (texture.state) {
        case canon::TextureRef::State::Resolved: {
            w.key("state");
            w.value("resolved");
            w.key("identity");
            writeRef(w, texture.resolved);
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
                std::string extension = textureFileExtension(chosen->encoding);
                std::string filename = stem + "." + extension;
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
                w.key("payload");
                w.beginObject();
                w.key("uri");
                w.value(uriPrefix + "textures/" + filename);
                w.key("media_type");
                w.value(textureMediaType(extension));
                w.endObject();
            }
            break;
        }
        case canon::TextureRef::State::KnownUnresolved:
            w.key("state");
            w.value("known_unresolved");
            if (!std::holds_alternative<canon::None>(texture.resolved.id)) {
                w.key("identity");
                writeRef(w, texture.resolved);
            }
            w.key("unresolved_reason");
            w.value(texture.unresolvedReason);
            break;
        case canon::TextureRef::State::Ambiguous:
            w.key("state");
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
    w.endObject();
}

}  // namespace husk::writers
