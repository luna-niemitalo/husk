#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "blp.hpp"
#include "chrmodel_db2.hpp"
#include "cli_args.hpp"
#include "commands.hpp"
#include "garment_footprint.hpp"
#include "m2_model.hpp"
#include "read_file.hpp"
#include "skin.hpp"
#include "skin_resolution.hpp"

// `husk uv-footprint` -- the debug view of garment_footprint.hpp: which
// composite-texture pixels each of a body model's geosets samples, against
// the layout's CharComponentTextureSections rectangles. Evidence for
// REFACTOR/GARMENTS.md stage 2 (WIKI_FINDINGS/GARMENTS.md).
namespace husk::commands {

namespace {

struct Footprint {
    uint16_t geosetId = 0;
    size_t triangles = 0;
    garment::Mask mask;
};

// Model-space bounds of a geoset's triangle corners: where on the body the
// mesh sits, the structural check of what a geoset group is.
std::string positionBounds(const std::vector<m2::Vertex>& vertices, const std::vector<uint32_t>& triangleIndices,
                           const std::vector<skin::Submesh>& submeshes, uint16_t geosetId) {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const skin::Submesh& s : submeshes) {
        if (s.skinSectionId != geosetId) continue;
        for (uint32_t i = s.indexStart; i < s.indexStart + s.indexCount; ++i) {
            const m2::Vec3& p = vertices.at(triangleIndices.at(i)).pos;
            const float c[3] = {p.x, p.y, p.z};
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], c[k]);
                hi[k] = std::max(hi[k], c[k]);
            }
        }
    }
    if (lo[0] > hi[0]) return "none";
    std::ostringstream out;
    out.precision(3);
    out << std::fixed << lo[0] << "," << lo[1] << "," << lo[2] << ".." << hi[0] << "," << hi[1] << "," << hi[2];
    return out.str();
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path.string() + "'");
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void writeMaskPng(const std::filesystem::path& path, const garment::Mask& mask) {
    blp::Image img{mask.width, mask.height, std::vector<uint8_t>(mask.covered.size() * 4, 0)};
    for (size_t i = 0; i < mask.covered.size(); ++i) {
        if (!mask.covered[i]) continue;
        std::fill_n(img.rgba.begin() + std::ptrdiff_t(i * 4), 4, uint8_t{255});
    }
    writeBytes(path, blp::encodePng(img));
}

// Hem/skirt geosets (group 13) red, thigh and knee geosets (groups 9, 11)
// green, everything else blue, added together so an overlap shows as a
// mixed colour; section rectangles outlined in white.
void writeOverviewPng(const std::filesystem::path& path, const std::vector<Footprint>& footprints,
                      const std::vector<chrmodel::CharComponentTextureSection>& sections, uint32_t width,
                      uint32_t height) {
    blp::Image img{width, height, std::vector<uint8_t>(size_t(width) * height * 4, 0)};
    for (size_t i = 0; i < size_t(width) * height; ++i) img.rgba[i * 4 + 3] = 255;
    for (const Footprint& f : footprints) {
        const unsigned group = f.geosetId / 100;
        const int channel = group == 13 ? 0 : (group == 9 || group == 11) ? 1 : 2;
        for (size_t i = 0; i < f.mask.covered.size(); ++i) {
            if (!f.mask.covered[i]) continue;
            uint8_t& c = img.rgba[i * 4 + size_t(channel)];
            c = uint8_t(std::min(255, c + 160));
        }
    }
    auto plot = [&](uint32_t x, uint32_t y) {
        if (x >= width || y >= height) return;
        std::fill_n(img.rgba.begin() + std::ptrdiff_t((size_t(y) * width + x) * 4), 4, uint8_t{255});
    };
    for (const auto& s : sections) {
        for (uint32_t x = s.x; x < s.x + s.width; ++x) {
            plot(x, s.y);
            plot(x, s.y + s.height - 1);
        }
        for (uint32_t y = s.y; y < s.y + s.height; ++y) {
            plot(s.x, y);
            plot(s.x + s.width - 1, y);
        }
    }
    writeBytes(path, blp::encodePng(img));
}

std::string rectText(const garment::PixelRect& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + "," +
           std::to_string(r.height);
}

}  // namespace

void addUvFootprintOptions(CLI::App& app, UvFootprintOptions& opts) {
    addConfigOption(app);
    app.add_option("model", opts.model, "the body .m2 (its lod0 .skin is found as `export --skin auto` finds it)")
        ->required();
    app.add_option("--layout-id", opts.layoutId,
                   "CharComponentTextureLayouts.ID of the body's composite texture: its size is the pixel grid, "
                   "its CharComponentTextureSections rows the regions reported")
        ->required();
    app.add_option("--geoset", opts.geosets, "a geoset id (skinSectionId) to report; repeatable");
    app.add_option("--group", opts.groups, "every geoset of this group (id / 100) to report; repeatable");
    app.add_option("--db2-dir", opts.db2Dir, "local DB2 directory (CharComponentTextureLayouts/Sections)");
    app.add_option("--dbd-dir", opts.dbdDir, "local WoWDBDefs checkout for --db2-dir's column names");
    app.add_option("--listfile-root", opts.listfileRoot, "corpus root whose _by_fdid/ holds the SFID .skin");
    app.add_option("--png-dir", opts.pngDir,
                   "write <model>_<geoset>.png masks and <model>_overview.png (sections outlined) here");
    app.add_option("--texture", opts.texture,
                   "a section texture (.blp), e.g. a legs item's LegUpper texture: counts its non-transparent "
                   "pixels inside each reported geoset's footprint");
    app.add_option("--texture-section", opts.textureSection,
                   "the SectionType --texture is painted into (its rectangle maps the texture onto the composite)");
}

