#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include <CLI/CLI.hpp>

#include "bone.hpp"
#include "chrmodel_db2.hpp"
#include "chunk.hpp"
#include "commands.hpp"
#include "export_animation.hpp"
#include "export_materials.hpp"
#include "export_skeleton.hpp"
#include "export_skin_resolution.hpp"
#include "export_transform.hpp"
#include "gltf.hpp"
#include "m2.hpp"
#include "phys.hpp"
#include "skel.hpp"
#include "skin.hpp"

// Roadmap stages 1-5 (see README.md): stage 1 resolves an M2's vertex array
// plus a .skin file's two-level triangle-index lookup (see src/skin.hpp)
// into a static mesh; stage 2 additionally resolves the `bones` array into
// a bind-pose glTF skin, wiring M2Vertex's bone_weights/bone_indices into
// JOINTS_0/WEIGHTS_0; stage 3 covers the case where those bones live in an
// external .skel file instead (see src/skel.hpp); stage 5 resolves the
// .skin file's per-submesh batches (M2's materials/textures arrays, see
// src/skin.hpp's Batch) into one glTF material + primitive per batch, with
// WoW's blend mode translated to glTF's alphaMode and (optionally, via
// --textures) a real baseColorTexture image embedded. All of the above
// convert WoW's Z-up coordinates to glTF's Y-up. Stage 6 (animation, see
// README.md) additionally resolves M2Sequence + each bone's M2Track<T>
// keyframes into real glTF animation clips, for a model's own inline bones
// and (via skel::parseSequences/skel::boneTrackBlob/skel::findAnimFileIds)
// a .skel-sourced skeleton alike -- either way, only for sequences whose
// keyframe data actually resolves: inline, or via --anim for an external
// .anim file, AFM2- or AFSB-shaped alike (see export_animation.hpp's
// buildAnimations doc comment).
//
// This file is the orchestrator (per FILE_SPLIT_TODO.md's Item 1): CLI flag
// registration (addExportOptions) plus exportGlb's own pipeline wiring,
// each stage of which is a named phase-function below rather than inline
// code, so the pipeline's shape (skin resolution -> skeleton -> bones-dir/
// .phys attachments -> per-LOD mesh build -> collision mesh -> glTF write)
// is visible from function signatures. The actual struct/algorithm bodies
// for each stage live in export_transform/export_skeleton/export_animation/
// export_materials/export_skin_resolution.
namespace husk::commands {

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw std::runtime_error("error reading '" + path + "': " + std::strerror(errno));
    }
    return bytes;
}

// Resolves which .skin file(s) to export, honoring --skin/--skin-dir/--lod
// (see resolveSkin/resolveAutoSkinPaths's own doc comments), printing the
// same "'auto' resolved ..." notes the inline code used to.
std::vector<std::pair<std::string, std::string>> resolveSkinsToExport(const m2::Header& header,
                                                                        const std::string& modelPath,
                                                                        const std::string& skinDir,
                                                                        bool skinDirNone, bool lodGiven,
                                                                        const std::string& lodArg,
                                                                        const std::string& skinArg) {
    if (skinArg != "auto") {
        return {{"", skinArg}};
    }
    if (lodGiven) {
        auto skinsToExport = resolveAutoSkinPaths(header, skinDir, modelPath, lodArg);
        for (const auto& [name, path] : skinsToExport) {
            std::cerr << "husk: note: 'auto' resolved '" << path << "'"
                      << (name.empty() ? " (SFID entry 0, highest-detail LOD)\n"
                                        : " (SFID, " + name + ")\n");
        }
        return skinsToExport;
    }
    return resolveSkin(header, modelPath, skinDir, skinDirNone);
}

// Builds the M2's own global vertex list (positions/normals/UVs, Z-up ->
// Y-up converted) -- shared by every LOD tier (a .skin file only ever
// selects a *subset* of it via its own vertices/indices two-level lookup,
// see src/skin.hpp; it never adds vertices of its own), built once rather
// than once per LOD.
gltf::Mesh buildBaseMesh(const std::vector<m2::Vertex>& vertices) {
    gltf::Mesh baseMesh;
    baseMesh.positions.reserve(vertices.size());
    baseMesh.normals.reserve(vertices.size());
    baseMesh.texCoords.reserve(vertices.size());
    baseMesh.texCoords2.reserve(vertices.size());
    for (size_t vi = 0; vi < vertices.size(); ++vi) {
        const auto& v = vertices[vi];
        // glTF requires finite POSITION/NORMAL values (and their accessor
        // min/max); a NaN/Inf here is a real symptom of a corrupted read or
        // truncated file, not valid mesh data -- catch it here, where the
        // offending vertex index is still known, rather than downstream.
        if (!isFinite(v.pos) || !isFinite(v.normal)) {
            throw std::runtime_error("vertex " + std::to_string(vi) +
                                      " has a non-finite (NaN/Inf) position or normal -- "
                                      "corrupted read or truncated file?");
        }
        baseMesh.positions.push_back(toGltf(v.pos));
        baseMesh.normals.push_back(toGltf(v.normal));
        baseMesh.texCoords.push_back({v.texCoords[0].x, v.texCoords[0].y});
        baseMesh.texCoords2.push_back({v.texCoords[1].x, v.texCoords[1].y});
    }
    return baseMesh;
}

// Resolves this model's bones -- inline first, falling back to an external
// .skel per the three-state --skel resolution (~/docs/CLI.md §2.11):
// 'none' means never look, even if a same-basename .skel exists; an
// explicit path overrides; unset auto-detects a same-basename '.skel' next
// to the model. `bonesAreInline`/`haveSkel`/`skelBytes` are out-parameters
// since buildAnimations (below) needs to know which source's track offsets
// `bones` carries, and haveSkel/skelBytes are reused by --bones-dir
// resolution too.
std::vector<m2::Bone> resolveBones(const std::string& modelPath, const std::vector<uint8_t>& blob,
                                    const m2::Header& header, bool skelGiven, bool skelNone,
                                    std::string skelPath, bool& bonesAreInline, bool& haveSkel,
                                    std::vector<uint8_t>& skelBytes) {
    auto bones = m2::parseBones(blob, header.bones);
    bonesAreInline = !bones.empty();
    haveSkel = false;

    if (bonesAreInline) {
        if (skelGiven && !skelNone) {
            std::cerr << "husk: note: '" << modelPath << "' has its own inline bones; ignoring '"
                      << skelPath << "'\n";
        }
    } else if (skelNone) {
        // Deliberately skip -- forces an unskinned mesh regardless of
        // whether a same-basename '.skel' actually exists.
    } else if (skelGiven) {
        skelBytes = readFileBytes(skelPath);
        haveSkel = true;
        bones = skel::parseBones(skelBytes);
    } else {
        // Not finding one isn't an error: plenty of 0-bone models
        // genuinely have no skeleton at all, and this model falls back to
        // the same unskinned-mesh output it always did.
        auto defaultSkel = std::filesystem::path(modelPath).replace_extension(".skel");
        std::error_code ec;
        if (std::filesystem::exists(defaultSkel, ec) && !ec) {
            skelPath = defaultSkel.string();
            std::cerr << "husk: note: '" << modelPath << "' has 0 inline bones -- found and using '"
                      << skelPath << "' next to it\n";
            skelBytes = readFileBytes(skelPath);
            haveSkel = true;
            bones = skel::parseBones(skelBytes);
        }
    }
    return bones;
}

