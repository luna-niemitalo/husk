#include "phys.hpp"

#include <cstring>

#include "chunk.hpp"

namespace husk::phys {

namespace {

// wowdev.wiki tag -> on-disk reversed literal (WMO/ADT convention -- see
// phys.hpp's doc comment; chunk.hpp's own doc comment covers M2's opposite,
// non-reversed convention).
constexpr std::string_view kTagPhys = "SYHP";  // PHYS
constexpr std::string_view kTagPhyt = "TYHP";  // PHYT
constexpr std::string_view kTagBody = "YDOB";  // BODY
constexpr std::string_view kTagBdy2 = "2YDB";  // BDY2
constexpr std::string_view kTagBdy3 = "3YDB";  // BDY3
constexpr std::string_view kTagBdy4 = "4YDB";  // BDY4
constexpr std::string_view kTagShap = "PAHS";  // SHAP
constexpr std::string_view kTagShp2 = "2PHS";  // SHP2
constexpr std::string_view kTagBoxs = "SXOB";  // BOXS
constexpr std::string_view kTagCaps = "SPAC";  // CAPS
constexpr std::string_view kTagSphs = "SHPS";  // SPHS
constexpr std::string_view kTagPlyt = "TYLP";  // PLYT
constexpr std::string_view kTagJoin = "NIOJ";  // JOIN
constexpr std::string_view kTagWelj = "JLEW";  // WELJ
constexpr std::string_view kTagWlj2 = "2JLW";  // WLJ2
constexpr std::string_view kTagWlj3 = "3JLW";  // WLJ3
constexpr std::string_view kTagSphj = "JHPS";  // SPHJ
constexpr std::string_view kTagShoj = "JOHS";  // SHOJ
constexpr std::string_view kTagShj2 = "2JHS";  // SHJ2
constexpr std::string_view kTagPrsj = "JSRP";  // PRSJ
constexpr std::string_view kTagPrs2 = "2SRP";  // PRS2
constexpr std::string_view kTagRevj = "JVER";  // REVJ
constexpr std::string_view kTagRev2 = "2VER";  // REV2
constexpr std::string_view kTagDstj = "JTSD";  // DSTJ
constexpr std::string_view kTagPhyv = "VYHP";  // PHYV

void checkFits(size_t bufSize, size_t off, size_t need, const char* what) {
    if (off + need > bufSize) {
        throw ParseError(std::string(what) + " at offset " + std::to_string(off) + " needs " +
                          std::to_string(need) + " bytes but the chunk is only " +
                          std::to_string(bufSize) + " bytes");
    }
}

uint16_t readU16(const uint8_t* buf, size_t bufSize, size_t off) {
    checkFits(bufSize, off, 2, "uint16 field");
    uint16_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

int16_t readI16(const uint8_t* buf, size_t bufSize, size_t off) {
    checkFits(bufSize, off, 2, "int16 field");
    int16_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

uint32_t readU32(const uint8_t* buf, size_t bufSize, size_t off) {
    checkFits(bufSize, off, 4, "uint32 field");
    uint32_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

int32_t readI32(const uint8_t* buf, size_t bufSize, size_t off) {
    checkFits(bufSize, off, 4, "int32 field");
    int32_t v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

float readF32(const uint8_t* buf, size_t bufSize, size_t off) {
    checkFits(bufSize, off, 4, "float field");
    float v;
    std::memcpy(&v, buf + off, sizeof(v));
    return v;
}

Vec3 readVec3(const uint8_t* buf, size_t bufSize, size_t off) {
    return {readF32(buf, bufSize, off), readF32(buf, bufSize, off + 4), readF32(buf, bufSize, off + 8)};
}

std::array<float, 12> readMat3x4(const uint8_t* buf, size_t bufSize, size_t off) {
    std::array<float, 12> m{};
    for (size_t i = 0; i < 12; ++i) m[i] = readF32(buf, bufSize, off + i * 4);
    return m;
}

// Every non-PLYT record chunk is a flat array of fixed-stride records --
// checks the chunk's size is an exact multiple of `stride` (never a partial
// trailing record, same discipline skel.cpp's record parsers use) and calls
// `readOne` once per record.
template <typename T, typename ReadOne>
std::vector<T> parseFixedRecords(const Chunk& chunk, size_t stride, const char* chunkName,
                                  ReadOne readOne) {
    if (chunk.size % stride != 0) {
        throw ParseError(std::string(chunkName) + " chunk is " + std::to_string(chunk.size) +
                          " bytes, not an exact multiple of its " + std::to_string(stride) +
                          "-byte record stride");
    }
    size_t count = chunk.size / stride;
    std::vector<T> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.push_back(readOne(chunk.data, chunk.size, i * stride));
    }
    return result;
}

Body readBody(const uint8_t* d, size_t n, size_t o) {  // BODY, v0/1 -- 0x1c (28) bytes
    Body b;
    b.type = readU16(d, n, o + 0x00);
    b.position = readVec3(d, n, o + 0x04);
    b.boneIndex = readU16(d, n, o + 0x10);
    b.shapeBase = readI32(d, n, o + 0x14);
    b.shapeCount = readI32(d, n, o + 0x18);
    return b;
}

Body readBdy2(const uint8_t* d, size_t n, size_t o) {  // BDY2 -- 0x20 (32) bytes
    Body b = readBody(d, n, o);
    b.x1c = readF32(d, n, o + 0x1c);
    return b;
}

Body readBdy3(const uint8_t* d, size_t n, size_t o) {  // BDY3 -- 0x2c (44) bytes
    Body b;
    b.type = readU16(d, n, o + 0x00);
    b.boneIndex = readU16(d, n, o + 0x02);
    b.position = readVec3(d, n, o + 0x04);
    b.shapeBase = readU16(d, n, o + 0x10);
    b.shapeCount = readI32(d, n, o + 0x14);
    b.unk0 = readF32(d, n, o + 0x18);
    b.x1c = readF32(d, n, o + 0x1c);
    b.drag = readF32(d, n, o + 0x20);
    b.unk1 = readF32(d, n, o + 0x24);
    b.x28 = readF32(d, n, o + 0x28);
    return b;
}

Body readBdy4(const uint8_t* d, size_t n, size_t o) {  // BDY4 -- 0x30 (48) bytes, trailing 4 unused
    return readBdy3(d, n, o);
}

std::vector<Body> parseBodies(const std::vector<Chunk>& chunks) {
    if (auto c = findChunk(chunks, kTagBdy4)) return parseFixedRecords<Body>(*c, 0x30, "BDY4", readBdy4);
    if (auto c = findChunk(chunks, kTagBdy3)) return parseFixedRecords<Body>(*c, 0x2c, "BDY3", readBdy3);
    if (auto c = findChunk(chunks, kTagBdy2)) return parseFixedRecords<Body>(*c, 0x20, "BDY2", readBdy2);
    if (auto c = findChunk(chunks, kTagBody)) return parseFixedRecords<Body>(*c, 0x1c, "BODY", readBody);
    return {};
}

Shape readShape(const uint8_t* d, size_t n, size_t o) {  // shared prefix of SHAP/SHP2
    Shape s;
    s.type = readI16(d, n, o + 0x00);
    s.index = readI16(d, n, o + 0x02);
    s.friction = readF32(d, n, o + 0x08);
    s.restitution = readF32(d, n, o + 0x0c);
    s.density = readF32(d, n, o + 0x10);
    return s;
}

std::vector<Shape> parseShapes(const std::vector<Chunk>& chunks) {
    if (auto c = findChunk(chunks, kTagShp2)) return parseFixedRecords<Shape>(*c, 0x20, "SHP2", readShape);
    if (auto c = findChunk(chunks, kTagShap)) return parseFixedRecords<Shape>(*c, 0x14, "SHAP", readShape);
    return {};
}

std::vector<BoxShape> parseBoxes(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagBoxs);
    if (!c) return {};
    return parseFixedRecords<BoxShape>(*c, 0x3c, "BOXS", [](const uint8_t* d, size_t n, size_t o) {
        BoxShape s;
        for (size_t i = 0; i < 12; ++i) s.a[i] = readF32(d, n, o + i * 4);
        s.c = readVec3(d, n, o + 0x30);
        return s;
    });
}

std::vector<CapsuleShape> parseCapsules(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagCaps);
    if (!c) return {};
    return parseFixedRecords<CapsuleShape>(*c, 0x1c, "CAPS", [](const uint8_t* d, size_t n, size_t o) {
        CapsuleShape s;
        s.localPosition1 = readVec3(d, n, o + 0x00);
        s.localPosition2 = readVec3(d, n, o + 0x0c);
        s.radius = readF32(d, n, o + 0x18);
        return s;
    });
}

std::vector<SphereShape> parseSpheres(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagSphs);
    if (!c) return {};
    return parseFixedRecords<SphereShape>(*c, 0x10, "SPHS", [](const uint8_t* d, size_t n, size_t o) {
        SphereShape s;
        s.localPosition = readVec3(d, n, o + 0x00);
        s.radius = readF32(d, n, o + 0x0c);
        return s;
    });
}

// PLYT combines a fixed-stride header array with a variable-length data
// region whose per-entry size depends on that same entry's own header
// (PHYS.md's own text -- see phys.hpp's doc comment). Not a
// parseFixedRecords case: verifies the *total* chunk size matches the sum
// of every entry's header + data size exactly.
std::vector<PolytopeShape> parsePolytopes(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagPlyt);
    if (!c) return {};
    const uint8_t* d = c->data;
    size_t n = c->size;
    uint32_t count = readU32(d, n, 0x00);

