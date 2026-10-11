#pragma once

#include <cstdint>
#include <vector>

#include "canon_scene.hpp"
#include "m2_canon_input.hpp"  // ExternalAnimBlobs
#include "m2_material_input.hpp"  // TextureResolutions
#include "m2_model.hpp"
#include "skel.hpp"

// husk::m2input: M2 -> canon::Scene (attachments, events, lights) and the
// ribbon and particle emitters as effect instances. The legacy glTF path
// reduces these to placement anchors
// (export_extras.cpp); this keeps every parsed field and resolves every track.
namespace husk::m2input {

// Particle texture slots, in layer order: the low 15 bits of
// M2Particle::textureId as three 5-bit texture indices when the emitter is
// MultiTexture (flags 0x10000000, wowdev.wiki M2#Particle_Flags), otherwise
// textureId itself. Shared by the texture-resolution pass and assembleEffects,
// which must agree on which texture indices an emitter uses.
std::vector<uint16_t> particleTextureIndices(const m2::ParticleEmitter& emitter);

// `sequenceCount` is the sequence array in effect for the model (the .skel's
// when one supplies the skeleton). `externalAnimBlobs` must be keyed against
// that same array and hold blobs whose offsets match `model.blob` -- pass an
// empty map for a .skel-sourced model, whose external blobs are AFSB
// skeleton data, not M2 data. `skelAttachments`, when non-null, replaces
// `model.attachments`; its tracks resolve inline only (a .skel's per-sequence
// attachment data lives in AFSA chunks, which husk does not read).
//
// Throws std::runtime_error if any of its three source arrays failed to parse
// (strict partial-failure policy, same as husk export), and whatever the m2
// track resolvers throw on malformed tracks.
canon::Scene assembleScene(const m2::Model& model, uint32_t sequenceCount, const ExternalAnimBlobs& externalAnimBlobs,
                           const skel::Attachments* skelAttachments);

// The model's particle and ribbon emitters as effect instances
// (REFACTOR/EFFECTS.md, literal tier), each anchored to its bone: the set
// the model owns as "effects", particles then ribbons, each id its position.
// Emitter textures come from `textureResolutions`, unresolved when absent.
// Same sequence inputs and throws as assembleScene.
canon::PlacementSet assembleEffects(const m2::Model& model, uint32_t sequenceCount,
                                    const ExternalAnimBlobs& externalAnimBlobs,
                                    const TextureResolutions& textureResolutions);

}  // namespace husk::m2input
