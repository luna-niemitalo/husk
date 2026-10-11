#define FUSE_USE_VERSION 31

#include <fuse.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <CLI/CLI.hpp>

#include "commands.hpp"
#include "corpus_store.hpp"

namespace fs = std::filesystem;

namespace husk::commands {

namespace {

// Reversed for the same reason as resolve()'s identical call (cmd_resolve.cpp).
bool parseArgs(CLI::App& app, int argc, char** args, int& exitCode) {
    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        exitCode = app.exit(e);
        return false;
    }
    return true;
}

// The game build a source tree is: casc-tool's mount serves `live` as a
// link to builds/<label>.
std::string gameLabelOf(const fs::path& sourceTree) {
    std::error_code ec;
    fs::path target = fs::read_symlink(sourceTree, ec);
    if (ec)
        throw std::runtime_error("--game-from " + sourceTree.string() +
                                 ": expected a link to builds/<label> (a casc-tool mount's tag), got " +
                                 ec.message());
    std::string label = target.filename().string();
    if (target.parent_path().filename() != "builds" || !corpus::validLabel(label))
        throw std::runtime_error("--game-from " + sourceTree.string() + ": expected a link to builds/<label>, got " +
                                 target.string());
    return label;
}

// --- mount ----------------------------------------------------------------

// One build's tree, loaded from its files.tsv on first use. Builds never
// change once listed (files.tsv is written last), so it is kept for good.
struct Build {
    struct Entry {
        bool isDir = false;
        uint32_t index = 0;  // into dirs, or files
    };
    struct Dir {
        std::map<std::string, Entry> entries;
    };
    std::vector<Dir> dirs{Dir{}};
    std::vector<corpus::Md5> files;
    std::string error;  // why files.tsv was refused; the build then shows only .error
};

void addPath(Build& b, const std::string& path, const corpus::Md5& key, size_t row) {
    std::vector<std::string> parts;
    std::stringstream in(path);
    for (std::string part; std::getline(in, part, '/');) parts.push_back(part);
    uint32_t dir = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        if (!corpus::validLabel(parts[i])) throw std::runtime_error("row " + std::to_string(row) + ": bad path " + path);
        bool leaf = i + 1 == parts.size();
        auto [it, inserted] = b.dirs[dir].entries.try_emplace(parts[i]);
        if (!inserted && it->second.isDir == leaf)
            throw std::runtime_error("row " + std::to_string(row) + ": " + path + " is both a file and a directory");
        if (inserted && leaf) {
            it->second = {false, static_cast<uint32_t>(b.files.size())};
            b.files.push_back(key);
        } else if (inserted) {
            it->second = {true, static_cast<uint32_t>(b.dirs.size())};
            b.dirs.emplace_back();
        } else if (leaf) {
            throw std::runtime_error("row " + std::to_string(row) + ": " + path + " listed twice");
        }
        dir = it->second.index;
    }
}

std::unique_ptr<Build> loadBuild(const fs::path& filesTsv) {
    auto b = std::make_unique<Build>();
    try {
        std::ifstream in(filesTsv);
        std::string line;
        if (!std::getline(in, line) || line != "path\tmd5")
            throw std::runtime_error("expected the header \"path<TAB>md5\", got \"" + line + "\"");
        for (size_t row = 2; std::getline(in, line); row++) {
            size_t tab = line.find('\t');
            auto key = tab == std::string::npos ? std::nullopt : corpus::parseHex(line.substr(tab + 1));
            if (!key)
                throw std::runtime_error("row " + std::to_string(row) + ": expected \"<path><TAB><md5>\", got \"" + line + "\"");
            addPath(*b, line.substr(0, tab), *key, row);
        }
    } catch (const std::exception& e) {
        *b = Build{};
        b->error = filesTsv.string() + ": " + e.what();
        std::cerr << "husk mount: " << b->error << "\n";
    }
    return b;
}

struct Mount {
    corpus::Store store;
    struct timespec mtime {};
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<Build>> builds;
};

Mount& ctx() { return *static_cast<Mount*>(fuse_get_context()->private_data); }

