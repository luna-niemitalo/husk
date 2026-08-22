#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "gltf_math.hpp"
#include "gltf_mesh.hpp"
#include "gltf_skeleton.hpp"

// Minimal glTF 2.0 binary (.glb) export, via tinygltf (see nix/flake.nix) --
// husk builds the mesh data and hands it to tinygltf rather than
// hand-rolling glTF's JSON/binary-chunk framing itself.
//
// This header is the index (FILE_SPLIT_TODO.md Item 3): the actual data
// model lives in gltf_math.hpp (Vec2/Vec3/Quat, Z-up->Y-up conversion),
// gltf_mesh.hpp (JointWeights/Material/Primitive/Mesh/NamedMesh), and
// gltf_skeleton.hpp (Skeleton/JointAnimation/Animation) -- included above so
// `#include "gltf.hpp"` alone still exposes every husk::gltf type, no
// caller-visible change. Only Error and the document-assembler entry points
// (writeGlb/writeGlbMulti) stay here.
namespace husk::gltf {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Serializes `mesh` as a minimal glTF binary (.glb): one buffer holding
// positions/normals/texCoords/indices, one mesh with one TRIANGLES
// primitive per `mesh.primitives` entry, one node, one scene. Throws Error
// if positions/normals/texCoords aren't all the same length, `primitives`
// is empty, any primitive's indices is empty or not a multiple of 3 or out
// of range for positions, or any primitive's materialIndex is out of range
// for `materials`. `mesh.texCoords2`, if non-empty, must be the same
// length as `mesh.positions` (adds a shared TEXCOORD_1 accessor to every
// primitive) -- Error if it's some other length.
//
// `materials` becomes the glTF document's materials array 1:1 (see
// Material's doc comment) -- pass an empty vector for the roadmap-stage-1-
// through-4 behavior (no material, no image; every primitive must then
// leave materialIndex at -1).
//
// If `skeleton` is non-null (and non-empty), it adds a joint node per
// skeleton entry (parented per `Joint::parent`) and a glTF skin with
// inverse bind matrices regardless of `mesh.skinning`. Whether `mesh`
// itself is deformed by that skeleton is decided by `mesh.skinning`:
// non-empty (must be the same length as `mesh.positions`) attaches the skin
// to `mesh`'s node and adds JOINTS_0/WEIGHTS_0 accessors; empty leaves
// `mesh`'s node unskinned even though the skeleton/skin still exist in the
// document (see writeGlbMulti's doc comment for why -- an unskinned
// NamedMesh entry alongside a skinned one is the concrete use case). If
// `skeleton` is null, `mesh.skinning` must be empty -- no orphaned skinning
// data. Throws Error on any joint's `parent` being out of range or
// self-referential, or on the skeleton/skinning-data mismatches above.
// `skeleton->correctionSets`, if non-empty, becomes a
// `bone_correction_sets` key in the skin's own glTF `extras` (see
// Skeleton::CorrectionSet's doc comment) -- Error if any correction's
// `joint` is out of range for `skeleton`. `skeleton->ribbonAnchors`/
// `particleAnchors`, if non-empty, likewise become `ribbon_emitters`/
// `particle_emitters` keys on the same skin `extras` object (see
// Skeleton::EmitterAnchor's doc comment) -- Error under the same
// out-of-range-joint condition. `skeleton->physicsBodies`, if non-empty,
// becomes a `physics_bodies` key on the same skin `extras` object (see
// Skeleton::PhysicsBody's doc comment) -- Error under the same
// out-of-range-joint condition. `skeleton->attachments`/`events`/`lights`,
// if non-empty, instead become real child glTF nodes, one per entry (see
// Skeleton::Attachment/Event/Light's doc comments and writeGlbMulti's doc
// comment for the exact node shape) -- Error under the same
// out-of-range-joint condition.
//
// `animations`, if non-empty, requires a non-null/non-empty `skeleton` --
// each becomes one glTF animation clip (see Animation's doc comment).
// Throws Error if any JointAnimation::joint is out of range for `skeleton`,
// or any of its three time/value pairs have mismatched lengths.
// `animations[i].sequenceMetadata`, if set, becomes a `sequence_metadata`
// key in that clip's own glTF `extras` (see Animation::SequenceMetadata's
// doc comment).
std::vector<uint8_t> writeGlb(const Mesh& mesh, const std::vector<Material>& materials = {},
                               const Skeleton* skeleton = nullptr,
                               const std::vector<Animation>& animations = {},
                               const std::string& slimTexturesOutputDir = "");

// Serializes multiple meshes into one .glb, each as its own named node (and
// its own glTF mesh/primitives/materials) -- husk export --lod all's case
// (see README.md): one node per LOD tier of the same M2, so a DCC tool's
// outliner shows them as separate, individually-toggleable objects instead
// of silently overwriting one another. `meshes` may only be empty when
// `skeleton` is non-null and has at least one joint -- a genuinely
// geometry-less M2 (a pure particle/ribbon VFX model, not just an empty
// .skin) still exports its skeleton and ribbon/particle emitter anchors/
// physics body anchors (Skeleton::ribbonAnchors/particleAnchors/
// physicsBodies), just with no mesh node at all
// -- glTF has no valid "mesh with zero primitives" representation to fall
// back to instead (see cmd_export.cpp's per-LOD-tier skip). Otherwise
// (`meshes` empty and no skeleton), Error -- there'd be nothing to put in
// the document. Every non-empty entry is validated exactly like writeGlb's
// single `mesh`/`materials` (same Error conditions, "writeGlbMulti" in
// place of "writeGlb" in the message).
// TODO: Remove: 3,807 real corpus files have zero vertices at the M2 level
// (the real-world scale of the geometry-less case above).
//
// `skeleton->geosetTags`, if non-empty, adds one inert placeholder joint
// per entry (Skeleton::GeosetTag's doc comment for the full mechanism and
// why) -- appended to the joint-node range right
// after every real bone (indices 0..skeleton->joints.size()-1 above are
// never touched or renumbered), each an identity-bind-pose node named
// `group_<geosetId/100>,variant_<geosetId%100>` (comma-separated,
// prefix-tagged fields rather than one combined token -- so a Blender-
// side script can recover the raw integers with a plain comma-split +
// prefix-strip, no division/modulo needed at the consuming end),
// parented under the single real root joint
// (single-root skeletons) or the synthesized multi-root parent node
// described below (multi-root skeletons) so the skin's "closest common
// root" property still holds. Unlike every anchor list further below,
// these *are* added to `skin.joints` (with their own identity inverse
// bind matrix) -- the entire point is for Blender's stock glTF importer to
// create one real vertex group per tag as an ordinary side effect of
// skin-weight import. A primitive whose `skinSectionId` matches a tag's
// `geosetId` gets that tag woven into its vertices' `JOINTS_1`/`WEIGHTS_1`
// (gltf_mesh.cpp's emitMeshNode) -- `JOINTS_0`/`WEIGHTS_0` (real bone
// skinning) is untouched. Empty `geosetTags`: no tag nodes, no
// `JOINTS_1`/`WEIGHTS_1` attributes at all, output identical to before
// this existed.
//
// If `skeleton` has more than one root joint (`Joint::parent == -1` on more
// than one entry -- a real, common M2 bone-forest shape, not corruption,
// see Skeleton's own doc comment above), one
// additional plain (non-joint) glTF node is synthesized as the parent of
// every real root joint (and every geoset tag node above, if any), appended
// past the end of the joint-node and geoset-tag-node ranges (index
// `meshCount + skeleton->joints.size() + skeleton->geosetTags.size()`)
// with a default/identity transform. It becomes the sole scene-root entry
// standing in for those joints (each real root is still reached via this
// node's own `.children`, not listed individually) and `skin.skeleton` is
// set to it -- the shape glTF's own tooling ecosystem already anticipates
// for multi-rooted skeletons. It is never added to `skin.joints` and never
// gets an inverse bind matrix -- every vertex/emitter-anchor/correction/
// animation joint index still refers to a real M2 bone by its original,
// unshifted position (the one invariant this must never break -- see
// Skeleton's own doc comment above). A single-root skeleton (the
// overwhelming majority of real models) is unaffected: no synthetic node,
// `skin.skeleton` left unset, output identical to before this existed.
// TODO: Remove: github.com/KhronosGroup/glTF/issues/1270 (external tracker
// citation for the multi-root shape glTF's tooling already anticipates).
//
// `skeleton->attachments`/`events`/`lights` each become one real, plain
// child glTF node -- named `attachment_<id>`/`event_<identifier>`/
// `light_<index>` (M2Attachment::id, M2Event::identifier -- not
// deduplicated, a real M2 file can repeat one, e.g. multiple "$CSD" sound
// events -- and the entry's own position in `Skeleton::lights`,
// respectively), translation-only (no rotation/scale, same convention as a
// joint node's own `.translation`) at `Attachment/Event/Light::position`,
// and parented as a `.children` entry of their owning joint node -- never
// added to `skin.joints` or given an inverse bind matrix, same invariant as
// the synthesized multi-root parent node above (geoset tag nodes above are
// the one deliberate exception to this convention, not these). Appended
// past the end of the joint-node range, past the geoset-tag-node range,
// and past the synthesized multi-root node (if any), attachments first,
// then events, then lights. Error under the same out-of-range-joint
// condition as every other anchor list above.
//
// `skeleton`/`animations` are shared across every entry, not per-mesh --
// valid because every LOD of one M2 draws from the same `bones` array (only
// the triangle/vertex-index *subset* a .skin selects differs per LOD, see
// src/skin.hpp), so one bind-pose skeleton and one set of animation clips
// cover all of them. If `skeleton` is given, each entry independently
// chooses skinned (mesh.skinning non-empty, must match that entry's own
// mesh.positions length) or unskinned (mesh.skinning left empty -- the
// node gets no glTF `skin` reference at all, and isn't deformed by the
// armature) -- e.g. a render mesh alongside an unskinned collision-mesh
// entry, parented directly to the scene root like any other node. There's
// exactly one glTF skin object, shared by every *skinned* mesh node.
//
// writeGlb(mesh, materials, skeleton, animations) is exactly
// writeGlbMulti({{"", mesh, materials}}, skeleton, animations) -- the
// single-mesh case is this function with one, unnamed, entry.
//
// `slimTexturesOutputDir` (--slim-textures, TODO/
// SLIM_GLB_EXTERNAL_TEXTURES_TODO.md) is the directory the resulting .glb
// itself will be written into. Left empty (the default), every resolved
// baseColorImagePng is embedded in the .glb's own binary buffer as before
// (img.bufferView). Non-empty instead writes each one as a real
// '<slimTexturesOutputDir>/textures/<name>.png' file (named by real
// FileDataID when known, else the resolved source filename -- see
// gltf_mesh.hpp's Material::baseColorTextureFileDataId/baseColorImageName)
// and sets `img.uri` to the relative path 'textures/<name>.png' instead of
// `img.bufferView` -- a real external glTF image reference, resolved by
// Blender's own importer relative to the .glb/.gltf file's directory.
// Materials sharing the same resolved image write the file once and share
// the same URI (the existing alternateTextureCache dedup, gltf_mesh.cpp).
// AdditionalTextureLayer/AlternateTextureCandidate images (extras-only,
// diagnostic/rare) always stay embedded regardless of this parameter.
std::vector<uint8_t> writeGlbMulti(const std::vector<NamedMesh>& meshes,
                                    const Skeleton* skeleton = nullptr,
                                    const std::vector<Animation>& animations = {},
                                    const std::string& slimTexturesOutputDir = "");

}  // namespace husk::gltf
