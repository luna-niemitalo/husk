#include "export_extras.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include "bone.hpp"
#include "chrcustomization_db2.hpp"
#include "chrmodel_db2.hpp"
#include "chrrace_db2.hpp"
#include "creature_geoset_db2.hpp"
#include "export_transform.hpp"
#include "phys.hpp"
#include "skel.hpp"
#include "texturefiledata_db2.hpp"

// The attachX() helper group's definitions -- split out of cmd_export.cpp
// per FILE_SPLIT_TODO.md's Item 1. See export_extras.hpp for the doc
// comments on each of these (preserved there verbatim, not duplicated here).
namespace husk::commands {

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

namespace {

// Resolves an M2Light color track (ambient_color/diffuse_color, both
// M2Track<C3Vector>) via the shared resolveAnimatedCurveGeneric
// (export_transform.hpp) -- the exact same shape
// export_texture_resolution.cpp's own resolveAnimatedColorCurve resolves
// M2Color::color with (both were duplicated near-verbatim before that
// template existed). `color`'s x/y/z are already 0..1 RGB, not a spatial
// vector, so no toGltf()/Z-up remap.
std::vector<gltf::Material::AnimatedColorCurve> resolveLightColorCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    return resolveAnimatedCurveGeneric<gltf::Material::AnimatedColorCurve>(
        blob, trackOffset, sequenceCount, m2::resolveVec3TrackSequence, m2::resolveVec3GlobalSequenceTrack,
        [](const m2::Vec3& v) { return gltf::Vec3{v.x, v.y, v.z}; });
}

// Resolves an M2Light M2Track<float> field (ambient_intensity/
// diffuse_intensity/attenuation_start/attenuation_end) -- unlike
// export_texture_resolution.cpp's fixed16-based fade curves, these are
// already plain floats on the wire (resolveFloatTrackSequence, not
// resolveRawIntTrackSequence + a fixed16 decode), so `toValue` is a no-op.
std::vector<gltf::Material::AnimatedScalarCurve> resolveLightFloatCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    return resolveAnimatedCurveGeneric<gltf::Material::AnimatedScalarCurve>(
        blob, trackOffset, sequenceCount, m2::resolveFloatTrackSequence,
        m2::resolveFloatGlobalSequenceTrack, [](float v) { return v; });
}