// Resolves per-M2Sequence + global-sequence animation clips for `bones`,
// either from the model's own inline blob/sequences or (bonesAreInline ==
// false) a .skel-sourced blob/sequences -- see buildAnimations's doc
// comment for why these two cases share one resolver. `animNone` (--anim
// none) short-circuits to no clips at all; global-sequence tracks aren't
// gated on that model even having per-sequence M2Sequence data, so they're
// still resolved whenever a bone/skel source is being used at all.
std::vector<gltf::Animation> resolveAnimationsForModel(bool animNone, bool bonesAreInline, bool haveSkel,
                                                        const m2::Header& header,
                                                        const std::vector<uint8_t>& blob,
                                                        const std::vector<uint8_t>& skelBytes,
                                                        const std::vector<m2::Bone>& bones,
                                                        const gltf::Skeleton& skeleton,
                                                        const std::string& animDir,
                                                        const std::string& modelPath) {
    std::vector<gltf::Animation> animations;
    if (animNone) return animations;

    if (bonesAreInline) {
        auto sequences = m2::parseSequences(blob, header.sequences);
        M2AnimInputs animInputs;
        animInputs.animFileIds = header.animFileIds;
        animInputs.animChunked = (header.globalFlags & 0x200000) != 0;
        animInputs.animDir = animDir;
        animInputs.modelPath = modelPath;
        animations = buildAnimations(blob, bones, skeleton, sequences, animInputs);
        // Global-sequence-driven tracks (continuous looping animation
        // independent of any M2Sequence) aren't tied to `sequences` at
        // all, so this runs unconditionally alongside the per-sequence
        // clips above, not gated on any of them existing.
        auto globalSeqAnims = buildGlobalSequenceAnimations(blob, bones, skeleton);
        animations.insert(animations.end(), std::make_move_iterator(globalSeqAnims.begin()),
                           std::make_move_iterator(globalSeqAnims.end()));
    } else if (haveSkel) {
        // Same buildAnimations, pointed at the .skel's own blob (SKB1's
        // payload, which is what `bones`'s track offsets are relative to
        // -- see skel::boneTrackBlob) and its own sequences/AFID table
        // instead of the M2's. Peeking for SKS1 first, rather than just
        // calling skel::parseSequences and letting its ParseError
        // propagate, treats "this .skel has no sequences at all" as "no
        // animation clips available," not export failure.
        auto skelTrackBlob = skel::boneTrackBlob(skelBytes);
        if (findChunk(readChunks(skelBytes.data(), skelBytes.size()), "SKS1")) {
            auto sequences = skel::parseSequences(skelBytes);
            M2AnimInputs animInputs;
            animInputs.animFileIds = skel::findAnimFileIds(skelBytes);
            animInputs.animChunked = (header.globalFlags & 0x200000) != 0;
            animInputs.animDir = animDir;
            animInputs.modelPath = modelPath;
            animations = buildAnimations(skelTrackBlob, bones, skeleton, sequences, animInputs);
        }
        auto globalSeqAnims = buildGlobalSequenceAnimations(skelTrackBlob, bones, skeleton);
        animations.insert(animations.end(), std::make_move_iterator(globalSeqAnims.begin()),
                           std::make_move_iterator(globalSeqAnims.end()));
    }
    return animations;
}

// --bones-dir: resolves each of the model's/.skel's BFID-declared
// FileDataIDs to a real '<bonesDir>/<id>.bone' file, if present (silently
// skipped otherwise, same "optional, resolve what's there" policy
// --textures already uses for a missing PNG) -- attached as inert
// gltf::Skeleton::CorrectionSet extras, never applied to the bind pose/
// animation (see gltf.hpp's CorrectionSet doc comment).
void attachBoneCorrections(const std::string& bonesDir, bool bonesAreInline, bool haveSkel,
                            const m2::Header& header, const std::vector<uint8_t>& skelBytes,
                            gltf::Skeleton& skeleton) {
    if (bonesDir.empty()) return;

    std::optional<std::vector<uint32_t>> boneFileDataIds =
        bonesAreInline ? header.boneFileDataIds
                        : (haveSkel ? skel::findBoneFileDataIds(skelBytes) : std::nullopt);
    if (!boneFileDataIds) return;

    size_t found = 0;
    for (uint32_t fdid : *boneFileDataIds) {
        auto bonePath = std::filesystem::path(bonesDir) / (std::to_string(fdid) + ".bone");
        std::error_code ec;
        if (!std::filesystem::exists(bonePath, ec) || ec) {
            continue;
        }
        auto boneBytes = readFileBytes(bonePath.string());
        auto corrections = bone::parse(boneBytes);
        gltf::Skeleton::CorrectionSet set;
        set.fileDataId = fdid;
        set.corrections.reserve(corrections.size());
        for (const auto& c : corrections) {
            if (c.boneIndex >= skeleton.joints.size()) {
                throw std::runtime_error("'" + bonePath.string() + "' corrects bone " +
                                          std::to_string(c.boneIndex) + ", out of range for " +
                                          std::to_string(skeleton.joints.size()) + " bones");
            }
            set.corrections.push_back({static_cast<int>(c.boneIndex), c.matrix});
        }
        skeleton.correctionSets.push_back(std::move(set));
        ++found;
    }
    if (found > 0) {
        std::cerr << "husk: note: attached " << found << "/" << boneFileDataIds->size()
                  << " '.bone' correction set(s) from '" << bonesDir
                  << "' as inert glTF extras (not applied to the render -- which slot is "
                     "'correct' for a given character depends on client-side "
                     "customization-choice data husk doesn't have)\n";
    }
}

// Ribbon/particle placement anchors (gltf::Skeleton::EmitterAnchor's doc
// comment): unconditional, no CLI flag -- this data comes straight from
// the model's own already-parsed header arrays. Full field/curve data
// lives in `husk dump-chunks`, not here -- this is placement only.
void attachEmitterAnchors(const std::vector<uint8_t>& blob, const m2::Header& header,
                           gltf::Skeleton& skeleton) {
    for (const auto& r : m2::parseRibbons(blob, header.ribbonEmitters)) {
        if (r.boneIndex >= skeleton.joints.size()) {
            throw std::runtime_error("ribbon emitter references bone " +
                                      std::to_string(r.boneIndex) + ", out of range for " +
                                      std::to_string(skeleton.joints.size()) + " bones");
        }
        skeleton.ribbonAnchors.push_back({r.ribbonId, static_cast<int>(r.boneIndex), toGltf(r.position)});
    }
    if (header.particleEmitters.count == 0 || header.version >= m2::kMinVerifiedParticleVersion) {
        for (const auto& p : m2::parseParticles(blob, header.particleEmitters)) {
            if (p.boneId >= skeleton.joints.size()) {
                throw std::runtime_error("particle emitter references bone " +
                                          std::to_string(p.boneId) + ", out of range for " +
                                          std::to_string(skeleton.joints.size()) + " bones");
            }
            skeleton.particleAnchors.push_back(
                {p.particleId, static_cast<int>(p.boneId), toGltf(p.position)});
        }
    }
    if (!skeleton.ribbonAnchors.empty() || !skeleton.particleAnchors.empty()) {
        std::cerr << "husk: note: attached " << skeleton.ribbonAnchors.size() << " ribbon and "
                  << skeleton.particleAnchors.size()
                  << " particle emitter placement anchor(s) as inert glTF extras (id/bone/"
                     "position only -- full field/curve data via `husk dump-chunks`)\n";
    }
}

