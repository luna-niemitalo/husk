#!/usr/bin/env python3
"""Derive the texture-tag vocabulary + co-occurrence structure that
TODO/TEXTURE_POOL_RECALL_TODO.md step 1 asks for: a tag vocabulary and its
co-occurrence structure measured off the real corpus, not eyeballed off one
folder (the prototype's own known gap -- see the TODO's "The finding"
section). This is a pure filename walk: no husk subprocess per file, so it
deliberately does not use corpus_scan_framework.py (that framework exists
for per-file subprocess work -- see tools/CORPUS_SCANS.md).

Method (see TODO's "Tokenization is the real design question" section for
why a single split("_") pass is not enough -- character/bloodelf/female/
alone mixes an underscore-clean convention ("bloodelf_female_dh_tattoo_30_e")
with a fully run-together one ("bloodelffemalenakedtorsoskin00_105_hd")):

  1. Walk every *.blp under --root. Record (top-level dir, lowercased stem).
  2. Split each stem into "atoms": break on non-alnum separators, then
     further break each piece into alpha-runs and digit-runs. This alone
     correctly tokenizes the underscore-clean convention but leaves
     run-together stems as one long opaque alpha atom
     ("bloodelffemalenakedtorsoskin").
  3. Seed a vocabulary from atoms that are already, verbatim, their own
     separator-delimited atom somewhere in the corpus with real
     cross-file frequency (--seed-min-count) -- "torso", "skin", "color",
     "hair", "female", "male", "hd", the "_e"/"_a" variant markers, etc.
     This alone tokenizes most of the corpus already: it's the
     underscore-clean convention's own vocabulary, measured, not
     eyeballed.
  4. Iteratively grow that vocabulary to reach into the run-together
     blobs (`ITERATE_RESIDUAL_MINING` below has the full rationale and
     the real failure that motivated it): re-segment every atom against
     the current vocabulary via greedy longest-match; collect the
     *unmatched* character spans left over (each contiguous unmatched
     run, not single dropped characters); weight each distinct residual
     span by how many distinct files produce it; promote any residual
     clearing --seed-min-count into the vocabulary; repeat until a round
     adds nothing new (or --max-rounds is hit). Each round strips one
     more layer -- first the race/gender prefix, exposing "naked" as an
     isolated residual; next round, with "naked" now recognized, exposes
     "pelvis" the same way.
  5. Recount final tokens -- corpus-wide and per top-level directory --
     over the CONVERGED vocabulary, keep only tokens at or above
     --report-min-count as the reported vocabulary.
  6. For that reported vocabulary, compute pairwise co-occurrence and
     conditional probabilities (P(B|A), P(A|B), lift = P(A&B)/(P(A)P(B))),
     and classify each pair (implies / disjoint / orthogonal) by the same
     shape of relations the TODO itself measured by hand ("scalp+lower =
     0", "naked = pelvis + torso", "_hd is a hard partition").

Known limitation, stated up front: a word that NEVER appears in a
different flanking context anywhere in the corpus -- not even after
stripping the outer prefix/suffix layers -- cannot be separated from an
arbitrary same-length neighboring window by frequency alone; nothing
statistical distinguishes it from noise. This is real and measured, not
theoretical: see the report appended to the TODO for which real words hit
this (there weren't any among the tokens the TODO itself named -- the
iterative approach converges on all of them -- but the general limit still
applies to anything this session didn't happen to check).

Run:

    direnv exec . tools/venv/bin/python tools/derive_texture_tag_vocabulary.py \\
        --root /media/luna/data/wow_export \\
        --output corpus_reports/texture_tag_vocabulary.json

Validate against the TODO's own hand counts (character/bloodelf/female/)
without touching the rest of the corpus:

    ... --validate-only
"""
from __future__ import annotations

import argparse
import json
import time
import os
import re
from collections import Counter, defaultdict
from pathlib import Path

SEPARATOR_RE = re.compile(r"[^a-z0-9]+")
ALNUM_RUN_RE = re.compile(r"[a-z]+|[0-9]+")

# A single dropped character is essentially always noise (a stray letter,
# not a word) -- residual spans shorter than this are never promoted,
# mining or reporting.
MIN_RESIDUAL_LEN = 2

