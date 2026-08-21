#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <CLI/CLI.hpp>
#include <sqlite3.h>

#include "animationdata_db2.hpp"
#include "chunk.hpp"
#include "commands.hpp"
#include "export_animation.hpp"
#include "export_extras.hpp"
#include "export_materials.hpp"
#include "export_skeleton.hpp"
#include "export_skin_resolution.hpp"
#include "export_transform.hpp"
#include "gltf.hpp"
#include "husk_config.hpp"
#include "listfile.hpp"
#include "m2.hpp"
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
std::vector<gltf::Animation> resolveAnimationsForModel(
    bool animNone, bool bonesAreInline, bool haveSkel, const m2::Header& header,
    const std::vector<uint8_t>& blob, const std::vector<uint8_t>& skelBytes, const std::vector<m2::Bone>& bones,
    const gltf::Skeleton& skeleton, const std::string& animDir, const std::string& modelPath,
    const std::unordered_map<uint32_t, std::string>* animationNames) {
    std::vector<gltf::Animation> animations;
    if (animNone) return animations;

    if (bonesAreInline) {
        auto sequences = m2::parseSequences(blob, header.sequences);
        M2AnimInputs animInputs;
        animInputs.animFileIds = header.animFileIds;
        animInputs.animChunked = (header.globalFlags & 0x200000) != 0;
        animInputs.animDir = animDir;
        animInputs.modelPath = modelPath;
        animations = buildAnimations(blob, bones, skeleton, sequences, animInputs, animationNames);
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
            animations = buildAnimations(skelTrackBlob, bones, skeleton, sequences, animInputs, animationNames);
        }
        auto globalSeqAnims = buildGlobalSequenceAnimations(skelTrackBlob, bones, skeleton);
        animations.insert(animations.end(), std::make_move_iterator(globalSeqAnims.begin()),
                           std::make_move_iterator(globalSeqAnims.end()));
    }
    return animations;
}

// --knowledge-db: queries husk's own pre-built knowledge base
// (TODO/KNOWLEDGE_BASE_DESIGN.md, `husk db2-build`) for this model's
// resolved object-skin texture, via its own FileDataID (looked up in the
// same database's `models` table by relative path against
// `--listfile-root`), and that texture's own real path (via the
// database's own `textures` table -- lets --knowledge-db alone resolve
// texture_id -> path cleanly, without also requiring a separate
// --listfile load just for this one texture). Never a live DB2/CASC read
// -- the knowledge base is itself a local, pre-built artifact, same
// "locally-extracted files only" tier as every other sidecar. Returns 0/
// empty (unresolved) rather than throwing on any lookup miss -- a model
// or texture outside the knowledge base's coverage is a real, expected
// case, not an error.
struct KbObjectSkinResolution {
    uint32_t textureFileDataId = 0;
    std::string texturePath;
};
KbObjectSkinResolution resolveObjectSkinTextureFromKb(const std::string& kbPath, const std::string& modelPath,
                                                        const std::string& listfileRoot) {
    KbObjectSkinResolution result;
    if (kbPath.empty()) return result;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(kbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "husk: note: --knowledge-db '" << kbPath << "' couldn't be opened -- skipping\n";
        sqlite3_close(db);
        return result;
    }

    std::error_code ec;
    std::string relPath;
    if (!listfileRoot.empty()) {
        auto rel = std::filesystem::relative(std::filesystem::path(modelPath), listfileRoot, ec);
        if (!ec) relPath = rel.generic_string();
    }
    if (relPath.empty()) {
        sqlite3_close(db);
        return result;
    }
    std::transform(relPath.begin(), relPath.end(), relPath.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    uint32_t modelFdid = 0;
    {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT file_data_id FROM models WHERE lower(path) = ?", -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, relPath.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) modelFdid = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
        sqlite3_finalize(stmt);
    }

    if (modelFdid != 0) {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT texture_file_data_id FROM model_object_skin_texture WHERE model_file_data_id = ?",
                            -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, modelFdid);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result.textureFileDataId = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    if (result.textureFileDataId != 0) {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db, "SELECT path FROM textures WHERE file_data_id = ?", -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, result.textureFileDataId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) result.texturePath = reinterpret_cast<const char*>(text);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return result;
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
    const std::string& texturesOutDir, const std::unordered_map<uint32_t, std::string>& listfile = {},
    const std::string& listfileRoot = "", uint32_t objectSkinTextureFileDataId = 0) {
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
                                                   texturesDir, modelPath, texturesOutDir, listfile,
                                                   listfileRoot.empty() ? texturesDir : listfileRoot,
                                                   objectSkinTextureFileDataId);

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
                         "always attached as inert 'extras' metadata, and a genuinely-animated "
                         "track's full keyframe curve is attached as 'texture_transform_animation'\n";
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

}  // namespace