// Resolves an M2Light color track (ambient_color/diffuse_color, both
// M2Track<C3Vector>) the same way export_materials.cpp's own (file-local,
// not reusable from here) resolveAnimatedColorCurve resolves M2Color::color
// -- one gltf::Material::AnimatedColorCurve per M2Sequence with real inline
// data, plus a synthetic global-sequence entry. `color`'s x/y/z are already
// 0..1 RGB, not a spatial vector, so no toGltf()/Z-up remap.
std::vector<gltf::Material::AnimatedColorCurve> resolveLightColorCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    std::vector<gltf::Material::AnimatedColorCurve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = m2::resolveVec3TrackSequence(blob, trackOffset, static_cast<uint32_t>(si));
        if (raw.empty()) continue;
        gltf::Material::AnimatedColorCurve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, v] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, gltf::Vec3{v.x, v.y, v.z});
        }
        curves.push_back(std::move(curve));
    }
    auto global = m2::resolveVec3GlobalSequenceTrack(blob, trackOffset);
    if (!global.empty()) {
        gltf::Material::AnimatedColorCurve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, v] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, gltf::Vec3{v.x, v.y, v.z});
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

// Resolves an M2Light M2Track<float> field (ambient_intensity/
// diffuse_intensity/attenuation_start/attenuation_end) -- unlike
// export_materials.cpp's fixed16-based fade curves, these are already plain
// floats on the wire (resolveFloatTrackSequence, not resolveRawIntTrackSequence
// + a fixed16 decode), so no scaling is needed.
std::vector<gltf::Material::AnimatedScalarCurve> resolveLightFloatCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    std::vector<gltf::Material::AnimatedScalarCurve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = m2::resolveFloatTrackSequence(blob, trackOffset, static_cast<uint32_t>(si));
        if (raw.empty()) continue;
        gltf::Material::AnimatedScalarCurve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, v] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, v);
        }
        curves.push_back(std::move(curve));
    }
    auto global = m2::resolveFloatGlobalSequenceTrack(blob, trackOffset);
    if (!global.empty()) {
        gltf::Material::AnimatedScalarCurve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, v] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, v);
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

// Resolves an M2Track<uint8_t> boolean-ish flag track -- M2Light::visibility
// ("enabled?") and M2Attachment::animate_attached both have this exact
// shape, a raw 0/1 flag, not a fixed16-scaled value, so each keyframe's
// zero-extended raw byte is cast straight to float rather than run through
// export_materials.cpp's decodeFixed16.
std::vector<gltf::Material::AnimatedScalarCurve> resolveRawByteTrackCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    std::vector<gltf::Material::AnimatedScalarCurve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = m2::resolveRawIntTrackSequence(blob, trackOffset, static_cast<uint32_t>(si), 1);
        if (raw.empty()) continue;
        gltf::Material::AnimatedScalarCurve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, bits] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, static_cast<float>(bits));
        }
        curves.push_back(std::move(curve));
    }
    auto global = m2::resolveRawIntGlobalSequenceTrack(blob, trackOffset, 1);
    if (!global.empty()) {
        gltf::Material::AnimatedScalarCurve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, bits] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, static_cast<float>(bits));
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

// Attachment/Event/Light placement nodes (gltf::Skeleton::
// Attachment/Event/Light's doc comments): unconditional, no CLI flag, same
// "always attached" treatment as ribbon/particle anchors -- but unlike
// those, these become real child glTF nodes, not skin extras, since a
// bone-relative position marker is all M2Attachment/M2Event/M2Light static
// data ever is. `bone == -1` ("not attached to any bone," real for M2Light
// and possibly M2Attachment) is treated as out of range and throws -- husk
// has no established "unparented placement node" concept yet. `sequenceCount`
// drives Light's animated-track resolution (ambient/diffuse color+intensity,
// attenuation, visibility) the same way M2MaterialInputs::sequenceCount
// drives the material tint/fade curves.
void attachPlacementNodes(const std::vector<uint8_t>& blob, const m2::Header& header,
                           size_t sequenceCount, gltf::Skeleton& skeleton) {
    for (const auto& a : m2::parseAttachments(blob, header.attachments)) {
        if (a.bone < 0 || static_cast<size_t>(a.bone) >= skeleton.joints.size()) {
            throw std::runtime_error("attachment " + std::to_string(a.id) + " references bone " +
                                      std::to_string(a.bone) + ", out of range for " +
                                      std::to_string(skeleton.joints.size()) + " bones");
        }
        gltf::Skeleton::Attachment attachment;
        attachment.id = a.id;
        attachment.joint = static_cast<int>(a.bone);
        attachment.position = toGltf(a.position);
        attachment.animateAttached =
            resolveRawByteTrackCurve(blob, a.animateAttachedTrackOffset, sequenceCount);
        skeleton.attachments.push_back(std::move(attachment));
    }
    for (const auto& e : m2::parseEvents(blob, header.events)) {
        if (e.bone >= skeleton.joints.size()) {
            throw std::runtime_error("event '" + e.identifier + "' references bone " +
                                      std::to_string(e.bone) + ", out of range for " +
                                      std::to_string(skeleton.joints.size()) + " bones");
        }
        skeleton.events.push_back({e.identifier, static_cast<int>(e.bone), toGltf(e.position)});
    }
    for (const auto& l : m2::parseLights(blob, header.lights)) {
        if (l.bone < 0 || static_cast<size_t>(l.bone) >= skeleton.joints.size()) {
            throw std::runtime_error("light references bone " + std::to_string(l.bone) +
                                      ", out of range for " + std::to_string(skeleton.joints.size()) +
                                      " bones");
        }
        gltf::Skeleton::Light light;
        light.joint = static_cast<int>(l.bone);
        light.position = toGltf(l.position);
        light.type = l.type;
        light.ambientColor = resolveLightColorCurve(blob, l.ambientColorTrackOffset, sequenceCount);
        light.ambientIntensity = resolveLightFloatCurve(blob, l.ambientIntensityTrackOffset, sequenceCount);
        light.diffuseColor = resolveLightColorCurve(blob, l.diffuseColorTrackOffset, sequenceCount);
        light.diffuseIntensity = resolveLightFloatCurve(blob, l.diffuseIntensityTrackOffset, sequenceCount);
        light.attenuationStart = resolveLightFloatCurve(blob, l.attenuationStartTrackOffset, sequenceCount);
        light.attenuationEnd = resolveLightFloatCurve(blob, l.attenuationEndTrackOffset, sequenceCount);
        light.visibility = resolveRawByteTrackCurve(blob, l.visibilityTrackOffset, sequenceCount);
        skeleton.lights.push_back(std::move(light));
    }
    if (!skeleton.attachments.empty() || !skeleton.events.empty() || !skeleton.lights.empty()) {
        std::cerr << "husk: note: attached " << skeleton.attachments.size() << " attachment, "
                  << skeleton.events.size() << " event, and " << skeleton.lights.size()
                  << " light placement node(s) to the exported skeleton\n";
    }
}

