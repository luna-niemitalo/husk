#pragma once

#include <cstddef>
#include <filesystem>

// An export tree's lookup tables (REFACTOR/CORPUS_EXPORT.md "index/"): how a
// game id finds the bundle that holds it. Derived from the tree's own
// documents, so they can be rebuilt at any time; TSV, not documents.
//
//   index/creature_displays.tsv   display  model  preset
//       a CreatureDisplayInfo row -> the model manifest and the preset
//       naming it among its sources
//   index/chr_models.tsv          chr_model  model
//       a ChrModel row -> each model manifest whose customization it draws
namespace husk::corpus {

struct IndexResult {
    size_t manifests = 0;
    size_t creatureDisplays = 0;
    size_t chrModels = 0;
};

IndexResult buildIndex(const std::filesystem::path& tree);

}  // namespace husk::corpus
