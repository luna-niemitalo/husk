#include <cstring>
#include <iostream>

#include "commands.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: husk <command> [args...]\n"
                     "\n"
                     "commands:\n"
                     "  info <file.m2>   parse and print an M2 header\n";
        return 1;
    }

    std::string command = argv[1];
    char** rest = argv + 2;
    int restArgc = argc - 2;

    if (command == "info") {
        return husk::commands::info(restArgc, rest);
    }
    if (command == "--help" || command == "-h") {
        std::cout << "usage: husk <command> [args...]\n"
                     "\n"
                     "commands:\n"
                     "  info <file.m2>   parse and print an M2 header\n";
        return 0;
    }

    std::cerr << "husk: unknown command '" << command << "'\n";
    return 1;
}