// --phys: three-state resolution mirroring --skel (DESIGN.md's Key design
// decisions -- PFID is a single scalar FileDataID, like SKID, not an array
// like BFID/AFID/SFID, so a directory flag doesn't apply here). 'none'
// means never look, even if a same-basename .phys exists; an explicit path
// overrides; unset auto-detects a same-basename '.phys' next to the model.
// Not finding one isn't an error -- most models have no physics data at
// all. Only the minimal per-body placement anchor (gltf::Skeleton::
// PhysicsBody's doc comment) is attached here; the full body/shape/joint/
// PHYV record set is available via `husk dump-chunks` instead.
void attachPhysicsBodies(bool physNone, bool physGiven, const std::string& physPath,
                          const std::string& modelPath, gltf::Skeleton& skeleton) {
    std::string resolvedPhysPath;
    if (physNone) {
        // Deliberately skip -- forces no physics_bodies extras regardless
        // of whether a same-basename '.phys' exists.
    } else if (physGiven) {
        resolvedPhysPath = physPath;
    } else {
        auto defaultPhys = std::filesystem::path(modelPath).replace_extension(".phys");
        std::error_code ec;
        if (std::filesystem::exists(defaultPhys, ec) && !ec) {
            resolvedPhysPath = defaultPhys.string();
        }
    }
    if (resolvedPhysPath.empty()) return;

    auto physBytes = readFileBytes(resolvedPhysPath);
    auto physFile = phys::parse(physBytes);
    skeleton.physicsBodies.reserve(physFile.bodies.size());
    for (size_t bi = 0; bi < physFile.bodies.size(); ++bi) {
        const auto& b = physFile.bodies[bi];
        if (b.boneIndex >= skeleton.joints.size()) {
            throw std::runtime_error("'" + resolvedPhysPath + "' body " + std::to_string(bi) +
                                      " references bone " + std::to_string(b.boneIndex) +
                                      ", out of range for " + std::to_string(skeleton.joints.size()) +
                                      " bones");
        }
        skeleton.physicsBodies.push_back(
            {static_cast<uint32_t>(bi), static_cast<int>(b.boneIndex), toGltf(b.position), b.type});
    }
    if (!skeleton.physicsBodies.empty()) {
        std::cerr << "husk: note: attached " << skeleton.physicsBodies.size()
                  << " physics body placement anchor(s) from '" << resolvedPhysPath
                  << "' as inert glTF extras (id/bone/position/type only -- full body/shape/"
                     "joint record data via `husk dump-chunks`)\n";
    }
}

// --db2-dir/--dbd-dir/--char-layout-id: attaches real character-texture
// placement geometry (gltf::Skeleton::CharTextureLayout's doc comment) as
// inert glTF extras. All three must be given -- a missing one is diagnosed
// and the feature simply doesn't attach anything, same "not an error, just
// nothing to offer" treatment as --bones-dir/--phys finding nothing.
void attachCharTextureLayout(const std::string& db2Dir, const std::string& dbdDir,
                              const std::string& charLayoutIdArg, gltf::Skeleton& skeleton) {
    if (db2Dir.empty() && dbdDir.empty() && charLayoutIdArg.empty()) return;  // feature simply unused
    if (db2Dir.empty() || dbdDir.empty() || charLayoutIdArg.empty()) {
        std::cerr << "husk: note: --db2-dir/--dbd-dir/--char-layout-id must all be given together "
                     "-- skipping character texture-layout extras\n";
        return;
    }
    uint32_t charLayoutId = 0;
    try {
        charLayoutId = static_cast<uint32_t>(std::stoul(charLayoutIdArg));
    } catch (const std::exception&) {
        std::cerr << "husk: note: --char-layout-id '" << charLayoutIdArg
                  << "' isn't a non-negative integer -- skipping character texture-layout extras\n";
        return;
    }

    std::optional<chrmodel::Data> data = chrmodel::load(db2Dir, dbdDir, std::cerr);
    if (!data) {
        std::cerr << "husk: note: no character texture-layout DB2 data resolved from '" << db2Dir
                  << "' -- skipping\n";
        return;
    }

    gltf::Skeleton::CharTextureLayout layout;
    layout.layoutId = charLayoutId;
    for (const chrmodel::CharComponentTextureLayout& l : data->layouts) {
        if (l.id == charLayoutId) {
            layout.width = l.width;
            layout.height = l.height;
            break;
        }
    }
    for (const chrmodel::ChrModelMaterial& m : data->materials) {
        if (m.charComponentTextureLayoutsId == charLayoutId) {
            layout.materials.push_back({m.id, m.textureType, m.width, m.height, m.flags});
        }
    }
    for (const chrmodel::CharComponentTextureSection& s : data->sections) {
        if (s.charComponentTextureLayoutId == charLayoutId) {
            layout.sections.push_back(
                {s.id, s.sectionType, s.x, s.y, s.width, s.height, s.overlapSectionMask});
        }
    }
    for (const chrmodel::ChrModelTextureLayer& t : data->textureLayers) {
        if (t.charComponentTextureLayoutsId == charLayoutId) {
            layout.textureLayers.push_back(
                {t.id, t.textureType, t.layer, t.flags, t.blendMode, t.textureSectionTypeBitMask});
        }
    }

    if (layout.materials.empty() && layout.sections.empty() && layout.textureLayers.empty()) {
        std::cerr << "husk: note: CharComponentTextureLayoutsID " << charLayoutId
                  << " matched no real rows in '" << db2Dir << "' -- skipping\n";
        return;
    }

    std::cerr << "husk: note: attached character texture-layout " << charLayoutId << " ("
              << layout.materials.size() << " material(s), " << layout.sections.size()
              << " section(s), " << layout.textureLayers.size()
              << " texture layer(s)) as inert glTF extras\n";
    skeleton.charTextureLayout = std::move(layout);
}

