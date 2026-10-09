#include "adt_canon_input.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace husk::adtinput {

namespace {

// Map origin is the map's centre; the north-west corner is +32 tiles on both axes.
constexpr float kMapHalfExtent = 32.0f * canon::kTerrainTileSize;

constexpr uint32_t kMcnkHighResHoles = 0x10000;

// Liquid object IDs below this are not LiquidObject rows but a vertex
// format number (ADT/v18.md SMLiquidInstance).
constexpr uint16_t kFirstLiquidObjectId = 42;

// MCNR is X, Y, Z on disk in this corpus -- the wiki's "X, Z, Y" field
// comment disagrees with every finite-difference check (findings doc).
canon::Vec3 decodeNormal(const std::array<int8_t, 3>& raw) {
    float x = static_cast<float>(raw[0]) / 127.0f;
    float y = static_cast<float>(raw[1]) / 127.0f;
    float z = static_cast<float>(raw[2]) / 127.0f;
    float len = std::sqrt(x * x + y * y + z * z);
    if (len == 0.0f) return {0, 0, 1};
    return {x / len, y / len, z / len};
}

// The 16-bit map marks 2x2-quad blocks (ADT/v18.md "Terrain Holes"). Not
// exercised by this corpus -- every sampled MCNK uses the 64-bit map.
std::array<uint8_t, 8> expandLowResHoles(uint16_t bits) {
    std::array<uint8_t, 8> rows{};
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            int bit = (r / 2) * 4 + (c / 2);
            if ((bits >> bit) & 1) rows[r] = static_cast<uint8_t>(rows[r] | (1 << c));
        }
    }
    return rows;
}

canon::TerrainChunk buildChunk(const adt::MapChunk& mc) {
    canon::TerrainChunk out;
    out.gridX = mc.indexX;
    out.gridY = mc.indexY;
    out.origin = {mc.position[0], mc.position[1]};
    for (int i = 0; i < canon::kTerrainVertexCount; ++i) {
        out.heights[i] = mc.position[2] + mc.heights[i];
        out.normals[i] = decodeNormal(mc.normals[i]);
    }
    out.holeRows = (mc.flags & kMcnkHighResHoles) ? mc.holesHighRes : expandLowResHoles(mc.holesLowRes);
    out.area = canon::Ref{canon::Db2Row{"AreaTable", mc.areaId}, "", canon::NameSource::None};
    return out;
}

canon::LiquidSurface buildLiquid(const adt::LiquidInstance& inst, uint32_t chunkIndex) {
    canon::LiquidSurface out;
    out.chunkGridX = chunkIndex % adt::kChunksPerSide;
    out.chunkGridY = chunkIndex / adt::kChunksPerSide;
    out.liquidType = canon::Ref{canon::Db2Row{"LiquidType", inst.liquidType}, "", canon::NameSource::None};
    if (inst.liquidObjectOrLvf >= kFirstLiquidObjectId) {
        out.liquidObject =
            canon::Ref{canon::Db2Row{"LiquidObject", inst.liquidObjectOrLvf}, "", canon::NameSource::None};
    }
    out.quadX = inst.xOffset;
    out.quadY = inst.yOffset;
    out.width = inst.width;
    out.height = inst.height;

    size_t quadCount = static_cast<size_t>(inst.width) * inst.height;
    out.quadExists.assign(quadCount, 1);
    if (!inst.existsBitmap.empty()) {
        for (size_t q = 0; q < quadCount; ++q) out.quadExists[q] = (inst.existsBitmap[q / 8] >> (q % 8)) & 1;
    }

    size_t vertexCount = static_cast<size_t>(inst.width + 1) * (inst.height + 1);
    if (!inst.vertexFormat) {
        out.heightSource = canon::LiquidHeightSource::MinHeightLevel;
        out.heights.assign(vertexCount, inst.minHeight);
    } else if (*inst.vertexFormat == adt::LiquidVertexFormat::DepthOnly) {
        out.heightSource = canon::LiquidHeightSource::Zero;
        out.heights.assign(vertexCount, 0.0f);
    } else {
        out.heightSource = canon::LiquidHeightSource::Heightmap;
        out.heights = inst.vertexHeights;
    }
    if (!inst.vertexDepths.empty()) {
        std::vector<float> depth;
        for (uint8_t d : inst.vertexDepths) depth.push_back(static_cast<float>(d) / 255.0f);
        out.depth = std::move(depth);
    }
    if (!inst.vertexUvs.empty()) {
        std::vector<canon::Vec2> uv;
        for (const auto& [u, v] : inst.vertexUvs) uv.push_back({static_cast<float>(u) / 8.0f, static_cast<float>(v) / 8.0f});
        out.uv = std::move(uv);
    }
    return out;
}

