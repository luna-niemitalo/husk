#include <algorithm>
#include <fstream>
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

void printUsage(std::ostream& out = std::cerr) {
    out << "usage: husk info <file.m2>\n"
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

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw m2::ParseError("couldn't open '" + path + "' for reading");
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw m2::ParseError("error reading '" + path + "'");
    }
    return bytes;
}

}  // namespace

int info(int argc, char** args) {
    if (argc >= 1 && isHelpFlag(args[0])) {
        printUsage(std::cout);
        return 0;
    }
    if (argc != 1) {
        printUsage();
        return 1;
    }

    std::string path = args[0];
    m2::Header h;
    std::vector<uint8_t> blob;
    try {
        auto bytes = readFileBytes(path);
        h = m2::parseHeader(bytes);
        blob = m2::extractBlob(bytes);
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
    if (h.version < m2::kMinVerifiedRecordStrideVersion) {
        std::cerr << "husk: warning: version " << h.version
                  << " is below Wrath (264) -- this parser's bones/sequences/ribbon_emitters "
                     "record sizes are only documented and verified for Wrath+; a real "
                     "Classic/TBC file may be silently misread at the wrong byte offset rather "
                     "than failing loudly (see FAILURES2.md #3)\n";
    }
    std::cout << "  name: " << (h.name.empty() ? "(empty)" : h.name) << "\n";
    std::cout << "  global_flags: 0x" << std::hex << h.globalFlags << std::dec << "\n";

    printArray("sequences", h.sequences);
    printArray("bones", h.bones);
    if (h.bones.count == 0 && h.skeletonFileId) {
        std::cout << "    note: 0 inline bones, but this model has an external skeleton"
                      " (SKID file data ID " << *h.skeletonFileId
                  << ") -- pass its .skel path to `husk export`'s optional 4th argument\n";
    }
    {
        auto bones = m2::parseBones(blob, h.bones);
        for (size_t i = 0; i < bones.size(); ++i) {
            if (const char* mode = m2::billboardModeName(bones[i].flags)) {
                std::cout << "    bone " << i << ": billboard=" << mode << "\n";
            }
        }
    }
    printArray("vertices", h.vertices);
    printArray("textures", h.textures);
    {
        auto textures = m2::parseTextures(blob, h.textures);
        for (size_t i = 0; i < textures.size(); ++i) {
            const auto& t = textures[i];
            std::cout << "    texture " << i << ": type=" << t.type << " flags=0x" << std::hex
                       << t.flags << std::dec;
            if (t.type == 0) {
                std::cout << " filename=" << (t.filename.empty() ? "(empty)" : t.filename);
            }
            if (h.textureFileDataIds && i < h.textureFileDataIds->size() &&
                (*h.textureFileDataIds)[i] != 0) {
                std::cout << " file_data_id=" << (*h.textureFileDataIds)[i];
            }
            std::cout << "\n";
        }
    }
    printArray("materials", h.materials);
    {
        auto materials = m2::parseMaterials(blob, h.materials);
        for (size_t i = 0; i < materials.size(); ++i) {
            std::cout << "    material " << i << ": flags=0x" << std::hex << materials[i].flags
                       << std::dec << " blend_mode=" << materials[i].blendMode << "\n";
        }
    }
    // FINDINGS.md §3.1: parsed and (since this session) resolved by `husk
    // export` into inert glTF extras for a batch that references one (see
    // m2::TextureTransform's doc comment) -- previously not even counted
    // here, the exact "parsed then dropped" gap that finding tracked.
    printArray("texture_transforms", h.textureTransforms);
    std::cout << "  num_skin_profiles: " << h.numSkinProfiles << "\n";

    if (h.textureFileDataIds && !h.textureFileDataIds->empty()) {
        std::cout << "  texture_file_data_ids: ";
        for (size_t i = 0; i < h.textureFileDataIds->size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << (*h.textureFileDataIds)[i];
        }
        std::cout << " (Legion+ TXID chunk -- also shown per-texture above)\n";
    }
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
    if (h.physFileId) {
        std::cout << "  phys_file_id: " << *h.physFileId
                  << " (.phys sidecar -- physics/collision data, not yet resolved by husk)\n";
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

    printArray("attachments", h.attachments);
    for (const auto& a : m2::parseAttachments(blob, h.attachments)) {
        std::cout << "    id=" << a.id << " bone=" << a.bone << " position=";
        printVec3(a.position);
        std::cout << "\n";
    }
    printArray("events", h.events);
    for (const auto& e : m2::parseEvents(blob, h.events)) {
        std::cout << "    " << (e.identifier.empty() ? "(empty)" : e.identifier) << " bone=" << e.bone
                   << " data=" << e.data << " position=";
        printVec3(e.position);
        std::cout << "\n";
    }
    printArray("lights", h.lights);
    for (const auto& l : m2::parseLights(blob, h.lights)) {
        std::cout << "    type=" << (l.type == 0 ? "directional" : l.type == 1 ? "point" : "unknown")
                   << " bone=" << l.bone << " position=";
        printVec3(l.position);
        std::cout << "\n";
    }
    printArray("cameras", h.cameras);
    printArray("ribbon_emitters", h.ribbonEmitters);
    for (const auto& r : m2::parseRibbons(blob, h.ribbonEmitters)) {
        std::cout << "    ribbonId=" << r.ribbonId << " bone=" << r.boneIndex << " position=";
        printVec3(r.position);
        std::cout << " edgesPerSecond=" << r.edgesPerSecond << " edgeLifetime=" << r.edgeLifetime
                   << "\n";
    }
    printArray("particle_emitters", h.particleEmitters);

    std::cout << "  bounding_box: min=";
    printVec3(h.boundingBox.min);
    std::cout << " max=";
    printVec3(h.boundingBox.max);
    std::cout << "\n";
    std::cout << "  bounding_sphere_radius: " << h.boundingSphereRadius << "\n";

    // FINDINGS.md §3.3: all four collision fields are parsed (m2.cpp's
    // parseHeader) but until this session only collision_positions was ever
    // printed here -- collision_box/collision_sphere_radius weren't printed
    // at all, and collision_indices/collision_face_normals weren't even
    // counted, despite `husk export` documenting real triangle topology for
    // this low-poly hit-test mesh (not the render mesh) sitting right next
    // to the fields that are surfaced. Still no `dump-chunks`-style content
    // dump of the actual mesh -- these are counts/scalars only, same depth
    // as every other still-🚧 array in the format matrix.
    std::cout << "  collision_box: min=";
    printVec3(h.collisionBox.min);
    std::cout << " max=";
    printVec3(h.collisionBox.max);
    std::cout << "\n";
    std::cout << "  collision_sphere_radius: " << h.collisionSphereRadius << "\n";
    printArray("collision_positions", h.collisionPositions);
    printArray("collision_indices", h.collisionIndices);
    printArray("collision_face_normals", h.collisionFaceNormals);

    return 0;
}

}  // namespace husk::commands
