#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <thread>

#include "corpus_index.hpp"
#include "corpus_store.hpp"
#include "input_log.hpp"

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

TEST_CASE("corpus store: a linked tree's files are its objects, so it takes no space of its own") {
    fs::path dir = freshDir("link");
    corpus::Store store(dir / "store");
    fs::path tree = exportTree("link-tree", "husk e587735");
    writeText(tree / "textures" / "9.dds", "DDS bytes");  // same content as 7.dds: one object

    corpus::IngestResult r = corpus::ingestTree(store, tree, "12.1.0.69933", true);

    CHECK(r.files == 4);
    CHECK(r.addedObjects == 3);
    fs::path texture = tree / "textures" / "7.dds";
    fs::path object = store.objectPath(corpus::md5File(texture));
    CHECK(fs::equivalent(texture, object));
    CHECK(fs::equivalent(tree / "textures" / "9.dds", object));
    CHECK(readText(texture) == "DDS bytes");
    CHECK((fs::status(texture).permissions() & fs::perms::owner_write) == fs::perms::none);
    CHECK_FALSE(fs::exists(tree / "textures" / "7.dds.link"));

    // Storing the same tree again rehashes nothing: every file is journaled and linked.
    corpus::IngestResult again = corpus::ingestTree(store, tree, "12.1.0.69933", true);
    CHECK(again.alreadyStored);
}

TEST_CASE("corpus store: linkIntoStore links a finished output and journals it for store-add") {
    fs::path dir = freshDir("link-live");
    corpus::Store store(dir / "store");
    fs::path tree = exportTree("link-live-tree", "husk e587735");
    {
        corpus::TreeJournal journal(store, tree);
        corpus::linkIntoStore(store, journal, tree, tree / "models" / "1.canon.bundle");
    }
    fs::path manifest = tree / "models" / "1.canon.bundle" / "manifest.json";
    CHECK(fs::equivalent(manifest, store.objectPath(corpus::md5File(manifest))));
    CHECK(fs::hard_link_count(tree / "models" / "2.canon.bundle" / "manifest.json") == 1);  // not linked
    corpus::TreeJournal reread(store, tree);
    CHECK(reread.stored("models/1.canon.bundle/manifest.json").has_value());
    CHECK_FALSE(reread.stored("models/2.canon.bundle/manifest.json").has_value());
}

TEST_CASE("corpus store: an interrupted ingest resumes from its journal while files are unchanged") {
    fs::path dir = freshDir("journal");
    corpus::Store store(dir / "store");
    fs::path tree = exportTree("journal-tree", "husk e587735");
    corpus::IngestResult first = corpus::ingestTree(store, tree, "12.1.0.69933");
    fs::path journal = tree / "store-journal.tsv";
    std::string rows = readText(journal);
    CHECK(rows.find("textures/7.dds\t9\t") != std::string::npos);
    CHECK(rows.find("8.dds.partial") == std::string::npos);

    // A journaled row is trusted without rehashing: point 7.dds's at another
    // stored object, as only a resumed run would record it.
    std::string manifestMd5 = corpus::hex(corpus::md5File(tree / "models" / "1.canon.bundle" / "manifest.json"));
    std::string textureMd5 = corpus::hex(corpus::md5File(tree / "textures" / "7.dds"));
    size_t at = rows.find(textureMd5);
    REQUIRE(at != std::string::npos);
    rows.replace(at, textureMd5.size(), manifestMd5);
    writeText(journal, rows + "textures/7.dds\t9\t12");  // a torn last row
    fs::remove_all(store.buildDir(first.id));
    corpus::ingestTree(store, tree, "12.1.0.69933");
    CHECK(readText(store.buildDir(first.id) / "files.tsv").find("textures/7.dds\t" + manifestMd5) != std::string::npos);

    // A changed mtime voids the row.
    fs::last_write_time(tree / "textures" / "7.dds", fs::last_write_time(tree / "textures" / "7.dds") + std::chrono::seconds(1));
    fs::remove_all(store.buildDir(first.id));
    corpus::ingestTree(store, tree, "12.1.0.69933");
    CHECK(readText(store.buildDir(first.id) / "files.tsv").find("textures/7.dds\t" + textureMd5) != std::string::npos);
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
    CHECK_THROWS_WITH_AS(corpus::ingestTree(store, none, "12.1.0.69933"), doctest::Contains("expected husk documents"),
                         std::runtime_error);
    CHECK(store.games().empty());
}

TEST_CASE("input log: uses go to the scope's output, the run's outside any scope, nowhere in a discarding one") {
    fs::path dir = freshDir("inputs");
    writeText(dir / "src" / "a.m2", "abc");
    writeText(dir / "src" / "b.blp", "b");
    writeText(dir / "elsewhere" / "listfile.csv", "1;x");
    {
        inputs::Log log(dir / "inputs.log", dir / "src");
        inputs::setRunLog(&log);
        inputs::use(dir / "elsewhere" / "listfile.csv");
        inputs::use(dir / "elsewhere" / "listfile.csv");
        {
            inputs::Scope scope(&log, "models/1.canon.bundle");
            inputs::use(dir / "src" / "a.m2");
            inputs::use(dir / "src" / "a.m2");
            inputs::Scope* caller = inputs::Scope::current();
            std::thread([&] {
                inputs::Scope::Adopt adopt(caller);
                inputs::use(dir / "src" / "b.blp");
            }).join();
        }
        {
            inputs::Scope discovery(nullptr, "");
            inputs::use(dir / "src" / "b.blp");
        }
        inputs::setRunLog(nullptr);
    }
    CHECK(readText(dir / "inputs.log") ==
          "*\t" + (dir / "elsewhere" / "listfile.csv").string() + "\t" +
              corpus::hex(corpus::md5File(dir / "elsewhere" / "listfile.csv")) + "\n" +
              "models/1.canon.bundle\ta.m2\t900150983cd24fb0d6963f7d28e17f72\n" +
              "models/1.canon.bundle\tb.blp\t" + corpus::hex(corpus::md5File(dir / "src" / "b.blp")) + "\n");
}