// Resolves an M2Track<uint8_t> boolean-ish flag track -- M2Light::visibility
// ("enabled?") and M2Attachment::animate_attached both have this exact
// shape, a raw 0/1 flag, not a fixed16-scaled value, so each keyframe's
// zero-extended raw byte is cast straight to float rather than run through
// export_texture_resolution.cpp's decodeFixed16.
std::vector<gltf::Material::AnimatedScalarCurve> resolveRawByteTrackCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    auto resolveSeq = [](const std::vector<uint8_t>& b, uint32_t off, uint32_t si,
                          const std::vector<uint8_t>* ext) {
        return m2::resolveRawIntTrackSequence(b, off, si, /*elementSize=*/1, ext);
    };
    auto resolveGlobal = [](const std::vector<uint8_t>& b, uint32_t off, const std::vector<uint8_t>* ext) {
        return m2::resolveRawIntGlobalSequenceTrack(b, off, /*elementSize=*/1, ext);
    };
    return resolveAnimatedCurveGeneric<gltf::Material::AnimatedScalarCurve>(
        blob, trackOffset, sequenceCount, resolveSeq, resolveGlobal,
        [](uint32_t bits) { return static_cast<float>(bits); });
}

}  // namespace

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
        skeleton.events.push_back({e.identifier, static_cast<int>(e.bone), toGltf(e.position), e.data});
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

    // See gltf::Skeleton::PhysicsJoint's own doc comment for why this is a
    // reduced, jiggle-tool-shaped view of the real joint graph, not the
    // full resolved record `dump_phys.cpp`'s `writePhysJoint` produces.
    skeleton.physicsJoints.reserve(physFile.joints.size());
    for (const auto& j : physFile.joints) {
        gltf::Skeleton::PhysicsJoint pj;
        pj.bodyA = j.bodyA;
        pj.bodyB = j.bodyB;
        switch (j.type) {
            case 1: {  // shoulder
                const auto& s = physFile.shoulderJoints[static_cast<size_t>(j.index)];
                pj.frequencyHz = s.motorFrequencyHz;
                pj.dampingRatio = s.motorDampingRatio;
                pj.swingLimitDeg = s.coneAngle;
                break;
            }
            case 2: {  // weld
                const auto& w = physFile.weldJoints[static_cast<size_t>(j.index)];
                pj.frequencyHz = w.angularFrequencyHz != 0.0f ? w.angularFrequencyHz : w.linearFrequencyHz;
                pj.dampingRatio = w.angularFrequencyHz != 0.0f ? w.angularDampingRatio : w.linearDampingRatio;
                break;
            }
            case 3: {  // revolute
                const auto& r = physFile.revoluteJoints[static_cast<size_t>(j.index)];
                pj.frequencyHz = r.motorFrequencyHz;
                pj.dampingRatio = r.motorDampingRatio;
                pj.swingLimitDeg = std::abs(r.upperAngle - r.lowerAngle);
                break;
            }
            case 4: {  // prismatic
                const auto& p = physFile.prismaticJoints[static_cast<size_t>(j.index)];
                pj.frequencyHz = p.motorFrequencyHz;
                pj.dampingRatio = p.motorDampingRatio;
                break;
            }
            default:
                break;  // spherical/distance -- no spring or swing-limit field, left at 0
        }
        skeleton.physicsJoints.push_back(pj);
    }

    if (!skeleton.physicsBodies.empty()) {
        std::cerr << "husk: note: attached " << skeleton.physicsBodies.size()
                  << " physics body placement anchor(s) and " << skeleton.physicsJoints.size()
                  << " reduced joint spring/limit record(s) from '" << resolvedPhysPath
                  << "' as inert glTF extras (id/bone/position/type, and body_a/body_b/frequency/"
                     "damping/swing-limit only -- full body/shape/joint record data, including "
                     "shape geometry and frame matrices, still needs `husk dump-chunks`)\n";
    }
}

