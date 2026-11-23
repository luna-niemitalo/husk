#pragma once

#include <filesystem>

#include "canon_model.hpp"

// husk::writers: the lean glTF projection of canon::Model (`husk export
// --glb`), the only glTF husk writes: just the real geometry/skin/
// animation/material data, built on gltf_math.hpp, gltf_buffer_utils.hpp,
// writer_common.hpp and tinygltf.
//
// Deliberate scope cut, stated once here rather than at each omission site:
// no extras of any kind are ever written (no geoset_id, no
// bone_correction_sets, no chr_texture_layout, ...) -- that data belongs to
// the model bundle instead. What doesn't fit a real core glTF field is left
// out, not smuggled back in via extras.
namespace husk::writers {

// Writes `model` to a real, spec-valid .glb at `outputPath`. Throws
// std::runtime_error on an out-of-range internal index (mirrors
// writers::localBindTranslation/composeJointCurves's own throwing
// convention) or when tinygltf fails to write the output stream.
void writeLeanGlb(const canon::Model& model, const std::filesystem::path& outputPath);

}  // namespace husk::writers
