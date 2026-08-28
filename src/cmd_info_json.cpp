#include <sstream>

#include "cmd_info_json.hpp"
#include "commands.hpp"
#include "json_writer.hpp"

// `husk info --json`'s field-by-field schema -- a structured mirror of
// cmd_info.cpp's prose path, built for tools/corpus_scan_tasks/*.py to parse
// via real JSON instead of the regexes several of those tasks compile
// against prose today (`particle_only_task.py`'s VERTICES_RE/
// PARTICLE_COUNT_RE, `black_additive_task.py`'s TEXTURE_LINE_RE/
// LOOKUP_LINE_RE/MATERIAL_RE/PARTICLE_COUNT_RE -- see
// REFACTOR/CLI_AND_TOOLING.md §3). Converting those tasks to consume this is
// explicitly a separate, later piece of work -- this file only emits the
// JSON.
//
// Every `m2::Array`-backed field below is written as `{"count", "offset"}`
// (matching printArray's own "N (offset 0xH)" prose), with a same-named
// `entries` array alongside it when the prose path dereferences that array
// into real records -- e.g. `bones`/`bone_lookup` vs. `vertices`/
// `texture_transforms`, which the prose path only ever prints as bare
// counts. Fields the prose path only prints conditionally (present in the
// wire header, an optional sidecar chunk, ...) are simply absent from the
// JSON object in that case, rather than a `null` placeholder -- a consumer
// checking "does this key exist" gets the same answer prose's own
// presence/absence already gave it.
namespace husk::commands {

namespace {

void writeVec3(json::Writer& w, const char* key, const m2::Vec3& v) {
    w.key(key);
    w.beginObject();
    w.key("x");
    w.value(static_cast<double>(v.x));
    w.key("y");
    w.value(static_cast<double>(v.y));
    w.key("z");
    w.value(static_cast<double>(v.z));
    w.endObject();
}

void writeBoundingBox(json::Writer& w, const char* key, const m2::BoundingBox& b) {
    w.key(key);
    w.beginObject();
    writeVec3(w, "min", b.min);
    writeVec3(w, "max", b.max);
    w.endObject();
}

// Opens `{"count": .., "offset": ..` (no closing brace) for an m2::Array --
// every array-backed field starts this way; callers append any `entries`
// (or other) keys before calling w.endObject() themselves. Mirrors
// printArray's own "N (offset 0xH)" prose line.
void beginArrayObject(json::Writer& w, const char* key, const m2::Array& a) {
    w.key(key);
    w.beginObject();
    w.key("count");
    w.value(static_cast<int64_t>(a.count));
    w.key("offset");
    w.value(static_cast<int64_t>(a.offset));
}

std::string hex(uint32_t v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << v;
    return ss.str();
}

void writeOptionalName(json::Writer& w, const char* key, const char* name) {
    w.key(key);
    if (name) {
        w.value(name);
    } else {
        w.nullValue();
    }
}

}  // namespace

void printInfoJson(std::ostream& out, const std::string& path, const m2::Header& h,
                    const std::vector<uint8_t>& blob) {
    json::Writer w(out);
    w.beginObject();

    w.key("path");
    w.value(path);
    w.key("format");
    w.value(h.chunked ? "legion_chunked" : "pre_legion");
    w.key("version");
    w.value(static_cast<int64_t>(h.version));
    w.key("expansion");
    w.value(m2::expansionForVersion(h.version));
    w.key("record_stride_version_verified");
    w.value(h.version >= m2::kMinVerifiedRecordStrideVersion);
    w.key("name");
    w.value(h.name);

    w.key("global_flags");
    w.beginObject();
    w.key("value");
    w.value(static_cast<int64_t>(h.globalFlags));
    w.key("hex");
    w.value(hex(h.globalFlags));
    w.key("names");
    w.beginArray();
    for (const auto& n : m2::globalFlagNames(h.globalFlags)) w.value(n);
    w.endArray();
    w.endObject();

    // Only present in the wire header at all when
    // GlobalFlag::kUseTextureCombinerCombos is set -- see Header::
    // textureCombinerCombos's own doc comment. Gated on count > 0, same
    // condition the prose path checks.
    if (h.textureCombinerCombos.count > 0) {
        beginArrayObject(w, "texture_combiner_combos", h.textureCombinerCombos);
        w.key("values");
        w.beginArray();
        for (uint16_t v : m2::parseUint16Array(blob, h.textureCombinerCombos)) {
            w.value(static_cast<int64_t>(v));
        }
        w.endArray();
        w.endObject();
    }

    beginArrayObject(w, "sequences", h.sequences);
    w.endObject();

    beginArrayObject(w, "sequence_lookup", h.sequenceLookup);
    if (h.sequenceLookup.count > 0) {
        auto lookup = m2::parseUint16Array(blob, h.sequenceLookup);
        auto sequences = m2::parseSequences(blob, h.sequences);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < lookup.size(); ++i) {
            if (lookup[i] == 0xFFFF) continue;
            w.beginObject();
            w.key("bucket");
            w.value(static_cast<int64_t>(i));
            w.key("sequence_index");
            w.value(static_cast<int64_t>(lookup[i]));
            w.key("sequence_id");
            if (lookup[i] < sequences.size()) {
                w.value(static_cast<int64_t>(sequences[lookup[i]].id));
            } else {
                w.nullValue();
            }
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "bones", h.bones);
    if (h.skeletonFileId) {
        w.key("external_skeleton_file_id");
        w.value(static_cast<int64_t>(*h.skeletonFileId));
    }
    {
        auto bones = m2::parseBones(blob, h.bones);
        w.key("billboard_bones");
        w.beginArray();
        for (size_t i = 0; i < bones.size(); ++i) {
            if (const char* mode = m2::billboardModeName(bones[i].flags)) {
                w.beginObject();
                w.key("index");
                w.value(static_cast<int64_t>(i));
                w.key("billboard_mode");
                w.value(mode);
                w.endObject();
            }
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "bone_lookup", h.boneLookup);
    if (h.boneLookup.count > 0) {
        auto keyBones = m2::parseUint16Array(blob, h.boneLookup);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < keyBones.size(); ++i) {
            if (keyBones[i] == 0xFFFF) continue;
            w.beginObject();
            w.key("key_bone_index");
            w.value(static_cast<int64_t>(i));
            writeOptionalName(w, "key_bone_name", m2::keyBoneName(static_cast<int32_t>(i)));
            w.key("bone_index");
            w.value(static_cast<int64_t>(keyBones[i]));
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    // vertices.count is this schema's answer to particle_only_task.py's
    // VERTICES_RE -- see this file's own header comment.
    beginArrayObject(w, "vertices", h.vertices);
    w.endObject();

    beginArrayObject(w, "textures", h.textures);
    {
        auto textures = m2::parseTextures(blob, h.textures);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < textures.size(); ++i) {
            const auto& t = textures[i];
            w.beginObject();
            w.key("index");
            w.value(static_cast<int64_t>(i));
            w.key("type");
            w.value(static_cast<int64_t>(t.type));
            w.key("flags");
            w.value(hex(t.flags));
            w.key("filename");
            if (t.type == 0) {
                w.value(t.filename);
            } else {
                w.nullValue();
            }
            w.key("file_data_id");
            if (h.textureFileDataIds && i < h.textureFileDataIds->size() &&
                (*h.textureFileDataIds)[i] != 0) {
                w.value(static_cast<int64_t>((*h.textureFileDataIds)[i]));
            } else {
                w.nullValue();
            }
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "texture_lookup", h.textureLookup);
    if (h.textureLookup.count > 0) {
        auto texLookup = m2::parseUint16Array(blob, h.textureLookup);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < texLookup.size(); ++i) {
            if (texLookup[i] == 0xFFFF) continue;
            w.beginObject();
            w.key("type_index");
            w.value(static_cast<int64_t>(i));
            writeOptionalName(w, "type_name", m2::textureTypeName(static_cast<uint32_t>(i)));
            w.key("texture_index");
            w.value(static_cast<int64_t>(texLookup[i]));
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    // materials[].blend_mode is this schema's answer to
    // particle_only_task.py's/black_additive_task.py's MATERIAL_RE.
    beginArrayObject(w, "materials", h.materials);
    {
        auto materials = m2::parseMaterials(blob, h.materials);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < materials.size(); ++i) {
            w.beginObject();
            w.key("index");
            w.value(static_cast<int64_t>(i));
            w.key("flags");
            w.value(hex(materials[i].flags));
            w.key("blend_mode");
            w.value(static_cast<int64_t>(materials[i].blendMode));
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "texture_transforms", h.textureTransforms);
    w.endObject();

    w.key("num_skin_profiles");
    w.value(static_cast<int64_t>(h.numSkinProfiles));

    if (h.textureFileDataIds && !h.textureFileDataIds->empty()) {
        w.key("texture_file_data_ids");
        w.beginArray();
        for (uint32_t id : *h.textureFileDataIds) w.value(static_cast<int64_t>(id));
        w.endArray();
    }
    if (h.skinFileDataIds) {
        w.key("skin_file_data_ids");
        w.beginArray();
        for (uint32_t id : *h.skinFileDataIds) w.value(static_cast<int64_t>(id));
        w.endArray();
    }
    if (h.lodCount) {
        w.key("lod_count");
        w.value(static_cast<int64_t>(*h.lodCount));
    }
    // The prose path only prints a count for these two (".bone sidecars,
    // not yet resolved" / ".anim sidecars, not yet resolved") -- the JSON
    // path surfaces the real FileDataIDs/anim keys Header already holds,
    // a strict superset of what prose shows, not an invented field.
    if (h.boneFileDataIds && !h.boneFileDataIds->empty()) {
        w.key("bone_file_data_ids");
        w.beginArray();
        for (uint32_t id : *h.boneFileDataIds) w.value(static_cast<int64_t>(id));
        w.endArray();
    }
    if (h.animFileIds && !h.animFileIds->empty()) {
        w.key("anim_file_ids");
        w.beginArray();
        for (const auto& e : *h.animFileIds) {
            w.beginObject();
            w.key("anim_id");
            w.value(static_cast<int64_t>(e.animId));
            w.key("sub_anim_id");
            w.value(static_cast<int64_t>(e.subAnimId));
            w.key("file_id");
            w.value(static_cast<int64_t>(e.fileId));
            w.endObject();
        }
        w.endArray();
    }
    if (h.physFileId) {
        w.key("phys_file_id");
        w.value(static_cast<int64_t>(*h.physFileId));
    }

    if (!h.chunkTags.empty()) {
        w.key("chunks");
        w.beginObject();
        w.key("tags");
        w.beginArray();
        for (const auto& tag : h.chunkTags) w.value(tag);
        w.endArray();
        w.key("undocumented_tags");
        w.beginArray();
        for (const auto& tag : h.chunkTags) {
            if (isUndocumentedChunkTag(tag)) w.value(tag);
        }
        w.endArray();
        w.endObject();
    }

    beginArrayObject(w, "attachments", h.attachments);
    {
        auto attachments = m2::parseAttachments(blob, h.attachments);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < attachments.size(); ++i) {
            const auto& a = attachments[i];
            w.beginObject();
            w.key("index");
            w.value(static_cast<int64_t>(i));
            w.key("id");
            w.value(static_cast<int64_t>(a.id));
            w.key("bone");
            w.value(static_cast<int64_t>(a.bone));
            writeVec3(w, "position", a.position);
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "attachment_lookup", h.attachmentLookup);
    if (h.attachmentLookup.count > 0) {
        auto attLookup = m2::parseUint16Array(blob, h.attachmentLookup);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < attLookup.size(); ++i) {
            if (attLookup[i] == 0xFFFF) continue;
            w.beginObject();
            w.key("type_index");
            w.value(static_cast<int64_t>(i));
            writeOptionalName(w, "type_name", m2::attachmentTypeName(static_cast<uint32_t>(i)));
            w.key("attachment_index");
            w.value(static_cast<int64_t>(attLookup[i]));
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "events", h.events);
    {
        auto events = m2::parseEvents(blob, h.events);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& e = events[i];
            w.beginObject();
            w.key("index");
            w.value(static_cast<int64_t>(i));
            w.key("identifier");
            w.value(e.identifier);
            w.key("data");
            w.value(static_cast<int64_t>(e.data));
            w.key("bone");
            w.value(static_cast<int64_t>(e.bone));
            writeVec3(w, "position", e.position);
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "lights", h.lights);
    {
        auto lights = m2::parseLights(blob, h.lights);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < lights.size(); ++i) {
            const auto& l = lights[i];
            w.beginObject();
            w.key("index");
            w.value(static_cast<int64_t>(i));
            w.key("type");
            w.value(l.type == 0 ? "directional" : l.type == 1 ? "point" : "unknown");
            w.key("bone");
            w.value(static_cast<int64_t>(l.bone));
            writeVec3(w, "position", l.position);
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "cameras", h.cameras);
    w.endObject();

    beginArrayObject(w, "camera_lookup", h.cameraLookup);
    if (h.cameraLookup.count > 0) {
        auto camLookup = m2::parseUint16Array(blob, h.cameraLookup);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < camLookup.size(); ++i) {
            if (camLookup[i] == 0xFFFF) continue;
            w.beginObject();
            w.key("type_index");
            w.value(static_cast<int64_t>(i));
            w.key("camera_index");
            w.value(static_cast<int64_t>(camLookup[i]));
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    beginArrayObject(w, "ribbon_emitters", h.ribbonEmitters);
    {
        auto ribbons = m2::parseRibbons(blob, h.ribbonEmitters);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < ribbons.size(); ++i) {
            const auto& r = ribbons[i];
            w.beginObject();
            w.key("index");
            w.value(static_cast<int64_t>(i));
            w.key("ribbon_id");
            w.value(static_cast<int64_t>(r.ribbonId));
            w.key("bone_index");
            w.value(static_cast<int64_t>(r.boneIndex));
            writeVec3(w, "position", r.position);
            w.key("edges_per_second");
            w.value(static_cast<double>(r.edgesPerSecond));
            w.key("edge_lifetime");
            w.value(static_cast<double>(r.edgeLifetime));
            w.key("texture_count");
            w.value(static_cast<int64_t>(r.textureIndices.size()));
            w.key("material_count");
            w.value(static_cast<int64_t>(r.materialIndices.size()));
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    // particle_emitters.count is this schema's answer to
    // particle_only_task.py's/black_additive_task.py's PARTICLE_COUNT_RE.
    beginArrayObject(w, "particle_emitters", h.particleEmitters);
    if (h.particleEmitters.count > 0 && h.version < m2::kMinVerifiedParticleVersion) {
        w.key("version_verified");
        w.value(false);
    } else {
        w.key("version_verified");
        w.value(true);
        auto particles = m2::parseParticles(blob, h.particleEmitters);
        w.key("entries");
        w.beginArray();
        for (size_t i = 0; i < particles.size(); ++i) {
            const auto& p = particles[i];
            w.beginObject();
            w.key("index");
            w.value(static_cast<int64_t>(i));
            w.key("particle_id");
            w.value(static_cast<int64_t>(p.particleId));
            w.key("bone_id");
            w.value(static_cast<int64_t>(p.boneId));
            writeVec3(w, "position", p.position);
            w.key("blending_type");
            w.value(static_cast<int64_t>(p.blendingType));
            w.key("emitter_type");
            w.value(static_cast<int64_t>(p.emitterType));
            w.key("rows");
            w.value(static_cast<int64_t>(p.rows));
            w.key("columns");
            w.value(static_cast<int64_t>(p.columns));
            w.endObject();
        }
        w.endArray();
    }
    w.endObject();

    writeBoundingBox(w, "bounding_box", h.boundingBox);
    w.key("bounding_sphere_radius");
    w.value(static_cast<double>(h.boundingSphereRadius));

    writeBoundingBox(w, "collision_box", h.collisionBox);
    w.key("collision_sphere_radius");
    w.value(static_cast<double>(h.collisionSphereRadius));
    beginArrayObject(w, "collision_positions", h.collisionPositions);
    w.endObject();
    beginArrayObject(w, "collision_indices", h.collisionIndices);
    w.endObject();
    beginArrayObject(w, "collision_face_normals", h.collisionFaceNormals);
    w.endObject();

    w.endObject();
    out << "\n";
}

}  // namespace husk::commands