namespace {

// Reverse lookup against an already-loaded --listfile map (FileDataID ->
// real path): finds `modelPath`'s own real FileDataID by matching its
// path relative to `listfileRoot` against the listfile's own paths,
// case-insensitively. A linear scan, not an index -- only run once per
// export when --chr-model-id auto needs it, not worth the memory of a
// second, reversed copy of a multi-million-row community listfile for a
// single lookup. Returns nullopt when --listfile wasn't given, the model
// isn't under --listfile-root, or no listfile row matches -- all three
// are "primary path unavailable," not errors, since --chr-model-id auto
// falls back to filename-based matching when this comes back empty.
std::optional<uint32_t> findFileDataIdForModelPath(const std::unordered_map<uint32_t, std::string>& listfile,
                                                     const std::string& modelPath,
                                                     const std::string& listfileRoot) {
    if (listfile.empty() || listfileRoot.empty()) return std::nullopt;
    std::error_code ec;
    auto rel = std::filesystem::relative(std::filesystem::path(modelPath), listfileRoot, ec);
    if (ec) return std::nullopt;
    std::string relPath = rel.generic_string();
    std::transform(relPath.begin(), relPath.end(), relPath.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    for (const auto& [fdid, path] : listfile) {
        if (path == relPath) return fdid;
    }
    return std::nullopt;
}

// Real primary(FileDataID)/fallback(filename) ChrModelID derivation --
// the same logic `--chr-model-id auto` uses (src/chrrace_db2.hpp), factored
// out so attachCustomizationChoices below can also attempt it best-effort
// even when the caller only gave --customization-choice-ids (no
// --chr-model-id at all) -- see that function's own doc comment for why.
// Returns nullopt (with a reason already reported to `err`) on any real
// failure -- never a guess.
std::optional<uint32_t> tryDeriveChrModelId(const std::string& db2Dir, const std::string& dbdDir,
                                             const std::string& modelPath,
                                             const std::unordered_map<uint32_t, std::string>& listfile,
                                             const std::string& listfileRoot, std::ostream& err) {
    std::optional<chrrace::Data> raceData = chrrace::load(db2Dir, dbdDir, err);
    if (!raceData) {
        err << "husk: note: --chr-model-id auto: no chrraces.db2/chrracexchrmodel.db2/"
               "chrmodel.db2/creaturedisplayinfo.db2/creaturemodeldata.db2 data resolved from '"
            << db2Dir << "' -- skipping\n";
        return std::nullopt;
    }

    // Primary path: the model's own real FileDataID (via --listfile),
    // resolved through CreatureModelData/CreatureDisplayInfo/ChrModel --
    // exact file identity, never ambiguous the way race+sex alone can be
    // (e.g. Dracthyr's dragon form is a real, valid answer to "Dracthyr,
    // male" too, but not to "this specific FileDataID"). Once this path
    // finds a real FileDataID match at all, its own answer (including a
    // reported ambiguity) is trusted over the weaker filename fallback,
    // not silently overridden by it.
    std::optional<uint32_t> modelFdid = findFileDataIdForModelPath(listfile, modelPath, listfileRoot);
    std::optional<uint32_t> derived;
    if (modelFdid) {
        derived = chrrace::deriveChrModelIdFromFileDataId(*raceData, *modelFdid, err);
        if (derived) {
            err << "husk: note: --chr-model-id auto: derived ChrModelID " << *derived << " from '"
                << modelPath << "'s own FileDataID " << *modelFdid
                << " (CreatureModelData/CreatureDisplayInfo/ChrModel chain)\n";
        }
    }

    // Fallback: filename-based race+sex matching, only when the primary
    // path never got a real FileDataID to work with at all (no --listfile,
    // or this path isn't under --listfile-root/in it).
    if (!derived && !modelFdid) {
        std::optional<chrrace::ParsedName> parsed = chrrace::parseModelBasename(modelPath);
        if (!parsed) {
            err << "husk: note: --chr-model-id auto: '" << modelPath
                << "''s own filename doesn't end in \"male\"/\"female\" (after an optional \"_hd\" "
                   "suffix) -- doesn't match the real character-model naming convention, skipping\n";
            return std::nullopt;
        }
        derived = chrrace::deriveChrModelId(*raceData, *parsed, err);
        if (derived) {
            err << "husk: note: --chr-model-id auto: derived ChrModelID " << *derived << " from '"
                << modelPath << "' (race token '" << parsed->raceToken << "', sex " << parsed->sex
                << ") -- filename fallback, no --listfile match for this model\n";
        }
    }

    return derived;
}

}  // namespace

void attachCustomizationChoices(const std::string& db2Dir, const std::string& dbdDir,
                                 const std::string& choiceIdsArg, const std::string& chrModelIdArg,
                                 const std::string& modelPath,
                                 const std::unordered_map<uint32_t, std::string>& listfile,
                                 const std::string& listfileRoot, gltf::Skeleton& skeleton) {
    if (db2Dir.empty() && dbdDir.empty() && choiceIdsArg.empty() && chrModelIdArg.empty()) {
        return;  // feature simply unused
    }
    if (db2Dir.empty() || dbdDir.empty()) {
        std::cerr << "husk: note: --db2-dir/--dbd-dir are required for any customization-choice "
                     "extras (--customization-choice-ids/--chr-model-id are optional overrides on "
                     "top of them, not required themselves) -- skipping\n";
        return;
    }
    // --customization-choice-ids/--chr-model-id are deliberately NOT
    // required beyond this point -- given only --db2-dir/--dbd-dir, this
    // function still attempts the same auto-derivation --chr-model-id
    // auto/tryDeriveChrModelId always did, and the same lowest-OrderIndex
    // default-choice heuristic --chr-model-id already used, entirely on
    // its own. Luna's own direct instruction: this should work by default,
    // not only when the caller separately utters --customization-choice-ids
    // or --chr-model-id. Both flags remain real overrides -- an explicit
    // --chr-model-id skips auto-derivation, an explicit
    // --customization-choice-ids skips the default-choice heuristic --
    // just no longer *required* to get anything at all.

    std::optional<chrcustomization::Data> data = chrcustomization::load(db2Dir, dbdDir, std::cerr);
    if (!data) {
        std::cerr << "husk: note: no customization-choice DB2 data resolved from '" << db2Dir
                  << "' -- skipping\n";
        return;
    }

    // Resolved both for default-choice picking below (when choiceIdsArg is
    // empty) and, whenever resolvable at all, to attach the FULL real
    // per-option choice menu near the end of this function -- automatic,
    // not gated behind any separate opt-in flag (Luna's own direct
    // instruction: this should be included by default, not only when the
    // caller happens to also ask for it). Left empty when neither an
    // explicit --chr-model-id nor auto-derivation produced a real answer.
    std::optional<uint32_t> resolvedChrModelId;

    std::vector<uint32_t> choiceIds;
    if (!choiceIdsArg.empty()) {
        std::stringstream ss(choiceIdsArg);
        std::string token;
        while (std::getline(ss, token, ',')) {
            try {
                choiceIds.push_back(static_cast<uint32_t>(std::stoul(token)));
            } catch (const std::exception&) {
                std::cerr << "husk: note: --customization-choice-ids entry '" << token
                          << "' isn't a non-negative integer -- skipping customization-choice extras\n";
                return;
            }
        }
        if (choiceIds.empty()) {
            std::cerr << "husk: note: --customization-choice-ids resolved to no real IDs -- skipping\n";
            return;
        }

        // Best-effort enrichment: also try to resolve a real ChrModelID
        // purely so the full per-option menu can still be attached below,
        // even though the caller only gave explicit choice IDs -- never
        // fatal to this function's own primary job (the explicit
        // choiceIds just parsed above). Reuses --chr-model-id's own value
        // directly when it was also given (no extra DB2 read needed)
        // rather than re-deriving. Same auto | none | <id> three-state
        // convention --textures/--skin-dir/--skel already use: unset
        // (empty) or "auto" both mean "try to derive it," "none" means
        // "don't," an explicit numeric ID overrides outright.
        if (chrModelIdArg == "none") {
            // Explicit opt-out -- no enrichment attempt at all.
        } else if (!chrModelIdArg.empty() && chrModelIdArg != "auto") {
            try {
                resolvedChrModelId = static_cast<uint32_t>(std::stoul(chrModelIdArg));
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch)
                // A garbage --chr-model-id alongside explicit choice IDs --
                // the explicit choice IDs are what matters here, so this is
                // a real but non-fatal gap: no full-menu enrichment this run.
            }
        } else {
            resolvedChrModelId = tryDeriveChrModelId(db2Dir, dbdDir, modelPath, listfile, listfileRoot, std::cerr);
        }
    } else {
        // Same auto | none | <id> convention as above -- unset/"auto" both
        // derive, "none" opts out entirely (nothing else to do without a
        // real --customization-choice-ids either, so return), an explicit
        // numeric ID overrides.
        if (chrModelIdArg == "none") {
            return;  // explicit opt-out, and no --customization-choice-ids either -- nothing to do
        }
        if (chrModelIdArg.empty() || chrModelIdArg == "auto") {
            resolvedChrModelId = tryDeriveChrModelId(db2Dir, dbdDir, modelPath, listfile, listfileRoot, std::cerr);
            if (!resolvedChrModelId) return;  // tryDeriveChrModelId already reported why
        } else {
            try {
                resolvedChrModelId = static_cast<uint32_t>(std::stoul(chrModelIdArg));
            } catch (const std::exception&) {
                std::cerr << "husk: note: --chr-model-id '" << chrModelIdArg
                          << "' isn't a non-negative integer, 'auto', or 'none' -- skipping "
                             "customization-choice extras\n";
                return;
            }
        }
        choiceIds = chrcustomization::defaultChoiceIdsForModel(*data, *resolvedChrModelId);
        if (choiceIds.empty()) {
            std::cerr << "husk: note: --chr-model-id " << *resolvedChrModelId
                      << " resolved no ChrCustomizationOption/Choice rows (chrcustomizationoption.db2/"
                         "chrcustomizationchoice.db2 not loaded, or no options for this model) -- "
                         "skipping\n";
            return;
        }
        std::cerr << "husk: note: --chr-model-id " << *resolvedChrModelId << ": auto-selected "
                  << choiceIds.size()
                  << " default choice(s) (lowest OrderIndex per option -- husk's own heuristic, not "
                     "a client-verified default; see --chr-model-id's own help text):\n";
        for (const auto& nc : chrcustomization::namedChoicesForModel(*data, *resolvedChrModelId, std::cerr)) {
            if (std::find(choiceIds.begin(), choiceIds.end(), nc.choiceId) == choiceIds.end()) continue;
            std::cerr << "husk:   " << nc.optionName << " (option " << nc.optionId << ") -> "
                      << nc.choiceName << " (choice " << nc.choiceId << ", OrderIndex "
                      << nc.choiceOrderIndex << ")\n";
        }
    }