# Real bug found via validation, one level up from the MIN_RESIDUAL_LEN
# fix above: 2-3 char natural atoms ("ak", "ed", "or", "to", "na" --
# real standalone atoms *somewhere* in the 771k-file corpus, just
# unrelated to character texture names) are common enough by pure chance
# to coincidentally recur as substrings inside many unrelated longer
# blobs. Letting them match mid-blob fragmented "naked" -> "n"+"ak"+"ed"
# and "torso" -> "t"+"or"+"so" before the real 5-6 char words ever got a
# clean, contiguous residual span to be mined from. Vocab words shorter
# than this may only match when they span an atom's *entire* length
# (the same rule MIN_RESIDUAL_LEN already applies below it) -- every
# real semantic tag this project cares about ("skin", "hair", "face",
# ...) is 4+ chars, so this costs nothing real.
MIN_MIDBLOB_MATCH_LEN = 4


def iter_blp_stems(root: Path):
    """Yields (top_level_dir, lowercased_stem) for every *.blp under root."""
    root = root.resolve()
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if not name.lower().endswith(".blp"):
                continue
            rel = os.path.relpath(dirpath, root)
            top = rel.split(os.sep, 1)[0] if rel != "." else "<root>"
            stem = name[: -len(".blp")].lower()
            yield top, stem


def split_atoms(stem: str) -> list[str]:
    """Splits a lowercased stem into alpha atoms only (digit runs dropped
    -- they're variant/LOD indices, not semantic tags: "00"/"105" never
    carry meaning on their own in any of the real filenames this was
    checked against)."""
    atoms: list[str] = []
    for segment in SEPARATOR_RE.split(stem):
        if not segment:
            continue
        for m in ALNUM_RUN_RE.finditer(segment):
            piece = m.group(0)
            if piece.isalpha():
                atoms.append(piece)
    return atoms


def _index_by_len(vocab: set[str]) -> list[tuple[int, set[str]]]:
    by_len: dict[int, set[str]] = defaultdict(set)
    for w in vocab:
        by_len[len(w)].add(w)
    return sorted(by_len.items(), key=lambda kv: -kv[0])


def segment_atom_parts(atom: str, vocab_by_len: list[tuple[int, set[str]]]) -> list[tuple[bool, str]]:
    """Greedy longest-match segmentation of one atom. Returns a sequence
    of (is_match, text) parts in left-to-right order: matched vocabulary
    tokens, and contiguous unmatched character spans.

    Short (<MIN_MIDBLOB_MATCH_LEN) vocab words -- the real "_e"/"_a" variant
    markers -- only ever legitimately occur as a WHOLE separator-
    delimited atom (split_atoms already isolates them that way), never
    as a fragment inside a longer run-together blob. A real bug found
    via validation: letting them match mid-blob (e.g. the "e" inside
    "naked") fragmented what should have been one contiguous "naked"
    residual into "nak" + "e" + garbage, since a single matched letter
    breaks residual-span contiguity. So a short word may only match when
    it spans the atom's *entire* length, i.e. the atom itself is that
    short word."""
    parts: list[tuple[bool, str]] = []
    i, n = 0, len(atom)
    residual_start = None
    while i < n:
        matched_word = None
        for length, words in vocab_by_len:
            if length < MIN_MIDBLOB_MATCH_LEN and not (i == 0 and length == n):
                continue
            if i + length > n:
                continue
            cand = atom[i : i + length]
            if cand in words:
                matched_word = cand
                break
        if matched_word is not None:
            if residual_start is not None:
                parts.append((False, atom[residual_start:i]))
                residual_start = None
            parts.append((True, matched_word))
            i += len(matched_word)
        else:
            if residual_start is None:
                residual_start = i
            i += 1
    if residual_start is not None:
        parts.append((False, atom[residual_start:n]))
    return parts


def segment_atom(atom: str, vocab_by_len: list[tuple[int, set[str]]]) -> list[str]:
    return [text for is_match, text in segment_atom_parts(atom, vocab_by_len) if is_match]


