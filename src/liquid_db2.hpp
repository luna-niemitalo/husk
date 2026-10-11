#pragma once

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

struct TypeRow {
    std::string name;
    uint32_t flags = 0;
    uint32_t materialId = 0;
    std::vector<TextureSlot> slots;
};

struct Data {
    std::unordered_map<uint32_t, ObjectRow> objects;
    std::unordered_map<uint32_t, TypeRow> types;
    std::unordered_map<uint32_t, MaterialRow> materials;
};

// Loads liquidobject/liquidtype/liquidmaterial/liquidtypextexture.db2 from
// `db2Dir`. nullopt, with the reason on `err`, when a file or its layout
// can't be read. Throws std::runtime_error on rows that contradict the
// tables' own shape: a row without an ID, or a type whose texture rows
// don't fill its slots (see liquid_db2.cpp).
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::liquiddb
