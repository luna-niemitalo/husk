#pragma once

#include <vector>

#include "canon_physics.hpp"
#include "canon_skeleton.hpp"
#include "json_writer.hpp"

// The model document's resources.physics (the .phys graph) and
// resources.rig.correction_sets (the .bone files). Both are data only:
// nothing in the bundle applies them.
namespace husk::writers {

void writePhysics(json::Writer& w, const canon::Physics& physics);
void writeCorrectionSets(json::Writer& w, const std::vector<canon::CorrectionSet>& sets);

}  // namespace husk::writers
