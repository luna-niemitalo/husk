#include "canon_bone_stem.hpp"

#include <array>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace husk::canon {

namespace {

std::string buildAlphabet() {
    std::string s;
    for (char c = 'a'; c <= 'z'; ++c) {
        if (c == 'i' || c == 'l' || c == 'o' || c == 'z') continue;
        s.push_back(c);
    }
    for (char c = '0'; c <= '9'; ++c) s.push_back(c);
    return s;
}

constexpr size_t kMiddlePairCount = kBoneStemAlphabetSize * (kBoneStemAlphabetSize - 1);
constexpr size_t kMiddleTierCount = kMiddlePairCount * 3;
constexpr size_t kXyzCount =
    kBoneStemAlphabetSize * (kBoneStemAlphabetSize - 1) * (kBoneStemAlphabetSize - 2);
constexpr size_t kTotalStems = kBoneStemAlphabetSize + kMiddleTierCount + kXyzCount;

// (commonIdx, distinctIdx) for the pairIndex'th pair, ordered common
// ascending then distinct ascending over the alphabet minus commonIdx --
// DESIGN.md leaves this ordering unspecified beyond "deterministic",
// this is the chosen one.
std::pair<size_t, size_t> decodePair(size_t pairIndex) {
    size_t common = pairIndex / (kBoneStemAlphabetSize - 1);
    size_t offset = pairIndex % (kBoneStemAlphabetSize - 1);
    size_t distinct = offset < common ? offset : offset + 1;
    return {common, distinct};
}

// Factorial-number-system decode of an ordered triple of distinct
// indices into [0, alphabetSize) -- only reached once XXX + the middle
// tier (~2600 stems) are exhausted, i.e. never for a real skeleton.
std::array<size_t, 3> decodeDistinctTriple(size_t m) {
    constexpr size_t n = kBoneStemAlphabetSize;
    size_t i0 = m / ((n - 1) * (n - 2));
    m %= (n - 1) * (n - 2);
    size_t i1 = m / (n - 2);
    size_t i2 = m % (n - 2);

    std::vector<size_t> pool(n);
    std::iota(pool.begin(), pool.end(), 0);
    size_t a = pool[i0];
    pool.erase(pool.begin() + static_cast<ptrdiff_t>(i0));
    size_t b = pool[i1];
    pool.erase(pool.begin() + static_cast<ptrdiff_t>(i1));
    size_t c = pool[i2];
    return {a, b, c};
}

}  // namespace

const std::string& boneStemAlphabet() {
    static const std::string alphabet = buildAlphabet();
    return alphabet;
}

std::string nthStem(size_t n) {
    if (n >= kTotalStems) throw std::out_of_range("canon::nthStem: n exceeds the fixed stem sequence");
    const std::string& alpha = boneStemAlphabet();

    if (n < kBoneStemAlphabetSize) {
        char c = alpha[n];
        return {c, c, c};
    }
    n -= kBoneStemAlphabetSize;

    if (n < kMiddleTierCount) {
        size_t pairIndex = n / 3;
        size_t shape = n % 3;  // 0 = XYX, 1 = XXY, 2 = YXX
        auto [commonIdx, distinctIdx] = decodePair(pairIndex);
        char common = alpha[commonIdx];
        char distinct = alpha[distinctIdx];
        switch (shape) {
            case 0: return {common, distinct, common};
            case 1: return {common, common, distinct};
            default: return {distinct, common, common};
        }
    }
    n -= kMiddleTierCount;

    auto [a, b, c] = decodeDistinctTriple(n);
    return {alpha[a], alpha[b], alpha[c]};
}

}  // namespace husk::canon
