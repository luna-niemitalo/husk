#include <fstream>
#include <iostream>
#include <stdexcept>

#include "commands.hpp"
#include "gltf.hpp"
#include "m2.hpp"
#include "skin.hpp"

// Roadmap stage 1 ("Static mesh, no material", see README.md): resolves an
// M2's vertex array plus a .skin file's two-level triangle-index lookup
// (see src/skin.hpp), converts WoW's Z-up coordinates to glTF's Y-up, and
// writes a minimal single-primitive glTF binary. No material, no image, no
// skin/skeleton -- later roadmap stages add those.
namespace husk::commands {

namespace {

void printUsage() {
    std::cerr << "usage: husk export <file.m2> <file.skin> <output.glb>\n"
                 "\n"
                 "Exports a static (untextured, unskinned) mesh: resolves the M2's\n"
                 "vertex array and the .skin file's triangle-index lookup tables,\n"
                 "converts WoW's Z-up coordinates to glTF's Y-up, and writes a\n"
                 "minimal single-primitive glTF binary (.glb) -- positions,\n"
                 "normals, and UVs, no material, no image, no skin.\n";
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("couldn't open '" + path + "' for reading");
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw std::runtime_error("error reading '" + path + "'");
    }
    return bytes;
}

gltf::Vec3 toGltf(const m2::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }

}  // namespace

int exportGlb(int argc, char** args) {
    if (argc != 3) {
        printUsage();
        return 1;
    }

    std::string modelPath = args[0];
    std::string skinPath = args[1];
    std::string outputPath = args[2];

    try {
        auto modelBytes = readFileBytes(modelPath);
        auto header = m2::parseHeader(modelBytes);
        auto blob = m2::extractBlob(modelBytes);
        auto vertices = m2::parseVertices(blob, header.vertices);

        auto skinBytes = readFileBytes(skinPath);
        auto skinHeader = skin::parseHeader(skinBytes);
        auto triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);

        // Cross-module boundary check: skin::resolveTriangleIndices only
        // validates indices against the skin file's own `vertices` array --
        // it has no idea how many vertices the M2 actually has. A skin file
        // that doesn't belong to this M2 (wrong LOD, wrong model) shows up
        // here as an out-of-range global vertex index.
        for (uint32_t idx : triangleIndices) {
            if (idx >= vertices.size()) {
                throw std::runtime_error(
                    "'" + skinPath + "' references M2 vertex " + std::to_string(idx) + " but '" +
                    modelPath + "' only has " + std::to_string(vertices.size()) +
                    " vertices -- model/.skin mismatch?");
            }
        }

        gltf::Mesh mesh;
        mesh.positions.reserve(vertices.size());
        mesh.normals.reserve(vertices.size());
        mesh.texCoords.reserve(vertices.size());
        for (const auto& v : vertices) {
            mesh.positions.push_back(toGltf(v.pos));
            mesh.normals.push_back(toGltf(v.normal));
            mesh.texCoords.push_back({v.texCoords[0].x, v.texCoords[0].y});
        }
        mesh.indices = triangleIndices;

        auto glb = gltf::writeGlb(mesh);

        std::ofstream out(outputPath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("couldn't open '" + outputPath + "' for writing");
        }
        out.write(reinterpret_cast<const char*>(glb.data()),
                  static_cast<std::streamsize>(glb.size()));
        if (!out) {
            throw std::runtime_error("error writing '" + outputPath + "'");
        }

        std::cout << outputPath << ": " << vertices.size() << " vertices, "
                  << (triangleIndices.size() / 3) << " triangles\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: export failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