    constexpr size_t kHeaderStride = 0x50;
    size_t headerOff = 4;
    size_t headerBytes = static_cast<size_t>(count) * kHeaderStride;
    checkFits(n, headerOff, headerBytes, "PLYT header array");

    std::vector<PolytopeShape> shapes;
    shapes.reserve(count);
    size_t dataOff = headerOff + headerBytes;
    for (uint32_t i = 0; i < count; ++i) {
        size_t ho = headerOff + i * kHeaderStride;
        PolytopeShape s;
        s.vertexCount = readU32(d, n, ho + 0x00);
        s.count10 = readU32(d, n, ho + 0x10);
        s.nodeCount = readU32(d, n, ho + 0x28);

        size_t vertBytes = static_cast<size_t>(s.vertexCount) * 12;
        checkFits(n, dataOff, vertBytes, "PLYT vertex data");
        s.vertices.reserve(s.vertexCount);
        for (size_t v = 0; v < s.vertexCount; ++v) s.vertices.push_back(readVec3(d, n, dataOff + v * 12));
        dataOff += vertBytes;

        size_t unk1Bytes = static_cast<size_t>(s.count10) * 16;
        checkFits(n, dataOff, unk1Bytes, "PLYT unk_1 data");
        s.unk1.reserve(s.count10);
        for (size_t u = 0; u < s.count10; ++u) {
            std::array<uint8_t, 16> entry{};
            std::memcpy(entry.data(), d + dataOff + u * 16, 16);
            s.unk1.push_back(entry);
        }
        dataOff += unk1Bytes;

        checkFits(n, dataOff, s.count10, "PLYT unk_2 data");
        s.unk2.assign(d + dataOff, d + dataOff + s.count10);
        dataOff += s.count10;

        size_t nodeBytes = static_cast<size_t>(s.nodeCount) * 4;
        checkFits(n, dataOff, nodeBytes, "PLYT node data");
        s.nodes.reserve(s.nodeCount);
        for (size_t nd = 0; nd < s.nodeCount; ++nd) {
            std::array<int8_t, 4> entry{};
            std::memcpy(entry.data(), d + dataOff + nd * 4, 4);
            s.nodes.push_back(entry);
        }
        dataOff += nodeBytes;

        shapes.push_back(std::move(s));
    }

