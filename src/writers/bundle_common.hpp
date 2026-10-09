#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "canon_material.hpp"
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

std::string textureFileExtension(canon::TextureEncoding encoding);

// Writes a TextureRef's `texture_state` and state-specific keys into the
// currently open object, and its payload file under `bundleDir/textures/`
// (deduped by filename via `writtenTextures`). See the definition for the
// DDS-over-PNG payload choice.
void writeTextureRef(json::Writer& w, const canon::TextureRef& texture, const std::filesystem::path& bundleDir,
                      std::unordered_set<std::string>& writtenTextures);

std::string isoTimestampUtc();

void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes);

}  // namespace husk::writers
