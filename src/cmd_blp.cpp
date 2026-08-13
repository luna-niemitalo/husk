#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "blp.hpp"
#include "commands.hpp"

// `husk blp-export` -- native BLP2 -> PNG conversion, reusing the exact
// `blp::decode`/`blp::encodePng` pipeline `husk export` already uses
// internally to embed real textures into every `.glb` (export_materials.cpp).
// TODO/TEXTURE_TOOLING_TODO.md's own motivating gap: the separate Python
// `blp/`/`husk-blp` tool's CLI is strictly one-file-in-one-file-out, no
// directory/batch mode, no flag conventions matching the rest of this
// project's CLI -- there was no new *decode* work needed here, only a thin
// wrapper exposing already-corpus-verified logic the same way `db2-export`
// wraps db2.hpp/dbd.hpp instead of shelling out to a separate tool.
namespace husk::commands {

namespace {

void printUsage(std::ostream& out = std::cerr) {
    out << "usage: husk blp-export <file.blp> <out.png>\n"
           "       husk blp-export --dir <blp-dir> <out-dir>\n"
           "\n"
           "Converts one BLP2 texture, or (with --dir) every *.blp file in a\n"
           "directory, to a real PNG -- mip level 0 (full resolution) only.\n"
           "In --dir mode, output filenames mirror each input's own basename\n"
           "(<name>.blp -> <name>.png in out-dir); a file that fails to parse\n"
           "is skipped with a diagnostic, not treated as a fatal error for the\n"
           "whole batch.\n";
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw blp::ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw blp::ParseError("error reading '" + path + "': " + std::strerror(errno));
    }
    return bytes;
}

void writeFileBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    errno = 0;
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw blp::ParseError("couldn't open '" + path + "' for writing: " + std::strerror(errno));
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f.good()) {
        throw blp::ParseError("error writing '" + path + "': " + std::strerror(errno));
    }
}

// Converts one file, printing husk's own "<out>: WxH" note on success.
// Returns false (already diagnosed to stderr, never throws) on failure --
// the shared shape --dir mode needs to skip-and-continue on a bad file.
bool convertOne(const std::string& inputPath, const std::string& outputPath) {
    try {
        auto raw = readFileBytes(inputPath);
        auto image = blp::decode(raw);
        auto png = blp::encodePng(image);
        writeFileBytes(outputPath, png);
        std::cout << "husk: blp-export: " << outputPath << ": " << image.width << "x" << image.height << "\n";
        return true;
    } catch (const blp::ParseError& e) {
        std::cerr << "husk: blp-export: '" << inputPath << "': " << e.what() << "\n";
        return false;
    }
}

}  // namespace

int blpExport(int argc, char** args) {
    if (argc >= 1 && isHelpFlag(args[0])) {
        printUsage(std::cout);
        return 0;
    }

    bool dirMode = argc >= 1 && std::string(args[0]) == "--dir";
    if (dirMode) {
        if (argc != 3) {
            printUsage();
            return 1;
        }
        std::filesystem::path dirPath = args[1];
        std::filesystem::path outDir = args[2];

        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            if (entry.path().extension() == ".blp") paths.push_back(entry.path());
        }
        std::sort(paths.begin(), paths.end());
        if (paths.empty()) {
            std::cerr << "husk: blp-export: no .blp files found under '" << dirPath.string() << "'\n";
            return 1;
        }

        std::filesystem::create_directories(outDir);
        size_t converted = 0, failed = 0;
        for (const auto& p : paths) {
            std::filesystem::path out = outDir / (p.stem().string() + ".png");
            if (convertOne(p.string(), out.string())) {
                ++converted;
            } else {
                ++failed;
            }
        }
        std::cout << "husk: blp-export: " << converted << "/" << paths.size() << " file(s) converted";
        if (failed > 0) std::cout << ", " << failed << " failed (see above)";
        std::cout << "\n";
        return failed > 0 && converted == 0 ? 1 : 0;
    }

    if (argc != 2) {
        printUsage();
        return 1;
    }
    return convertOne(args[0], args[1]) ? 0 : 1;
}

}  // namespace husk::commands
