#include "gltf_math.hpp"

#include <cmath>

namespace husk::gltf {

namespace {

// The single source of truth for WoW's Z-up -> glTF's Y-up axis conversion
// -- see zUpToYUp/rotationZUpToYUp/scaleZUpToYUp's own doc comments
// (gltf_math.hpp) for why this is one matrix, mechanically applied per value
// kind, rather than three independently hand-derived functions. Row-major:
// out = M * in.
struct Mat3 {
    float m[3][3];
};

// (x, y, z) -> (x, z, -y) -- see gltf_math.hpp's doc comment on zUpToYUp for
// the full conversion contract this implements.
constexpr Mat3 kWowToGltf = {{
    {1, 0, 0},
    {0, 0, 1},
    {0, -1, 0},
}};

constexpr float mat3Determinant(const Mat3& a) {
    return a.m[0][0] * (a.m[1][1] * a.m[2][2] - a.m[1][2] * a.m[2][1]) -
           a.m[0][1] * (a.m[1][0] * a.m[2][2] - a.m[1][2] * a.m[2][0]) +
           a.m[0][2] * (a.m[1][0] * a.m[2][1] - a.m[1][1] * a.m[2][0]);
}

// A negative determinant here would mean kWowToGltf is a reflection, not a
// rotation -- the rotation conjugation below (R' = M R M^T) is only valid
// for a proper rotation basis change. Doesn't, on its own, distinguish a
// correct proper rotation from an incorrect one that's still a valid
// rotation (e.g. the exact inverse of the correct conversion) -- this
// catches a different, simpler mistake (an accidental handedness flip),
// not that one.
static_assert(mat3Determinant(kWowToGltf) > 0.0f,
              "WoW->glTF axis conversion must be a proper rotation (determinant +1)");

Vec3 applyMat3(const Mat3& M, const Vec3& v) {
    return {
        M.m[0][0] * v.x + M.m[0][1] * v.y + M.m[0][2] * v.z,
        M.m[1][0] * v.x + M.m[1][1] * v.y + M.m[1][2] * v.z,
        M.m[2][0] * v.x + M.m[2][1] * v.y + M.m[2][2] * v.z,
    };
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    }
    return r;
}

Mat3 transpose(const Mat3& a) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i];
    }
    return r;
}

Mat3 quatToMat3(const Quat& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return {{
        {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
        {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
        {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)},
    }};
}

// Standard robust matrix->quaternion conversion (Shepperd's method: picks
// whichever of w/x/y/z has the largest magnitude as the division pivot, to
// avoid the numerical blowup a naive formula hits near a 180-degree
// rotation). The returned quaternion's sign (q vs. -q, the same rotation
// either way) isn't normalized against any convention -- callers that care
// about literal component values, not just the rotation itself, should
// compare via a property (e.g. does it rotate a probe vector the same way),
// not raw equality.
Quat mat3ToQuat(const Mat3& m) {
    float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
    Quat q;
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.m[2][1] - m.m[1][2]) / s;
        q.y = (m.m[0][2] - m.m[2][0]) / s;
        q.z = (m.m[1][0] - m.m[0][1]) / s;
    } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        float s = std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f;
        q.w = (m.m[2][1] - m.m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[0][1] + m.m[1][0]) / s;
        q.z = (m.m[0][2] + m.m[2][0]) / s;
    } else if (m.m[1][1] > m.m[2][2]) {
        float s = std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f;
        q.w = (m.m[0][2] - m.m[2][0]) / s;
        q.x = (m.m[0][1] + m.m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[1][2] + m.m[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f;
        q.w = (m.m[1][0] - m.m[0][1]) / s;
        q.x = (m.m[0][2] + m.m[2][0]) / s;
        q.y = (m.m[1][2] + m.m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

}  // namespace

Vec3 zUpToYUp(const Vec3& v) { return applyMat3(kWowToGltf, v); }

Quat rotationZUpToYUp(const Quat& q) {
    Mat3 r = quatToMat3(q);
    Mat3 rPrime = multiply(multiply(kWowToGltf, r), transpose(kWowToGltf));
    return mat3ToQuat(rPrime);
}

Vec3 scaleZUpToYUp(const Vec3& s) {
    Mat3 unsignedM = kWowToGltf;
    for (auto& row : unsignedM.m) {
        for (auto& e : row) e = std::abs(e);
    }
    return applyMat3(unsignedM, s);
}

}  // namespace husk::gltf
