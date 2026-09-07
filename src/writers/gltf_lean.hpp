#pragma once

#include <filesystem>

#include "canon_model.hpp"

// husk::writers: the lean glTF projection -- proof that canon::Model can
// feed a genuinely independent writer, and that a "just the real geometry/
// skin/animation/material data, nothing else" glTF is both leaner and more
// strictly spec-compliant than the existing extras-heavy exporter
// (src/gltf.hpp/.cpp, src/gltf_mesh.*, src/gltf_skeleton.*). Deliberately
// does not reuse or depend on any of those files (see this task's own
// brief) -- only the pure, extras-free math (gltf_math.hpp,
// gltf_buffer_utils.hpp, writer_common.hpp) and tinygltf itself.
//
// Deliberate scope cut, stated once here rather than at each omission site:
// no extras of any kind are ever written (no geoset_id, no
// bone_correction_sets, no chr_texture_layout, ...) -- that data belongs to
// the sibling native bundle writer's manifest instead. What doesn't fit a
// real core glTF field is left out, not smuggled back in via extras -- that
// omission IS the leanness claim this writer exists to prove.
namespace husk::writers {

// Writes `model` to a real, spec-valid .glb at `outputPath`. Throws
// std::runtime_error on an out-of-range internal index (mirrors
// writers::localBindTranslation/composeJointCurves's own throwing
// convention) or when tinygltf fails to write the output stream.
void writeLeanGlb(const canon::Model& model, const std::filesystem::path& outputPath);

}  // namespace husk::writers