// One NamedMesh per LOD tier: each resolves its own .skin file's
// triangle-index lookup/submeshes/batches (see src/skin.hpp) into its own
// primitives/materials, but reuses `baseMesh`'s shared positions/normals/
// texCoords/texCoords2/skinning as-is. Skips a tier entirely when its
// .skin has no renderable geometry (glTF requires a mesh's own primitives
// list to be non-empty) -- the model's skeleton and ribbon/particle
// emitter anchors still export regardless.
std::vector<gltf::NamedMesh> buildLodTierMeshes(
    const std::vector<std::pair<std::string, std::string>>& skinsToExport,
    const std::vector<m2::Vertex>& vertices, const gltf::Mesh& baseMesh, const M2MaterialInputs& m2Inputs,
    const std::string& texturesDir, const std::string& modelPath, const std::string& modelBasename,
    const std::string& texturesOutDir) {
    std::vector<gltf::NamedMesh> namedMeshes;
    namedMeshes.reserve(skinsToExport.size());
    for (const auto& [name, path] : skinsToExport) {
        auto skinBytes = readFileBytes(path);
        auto skinHeader = skin::parseHeader(skinBytes);
        auto triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);
        auto submeshes = skin::parseSubmeshes(skinBytes, skinHeader.submeshes);
        auto batches = skin::parseBatches(skinBytes, skinHeader.batches);

        // Cross-module boundary check: skin::resolveTriangleIndices only
        // validates indices against the skin file's own `vertices` array
        // -- it has no idea how many vertices the M2 actually has. A skin
        // file that doesn't belong to this M2 (wrong LOD, wrong model)
        // shows up here as an out-of-range global vertex index. Report
        // every out-of-range index found (count + the worst offender), not
        // just the first -- a real wrong-.skin pairing references
        // hundreds of out-of-range indices, not one.
        uint32_t maxOutOfRange = 0;
        size_t outOfRangeCount = 0;
        for (uint32_t idx : triangleIndices) {
            if (idx >= vertices.size()) {
                maxOutOfRange = std::max(maxOutOfRange, idx);
                ++outOfRangeCount;
            }
        }
        if (outOfRangeCount > 0) {
            throw std::runtime_error(
                "'" + path + "' references " + std::to_string(outOfRangeCount) +
                " out-of-range M2 vertex index(es) (up to " + std::to_string(maxOutOfRange) +
                ") but '" + modelPath + "' only has " + std::to_string(vertices.size()) +
                " vertices -- model/.skin mismatch?");
        }

        auto built = buildMaterialsAndPrimitives(triangleIndices, submeshes, batches, m2Inputs,
                                                   texturesDir, modelPath, texturesOutDir);

        // See BuiltMaterials::distinctSkinSectionIds's own doc comment for
        // why this note exists.
        if (built.distinctSkinSectionIds.size() > 1) {
            std::cerr << "husk: note: '" << path << "'" << (name.empty() ? "" : " (" + name + ")")
                      << "'s batches span " << built.distinctSkinSectionIds.size()
                      << " distinct geoset IDs (skinSectionId: ";
            for (size_t i = 0; i < built.distinctSkinSectionIds.size(); ++i) {
                if (i) std::cerr << ", ";
                std::cerr << built.distinctSkinSectionIds[i];
            }
            std::cerr << ") -- husk doesn't filter geosets yet, so every one of them is exported "
                         "unfiltered into this mesh\n";
        }

        // See BuiltMaterials::multiTextureBatchCount's own doc comment for
        // why this note exists.
        if (built.multiTextureBatchCount > 0) {
            std::cerr << "husk: note: '" << path << "'" << (name.empty() ? "" : " (" + name + ")")
                      << "' has " << built.multiTextureBatchCount
                      << " batch(es) with more than one texture (textureCount > 1) -- husk only "
                         "wires the first texture per batch into the rendered material; "
                         "additional layers are exported as inert 'extras' metadata, not "
                         "applied to the render\n";
        }

        // See BuiltMaterials::animatedTintOrFadeBatchCount's own doc
        // comment for why this note exists.
        if (built.animatedTintOrFadeBatchCount > 0) {
            std::cerr << "husk: note: '" << path << "'" << (name.empty() ? "" : " (" + name + ")")
                      << "' has " << built.animatedTintOrFadeBatchCount
                      << " batch(es) whose color tint (M2Color) or transparency fade "
                         "(M2TextureWeight) is animated (per-sequence or global-sequence "
                         "keyframes, not a single constant value) -- core glTF has no way to "
                         "animate a material's baseColorFactor, so each batch's static default "
                         "is used on the render, but the real curve is attached as "
                         "'tint_animation'/'fade_animation' extras\n";
        }

        // See BuiltMaterials::textureTransformBatchCount's own doc comment
        // for why this note exists.
        if (built.textureTransformBatchCount > 0) {
            std::cerr << "husk: note: '" << path << "'" << (name.empty() ? "" : " (" + name + ")")
                      << "' has " << built.textureTransformBatchCount
                      << " batch(es) with a UV transform (M2TextureTransform) -- constant, "
                         "planar-rotation ones with a resolved baseColorTexture get a real "
                         "KHR_texture_transform on the render; every batch's raw values are also "
                         "always attached as inert 'extras' metadata\n";
        }

        // Every batch that got its texture from the basename-fuzzy pool,
        // not one of the two real deterministic matches -- see
        // BuiltMaterials::FuzzyMatch's doc comment. Printed per match (not
        // just a count) precisely so each one is easy to go check by hand.
        for (const auto& fm : built.fuzzyMatches) {
            std::cerr << "husk: warning: material '" << fm.materialName << "' linked '" << fm.fileName
                      << "' via non-deterministic basename matching, not a verified FileDataID "
                         "or exact-name match -- please confirm this is the correct texture";
            if (fm.fileDataId != 0) {
                std::cerr << " (resolved FileDataID " << fm.fileDataId
                          << ", NOT independently verified against it -- husk has no CASC/"
                             "listfile access to check a FileDataID's real name)";
            } else {
                std::cerr << " (no FileDataID at all for this hardcoded slot to cross-reference)";
            }
            std::cerr << "\n";
        }

        // Genuinely ambiguous hardcoded slots (2+ same-basename candidates)
        // -- see BuiltMaterials::AmbiguousMatch's doc comment. Every real
        // candidate is embedded as an alternate_textures extras entry;
        // this just names which one husk arbitrarily wired in as the
        // default baseColorTexture, and every other real option sitting in
        // the file, so a human can go pick the actually-correct one.
        for (const auto& am : built.ambiguousMatches) {
            std::cerr << "husk: warning: material '" << am.materialName << "' had "
                      << am.allFileNames.size() << " same-basename texture candidate(s) ("
                      << am.defaultFileName << " picked arbitrarily as the default) -- all "
                      << am.allFileNames.size()
                      << " are embedded as 'alternate_textures' extras on this material";
            if (am.fileDataId != 0) {
                std::cerr << " (resolved FileDataID " << am.fileDataId
                          << ", NOT independently verified against it)";
            } else {
                std::cerr << " (no FileDataID at all for this hardcoded slot to cross-reference)";
            }
            std::cerr << ": ";
            for (size_t i = 0; i < am.allFileNames.size(); ++i) {
                if (i) std::cerr << ", ";
                std::cerr << am.allFileNames[i];
            }
            std::cerr << "\n";
        }

        if (built.primitives.empty()) {
            std::cerr << "husk: note: '" << path << "'" << (name.empty() ? "" : " (" + name + ")")
                      << "' has no renderable geometry -- skipping mesh output for this LOD tier "
                         "(skeleton and ribbon/particle emitter anchors, if any, are still "
                         "exported)\n";
            continue;
        }

        gltf::NamedMesh nm;
        // A real tier label ("lod0", ...) for --lod all; falls back to the
        // model's own basename otherwise, so the render mesh's glTF node
        // isn't left unnamed.
        nm.name = name.empty() ? modelBasename : name;
        nm.mesh = baseMesh;
        nm.mesh.primitives = built.primitives;
        nm.materials = std::move(built.materials);
        namedMeshes.push_back(std::move(nm));
    }
    return namedMeshes;
}

