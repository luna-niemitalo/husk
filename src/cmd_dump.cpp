#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>

#include <CLI/CLI.hpp>

#include "bone.hpp"
#include "chunk.hpp"
#include "commands.hpp"
#include "dump_chunks_misc.hpp"
#include "dump_emitters.hpp"
#include "dump_phys.hpp"
#include "json_writer.hpp"
#include "m2.hpp"
#include "phys.hpp"

// `husk dump-chunks`: extracts M2 data that doesn't feed into `husk
// export`'s .glb output into readable JSON on stdout instead of leaving it
// silently unread. Two categories, both real:
//
// - Legion+-only chunks (TXAC/EXPT/PABC/PADC/PSBC/PEDC/RPID/GPID/PGD1/
//   WFV3/NERF/EDGF/DBOC/TEXL/PFDC/EXP2/PCOL, all reasonably well documented
//   on wowdev.wiki) -- rendering-effect/gameplay-metadata concerns glTF's
//   own material model has no real equivalent for (edge fade, PBR-ish
//   waterfall shading, parent-model animation overrides, inline physics,
//   extended-particle alpha-cutoff curves, ...). DETL (per-light shadow-RT
//   scale/diffuse-multiplier data) is likewise fully parsed but handled
//   separately from the lookup table below, since its own defensive
//   record-count floor needs the header's own lights.count -- the same
//   reason ribbon_emitters/particle_emitters aren't chunk-tag-driven
//   either (see dumpDetl's doc comment). WFV1/WFV2/DPIV/AFRA have no
//   wowdev.wiki struct at all; their layout was resolved from real bytes
//   instead (WFV1/AFRA: generically-named float32 fields; DPIV: a real
//   record array, chunk.size/32 records; WFV2: a flat 16x float32 array).
//   Only present in Legion+ chunked files.
// - `ribbon_emitters`/`particle_emitters` -- core MD20 header arrays
//   present in every version, not Legion+ chunks, but the same "no glTF
//   slot" rationale applies: procedural emitter systems, not renderable
//   geometry (see gltf.hpp's EmitterAnchor doc comment). `husk export`
//   still attaches a minimal position/bone anchor to the .glb itself (see
//   gltf::Skeleton::RibbonAnchor/ParticleAnchor), so a consumer that only
//   needs placement doesn't need this command at all; this is the home for
//   every other field and fully-resolved animation curve.
//
// A `.phys` file, passed directly (like `.bone`), also gets its full
// body/shape/joint/PHYV record set dumped here -- same ".glb gets a
// minimal placement anchor, this command gets everything else" split as
// ribbon/particle emitters above (see gltf::Skeleton::PhysicsBody's doc
// comment).
//
// This is deliberately a *separate* intermediary format, not a step toward
// richer glTF export.
//
// Per FILE_SPLIT_TODO.md's Item 4, this file is now the index: the actual
// per-chunk dumpers live in dump_chunks_misc.cpp (~30 small Legion+-tag
// dumpers), dump_emitters.cpp (ribbon_emitters/particle_emitters), and
// dump_phys.cpp (.phys's own body/shape/joint/PHYV set) -- all pure moves,
// no behavior change. Only the CLI entry point and the chunk-tag dispatch
// table stay here.
// TODO: Remove: WFV1/WFV2/DPIV/AFRA parsing was former
// RO_COMPLETENESS_TODO.md Item 3, see WIKI_FINDINGS.md. Ribbon/particle
// scope was broadened from chunk-tags-only once real weapon fixtures made
// full parsing verifiable, see WIKI_FINDINGS.md.
namespace husk::commands {

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw std::runtime_error("error reading '" + path + "': " + std::strerror(errno));
    }
    return bytes;
}

}  // namespace

void addDumpChunksOptions(CLI::App& app, DumpChunksOptions& opts) {
    app.add_option("model", opts.model, "the .m2, .bone, or .phys file to dump")->required();
}

