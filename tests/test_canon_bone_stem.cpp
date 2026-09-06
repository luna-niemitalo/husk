// Tests for husk::canon bone-stem generation (src/canon_bone_stem.hpp) --
// the alphabet, the fixed generation order, and determinism, per
// DESIGN.md's "canon:: bone naming" section.

#include <doctest/doctest.h>

#include <set>
#include <stdexcept>

#include "canon_bone_stem.hpp"

using namespace husk::canon;

namespace {

// XXX / XYX / XXY / YXX / XYZ per DESIGN.md's template vocabulary.
std::string shapeOf(const std::string& stem) {
    bool ab = stem[0] == stem[1];
    bool bc = stem[1] == stem[2];
    bool ac = stem[0] == stem[2];
    if (ab && bc) return "XXX";
    if (ac && !ab) return "XYX";
    if (ab && !bc) return "XXY";
    if (bc && !ab) return "YXX";
    return "XYZ";
}

}  // namespace

TEST_CASE("boneStemAlphabet: excludes i/l/o/z, includes digits, size 32") {
    const std::string& a = boneStemAlphabet();
    CHECK(a.size() == kBoneStemAlphabetSize);
    // 26 letters - {i, l, o, z} + 10 digits = 32.
    CHECK(a.size() == 32);
    CHECK(a.find('i') == std::string::npos);
    CHECK(a.find('l') == std::string::npos);
    CHECK(a.find('o') == std::string::npos);
    CHECK(a.find('z') == std::string::npos);
    CHECK(a.find('0') != std::string::npos);
    CHECK(a.find('1') != std::string::npos);
    CHECK(a.find('2') != std::string::npos);
    for (char c : a) {
        bool isLetter = c >= 'a' && c <= 'z';
        bool isDigit = c >= '0' && c <= '9';
        CHECK((isLetter || isDigit));
    }
}

TEST_CASE("nthStem: first kBoneStemAlphabetSize stems are all XXX-shaped, one per alphabet character, no repeats") {
    const std::string& alpha = boneStemAlphabet();
    std::set<std::string> seen;
    for (size_t i = 0; i < kBoneStemAlphabetSize; ++i) {
        std::string stem = nthStem(i);
        CAPTURE(i);
        CAPTURE(stem);
        REQUIRE(stem.size() == 3);
        CHECK(stem[0] == stem[1]);
        CHECK(stem[1] == stem[2]);
        CHECK(stem[0] == alpha[i]);
        CHECK(seen.insert(stem).second);
    }
}

TEST_CASE("nthStem: immediately after the XXX run, template shape visibly alternates") {
    std::string s0 = nthStem(kBoneStemAlphabetSize + 0);
    std::string s1 = nthStem(kBoneStemAlphabetSize + 1);
    std::string s2 = nthStem(kBoneStemAlphabetSize + 2);

    std::string shape0 = shapeOf(s0);
    std::string shape1 = shapeOf(s1);
    std::string shape2 = shapeOf(s2);

    CAPTURE(s0);
    CAPTURE(s1);
    CAPTURE(s2);

    // Same characters (a, 1 -- the first common/distinct pair), three
    // distinct shapes -- the one property DESIGN.md cares about most:
    // shape varies before filler does.
    CHECK(shape0 != shape1);
    CHECK(shape1 != shape2);
    CHECK(shape0 != shape2);
    for (const auto& shape : {shape0, shape1, shape2}) {
        CHECK((shape == "XYX" || shape == "XXY" || shape == "YXX"));
    }
}

TEST_CASE("nthStem: a full (common, distinct) pair's three shapes share both characters") {
    std::string xyx = nthStem(kBoneStemAlphabetSize + 0);
    std::string xxy = nthStem(kBoneStemAlphabetSize + 1);
    std::string yxx = nthStem(kBoneStemAlphabetSize + 2);

    std::set<char> chars{xyx[0], xyx[1], xyx[2]};
    CHECK(chars.size() == 2);
    CHECK(std::set<char>{xxy[0], xxy[1], xxy[2]} == chars);
    CHECK(std::set<char>{yxx[0], yxx[1], yxx[2]} == chars);
}

TEST_CASE("nthStem: determinism -- same index always gives the same stem") {
    for (size_t i : {size_t{0}, size_t{29}, size_t{30}, size_t{31}, size_t{100}, size_t{2000}}) {
        CHECK(nthStem(i) == nthStem(i));
    }
}

TEST_CASE("StemGenerator: sequential .next() calls match nthStem(0..N-1)") {
    StemGenerator gen;
    for (size_t i = 0; i < 200; ++i) {
        CAPTURE(i);
        CHECK(gen.next() == nthStem(i));
    }
}

TEST_CASE("nthStem: no collisions among the first 100 generated stems") {
    std::set<std::string> seen;
    for (size_t i = 0; i < 100; ++i) {
        CHECK(seen.insert(nthStem(i)).second);
    }
    CHECK(seen.size() == 100);
}

TEST_CASE("nthStem: throws once the fixed sequence is exhausted") {
    constexpr size_t n = kBoneStemAlphabetSize;
    constexpr size_t total = n + n * (n - 1) * 3 + n * (n - 1) * (n - 2);
    CHECK_NOTHROW(nthStem(total - 1));
    CHECK_THROWS_AS(nthStem(total), std::out_of_range);
}
