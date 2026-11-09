#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>

#include "chrrace_db2.hpp"
#include "listfile_index.hpp"
#include "sources/catalog.hpp"

// The catalog's DB2-character texture tier input (sources::
// CharacterTextureContext), resolved straight from the DB2 tables: the
// model's ChrModel (given or derived), its texture layout's single-layer
// texture types, and the selected customization choices' textures.
namespace husk::chartexture {

// The export flags this reads, raw. Empty = not given; "none"/"auto" as
// the flags define them.
struct Args {
    std::string db2Dir;
    std::string dbdDir;
    std::string charLayoutId;   // --char-layout-id
    std::string chrModelId;     // --chr-model-id: auto | none | <id>
    std::string choiceIds;      // --customization-choice-ids, comma-separated
};

// The model's ChrModelID: through its FileDataID (CreatureModelData ->
// CreatureDisplayInfo -> ChrModel) when the listfile knows the file, else
// from a `<race><male|female>[_hd]` basename. nullopt, with the reason on
// `err`, when neither answers. `raceDataOut` receives the tables loaded.
std::optional<uint32_t> deriveChrModelId(const std::string& db2Dir, const std::string& dbdDir,
                                         const std::string& modelPath, const ListfileIndex& listfile,
                                         const std::string& listfileRoot, std::ostream& err,
                                         std::optional<chrrace::Data>* raceDataOut = nullptr);

// nullopt when the feature is unused (no DB2 flags at all) or nothing
// resolves; each half (layout, choices) is skipped on its own failure,
// reported on `err`.
std::optional<sources::CharacterTextureContext> build(const Args& args, const std::string& modelPath,
                                                      const ListfileIndex& listfile,
                                                      const std::string& listfileRoot, std::ostream& err);

}  // namespace husk::chartexture