// MDDF/MODF store position as (west-offset, height, north-offset) measured
// from the map's north-west corner. Verified for MDDF against terrain height
// (findings doc); MODF uses the same documented transform, not separately verified.
canon::Vec3 placementPosition(const std::array<float, 3>& p) {
    return {kMapHalfExtent - p[2], kMapHalfExtent - p[0], p[1]};
}

using Mat3 = std::array<std::array<double, 3>, 3>;

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) out[i][j] += a[i][k] * b[k][j];
        }
    }
    return out;
}

Mat3 axisRotation(int axis, double degrees) {
    double r = degrees * 3.14159265358979323846 / 180.0;
    double c = std::cos(r);
    double s = std::sin(r);
    if (axis == 0) return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
    if (axis == 1) return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

canon::Quat toQuat(const Mat3& m) {
    double trace = m[0][0] + m[1][1] + m[2][2];
    double x, y, z, w;
    if (trace > 0) {
        double s = std::sqrt(trace + 1.0) * 2;
        w = 0.25 * s;
        x = (m[2][1] - m[1][2]) / s;
        y = (m[0][2] - m[2][0]) / s;
        z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        double s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2;
        w = (m[2][1] - m[1][2]) / s;
        x = 0.25 * s;
        y = (m[0][1] + m[1][0]) / s;
        z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        double s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2;
        w = (m[0][2] - m[2][0]) / s;
        x = (m[0][1] + m[1][0]) / s;
        y = 0.25 * s;
        z = (m[1][2] + m[2][1]) / s;
    } else {
        double s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2;
        w = (m[1][0] - m[0][1]) / s;
        x = (m[0][2] + m[2][0]) / s;
        y = (m[1][2] + m[2][1]) / s;
        z = 0.25 * s;
    }
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), static_cast<float>(w)};
}

// ADT/v18.md MDDF createPlacementMatrix, rotation part. Its leading
// Rx(90)*Ry(90) maps the placement frame onto exactly this module's world
// frame, so the product is world-from-model directly. Tilt convention
// checked against terrain slope (findings doc); yaw sign is wiki-only.
canon::Quat placementRotation(const std::array<float, 3>& r) {
    Mat3 m = multiply(axisRotation(0, 90), axisRotation(1, 90));
    m = multiply(m, axisRotation(1, r[1] - 270.0));
    m = multiply(m, axisRotation(2, -r[0]));
    m = multiply(m, axisRotation(0, r[2] - 90.0));
    return toQuat(m);
}

// A name-table path the listfile resolved becomes a FileDataID (named later,
// from the listfile, like every FileDataID-flagged placement); one it did
// not keeps the raw path as its only name.
canon::Ref pathRef(const std::string& path, const Resolutions& resolutions) {
    auto it = resolutions.gamePaths.find(path);
    if (it != resolutions.gamePaths.end()) return canon::Ref{canon::FileDataId{it->second}, "", canon::NameSource::None};
    return canon::Ref{canon::None{}, path, canon::NameSource::AdtEmbedded};
}

// The parser already checked a name-table nameId against its table.
canon::Ref assetRef(uint32_t nameId, bool isFileDataId, const std::vector<std::string>& names,
                    const Resolutions& resolutions) {
    if (isFileDataId) return canon::Ref{canon::FileDataId{nameId}, "", canon::NameSource::None};
    return pathRef(names.at(nameId), resolutions);
}

