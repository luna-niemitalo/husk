#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "canon_liquid.hpp"
#include "canon_material.hpp"
#include "canon_placement.hpp"
#include "canon_scene.hpp"  // Animated, LifetimeCurve for the curve writers
#include "canon_ref.hpp"
#include "json_writer.hpp"

// husk::writers: manifest building blocks shared by every bundle writer
// (bundle_writer.cpp for canon::Model, terrain_bundle_writer.cpp for
// canon::Terrain). bundle_writer.hpp's doc comment remains the canonical
// description of the BufferSlice and Ref JSON shapes these produce.
namespace husk::writers {

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

// A flat array holding `componentCount` values per element.
template <typename T>
BufferSlice appendComponents(std::vector<uint8_t>& buf, std::string filename, const std::vector<T>& values,
                             uint32_t componentCount, std::string componentType) {
    BufferSlice slice = appendArray(buf, std::move(filename), values, componentCount, std::move(componentType));
    slice.count = values.size() / componentCount;
    return slice;
}

void writeBufferSlice(json::Writer& w, const BufferSlice& slice, const char* semantic = nullptr);

// One shared serializer for canon::Ref's Identity variant -- every call
// site (bone/geoset/material-layer/texture/terrain identities) shares this
// rather than re-visiting the variant per site.
// `uri`, when non-empty, adds BUNDLE_FORMAT.md's "Embed or reference"
// field.
void writeRef(json::Writer& w, const canon::Ref& ref, const std::string& uri = "");

// FileDataID -> uri of a file the caller produced outside this bundle (a
// model bundle, or a texture shared across tiles), relative to the file that
// will reference it.
using AssetUris = std::unordered_map<uint32_t, std::string>;

// A Ref whose payload may live in another bundle: `uri` when the exporter
// produced that bundle, identity only otherwise.
void writeAssetRef(json::Writer& w, const canon::Ref& ref, const AssetUris& uris);

// Who wrote a document and from what (schema/common.schema.json
// "provenance"); the writer adds the export time.
struct Provenance {
    struct Source {
        std::string name;
        std::string role;
        bool available = false;
        std::string detail;
    };
    std::string producer;
    std::string inputFormat;
    std::vector<Source> sources;
};

// The contract header's first keys, in order: format, kind,
// schema_version, subject, space, provenance. The caller continues with
// definition, selection, references and the kind's data.
// The subject Ref of a document decoded from `file`: identity is the
// FileDataID when the caller knows it. The name is the file's stem; the
// corpus's file names come from the community listfile, except a
// casc-tool store path (`_by_fdid/<fdid>`), whose stem is the id itself.
canon::Ref subjectRef(const std::filesystem::path& file, std::optional<uint32_t> fileDataId);

void writeContractHeader(json::Writer& w, const char* kind, const canon::Ref& subject, const Provenance& provenance);

// A flag word as stored, lowercase hex ("0x1f").
std::string hexFlags(uint32_t flags);

void writeSourceRecord(json::Writer& w, const canon::SourceRecord& record);
void writeUnresolved(json::Writer& w, const std::vector<canon::UnresolvedField>& fields);

struct OwnedSetEntry {
    const canon::PlacementSet* set = nullptr;
    bool alwaysOn = false;
    bool inlineDocument = false;  // written into the owner's manifest, not sets/
};

// A placement_set document written by writePlacementSetDocuments: a file
// at `uri`, or the document itself in `inlineText` for an inline set.
struct SetDocument {
    canon::Ref ref;
    std::string uri;  // relative to the bundle directory; empty for an inline set
    bool alwaysOn = false;
    size_t instanceCount = 0;
    std::string inlineText;
};

// Writes each set as a placement_set document (schema/placement_set.schema.json):
// `sets/<index>.json` under `bundleDir`, or text for the caller to embed for
// an inline set; and every effect their instances place as
// `effects/<name>.json`. `assetUris` are relative to `bundleDir`.
std::vector<SetDocument> writePlacementSetDocuments(const std::vector<OwnedSetEntry>& sets,
                                                    const std::filesystem::path& bundleDir, const AssetUris& assetUris,
                                                    const Provenance& provenance);

// An effect document (schema/effect.schema.json): the contract header,
// then the body checked in as effects/<name>.json. Throws when husk embeds
// no effect of that name.
void writeEffectDocument(const std::string& name, const std::filesystem::path& path, const Provenance& provenance);

// The parts of a document that link it to the sets it owns, set i being
// reference i: `writeOwnedSetDefinitions` writes the "placement_sets" key
// inside an open definition object; `writeOwnedSetSelectionAndReferences`
// writes the selection (no optional set chosen) and references keys.
void writeOwnedSetDefinitions(json::Writer& w, const std::vector<SetDocument>& sets);
void writeOwnedSetSelectionAndReferences(json::Writer& w, const std::vector<SetDocument>& sets);

std::string textureFileExtension(canon::TextureEncoding encoding);

// IANA media type of a texture file by its extension ("png", "dds").
std::string textureMediaType(const std::string& extension);

// Writes a TextureRef as a texture entry object (schema/common.schema.json
// "texture_entry"), and its payload file under `bundleDir/textures/`
// (deduped by filename via `writtenTextures`). `uriPrefix` leads from the
// document being written to `bundleDir` ("../" from sets/). See the
// definition for the DDS-over-PNG payload choice.
void writeTextureEntry(json::Writer& w, const canon::TextureRef& texture, const std::filesystem::path& bundleDir,
                       std::unordered_set<std::string>& writtenTextures, const std::string& uriPrefix = "");

// The schema's framebuffer blend vocabulary.
std::string framebufferBlendName(canon::FramebufferBlend blend);

// Value and field writers every document writer shares.
void writeVec2Value(json::Writer& w, const canon::Vec2& v);
void writeVec3Value(json::Writer& w, const canon::Vec3& v);
void writeQuatValue(json::Writer& w, const canon::Quat& q);
void writeScalarValue(json::Writer& w, float v);
void writeFloatField(json::Writer& w, const char* key, float v);
void writeIntField(json::Writer& w, const char* key, int64_t v);
void writeVec3Field(json::Writer& w, const char* key, const canon::Vec3& v);
void writeFloatArrayField(json::Writer& w, const char* key, const float* values, size_t count);
std::string sequenceKindName(canon::SequenceRef::Kind kind);
std::string interpolationName(canon::Interpolation interp);

// One curve's keyframes inline (schema "inline_curve"): small, per-material
// or per-emitter data, deliberately not a BufferSlice. `writeValue` writes
// one keyframe's value in whatever shape T needs (a number for float, an
// array for Vec3/Quat).
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

// resources.liquid_types (schema/common.schema.json "liquid_type"), the
// array only; `writeTexture` writes one frame's texture entry, so terrain
// and model writers keep their own payload placement.
void writeLiquidTypes(json::Writer& w, const std::vector<canon::LiquidType>& types,
                      const std::function<void(const canon::TextureRef&)>& writeTexture);

std::string isoTimestampUtc();

void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);

}  // namespace husk::writers
