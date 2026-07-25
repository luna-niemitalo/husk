#include <algorithm>
#include <iostream>
#include <vector>

#include "commands.hpp"
#include "m2.hpp"

namespace husk::commands {

namespace {

// Every top-level M2 chunk tag documented on wowdev.wiki/M2#Chunks as of
// this fetch (2026-07-25) -- 30 of them, spanning client build 7.0.1.20740
// through an unreleased 12.0.0.63967, whether or not husk actually parses
// that chunk's contents (only MD21/SKID/TXID/SFID/LDV1/BFID/AFID are read;
// the rest are just recognized). New chunks have shown up at a steady clip
// since Legion --
// see README.md's Design notes for the recurring shapes they take (mostly:
// a formerly-filename-based reference replaced by a FileDataID chunk, or a
// small additive chunk tied to one specific new rendering feature) -- so
// this list *will* go stale again. That's the point of tracking it
// explicitly rather than not at all: a tag turning up in a real file that
// isn't even in this list is a strong, specific signal that the format
// moved, surfaced right here instead of silently doing nothing (see
// isUndocumentedChunkTag below).
const std::vector<std::string>& documentedM2ChunkTags() {
    static const std::vector<std::string> tags = {
        "MD21", "PFID", "SFID", "AFID", "BFID", "TXAC", "EXPT", "EXP2", "PABC", "PADC",
        "PSBC", "PEDC", "SKID", "TXID", "LDV1", "RPID", "GPID", "WFV1", "WFV2", "PGD1",
        "WFV3", "PFDC", "EDGF", "NERF", "DETL", "DBOC", "AFRA", "PCOL", "DPIV", "TEXL",
    };
    return tags;
}

bool isUndocumentedChunkTag(const std::string& tag) {
    const auto& known = documentedM2ChunkTags();
    return std::find(known.begin(), known.end(), tag) == known.end();
}

void printUsage() {
    std::cerr << "usage: husk info <file.m2>\n"
                 "\n"
                 "Parses an M2 model's header and prints what was found:\n"
                 "magic/version/name, whether it's Legion+ chunked, and the\n"
                 "record counts (bones, vertices, textures, ...) from the\n"
                 "header's M2Array fields.\n";
}

void printArray(const char* label, const m2::Array& a) {
    std::cout << "  " << label << ": " << a.count << " (offset 0x" << std::hex << a.offset
               << std::dec << ")\n";
}

void printVec3(const m2::Vec3& v) {
    std::cout << "(" << v.x << ", " << v.y << ", " << v.z << ")";
}

}  // namespace

int info(int argc, char** args) {
    if (argc != 1) {
        printUsage();
        return 1;
    }

    std::string path = args[0];
    m2::Header h;
    try {
        h = m2::loadFile(path);
    } catch (const std::exception& e) {
        // Catches m2::ParseError (a malformed-but-readable file) and
        // anything else that can escape loadFile -- e.g. husk::ChunkError
        // from a garbage/malformed chunked file, or std::ios_base::failure
        // from a genuine OS-level read error (a directory path, a special
        // file, ...). Narrower catches here have crashed the whole process
        // with an unhandled-exception abort instead of a clean message; see
        // FAILURES.md #1.
        std::cerr << "husk: couldn't read '" << path << "': " << e.what() << "\n";
        return 1;
    }

    std::cout << path << "\n";
    std::cout << "  format: " << (h.chunked ? "Legion+ chunked (MD21-wrapped)" : "pre-Legion (flat MD20)")
               << "\n";
    std::cout << "  version: " << h.version << " (" << m2::expansionForVersion(h.version) << ")\n";
    std::cout << "  name: " << (h.name.empty() ? "(empty)" : h.name) << "\n";
    std::cout << "  global_flags: 0x" << std::hex << h.globalFlags << std::dec << "\n";

    printArray("sequences", h.sequences);
    printArray("bones", h.bones);
    if (h.bones.count == 0 && h.skeletonFileId) {
        std::cout << "    note: 0 inline bones, but this model has an external skeleton"
                      " (SKID file data ID " << *h.skeletonFileId
                  << ") -- pass its .skel path to `husk export`'s optional 4th argument\n";
    }
    printArray("vertices", h.vertices);
    printArray("textures", h.textures);
    printArray("materials", h.materials);
    std::cout << "  num_skin_profiles: " << h.numSkinProfiles << "\n";

    if (h.skinFileDataIds) {
        std::cout << "  skin_file_data_ids: ";
        for (size_t i = 0; i < h.skinFileDataIds->size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << (*h.skinFileDataIds)[i];
        }
        std::cout << " (entry 0 = highest-detail LOD -- see `husk export`'s 'auto' + "
                     "--skin-dir)\n";
    }
    if (h.lodCount) {
        std::cout << "  lod_count: " << *h.lodCount << "\n";
    }
    if (h.boneFileDataIds && !h.boneFileDataIds->empty()) {
        std::cout << "  bone_file_data_ids: " << h.boneFileDataIds->size()
                  << " (.bone sidecars -- per-bone animation track data, not yet resolved by "
                     "husk, see README.md roadmap stage 6)\n";
    }
    if (h.animFileIds && !h.animFileIds->empty()) {
        std::cout << "  anim_file_ids: " << h.animFileIds->size()
                  << " (.anim sidecars -- animation track data, not yet resolved by husk, see "
                     "README.md roadmap stage 6)\n";
    }

    if (!h.chunkTags.empty()) {
        std::cout << "  chunks: ";
        for (size_t i = 0; i < h.chunkTags.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << h.chunkTags[i];
        }
        std::cout << "\n";

        std::vector<std::string> undocumented;
        for (const auto& tag : h.chunkTags) {
            if (isUndocumentedChunkTag(tag)) undocumented.push_back(tag);
        }
        if (!undocumented.empty()) {
            std::cout << "    note: " << undocumented.size()
                      << " chunk tag(s) not in husk's known M2 chunk list (";
            for (size_t i = 0; i < undocumented.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << undocumented[i];
            }
            std::cout << ") -- the M2 format may have grown a new chunk since that list was"
                         " last updated; check wowdev.wiki/M2#Chunks and see README.md's"
                         " Design notes\n";
        }
    }

    std::cout << "  bounding_box: min=";
    printVec3(h.boundingBox.min);
    std::cout << " max=";
    printVec3(h.boundingBox.max);
    std::cout << "\n";
    std::cout << "  bounding_sphere_radius: " << h.boundingSphereRadius << "\n";

    return 0;
}

}  // namespace husk::commands
