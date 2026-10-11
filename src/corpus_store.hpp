#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// The canonical export store (REFACTOR/CORPUS_EXPORT.md). Owns every path
// convention under <store>/, so commands never spell one out:
//
//   objects/ab/cd/<md5>      one output file's bytes; md5(bytes) == name, always
//   builds/<game>/<husk>/    files.tsv, missing.tsv, meta.json
//   tags/<name>              one line: <game>/<husk>
//   tmp/                     staging for object writes; safe to empty when idle
//
// Layout and invariants follow casc-tool's build store on purpose; the code
// is husk's own so neither tool needs the other.
namespace husk::corpus {

using Md5 = std::array<uint8_t, 16>;

std::string hex(const Md5& key);
std::optional<Md5> parseHex(const std::string& text);
Md5 md5File(const std::filesystem::path& path);

// A stored build: which game build it was exported from, by which husk.
struct BuildId {
    std::string game;  // e.g. 12.1.0.69933
    std::string husk;  // the producer's version, e.g. e587735
    std::string path() const { return game + "/" + husk; }
};

// A label is one path component: no separators, no dot-only names.
bool validLabel(const std::string& label);
// "<game>/<husk>", as a tag file holds it.
std::optional<BuildId> parseBuildPath(const std::string& text);

// Writes via a sibling + rename, so a mount never sees a half-written file.
void writeTextAtomic(const std::filesystem::path& path, const std::string& text);

class Store {
public:
    explicit Store(std::filesystem::path root) : root_(std::move(root)) {}

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path objectPath(const Md5& key) const;
    std::filesystem::path buildDir(const BuildId& id) const { return root_ / "builds" / id.game / id.husk; }

    struct Put {
        Md5 key;
        bool added;  // false when the object was already stored
    };
    // Stores `source`'s bytes under their md5. The copy is staged in tmp/,
    // re-hashed, and renamed into place only on a match, so no object can
    // hold bytes other than its name says.
    Put putFile(const std::filesystem::path& source) const;

    // Complete builds only (files.tsv present), and the games having one.
    std::vector<std::string> games() const;
    std::vector<std::string> huskBuilds(const std::string& game) const;
    // The game's build stored last (meta.json's stored_at); nullopt if none.
    std::optional<BuildId> newest(const std::string& game) const;
    std::map<std::string, BuildId> tags() const;
    void setTag(const std::string& name, const BuildId& id) const;

private:
    std::filesystem::path root_;
};

struct MissingRow {
    std::string phase;
    std::string item;
    std::string reason;
};

struct IngestResult {
    BuildId id;
    size_t files = 0;
    uint64_t bytes = 0;
    size_t addedObjects = 0;
    size_t missing = 0;
    bool alreadyStored = false;  // the same build was ingested before, with the same files
};

// Records `tree` (an export's output directory) as the build
// <game>/<producer version>. The producer and schema version come from the
// tree's own documents, which must agree, and a dirty or unknown producer
// is refused: a label has to name the code that wrote it. The tree's
// errors.log becomes missing.tsv instead of a stored file. A build that
// exists with other files is refused; with the same files it is a no-op.
IngestResult ingestTree(const Store& store, const std::filesystem::path& tree, const std::string& game);

}  // namespace husk::corpus
