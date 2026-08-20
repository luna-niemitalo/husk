#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <CLI/CLI.hpp>

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

void printArray(const char* label, const m2::Array& a) {
    std::cout << "  " << label << ": " << a.count << " (offset 0x" << std::hex << a.offset
               << std::dec << ")\n";
}

void printVec3(const m2::Vec3& v) {
    std::cout << "(" << v.x << ", " << v.y << ", " << v.z << ")";
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw m2::ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw m2::ParseError("error reading '" + path + "': " + std::strerror(errno));
    }
    return bytes;
}

}  // namespace

void addInfoOptions(CLI::App& app, InfoOptions& opts) {
    app.add_option("model", opts.model, "the .m2 file to inspect")->required();
}

int info(int argc, char** args) {
    InfoOptions opts;
    CLI::App app{
        "Parses an M2 model's header and prints what was found: magic/version/name, whether it's "
        "Legion+ chunked, and the record counts (bones, vertices, textures, ...) from the header's "
        "M2Array fields.",
        "husk info"};
    addInfoOptions(app, opts);

    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    const std::string& path = opts.model;

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
        // file, ...). A narrower catch here would abort the process on some
        // of those instead of printing a clean message.
        // TODO: Remove: found via FAILURES.md #1.
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
                     "than failing loudly\n";
    }
    std::cout << "  name: " << (h.name.empty() ? "(empty)" : h.name) << "\n";
    std::cout << "  global_flags: 0x" << std::hex << h.globalFlags << std::dec;
    {
        auto names = m2::globalFlagNames(h.globalFlags);
        if (names.empty()) {
            std::cout << " (none set)";
        } else {
            std::cout << " (";
            for (size_t i = 0; i < names.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << names[i];
            }
            std::cout << ")";
        }
    }
    std::cout << "\n";
    if (h.textureCombinerCombos.count > 0) {
        printArray("textureCombinerCombos", h.textureCombinerCombos);
        auto combos = m2::parseUint16Array(blob, h.textureCombinerCombos);
        std::cout << "    values:";
        for (uint16_t v : combos) {
            std::cout << " " << v;
        }
        std::cout << "\n";
    }

    printArray("sequences", h.sequences);
    printArray("sequence_lookup", h.sequenceLookup);
    if (h.sequenceLookup.count > 0) {
        // wowdev.wiki M2#Animation_Lookup: a hash table (bucket = anim_id %
        // count, quadratic probing on collision), not a direct id-indexed
        // array -- printing "bucket -> sequence" rather than pretending the
        // bucket index means anything on its own.
        auto lookup = m2::parseUint16Array(blob, h.sequenceLookup);
        auto sequences = m2::parseSequences(blob, h.sequences);
        for (size_t i = 0; i < lookup.size(); ++i) {
            if (lookup[i] == 0xFFFF) continue;
            std::cout << "    bucket " << i << " -> sequence[" << lookup[i] << "]";
            if (lookup[i] < sequences.size()) {
                std::cout << " (id=" << sequences[lookup[i]].id << ")";
            }
            std::cout << "\n";
        }
    }
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
    printArray("bone_lookup", h.boneLookup);
    if (h.boneLookup.count > 0) {
        // wowdev.wiki M2#Key-Bone_Lookup ("boneIndicesById"): index =
        // keyBoneId (see m2::keyBoneName), value = bone index, 0xFFFF ("-1")
        // = no bone assigned to that key-bone role in this model. The
        // reverse direction of Bone::keyBoneId (bone -> role), already
        // surfaced per-bone via billboardModeName's neighboring loop above.
        auto keyBones = m2::parseUint16Array(blob, h.boneLookup);
        for (size_t i = 0; i < keyBones.size(); ++i) {
            if (keyBones[i] == 0xFFFF) continue;
            std::cout << "    key bone " << i;
            if (const char* name = m2::keyBoneName(static_cast<int32_t>(i))) {
                std::cout << " (" << name << ")";
            }
            std::cout << " -> bone " << keyBones[i] << "\n";
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
    printArray("texture_lookup", h.textureLookup);
    if (h.textureLookup.count > 0) {
        // wowdev.wiki M2#Replacable_texture_lookup ("textureIndicesById"): a
        // reverse lookup, index = replaceable texture id (Texture::type --
        // see m2::textureTypeName), value = index into `textures`, 0xFFFF
        // ("-1") = that id isn't used by this model. Distinct from
        // `textureCombos` (wiki's separately-named "Texture lookup table"),
        // which is batch-order combiner data cmd_export.cpp already
        // consumes for real rendering.
        auto texLookup = m2::parseUint16Array(blob, h.textureLookup);
        for (size_t i = 0; i < texLookup.size(); ++i) {
            if (texLookup[i] == 0xFFFF) continue;
            std::cout << "    texture type " << i;
            if (const char* name = m2::textureTypeName(static_cast<uint32_t>(i))) {
                std::cout << " (" << name << ")";
            }
            std::cout << " -> texture " << texLookup[i] << "\n";
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
    // TODO: Remove: FINDINGS.md §3.1 tracked texture_transforms not being
    // counted here at all; now resolved by `husk export` into inert glTF
    // extras for a batch that references one (see m2::TextureTransform's
    // doc comment).
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
                  << " (.phys sidecar -- physics/collision data; a minimal per-body placement "
                     "anchor is attached via `husk export --phys`, full body/shape/joint records "
                     "via `husk dump-chunks <file.phys>`)\n";
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
    printArray("attachment_lookup", h.attachmentLookup);
    if (h.attachmentLookup.count > 0) {
        // wowdev.wiki M2#Attachment_Lookup: index = attachment type id (see
        // m2::attachmentTypeName), value = index into `attachments`, 0xFFFF
        // ("-1") = that type isn't used by this model.
        auto attLookup = m2::parseUint16Array(blob, h.attachmentLookup);
        for (size_t i = 0; i < attLookup.size(); ++i) {
            if (attLookup[i] == 0xFFFF) continue;
            std::cout << "    attachment type " << i;
            if (const char* name = m2::attachmentTypeName(static_cast<uint32_t>(i))) {
                std::cout << " (" << name << ")";
            }
            std::cout << " -> attachment " << attLookup[i] << "\n";
        }
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
    printArray("camera_lookup", h.cameraLookup);
    if (h.cameraLookup.count > 0) {
        // wowdev.wiki M2#Camera_lookup_table: index = camera type (type 1 is
        // always the character-tab camera per the wiki), value = index into
        // `cameras`, 0xFFFF ("-1") = not referenced. The wiki notes a valid
        // block may be all -1s (an exporter quirk, e.g.
        // ui_mainmenu_warlords.m2) -- that's real data, not a parse bug.
        auto camLookup = m2::parseUint16Array(blob, h.cameraLookup);
        for (size_t i = 0; i < camLookup.size(); ++i) {
            if (camLookup[i] == 0xFFFF) continue;
            std::cout << "    camera type " << i << " -> camera " << camLookup[i] << "\n";
        }
    }
    printArray("ribbon_emitters", h.ribbonEmitters);
    for (const auto& r : m2::parseRibbons(blob, h.ribbonEmitters)) {
        std::cout << "    ribbonId=" << r.ribbonId << " bone=" << r.boneIndex << " position=";
        printVec3(r.position);
        std::cout << " edgesPerSecond=" << r.edgesPerSecond << " edgeLifetime=" << r.edgeLifetime
                   << " textures=" << r.textureIndices.size() << " materials=" << r.materialIndices.size()
                   << " (full field/curve data: `husk dump-chunks`)\n";
    }
    printArray("particle_emitters", h.particleEmitters);
    if (h.particleEmitters.count > 0 && h.version < m2::kMinVerifiedParticleVersion) {
        std::cerr << "husk: warning: version " << h.version << " is below Cataclysm ("
                  << m2::kMinVerifiedParticleVersion
                  << ") -- M2Particle's record shape is only documented and verified for Cata+; "
                     "particle_emitters is count-only for this file rather than risking a "
                     "silent misread at the wrong byte offset\n";
    } else {
        for (const auto& p : m2::parseParticles(blob, h.particleEmitters)) {
            std::cout << "    particleId=" << p.particleId << " bone=" << p.boneId << " position=";
            printVec3(p.position);
            std::cout << " blendingType=" << static_cast<int>(p.blendingType)
                       << " emitterType=" << static_cast<int>(p.emitterType) << " rows=" << p.rows
                       << " columns=" << p.columns
                       << " (full field/curve data: `husk dump-chunks`)\n";
        }
    }

    std::cout << "  bounding_box: min=";
    printVec3(h.boundingBox.min);
    std::cout << " max=";
    printVec3(h.boundingBox.max);
    std::cout << "\n";
    std::cout << "  bounding_sphere_radius: " << h.boundingSphereRadius << "\n";

    // These are counts/scalars only, not a full content dump -- same depth
    // as other still-🚧 rows, see
    // README.md#format-support-matrix-m2--m3--wmo--adt--blp.
    // TODO: Remove: FINDINGS.md §3.3 tracked collision_box/
    // collision_sphere_radius/collision_indices/collision_face_normals not
    // being printed/counted here at all; now fixed.
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
