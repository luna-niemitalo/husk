#include "export_transform.hpp"

#include <cmath>

namespace husk::commands {

gltf::Vec3 toGltf(const m2::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }
gltf::Vec3 toGltf(const phys::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }

gltf::Quat toGltf(const m2::Quat& q) { return gltf::rotationZUpToYUp({q.x, q.y, q.z, q.w}); }

gltf::Vec3 toGltfScale(const m2::Vec3& s) { return gltf::scaleZUpToYUp({s.x, s.y, s.z}); }

bool isFinite(const m2::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool isFinite(const m2::Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

}  // namespace husk::commands