Build* build(Mount& m, const corpus::BuildId& id) {
    std::lock_guard<std::mutex> lock(m.mutex);
    auto& slot = m.builds[id.path()];
    if (!slot) slot = loadBuild(m.store.buildDir(id) / "files.tsv");
    return slot.get();
}

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

// What a path names (REFACTOR/CORPUS_EXPORT.md "The mount"):
//   /<tag> -> games/<game>/<husk>          /games/<game>/current -> <husk>
//   /games/<game>/<husk>/<tree path>       /games/<game>/<husk>/.build .missing .inputs
struct Node {
    enum Kind { None, Dir, Object, Link, Text, Real } kind = None;
    enum DirKind { Root, Games, Game, Tree } dirKind = Root;
    std::string game;
    corpus::BuildId id;
    Build* build = nullptr;
    uint32_t dir = 0;
    corpus::Md5 key{};
    std::string text;  // Link target or Text content
    fs::path real;
};

Node dirNode(Node::DirKind kind) {
    Node n;
    n.kind = Node::Dir;
    n.dirKind = kind;
    return n;
}

Node textNode(Node::Kind kind, std::string text) {
    Node n;
    n.kind = kind;
    n.text = std::move(text);
    return n;
}

Node realNode(fs::path real) {
    Node n;
    n.kind = Node::Real;
    n.real = std::move(real);
    return n;
}

std::vector<std::string> splitPath(const char* path) {
    std::vector<std::string> out;
    std::stringstream in(path);
    for (std::string part; std::getline(in, part, '/');)
        if (!part.empty()) out.push_back(part);
    return out;
}

// The build's own files beside its tree, shown only where they exist.
const std::vector<std::pair<std::string, std::string>> kBuildRecords = {
    {".build", "meta.json"}, {".missing", "missing.tsv"}, {".inputs", "inputs.tsv"}};

Node resolveInBuild(Mount& m, const corpus::BuildId& id, const std::vector<std::string>& rest) {
    Build* b = build(m, id);
    Node tree = dirNode(Node::Tree);
    tree.id = id;
    tree.build = b;
    if (rest.empty()) return tree;
    if (rest.size() == 1) {
        if (rest[0] == ".error" && !b->error.empty()) return textNode(Node::Text, b->error + "\n");
        for (const auto& [name, file] : kBuildRecords) {
            fs::path p = m.store.buildDir(id) / file;
            if (rest[0] == name && fs::exists(p)) return realNode(p);
        }
    }
    uint32_t dir = 0;
    for (size_t i = 0; i < rest.size(); i++) {
        auto it = b->dirs[dir].entries.find(rest[i]);
        if (it == b->dirs[dir].entries.end()) return {};
        bool leaf = i + 1 == rest.size();
        if (!it->second.isDir) {
            if (!leaf) return {};
            Node n;
            n.kind = Node::Object;
            n.key = b->files[it->second.index];
            return n;
        }
        dir = it->second.index;
    }
    tree.dir = dir;
    return tree;
}

Node resolve(Mount& m, const char* path) {
    auto parts = splitPath(path);
    if (parts.empty()) return dirNode(Node::Root);
    if (parts[0] != "games") {
        auto tags = m.store.tags();
        auto t = tags.find(parts[0]);
        if (parts.size() != 1 || t == tags.end()) return {};
        return textNode(Node::Link, "games/" + t->second.path());
    }
    if (parts.size() == 1) return dirNode(Node::Games);
    const std::string& game = parts[1];
    if (!contains(m.store.games(), game)) return {};
    if (parts.size() == 2) {
        Node n = dirNode(Node::Game);
        n.game = game;
        return n;
    }
    if (parts[2] == "current") {
        auto newest = m.store.newest(game);
        if (parts.size() != 3 || !newest) return {};
        return textNode(Node::Link, newest->husk);
    }
    if (!contains(m.store.huskBuilds(game), parts[2])) return {};
    return resolveInBuild(m, {game, parts[2]}, std::vector<std::string>(parts.begin() + 3, parts.end()));
}

