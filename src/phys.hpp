#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// .phys file parsing -- physics/collision simulation input (rigid bodies,
// collision shapes, joints) for Blizzard's "Domino" physics engine
// (MoP+). Unlike .bone, this format IS documented:
// documentation/wowdev-wiki/md/PHYS.md (wiki_revision 30458) gives byte
// offsets for nearly every field; this parser implements that spec.
// TODO: Remove: verified against 103 real files, see WIKI_FINDINGS.md §9.
//
// Layout: WoW's usual chunked container (husk::readChunks/findChunk, see
// chunk.hpp), but with WMO/ADT-style *reversed* chunk tags on disk (e.g.
// "PHYS" is stored as the literal bytes 'S','Y','H','P') -- the opposite of
// M2's own inline chunks. phys.cpp's chunk-tag constants are the already-
// reversed on-disk literals, one per wowdev.wiki name.
//
// Chunk-variant selection is by chunk *tag*, not the top-level PHYS.version
// field -- each variant has its own byte stride, and PHYS.md's own text
// ("chunk identifiers changed at v2*") means the tag itself, not the
// version number, tells you which stride applies. Body prefers BDY4 over
// BDY3 over BDY2 over BODY; Shape prefers SHP2 over SHAP; weld/shoulder/
// prismatic/revolute joints likewise prefer their newest variant. A bare
// SHOJ tag is ambiguous between two strides (0x6c pre-7.0.1.20979, 0x74
// post) -- disambiguated by which stride the chunk's own size divides
// evenly; a real file hitting both throws rather than guessing.
//
// TODO: Remove: coverage verified against real files (WIKI_FINDINGS.md
// §9): PHYS/PHYT, BODY/BDY3/BDY4, SHAP/SHP2, CAPS, PLYT, SPHS, JOIN,
// WELJ/WLJ2, SHOJ (0x74), REVJ, PHYV confirmed against 103 real files;
// BDY2, BOXS, WLJ3, SHOJ (0x6c)/SHJ2, REV2, SPHJ, PRSJ/PRS2, DSTJ
// transcribed from the wiki only, never observed in that sample -- husk
// still parses these (offsets are real, just unverified). No durable-doc
// home found for this table (checked DESIGN.md#Boundaries and
// M2_COMPLETENESS.md); revisit if one is added.
namespace husk::phys {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// One rigid body, connected to one bone. `type`/`unk1` together classify
// kinematic-vs-dynamic; "type == 0" does NOT mean "the root" -- most real
// files have several type-0 bodies. `shapeBase`/`shapeCount` index into
// File::shapes (BODY/BDY2's `shapes_base`, BDY3/BDY4's `shapeIndex` --
// same role, different field width/name upstream).
// TODO: Remove: WIKI_FINDINGS.md §9.
struct Body {
    uint16_t type = 0;
    uint16_t boneIndex = 0;
    Vec3 position;
    int32_t shapeBase = 0;
    int32_t shapeCount = 0;
    // BDY3+ only -- left at these wiki-documented defaults for BODY/BDY2
    // (PHYS.md: "fills up with default values if loading older versions").
    float unk0 = 0.0f;
    float x1c = 1.0f;
    float drag = 0.0f;
    float unk1 = 0.0f;
    float x28 = 0.9f;
};

// One collision shape, indexing into the type-specific array below by
// `type`/`index`.
struct Shape {
    int16_t type = 0;  // 0=box, 1=capsule, 2=sphere, 3=polytope (v3+)
    int16_t index = 0;
    float friction = 0, restitution = 0, density = 0;
};

struct BoxShape {  // unverified -- see this file's own doc comment
    std::array<float, 12> a{};
    Vec3 c;
};

struct CapsuleShape {
    Vec3 localPosition1, localPosition2;
    float radius = 0;
};

struct SphereShape {  // unverified -- see this file's own doc comment
    Vec3 localPosition;
    float radius = 0;
};

// PLYT's own self-describing variable-length format (PHYS.md: "This chunk
// does its own array handling ... instead of splitting in a header and a
// data chunk, it is combining both parts"). `unk1`/`unk2`/`nodes`'
// semantics beyond "a tree structure that connects the vertices together"
// aren't resolved -- surfaced raw, not modeled further.
// TODO: Remove: WIKI_FINDINGS.md §9.
struct PolytopeShape {
    uint32_t vertexCount = 0;
    uint32_t count10 = 0;
    uint32_t nodeCount = 0;
    std::vector<Vec3> vertices;
    std::vector<std::array<uint8_t, 16>> unk1;
    std::vector<uint8_t> unk2;
    std::vector<std::array<int8_t, 4>> nodes;
};

// One joint, connecting two bodies. `type`/`index` select the type-specific
// array below.
struct Joint {
    uint32_t bodyA = 0, bodyB = 0;
    int16_t type = 0;  // 0=spherical, 1=shoulder, 2=weld, 3=revolute, 4=prismatic, 5=distance
    int16_t index = 0;
};

struct WeldJoint {
    std::array<float, 12> frameA{}, frameB{};  // mat3x4, row-major
    float angularFrequencyHz = 0, angularDampingRatio = 0;
    float linearFrequencyHz = 0, linearDampingRatio = 0;  // WLJ2+
    float unk70 = 0;                                      // WLJ3 only -- unverified
};

struct SphericalJoint {  // unverified -- see this file's own doc comment
    Vec3 anchorA, anchorB;
    float frictionTorque = 0;
};

struct ShoulderJoint {
    std::array<float, 12> frameA{}, frameB{};
    float lowerTwistAngle = 0, upperTwistAngle = 0, coneAngle = 0;
    float maxMotorTorque = 0;   // SHOJ (0x74)+
    uint32_t motorMode = 0;     // SHOJ (0x74)+
    float motorFrequencyHz = 0, motorDampingRatio = 0;  // SHJ2 only -- unverified
};

struct PrismaticJoint {  // unverified -- see this file's own doc comment
    std::array<float, 12> frameA{}, frameB{};
    float lowerLimit = 0, upperLimit = 0, x68 = 0, maxMotorForce = 0, x70 = 0;
    uint32_t motorMode = 0;
    float motorFrequencyHz = 0, motorDampingRatio = 0;  // PRS2 only
};

struct RevoluteJoint {
    std::array<float, 12> frameA{}, frameB{};
    float lowerAngle = 0, upperAngle = 0, maxMotorTorque = 0;
    uint32_t motorMode = 0;
    float motorFrequencyHz = 0, motorDampingRatio = 0;  // REV2 only -- unverified
};

struct DistanceJoint {  // unverified -- see this file's own doc comment
    Vec3 localAnchorA, localAnchorB;
    float someDistanceFactor = 0;
};

// PHYS.md: "When this chunk is present there seem to be no other following
// chunks" -- a single-collider file (real fixture:
// world/expansion06/doodads/valsharah/7vs_detail_nightmareplant01_phys.phys).
struct PhysV {
    std::array<float, 6> values{};
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct File {
    int16_t version = 0;
    uint32_t phyt = 0;
    std::vector<Body> bodies;
    std::vector<Shape> shapes;
    std::vector<BoxShape> boxes;
    std::vector<CapsuleShape> capsules;
    std::vector<SphereShape> spheres;
    std::vector<PolytopeShape> polytopes;
    std::vector<Joint> joints;
    std::vector<WeldJoint> weldJoints;
    std::vector<SphericalJoint> sphericalJoints;
    std::vector<ShoulderJoint> shoulderJoints;
    std::vector<PrismaticJoint> prismaticJoints;
    std::vector<RevoluteJoint> revoluteJoints;
    std::vector<DistanceJoint> distanceJoints;
    std::vector<PhysV> phyv;
};

// Parses a complete .phys file already read into memory. Throws ParseError
// if there's no PHYS chunk, a chunk's size isn't an exact multiple of its
// record stride, a SHOJ chunk's size divides evenly by both known strides
// (an ambiguity never seen in any real file checked -- a real occurrence
// is new information, not a case to guess at), or any
// Body::shapeBase/shapeCount, Shape::index, or Joint::bodyA/bodyB/index
// reference falls outside its target array (see this file's own doc
// comment).
File parse(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::phys
