#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "canon_physics.hpp"
#include "canon_skeleton.hpp"
#include "listfile_index.hpp"
#include "m2_header.hpp"

// The M2's physics and bone-correction sidecars as canon data.
namespace husk::m2input {

// Where a model's .phys bytes came from. `bytes` empty = no physics.
// `detail` names the source for provenance and errors; `available` false
// means a source was named but couldn't be read (PFID not found locally,
// --phys none), so absent physics is unknown rather than "has none".
struct PhysicsInput {
    std::vector<uint8_t> bytes;
    std::string detail;
    bool available = true;
};

// IN (non-critical): the physics a model names, first hit wins:
// `explicitPath` (--phys), the model's PFDC chunk (an inline .phys), the
// PFID FileDataID (store `_by_fdid`, then the listfile path), then a
// same-basename .phys beside the model. `disabled` (--phys none) reads
// nothing. A PFDC whose chunk container doesn't parse throws.
PhysicsInput resolvePhysics(const std::string& modelPath, const std::vector<uint8_t>& modelBytes,
                            const m2::Header& header, const std::string& explicitPath, bool disabled,
                            const ListfileIndex& listfile, const std::string& listfileRoot);

// IN (critical): .phys bytes, named `origin` in errors. Every body's bone
// must exist (`jointCount`); a body naming a missing bone throws, naming
// origin, body, bone and joint count.
canon::Physics loadPhysics(const std::vector<uint8_t>& physBytes, const std::string& origin, size_t jointCount);

// IN (non-critical): the .bone files `boneFileDataIds` names (the model's
// or .skel's BFID). Each is looked up as `<bonesDir>/<fdid>.bone`, then
// through the listfile and the store's _by_fdid; one not found is skipped
// and counted on `err`. A correction naming a missing joint throws.
std::vector<canon::CorrectionSet> loadCorrectionSets(const std::vector<uint32_t>& boneFileDataIds,
                                                     const std::string& bonesDir, const ListfileIndex& listfile,
                                                     const std::string& listfileRoot, size_t jointCount,
                                                     std::ostream& err);

}  // namespace husk::m2input
