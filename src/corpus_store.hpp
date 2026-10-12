#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The canonical export store (REFACTOR/CORPUS_EXPORT.md "The store"):
// owns every path convention under <store>/, so commands never spell one
// out. Layout and invariants follow casc-tool's build store; the code is
// husk's own so neither tool needs the other.
namespace husk::corpus {

using Md5 = std::array<uint8_t, 16>;

std::string hex(const Md5& key);
std::optional<Md5> parseHex(const std::string& text);
Md5 md5File(const std::filesystem::path& path);
Md5 md5Bytes(std::span<const uint8_t> bytes);

// A stored build: which game build it was exported from, by which husk.
struct BuildId {
    std::string game;  // e.g. 12.1.0.69933
    std::string husk;  // the producer's version, e.g. e587735
    std::string path() const { return game + "/" + husk; }
};

// A label is one path component: no separators, no dot-only names.
bool validLabel(const std::string& label);
// The <husk> label of a build its documents' producer ("husk <version>")
// wrote. Throws for a dirty or unknown version: a label has to name the code.
std::string huskLabel(const std::string& producer);

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

    // Complete builds only (files.tsv present), and the games having one
    // or an in-progress run.
    std::vector<std::string> games() const;
    std::vector<std::string> huskBuilds(const std::string& game) const;
    // The game's build stored last (meta.json's stored_at); nullopt if none.
    std::optional<BuildId> newest(const std::string& game) const;
    std::map<std::string, BuildId> tags() const;
    void setTag(const std::string& name, const BuildId& id) const;

    // An export still writing its tree: runs/<game>/<husk> names the tree
    // until the build is stored, so the mount can show it as it grows.
    void beginRun(const BuildId& id, const std::filesystem::path& tree) const;
    void endRun(const BuildId& id) const;
    // In-progress runs of `game` with no stored build of the same label yet,
    // and the tree each is writing.
    std::map<std::string, std::filesystem::path> runs(const std::string& game) const;

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
    size_t inputs = 0;  // inputs.tsv rows
    bool alreadyStored = false;  // the same build was ingested before, with the same files
};

// Records `tree` (an export's output directory) as the build
// <game>/<producer version>. The producer and schema version come from the
// tree's own documents, which must agree, and a dirty or unknown producer
// is refused: a label has to name the code that wrote it. The tree's
// errors.log and inputs.log become missing.tsv and inputs.tsv instead of
// stored files. Each stored file is journaled to the tree's
// store-journal.tsv as it lands, so a rerun after a crash resumes instead
// of rehashing. A build that exists with other files is refused; with the
// same files it is a no-op.
IngestResult ingestTree(const Store& store, const std::filesystem::path& tree, const std::string& game);

}  // namespace husk::corpus