    // Loaded once, unconditionally -- cheap relative to the DB2 reads
    // already done above, and materialsResolved below needs it for every
    // choice that carries a real ChrCustomizationMaterialID. A missing/
    // empty texturefiledata.db2 isn't fatal: TextureFileData::load
    // returning nullopt just means every material below reports
    // fileDataId 0 (unresolved), same "real, reportable gap, not
    // fabricated" policy as everywhere else here.
    std::optional<texturefiledata::Data> tfdData = texturefiledata::load(db2Dir, dbdDir, std::cerr);
    auto resolveFileDataId = [&tfdData](uint32_t materialResourcesId) -> uint32_t {
        if (!tfdData) return 0;
        auto it = tfdData->find(materialResourcesId);
        return it != tfdData->end() ? it->second : 0;
    };

    // For filtering resolution.materials below: a material whose own
    // Element row carries a nonzero relatedChoiceId only applies when that
    // *other* choice is also part of this same export's real selection --
    // see chrcustomization::Element::relatedChoiceId's own doc comment for
    // the real data (a "Tiara" Hairstyle choice with 10 conditional
    // materials, one per real Hair Color choice) that makes this matter:
    // without this filter, resolving Tiara alone would attach all 10 as if
    // simultaneously valid, when only the one matching the caller's own
    // selected Hair Color choice actually is.
    std::unordered_set<uint32_t> choiceIdSet(choiceIds.begin(), choiceIds.end());