    if (dataOff != n) {
        throw ParseError("PLYT chunk's header+data doesn't consume the whole chunk: expected " +
                          std::to_string(dataOff) + " bytes, chunk is " + std::to_string(n) + " bytes");
    }
    return shapes;
}

std::vector<Joint> parseJoints(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagJoin);
    if (!c) return {};
    return parseFixedRecords<Joint>(*c, 0x10, "JOIN", [](const uint8_t* d, size_t n, size_t o) {
        Joint j;
        j.bodyA = readU32(d, n, o + 0x00);
        j.bodyB = readU32(d, n, o + 0x04);
        j.type = readI16(d, n, o + 0x0c);
        j.index = readI16(d, n, o + 0x0e);
        return j;
    });
}

WeldJoint readWeldBase(const uint8_t* d, size_t n, size_t o) {
    WeldJoint w;
    w.frameA = readMat3x4(d, n, o + 0x00);
    w.frameB = readMat3x4(d, n, o + 0x30);
    w.angularFrequencyHz = readF32(d, n, o + 0x60);
    w.angularDampingRatio = readF32(d, n, o + 0x64);
    return w;
}

std::vector<WeldJoint> parseWeldJoints(const std::vector<Chunk>& chunks) {
    if (auto c = findChunk(chunks, kTagWlj3)) {
        return parseFixedRecords<WeldJoint>(*c, 0x74, "WLJ3", [](const uint8_t* d, size_t n, size_t o) {
            WeldJoint w = readWeldBase(d, n, o);
            w.linearFrequencyHz = readF32(d, n, o + 0x68);
            w.linearDampingRatio = readF32(d, n, o + 0x6c);
            w.unk70 = readF32(d, n, o + 0x70);
            return w;
        });
    }
    if (auto c = findChunk(chunks, kTagWlj2)) {
        return parseFixedRecords<WeldJoint>(*c, 0x70, "WLJ2", [](const uint8_t* d, size_t n, size_t o) {
            WeldJoint w = readWeldBase(d, n, o);
            w.linearFrequencyHz = readF32(d, n, o + 0x68);
            w.linearDampingRatio = readF32(d, n, o + 0x6c);
            return w;
        });
    }
    if (auto c = findChunk(chunks, kTagWelj)) {
        return parseFixedRecords<WeldJoint>(*c, 0x68, "WELJ", readWeldBase);
    }
    return {};
}