// See commands.hpp's doc comment on ExportOptions: this is the one place
// export's flag surface is declared, shared by exportGlb's own real parse
// and main.cpp's `--print-completion` introspection.
void addExportOptions(CLI::App& app, ExportOptions& opts) {
    // TOML config file for defaults -- CLI11's own set_config() maps config keys onto
    // these exact add_option() registrations (below), so every flag's own ->check()
    // validator and CLI-flag > config > default precedence come for free, with no
    // second parser and no hand-written key->flag table to keep in sync (see
    // TODO/CLEANUP_TODO.md for the other commands still needing this migration
    // before they can get the same treatment). Path resolution: --config, else
    // $HUSK_CONFIG, else husk::defaultConfigPath() (XDG default), else no config --
    // a missing file at any of those isn't an error (config_required=false).
    app.set_config("--config", husk::defaultConfigPath(),
                    "TOML file of default flag values (see README.md's config-file "
                    "section) -- explicit CLI flags always override a config value")
        ->envname("HUSK_CONFIG");

    // Not ->required() here even though single-file mode needs it -- --from-list
    // mode (below) takes its model paths from a file instead, so requiredness is
    // enforced by hand after parsing (see exportGlb), once it's known which mode
    // this invocation is actually in.
    app.add_option("-i,--input,input", opts.modelPath, "the .m2 file to export");
    app.add_option("-o,--output,output", opts.outputPath,
                    "output .glb path (default: '<model-basename>.glb') -- mutually exclusive "
                    "with --from-list, which always writes into --output-dir instead");
    app.add_option("--from-list", opts.fromListArg,
                    "batch mode: a plain text file of .m2 paths, one per line (blank lines and "
                    "'#'-prefixed comment lines skipped) -- exports every one of them with the "
                    "same options given on this command line, opening --listfile once and reusing "
                    "it across the whole batch rather than reloading it per file, the same "
                    "'--from-list <ids-file> <out-dir>' shape casc-tool's own 'extract-batch' has. "
                    "Requires --output-dir instead of --output; one file failing is reported and "
                    "skipped, not fatal to the rest of the batch -- see the run's own final "
                    "'N succeeded, M failed' summary line");
    app.add_option("--output-dir", opts.outputDirArg,
                    "directory to write each --from-list entry's .glb into (named "
                    "'<model-basename>.glb', or '<parent-dir-name>_<model-basename>.glb' if two "
                    "entries in the same batch share a basename) -- required alongside "
                    "--from-list, meaningless without it");
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
                    "directory of real, already-extracted character/creature DB2 files "
                    "(chrmodelmaterial.db2/charcomponenttexturesections.db2/"
                    "chrmodeltexturelayer.db2/charcomponenttexturelayouts.db2 for --char-layout-id; "
                    "chrcustomizationelement.db2/chrcustomizationgeoset.db2/"
                    "chrcustomizationboneset.db2 for --customization-choice-ids; "
                    "creaturedisplayinfogeosetdata.db2 for --creature-display-id -- real lowercase "
                    "casc-tool filenames, same directory serves all three) -- combined with --dbd-dir "
                    "and one of --char-layout-id/--customization-choice-ids/--creature-display-id to "
                    "attach real DB2 extras; unset (default) skips these features entirely, same as "
                    "every other opt-in sidecar");
    app.add_option("--dbd-dir", opts.dbdDirArg,
                    "a local WoWDBDefs checkout (github.com/wowdev/WoWDBDefs), used to resolve "
                    "--db2-dir's real column names -- required alongside --db2-dir/"
                    "--char-layout-id or --db2-dir/--customization-choice-ids, same role as "
                    "`husk db2-export`'s own --dbd-dir");
    app.add_option("--char-layout-id", opts.charLayoutIdArg,
                    "a real CharComponentTextureLayoutsID (see `husk db2-export`) to filter "
                    "--db2-dir's data down to. Optional -- unset (default) auto-derives it from "
                    "the resolved --chr-model-id's own real ChrModel.CharComponentTextureLayoutID "
                    "column (same auto|none|<id> derivation --chr-model-id itself already uses; "
                    "--chr-model-id none disables this too); an explicit value always overrides. "
                    "Requires --db2-dir/--dbd-dir either way");
    app.add_option("--customization-choice-ids", opts.customizationChoiceIdsArg,
                    "comma-separated real ChrCustomizationChoiceID(s) to resolve against "
                    "--db2-dir -- attaches each choice's real geoset selection as "
                    "'enabled_geosets' skin extras, and marks "
                    "any matching --bones-dir correction set with 'selected_by_choice_ids'; "
                    "requires --db2-dir/--dbd-dir too, doesn't filter/apply anything itself, same "
                    "inert-extras treatment as --char-layout-id");
    app.add_option("--chr-model-id", opts.chrModelIdArg,
                    "a real ChrModelID (see ChrModel.db2 / `husk db2-export`), the literal 'auto' to "
                    "derive one automatically, or 'none' to explicitly disable derivation. Same "
                    "auto|none|<id> three-state convention as --textures/--skin-dir/--skel -- "
                    "**unset defaults to 'auto'**: whenever --db2-dir/--dbd-dir are given at all, "
                    "husk now tries to derive a real character identity and attach its full real "
                    "customization menu (chr_customization_options) automatically, with no separate "
                    "flag needed. Two derivation paths, tried in order: (1) primary, "
                    "if --listfile/--listfile-root resolve this .m2's own real FileDataID -- an exact "
                    "identity match via CreatureModelData/CreatureDisplayInfo/ChrModel, never "
                    "ambiguous for a real file (e.g. correctly tells dracthyrmale.m2 apart from "
                    "dracthyrdragon.m2 even though both are valid 'Dracthyr, male' answers); "
                    "(2) fallback, only when no FileDataID was found: this .m2's own filename against "
                    "the real client-side <ClientFileString><\"male\"|\"female\">[_hd].m2 convention "
                    "(exact, case-insensitive match only, never fuzzy) -- can be genuinely ambiguous "
                    "when a race+sex has more than one real model (e.g. Dracthyr's dragon form). "
                    "Either path reports and skips rather than guessing when it can't resolve to "
                    "exactly one real ChrModelID. Either way, once resolved, auto-selects a "
                    "*sensible default* customization choice per option: the choice "
                    "with the lowest real OrderIndex under each ChrCustomizationOption belonging to "
                    "this model, mirroring the character-creation UI's own first-shown option; NOT a "
                    "client-verified default the way --creature-display-id's geoset selection is "
                    "(no DB2 table states an explicit player default), husk's own heuristic instead "
                    "-- see TODO/TODO_correctness.md #2. Ignored when --customization-choice-ids is "
                    "also given (an explicit pick always wins); requires --db2-dir/--dbd-dir too");
    app.add_option("--creature-display-id", opts.creatureDisplayIdArg,
                    "a real CreatureDisplayInfoID (see `husk db2-export`) to resolve against "
                    "--db2-dir's creaturedisplayinfogeosetdata.db2 -- attaches that display's real "
                    "*default* geoset selection as 'creature_enabled_geosets' skin extras (unlike "
                    "--customization-choice-ids, this is a true default, not a per-choice pick); "
                    "husk has no way to derive which display ID applies to a given .m2 model on its "
                    "own, so this must be supplied directly; requires --db2-dir/--dbd-dir too");
    app.add_option("--object-skin-texture-id", opts.objectSkinTextureIdArg,
                    "a real texture FileDataID to fill into any type=2 (object_skin) texture "
                    "slot that has no FileDataID of its own -- husk can't derive this on its "
                    "own (it needs the real ItemDisplayInfo/TextureFileData DB2 chain resolved "
                    "externally), so it must be supplied directly; unset (default) leaves those "
                    "slots unresolved, same as before this flag existed");
    app.add_option("--knowledge-db", opts.knowledgeDbArg,
                    "husk's own pre-built knowledge-base SQLite database (`husk db2-build`, "
                    "TODO/KNOWLEDGE_BASE_DESIGN.md) -- when given, resolves this model's own "
                    "object-skin texture automatically (via the database's own model->texture "
                    "mapping, keyed by the model's FileDataID under --listfile-root), instead of "
                    "requiring --object-skin-texture-id per invocation; --object-skin-texture-id "
                    "still wins if both are given");
    app.add_option("--listfile", opts.listfileArg,
                    "a local community-listfile.csv-style snapshot (FileDataID;path per line, "
                    "github.com/wowdev/wow-listfile) -- last-resort fallback when a "
                    "FileDataID-named texture slot isn't found as '<FileDataID>.{blp,png}' next "
                    "to --textures: looks up its real name and tries '<listfile-root>/<real-path>' "
                    "instead, before falling back to the same-basename fuzzy pool. Optional; unset "
                    "(default) skips this tier entirely, same as every other opt-in sidecar -- "
                    "never fetched by husk itself, same tier as --dbd-dir's WoWDBDefs checkout");
    app.add_option("--listfile-root", opts.listfileRootArg,
                    "the corpus root --listfile's paths are relative to -- deliberately separate "
                    "from --textures, since --textures also drives the *directory-local* "
                    "embedded-filename/same-basename matching tried first, and a real corpus root "
                    "is typically many directories away from any one model. Only meaningful "
                    "alongside --listfile; ignored otherwise. Default: --textures itself, for the "
                    "case where a single directory happens to serve both roles");
}

