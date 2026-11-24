#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// An export tree's lookup tables (REFACTOR/CORPUS_EXPORT.md "index/"): how a
// game id finds the bundle that holds it. Derived from the tree's own
// documents, so they can be rebuilt at any time; TSV, not documents.
//
//   index/creature_displays.tsv   display  model  preset_id
//       a CreatureDisplayInfo row -> the model manifest and the id of the
//       preset naming it among its sources
//   index/chr_models.tsv          chr_model  model
//       a ChrModel row -> each model manifest whose customization it draws
namespace husk::corpus {

struct IndexResult {
    size_t manifests = 0;
    size_t creatureDisplays = 0;
    size_t chrModels = 0;
};

IndexResult buildIndex(const std::filesystem::path& tree);

struct CreatureDisplayRow {
    uint32_t display = 0;
    std::string model;  // the model manifest, relative to the tree
    std::string preset;  // the preset's id in that manifest's selection.presets
};

// IN, critical: the tree's index/creature_displays.tsv as buildIndex wrote
// it. Throws std::runtime_error when the file is missing (index-tree has
// not run) or a line isn't `display<TAB>model<TAB>preset_id`, naming the line
// with expected and actual.
std::vector<CreatureDisplayRow> readCreatureDisplays(const std::filesystem::path& tree);

}  // namespace husk::corpus