int fsGetattr(const char* path, struct stat* st, struct fuse_file_info*) {
    Mount& m = ctx();
    Node n = resolve(m, path);
    std::memset(st, 0, sizeof(*st));
    st->st_uid = ::getuid();
    st->st_gid = ::getgid();
    st->st_mtim = st->st_ctim = m.mtime;
    switch (n.kind) {
        case Node::None: return -ENOENT;
        case Node::Dir:
            st->st_mode = S_IFDIR | 0555;
            st->st_nlink = 2;
            return 0;
        case Node::Link:
            st->st_mode = S_IFLNK | 0777;
            st->st_nlink = 1;
            st->st_size = off_t(n.text.size());
            return 0;
        case Node::Text:
            st->st_mode = S_IFREG | 0444;
            st->st_nlink = 1;
            st->st_size = off_t(n.text.size());
            return 0;
        case Node::Object:
        case Node::Real: {
            struct stat real;
            fs::path p = n.kind == Node::Object ? m.store.objectPath(n.key) : n.real;
            if (::stat(p.c_str(), &real) != 0) return -ENOENT;
            st->st_mode = S_IFREG | 0444;
            st->st_nlink = 1;
            st->st_size = real.st_size;
            st->st_blocks = real.st_blocks;
            st->st_mtim = real.st_mtim;
            return 0;
        }
    }
    return -ENOENT;
}

int fsReadlink(const char* path, char* buf, size_t size) {
    Node n = resolve(ctx(), path);
    if (n.kind != Node::Link) return -EINVAL;
    std::snprintf(buf, size, "%s", n.text.c_str());
    return 0;
}

int fsReaddir(const char* path, void* buf, fuse_fill_dir_t fill, off_t, struct fuse_file_info*,
              enum fuse_readdir_flags) {
    Mount& m = ctx();
    Node n = resolve(m, path);
    if (n.kind != Node::Dir) return -ENOTDIR;
    auto add = [&](const std::string& name) { fill(buf, name.c_str(), nullptr, 0, fuse_fill_dir_flags(0)); };
    add(".");
    add("..");
    switch (n.dirKind) {
        case Node::Root:
            add("games");
            for (const auto& [tag, _] : m.store.tags()) add(tag);
            break;
        case Node::Games:
            for (const std::string& game : m.store.games()) add(game);
            break;
        case Node::Game:
            add("current");
            for (const std::string& husk : m.store.huskBuilds(n.game)) add(husk);
            break;
        case Node::Tree:
            if (n.dir == 0) {
                if (!n.build->error.empty()) add(".error");
                for (const auto& [name, file] : kBuildRecords)
                    if (fs::exists(m.store.buildDir(n.id) / file)) add(name);
            }
            for (const auto& [name, _] : n.build->dirs[n.dir].entries) add(name);
            break;
    }
    return 0;
}

constexpr uint64_t kNoFd = UINT64_MAX;  // fi->fh for in-memory text

int fsOpen(const char* path, struct fuse_file_info* fi) {
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EROFS;
    Mount& m = ctx();
    Node n = resolve(m, path);
    if (n.kind == Node::Text) {
        fi->fh = kNoFd;
        return 0;
    }
    if (n.kind != Node::Object && n.kind != Node::Real) return n.kind == Node::None ? -ENOENT : -EISDIR;
    fs::path p = n.kind == Node::Object ? m.store.objectPath(n.key) : n.real;
    int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    fi->fh = uint64_t(fd);
    fi->keep_cache = n.kind == Node::Object;  // objects never change
    return 0;
}

int fsRead(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    if (fi->fh == kNoFd) {
        Node n = resolve(ctx(), path);
        if (offset >= off_t(n.text.size())) return 0;
        size_t count = std::min(size, n.text.size() - size_t(offset));
        std::memcpy(buf, n.text.data() + offset, count);
        return int(count);
    }
    ssize_t r = ::pread(int(fi->fh), buf, size, offset);
    return r < 0 ? -errno : int(r);
}

int fsRelease(const char*, struct fuse_file_info* fi) {
    if (fi->fh != kNoFd) ::close(int(fi->fh));
    return 0;
}

