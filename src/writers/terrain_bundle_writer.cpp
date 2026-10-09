#include "writers/terrain_bundle_writer.hpp"

#include <array>
#include <initializer_list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <variant>

#include "json_writer.hpp"
#include "writers/bundle_common.hpp"

namespace husk::writers {

namespace {

constexpr const char* kTerrainBin = "terrain.bin";
constexpr const char* kLiquidBin = "liquid.bin";

void writeFloatArray(json::Writer& w, std::initializer_list<float> values) {
    w.beginArray();
    for (float v : values) w.value(static_cast<double>(v));
    w.endArray();
}

std::string heightSourceName(canon::LiquidHeightSource source) {
    switch (source) {
        case canon::LiquidHeightSource::Heightmap: return "heightmap";
        case canon::LiquidHeightSource::MinHeightLevel: return "min_height_level";
        case canon::LiquidHeightSource::Zero: return "zero";
    }
    return "heightmap";
}

// A Ref whose payload may live in another bundle: `uri` when the exporter
// produced that bundle, identity only otherwise.
void writeAssetRef(json::Writer& w, const canon::Ref& ref, const AssetUris& modelUris) {
    std::string uri;
    if (auto fdid = std::get_if<canon::FileDataId>(&ref.id)) {
        auto it = modelUris.find(fdid->value);
        if (it != modelUris.end()) uri = it->second;
    }
    writeRef(w, ref, uri);
}

void writeTerrainTexture(json::Writer& w, const canon::TextureRef& texture, const std::filesystem::path& bundleDir,
                         std::unordered_set<std::string>& written, const AssetUris& textureUris) {
    w.beginObject();
    bool hasPayload = texture.payload || texture.rawPayload;
    auto fdid = std::get_if<canon::FileDataId>(&texture.resolved.id);
    if (texture.state == canon::TextureRef::State::Resolved && !hasPayload && fdid && textureUris.count(fdid->value)) {
        w.key("texture_state");
        w.value("resolved");
        w.key("texture");
        writeRef(w, texture.resolved, textureUris.at(fdid->value));
    } else {
        writeTextureRef(w, texture, bundleDir, written);
    }
    w.endObject();
}

void writeTexturesSection(json::Writer& w, const std::vector<canon::TerrainTexture>& textures,
                          const std::filesystem::path& bundleDir, const AssetUris& textureUris) {
    std::unordered_set<std::string> written;
    w.beginArray();
    for (const canon::TerrainTexture& t : textures) {
        w.beginObject();
        w.key("diffuse");
        writeTerrainTexture(w, t.diffuse, bundleDir, written, textureUris);
        if (t.height) {
            w.key("height");
            writeTerrainTexture(w, *t.height, bundleDir, written, textureUris);
        }
        w.key("repeats_per_chunk");
        w.value(static_cast<double>(t.repeatsPerChunk));
        w.key("height_scale");
        w.value(static_cast<double>(t.heightScale));
        w.key("height_offset");
        w.value(static_cast<double>(t.heightOffset));
        w.endObject();
    }
    w.endArray();
}

void writeGroundEffectsSection(json::Writer& w, const std::vector<canon::GroundEffect>& effects,
                               const AssetUris& modelUris) {
    w.beginArray();
    for (const canon::GroundEffect& e : effects) {
        w.beginObject();
        w.key("ref");
        writeRef(w, e.ref);
        if (e.density) {
            w.key("density");
            w.value(static_cast<int64_t>(*e.density));
        }
        w.key("doodads");
        w.beginArray();
        for (const canon::GroundEffectDoodad& d : e.doodads) {
            w.beginObject();
            w.key("doodad");
            writeRef(w, d.doodad);
            w.key("model");
            writeAssetRef(w, d.model, modelUris);
            w.key("weight");
            w.value(static_cast<int64_t>(d.weight));
            w.key("flags");
            w.value(static_cast<int64_t>(d.flags));
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
}

void writeLayer(json::Writer& w, const canon::TerrainLayer& layer, std::vector<uint8_t>& bin) {
    w.beginObject();
    w.key("texture_index");
    w.value(static_cast<int64_t>(layer.textureIndex));
    if (layer.groundEffectIndex) {
        w.key("ground_effect_index");
        w.value(static_cast<int64_t>(*layer.groundEffectIndex));
    }
    if (layer.overbright) {
        w.key("overbright");
        w.value(true);
    }
    if (layer.animation) {
        w.key("animation");
        w.beginObject();
        w.key("direction_degrees");
        w.value(static_cast<double>(layer.animation->directionDegrees));
        w.key("speed");
        w.value(static_cast<int64_t>(layer.animation->speed));
        w.endObject();
    }
    if (!layer.alpha.empty()) {
        w.key("alpha");
        writeBufferSlice(w, appendArray(bin, kTerrainBin, layer.alpha, 1, "u8"));
    }
    w.endObject();
}

void writeChunksSection(json::Writer& w, const std::vector<canon::TerrainChunk>& chunks, std::vector<uint8_t>& bin) {
    std::vector<canon::Vec2> origins;
    std::vector<std::array<float, canon::kTerrainVertexCount>> heights;
    std::vector<canon::Vec3> normals;
    std::vector<std::array<uint8_t, canon::kTerrainQuadsPerSide>> holeRows;
    std::vector<std::array<uint8_t, canon::kTerrainQuadsPerSide * canon::kTerrainQuadsPerSide>> dominant;
    std::vector<std::array<uint8_t, canon::kTerrainQuadsPerSide>> suppressed;
    std::vector<uint32_t> areaIndex;
    std::vector<canon::Ref> areas;
    std::map<uint32_t, uint32_t> areaSlot;
    for (const canon::TerrainChunk& c : chunks) {
        origins.push_back(c.origin);
        heights.push_back(c.heights);
        normals.insert(normals.end(), c.normals.begin(), c.normals.end());
        holeRows.push_back(c.holeRows);
        dominant.push_back(c.dominantLayer);
        suppressed.push_back(c.groundEffectSuppressedRows);
        uint32_t row = std::get<canon::Db2Row>(c.area.id).row;
        auto [it, inserted] = areaSlot.emplace(row, static_cast<uint32_t>(areas.size()));
        if (inserted) areas.push_back(c.area);
        areaIndex.push_back(it->second);
    }

    w.beginObject();
    w.key("count");
    w.value(static_cast<int64_t>(chunks.size()));
    w.key("origin");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, origins, 2, "f32"));
    w.key("heights");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, heights, canon::kTerrainVertexCount, "f32"));
    w.key("normals");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, normals, 3, "f32"), "NORMAL");
    w.key("hole_rows");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, holeRows, canon::kTerrainQuadsPerSide, "u8"));
    w.key("dominant_layer");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, dominant,
                                    canon::kTerrainQuadsPerSide * canon::kTerrainQuadsPerSide, "u8"));
    w.key("ground_effect_suppressed_rows");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, suppressed, canon::kTerrainQuadsPerSide, "u8"));
    w.key("area_index");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, areaIndex, 1, "u32"));
    w.key("areas");
    w.beginArray();
    for (const canon::Ref& a : areas) writeRef(w, a);
    w.endArray();
    w.key("layers");
    w.beginArray();
    for (const canon::TerrainChunk& c : chunks) {
        w.beginArray();
        for (const canon::TerrainLayer& layer : c.layers) writeLayer(w, layer, bin);
        w.endArray();
    }
    w.endArray();
    w.endObject();
}