// The single-model export pipeline, unchanged in substance from the old
// monolithic exportGlb -- factored out so --from-list (below) can call it
// once per entry, sharing one already-parsed `opts`/`app` and one
// already-loaded `listfile` across the whole batch instead of re-parsing
// argv or re-reading a multi-million-line CSV per file. `modelPath`/
// `outputPath`/`outputGiven` are passed explicitly rather than read off
// `opts` directly, since those three are the only fields that vary per
// batch entry -- everything else (--textures, --db2-dir, ...) applies
// identically to every file in the batch, same as a real casc-tool
// `extract-batch` run shares its own storage/listfile handle across every
// extracted entry.
// `listfile` is taken by value, not by reference: --knowledge-db (below)
// adds this one model's own resolved object-skin texture into it, and
// that addition must not leak into the next model's own export when this
// runs inside a --from-list batch sharing one loaded listfile.
int exportOneModel(const ExportOptions& opts, CLI::App& app, const std::string& modelPath,
                    const std::string& outputPathIn, bool outputGiven,
                    std::unordered_map<uint32_t, std::string> listfile) {
    std::string outputPath = outputPathIn;
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

    if (!outputGiven) {
        outputPath = std::filesystem::path(modelPath).replace_extension(".glb").string();
        std::cerr << "husk: note: no output path given -- writing to '" << outputPath << "'\n";
    } else if (std::filesystem::is_directory(outputPath)) {
        // -o pointed at an existing directory: infer <dir>/<model-basename>.glb
        // -- the model's own filename is the obvious guess for what the user
        // wants the output named, the same way `cp foo.txt somedir/` infers
        // the destination filename from the source, rather than failing with
        // "Is a directory".
        std::string basename = std::filesystem::path(modelPath).stem().string() + ".glb";
        outputPath = (std::filesystem::path(outputPath) / basename).string();
        std::cerr << "husk: note: -o pointed at a directory -- writing to '" << outputPath << "'\n";
    }

    // -o is where the user *wants* the file to end up, not an assertion that
    // the directory structure already exists -- create any missing parent
    // directories up front (same TODO finding as above), and do it here,
    // before the real parse/export pipeline below, so a bad -o path fails
    // fast instead of costing a multi-second re-parse to discover.
    {
        auto parent = std::filesystem::path(outputPath).parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "husk: couldn't create output directory '" << parent.string()
                          << "': " << ec.message() << "\n";
                return 1;
            }
        }
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

    // `listfile` itself is a parameter now, not loaded here -- see this
    // function's own doc comment: exportGlb loads it once, before either
    // the single-file call or the --from-list loop, never per model.
    // --listfile-root is deliberately its own directory, not texturesDir --
    // see that flag's own help text for why reusing texturesDir would
    // silently break the directory-local matching tried before it.
    std::string listfileRoot = app.count("--listfile-root") ? opts.listfileRootArg : texturesDir;

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
    std::string customizationChoiceIdsArg =
        app.count("--customization-choice-ids") ? opts.customizationChoiceIdsArg : "";
    std::string chrModelIdArg = app.count("--chr-model-id") ? opts.chrModelIdArg : "";
    std::string creatureDisplayIdArg =
        app.count("--creature-display-id") ? opts.creatureDisplayIdArg : "";
    uint32_t objectSkinTextureFileDataId = 0;
    if (app.count("--object-skin-texture-id")) {
        try {
            objectSkinTextureFileDataId = static_cast<uint32_t>(std::stoul(opts.objectSkinTextureIdArg));
        } catch (const std::exception&) {
            std::cerr << "husk: note: --object-skin-texture-id '" << opts.objectSkinTextureIdArg
                      << "' isn't a valid integer -- ignoring\n";
        }
    } else if (app.count("--knowledge-db")) {
        KbObjectSkinResolution kbResolution =
            resolveObjectSkinTextureFromKb(opts.knowledgeDbArg, modelPath, listfileRoot);
        objectSkinTextureFileDataId = kbResolution.textureFileDataId;
        // Fills the embed path's --listfile fallback tier (export_materials.cpp)
        // straight from the knowledge base's own 'textures' table, without
        // requiring a separate --listfile load just for this one texture --
        // only when --listfile didn't already resolve this fdid itself.
        if (objectSkinTextureFileDataId != 0 && !kbResolution.texturePath.empty()) {
            listfile.emplace(objectSkinTextureFileDataId, kbResolution.texturePath);
        }
    }

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

        // AnimationData.db2's real Name column ("Stand", "Walk", ...) --
        // opportunistic, same "--db2-dir/--dbd-dir unlocks this, nothing
        // else required" convention as the other DB2-driven enrichments
        // below, but independent of any --chr-model-id/--customization-*
        // flag: every animated model (creature or player) has real
        // AnimationData.db2 rows, not just characters.
        std::unordered_map<uint32_t, std::string> animationNames;
        if (!db2Dir.empty() && !dbdDirForChr.empty()) {
            if (auto animData = animationdata::load(db2Dir, dbdDirForChr, std::cerr)) {
                animationNames = animationdata::toNameMap(*animData);
            }
        }

        gltf::Skeleton skeleton;
        std::vector<gltf::Animation> animations;
        if (!bones.empty()) {
            skeleton = buildSkeleton(bones);
            baseMesh.skinning = buildSkinning(vertices, bones.size());
            animations = resolveAnimationsForModel(animNone, bonesAreInline, haveSkel, header, blob,
                                                    skelBytes, bones, skeleton, animDir, modelPath,
                                                    animationNames.empty() ? nullptr : &animationNames);
            attachBoneCorrections(bonesDir, bonesAreInline, haveSkel, header, skelBytes, skeleton);
            attachEmitterAnchors(blob, header, skeleton);
            attachPlacementNodes(blob, header, header.sequences.count, skeleton);
            attachPhysicsBodies(physNone, physGiven, physPath, modelPath, skeleton);
            attachCharTextureLayout(db2Dir, dbdDirForChr, charLayoutIdArg, chrModelIdArg, modelPath,
                                     listfile, listfileRoot, skeleton);
            // Must run after attachBoneCorrections just above -- it only
            // marks/extends already-resolved correction sets, never
            // attaches new '.bone' data of its own.
            attachCustomizationChoices(db2Dir, dbdDirForChr, customizationChoiceIdsArg, chrModelIdArg, modelPath,
                                        listfile, listfileRoot, skeleton);
            attachCreatureGeosets(db2Dir, dbdDirForChr, creatureDisplayIdArg, skeleton);
            // Needs skeleton.attachments/events already populated (just
            // above), so it can't run inside buildSkeleton itself.
            applyContextualBoneNames(skeleton);
        }

        std::string modelBasename = std::filesystem::path(modelPath).stem().string();
        auto namedMeshes =
            buildLodTierMeshes(skinsToExport, vertices, baseMesh, m2Inputs, texturesDir, modelPath,
                                modelBasename, texturesOutDir, listfile, listfileRoot,
                                objectSkinTextureFileDataId);

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

