#include "writers/bundle_physics.hpp"

#include <cstdio>
#include <string>

#include "writers/bundle_common.hpp"

namespace husk::writers {

namespace {

void writePhysVec3(json::Writer& w, const char* key, const phys::Vec3& v) {
    writeVec3Field(w, key, canon::Vec3{v.x, v.y, v.z});
}

template <size_t N>
void writeFloats(json::Writer& w, const char* key, const std::array<float, N>& values) {
    writeFloatArrayField(w, key, values.data(), N);
}

std::string hex(const uint8_t* bytes, size_t count) {
    std::string out;
    char digits[3];
    for (size_t i = 0; i < count; ++i) {
        std::snprintf(digits, sizeof digits, "%02x", bytes[i]);
        out += digits;
    }
    return out;
}

const char* shapeKind(int16_t type) {
    switch (type) {
        case 0: return "box";
        case 1: return "capsule";
        case 2: return "sphere";
        case 3: return "polytope";
        default: return "unknown";
    }
}

const char* jointKind(int16_t type) {
    switch (type) {
        case 0: return "spherical";
        case 1: return "shoulder";
        case 2: return "weld";
        case 3: return "revolute";
        case 4: return "prismatic";
        case 5: return "distance";
        default: return "unknown";
    }
}

void writeBody(json::Writer& w, const phys::Body& b) {
    w.beginObject();
    w.key("joint");
    w.value(static_cast<int64_t>(b.boneIndex));
    w.key("type");
    w.value(static_cast<int64_t>(b.type));
    writePhysVec3(w, "position", b.position);
    w.key("shapes");
    w.beginArray();
    for (int32_t i = 0; i < b.shapeCount; ++i) w.value(static_cast<int64_t>(b.shapeBase + i));
    w.endArray();
    w.key("source");
    w.beginObject();
    writeFloatField(w, "unk0", b.unk0);
    writeFloatField(w, "x1c", b.x1c);
    writeFloatField(w, "drag", b.drag);
    writeFloatField(w, "unk1", b.unk1);
    writeFloatField(w, "x28", b.x28);
    w.endObject();
    w.endObject();
}

// Capsule and polytope geometry are verified on real files; box and sphere
// layouts are not (phys.hpp), so theirs stays under `source` as stored.
void writeShape(json::Writer& w, const phys::File& f, const phys::Shape& s) {
    auto at = static_cast<size_t>(s.index);
    w.beginObject();
    w.key("kind");
    w.value(shapeKind(s.type));
    writeFloatField(w, "friction", s.friction);
    writeFloatField(w, "restitution", s.restitution);
    writeFloatField(w, "density", s.density);
    switch (s.type) {
        case 0:
            w.key("source");
            w.beginObject();
            writeFloats(w, "a", f.boxes[at].a);
            writePhysVec3(w, "c", f.boxes[at].c);
            w.endObject();
            break;
        case 1:
            writePhysVec3(w, "a", f.capsules[at].localPosition1);
            writePhysVec3(w, "b", f.capsules[at].localPosition2);
            writeFloatField(w, "radius", f.capsules[at].radius);
            break;
        case 2:
            w.key("source");
            w.beginObject();
            writePhysVec3(w, "local_position", f.spheres[at].localPosition);
            writeFloatField(w, "radius", f.spheres[at].radius);
            w.endObject();
            break;
        case 3: {
            const phys::PolytopeShape& p = f.polytopes[at];
            w.key("vertices");
            w.beginArray();
            for (const phys::Vec3& v : p.vertices) writeVec3Value(w, canon::Vec3{v.x, v.y, v.z});
            w.endArray();
            w.key("source");
            w.beginObject();
            w.key("count_10");
            w.value(static_cast<int64_t>(p.count10));
            w.key("unk1");
            w.beginArray();
            for (const auto& row : p.unk1) w.value(hex(row.data(), row.size()));
            w.endArray();
            w.key("unk2");
            w.value(hex(p.unk2.data(), p.unk2.size()));
            w.key("nodes");
            w.beginArray();
            for (const auto& node : p.nodes) {
                w.beginArray();
                for (int8_t n : node) w.value(static_cast<int64_t>(n));
                w.endArray();
            }
            w.endArray();
            w.endObject();
            break;
        }
        default: break;
    }
    w.endObject();
}

// A joint's kind-specific record as stored: its angle limits' unit is not
// established (radians vs degrees), so every field stays under `source`.
void writeJointSource(json::Writer& w, const phys::File& f, const phys::Joint& j) {
    auto at = static_cast<size_t>(j.index);
    w.key("source");
    w.beginObject();
    switch (j.type) {
        case 0: {
            const auto& s = f.sphericalJoints[at];
            writePhysVec3(w, "anchor_a", s.anchorA);
            writePhysVec3(w, "anchor_b", s.anchorB);
            writeFloatField(w, "friction_torque", s.frictionTorque);
            break;
        }
        case 1: {
            const auto& s = f.shoulderJoints[at];
            writeFloats(w, "frame_a", s.frameA);
            writeFloats(w, "frame_b", s.frameB);
            writeFloatField(w, "lower_twist_angle", s.lowerTwistAngle);
            writeFloatField(w, "upper_twist_angle", s.upperTwistAngle);
            writeFloatField(w, "cone_angle", s.coneAngle);
            writeFloatField(w, "max_motor_torque", s.maxMotorTorque);
            w.key("motor_mode");
            w.value(static_cast<int64_t>(s.motorMode));
            writeFloatField(w, "motor_frequency_hz", s.motorFrequencyHz);
            writeFloatField(w, "motor_damping_ratio", s.motorDampingRatio);
            break;
        }
        case 2: {
            const auto& s = f.weldJoints[at];
            writeFloats(w, "frame_a", s.frameA);
            writeFloats(w, "frame_b", s.frameB);
            writeFloatField(w, "angular_frequency_hz", s.angularFrequencyHz);
            writeFloatField(w, "angular_damping_ratio", s.angularDampingRatio);
            writeFloatField(w, "linear_frequency_hz", s.linearFrequencyHz);
            writeFloatField(w, "linear_damping_ratio", s.linearDampingRatio);
            writeFloatField(w, "unk70", s.unk70);
            break;
        }
        case 3: {
            const auto& s = f.revoluteJoints[at];
            writeFloats(w, "frame_a", s.frameA);
            writeFloats(w, "frame_b", s.frameB);
            writeFloatField(w, "lower_angle", s.lowerAngle);
            writeFloatField(w, "upper_angle", s.upperAngle);
            writeFloatField(w, "max_motor_torque", s.maxMotorTorque);
            w.key("motor_mode");
            w.value(static_cast<int64_t>(s.motorMode));
            writeFloatField(w, "motor_frequency_hz", s.motorFrequencyHz);
            writeFloatField(w, "motor_damping_ratio", s.motorDampingRatio);
            break;
        }
        case 4: {
            const auto& s = f.prismaticJoints[at];
            writeFloats(w, "frame_a", s.frameA);
            writeFloats(w, "frame_b", s.frameB);
            writeFloatField(w, "lower_limit", s.lowerLimit);
            writeFloatField(w, "upper_limit", s.upperLimit);
            writeFloatField(w, "x68", s.x68);
            writeFloatField(w, "max_motor_force", s.maxMotorForce);
            writeFloatField(w, "x70", s.x70);
            w.key("motor_mode");
            w.value(static_cast<int64_t>(s.motorMode));
            writeFloatField(w, "motor_frequency_hz", s.motorFrequencyHz);
            writeFloatField(w, "motor_damping_ratio", s.motorDampingRatio);
            break;
        }
        case 5: {
            const auto& s = f.distanceJoints[at];
            writePhysVec3(w, "local_anchor_a", s.localAnchorA);
            writePhysVec3(w, "local_anchor_b", s.localAnchorB);
            writeFloatField(w, "some_distance_factor", s.someDistanceFactor);
            break;
        }
        default: break;
    }
    w.endObject();
}

}  // namespace

void writePhysics(json::Writer& w, const canon::Physics& physics) {
    w.beginObject();
    w.key("bodies");
    w.beginArray();
    for (const phys::Body& b : physics.bodies) writeBody(w, b);
    w.endArray();
    w.key("shapes");
    w.beginArray();
    for (const phys::Shape& s : physics.shapes) writeShape(w, physics, s);
    w.endArray();
    w.key("joints");
    w.beginArray();
    for (const phys::Joint& j : physics.joints) {
        w.beginObject();
        w.key("kind");
        w.value(jointKind(j.type));
        w.key("body_a");
        w.value(static_cast<int64_t>(j.bodyA));
        w.key("body_b");
        w.value(static_cast<int64_t>(j.bodyB));
        writeJointSource(w, physics, j);
        w.endObject();
    }
    w.endArray();
    w.key("source");
    w.beginObject();
    w.key("version");
    w.value(static_cast<int64_t>(physics.version));
    w.key("phyt");
    w.value(static_cast<int64_t>(physics.phyt));
    w.key("phyv");
    w.beginArray();
    for (const phys::PhysV& v : physics.phyv) {
        w.beginArray();
        for (float x : v.values) w.value(static_cast<double>(x));
        w.endArray();
    }
    w.endArray();
    w.endObject();
    w.endObject();
}

void writeCorrectionSets(json::Writer& w, const std::vector<canon::CorrectionSet>& sets) {
    w.beginArray();
    for (const canon::CorrectionSet& set : sets) {
        w.beginObject();
        w.key("ref");
        writeRef(w, set.ref);
        w.key("corrections");
        w.beginArray();
        for (const canon::CorrectionSet::Correction& c : set.corrections) {
            w.beginObject();
            w.key("joint");
            w.value(static_cast<int64_t>(c.joint));
            writeFloats(w, "matrix", c.matrix);
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
}

}  // namespace husk::writers