# ---------------------------------------------------------------------------
# ITERATE_RESIDUAL_MINING
#
# The first version of this script mined arbitrary frequent substrings
# (length 3-12) directly out of raw run-together atoms, weighted by how
# many distinct files shared that exact substring. It failed a real
# validation run against character/bloodelf/female/ (naked/pelvis/torso/
# scalp+upper all came back 0, not the TODO's hand-measured 80/40/40/14),
# root-caused to two compounding problems, both confirmed with real
# numbers before this rewrite:
#
#   1. Weighting by file count let one heavily-repeated atom (e.g.
#      "bloodelffemalenakedpelvisskin", which recurs across ~40 files
#      differing only by a trailing digit) inflate EVERY 12-char window
#      of itself to the same weight -- including boundary-crossing
#      garbage like "vfemalenaked" -- so arbitrary windows tied with or
#      beat the real word boundaries.
#   2. Even after switching to distinct-atom document frequency (each
#      atom string contributes at most 1, fixing #1), a shorter substring
#      is *mathematically guaranteed* to have doc-freq >= any longer
#      string containing it (every occurrence of the longer string is an
#      occurrence of the shorter one). Real measured numbers: "naked"
#      doc-freq 111 vs. its 12-char superstring "akedpelviss" at 58 --
#      "naked" wins there -- but "pelvis" (doc-freq 58) exactly TIES
#      "akedpelviss" (58), because in this real corpus "pelvis" and
#      "naked" have never once appeared apart. There is no frequency
#      signal in the raw corpus that prefers "pelvis" over an arbitrary
#      same-frequency window -- both are equally "correct" by pure
#      substring statistics.
#
# The fix is iterative, not a better single-pass metric: the reason
# "naked" and "pelvis" tie with noise is that every occurrence of either
# is still carrying the race/gender prefix ("bloodelf", "orc", "troll",
# ...) as part of what's being counted. Once vocabulary from an earlier
# round strips that prefix off during segmentation, the LEFTOVER residual
# span is the same short string ("naked", then "pelvis") regardless of
# race -- and its frequency, recomputed fresh over just that residual,
# is no longer diluted or tied against overlapping noise windows, because
# the race-specific noise is gone. This is why the module docstring's
# step 4 re-segments and re-mines in a loop instead of doing it once.
# ---------------------------------------------------------------------------


def mine_compound_affixes(compound_weight: dict[str, int]) -> Counter:
    """Second mining source within each round, alongside raw-file
    residuals: a real gap the raw-file pass alone can't close. Once
    "nakedtorso" and "nakedpelvis" are both promoted (each individually
    frequent -- 859 and 1038 real files), greedy longest-match always
    matches them whole from then on, so re-segmenting the *original*
    file atoms never again produces a residual that would expose their
    shared "naked" prefix -- the round hits a stable fixed point one
    layer too coarse. Comparing the vocabulary's own compound entries
    pairwise for a shared prefix/suffix (>= MIN_MIDBLOB_MATCH_LEN) finds
    it directly: "naked" is the common prefix of two independently-
    frequent compounds, regardless of whether "torso"/"pelvis" have
    separately been recognized yet. Candidate weight is the *sum* of
    every compound word sharing that exact affix (not just the pair that
    suggested it), so the promotion threshold below still means the same
    thing it means for a raw-file residual: real, corpus-wide recurrence."""
    words = list(compound_weight.keys())
    candidate_affixes: set[str] = set()
    for i in range(len(words)):
        w1 = words[i]
        for j in range(i + 1, len(words)):
            w2 = words[j]
            k = 0
            while k < len(w1) and k < len(w2) and w1[k] == w2[k]:
                k += 1
            if k >= MIN_MIDBLOB_MATCH_LEN:
                candidate_affixes.add(w1[:k])
            k = 0
            while k < len(w1) and k < len(w2) and w1[-1 - k] == w2[-1 - k]:
                k += 1
            if k >= MIN_MIDBLOB_MATCH_LEN:
                candidate_affixes.add(w1[len(w1) - k :])

    weights: Counter = Counter()
    for affix in candidate_affixes:
        weights[affix] = sum(
            c for w, c in compound_weight.items() if w.startswith(affix) or w.endswith(affix)
        )
    return weights