canon::Placement buildDoodad(const adt::DoodadPlacement& d, const adt::ObjFile& obj, const Resolutions& resolutions) {
    canon::Placement out;
    out.kind = canon::Placement::Kind::Model;
    out.asset = assetRef(d.nameId, d.flags & adt::kMddfNameIsFileDataId, obj.doodadNames, resolutions);
    out.uniqueId = d.uniqueId;
    out.position = placementPosition(d.position);
    out.rotation = placementRotation(d.rotation);
    out.scale = static_cast<float>(d.scale) / 1024.0f;
    out.flags = d.flags;
    return out;
}

constexpr uint16_t kModfHasScale = 0x4;

canon::Placement buildMapObject(const adt::MapObjectPlacement& m, const adt::ObjFile& obj,
                                const Resolutions& resolutions) {
    canon::Placement out;
    out.kind = canon::Placement::Kind::MapObject;
    out.asset = assetRef(m.nameId, m.flags & adt::kModfNameIsFileDataId, obj.mapObjectNames, resolutions);
    out.uniqueId = m.uniqueId;
    out.position = placementPosition(m.position);
    out.rotation = placementRotation(m.rotation);
    out.scale = (m.flags & kModfHasScale) ? static_cast<float>(m.scale) / 1024.0f : 1.0f;
    out.flags = m.flags;
    out.doodadSet = m.doodadSet;
    return out;
}

void checkTileCoordinates(const adt::MapChunk& first, uint32_t tileX, uint32_t tileY) {
    float expectedY = kMapHalfExtent - static_cast<float>(tileX) * canon::kTerrainTileSize;
    float expectedX = kMapHalfExtent - static_cast<float>(tileY) * canon::kTerrainTileSize;
    if (std::abs(first.position[0] - expectedX) > 0.5f || std::abs(first.position[1] - expectedY) > 0.5f) {
        throw std::runtime_error("tile (" + std::to_string(tileX) + "," + std::to_string(tileY) +
                                 ") from file name: expected MCNK #0 at world (" + std::to_string(expectedX) + "," +
                                 std::to_string(expectedY) + "), got (" + std::to_string(first.position[0]) + "," +
                                 std::to_string(first.position[1]) + ")");
    }
}

canon::TextureRef textureFor(uint32_t fdid, const Resolutions& resolutions) {
    auto it = resolutions.textures.find(fdid);
    if (it != resolutions.textures.end()) return it->second;
    canon::TextureRef ref;
    ref.state = canon::TextureRef::State::KnownUnresolved;
    ref.unresolvedReason = "FileDataID " + std::to_string(fdid) + " not resolved by the exporter";
    return ref;
}

canon::TextureRef textureForPath(const std::string& path, const Resolutions& resolutions) {
    auto it = resolutions.gamePaths.find(path);
    if (it != resolutions.gamePaths.end()) return textureFor(it->second, resolutions);
    canon::TextureRef ref;
    ref.state = canon::TextureRef::State::KnownUnresolved;
    ref.unresolvedReason = "MTEX path not in listfile: " + path;
    return ref;
}

// A ground texture tiles 8x across a chunk. That base is a renderer
// convention with no field on disk; MTXP's texture_scale (flags bits 4..7)
// divides it by 1 << texture_scale, and a tile without MTXP uses the base.
constexpr float kBaseRepeatsPerChunk = 8.0f;
constexpr uint32_t kTextureScaleShift = 4;
constexpr uint32_t kTextureScaleMask = 0xF;