    size_t geosetsResolved = 0;
    size_t boneSetsMatched = 0;
    size_t materialsResolved = 0;
    size_t materialsSkippedUnmatchedRelated = 0;
    for (uint32_t choiceId : choiceIds) {
        chrcustomization::Resolution resolution = chrcustomization::resolveChoice(*data, choiceId, std::cerr);
        if (resolution.geosetId) {
            skeleton.enabledGeosets.push_back({choiceId, *resolution.geosetId});
            ++geosetsResolved;
        }
        if (resolution.boneFileDataId) {
            bool matched = false;
            for (auto& cs : skeleton.correctionSets) {
                if (cs.fileDataId == *resolution.boneFileDataId) {
                    cs.selectedByChoiceIds.push_back(choiceId);
                    matched = true;
                    break;
                }
            }
            if (matched) {
                ++boneSetsMatched;
            } else {
                std::cerr << "husk: note: ChrCustomizationChoiceID " << choiceId
                          << " resolves to BoneFileDataID " << *resolution.boneFileDataId
                          << ", but that FileDataID wasn't among this model's own resolved "
                             "--bones-dir correction sets -- not marked\n";
            }
        }
        for (const auto& matRes : resolution.materials) {
            if (matRes.relatedChoiceId != 0 && !choiceIdSet.count(matRes.relatedChoiceId)) {
                std::cerr << "husk: note: ChrCustomizationChoiceID " << choiceId
                          << "'s MaterialResourcesID " << matRes.materialResourcesId
                          << " only applies together with ChrCustomizationChoiceID "
                          << matRes.relatedChoiceId << ", which isn't part of this export's own "
                             "selection -- not attaching\n";
                ++materialsSkippedUnmatchedRelated;
                continue;
            }
            uint32_t fileDataId = resolveFileDataId(matRes.materialResourcesId);
            if (fileDataId == 0) {
                std::cerr << "husk: note: ChrCustomizationChoiceID " << choiceId
                          << " resolves to MaterialResourcesID " << matRes.materialResourcesId
                          << ", but texturefiledata.db2 didn't resolve a real FileDataID for it -- "
                             "attaching with file_data_id 0\n";
            }
            skeleton.enabledMaterials.push_back(
                {choiceId, matRes.chrModelTextureTargetId, matRes.materialResourcesId, fileDataId});
            ++materialsResolved;
        }
    }
    if (materialsSkippedUnmatchedRelated) {
        std::cerr << "husk: note: " << materialsSkippedUnmatchedRelated << " conditional material(s) "
                     "skipped (their own related choice wasn't part of this export's selection)\n";
    }
    std::cerr << "husk: note: resolved " << choiceIds.size() << " customization choice ID(s): "
              << geosetsResolved << " real geoset selection(s), " << boneSetsMatched
              << " matched bone-correction-set selection(s), " << materialsResolved
              << " real material selection(s)\n";

