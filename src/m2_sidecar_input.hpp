#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "canon_physics.hpp"
#include "canon_skeleton.hpp"
#include "listfile_index.hpp"

// The M2's physics and bone-correction sidecars as canon data.
namespace husk::m2input {

// IN (critical): the .phys at `physPath`. Every body's bone must exist
// (`jointCount`); a body naming a missing bone throws, naming file, body,
// bone and joint count.
canon::Physics loadPhysics(const std::string& physPath, size_t jointCount);

// IN (non-critical): the .bone files `boneFileDataIds` names (the model's
// or .skel's BFID). Each is looked up as `<bonesDir>/<fdid>.bone`, then
// through the listfile and the store's _by_fdid; one not found is skipped
// and counted on `err`. A correction naming a missing joint throws.
std::vector<canon::CorrectionSet> loadCorrectionSets(const std::vector<uint32_t>& boneFileDataIds,
                                                     const std::string& bonesDir, const ListfileIndex& listfile,
                                                     const std::string& listfileRoot, size_t jointCount,
                                                     std::ostream& err);

}  // namespace husk::m2input