std::vector<canon::TerrainTexture> buildTextures(const adt::TexFile& tex, const Resolutions& resolutions) {
    std::vector<canon::TerrainTexture> out;
    for (size_t i = 0; i < tex.textureCount(); ++i) {
        canon::TerrainTexture t;
        t.diffuse = tex.diffuseIds.empty() ? textureForPath(tex.diffuseNames[i], resolutions)
                                           : textureFor(tex.diffuseIds[i], resolutions);
        if (!tex.heightIds.empty() && tex.heightIds[i] != 0) t.height = textureFor(tex.heightIds[i], resolutions);
        if (tex.params) {
            if (tex.params->size() != tex.textureCount()) {
                throw std::runtime_error("MTXP: expected " + std::to_string(tex.textureCount()) +
                                         " entries (one per texture), got " + std::to_string(tex.params->size()));
            }
            const adt::TextureParams& p = (*tex.params)[i];
            uint32_t scale = 1u << ((p.flags >> kTextureScaleShift) & kTextureScaleMask);
            t.repeatsPerChunk = kBaseRepeatsPerChunk / static_cast<float>(scale);
            t.heightScale = p.heightScale;
            t.heightOffset = p.heightOffset;
        }
        out.push_back(std::move(t));
    }
    return out;
}

// predominantTexture is uint2_t[8][8]; taken LSB-first, row-major (unverified).
std::array<uint8_t, 64> unpackDominantLayer(const std::array<uint8_t, 16>& packed) {
    std::array<uint8_t, 64> out{};
    for (size_t i = 0; i < out.size(); ++i) out[i] = (packed[i / 4] >> ((i % 4) * 2)) & 0x3;
    return out;
}

struct LayerContext {
    bool eightBitAlpha = false;
    std::vector<canon::GroundEffect>& groundEffects;
    std::unordered_map<uint32_t, uint32_t>& groundEffectSlot;
    const Resolutions& resolutions;
};

uint32_t groundEffectIndex(uint32_t effectId, LayerContext& ctx) {
    auto [it, inserted] = ctx.groundEffectSlot.emplace(effectId, static_cast<uint32_t>(ctx.groundEffects.size()));
    if (!inserted) return it->second;
    auto def = ctx.resolutions.groundEffects.find(effectId);
    if (def != ctx.resolutions.groundEffects.end()) {
        ctx.groundEffects.push_back(def->second);
    } else {
        ctx.groundEffects.push_back({canon::Ref{canon::Db2Row{"GroundEffectTexture", effectId}, "", canon::NameSource::None}});
    }
    return it->second;
}

std::vector<canon::TerrainLayer> buildLayers(const adt::TexChunk& tc, const adt::MapChunk& mc, LayerContext& ctx) {
    std::vector<canon::TerrainLayer> out;
    for (size_t i = 0; i < tc.layers.size(); ++i) {
        const adt::TextureLayer& l = tc.layers[i];
        canon::TerrainLayer layer;
        layer.textureIndex = l.textureId;
        if (l.effectId != adt::kNoGroundEffect) layer.groundEffectIndex = groundEffectIndex(l.effectId, ctx);
        layer.overbright = l.flags & adt::kLayerOverbright;
        if (l.flags & adt::kLayerAnimationEnabled) {
            layer.animation = canon::TerrainLayerAnimation{
                static_cast<float>(l.flags & adt::kLayerAnimationRotationMask) * 45.0f,
                (l.flags >> adt::kLayerAnimationSpeedShift) & adt::kLayerAnimationSpeedMask};
        }
        if (i > 0) {
            if (!(l.flags & adt::kLayerUseAlphaMap)) {
                throw std::runtime_error("MCLY layer " + std::to_string(i) + " of MCNK (" + std::to_string(mc.indexX) +
                                         "," + std::to_string(mc.indexY) +
                                         "): expected use_alpha_map (0x100) on every layer after the first, got flags " +
                                         std::to_string(l.flags));
            }
            auto alpha = adt::decodeAlphaMap(tc.mcal, l, ctx.eightBitAlpha, mc.flags & adt::kMcnkDoNotFixAlphaMap);
            layer.alpha.assign(alpha.begin(), alpha.end());
        }
        out.push_back(std::move(layer));
    }
    return out;
}

}  // namespace

