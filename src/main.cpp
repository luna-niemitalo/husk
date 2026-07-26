#include <cstring>
#include <iostream>

#include "commands.hpp"

int main(int argc, char** argv) {
    static const char* usage =
        "usage: husk <command> [args...]\n"
        "\n"
        "commands:\n"
        "  info <file.m2>              parse and print an M2 header\n"
        "  export <file.m2> [args...]  export a mesh (+ skin/animation) to glTF (see --help)\n"
        "  dump-chunks <file.m2|.bone> extract misc chunks to JSON (see --help)\n"
        "  --version, -V                print the build version and exit\n"
        "\n"
        "run `husk <command> --help` for a command's full usage and defaults.\n";

    if (argc < 2) {
        std::cerr << usage;
        return 1;
    }

    std::string command = argv[1];
    char** rest = argv + 2;
    int restArgc = argc - 2;

    if (command == "info") {
        return husk::commands::info(restArgc, rest);
    }
    if (command == "export") {
        return husk::commands::exportGlb(restArgc, rest);
    }
    if (command == "dump-chunks") {
        return husk::commands::dumpChunks(restArgc, rest);
    }
    if (command == "--help" || command == "-h") {
        std::cout << usage;
        return 0;
    }
    // A durable fact, not a per-invocation flag choice (~/docs/CLI.md
    // §2.5) -- HUSK_VERSION is baked in at CMake configure time
    // (CMakeLists.txt), never recomputed here.
    if (command == "--version" || command == "-V") {
        std::cout << "husk " << HUSK_VERSION << "\n";
        return 0;
    }

    std::cerr << "husk: unknown command '" << command << "'\n";
    return 1;
}
