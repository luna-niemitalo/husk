#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "canon_item.hpp"
#include "creature_display_db2.hpp"
#include "listfile_index.hpp"
#include "skin_resolution.hpp"  // LodSkinPath
#include "writers/texture_store.hpp"
#include "m2.hpp"
#include "m2_material_input.hpp"  // m2input::TextureResolutions
#include "skin.hpp"
#include "sources/catalog.hpp"

// husk::commands: `husk export --export-canon`'s own path -- REFACTOR/
// README.md stage 3's runtime convergence gate. See cmd_export_canon.cpp's
// doc comment for the full rationale; this header only declares the one
// entry point cmd_export.cpp calls.
namespace husk::commands {

// One catalog.texture() call per distinct M2 texture-array index a batch
// layer or an emitter references (layer i's combo index is
// `textureComboIndex + i`). An index with no FileDataID but an embedded
// filename (pre-Cataclysm inline names) is left out: canon::TextureRef has
// no shape for it, so it stays KnownUnresolved.
m2input::TextureResolutions buildTextureResolutions(const m2::Model& model, const std::vector<skin::Batch>& batches,
                                                   sources::Catalog& catalog, const std::string& modelPath,
                                                   uint32_t objectSkinTextureFileDataId);

// What a model bundle takes beyond the M2 itself, resolved by the caller.
struct BundleSidecars {
    std::string physPath;  // the .phys to read; empty = none
    std::string bonesDir;  // tried first for '<fdid>.bone'; the listfile/store follow
    const ListfileIndex* listfile = nullptr;
    // --appearance: its customization choices and gear become one preset.
    // `gearModelUris[i]` is gear[i]'s exported model bundle manifest,
    // relative to the bundle; empty = not exported.
    std::string appearance;
    std::vector<uint32_t> appearanceChoiceIds;
    std::vector<canon::Item> gear;
    std::vector<std::string> gearModelUris;
    std::string leanGlbPath;  // --glb: also write the lean glTF projection here; empty = none
    bool noAnimation = false;  // --anim none: drop every clip
};

// One model bundle at `bundleDir`: the M2 assembled with the first skin
// tier, its mesh levels from `lodSkins` (resolveLodSkinPaths), and the
// sidecars. Throws on failure.
void writeCanonBundleOnly(const m2::Model& model, const std::string& skinPath,
                          const std::vector<LodSkinPath>& lodSkins, husk::sources::Catalog& catalog,
                          const std::string& modelPath, std::optional<uint32_t> modelFileDataId,
                          uint32_t objectSkinTextureFileDataId,
                          const std::string& animDir, const std::string& listfileRoot, bool bonesAreInline, bool haveSkel,
                          const std::vector<uint8_t>& skelBytes, const std::string& bundleDir,
                          const creaturedisplay::Data* creatures = nullptr,
                          const writers::StoreLink* textureStore = nullptr, const BundleSidecars& sidecars = {});

}  // namespace husk::commands