def collapse_redundant_compounds(
    vocab: set[str], compound_weight: dict[str, int], seed_min_count: int
) -> dict[str, int]:
    """Retires a compound word once the *rest* of the vocabulary can
    already explain part of it -- e.g. once "naked" is recognized,
    "nakedtorso" (a coarser entry from an earlier round) is redundant:
    greedy longest-match would otherwise keep preferring the longer
    "nakedtorso" over "naked" + residual forever, since nothing ever
    forces a re-look at an already-accepted word. Retiring it and
    crediting its own established weight to the newly-exposed residual
    ("torso") is what actually lets "torso"/"pelvis" surface as
    independent tokens once "naked" is known -- confirmed necessary via
    validation: without this, "naked" alone was discoverable but
    "torso"/"pelvis" stayed permanently shadowed by the coarser
    "nakedtorso"/"nakedpelvis" compounds.

    Guards against a real coincidental-overlap risk found while making
    this collapsible for seed words too: "female" trivially contains
    "male" (a separately, legitimately high-frequency word) as a
    substring at position 2, leaving "fe" -- pure noise, not a second
    real word -- as the leftover. Retiring "female" over that would be a
    straightforward regression. So a word is only ever retired when
    EVERY leftover residual piece also clears MIN_MIDBLOB_MATCH_LEN (or
    there is no residual at all, i.e. full coverage by recognized
    words) -- "scalpupperhair" -> "upper" + "hair" + residual "scalp"
    (5 chars) passes; "female" -> "male" + residual "fe" (2 chars) does
    not, so "female" is correctly left intact.

    Mutates vocab/compound_weight in place. Returns the newly promoted
    residual words (word -> credited weight), for the caller to fold
    into the next round the same way as any other new word."""
    promoted: dict[str, int] = {}
    changed = True
    while changed:
        changed = False
        for w in list(compound_weight.keys()):
            if w not in vocab:
                continue
            trial_by_len = _index_by_len(vocab - {w})
            parts = segment_atom_parts(w, trial_by_len)
            has_match = any(is_match for is_match, _ in parts)
            all_residuals_meaningful = all(
                len(text) >= MIN_MIDBLOB_MATCH_LEN for is_match, text in parts if not is_match
            )
            if not has_match or not all_residuals_meaningful:
                continue  # doesn't decompose cleanly (yet) -- keep it as-is
            weight = compound_weight[w]
            vocab.discard(w)
            del compound_weight[w]
            changed = True
            for is_match, text in parts:
                if is_match or len(text) < MIN_MIDBLOB_MATCH_LEN:
                    continue
                new_weight = promoted.get(text, 0) + weight
                promoted[text] = new_weight
                if new_weight >= seed_min_count and text not in vocab:
                    vocab.add(text)
                    compound_weight[text] = new_weight
    return promoted


def grow_vocabulary_iteratively(
    file_atoms: list[tuple[str, list[str]]],
    seed_weight: dict[str, int],
    seed_min_count: int,
    max_rounds: int,
) -> tuple[set[str], int]:
    vocab = set(seed_weight)
    # Tracks every collapsible word's own real file-count -- seed atoms
    # included, not just words this loop discovered. Seed atoms need to
    # be collapsible too: "scalpupperhair" carries no race prefix (all
    # 27 races share the exact same filename), so it clears the seed
    # threshold as ONE atom straight away, before any mining round runs
    # -- without including it here, it would sit in vocab as a
    # permanently-whole seed word and "scalp"/"upper"/"hair" would never
    # get a chance to be recognized separately, the same shadowing
    # problem collapse_redundant_compounds exists to fix. See that
    # function's own docstring for why this is safe (residual-length
    # guard) rather than a "female" -> "male" + "fe" regression risk.
    compound_weight: dict[str, int] = dict(seed_weight)
    rounds_run = 0
    for round_i in range(1, max_rounds + 1):
        rounds_run = round_i
        vocab_by_len = _index_by_len(vocab)
        residual_file_count: Counter = Counter()
        for _top, atoms in file_atoms:
            file_residuals: set[str] = set()
            for atom in atoms:
                for is_match, text in segment_atom_parts(atom, vocab_by_len):
                    if not is_match and len(text) >= MIN_RESIDUAL_LEN:
                        file_residuals.add(text)
            for span in file_residuals:
                residual_file_count[span] += 1

        candidates = Counter(residual_file_count)
        if compound_weight:
            candidates.update(mine_compound_affixes(compound_weight))

        new_words = {s: c for s, c in candidates.items() if c >= seed_min_count and s not in vocab}
        if new_words:
            vocab |= new_words.keys()
            compound_weight.update(new_words)

        collapsed = collapse_redundant_compounds(vocab, compound_weight, seed_min_count)
        if not new_words and not collapsed:
            break
    return vocab, rounds_run