TEST_CASE("corpus store: inputs.log becomes inputs.tsv, last row winning, gone outputs dropped") {
    fs::path dir = freshDir("ingest-inputs");
    corpus::Store store(dir / "store");
    fs::path tree = exportTree("ingest-inputs-tree", "husk e587735");
    const std::string a(32, 'a'), b(32, 'b'), c(32, 'c');
    writeText(tree / "inputs.log", "*\tdbfilesclient/x.db2\t" + a + "\n" +
                                       "models/1.canon.bundle\tm/1.m2\t" + a + "\n" +
                                       "models/1.canon.bundle\tm/1.m2\t" + b + "\n" +
                                       "models/9.canon.bundle\tm/9.m2\t" + c + "\n");
    corpus::IngestResult r = corpus::ingestTree(store, tree, "12.1.0.69933");
    CHECK(r.inputs == 2);
    CHECK(r.files == 3);
    CHECK(readText(store.buildDir(r.id) / "inputs.tsv") ==
          "output\tinput\tmd5\n*\tdbfilesclient/x.db2\t" + a + "\nmodels/1.canon.bundle\tm/1.m2\t" + b + "\n");
}

TEST_CASE("corpus index: displays find their model and preset, ChrModels their model") {
    fs::path tree = freshDir("index-tree");
    auto displayRef = [](int row) {
        return R"({"ref": {"id": {"kind": "db2_row", "table": "CreatureDisplayInfo", "row": )" + std::to_string(row) +
               "}}}";
    };
    writeText(tree / "models" / "5.canon.bundle" / "manifest.json",
              R"({"format": "husk-bundle", "resources": {"rig": [1, 2, 3]},
                 "definition": {"customization": {"options": [
                   {"chr_model": {"id": {"kind": "db2_row", "table": "ChrModel", "row": 45}}},
                   {"chr_model": {"id": {"kind": "db2_row", "table": "ChrModel", "row": 1}}}]}},
                 "selection": {"presets": [{"sources": [)" + displayRef(20) + "," + displayRef(7) +
                  R"(]}, {"sources": [)" + displayRef(9) + "]}]}}");
    writeText(tree / "models" / "6.canon.bundle" / "manifest.json", R"({"format": "husk-bundle", "selection": {}})");

    corpus::IndexResult r = corpus::buildIndex(tree);
    CHECK(r.manifests == 2);
    CHECK(readText(tree / "index" / "creature_displays.tsv") ==
          "display\tmodel\tpreset\n"
          "7\tmodels/5.canon.bundle/manifest.json\t0\n"
          "9\tmodels/5.canon.bundle/manifest.json\t1\n"
          "20\tmodels/5.canon.bundle/manifest.json\t0\n");
    CHECK(readText(tree / "index" / "chr_models.tsv") ==
          "chr_model\tmodel\n1\tmodels/5.canon.bundle/manifest.json\n45\tmodels/5.canon.bundle/manifest.json\n");
}

TEST_CASE("input log: readUsed records only the candidate it read, hashed from the bytes read") {
    fs::path dir = freshDir("read-used");
    writeText(dir / "src" / "_by_fdid" / "42", "anim");
    {
        inputs::Log log(dir / "inputs.log", dir / "src");
        inputs::Scope scope(&log, "models/1.canon.bundle");
        CHECK_FALSE(inputs::readUsed(dir / "src" / "42.anim"));
        auto read = inputs::readUsed(dir / "src" / "_by_fdid" / "42");
        REQUIRE(read);
        CHECK(std::string(read->begin(), read->end()) == "anim");
    }
    CHECK(readText(dir / "inputs.log") ==
          "models/1.canon.bundle\t_by_fdid/42\t" + corpus::hex(corpus::md5File(dir / "src" / "_by_fdid" / "42")) + "\n");
}

TEST_CASE("corpus store: an in-progress run is listed until its build is stored") {
    fs::path dir = freshDir("runs");
    corpus::Store store(dir / "store");
    fs::path tree = exportTree("runs-tree", "husk e587735");
    corpus::BuildId id{"12.1.0.69933", "e587735"};

    store.beginRun(id, tree);
    CHECK(store.games() == std::vector<std::string>{"12.1.0.69933"});
    REQUIRE(store.runs("12.1.0.69933").count("e587735"));
    CHECK(store.runs("12.1.0.69933").at("e587735") == fs::absolute(tree).lexically_normal());

    corpus::ingestTree(store, tree, "12.1.0.69933");
    CHECK(store.runs("12.1.0.69933").empty());

    store.endRun(id);
    CHECK_FALSE(fs::exists(dir / "store" / "runs" / "12.1.0.69933" / "e587735"));
}

TEST_CASE("corpus store: a build is labelled only by a clean producer") {
    CHECK(corpus::huskLabel("husk d1d6ff6") == "d1d6ff6");
    CHECK_THROWS_WITH_AS(corpus::huskLabel("husk d1d6ff6-dirty"), doctest::Contains("names no commit"),
                         std::runtime_error);
    CHECK_THROWS_WITH_AS(corpus::huskLabel("other 1.0"), doctest::Contains("is not husk's"), std::runtime_error);
}