std::vector<SphericalJoint> parseSphericalJoints(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagSphj);
    if (!c) return {};
    return parseFixedRecords<SphericalJoint>(*c, 0x1c, "SPHJ", [](const uint8_t* d, size_t n, size_t o) {
        SphericalJoint s;
        s.anchorA = readVec3(d, n, o + 0x00);
        s.anchorB = readVec3(d, n, o + 0x0c);
        s.frictionTorque = readF32(d, n, o + 0x18);
        return s;
    });
}

ShoulderJoint readShoulderBase(const uint8_t* d, size_t n, size_t o) {
    ShoulderJoint s;
    s.frameA = readMat3x4(d, n, o + 0x00);
    s.frameB = readMat3x4(d, n, o + 0x30);
    s.lowerTwistAngle = readF32(d, n, o + 0x60);
    s.upperTwistAngle = readF32(d, n, o + 0x64);
    s.coneAngle = readF32(d, n, o + 0x68);
    return s;
}

std::vector<ShoulderJoint> parseShoulderJoints(const std::vector<Chunk>& chunks) {
    if (auto c = findChunk(chunks, kTagShj2)) {
        return parseFixedRecords<ShoulderJoint>(*c, 0x7c, "SHJ2", [](const uint8_t* d, size_t n, size_t o) {
            ShoulderJoint s = readShoulderBase(d, n, o);
            s.maxMotorTorque = readF32(d, n, o + 0x6c);
            s.motorMode = readU32(d, n, o + 0x70);
            s.motorFrequencyHz = readF32(d, n, o + 0x74);
            s.motorDampingRatio = readF32(d, n, o + 0x78);
            return s;
        });
    }
    auto c = findChunk(chunks, kTagShoj);
    if (!c) return {};
    // SHOJ is ambiguous between two real strides -- 0x6c (pre-7.0.1.20979,
    // no motor fields) and 0x74 (post, adds maxMotorTorque/motorMode) --
    // sharing one chunk tag (PHYS.md's own text: "even though this chunk is
    // handled differently since version 2, it does not have a version 2*
    // chunk name"). A real file hitting both strides at once is new
    // information, not a case to silently pick one for.
    // TODO: Remove: checked all 86 real SHOJ chunks found this session --
    // never both at once, see `WIKI_FINDINGS/PHYS.md`.
    bool fits6c = c->size % 0x6c == 0;
    bool fits74 = c->size % 0x74 == 0;
    if (fits6c && fits74) {
        throw ParseError("SHOJ chunk (" + std::to_string(c->size) +
                          " bytes) divides evenly by both known strides (0x6c and 0x74) -- "
                          "genuinely ambiguous, not seen in any real file this parser was checked "
                          "against");
    }
    if (fits74) {
        return parseFixedRecords<ShoulderJoint>(*c, 0x74, "SHOJ", [](const uint8_t* d, size_t n, size_t o) {
            ShoulderJoint s = readShoulderBase(d, n, o);
            s.maxMotorTorque = readF32(d, n, o + 0x6c);
            s.motorMode = readU32(d, n, o + 0x70);
            return s;
        });
    }
    if (fits6c) {
        return parseFixedRecords<ShoulderJoint>(*c, 0x6c, "SHOJ", readShoulderBase);
    }
    throw ParseError("SHOJ chunk (" + std::to_string(c->size) +
                      " bytes) doesn't divide evenly by either known stride (0x6c or 0x74)");
}