canon::Terrain buildCanonTerrain(const TileSources& sources, const Resolutions& resolutions) {
    const adt::RootFile& root = sources.root;
    checkTileCoordinates(root.chunks.front(), sources.tileX, sources.tileY);
    canon::Terrain out;
    out.map = canon::Ref{canon::None{}, sources.mapName, canon::NameSource::Listfile};
    out.tileX = sources.tileX;
    out.tileY = sources.tileY;
    for (const adt::MapChunk& mc : root.chunks) out.chunks.push_back(buildChunk(mc));

    if (sources.tex) {
        if (!sources.wdtFlags) {
            throw std::runtime_error("texture layers need the map's WDT MPHD flags to pick the alpha-map format");
        }
        out.textures = buildTextures(*sources.tex, resolutions);
        std::unordered_map<uint32_t, uint32_t> slots;
        LayerContext ctx{(*sources.wdtFlags & (adt::kMphdBigAlpha | adt::kMphdHeightTexturing)) != 0,
                         out.groundEffects, slots, resolutions};
        for (size_t ci = 0; ci < out.chunks.size(); ++ci) {
            out.chunks[ci].layers = buildLayers(sources.tex->chunks[ci], root.chunks[ci], ctx);
            out.chunks[ci].dominantLayer = unpackDominantLayer(root.chunks[ci].predominantTexture);
            out.chunks[ci].groundEffectSuppressedRows = root.chunks[ci].noEffectDoodad;
        }
    }

    if (root.liquid) {
        for (uint32_t ci = 0; ci < adt::kChunkCount; ++ci) {
            for (const adt::LiquidInstance& inst : (*root.liquid)[ci].instances) out.liquids.push_back(buildLiquid(inst, ci));
        }
    }
    if (sources.obj) {
        for (const adt::DoodadPlacement& d : sources.obj->doodads) {
            out.placements.push_back(buildDoodad(d, *sources.obj, resolutions));
        }
        for (const adt::MapObjectPlacement& m : sources.obj->mapObjects) {
            out.placements.push_back(buildMapObject(m, *sources.obj, resolutions));
        }
    }
    return out;
}

std::vector<uint32_t> textureFileDataIds(const adt::TexFile& tex, const GamePathResolutions& gamePaths) {
    std::vector<uint32_t> out;
    std::unordered_set<uint32_t> seen;
    auto add = [&](uint32_t fdid) {
        if (fdid != 0 && seen.insert(fdid).second) out.push_back(fdid);
    };
    for (uint32_t fdid : tex.diffuseIds) add(fdid);
    for (const std::string& name : tex.diffuseNames) {
        auto it = gamePaths.find(name);
        if (it != gamePaths.end()) add(it->second);
    }
    for (uint32_t fdid : tex.heightIds) add(fdid);
    return out;
}

GroundEffectDefinitions groundEffectDefinitions(const groundeffect::Data& data) {
    GroundEffectDefinitions out;
    for (const groundeffect::TextureRow& row : data.textures) {
        canon::GroundEffect effect;
        effect.ref = canon::Ref{canon::Db2Row{"GroundEffectTexture", row.id}, "", canon::NameSource::None};
        effect.density = row.density;
        for (const groundeffect::DoodadSlot& slot : row.doodads) {
            canon::GroundEffectDoodad d;
            d.doodad = canon::Ref{canon::Db2Row{"GroundEffectDoodad", slot.doodadId}, "", canon::NameSource::None};
            d.weight = slot.weight;
            auto found = data.doodads.find(slot.doodadId);
            if (found != data.doodads.end()) {
                d.model = canon::Ref{canon::FileDataId{found->second.modelFileId}, "", canon::NameSource::None};
                d.flags = found->second.flags;
            }
            effect.doodads.push_back(std::move(d));
        }
        out.emplace(row.id, std::move(effect));
    }
    return out;
}

std::vector<std::string> gamePaths(const std::optional<adt::ObjFile>& obj, const std::optional<adt::TexFile>& tex) {
    std::vector<std::string> out;
    if (obj) {
        out.insert(out.end(), obj->doodadNames.begin(), obj->doodadNames.end());
        out.insert(out.end(), obj->mapObjectNames.begin(), obj->mapObjectNames.end());
    }
    if (tex) out.insert(out.end(), tex->diffuseNames.begin(), tex->diffuseNames.end());
    return out;
}

}  // namespace husk::adtinput
