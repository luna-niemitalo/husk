#include "writers/bundle_common.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>

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

void writeRef(json::Writer& w, const canon::Ref& ref, const std::string& uri) {
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
    w.value("1.0.0");
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
    w.key("exported_at");
    w.value(isoTimestampUtc());
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

namespace {

void writeFloats(json::Writer& w, std::initializer_list<float> values) {
    w.beginArray();
    for (float v : values) w.value(static_cast<double>(v));
    w.endArray();
}

// Identity key for deduplicating a set's referenced assets.
std::string identityKey(const canon::Ref& ref) {
    std::ostringstream key;
    std::visit(
        [&key](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, canon::None>) {
                key << "none";
            } else if constexpr (std::is_same_v<T, canon::Db2Row>) {
                key << "db2:" << id.table << ":" << id.row;
            } else if constexpr (std::is_same_v<T, canon::FileDataId>) {
                key << "fdid:" << id.value;
            } else {
                key << "record:" << id.value;
            }
        },
        ref.id);
    key << "/" << ref.name;
    return key.str();
}

void writePlacementSetFile(const canon::PlacementSet& set, const std::filesystem::path& path, const AssetUris& uris,
                           const Provenance& provenance) {
    std::vector<const canon::Ref*> assets;
    std::unordered_map<std::string, size_t> assetIndex;
    std::vector<size_t> instanceAsset;
    for (const canon::PlacedInstance& instance : set.instances) {
        auto [it, added] = assetIndex.emplace(identityKey(instance.asset), assets.size());
        if (added) assets.push_back(&instance.asset);
        instanceAsset.push_back(it->second);
    }

    std::ostringstream text;
    json::Writer w(text);
    w.beginObject();
    writeContractHeader(w, "placement_set", set.ref, provenance);
    w.key("definition");
    w.beginObject();
    w.endObject();
    w.key("selection");
    w.beginObject();
    w.endObject();
    w.key("references");
    w.beginArray();
    for (const canon::Ref* asset : assets) {
        w.beginObject();
        w.key("kind");
        w.value("model");
        w.key("asset");
        writeAssetRef(w, *asset, uris);
        w.endObject();
    }
    w.endArray();
    w.key("instances");
    w.beginArray();
    for (size_t i = 0; i < set.instances.size(); ++i) {
        const canon::PlacedInstance& instance = set.instances[i];
        w.beginObject();
        w.key("id");
        w.value(static_cast<int64_t>(instance.id));
        w.key("reference");
        w.value(static_cast<int64_t>(instanceAsset[i]));
        w.key("translation");
        writeFloats(w, {instance.translation.x, instance.translation.y, instance.translation.z});
        w.key("rotation");
        writeFloats(w, {instance.rotation.x, instance.rotation.y, instance.rotation.z, instance.rotation.w});
        w.key("scale");
        w.value(static_cast<double>(instance.scale));
        if (instance.tint) {
            w.key("tint");
            w.beginObject();
            w.key("rgba");
            w.beginArray();
            for (uint8_t c : instance.tint->rgba) w.value(c / 255.0);
            w.endArray();
            w.key("multiplier");
            w.value(static_cast<double>(instance.tint->multiplier));
            w.endObject();
        }
        w.key("selection");
        w.beginObject();
        w.key("placement_sets");
        w.beginArray();
        if (instance.activeSets) {
            for (const canon::Ref& ref : *instance.activeSets) {
                w.value(static_cast<int64_t>(std::get<canon::RecordIndex>(ref.id).value));
            }
        }
        w.endArray();
        w.endObject();
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(instance.flags));
        w.endObject();
        w.endObject();
    }
    w.endArray();
    w.endObject();

    std::string bytes = text.str();
    writeFile(path, std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

}  // namespace

std::vector<SetDocument> writePlacementSetDocuments(const std::vector<OwnedSetEntry>& sets,
                                                    const std::filesystem::path& bundleDir, const AssetUris& assetUris,
                                                    const Provenance& provenance) {
    // Set files sit one level down, in sets/.
    AssetUris setUris;
    for (const auto& [fdid, uri] : assetUris) {
        setUris.emplace(fdid, std::filesystem::path(uri).is_absolute() ? uri : "../" + uri);
    }
    if (!sets.empty()) std::filesystem::create_directories(bundleDir / "sets");
    std::vector<SetDocument> out;
    for (size_t i = 0; i < sets.size(); ++i) {
        std::string uri = "sets/" + std::to_string(i) + ".json";
        writePlacementSetFile(*sets[i].set, bundleDir / uri, setUris, provenance);
        out.push_back({sets[i].set->ref, uri, sets[i].alwaysOn, sets[i].set->instances.size()});
    }
    return out;
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
void writeOwnedSetDefinitions(json::Writer& w, const std::vector<SetDocument>& sets) {
    w.key("placement_sets");
    w.beginArray();
    for (size_t i = 0; i < sets.size(); ++i) {
        w.beginObject();
        w.key("reference");
        w.value(static_cast<int64_t>(i));
        w.key("always_on");
        w.value(sets[i].alwaysOn);
        w.key("instance_count");
        w.value(static_cast<int64_t>(sets[i].instanceCount));
        w.endObject();
    }
    w.endArray();
}

void writeOwnedSetSelectionAndReferences(json::Writer& w, const std::vector<SetDocument>& sets) {
    w.key("selection");
    w.beginObject();
    w.key("placement_sets");
    w.beginArray();
    w.endArray();
    w.endObject();

    w.key("references");
    w.beginArray();
    for (const SetDocument& set : sets) {
        w.beginObject();
        w.key("kind");
        w.value("placement_set");
        w.key("asset");
        writeRef(w, set.ref, set.uri);
        w.endObject();
    }
    w.endArray();
}

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

// Writes `texture`'s resolved payload (when it carries one, AUDIT.md §7.4)
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
                       std::unordered_set<std::string>& writtenTextures) {
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
                w.value("textures/" + filename);
                w.key("media_type");
                w.value(textureMediaType(extension));
                w.endObject();
            }
            break;
        }
        case canon::TextureRef::State::KnownUnresolved:
            w.key("state");
            w.value("known_unresolved");
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
