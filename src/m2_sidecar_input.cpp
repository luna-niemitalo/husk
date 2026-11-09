#include "m2_sidecar_input.hpp"

#include <filesystem>
#include <stdexcept>

#include "bone.hpp"
#include "read_file.hpp"
#include "sources/listfile_catalog.hpp"

namespace husk::m2input {

canon::Physics loadPhysics(const std::string& physPath, size_t jointCount) {
    canon::Physics physics = phys::parse(commands::readFileBytes(physPath));
    for (size_t i = 0; i < physics.bodies.size(); ++i) {
        uint16_t bone = physics.bodies[i].boneIndex;
        if (bone >= jointCount) {
            throw std::runtime_error("'" + physPath + "' body " + std::to_string(i) + " references bone " +
                                     std::to_string(bone) + ", expected below the model's " +
                                     std::to_string(jointCount) + " joints");
        }
    }
    return physics;
}

std::vector<canon::CorrectionSet> loadCorrectionSets(const std::vector<uint32_t>& boneFileDataIds,
                                                     const std::string& bonesDir, const ListfileIndex& listfile,
                                                     const std::string& listfileRoot, size_t jointCount,
                                                     std::ostream& err) {
    std::vector<canon::CorrectionSet> sets;
    size_t missing = 0;
    for (uint32_t fdid : boneFileDataIds) {
        std::optional<std::filesystem::path> path;
        if (!bonesDir.empty()) {
            std::filesystem::path named = std::filesystem::path(bonesDir) / (std::to_string(fdid) + ".bone");
            std::error_code ec;
            if (std::filesystem::exists(named, ec)) path = named;
        }
        if (!path && !listfileRoot.empty()) path = sources::localFileForFileDataId(listfile, listfileRoot, fdid);
        if (!path) {
            ++missing;
            continue;
        }

        canon::CorrectionSet set;
        set.ref = canon::Ref{canon::FileDataId{fdid}, "", canon::NameSource::None};
        for (const bone::Correction& c : bone::parse(commands::readFileBytes(path->string()))) {
            if (c.boneIndex >= jointCount) {
                throw std::runtime_error("'" + path->string() + "' corrects bone " + std::to_string(c.boneIndex) +
                                         ", expected below the model's " + std::to_string(jointCount) + " joints");
            }
            set.corrections.push_back({c.boneIndex, c.matrix});
        }
        sets.push_back(std::move(set));
    }
    if (missing > 0) {
        err << "husk: note: " << missing << " of " << boneFileDataIds.size()
            << " .bone correction set(s) the model names weren't found locally\n";
    }
    return sets;
}

}  // namespace husk::m2input
