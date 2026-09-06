#pragma once

#include <cstddef>
#include <string>

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// Stem generation for DESIGN.md's "canon:: bone naming" scheme -- the
// <stem> half of <stem>_<side>_<index>.<subindex>. Chain/symmetry
// detection and wiring into canon::Joint are separate, later work; this
// file only produces the deterministic 3-character stem sequence.
namespace husk::canon {

// Crockford Base32's alphabet drops i/l/o (each reads as a digit: i/l as
// 1, o as 0) for the same reason -- this scheme reuses that precedent
// rather than re-deriving it. 26 letters - {i,l,o} + 10 digits = 33.
inline constexpr size_t kBoneStemAlphabetSize = 33;

// One string, ascending: a-z (i/l/o skipped) then 0-9. Arbitrary but
// fixed -- nthStem's ordering only needs *a* total order over the
// alphabet, not this particular one, so any other fixed order would be
// equally correct; this one just reads as "the alphabet, then digits".
const std::string& boneStemAlphabet();

// The Nth stem in the fixed, deterministic sequence DESIGN.md specifies:
// all kBoneStemAlphabetSize XXX candidates (one chunk, best) first, one per alphabet
// character in boneStemAlphabet() order; then the XYX/XXY/YXX middle
// tier (two chunks), grouped per (common, distinct) character pair so a
// pair's three shapes appear together -- "a1a, aa1, 1aa" reads as
// clearly distinct (shape varies) where "aa1, aa2, aaq" would not (only
// filler varies) -- before moving to the next pair; pairs ordered by
// common character ascending, then distinct character ascending over
// the remaining alphabet. Falls through to XYZ (three chunks, worst)
// only once the middle tier's stems (kBoneStemAlphabetSize choose 2,
// times 3 shapes) are exhausted, which no real skeleton (a few hundred
// bones) reaches.
//
// Pure function of n: calling it twice with the same n gives the same
// stem. Throws std::out_of_range once the whole fixed sequence is
// exhausted -- there is no meaningful stem past every distinct
// 3-character arrangement of the alphabet.
std::string nthStem(size_t n);

// Stateful wrapper around nthStem for chain-walking consumers that just
// want "the next unused stem" without tracking an index themselves.
class StemGenerator {
public:
    std::string next() { return nthStem(index_++); }

private:
    size_t index_ = 0;
};

}  // namespace husk::canon