    // The full real customization menu -- every real ChrCustomizationOption/
    // Choice for this model, not just the choice(s) resolved into
    // enabledGeosets/enabledMaterials above. Attached automatically whenever
    // resolvedChrModelId ended up with a real answer at all (see its own
    // doc comment above) -- no separate opt-in flag, so a Blender script
    // building a live choice switch (TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md)
    // never needs a second export run just to see what's selectable.
    if (resolvedChrModelId && !data->options.empty() && !data->choices.empty()) {
        std::vector<gltf::Skeleton::CustomizationOption> options;
        for (const auto& nc : chrcustomization::namedChoicesForModel(*data, *resolvedChrModelId, std::cerr)) {
            auto optIt = std::find_if(options.begin(), options.end(),
                                       [&](const gltf::Skeleton::CustomizationOption& o) {
                                           return o.optionId == nc.optionId;
                                       });
            if (optIt == options.end()) {
                options.push_back({nc.optionId, nc.optionName, nc.optionOrderIndex, {}});
                optIt = options.end() - 1;
            }

            gltf::Skeleton::CustomizationChoice choice;
            choice.choiceId = nc.choiceId;
            choice.choiceName = nc.choiceName;
            choice.choiceOrderIndex = nc.choiceOrderIndex;
            choice.geosetId = nc.resolution.geosetId;
            for (const auto& matRes : nc.resolution.materials) {
                choice.materials.push_back({matRes.chrModelTextureTargetId, matRes.materialResourcesId,
                                             resolveFileDataId(matRes.materialResourcesId),
                                             matRes.relatedChoiceId});
            }
            optIt->choices.push_back(std::move(choice));
        }
        if (!options.empty()) {
            size_t totalChoices = 0;
            for (const auto& o : options) totalChoices += o.choices.size();
            std::cerr << "husk: note: ChrModelID " << *resolvedChrModelId << ": attached the full real "
                      << "customization menu (" << options.size() << " option(s), " << totalChoices
                      << " choice(s) total) as inert chr_customization_options glTF extras\n";
            skeleton.customizationOptions = std::move(options);
        }
    }
}