// The collision mesh (physics/hit-testing, m2::CollisionMesh) is a plain
// triangle mesh with an unambiguous glTF translation -- unlike geoset
// selection/multi-texture-layers (data with no unambiguous glTF shape,
// hence inert extras only), so when requested it's exported as real
// geometry: one more NamedMesh, unskinned (a collision mesh is static, not
// deformed by the armature), tagged `isCollision` so writeGlbMulti marks
// its node `{"collision": true}` in extras. Off by default -- Blender's
// stock importer has no concept of that extras tag and renders the node
// like any other mesh, and the collision hull is usually a coarse box/
// capsule that's larger than and visually occludes the real character
// (found the hard way: it fully hid a real character render). Appends
// nothing (leaves `namedMeshes` untouched) unless `--collision` was given
// and the model actually has collision data.
void appendCollisionMesh(const m2::Header& header, const std::vector<uint8_t>& blob,
                          const std::string& modelPath, bool collisionRequested,
                          std::vector<gltf::NamedMesh>& namedMeshes) {
    if (!collisionRequested || header.collisionPositions.count == 0 ||
        header.collisionIndices.count == 0) {
        return;
    }

    auto collisionMesh = m2::parseCollisionMesh(blob, header.collisionPositions, header.collisionIndices,
                                                 header.collisionFaceNormals);

    if (collisionMesh.indices.size() % 3 != 0) {
        throw std::runtime_error("'" + modelPath + "'s collision mesh has " +
                                  std::to_string(collisionMesh.indices.size()) +
                                  " indices, not a multiple of 3 (one triangle per 3 entries)");
    }
    for (uint16_t idx : collisionMesh.indices) {
        if (idx >= collisionMesh.positions.size()) {
            throw std::runtime_error("'" + modelPath + "'s collision mesh index " +
                                      std::to_string(idx) + " is out of range for " +
                                      std::to_string(collisionMesh.positions.size()) +
                                      " collision positions");
        }
    }
    size_t triCount = collisionMesh.indices.size() / 3;
    if (!collisionMesh.faceNormals.empty() && collisionMesh.faceNormals.size() != triCount) {
        throw std::runtime_error("'" + modelPath + "'s collision mesh has " +
                                  std::to_string(collisionMesh.faceNormals.size()) +
                                  " face normals for " + std::to_string(triCount) + " triangles");
    }

    gltf::Mesh collisionGltfMesh;
    collisionGltfMesh.positions.reserve(collisionMesh.positions.size());
    for (const auto& p : collisionMesh.positions) {
        collisionGltfMesh.positions.push_back(toGltf(p));
    }

    // No per-vertex normals exist in the source data (only one face normal
    // per triangle, m2::CollisionMesh's doc comment) -- approximate them
    // by averaging every adjacent triangle's face normal at each shared
    // vertex, the standard flat-to-smooth normal derivation. A collision
    // mesh isn't shaded/rendered in practice, so this is only to satisfy
    // gltf::Mesh's own positions/normals/texCoords-all-same-length
    // invariant with real, finite, unit-length data rather than a
    // placeholder.
    std::vector<gltf::Vec3> normalSums(collisionMesh.positions.size(), gltf::Vec3{0, 0, 0});
    if (!collisionMesh.faceNormals.empty()) {
        for (size_t t = 0; t < triCount; ++t) {
            gltf::Vec3 faceNormal = toGltf(collisionMesh.faceNormals[t]);
            for (int c = 0; c < 3; ++c) {
                uint16_t vi = collisionMesh.indices[t * 3 + static_cast<size_t>(c)];
                normalSums[vi].x += faceNormal.x;
                normalSums[vi].y += faceNormal.y;
                normalSums[vi].z += faceNormal.z;
            }
        }
    }
    collisionGltfMesh.normals.reserve(normalSums.size());
    for (const auto& sum : normalSums) {
        float len = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
        collisionGltfMesh.normals.push_back(len > 1e-8f ? gltf::Vec3{sum.x / len, sum.y / len, sum.z / len}
                                                          : gltf::Vec3{0, 1, 0});
    }
    collisionGltfMesh.texCoords.assign(collisionMesh.positions.size(), gltf::Vec2{0, 0});

    gltf::Primitive collisionPrim;
    collisionPrim.indices.assign(collisionMesh.indices.begin(), collisionMesh.indices.end());
    collisionGltfMesh.primitives = {collisionPrim};

    gltf::NamedMesh collisionNamedMesh;
    collisionNamedMesh.name = "collision";
    collisionNamedMesh.mesh = std::move(collisionGltfMesh);
    collisionNamedMesh.isCollision = true;
    namedMeshes.push_back(std::move(collisionNamedMesh));

    std::cout << "husk: note: attached a " << collisionMesh.positions.size() << "-position/" << triCount
              << "-triangle collision mesh (unskinned, tagged 'collision' in glTF extras, not "
                 "applied to the render)\n";
}

// Prints exportGlb's final one-line-or-table summary: no renderable
// geometry at all, exactly one render mesh, or several LOD tiers -- each
// shape needs different formatting, so this is one function rather than
// three call sites duplicating the bones/animations suffix logic.
void printExportSummary(const std::string& outputPath, const std::vector<m2::Vertex>& vertices,
                         const std::vector<m2::Bone>& bones,
                         const std::vector<gltf::Animation>& animations,
                         const std::vector<gltf::NamedMesh>& namedMeshes, size_t renderMeshCount) {
    if (renderMeshCount == 0) {
        // Every LOD tier's .skin was geometry-less (or there was only ever
        // one tier and it was empty) -- no mesh node exists in this .glb
        // at all, only the skeleton and whatever ribbon/particle emitter
        // anchors were attached.
        std::cout << outputPath << ": no renderable geometry (particle/ribbon-effect model?)";
        if (!bones.empty()) {
            std::cout << ", " << bones.size() << " bones";
            if (!animations.empty()) {
                std::cout << ", " << animations.size() << " animation(s)";
            } else {
                std::cout << " (bind pose only, no animation)";
            }
        }
        std::cout << "\n";
    } else if (renderMeshCount == 1) {
        size_t triCount = 0;
        for (const auto& p : namedMeshes[0].mesh.primitives) triCount += p.indices.size() / 3;
        std::cout << outputPath << ": " << vertices.size() << " vertices, " << triCount << " triangles";
        if (!bones.empty()) {
            std::cout << ", " << bones.size() << " bones";
            if (!animations.empty()) {
                std::cout << ", " << animations.size() << " animation(s)";
            } else {
                std::cout << " (bind pose only, no animation)";
            }
        }
        if (!namedMeshes[0].materials.empty()) {
            size_t withImage = 0;
            for (const auto& m : namedMeshes[0].materials) {
                if (!m.baseColorImagePng.empty()) ++withImage;
            }
            std::cout << ", " << namedMeshes[0].materials.size() << " materials (" << withImage
                      << " with an embedded texture)";
        }
        std::cout << "\n";
    } else {
        std::cout << outputPath << ": " << vertices.size() << " vertices (shared), " << renderMeshCount
                  << " LOD tier(s) as separate nodes:\n";
        for (size_t mi = 0; mi < renderMeshCount; ++mi) {
            const auto& nm = namedMeshes[mi];
            size_t triCount = 0;
            for (const auto& p : nm.mesh.primitives) triCount += p.indices.size() / 3;
            std::cout << "  " << nm.name << ": " << triCount << " triangles, " << nm.materials.size()
                      << " materials\n";
        }
        if (!bones.empty()) {
            std::cout << "  " << bones.size() << " bones (shared)";
            if (!animations.empty()) {
                std::cout << ", " << animations.size() << " animation(s) (shared)";
            } else {
                std::cout << " (bind pose only, no animation)";
            }
            std::cout << "\n";
        }
    }
}

}  // namespace

