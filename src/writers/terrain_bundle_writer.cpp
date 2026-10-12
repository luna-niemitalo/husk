#include "writers/terrain_bundle_writer.hpp"

#include <array>
#include <numbers>
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

std::string heightSourceName(canon::LiquidHeightSource source) {
    switch (source) {
        case canon::LiquidHeightSource::Heightmap: return "stored";
        case canon::LiquidHeightSource::MinHeightLevel: return "min_height_level";
        case canon::LiquidHeightSource::Zero: return "zero";
    }
    return "stored";
}

void writeTexturesSection(json::Writer& w, const std::vector<canon::TerrainTexture>& textures, TextureTable& table) {
    w.beginArray();
    for (const canon::TerrainTexture& t : textures) {
        w.beginObject();
        w.key("diffuse");
        w.value(static_cast<int64_t>(table.add(t.diffuse)));
        if (t.height) {
            w.key("height");
            w.value(static_cast<int64_t>(table.add(*t.height)));
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
        w.key("identity");
        writeRef(w, e.ref);
        if (e.density) {
            w.key("density");
            w.value(static_cast<int64_t>(*e.density));
        }
        if (e.surfaceType) {
            w.key("surface_type");
            w.value(static_cast<int64_t>(*e.surfaceType));
        }
        w.key("doodads");
        w.beginArray();
        for (const canon::GroundEffectDoodad& d : e.doodads) {
            w.beginObject();
            w.key("identity");
            writeRef(w, d.doodad);
            w.key("model");
            writeAssetRef(w, d.model, modelUris);
            w.key("weight");
            w.value(static_cast<int64_t>(d.weight));
            w.key("source");
            w.beginObject();
            w.key("flags");
            w.value(hexFlags(d.flags));
            w.endObject();
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
        w.key("direction");
        w.value(static_cast<double>(layer.animation->directionDegrees) * std::numbers::pi / 180.0);
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
    std::vector<std::array<uint8_t, canon::kTerrainQuadsPerSide>> fishable;
    std::vector<std::array<uint8_t, canon::kTerrainQuadsPerSide>> deep;
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
        fishable.push_back(c.liquidFishableRows);
        deep.push_back(c.liquidDeepRows);
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
    w.key("liquid_fishable_rows");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, fishable, canon::kTerrainQuadsPerSide, "u8"));
    w.key("liquid_deep_rows");
    writeBufferSlice(w, appendArray(bin, kTerrainBin, deep, canon::kTerrainQuadsPerSide, "u8"));
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

void writeLiquidObjects(json::Writer& w, const std::vector<canon::LiquidObject>& objects) {
    w.beginArray();
    for (const canon::LiquidObject& o : objects) {
        w.beginObject();
        w.key("identity");
        writeRef(w, o.ref);
        if (o.row) {
            w.key("flow_direction");
            w.value(static_cast<double>(o.row->flowDirection));
            w.key("flow_speed");
            w.value(static_cast<double>(o.row->flowSpeed));
            w.key("fishable");
            w.value(static_cast<int64_t>(o.row->fishable));
            w.key("reflection");
            w.value(static_cast<int64_t>(o.row->reflection));
        }
        w.endObject();
    }
    w.endArray();
}

void writeLiquid(json::Writer& w, const canon::LiquidSurface& l, std::vector<uint8_t>& bin) {
    w.beginObject();
    w.key("chunk_row");
    w.value(static_cast<int64_t>(l.chunkGridY));
    w.key("chunk_column");
    w.value(static_cast<int64_t>(l.chunkGridX));
    w.key("liquid_type");
    writeRef(w, l.liquidType);
    if (!std::holds_alternative<canon::None>(l.liquidObject.id)) {
        w.key("liquid_object");
        writeRef(w, l.liquidObject);
    }
    w.key("quad_rect");
    w.beginObject();
    w.key("row");
    w.value(static_cast<int64_t>(l.quadY));
    w.key("column");
    w.value(static_cast<int64_t>(l.quadX));
    w.key("rows");
    w.value(static_cast<int64_t>(l.height));
    w.key("columns");
    w.value(static_cast<int64_t>(l.width));
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

}  // namespace

void writeTerrainBundle(const canon::Terrain& terrain, const std::filesystem::path& bundleDir,
                        const Provenance& provenance, const AssetUris& modelUris, const AssetUris& textureUris) {
    std::error_code ec;
    std::filesystem::create_directories(bundleDir, ec);
    if (ec) {
        throw std::runtime_error("terrain bundle writer: could not create directory " + bundleDir.string() + ": " +
                                 ec.message());
    }

    std::vector<uint8_t> terrainBin;
    std::vector<uint8_t> liquidBin;
    // Both sets are the tile's own content, so both are always on.
    std::vector<SetDocument> sets = writePlacementSetDocuments({{&terrain.doodads, true}, {&terrain.mapObjects, true}},
                                                               bundleDir, modelUris, provenance);
    std::ostringstream manifestStream;
    json::Writer w(manifestStream);

    w.beginObject();
    writeContractHeader(w, "model", terrain.subject, provenance);
    w.key("definition");
    w.beginObject();
    writeOwnedSetDefinitions(w, sets);
    w.key("terrain");
    w.beginObject();
    w.key("edge_vertices");
    w.value("duplicated");
    w.endObject();
    w.endObject();
    writeOwnedSetSelectionAndReferences(w, sets);

    std::unordered_set<std::string> writtenTextures;
    // Ground textures exported once under the export's textures/ are referenced by their shared uri.
    TextureTable table(bundleDir, writtenTextures, "", &textureUris);
    w.key("resources");
    w.beginObject();
    w.key("terrain");
    w.beginObject();
    w.key("chunks_per_side");
    w.value(static_cast<int64_t>(canon::kTerrainChunksPerSide));
    w.key("quads_per_side");
    w.value(static_cast<int64_t>(canon::kTerrainQuadsPerSide));
    w.key("tile_size");
    w.value(static_cast<double>(canon::kTerrainTileSize));
    w.key("chunk_size");
    w.value(static_cast<double>(canon::kTerrainChunkSize));
    w.key("quad_size");
    w.value(static_cast<double>(canon::kTerrainQuadSize));
    // canon_terrain.hpp's TerrainChunk: rows step south (-X), columns east (-Y).
    for (auto [key, axis] : {std::pair{"row_axis", std::array{-1, 0, 0}}, std::pair{"column_axis", std::array{0, -1, 0}}}) {
        w.key(key);
        w.beginArray();
        for (int c : axis) w.value(static_cast<double>(c));
        w.endArray();
    }
    w.key("chunk_vertex_layout");
    w.value("interleaved_9x9_corners_8x8_centres");
    w.key("chunk_topology");
    w.value("quad_centre_fan");
    w.key("textures");
    writeTexturesSection(w, terrain.textures, table);
    w.key("ground_effects");
    writeGroundEffectsSection(w, terrain.groundEffects, modelUris);
    w.key("chunks");
    writeChunksSection(w, terrain.chunks, terrainBin);
    w.key("liquid_objects");
    writeLiquidObjects(w, terrain.liquidObjects);
    w.key("liquids");
    w.beginArray();
    for (const canon::LiquidSurface& l : terrain.liquids) writeLiquid(w, l, liquidBin);
    w.endArray();
    w.endObject();  // terrain
    if (!terrain.surfaceTypes.empty()) {
        w.key("surface_types");
        writeSurfaceTypes(w, terrain.surfaceTypes);
    }
    if (!terrain.liquidTypes.empty()) {
        w.key("liquid_types");
        writeLiquidTypes(w, terrain.liquidTypes, [&](const canon::TextureRef& frame) {
            w.value(static_cast<int64_t>(table.add(frame)));
        });
    }
    if (!table.empty()) table.write(w);
    w.endObject();  // resources
    w.endObject();

    writeFile(bundleDir / kTerrainBin, terrainBin);
    writeFile(bundleDir / kLiquidBin, liquidBin);
    std::string manifest = manifestStream.str();
    writeFile(bundleDir / "manifest.json", std::vector<uint8_t>(manifest.begin(), manifest.end()));
}

}  // namespace husk::writers