void writeLiquid(json::Writer& w, const canon::LiquidSurface& l, std::vector<uint8_t>& bin) {
    w.beginObject();
    w.key("chunk_grid_x");
    w.value(static_cast<int64_t>(l.chunkGridX));
    w.key("chunk_grid_y");
    w.value(static_cast<int64_t>(l.chunkGridY));
    w.key("liquid_type");
    writeRef(w, l.liquidType);
    if (!std::holds_alternative<canon::None>(l.liquidObject.id)) {
        w.key("liquid_object");
        writeRef(w, l.liquidObject);
    }
    w.key("quad_rect");
    w.beginObject();
    w.key("x");
    w.value(static_cast<int64_t>(l.quadX));
    w.key("y");
    w.value(static_cast<int64_t>(l.quadY));
    w.key("width");
    w.value(static_cast<int64_t>(l.width));
    w.key("height");
    w.value(static_cast<int64_t>(l.height));
    w.endObject();
    w.key("quad_exists");
    writeBufferSlice(w, appendArray(bin, kLiquidBin, l.quadExists, 1, "u8"));
    w.key("height_source");
    w.value(heightSourceName(l.heightSource));
    w.key("heights");
    writeBufferSlice(w, appendArray(bin, kLiquidBin, l.heights, 1, "f32"));
    if (l.depth) {
        w.key("depth");
        writeBufferSlice(w, appendArray(bin, kLiquidBin, *l.depth, 1, "f32"));
    }
    if (l.uv) {
        w.key("uv");
        writeBufferSlice(w, appendArray(bin, kLiquidBin, *l.uv, 2, "f32"));
    }
    w.endObject();
}

