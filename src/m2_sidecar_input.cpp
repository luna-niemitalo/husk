#include "m2_sidecar_input.hpp"

#include <filesystem>
#include <stdexcept>

#include "bone.hpp"
#include "chunk.hpp"
#include "dump_writer_utils.hpp"  // physPayloadRealLength
#include "read_file.hpp"
#include "sources/listfile_catalog.hpp"

namespace husk::m2input {

PhysicsInput resolvePhysics(const std::string& modelPath, const std::vector<uint8_t>& modelBytes,
                            const m2::Header& header, const std::string& explicitPath, bool disabled,
                            const ListfileIndex& listfile, const std::string& listfileRoot) {
    if (disabled) return {{}, "--phys none", false};
    if (!explicitPath.empty()) return {commands::readFileBytes(explicitPath), "--phys " + explicitPath, true};

    if (header.chunked) {
        if (auto pfdc = findChunk(readChunks(modelBytes.data(), modelBytes.size()), "PFDC")) {
            // PFDC pads its .phys container to an alignment boundary; the
            // padding isn't a chunk, so it is trimmed before phys::parse.
            size_t length = commands::physPayloadRealLength(pfdc->data, pfdc->size);
            return {std::vector<uint8_t>(pfdc->data, pfdc->data + length), "PFDC chunk", true};
        }
    }

    bool pfidMissing = false;
    if (header.physFileId && !listfileRoot.empty()) {
        uint32_t fdid = *header.physFileId;
        std::optional<std::filesystem::path> path = sources::storePathForFileDataId(listfileRoot, fdid);
        if (!path) path = sources::localFileForFileDataId(listfile, listfileRoot, fdid);
        if (path) return {commands::readFileBytes(path->string()), "PFID " + std::to_string(fdid) + ": " + path->string(), true};
        pfidMissing = true;
    }

    std::filesystem::path beside = std::filesystem::path(modelPath).replace_extension(".phys");
    std::error_code ec;
    if (std::filesystem::exists(beside, ec)) return {commands::readFileBytes(beside.string()), "beside the model: " + beside.string(), true};

    if (header.physFileId) {
        std::string why = pfidMissing ? " not found locally" : " not looked up (no --listfile-root)";
        return {{}, "PFID " + std::to_string(*header.physFileId) + why, false};
    }
    return {{}, "the model names no physics (no PFDC, no PFID, no .phys beside it)", true};
}

canon::Physics loadPhysics(const std::vector<uint8_t>& physBytes, const std::string& origin, size_t jointCount) {
    canon::Physics physics = phys::parse(physBytes);
    for (size_t i = 0; i < physics.bodies.size(); ++i) {
        uint16_t bone = physics.bodies[i].boneIndex;
        if (bone >= jointCount) {
            throw std::runtime_error("physics (" + origin + ") body " + std::to_string(i) + " references bone " +
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