void attachCharTextureLayout(const std::string& db2Dir, const std::string& dbdDir,
                              const std::string& charLayoutIdArg, const std::string& chrModelIdArg,
                              const std::string& modelPath,
                              const std::unordered_map<uint32_t, std::string>& listfile,
                              const std::string& listfileRoot, gltf::Skeleton& skeleton) {
    if (db2Dir.empty() && dbdDir.empty() && charLayoutIdArg.empty() && chrModelIdArg.empty()) {
        return;  // feature simply unused
    }
    if (db2Dir.empty() || dbdDir.empty()) {
        std::cerr << "husk: note: --db2-dir/--dbd-dir are required for character texture-layout "
                     "extras (--char-layout-id is an optional override on top of them, not required "
                     "itself) -- skipping\n";
        return;
    }

    uint32_t charLayoutId = 0;
    if (!charLayoutIdArg.empty()) {
        try {
            charLayoutId = static_cast<uint32_t>(std::stoul(charLayoutIdArg));
        } catch (const std::exception&) {
            std::cerr << "husk: note: --char-layout-id '" << charLayoutIdArg
                      << "' isn't a non-negative integer -- skipping character texture-layout extras\n";
            return;
        }
    } else if (chrModelIdArg == "none") {
        std::cerr << "husk: note: no --char-layout-id given and --chr-model-id none disables "
                     "auto-derivation -- skipping character texture-layout extras\n";
        return;
    } else {
        std::optional<uint32_t> resolvedChrModelId;
        if (!chrModelIdArg.empty() && chrModelIdArg != "auto") {
            try {
                resolvedChrModelId = static_cast<uint32_t>(std::stoul(chrModelIdArg));
            } catch (const std::exception&) {
                std::cerr << "husk: note: --chr-model-id '" << chrModelIdArg
                          << "' isn't a non-negative integer, 'auto', or 'none' -- skipping character "
                             "texture-layout extras\n";
                return;
            }
        } else {
            resolvedChrModelId =
                tryDeriveChrModelId(db2Dir, dbdDir, modelPath, listfile, listfileRoot, std::cerr);
            if (!resolvedChrModelId) return;  // tryDeriveChrModelId already reported why
        }

        std::optional<chrrace::Data> raceData = chrrace::load(db2Dir, dbdDir, std::cerr);
        if (!raceData) {
            std::cerr << "husk: note: no ChrModel.db2 data resolved from '" << db2Dir
                      << "' -- can't auto-derive --char-layout-id -- skipping character "
                         "texture-layout extras\n";
            return;
        }
        auto it = std::find_if(raceData->chrModels.begin(), raceData->chrModels.end(),
                                [&](const chrrace::ChrModelDisplay& m) {
                                    return m.chrModelId == *resolvedChrModelId;
                                });
        if (it == raceData->chrModels.end() || it->charComponentTextureLayoutId == 0) {
            std::cerr << "husk: note: ChrModelID " << *resolvedChrModelId
                      << " has no real CharComponentTextureLayoutID -- skipping character "
                         "texture-layout extras\n";
            return;
        }
        charLayoutId = it->charComponentTextureLayoutId;
        std::cerr << "husk: note: --char-layout-id auto-derived as " << charLayoutId
                  << " from ChrModelID " << *resolvedChrModelId << "\n";
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
            layout.textureLayers.push_back({t.id, t.textureType, t.layer, t.flags, t.blendMode,
                                             t.textureSectionTypeBitMask, t.chrModelTextureTargetId});
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

void attachCreatureGeosets(const std::string& db2Dir, const std::string& dbdDir,
                            const std::string& creatureDisplayIdArg, gltf::Skeleton& skeleton) {
    if (db2Dir.empty() && dbdDir.empty() && creatureDisplayIdArg.empty()) return;  // feature simply unused
    if (db2Dir.empty() || dbdDir.empty() || creatureDisplayIdArg.empty()) {
        std::cerr << "husk: note: --db2-dir/--dbd-dir/--creature-display-id must all be given "
                     "together -- skipping creature geoset extras\n";
        return;
    }

    uint32_t displayId = 0;
    try {
        displayId = static_cast<uint32_t>(std::stoul(creatureDisplayIdArg));
    } catch (const std::exception&) {
        std::cerr << "husk: note: --creature-display-id '" << creatureDisplayIdArg
                  << "' isn't a non-negative integer -- skipping creature geoset extras\n";
        return;
    }

    std::optional<creaturegeoset::Data> data = creaturegeoset::load(db2Dir, dbdDir, std::cerr);
    if (!data) {
        std::cerr << "husk: note: no CreatureDisplayInfoGeosetData resolved from '" << db2Dir
                  << "' -- skipping\n";
        return;
    }

    auto resolved = creaturegeoset::resolveDisplay(*data, displayId);
    if (resolved.empty()) {
        std::cerr << "husk: note: CreatureDisplayInfoID " << displayId
                  << " has no CreatureDisplayInfoGeosetData rows -- nothing to attach (real and "
                     "common: most displays enable none of their model's optional geosets)\n";
        return;
    }

    for (const auto& r : resolved) {
        skeleton.creatureEnabledGeosets.push_back({r.geosetIndex, r.geosetValue, r.geosetId});
    }
    std::cerr << "husk: note: attached " << resolved.size()
              << " real default geoset selection(s) for CreatureDisplayInfoID " << displayId
              << " as inert glTF extras\n";
}

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

}  // namespace husk::commands