def classify_pair(count_a: int, count_b: int, count_both: int, total_files: int) -> str:
    if count_both == 0:
        return "disjoint"
    p_b_given_a = count_both / count_a
    p_a_given_b = count_both / count_b
    p_a = count_a / total_files
    p_b = count_b / total_files
    lift = (count_both / total_files) / (p_a * p_b) if p_a > 0 and p_b > 0 else 0.0
    if p_b_given_a >= 0.95 and p_a_given_b < 0.8:
        return "a_implies_b"
    if p_a_given_b >= 0.95 and p_b_given_a < 0.8:
        return "b_implies_a"
    if p_b_given_a >= 0.95 and p_a_given_b >= 0.95:
        return "equivalent"
    if 0.85 <= lift <= 1.15:
        return "orthogonal"
    return "correlated"


def derive(root: Path, seed_min_count: int, report_min_count: int, cooccur_top_k: int, max_rounds: int):
    t0 = time.monotonic()

    # ---- Pass 1: walk + atom extraction ----
    file_atoms: list[tuple[str, list[str]]] = []
    dir_counts: Counter = Counter()
    total_files = 0
    for top, stem in iter_blp_stems(root):
        atoms = split_atoms(stem)
        file_atoms.append((top, atoms))
        dir_counts[top] += 1
        total_files += 1
    t1 = time.monotonic()

    # ---- Pass 2: seed vocabulary -- natural atoms with real standalone
    # cross-file frequency (the underscore-clean convention's own words) ----
    atom_file_count: Counter = Counter()
    for _top, atoms in file_atoms:
        for a in set(atoms):
            atom_file_count[a] += 1
    seed_weight = {a: c for a, c in atom_file_count.items() if c >= seed_min_count}
    t2 = time.monotonic()

    # ---- Pass 3: iterative residual mining (see ITERATE_RESIDUAL_MINING) ----
    vocab, rounds_run = grow_vocabulary_iteratively(file_atoms, seed_weight, seed_min_count, max_rounds)
    vocab_by_len = _index_by_len(vocab)
    t3 = time.monotonic()

    # ---- Pass 4: re-segment every file, recount over the converged vocab ----
    token_corpus_count: Counter = Counter()
    token_dir_count: dict[str, Counter] = defaultdict(Counter)
    covered_chars = 0
    total_chars = 0
    file_token_cache: list[tuple[str, set[str]]] = []
    for top, atoms in file_atoms:
        file_tokens: set[str] = set()
        for atom in atoms:
            total_chars += len(atom)
            segs = segment_atom(atom, vocab_by_len)
            covered_chars += sum(len(s) for s in segs)
            file_tokens.update(segs)
        file_token_cache.append((top, file_tokens))
        for tok in file_tokens:
            token_corpus_count[tok] += 1
            token_dir_count[top][tok] += 1
    t4 = time.monotonic()

    reported = {t: c for t, c in token_corpus_count.items() if c >= report_min_count}

    # ---- Pass 5: co-occurrence over the top-K reported tokens ----
    top_tokens = [t for t, _ in sorted(reported.items(), key=lambda kv: -kv[1])[:cooccur_top_k]]
    top_token_set = set(top_tokens)
    both_count: Counter = Counter()
    for _top, file_tokens in file_token_cache:
        present = sorted(file_tokens & top_token_set)
        for i in range(len(present)):
            for j in range(i + 1, len(present)):
                both_count[(present[i], present[j])] += 1
    t5 = time.monotonic()

    cooccurrence = []
    for (a, b), both in both_count.items():
        ca, cb = reported[a], reported[b]
        relation = classify_pair(ca, cb, both, total_files)
        cooccurrence.append(
            {
                "a": a,
                "b": b,
                "count_a": ca,
                "count_b": cb,
                "count_both": both,
                "p_b_given_a": round(both / ca, 4),
                "p_a_given_b": round(both / cb, 4),
                "lift": round((both / total_files) / ((ca / total_files) * (cb / total_files)), 4) if ca and cb else 0.0,
                "relation": relation,
            }
        )
    cooccurrence.sort(key=lambda r: -r["count_both"])

    token_frequency = {}
    for tok, cnt in reported.items():
        by_dir = {d: token_dir_count[d][tok] for d in dir_counts if token_dir_count[d][tok] > 0}
        token_frequency[tok] = {"corpus": cnt, "by_dir": by_dir}

    timings = {
        "walk_and_tokenize_s": round(t1 - t0, 2),
        "seed_vocabulary_s": round(t2 - t1, 2),
        "iterative_mining_s": round(t3 - t2, 2),
        "segment_and_recount_s": round(t4 - t3, 2),
        "cooccurrence_s": round(t5 - t4, 2),
        "total_s": round(t5 - t0, 2),
    }

    result = {
        "method": {
            "description": (
                "Seed vocabulary from atoms already standalone somewhere "
                "in the corpus (real cross-file frequency), then "
                "iteratively re-segment + mine leftover unmatched spans "
                "as new vocabulary rounds until convergence. See "
                "tools/derive_texture_tag_vocabulary.py's module "
                "docstring and the ITERATE_RESIDUAL_MINING comment for "
                "the full method, the failed first attempt, and the "
                "stated limitation."
            ),
            "seed_min_count": seed_min_count,
            "report_min_count": report_min_count,
            "cooccur_top_k": cooccur_top_k,
            "max_rounds": max_rounds,
            "rounds_run": rounds_run,
            "min_residual_len": MIN_RESIDUAL_LEN,
            "min_midblob_match_len": MIN_MIDBLOB_MATCH_LEN,
        },
        "corpus": {
            "root": str(root),
            "total_blp_files": total_files,
            "top_level_dirs": dict(sorted(dir_counts.items(), key=lambda kv: -kv[1])),
            "segmentation_char_coverage": round(covered_chars / total_chars, 4) if total_chars else 0.0,
            "vocab_dictionary_size": len(vocab),
            "seed_vocabulary_size": len(seed_weight),
            "reported_token_count": len(reported),
        },
        "timings": timings,
        "token_frequency": dict(sorted(token_frequency.items(), key=lambda kv: -kv[1]["corpus"])),
        "cooccurrence": cooccurrence,
    }
    return result, vocab_by_len