void* fsInit(struct fuse_conn_info*, struct fuse_config* cfg) {
    // Objects and listed builds never change, but tags and the build list
    // do when store-add runs alongside the mount: a minute of kernel
    // caching bounds how stale those look.
    cfg->entry_timeout = 60;
    cfg->attr_timeout = 60;
    cfg->negative_timeout = 10;
    return fuse_get_context()->private_data;
}

}  // namespace

void addStoreAddOptions(CLI::App& app, StoreAddOptions& opts) {
    app.add_option("store", opts.store, "the canonical store directory")->required();
    app.add_option("tree", opts.tree, "an export's output directory to record as a build")->required();
    app.add_option("--game", opts.game, "the game build the tree was exported from, e.g. 12.1.0.69933");
    app.add_option("--game-from", opts.gameFrom,
                   "read the game build from a source mount's tag link instead, e.g. /media/luna/wow_export/live");
    app.add_option("--tag", opts.tags, "point this tag (live, ptr, ...) at the build; repeatable");
}

int storeAdd(int argc, char** args) {
    CLI::App app{"husk store-add -- record an export tree as a build in the canonical store", "husk store-add"};
    StoreAddOptions opts;
    addStoreAddOptions(app, opts);
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;
    if (opts.game.empty() == opts.gameFrom.empty()) {
        std::cerr << "husk: store-add: give exactly one of --game and --game-from\n";
        return 2;
    }
    try {
        std::string game = opts.game.empty() ? gameLabelOf(opts.gameFrom) : opts.game;
        corpus::Store store(fs::absolute(opts.store));
        corpus::IngestResult r = corpus::ingestTree(store, opts.tree, game);
        for (const std::string& tag : opts.tags) store.setTag(tag, r.id);
        std::cout << (r.alreadyStored ? "already stored: " : "stored: ") << r.id.path() << ", " << r.files
                  << " files, " << r.bytes << " bytes, " << r.addedObjects << " new objects, " << r.missing
                  << " missing, " << r.inputs << " input rows\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: store-add: " << e.what() << "\n";
        return 1;
    }
}

void addMountOptions(CLI::App& app, MountOptions& opts) {
    app.add_option("store", opts.store, "the canonical store directory")->required();
    app.add_option("mountpoint", opts.mountpoint, "an empty directory you own")->required();
    app.add_flag("--foreground", opts.foreground, "stay attached to the terminal instead of daemonizing");
}

int mount(int argc, char** args) {
    CLI::App app{"husk mount -- browse every stored build as a read-only filesystem (fusermount3 -u to unmount)",
                 "husk mount"};
    MountOptions opts;
    addMountOptions(app, opts);
    int exitCode = 0;
    if (!parseArgs(app, argc, args, exitCode)) return exitCode;
    fs::path storeDir = fs::absolute(opts.store);
    if (!fs::is_directory(storeDir)) {
        std::cerr << "husk: mount: expected a store directory, got " << storeDir << "\n";
        return 1;
    }

    struct stat rootStat {};
    ::stat(storeDir.c_str(), &rootStat);
    Mount m{corpus::Store(storeDir), rootStat.st_mtim, {}, {}};
    std::string tagSummary;
    for (const auto& [tag, id] : m.store.tags()) tagSummary += " " + tag + "->" + id.path();
    std::cerr << "mounting " << storeDir.string() << " at " << opts.mountpoint << ": " << m.store.games().size()
              << " game builds, tags:" << (tagSummary.empty() ? " none" : tagSummary) << "\n";

    struct fuse_operations ops {};
    ops.getattr = fsGetattr;
    ops.readlink = fsReadlink;
    ops.readdir = fsReaddir;
    ops.open = fsOpen;
    ops.read = fsRead;
    ops.release = fsRelease;
    ops.init = fsInit;

    std::vector<std::string> argvStore = {"husk", opts.mountpoint, "-o",
                                          "ro,subtype=husk,fsname=husk-store:" + storeDir.string()};
    if (opts.foreground) argvStore.push_back("-f");
    std::vector<char*> argv;
    for (auto& a : argvStore) argv.push_back(a.data());
    return fuse_main(int(argv.size()), argv.data(), &ops, &m);
}

}  // namespace husk::commands
