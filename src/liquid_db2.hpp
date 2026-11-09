#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Typed reader for the liquid tables: LiquidObject (an MH2O instance's
// object: flow and its LiquidType), LiquidType (name, flags, material,
// texture slots), LiquidMaterial (the vertex format) and LiquidTypeXTexture
// (every slot's frames as FileDataIDs). Consumed by adtinput/wmoinput
// through liquidTypeDefinitions; same db2table.hpp-backed pattern as
// groundeffect_db2.hpp.
namespace husk::liquiddb {

struct ObjectRow {
    uint32_t liquidType = 0;
    float flowDirection = 0;
    float flowSpeed = 0;
    uint32_t fishable = 0;
    uint32_t reflection = 0;
};

struct MaterialRow {
    uint32_t vertexFormat = 0;  // LVF: the MH2O/MLIQ vertex layout, 0..3
    uint32_t flags = 0;
};

struct TextureSlot {
    std::vector<uint32_t> frames;  // FileDataIDs in animation order; 0 = a procedural texture
    int32_t kind = -1;             // LiquidTypeXTexture.Type of its first frame, as stored
};

// An FFTTile row: an ocean-wave spectrum. WoWDBDefs names none of its
// columns yet (WIKI_FINDINGS/FOREVER.md), so they stay raw.
struct WaveSpectrumRow {
    uint32_t field0 = 0;           // u32; 16..512 in 1.60.1, likely the FFT grid size
    std::array<float, 9> values{};  // fields 1..9
};

struct TypeRow {
    std::string name;
    uint32_t flags = 0;
    uint32_t materialId = 0;
    std::vector<TextureSlot> slots;
    std::vector<uint32_t> waveSpectra;  // FFTTile rows, in LiquidTypeXFFTTile ID order
};

struct Data {
    std::unordered_map<uint32_t, ObjectRow> objects;
    std::unordered_map<uint32_t, TypeRow> types;
    std::unordered_map<uint32_t, MaterialRow> materials;
    std::unordered_map<uint32_t, WaveSpectrumRow> waveSpectra;  // by FFTTile ID
};

// Loads liquidobject/liquidtype/liquidmaterial/liquidtypextexture.db2 from
// `db2Dir`, and fftTile/liquidtypexffttile.db2 when both exist (WoW
// Forever 1.60.1, retail 12.1.7+). nullopt, with the reason on `err`,
// when a required file or its layout can't be read; unreadable wave
// tables are reported on `err` and leave every type without waves.
// Throws std::runtime_error on rows that contradict the tables' own
// shape: a row without an ID, a type whose texture rows don't fill its
// slots (see liquid_db2.cpp), or a type naming an FFTTile row that
// doesn't exist.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::liquiddb
