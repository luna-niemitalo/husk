#include <cstring>
#include <iostream>

#include "commands.hpp"

int main(int argc, char** argv) {
    static const char* usage =
        "usage: husk <command> [args...]\n"
        "\n"
        "commands:\n"
        "  info <file.m2>                            parse and print an M2 header\n"
        "  export <file.m2> <file.skin> <out.glb>     export a mesh (+ skin/animation) to glTF\n"
        "  dump-chunks <file.m2>                      extract misc chunks to JSON (see --help)\n";

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

    std::cerr << "husk: unknown command '" << command << "'\n";
    return 1;
}