// Reads --from-list's plain-text worklist: one .m2 path per line, blank
// lines and '#'-prefixed comments skipped -- same convention casc-tool's
// own --from-list uses for its FileDataID worklists. A missing/unreadable
// --from-list is a direct user mistake (bad path), thrown loudly, same
// "bad flag value fails fast, a malformed *line* doesn't" split
// loadListfile already draws for --listfile.
std::vector<std::string> readModelListFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("couldn't open --from-list '" + path + "'");
    }
    std::vector<std::string> result;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF-saved copies
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        if (line[start] == '#') continue;
        size_t end = line.find_last_not_of(" \t");
        result.push_back(line.substr(start, end - start + 1));
    }
    return result;
}

// Picks each batch entry's output filename: '<model-basename>.glb', or,
// when that collides with an earlier entry in the same batch (real corpora
// commonly reuse basenames across expansion/race subdirectories),
// '<parent-dir-name>_<model-basename>.glb', falling back to a numeric
// suffix if even that still collides. `usedNames` accumulates across the
// whole batch so collisions are detected regardless of how far apart the
// colliding entries are in the list.
std::string pickBatchOutputName(const std::string& modelPath, size_t indexInBatch,
                                 std::unordered_map<std::string, int>& usedNames) {
    std::string stem = std::filesystem::path(modelPath).stem().string();
    std::string name = stem + ".glb";
    if (usedNames.find(name) == usedNames.end()) {
        usedNames.emplace(name, 1);
        return name;
    }
    std::string parentName = std::filesystem::path(modelPath).parent_path().filename().string();
    std::string disambiguated = parentName.empty() ? (stem + "_" + std::to_string(indexInBatch) + ".glb")
                                                     : (parentName + "_" + stem + ".glb");
    if (usedNames.find(disambiguated) == usedNames.end()) {
        usedNames.emplace(disambiguated, 1);
        return disambiguated;
    }
    disambiguated = stem + "_" + std::to_string(indexInBatch) + ".glb";
    usedNames.emplace(disambiguated, 1);
    return disambiguated;
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

    bool batchMode = app.count("--from-list") > 0;

    // -i/--input is required in single-file mode, meaningless (and never
    // required) in --from-list mode -- see addExportOptions' comment for
    // why this can't just be CLI11's own ->required() on the option.
    if (!batchMode && opts.modelPath.empty()) {
        std::cerr << "husk: the following arguments are required: --input,input\n";
        return 1;
    }
    if (batchMode && !opts.modelPath.empty()) {
        std::cerr << "husk: --input/positional input and --from-list are mutually exclusive -- "
                     "--from-list supplies every model path itself\n";
        return 1;
    }
    if (batchMode && app.count("--output") > 0) {
        std::cerr << "husk: --output and --from-list are mutually exclusive -- pass --output-dir "
                     "instead, --from-list always names its own per-file outputs\n";
        return 1;
    }
    if (batchMode && app.count("--output-dir") == 0) {
        std::cerr << "husk: --from-list requires --output-dir\n";
        return 1;
    }
    if (!batchMode && app.count("--output-dir") > 0) {
        std::cerr << "husk: --output-dir only does anything alongside --from-list\n";
        return 1;
    }

    // --listfile: optional, unset by default (empty map, same "skip this
    // tier entirely" behavior as before this flag existed). Loaded once up
    // front, not per-batch-entry -- a real community-listfile.csv is
    // millions of lines, and --from-list can drive thousands of exports
    // from one invocation.
    std::unordered_map<uint32_t, std::string> listfile;
    if (app.count("--listfile")) {
        listfile = husk::loadListfile(opts.listfileArg);
        // A bad --listfile *path* already throws (loadListfile itself, a
        // direct user mistake worth failing loudly on) -- but a listfile
        // that opens fine and parses to zero usable entries (genuinely
        // empty, or every line malformed -- e.g. the wrong file entirely,
        // or corrupted mid-download) previously degraded *silently*: every
        // consumer already treats an empty map the same as "no --listfile
        // given" and falls back to local-only resolution correctly, but
        // that fallback was invisible, which is the wrong failure mode for
        // something the caller explicitly asked for. Recoverable (every
        // downstream feature still has a real local fallback tier), so
        // this warns rather than aborting the export -- never silent.
        if (listfile.empty()) {
            std::cerr << "husk: warning: --listfile '" << opts.listfileArg
                      << "' loaded but contained no usable entries (empty file, or every line "
                         "failed to parse) -- every listfile-dependent feature will fall back to "
                         "local-only resolution for this run, same as if --listfile had never "
                         "been given\n";
        }
    }

    if (!batchMode) {
        return exportOneModel(opts, app, opts.modelPath, opts.outputPath, app.count("--output") > 0, listfile);
    }

    std::vector<std::string> modelPaths;
    try {
        modelPaths = readModelListFile(opts.fromListArg);
    } catch (const std::exception& e) {
        std::cerr << "husk: " << e.what() << "\n";
        return 1;
    }
    if (modelPaths.empty()) {
        std::cerr << "husk: note: --from-list '" << opts.fromListArg << "' has no entries -- nothing to do\n";
        return 0;
    }

    std::error_code ec;
    std::filesystem::create_directories(opts.outputDirArg, ec);
    if (ec) {
        std::cerr << "husk: couldn't create --output-dir '" << opts.outputDirArg << "': " << ec.message() << "\n";
        return 1;
    }

    std::unordered_map<std::string, int> usedNames;
    size_t succeeded = 0;
    size_t failed = 0;
    for (size_t i = 0; i < modelPaths.size(); ++i) {
        const std::string& modelPath = modelPaths[i];
        std::string outName = pickBatchOutputName(modelPath, i, usedNames);
        std::string outputPath = (std::filesystem::path(opts.outputDirArg) / outName).string();

        std::cerr << "husk: [" << (i + 1) << "/" << modelPaths.size() << "] " << modelPath << " -> "
                  << outputPath << "\n";
        // Deliberately not fatal: a single bad file in a multi-thousand-entry
        // worklist shouldn't abort hours of unrelated corpus work -- each
        // entry's own real error is already printed by exportOneModel itself
        // (or, for anything thrown before it gets that far, caught here).
        int rc = 1;
        try {
            rc = exportOneModel(opts, app, modelPath, outputPath, /*outputGiven=*/true, listfile);
        } catch (const std::exception& e) {
            std::cerr << "husk: export failed: " << e.what() << "\n";
        }
        if (rc == 0) {
            ++succeeded;
        } else {
            ++failed;
        }
    }

    std::cerr << "husk: batch export done: " << succeeded << " succeeded, " << failed << " failed (of "
              << modelPaths.size() << ")\n";
    return failed > 0 ? 1 : 0;
}

}  // namespace husk::commands