def validate_against_bloodelf_female(vocab_by_len, root: Path):
    """Reproduces TODO/TEXTURE_POOL_RECALL_TODO.md's hand-measured
    character/bloodelf/female/ counts using this script's own tokenizer,
    to prove the derived method isn't silently wrong before trusting the
    corpus-wide numbers."""
    folder = root / "character" / "bloodelf" / "female"
    stems = [p.name[: -len(".blp")].lower() for p in folder.iterdir() if p.suffix.lower() == ".blp"]

    def toks(stem):
        s = set()
        for atom in split_atoms(stem):
            s.update(segment_atom(atom, vocab_by_len))
        return s

    file_tokens = [toks(s) for s in stems]

    def count(*want):
        return sum(1 for ft in file_tokens if all(w in ft for w in want))

    checks = [
        ("skin", ("skin",), 154),
        ("skin+color", ("skin", "color"), 12),
        ("skin+pelvis", ("skin", "pelvis"), 40),
        ("skin+torso", ("skin", "torso"), 40),
        ("naked", ("naked",), 80),
        ("hair+color", ("hair", "color"), 5),
        ("scalp+upper", ("scalp", "upper"), 14),
        ("scalp+lower", ("scalp", "lower"), 0),
        ("tattoo", ("tattoo",), 72),
    ]
    print(f"total .blp in {folder}: {len(stems)} (TODO states 1029)")
    for label, tokens, expected in checks:
        got = count(*tokens)
        flag = "OK" if got == expected else "MISMATCH"
        print(f"  {label:16s} got={got:4d} expected={expected:4d}  {flag}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=Path("/media/luna/data/wow_export"))
    ap.add_argument("--output", type=Path, default=Path("corpus_reports/texture_tag_vocabulary.json"))
    ap.add_argument(
        "--seed-min-count",
        type=int,
        default=100,
        help="Minimum distinct-file count for an atom (or a mined residual span) to enter the vocabulary -- stripRaceGenderSuffix's own precedent ('only codes with hundreds of real occurrences kept').",
    )
    ap.add_argument("--report-min-count", type=int, default=200)
    ap.add_argument(
        "--cooccur-top-k",
        type=int,
        default=5000,
        help="Real bug found via inspection, not just performance headroom: capping this at a modest value (150) selects the top tokens by RAW CORPUS-WIDE frequency, which is dominated by huge non-character directories (bakednpctextures/'s ~82k-file single-template naming, world tile/minimap naming, ...) -- every TODO-named pair (naked+torso, scalp+upper, ...) fell out of that top-150 entirely despite each token's own count being in the thousands, because unrelated cross-domain tokens numbered in the tens of thousands crowded them out. 5000 comfortably covers every --report-min-count-qualified token today (currently ~1200); the pairwise cost is bounded by co-occurring tokens per FILE, not by this value squared, so raising it is cheap.",
    )
    ap.add_argument(
        "--max-rounds",
        type=int,
        default=12,
        help="Safety cap, not a tuning knob -- validated convergence (character/bloodelf/female/) reaches its final vocabulary by round 8 and stays identical through round 20; the loop's own convergence check doesn't always detect a clean fixed point (a harmless round-over-round flap that doesn't change the resulting vocabulary), so this cap exists to bound worst-case runtime, not to cut mining short.",
    )
    ap.add_argument("--validate-only", action="store_true", help="Only run the bloodelf/female sanity check, skip the full corpus walk + output write.")
    args = ap.parse_args()

    if args.validate_only:
        # Needs a real vocabulary to segment against -- mine it from the
        # WHOLE corpus, not just the validation folder or even just
        # character/: words like "torso"/"upper"/"lower" mostly recur
        # standalone in item/ and interface/, not character/, so
        # restricting the mining scope would make this check pass for
        # the wrong reason (or fail this check while the real,
        # full-corpus run succeeds).
        file_atoms = []
        for top, stem in iter_blp_stems(args.root):
            file_atoms.append((top, split_atoms(stem)))
        atom_file_count = Counter()
        for _top, atoms in file_atoms:
            for a in set(atoms):
                atom_file_count[a] += 1
        seed_weight = {a: c for a, c in atom_file_count.items() if c >= args.seed_min_count}
        vocab, rounds_run = grow_vocabulary_iteratively(file_atoms, seed_weight, args.seed_min_count, args.max_rounds)
        print(f"seed vocab size: {len(seed_weight)}, converged vocab size: {len(vocab)}, rounds: {rounds_run}")
        validate_against_bloodelf_female(_index_by_len(vocab), args.root)
        return

    result, _vocab_by_len = derive(args.root, args.seed_min_count, args.report_min_count, args.cooccur_top_k, args.max_rounds)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(result, f, indent=1, sort_keys=False)

    print(f"wrote {args.output}")
    print(f"total .blp files: {result['corpus']['total_blp_files']}")
    print(f"vocab dictionary size: {result['corpus']['vocab_dictionary_size']} (seed {result['corpus']['seed_vocabulary_size']}, {result['method']['rounds_run']} rounds)")
    print(f"reported tokens (>= {args.report_min_count}): {result['corpus']['reported_token_count']}")
    print(f"char coverage: {result['corpus']['segmentation_char_coverage']}")
    print(f"timings: {result['timings']}")


if __name__ == "__main__":
    main()