int uvFootprint(int argc, char** args) {
    UvFootprintOptions opts;
    CLI::App app{"Rasterizes a body model's geosets in UV space onto its composite skin texture and reports "
                 "each footprint's bounds, pixel count, texture sections and pairwise overlaps.",
                 "husk uv-footprint"};
    addUvFootprintOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;

    try {
        std::optional<chrmodel::Data> db = chrmodel::load(opts.db2Dir, opts.dbdDir, std::cerr);
        if (!db) throw std::runtime_error("no CharComponentTexture tables under --db2-dir '" + opts.db2Dir + "'");
        auto layout = std::find_if(db->layouts.begin(), db->layouts.end(),
                                   [&](const auto& l) { return l.id == opts.layoutId; });
        if (layout == db->layouts.end()) {
            throw std::runtime_error("CharComponentTextureLayouts has no ID " + std::to_string(opts.layoutId));
        }
        std::vector<chrmodel::CharComponentTextureSection> sections;
        for (const auto& s : db->sections) {
            if (s.charComponentTextureLayoutId == opts.layoutId) sections.push_back(s);
        }
        std::sort(sections.begin(), sections.end(), [](const auto& a, const auto& b) { return a.sectionType < b.sectionType; });

        const m2::Model model = m2::loadModel(readFileBytes(opts.model));
        auto skins = resolveSkin(model.header, opts.model, "", opts.listfileRoot, false);
        if (skins.empty()) throw std::runtime_error("no .skin found for '" + opts.model + "'");
        const std::vector<uint8_t> skinBytes = readFileBytes(skins.front().second);
        const skin::Header skinHeader = skin::parseHeader(skinBytes);
        const std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinBytes, skinHeader);
        const std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinBytes, skinHeader.submeshes);

        std::vector<uint16_t> ids(opts.geosets.begin(), opts.geosets.end());
        for (const skin::Submesh& s : submeshes) {
            const bool inGroup = std::find(opts.groups.begin(), opts.groups.end(), s.skinSectionId / 100u) !=
                                 opts.groups.end();
            if (inGroup) ids.push_back(s.skinSectionId);
        }
        if (opts.geosets.empty() && opts.groups.empty()) {
            for (const skin::Submesh& s : submeshes) ids.push_back(s.skinSectionId);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

        std::cout << "layout " << layout->id << " " << layout->width << "x" << layout->height << "\n";
        std::cout << "skin " << skins.front().second << "\n";
        for (const auto& s : sections) {
            std::cout << "section " << s.sectionType << " " << s.x << "," << s.y << "," << s.width << ","
                      << s.height << "\n";
        }

        std::vector<Footprint> footprints;
        for (uint16_t id : ids) {
            auto tris = garment::geosetTriangles(model.vertices, triangleIndices, submeshes, id);
            footprints.push_back({id, tris.size(), garment::rasterize(tris, layout->width, layout->height)});
        }
        for (const Footprint& f : footprints) {
            auto box = garment::bounds(f.mask);
            std::cout << "geoset " << f.geosetId << " triangles=" << f.triangles << " pixels=" << f.mask.count()
                      << " bbox=" << (box ? rectText(*box) : "none")
                      << " pos=" << positionBounds(model.vertices, triangleIndices, submeshes, f.geosetId)
                      << " sections=";
            bool first = true;
            for (const auto& s : sections) {
                size_t n = garment::countInside(f.mask, {s.x, s.y, s.width, s.height});
                if (n == 0) continue;
                std::cout << (first ? "" : ";") << s.sectionType << ":" << n;
                first = false;
            }
            std::cout << "\n";
        }
        for (size_t i = 0; i < footprints.size(); ++i) {
            for (size_t j = i + 1; j < footprints.size(); ++j) {
                size_t n = garment::overlapCount(footprints[i].mask, footprints[j].mask);
                if (n == 0) continue;
                std::cout << "overlap " << footprints[i].geosetId << " " << footprints[j].geosetId << " " << n << "\n";
            }
        }

        if (!opts.texture.empty()) {
            auto section = std::find_if(sections.begin(), sections.end(),
                                        [&](const auto& s) { return s.sectionType == opts.textureSection; });
            if (section == sections.end()) {
                throw std::runtime_error("layout " + std::to_string(opts.layoutId) + " has no SectionType " +
                                         std::to_string(opts.textureSection));
            }
            const blp::Image tex = blp::decode(readFileBytes(opts.texture));
            for (const Footprint& f : footprints) {
                size_t inside = 0, opaque = 0;
                for (uint32_t y = section->y; y < section->y + section->height && y < f.mask.height; ++y) {
                    for (uint32_t x = section->x; x < section->x + section->width && x < f.mask.width; ++x) {
                        if (!f.mask.at(x, y)) continue;
                        ++inside;
                        const uint32_t tx = (x - section->x) * tex.width / section->width;
                        const uint32_t ty = (y - section->y) * tex.height / section->height;
                        opaque += tex.rgba[(size_t(ty) * tex.width + tx) * 4 + 3] > 0 ? 1 : 0;
                    }
                }
                std::cout << "texture geoset=" << f.geosetId << " footprint_in_section=" << inside
                          << " opaque=" << opaque << "\n";
            }
        }

        if (!opts.pngDir.empty()) {
            const std::filesystem::path dir = opts.pngDir;
            std::filesystem::create_directories(dir);
            const std::string stem = std::filesystem::path(opts.model).stem().string();
            for (const Footprint& f : footprints) {
                writeMaskPng(dir / (stem + "_" + std::to_string(f.geosetId) + ".png"), f.mask);
            }
            writeOverviewPng(dir / (stem + "_overview.png"), footprints, sections, layout->width, layout->height);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: uv-footprint: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
