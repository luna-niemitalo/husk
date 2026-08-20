#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>

// Real, typed reader for TextureFileData.db2 -- resolves a real
// MaterialResourcesID (the shape ChrCustomizationMaterial.MaterialResourcesID
// and ItemDisplayInfoModelMatRes.MaterialResourcesID both carry) to the real
// texture FileDataID it names. Same db2table.hpp-backed thin-wrapper pattern
// as chrmodel_db2.hpp/chrcustomization_db2.hpp.
//
// Real wrinkle, confirmed against reference/wow.export's own
// DBCharacterCustomization.js (`tfd_map.set(tfd_row.MaterialResourcesID,
// tfd_row.FileDataID)` guarded by `if (tfd_row.UsageType != 0) continue`):
// a single MaterialResourcesID can own more than one TextureFileData row,
// each tagged with a different real UsageType -- only UsageType == 0 is the
// base-skin texture this reader's callers want (TODO/
// CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 3 material chain); other
// UsageType values are real but out of scope here (e.g. specular/other
// texture kinds the client also derives from the same MaterialResourcesID).
// Filtered at load time, not left for the caller to filter.
namespace husk::texturefiledata {

// MaterialResourcesID -> real texture FileDataID, UsageType == 0 rows only.
// A MaterialResourcesID with no UsageType == 0 row simply isn't in this map
// -- callers treat a missing key as "not resolved," never fabricate one.
using Data = std::unordered_map<uint32_t, uint32_t>;

// Loads texturefiledata.db2 from `db2Dir`. Returns nullopt when the file
// can't be read/parsed or no row resolves (nothing to offer), same "partial
// real data is still useful, total failure only on totally empty" policy as
// chrmodel::load.
std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err);

}  // namespace husk::texturefiledata