int dumpChunks(int argc, char** args) {
    DumpChunksOptions opts;
    CLI::App app{
        "Extracts M2 data husk doesn't fold into `export`'s glTF output into readable JSON on "
        "stdout: ribbon_emitters/particle_emitters (every field and fully resolved animation "
        "curve, present in every M2 version, particle_emitters count-only below Cataclysm); "
        "Legion+ chunk tags (TXAC/EXPT/PABC/PADC/PSBC/PEDC/RPID/GPID/PGD1/WFV3/NERF/EDGF/DBOC/"
        "TEXL/PFDC/EXP2/DETL/PCOL, all documented on wowdev.wiki, plus WFV1/WFV2/DPIV/AFRA which "
        "aren't). A .bone file (M2/.skel's BFID chunk) is also accepted -- dumps its per-bone "
        "correction matrices (src/bone.hpp). A .phys file (M2's PFID chunk) is also accepted -- "
        "dumps every body/shape/joint/PHYV record (src/phys.hpp); `husk export --phys` attaches "
        "only a minimal per-body placement anchor to the .glb itself, this is the home for "
        "everything else.",
        "husk dump-chunks"};
    addDumpChunksOptions(app, opts);

    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    const std::string& path = opts.model;

    try {
        auto fileBytes = readFileBytes(path);

        // .phys files start with a PHYS chunk whose tag is byte-reversed on
        // disk ("SYHP" -- see phys.hpp's doc comment), unambiguous against
        // both M2's own MD20/MD21 magic and .bone's leading version field --
        // sniffed first, most specific check first.
        bool looksLikePhys =
            fileBytes.size() >= 4 && std::memcmp(fileBytes.data(), "SYHP", 4) == 0;
        if (looksLikePhys) {
            auto physFile = phys::parse(fileBytes);
            json::Writer w(std::cout);
            writePhysFile(w, physFile);
            std::cout << "\n";
            return 0;
        }

        // .bone files have no MD20/MD21 magic of their own (see bone.hpp) --
        // sniff for that first so a .bone path doesn't hit m2::parseHeader's
        // "bad magic" error instead of actually being read.
        bool looksLikeM2 = fileBytes.size() >= 4 &&
                            (std::memcmp(fileBytes.data(), "MD20", 4) == 0 ||
                             std::memcmp(fileBytes.data(), "MD21", 4) == 0);
        if (!looksLikeM2) {
            auto corrections = bone::parse(fileBytes);
            json::Writer w(std::cout);
            w.beginArray();
            for (const auto& c : corrections) {
                w.beginObject();
                w.key("bone_index");
                w.value(static_cast<int64_t>(c.boneIndex));
                w.key("matrix_row_major_inferred");
                w.beginArray();
                for (float f : c.matrix) w.value(static_cast<double>(f));
                w.endArray();
                w.endObject();
            }
            w.endArray();
            std::cout << "\n";
            return 0;
        }

        auto header = m2::parseHeader(fileBytes);
        auto blob = m2::extractBlob(fileBytes);

        json::Writer w(std::cout);
        w.beginObject();

        // ribbon_emitters/particle_emitters are core MD20 header arrays,
        // present in every version -- written unconditionally, unlike the
        // Legion+-only chunk tags below (see dumpEmitters's doc comment).
        dumpEmitters(w, blob, header);

        if (!header.chunked) {
            std::cerr << "husk: '" << path
                      << "' is a pre-Legion flat MD20 file -- the Legion+-only chunks below "
                         "(TXAC/EXPT/.../TEXL) don't exist outside the chunked container, so only "
                         "ribbon_emitters/particle_emitters are populated above\n";
            w.endObject();
            std::cout << "\n";
            return 0;
        }
        auto chunks = readChunks(fileBytes.data(), fileBytes.size());

        struct Entry {
            const char* tag;
            void (*dump)(json::Writer&, const Chunk&);
        };
        static const Entry kDocumented[] = {
            {"TXAC", dumpTxac},          {"EXPT", dumpExpt},   {"PABC", dumpU16ArrayChunk},
            {"PADC", dumpPadc},          {"PSBC", dumpPsbc},   {"PEDC", dumpPedc},
            {"RPID", dumpFileDataIdArrayChunk}, {"GPID", dumpFileDataIdArrayChunk},
            {"PGD1", dumpU16ArrayChunk}, {"WFV3", dumpWfv3},   {"NERF", dumpNerf},
            {"EDGF", dumpEdgf},          {"DBOC", dumpDboc},   {"TEXL", dumpTexl},
            {"PFDC", dumpPfdc},          {"EXP2", dumpExp2},   {"PCOL", dumpPcol},
            {"WFV1", dumpWfv1},          {"WFV2", dumpWfv2},   {"DPIV", dumpDpiv},
            {"AFRA", dumpAfra},
        };
        for (const auto& e : kDocumented) {
            auto c = findChunk(chunks, e.tag);
            if (!c) continue;
            w.key(e.tag);
            e.dump(w, *c);
        }

        // DETL doesn't fit kDocumented's fixed 2-arg dump-function shape --
        // its own defensive record-count floor needs the header's
        // lights.count (see dumpDetl's doc comment), the same reason
        // ribbon_emitters/particle_emitters above are handled outside this
        // table too, rather than a real structural difference from the
        // "documented" chunks in that list.
        if (auto c = findChunk(chunks, "DETL")) {
            w.key("DETL");
            dumpDetl(w, *c, header.lights.count);
        }

        w.endObject();
        std::cout << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: dump-chunks failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