// See commands.hpp's doc comment on ExportOptions: this is the one place
// export's flag surface is declared, shared by exportGlb's own real parse
// and main.cpp's `--print-completion` introspection.
void addExportOptions(CLI::App& app, ExportOptions& opts) {
    app.add_option("-i,--input,input", opts.modelPath, "the .m2 file to export")->required();
    app.add_option("-o,--output,output", opts.outputPath,
                    "output .glb path (default: '<model-basename>.glb')");
    app.add_option("-s,--skin", opts.skinArg,
                    "a .skin path, or 'auto' to resolve via the model's own SFID chunk, falling "
                    "back to a same-basename numbered scan next to the model if that doesn't "
                    "resolve")
        ->capture_default_str()
        ->check(
            [](const std::string& v) -> std::string {
                if (v == "none") {
                    return "--skin takes a .skin path or 'auto', never 'none' -- a .skin file is "
                           "the sole source of triangle/submesh/batch data, not optional "
                           "enrichment (pass an explicit path instead)";
                }
                return "";
            },
            "SKIN");
    app.add_option("-t,--textures", opts.texturesArg,
                    "directory of already-converted '<FileDataID>.png' files, raw '<FileDataID>.blp' "
                    "files (decoded and embedded in-memory, no separate husk-blp step needed), or "
                    "'none' to skip embedding images (default: the model's own directory)");
    app.add_option("--textures-out", opts.texturesOutArg,
                    "directory to also write each --textures .blp's decoded .png to, mirroring its "
                    "location under --textures (default: unset -- decoded textures stay in-memory "
                    "only, embedded straight into the .glb, nothing written to disk)");
    app.add_option("--skin-dir", opts.skinDirArg,
                    "directory 'auto' searches for the SFID-declared '<FileDataID>.skin' file, or "
                    "'none' to skip that stage (default: the model's own directory)");
    app.add_option("-a,--anim", opts.animArg,
                    "'auto': inline + global-sequence + best-effort external directory search; "
                    "'inline': inline + global-sequence only, no external search; 'none': no "
                    "animation clips at all (bind pose only); or a directory of "
                    "'<FileDataID>.anim' files, falling back to "
                    "'<model-basename><animId>-<subId>.anim' when a FileDataID-named file isn't found")
        ->capture_default_str();
    app.add_option("--skel", opts.skelArg,
                    "external .skel path (only relevant for a model with 0 inline bones), or "
                    "'none' to never look for one (default: a same-basename '.skel' next to the "
                    "model, if any)");
    app.add_option("--lod", opts.lodArg,
                    "'<n>' or 'all' -- only meaningful when --skin resolves via 'auto' (default: "
                    "entry 0)");
    app.add_option("--bones-dir", opts.bonesDirArg,
                    "directory of already-extracted '<FileDataID>.bone' files (per the model's/"
                    "'.skel's BFID array), or 'none' to skip (default: the model's own directory) "
                    "-- attached as inert glTF extras only, never applied to the render");
    app.add_option("--phys", opts.physArg,
                    "external .phys path (per the model's own PFID scalar), or 'none' to never "
                    "look for one (default: a same-basename '.phys' next to the model, if any) -- "
                    "a minimal per-body placement anchor is attached as inert glTF extras; the "
                    "full body/shape/joint record set is available via 'husk dump-chunks' instead");
    app.add_flag("--collision", opts.collisionRequested,
                 "include the collision mesh, when present, as real (unskinned) geometry tagged "
                 "{\"collision\": true} in glTF extras -- off by default, since Blender's stock "
                 "importer has no concept of that tag and renders it like any other mesh, and the "
                 "collision hull is often larger than and visually occludes the real character; "
                 "the full body/shape/joint record set is also always available via "
                 "'husk dump-chunks'");
    app.add_option("--db2-dir", opts.db2DirArg,
                    "directory of real, already-extracted character-texture DB2 files "
                    "(chrmodelmaterial.db2/charcomponenttexturesections.db2/"
                    "chrmodeltexturelayer.db2/charcomponenttexturelayouts.db2, real lowercase "
                    "casc-tool filenames) -- combined with --dbd-dir and --char-layout-id to "
                    "attach real texture-layout placement geometry as inert glTF extras; unset "
                    "(default) skips this feature entirely, same as every other opt-in sidecar");
    app.add_option("--dbd-dir", opts.dbdDirArg,
                    "a local WoWDBDefs checkout (github.com/wowdev/WoWDBDefs), used to resolve "
                    "--db2-dir's real column names -- required alongside --db2-dir/"
                    "--char-layout-id, same role as `husk db2-export`'s own --dbd-dir");
    app.add_option("--char-layout-id", opts.charLayoutIdArg,
                    "a real CharComponentTextureLayoutsID (see `husk db2-export`) to filter "
                    "--db2-dir's data down to -- husk has no way to derive which layout ID "
                    "applies to a given .m2 model on its own, so this must be supplied directly; "
                    "requires --db2-dir/--dbd-dir too");
}

