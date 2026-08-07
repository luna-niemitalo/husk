#include "dump_phys.hpp"

#include <array>

#include "dump_writer_utils.hpp"

namespace husk::commands {

namespace {

void writePhysVec3(json::Writer& w, const phys::Vec3& v) {
    w.beginObject();
    w.key("x");
    w.value(static_cast<double>(v.x));
    w.key("y");
    w.value(static_cast<double>(v.y));
    w.key("z");
    w.value(static_cast<double>(v.z));
    w.endObject();
}

void writeMat3x4(json::Writer& w, const std::array<float, 12>& m) {
    w.beginArray();
    for (float f : m) w.value(static_cast<double>(f));
    w.endArray();
}

// Resolves shape `s` (a body's shape entry) to its full type-specific
// record -- `f.boxes`/`capsules`/`spheres`/`polytopes[s.index]`, already
// bounds-checked by husk::phys::parse itself (see phys.hpp's file doc) --
// trusted, not re-checked here.
void writePhysShape(json::Writer& w, const phys::File& f, const phys::Shape& s) {
    w.beginObject();
    w.key("type");
    w.value(static_cast<int64_t>(s.type));
    w.key("index");
    w.value(static_cast<int64_t>(s.index));
    w.key("friction");
    w.value(static_cast<double>(s.friction));
    w.key("restitution");
    w.value(static_cast<double>(s.restitution));
    w.key("density");
    w.value(static_cast<double>(s.density));
    w.key("resolved");
    switch (s.type) {
        case 0: {
            const auto& b = f.boxes[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("box");
            w.key("a");
            writeMat3x4(w, b.a);
            w.key("c");
            writePhysVec3(w, b.c);
            w.endObject();
            break;
        }
        case 1: {
            const auto& c = f.capsules[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("capsule");
            w.key("local_position1");
            writePhysVec3(w, c.localPosition1);
            w.key("local_position2");
            writePhysVec3(w, c.localPosition2);
            w.key("radius");
            w.value(static_cast<double>(c.radius));
            w.endObject();
            break;
        }
        case 2: {
            const auto& sp = f.spheres[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("sphere");
            w.key("local_position");
            writePhysVec3(w, sp.localPosition);
            w.key("radius");
            w.value(static_cast<double>(sp.radius));
            w.endObject();
            break;
        }
        case 3: {
            const auto& p = f.polytopes[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("polytope");
            w.key("vertex_count");
            w.value(static_cast<int64_t>(p.vertexCount));
            w.key("count_10");
            w.value(static_cast<int64_t>(p.count10));
            w.key("node_count");
            w.value(static_cast<int64_t>(p.nodeCount));
            w.key("vertices");
            w.beginArray();
            for (const auto& v : p.vertices) writePhysVec3(w, v);
            w.endArray();
            w.key("unk1_hex");
            w.beginArray();
            for (const auto& u : p.unk1) w.value(hexDump(u.data(), u.size()));
            w.endArray();
            w.key("unk2");
            w.beginArray();
            for (uint8_t u : p.unk2) w.value(static_cast<int64_t>(u));
            w.endArray();
            w.key("nodes");
            w.beginArray();
            for (const auto& n : p.nodes) {
                w.beginObject();
                w.key("unk");
                w.value(static_cast<int64_t>(n[0]));
                w.key("vertex_index");
                w.value(static_cast<int64_t>(n[1]));
                w.key("unk_index0");
                w.value(static_cast<int64_t>(n[2]));
                w.key("unk_index1");
                w.value(static_cast<int64_t>(n[3]));
                w.endObject();
            }
            w.endArray();
            w.endObject();
            break;
        }
        default:
            w.nullValue();
    }
    w.endObject();
}

void writePhysWeldJoint(json::Writer& w, const phys::WeldJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("weld");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("angular_frequency_hz");
    w.value(static_cast<double>(j.angularFrequencyHz));
    w.key("angular_damping_ratio");
    w.value(static_cast<double>(j.angularDampingRatio));
    w.key("linear_frequency_hz");
    w.value(static_cast<double>(j.linearFrequencyHz));
    w.key("linear_damping_ratio");
    w.value(static_cast<double>(j.linearDampingRatio));
    w.key("unk70");
    w.value(static_cast<double>(j.unk70));
    w.endObject();
}

void writePhysSphericalJoint(json::Writer& w, const phys::SphericalJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("spherical");
    w.key("anchor_a");
    writePhysVec3(w, j.anchorA);
    w.key("anchor_b");
    writePhysVec3(w, j.anchorB);
    w.key("friction_torque");
    w.value(static_cast<double>(j.frictionTorque));
    w.endObject();
}

void writePhysShoulderJoint(json::Writer& w, const phys::ShoulderJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("shoulder");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("lower_twist_angle");
    w.value(static_cast<double>(j.lowerTwistAngle));
    w.key("upper_twist_angle");
    w.value(static_cast<double>(j.upperTwistAngle));
    w.key("cone_angle");
    w.value(static_cast<double>(j.coneAngle));
    w.key("max_motor_torque");
    w.value(static_cast<double>(j.maxMotorTorque));
    w.key("motor_mode");
    w.value(static_cast<int64_t>(j.motorMode));
    w.key("motor_frequency_hz");
    w.value(static_cast<double>(j.motorFrequencyHz));
    w.key("motor_damping_ratio");
    w.value(static_cast<double>(j.motorDampingRatio));
    w.endObject();
}

void writePhysPrismaticJoint(json::Writer& w, const phys::PrismaticJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("prismatic");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("lower_limit");
    w.value(static_cast<double>(j.lowerLimit));
    w.key("upper_limit");
    w.value(static_cast<double>(j.upperLimit));
    w.key("max_motor_force");
    w.value(static_cast<double>(j.maxMotorForce));
    w.key("motor_mode");
    w.value(static_cast<int64_t>(j.motorMode));
    w.key("motor_frequency_hz");
    w.value(static_cast<double>(j.motorFrequencyHz));
    w.key("motor_damping_ratio");
    w.value(static_cast<double>(j.motorDampingRatio));
    w.endObject();
}

void writePhysRevoluteJoint(json::Writer& w, const phys::RevoluteJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("revolute");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("lower_angle");
    w.value(static_cast<double>(j.lowerAngle));
    w.key("upper_angle");
    w.value(static_cast<double>(j.upperAngle));
    w.key("max_motor_torque");
    w.value(static_cast<double>(j.maxMotorTorque));
    w.key("motor_mode");
    w.value(static_cast<int64_t>(j.motorMode));
    w.key("motor_frequency_hz");
    w.value(static_cast<double>(j.motorFrequencyHz));
    w.key("motor_damping_ratio");
    w.value(static_cast<double>(j.motorDampingRatio));
    w.endObject();
}

void writePhysDistanceJoint(json::Writer& w, const phys::DistanceJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("distance");
    w.key("local_anchor_a");
    writePhysVec3(w, j.localAnchorA);
    w.key("local_anchor_b");
    writePhysVec3(w, j.localAnchorB);
    w.key("some_distance_factor");
    w.value(static_cast<double>(j.someDistanceFactor));
    w.endObject();
}

// `j.bodyA`/`bodyB` are left as plain indices (not resolved to the body's
// own data) -- that's how the source data itself relates bodies to each
// other, and a caller already has the `bodies` array from the same JSON
// document to look them up in.
void writePhysJoint(json::Writer& w, const phys::File& f, const phys::Joint& j) {
    w.beginObject();
    w.key("body_a");
    w.value(static_cast<int64_t>(j.bodyA));
    w.key("body_b");
    w.value(static_cast<int64_t>(j.bodyB));
    w.key("type");
    w.value(static_cast<int64_t>(j.type));
    w.key("index");
    w.value(static_cast<int64_t>(j.index));
    w.key("resolved");
    switch (j.type) {
        case 0: writePhysSphericalJoint(w, f.sphericalJoints[static_cast<size_t>(j.index)]); break;
        case 1: writePhysShoulderJoint(w, f.shoulderJoints[static_cast<size_t>(j.index)]); break;
        case 2: writePhysWeldJoint(w, f.weldJoints[static_cast<size_t>(j.index)]); break;
        case 3: writePhysRevoluteJoint(w, f.revoluteJoints[static_cast<size_t>(j.index)]); break;
        case 4: writePhysPrismaticJoint(w, f.prismaticJoints[static_cast<size_t>(j.index)]); break;
        case 5: writePhysDistanceJoint(w, f.distanceJoints[static_cast<size_t>(j.index)]); break;
        default: w.nullValue();
    }
    w.endObject();
}

}  // namespace

void writePhysFile(json::Writer& w, const phys::File& f) {
    w.beginObject();
    w.key("version");
    w.value(static_cast<int64_t>(f.version));
    w.key("phyt");
    w.value(static_cast<int64_t>(f.phyt));

    w.key("bodies");
    w.beginArray();
    for (size_t i = 0; i < f.bodies.size(); ++i) {
        const auto& b = f.bodies[i];
        w.beginObject();
        w.key("index");
        w.value(static_cast<int64_t>(i));
        w.key("type");
        w.value(static_cast<int64_t>(b.type));
        w.key("bone");
        w.value(static_cast<int64_t>(b.boneIndex));
        w.key("position");
        writePhysVec3(w, b.position);
        w.key("shape_base");
        w.value(static_cast<int64_t>(b.shapeBase));
        w.key("shape_count");
        w.value(static_cast<int64_t>(b.shapeCount));
        w.key("unk0");
        w.value(static_cast<double>(b.unk0));
        w.key("x1c");
        w.value(static_cast<double>(b.x1c));
        w.key("drag");
        w.value(static_cast<double>(b.drag));
        w.key("unk1");
        w.value(static_cast<double>(b.unk1));
        w.key("x28");
        w.value(static_cast<double>(b.x28));
        w.key("shapes");
        w.beginArray();
        for (int32_t si = b.shapeBase; si < b.shapeBase + b.shapeCount; ++si) {
            writePhysShape(w, f, f.shapes[static_cast<size_t>(si)]);
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();

    w.key("joints");
    w.beginArray();
    for (const auto& j : f.joints) writePhysJoint(w, f, j);
    w.endArray();

    w.key("phyv");
    w.beginArray();
    for (const auto& v : f.phyv) {
        w.beginArray();
        for (float x : v.values) w.value(static_cast<double>(x));
        w.endArray();
    }
    w.endArray();

    w.endObject();
}

}  // namespace husk::commands
