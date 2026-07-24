#include <iostream>

#include "commands.hpp"
#include "m2.hpp"

namespace husk::commands {

namespace {

void printUsage() {
    std::cerr << "usage: husk info <file.m2>\n"
                 "\n"
                 "Parses an M2 model's header and prints what was found:\n"
                 "magic/version/name, whether it's Legion+ chunked, and the\n"
                 "record counts (bones, vertices, textures, ...) from the\n"
                 "header's M2Array fields.\n";
}

void printArray(const char* label, const m2::Array& a) {
    std::cout << "  " << label << ": " << a.count << " (offset 0x" << std::hex << a.offset
               << std::dec << ")\n";
}

void printVec3(const m2::Vec3& v) {
    std::cout << "(" << v.x << ", " << v.y << ", " << v.z << ")";
}

}  // namespace

int info(int argc, char** args) {
    if (argc != 1) {
        printUsage();
        return 1;
    }

    std::string path = args[0];
    m2::Header h;
    try {
        h = m2::loadFile(path);
    } catch (const m2::ParseError& e) {
        std::cerr << "husk: couldn't read '" << path << "': " << e.what() << "\n";
        return 1;
    }

    std::cout << path << "\n";
    std::cout << "  format: " << (h.chunked ? "Legion+ chunked (MD21-wrapped)" : "pre-Legion (flat MD20)")
               << "\n";
    std::cout << "  version: " << h.version << " (" << m2::expansionForVersion(h.version) << ")\n";
    std::cout << "  name: " << (h.name.empty() ? "(empty)" : h.name) << "\n";
    std::cout << "  global_flags: 0x" << std::hex << h.globalFlags << std::dec << "\n";

    printArray("sequences", h.sequences);
    printArray("bones", h.bones);
    printArray("vertices", h.vertices);
    printArray("textures", h.textures);
    printArray("materials", h.materials);
    std::cout << "  num_skin_profiles: " << h.numSkinProfiles << "\n";

    std::cout << "  bounding_box: min=";
    printVec3(h.boundingBox.min);
    std::cout << " max=";
    printVec3(h.boundingBox.max);
    std::cout << "\n";
    std::cout << "  bounding_sphere_radius: " << h.boundingSphereRadius << "\n";

    return 0;
}

}  // namespace husk::commands