void writePlacement(json::Writer& w, const canon::Placement& p, const AssetUris& modelUris) {
    w.beginObject();
    w.key("kind");
    w.value(p.kind == canon::Placement::Kind::Model ? "model" : "map_object");
    w.key("asset");
    writeAssetRef(w, p.asset, modelUris);
    w.key("unique_id");
    w.value(static_cast<int64_t>(p.uniqueId));
    w.key("position");
    writeFloatArray(w, {p.position.x, p.position.y, p.position.z});
    w.key("rotation");
    writeFloatArray(w, {p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w});
    w.key("scale");
    w.value(static_cast<double>(p.scale));
    w.key("flags");
    w.value(static_cast<int64_t>(p.flags));
    if (p.doodadSet) {
        w.key("doodad_set");
        w.value(static_cast<int64_t>(*p.doodadSet));
    }
    w.endObject();
}

}  // namespace

void writeTerrainBundle(const canon::Terrain& terrain, const std::filesystem::path& bundleDir,
                         const AssetUris& modelUris, const AssetUris& textureUris, const std::string& producer) {
    std::error_code ec;
    std::filesystem::create_directories(bundleDir, ec);
    if (ec) {
        throw std::runtime_error("terrain bundle writer: could not create directory " + bundleDir.string() + ": " +
                                 ec.message());
    }

    std::vector<uint8_t> terrainBin;
    std::vector<uint8_t> liquidBin;
    std::ostringstream manifestStream;
    json::Writer w(manifestStream);

    w.beginObject();
    w.key("schema_version");
    w.value("0.1.0");
    w.key("kind");
    w.value("terrain_tile");
    w.key("exported_at");
    w.value(isoTimestampUtc());
    w.key("producer");
    w.value(producer);
    w.key("endianness");
    w.value("little");

    w.key("terrain");
    w.beginObject();
    w.key("map");
    writeRef(w, terrain.map);
    w.key("tile_x");
    w.value(static_cast<int64_t>(terrain.tileX));
    w.key("tile_y");
    w.value(static_cast<int64_t>(terrain.tileY));
    w.key("frame");
    w.value("wow_world_yards_x_north_y_west_z_up");
    w.key("tile_size");
    w.value(static_cast<double>(canon::kTerrainTileSize));
    w.key("chunk_size");
    w.value(static_cast<double>(canon::kTerrainChunkSize));
    w.key("quad_size");
    w.value(static_cast<double>(canon::kTerrainQuadSize));
    w.key("chunk_vertex_layout");
    w.value("interleaved_9x9_corners_8x8_centres");
    w.key("chunk_topology");
    w.value("quad_centre_fan_4_triangles_holes_skip_quad");
    w.key("textures");
    writeTexturesSection(w, terrain.textures, bundleDir, textureUris);
    w.key("ground_effects");
    writeGroundEffectsSection(w, terrain.groundEffects, modelUris);
    w.key("chunks");
    writeChunksSection(w, terrain.chunks, terrainBin);
    w.key("liquids");
    w.beginArray();
    for (const canon::LiquidSurface& l : terrain.liquids) writeLiquid(w, l, liquidBin);
    w.endArray();
    w.key("placements");
    w.beginArray();
    for (const canon::Placement& p : terrain.placements) writePlacement(w, p, modelUris);
    w.endArray();
    w.endObject();  // terrain
    w.endObject();  // root

    writeFile(bundleDir / kTerrainBin, terrainBin);
    writeFile(bundleDir / kLiquidBin, liquidBin);
    std::string manifest = manifestStream.str();
    writeFile(bundleDir / "manifest.json", std::vector<uint8_t>(manifest.begin(), manifest.end()));
}

}  // namespace husk::writers
