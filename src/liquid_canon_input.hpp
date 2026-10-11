#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "canon_liquid.hpp"
#include "liquid_db2.hpp"

// husk::liquidinput: liquid DB2 rows -> the canon::LiquidType/LiquidObject
// definitions a document lists, shared by adtinput (MH2O) and wmoinput
// (MLIQ). Pure: textures arrive resolved, keyed by FileDataID.
namespace husk::liquidinput {

using TextureResolutions = std::unordered_map<uint32_t, canon::TextureRef>;

// `data` is nullptr without the liquid DB2s: the result is then identity
// only, as it is for a row the tables don't have.
canon::LiquidType buildLiquidType(uint32_t row, const liquiddb::Data* data, const TextureResolutions& textures);
canon::LiquidObject buildLiquidObject(uint32_t row, const liquiddb::Data* data);

// Every texture frame FileDataID of the `rows` types, nonzero, in first-seen
// order: what a caller resolves into `textures` before building.
std::vector<uint32_t> textureFileDataIds(const std::vector<uint32_t>& rows, const liquiddb::Data& data);

}  // namespace husk::liquidinput
