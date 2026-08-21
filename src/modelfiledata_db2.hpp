#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

// Real, typed reader for ModelFileData.db2 -- resolves a real
// ModelResourcesID (the shape ItemDisplayInfo.ModelResourcesID_0 carries,
// TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 6) to the real .m2
// FileDataID(s) that share it. Same db2table.hpp-backed thin-wrapper
// pattern as texturefiledata_db2.hpp.
//
// One ModelResourcesID legitimately owns more than one FileDataID -- real
// LOD variants of the same model, each its own file -- so this keeps every
// candidate rather than picking one, same "husk resolves, never guesses
// among real alternatives" policy cmd_db2_build.cpp's own model/
// ItemDisplayInfo ambiguity handling already established.
namespace husk::modelfiledata {

// ModelResourcesID -> every real .m2 FileDataID sharing it.
using Data = std::unordered_map<uint32_t, std::vector<uint32_t>>;

// Loads modelfiledata.db2 from `db2Dir`. Returns nullopt when the file
// can't be read/parsed or no row resolves.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::modelfiledata
