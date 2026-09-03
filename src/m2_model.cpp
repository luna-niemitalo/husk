#include "m2_model.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>

// See m2_model.hpp for the design (why eager parsing needs per-field
// failure isolation, and why particleEmitters alone is version-gated
// before being attempted at all).
namespace husk::m2 {

namespace {

// Runs `fn` (a no-arg callable that assigns into one Model field), and on
// ParseError records the failure instead of letting it propagate. `fn`
// only assigns after its own parse* call has fully returned, so a mid-parse
// throw never leaves the target field partially written -- it stays
// whatever it was before (default-constructed empty), the same value a
// field this function was never called for also has.
template <typename Fn>
void tryParse(std::vector<FieldParseFailure>& failures, const char* field, Fn&& fn) {
    try {
        fn();
    } catch (const ParseError& e) {
        failures.push_back({field, e.what()});
    }
}

}  // namespace

Model loadModel(const std::vector<uint8_t>& fileBytes) {
    Model model;
    // No Model without a header/blob -- propagates, matching every
    // existing caller's own behavior for a file this broken.
    model.header = parseHeader(fileBytes);
    model.blob = extractBlob(fileBytes);

    const Header& h = model.header;
    const std::vector<uint8_t>& blob = model.blob;
    auto& failures = model.parseFailures;

    tryParse(failures, "global_loops",
             [&] { model.globalLoops = parseGlobalLoops(blob, h.globalLoops); });
    tryParse(failures, "sequences", [&] { model.sequences = parseSequences(blob, h.sequences); });
    tryParse(failures, "sequence_lookup",
             [&] { model.sequenceLookup = parseUint16Array(blob, h.sequenceLookup); });
    tryParse(failures, "bones", [&] { model.bones = parseBones(blob, h.bones); });
    tryParse(failures, "bone_lookup",
             [&] { model.boneLookup = parseUint16Array(blob, h.boneLookup); });
    tryParse(failures, "bone_combos",
             [&] { model.boneCombos = parseUint16Array(blob, h.boneCombos); });
    tryParse(failures, "vertices", [&] { model.vertices = parseVertices(blob, h.vertices); });
    tryParse(failures, "colors", [&] { model.colors = parseColors(blob, h.colors); });
    tryParse(failures, "textures", [&] { model.textures = parseTextures(blob, h.textures); });
    tryParse(failures, "texture_weights",
             [&] { model.textureWeights = parseTextureWeights(blob, h.textureWeights); });
    tryParse(failures, "texture_transforms",
             [&] { model.textureTransforms = parseTextureTransforms(blob, h.textureTransforms); });
    tryParse(failures, "texture_lookup",
             [&] { model.textureLookup = parseUint16Array(blob, h.textureLookup); });
    tryParse(failures, "materials", [&] { model.materials = parseMaterials(blob, h.materials); });
    tryParse(failures, "texture_combos",
             [&] { model.textureCombos = parseUint16Array(blob, h.textureCombos); });
    tryParse(failures, "texture_coord_combos",
             [&] { model.textureCoordCombos = parseUint16Array(blob, h.textureCoordCombos); });
    tryParse(failures, "texture_weight_combos",
             [&] { model.textureWeightCombos = parseUint16Array(blob, h.textureWeightCombos); });
    tryParse(failures, "texture_transform_combos", [&] {
        model.textureTransformCombos = parseUint16Array(blob, h.textureTransformCombos);
    });
    tryParse(failures, "collision_mesh", [&] {
        model.collisionMesh =
            parseCollisionMesh(blob, h.collisionPositions, h.collisionIndices, h.collisionFaceNormals);
    });
    tryParse(failures, "attachments",
             [&] { model.attachments = parseAttachments(blob, h.attachments); });
    tryParse(failures, "attachment_lookup",
             [&] { model.attachmentLookup = parseUint16Array(blob, h.attachmentLookup); });
    tryParse(failures, "events", [&] { model.events = parseEvents(blob, h.events); });
    tryParse(failures, "lights", [&] { model.lights = parseLights(blob, h.lights); });
    tryParse(failures, "camera_lookup",
             [&] { model.cameraLookup = parseUint16Array(blob, h.cameraLookup); });
    tryParse(failures, "ribbon_emitters",
             [&] { model.ribbonEmitters = parseRibbons(blob, h.ribbonEmitters); });

    // Version-gated before being attempted at all, not just wrapped in
    // try/catch -- see loadModel's own doc comment (m2_model.hpp) for why.
    if (h.version >= kMinVerifiedParticleVersion) {
        tryParse(failures, "particle_emitters",
                 [&] { model.particleEmitters = parseParticles(blob, h.particleEmitters); });
    }

    tryParse(failures, "texture_combiner_combos",
             [&] { model.textureCombinerCombos = parseUint16Array(blob, h.textureCombinerCombos); });

    return model;
}

Model loadModelFile(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw ParseError("error reading '" + path + "': " + std::strerror(errno));
    }
    return loadModel(bytes);
}

}  // namespace husk::m2
