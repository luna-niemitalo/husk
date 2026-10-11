#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "corpus_store.hpp"

namespace fs = std::filesystem;
using namespace husk;

namespace {

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

fs::path freshDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("husk-store-test-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string document(const std::string& producer, const std::string& body = "") {
    return "{\n  \"format\": \"husk-bundle\",\n  \"kind\": \"model\",\n  \"schema_version\": \"0.1.0\",\n"
           "  \"provenance\": {\n    \"producer\": \"" + producer + "\"\n  }" + body + "\n}\n";
}

// A small export tree: two documents, a texture, a run log, an interrupted write.
fs::path exportTree(const std::string& name, const std::string& producer) {
    fs::path tree = freshDir(name);
    writeText(tree / "models" / "1.canon.bundle" / "manifest.json", document(producer));
    writeText(tree / "models" / "2.canon.bundle" / "manifest.json", document(producer, ",\n  \"x\": 2"));
    writeText(tree / "textures" / "7.dds", "DDS bytes");
    writeText(tree / "textures" / "8.dds.partial", "half");
    writeText(tree / "errors.log", "model\t9\tno local file\nmodel\t9\tno local file\nwmo\t5\tbroken\n");
    return tree;
}

}  // namespace

TEST_CASE("corpus store: md5 names match RFC 1321's test vectors") {
    fs::path dir = freshDir("md5");
    writeText(dir / "abc", "abc");
    CHECK(corpus::hex(corpus::md5File(dir / "abc")) == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(corpus::parseHex("900150983cd24fb0d6963f7d28e17f72") == corpus::md5File(dir / "abc"));
    CHECK_FALSE(corpus::parseHex("900150983cd24fb0d6963f7d28e17f7"));
    CHECK_FALSE(corpus::parseHex("900150983CD24FB0D6963F7D28E17F72"));
}

TEST_CASE("corpus store: an object is stored once, read-only, under its md5") {
    fs::path dir = freshDir("put");
    corpus::Store store(dir / "store");
    writeText(dir / "a", "same");
    writeText(dir / "b", "same");
    corpus::Store::Put first = store.putFile(dir / "a");
    corpus::Store::Put second = store.putFile(dir / "b");
    CHECK(first.added);
    CHECK_FALSE(second.added);
    CHECK(first.key == second.key);
    fs::path object = store.objectPath(first.key);
    CHECK(object.parent_path().filename() == corpus::hex(first.key).substr(2, 2));
    CHECK(readText(object) == "same");
    CHECK((fs::status(object).permissions() & fs::perms::owner_write) == fs::perms::none);
    CHECK(fs::is_empty(dir / "store" / "tmp"));
}

TEST_CASE("corpus store: labels and build paths are one path component each") {
    CHECK(corpus::validLabel("12.1.0.69933"));
    CHECK_FALSE(corpus::validLabel(".."));
    CHECK_FALSE(corpus::validLabel("a/b"));
    CHECK_FALSE(corpus::validLabel(""));
    auto id = corpus::parseBuildPath("12.1.0.69933/e587735");
    REQUIRE(id);
    CHECK(id->game == "12.1.0.69933");
    CHECK(id->husk == "e587735");
    CHECK_FALSE(corpus::parseBuildPath("12.1.0.69933"));
    CHECK_FALSE(corpus::parseBuildPath("../x"));
    CHECK_FALSE(corpus::parseBuildPath("a/b/c"));
}

TEST_CASE("corpus store: ingesting a tree records its files, missing rows and producer") {
    fs::path dir = freshDir("ingest");
    corpus::Store store(dir / "store");
    fs::path tree = exportTree("ingest-tree", "husk e587735");

    corpus::IngestResult r = corpus::ingestTree(store, tree, "12.1.0.69933");
    CHECK(r.id.path() == "12.1.0.69933/e587735");
    CHECK(r.files == 3);  // errors.log and the .partial file are not stored
    CHECK(r.addedObjects == 3);
    CHECK(r.missing == 2);
    CHECK_FALSE(r.alreadyStored);

    fs::path build = store.buildDir(r.id);
    std::string files = readText(build / "files.tsv");
    CHECK(files.rfind("path\tmd5\nmodels/1.canon.bundle/manifest.json\t", 0) == 0);
    CHECK(files.find("textures/7.dds\t" + corpus::hex(corpus::md5File(tree / "textures" / "7.dds"))) !=
          std::string::npos);
    CHECK(files.find("errors.log") == std::string::npos);
    CHECK(readText(build / "missing.tsv") == "phase\titem\treason\nmodel\t9\tno local file\nwmo\t5\tbroken\n");
    std::string meta = readText(build / "meta.json");
    CHECK(meta.find("\"producer\": \"husk e587735\"") != std::string::npos);
    CHECK(meta.find("\"schema_version\": \"0.1.0\"") != std::string::npos);

    CHECK(store.games() == std::vector<std::string>{"12.1.0.69933"});
    CHECK(store.huskBuilds("12.1.0.69933") == std::vector<std::string>{"e587735"});
    REQUIRE(store.newest("12.1.0.69933"));
    CHECK(store.newest("12.1.0.69933")->husk == "e587735");

    store.setTag("live", r.id);
    REQUIRE(store.tags().count("live"));
    CHECK(store.tags().at("live").path() == "12.1.0.69933/e587735");
}

TEST_CASE("corpus store: the same build again is a no-op, different files under it are refused") {
    fs::path dir = freshDir("resume");
    corpus::Store store(dir / "store");
    fs::path tree = exportTree("resume-tree", "husk e587735");
    corpus::ingestTree(store, tree, "12.1.0.69933");

    corpus::IngestResult again = corpus::ingestTree(store, tree, "12.1.0.69933");
    CHECK(again.alreadyStored);
    CHECK(again.addedObjects == 0);

    writeText(tree / "textures" / "7.dds", "other bytes");
    CHECK_THROWS_WITH_AS(corpus::ingestTree(store, tree, "12.1.0.69933"),
                         doctest::Contains("already holds this build with other files"), std::runtime_error);
}

TEST_CASE("corpus store: a tree whose producer names no commit, or several, is refused") {
    fs::path dir = freshDir("refuse");
    corpus::Store store(dir / "store");
    CHECK_THROWS_WITH_AS(corpus::ingestTree(store, exportTree("dirty-tree", "husk e587735-dirty"), "12.1.0.69933"),
                         doctest::Contains("names no commit"), std::runtime_error);
    CHECK_THROWS_WITH_AS(corpus::ingestTree(store, exportTree("unknown-tree", "husk unknown"), "12.1.0.69933"),
                         doctest::Contains("names no commit"), std::runtime_error);

    fs::path mixed = exportTree("mixed-tree", "husk e587735");
    writeText(mixed / "models" / "3.canon.bundle" / "manifest.json", document("husk a870776"));
    CHECK_THROWS_WITH_AS(corpus::ingestTree(store, mixed, "12.1.0.69933"),
                         doctest::Contains("documents disagree"), std::runtime_error);

    fs::path none = freshDir("none-tree");
    writeText(none / "textures" / "7.dds", "DDS bytes");
    CHECK_THROWS_WITH_AS(corpus::ingestTree(store, none, "12.1.0.69933"), doctest::Contains("no husk documents"),
                         std::runtime_error);
    CHECK(store.games().empty());
}