std::vector<PrismaticJoint> parsePrismaticJoints(const std::vector<Chunk>& chunks) {
    auto readBase = [](const uint8_t* d, size_t n, size_t o) {
        PrismaticJoint p;
        p.frameA = readMat3x4(d, n, o + 0x00);
        p.frameB = readMat3x4(d, n, o + 0x30);
        p.lowerLimit = readF32(d, n, o + 0x60);
        p.upperLimit = readF32(d, n, o + 0x64);
        p.x68 = readF32(d, n, o + 0x68);
        p.maxMotorForce = readF32(d, n, o + 0x6c);
        p.x70 = readF32(d, n, o + 0x70);
        p.motorMode = readU32(d, n, o + 0x74);
        return p;
    };
    if (auto c = findChunk(chunks, kTagPrs2)) {
        return parseFixedRecords<PrismaticJoint>(*c, 0x80, "PRS2", [&](const uint8_t* d, size_t n, size_t o) {
            PrismaticJoint p = readBase(d, n, o);
            p.motorFrequencyHz = readF32(d, n, o + 0x78);
            p.motorDampingRatio = readF32(d, n, o + 0x7c);
            return p;
        });
    }
    if (auto c = findChunk(chunks, kTagPrsj)) {
        return parseFixedRecords<PrismaticJoint>(*c, 0x78, "PRSJ", readBase);
    }
    return {};
}

std::vector<RevoluteJoint> parseRevoluteJoints(const std::vector<Chunk>& chunks) {
    auto readBase = [](const uint8_t* d, size_t n, size_t o) {
        RevoluteJoint r;
        r.frameA = readMat3x4(d, n, o + 0x00);
        r.frameB = readMat3x4(d, n, o + 0x30);
        r.lowerAngle = readF32(d, n, o + 0x60);
        r.upperAngle = readF32(d, n, o + 0x64);
        r.maxMotorTorque = readF32(d, n, o + 0x68);
        r.motorMode = readU32(d, n, o + 0x6c);
        return r;
    };
    if (auto c = findChunk(chunks, kTagRev2)) {
        return parseFixedRecords<RevoluteJoint>(*c, 0x78, "REV2", [&](const uint8_t* d, size_t n, size_t o) {
            RevoluteJoint r = readBase(d, n, o);
            r.motorFrequencyHz = readF32(d, n, o + 0x70);
            r.motorDampingRatio = readF32(d, n, o + 0x74);
            return r;
        });
    }
    if (auto c = findChunk(chunks, kTagRevj)) {
        return parseFixedRecords<RevoluteJoint>(*c, 0x70, "REVJ", readBase);
    }
    return {};
}

std::vector<DistanceJoint> parseDistanceJoints(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagDstj);
    if (!c) return {};
    return parseFixedRecords<DistanceJoint>(*c, 0x1c, "DSTJ", [](const uint8_t* d, size_t n, size_t o) {
        DistanceJoint dj;
        dj.localAnchorA = readVec3(d, n, o + 0x00);
        dj.localAnchorB = readVec3(d, n, o + 0x0c);
        dj.someDistanceFactor = readF32(d, n, o + 0x18);
        return dj;
    });
}

std::vector<PhysV> parsePhysV(const std::vector<Chunk>& chunks) {
    auto c = findChunk(chunks, kTagPhyv);
    if (!c) return {};
    return parseFixedRecords<PhysV>(*c, 0x18, "PHYV", [](const uint8_t* d, size_t n, size_t o) {
        PhysV v;
        for (size_t i = 0; i < 6; ++i) v.values[i] = readF32(d, n, o + i * 4);
        return v;
    });
}