int exportGlb(int argc, char** args) {
    ExportOptions opts;
    CLI::App app{"husk export: WoW M2 (+ .skin/.skel/.anim sidecars) -> glTF 2.0 (.glb)",
                 "husk export"};
    addExportOptions(app, opts);

    try {
        // App::parse(vector<string>&) processes tokens back-to-front
        // (mirroring how its (argc, argv) sibling reverses argv before
        // handing off to the same internal _parse) -- reverse `args` into
        // that expected order, or every flag's value binds to the wrong
        // neighbor.
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    const std::string& modelPath = opts.modelPath;
    std::string outputPath = opts.outputPath;
    bool skinDirGiven = app.count("--skin-dir") > 0;
    bool lodGiven = !opts.lodArg.empty();

    // --skin-dir/--lod only mean anything alongside --skin auto (the
    // default) -- an explicit .skin path has no SFID-resolution stage for
    // either of them to modify.
    if (opts.skinArg != "auto") {
        if (skinDirGiven) {
            std::cerr << "husk: --skin-dir only does anything when --skin is 'auto'\n";
            return 1;
        }
        if (lodGiven) {
            std::cerr << "husk: --lod only does anything when --skin is 'auto'\n";
            return 1;
        }
    }
    if (lodGiven && skinDirGiven && opts.skinDirArg == "none") {
        std::cerr << "husk: --lod needs the SFID-based resolution --skin-dir 'none' disables\n";
        return 1;
    }

    if (app.count("--output") == 0) {
        outputPath = std::filesystem::path(modelPath).replace_extension(".glb").string();
        std::cerr << "husk: note: no output path given -- writing to '" << outputPath << "'\n";
    }

    std::filesystem::path modelDir = std::filesystem::path(modelPath).parent_path();
    std::string modelDirStr = modelDir.empty() ? "." : modelDir.string();

    // --textures/--skin-dir: three-state resolution (~/docs/CLI.md §2.11)
    // -- unset defaults to the model's own directory (a real casc-tool-
    // style extraction drops the .m2 and every sidecar it needs into one
    // directory together, see README.md's Usage section); an explicit
    // 'none' means deliberately skip, never attempted; anything else
    // overrides. (--skel gets the identical three states below, but its
    // "unset" default -- a same-basename '.skel' next to the model -- is a
    // filesystem *check*, not a fixed directory, so it's resolved later,
    // once bones are known to actually be needed.)
    std::string texturesDir = app.count("--textures") ? opts.texturesArg : modelDirStr;
    if (texturesDir == "none") texturesDir.clear();

    // --textures-out: unset (the default) means "no disk copy at all" --
    // unlike --textures/--skin-dir/--bones-dir, there's no directory this
    // should silently default to, since writing files unasked is a much
    // bigger deal than reading them.
    std::string texturesOutDir = app.count("--textures-out") ? opts.texturesOutArg : "";

    std::string bonesDir = app.count("--bones-dir") ? opts.bonesDirArg : modelDirStr;
    if (bonesDir == "none") bonesDir.clear();

    bool skinDirNone = skinDirGiven && opts.skinDirArg == "none";
    std::string skinDir = skinDirNone ? "" : (skinDirGiven ? opts.skinDirArg : modelDirStr);

    // --anim: four states, not three (see DESIGN.md's "Does inline
    // generalize past --anim?") -- inline M2Sequence/global-sequence bone
    // tracks and external-.anim-directory resolution are independent axes.
    // 'inline' deliberately leaves `animDir` empty rather than setting a
    // separate flag: buildAnimations already treats an empty animDir as
    // "skip external resolution," which is exactly what 'inline' means.
    std::string animDir;
    bool animNone = false;
    if (opts.animArg == "auto") {
        animDir = modelDirStr;
    } else if (opts.animArg == "none") {
        animNone = true;
    } else if (opts.animArg != "inline") {
        animDir = opts.animArg;
    }

    bool skelGiven = app.count("--skel") > 0;
    bool skelNone = skelGiven && opts.skelArg == "none";
    std::string skelPath = (skelGiven && !skelNone) ? opts.skelArg : "";

    bool physGiven = app.count("--phys") > 0;
    bool physNone = physGiven && opts.physArg == "none";
    std::string physPath = (physGiven && !physNone) ? opts.physArg : "";

    // --db2-dir/--dbd-dir/--char-layout-id: no three-state resolution here
    // (unlike --bones-dir/--phys) -- there's no model-relative default to
    // fall back to, since husk has no way to derive a layout ID on its own
    // (see chrmodel_db2.hpp's module comment). All three must be given
    // together or the feature is simply off.
    std::string db2Dir = app.count("--db2-dir") ? opts.db2DirArg : "";
    std::string dbdDirForChr = app.count("--dbd-dir") ? opts.dbdDirArg : "";
    std::string charLayoutIdArg = app.count("--char-layout-id") ? opts.charLayoutIdArg : "";

    try {
        auto modelBytes = readFileBytes(modelPath);
        auto header = m2::parseHeader(modelBytes);
        auto blob = m2::extractBlob(modelBytes);
        auto vertices = m2::parseVertices(blob, header.vertices);

        if (header.version < m2::kMinVerifiedRecordStrideVersion) {
            std::cerr << "husk: warning: '" << modelPath << "' is version " << header.version
                      << ", below Wrath (264) -- this parser's bones/sequences/ribbon_emitters "
                         "record sizes are only documented and verified for Wrath+; the "
                         "exported bind pose/animation/ribbon data may be silently wrong rather "
                         "than failing loudly\n";
        }

        M2MaterialInputs m2Inputs;
        m2Inputs.materials = m2::parseMaterials(blob, header.materials);
        m2Inputs.textures = m2::parseTextures(blob, header.textures);
        m2Inputs.textureCombos = m2::parseUint16Array(blob, header.textureCombos);
        m2Inputs.textureCoordCombos = m2::parseUint16Array(blob, header.textureCoordCombos);
        m2Inputs.colors = m2::parseColors(blob, header.colors);
        m2Inputs.textureWeights = m2::parseTextureWeights(blob, header.textureWeights);
        m2Inputs.textureWeightCombos = m2::parseUint16Array(blob, header.textureWeightCombos);
        m2Inputs.textureTransforms = m2::parseTextureTransforms(blob, header.textureTransforms);
        m2Inputs.textureTransformCombos = m2::parseUint16Array(blob, header.textureTransformCombos);
        m2Inputs.textureFileDataIds = header.textureFileDataIds;
        m2Inputs.blob = &blob;
        m2Inputs.sequenceCount = header.sequences.count;

        auto skinsToExport =
            resolveSkinsToExport(header, modelPath, skinDir, skinDirNone, lodGiven, opts.lodArg, opts.skinArg);

        gltf::Mesh baseMesh = buildBaseMesh(vertices);

        bool bonesAreInline = false;
        bool haveSkel = false;
        std::vector<uint8_t> skelBytes;
        auto bones = resolveBones(modelPath, blob, header, skelGiven, skelNone, skelPath, bonesAreInline,
                                   haveSkel, skelBytes);

        gltf::Skeleton skeleton;
        std::vector<gltf::Animation> animations;
        if (!bones.empty()) {
            skeleton = buildSkeleton(bones);
            baseMesh.skinning = buildSkinning(vertices, bones.size());
            animations = resolveAnimationsForModel(animNone, bonesAreInline, haveSkel, header, blob,
                                                    skelBytes, bones, skeleton, animDir, modelPath);
            attachBoneCorrections(bonesDir, bonesAreInline, haveSkel, header, skelBytes, skeleton);
            attachEmitterAnchors(blob, header, skeleton);
            attachPlacementNodes(blob, header, header.sequences.count, skeleton);
            attachPhysicsBodies(physNone, physGiven, physPath, modelPath, skeleton);
            attachCharTextureLayout(db2Dir, dbdDirForChr, charLayoutIdArg, skeleton);
            // Needs skeleton.attachments/events already populated (just
            // above), so it can't run inside buildSkeleton itself.
            applyContextualBoneNames(skeleton);
        }

        std::string modelBasename = std::filesystem::path(modelPath).stem().string();
        auto namedMeshes =
            buildLodTierMeshes(skinsToExport, vertices, baseMesh, m2Inputs, texturesDir, modelPath,
                                modelBasename, texturesOutDir);

        // One geoset tag joint per distinct skinSectionId across every LOD
        // tier's primitives -- lets tools/husk_blender_geoset_mask.py
        // toggle mutually-exclusive geoset variants (hairstyles,
        // boot cuffs, eye-glow, ...) that husk itself has no DBC/DB2 data
        // to filter (see gltf_skeleton.hpp's Skeleton::GeosetTag doc
        // comment for the full mechanism). std::set for dedup + a
        // deterministic (numeric) order; only meaningful alongside a real
        // skeleton -- an unskinned model has no `skin.joints` to append to.
        if (!bones.empty()) {
            std::set<int> distinctGeosetIds;
            for (const auto& nm : namedMeshes) {
                for (const auto& prim : nm.mesh.primitives) {
                    if (prim.skinSectionId >= 0) distinctGeosetIds.insert(prim.skinSectionId);
                }
            }
            for (int id : distinctGeosetIds) {
                skeleton.geosetTags.push_back({id});
            }
        }

        // Captured before the collision mesh (if any) is appended below --
        // the final summary needs to know how many of `namedMeshes` are
        // real render/LOD entries versus the trailing collision entry, so
        // it doesn't mislabel the collision mesh as another LOD tier.
        size_t renderMeshCount = namedMeshes.size();
        appendCollisionMesh(header, blob, modelPath, opts.collisionRequested, namedMeshes);

        auto glb = gltf::writeGlbMulti(namedMeshes, bones.empty() ? nullptr : &skeleton, animations);

        errno = 0;
        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("couldn't open '" + outputPath +
                                      "' for writing: " + std::strerror(errno));
        }
        out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
        if (!out) {
            throw std::runtime_error("error writing '" + outputPath + "'");
        }

        printExportSummary(outputPath, vertices, bones, animations, namedMeshes, renderMeshCount);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: export failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