// A violation here is corruption or a parser bug (see phys.hpp's file
// doc) -- caught immediately rather than surfacing as an out-of-range read
// somewhere downstream (dump-chunks/gltf.cpp's own extras attachment).
void validateReferences(const File& f) {
    for (size_t i = 0; i < f.bodies.size(); ++i) {
        const auto& b = f.bodies[i];
        if (b.shapeBase < 0 || b.shapeCount < 0 ||
            static_cast<size_t>(b.shapeBase) + static_cast<size_t>(b.shapeCount) > f.shapes.size()) {
            throw ParseError("body " + std::to_string(i) + " references shapes [" +
                              std::to_string(b.shapeBase) + ", " +
                              std::to_string(b.shapeBase + b.shapeCount) + ") but this file only has " +
                              std::to_string(f.shapes.size()) + " shape(s)");
        }
    }
    for (size_t i = 0; i < f.shapes.size(); ++i) {
        const auto& s = f.shapes[i];
        size_t targetCount = 0;
        const char* targetName;
        switch (s.type) {
            case 0: targetCount = f.boxes.size(); targetName = "box"; break;
            case 1: targetCount = f.capsules.size(); targetName = "capsule"; break;
            case 2: targetCount = f.spheres.size(); targetName = "sphere"; break;
            case 3: targetCount = f.polytopes.size(); targetName = "polytope"; break;
            default:
                throw ParseError("shape " + std::to_string(i) + " has unrecognized type " +
                                  std::to_string(s.type));
        }
        if (s.index < 0 || static_cast<size_t>(s.index) >= targetCount) {
            throw ParseError("shape " + std::to_string(i) + " (type " + std::to_string(s.type) +
                              ", " + targetName + ") references index " + std::to_string(s.index) +
                              " but this file only has " + std::to_string(targetCount) + " " +
                              targetName + "(s)");
        }
    }
    for (size_t i = 0; i < f.joints.size(); ++i) {
        const auto& j = f.joints[i];
        if (j.bodyA >= f.bodies.size() || j.bodyB >= f.bodies.size()) {
            throw ParseError("joint " + std::to_string(i) + " references bodies (" +
                              std::to_string(j.bodyA) + ", " + std::to_string(j.bodyB) +
                              ") but this file only has " + std::to_string(f.bodies.size()) +
                              " body/bodies");
        }
        size_t targetCount = 0;
        const char* targetName;
        switch (j.type) {
            case 0: targetCount = f.sphericalJoints.size(); targetName = "spherical"; break;
            case 1: targetCount = f.shoulderJoints.size(); targetName = "shoulder"; break;
            case 2: targetCount = f.weldJoints.size(); targetName = "weld"; break;
            case 3: targetCount = f.revoluteJoints.size(); targetName = "revolute"; break;
            case 4: targetCount = f.prismaticJoints.size(); targetName = "prismatic"; break;
            case 5: targetCount = f.distanceJoints.size(); targetName = "distance"; break;
            default:
                throw ParseError("joint " + std::to_string(i) + " has unrecognized type " +
                                  std::to_string(j.type));
        }
        if (j.index < 0 || static_cast<size_t>(j.index) >= targetCount) {
            throw ParseError("joint " + std::to_string(i) + " (type " + std::to_string(j.type) + ", " +
                              targetName + ") references index " + std::to_string(j.index) +
                              " but this file only has " + std::to_string(targetCount) + " " +
                              targetName + " joint(s)");
        }
    }
}

}  // namespace

File parse(const std::vector<uint8_t>& fileBytes) {
    auto chunks = readChunks(fileBytes.data(), fileBytes.size());
    auto physChunk = findChunk(chunks, kTagPhys);
    if (!physChunk) {
        std::string found;
        for (const auto& c : chunks) {
            if (!found.empty()) found += ", ";
            found += c.tag;
        }
        throw ParseError("no PHYS chunk found; chunks found (on-disk, reversed): [" + found + "]");
    }

    File f;
    f.version = readI16(physChunk->data, physChunk->size, 0);
    if (auto phytChunk = findChunk(chunks, kTagPhyt)) {
        f.phyt = readU32(phytChunk->data, phytChunk->size, 0);
    }
    f.bodies = parseBodies(chunks);
    f.shapes = parseShapes(chunks);
    f.boxes = parseBoxes(chunks);
    f.capsules = parseCapsules(chunks);
    f.spheres = parseSpheres(chunks);
    f.polytopes = parsePolytopes(chunks);
    f.joints = parseJoints(chunks);
    f.weldJoints = parseWeldJoints(chunks);
    f.sphericalJoints = parseSphericalJoints(chunks);
    f.shoulderJoints = parseShoulderJoints(chunks);
    f.prismaticJoints = parsePrismaticJoints(chunks);
    f.revoluteJoints = parseRevoluteJoints(chunks);
    f.distanceJoints = parseDistanceJoints(chunks);
    f.phyv = parsePhysV(chunks);

    validateReferences(f);
    return f;
}

}  // namespace husk::phys
