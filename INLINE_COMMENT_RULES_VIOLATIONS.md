# Inline Comment Rules — Violations

Evaluated against `~/nix/claude-rules/CODE_COMMENT_RULES.md`. Scope: every `.cpp`/
`.hpp` in `src/` (~11,660 lines, ~3,194 comment lines) and every `.cpp`/`.hpp`/`.py`
in `tests/` (~13,465 lines, ~1,940 comment lines), both read in full — not sampled.
Docs (`*.md`) weren't in scope for this pass.

`tests/` gets one additional allowance `src/` doesn't: more verbose, developer-facing
commentary explaining *why a specific test exists or is shaped a certain way* is fine
and shouldn't be trimmed to a one-liner the way `src/` comments must be — a test file
is read by a maintainer, not shipped to a user. Two things still don't get that pass,
covered in their own section below (`## tests/`):

- **Dev-trace-doc citations** — identical policy to `src/`: any citation of
  `WIKI_FINDINGS.md`, `FAILURES.md`/`FAILURES2.md`, `FINDINGS.md`,
  `TODO_correctness.md`, `TRANSFORM_TRIAGE.md`, a deleted TODO file's former item
  number, or dated "this session"/"an earlier version" narrative gets
  **`TODO: Remove`**, same as in `src/`, regardless of being in a test file.
- **Scattered test-suite-design rationale** — commentary explaining a decision about
  the *test suite itself* (why real-data fixtures are `doctest::skip`-gated behind
  `HUSK_TEST_*` env vars, the four-tier unit/CLI/integration/conformance
  architecture, the "prove a regression test actually regresses" discipline, etc.)
  that's restated near-verbatim across multiple files. This is legitimate content,
  just in the wrong place scattered N times — it belongs once in a new
  **`TEST_DESIGN.md`** (drafted alongside this file), with each site trimmed to a
  short pointer.

## Verdict

The codebase is **compliant on mechanics**: bounds-check invariants, throws-conditions,
byte-offset tables, and ordering gotchas are genuinely load-bearing and read correctly
as inline comments. `chunk.hpp/cpp`, `bone.hpp/cpp`, `json_writer.hpp`, `commands.hpp`,
and `main.cpp` have no material violations — they stay at "why/invariant/gotcha"
altitude throughout.

The violations cluster into one dominant, repeated pattern: **investigation-narrative
comments** shaped like `<what was found> + <how many real files / what corpus scan> +
<see WIKI_FINDINGS.md §N>` — a findings-doc/commit-message entry pasted inline instead
of linked (rule §"Historical narrative... belongs in commit message or CHANGELOG", and
the "decision vs. fact" litmus test). A grep for citation patterns (`WIKI_FINDINGS.md`,
`FAILURES2.md`, `TODO_correctness.md`, `M2_GAPS_TODO.md`, `FINDINGS.md`,
`TRANSFORM_TRIAGE.md`, corpus counts like "2,354 real files") returns **156 hits**,
concentrated in `cmd_export.cpp` (48), `cmd_dump.cpp` (17), `m2.hpp` (15), `gltf.hpp`
(13). This is compounded by a second pattern: the *same* design decision re-explained
at 2–4 separate call sites (a struct's doc comment in the `.hpp`, its serialization
comment in the `.cpp`, and its note-printing comment in `cmd_export.cpp` all restate
"why this is extras, not real glTF" for one feature) instead of stated once and linked.

Roughly 55 distinct comment blocks are flagged below; of the 156 raw citation-hits,
the remainder are terse one-clause pointers that stay just inside "boundary contract"
territory and aren't listed individually.

Categories referenced below, from the rule file:
1. Restates the code
2. Narrates control flow
3. Design rationale spanning multiple functions/modules → belongs in DESIGN.md
4. Needs a paragraph → needs a doc, not a `//`
5. Historical narrative ("we used to do X, then Y") → belongs in commit message
6. Cross-cutting duplication — same rationale re-justified at multiple call sites

## Disposition policy (v1 gate)

Luna's follow-up call: a bare citation like `(see FAILURES2.md #8)` is legitimate
*while building* — it's not okay to ship in v1.

- **Cites a dev-phase trace doc** — `WIKI_FINDINGS.md`, `FAILURES.md`/`FAILURES2.md`,
  `FINDINGS.md`, `TODO_correctness.md`, `TRANSFORM_TRIAGE.md`, `M2_GAPS_TODO.md`,
  `BLENDER_EXPORT_TODO.md`, `CORPUS_TODO.md`, `RO_COMPLETENESS_TODO.md`, any
  now-deleted TODO file's "former Item N," a scan-tool script, or an external issue
  tracker link — these record a failure or investigation *found during development*,
  not a spot in the actual design. **Mark `// TODO: Remove` and keep the citation
  until removal**, don't delete it outright in this pass — it's still useful for
  someone verifying the claim before v1 ships, just not meant to survive past it.
- **Cites `DESIGN.md` or `README.md`** — these point at the actual persisted design,
  not a build-time failure log, so the citation itself is allowed to stay. But a bare
  `see DESIGN.md` forces a reader to search the whole file, and forces the comment to
  be revisited every time DESIGN.md grows a new section above it. **Tighten every one
  to a specific anchor** — `DESIGN.md#<Heading>#<Subheading>` (or the closest the doc's
  own heading structure supports) — so the pointer stays correct without editing the
  comment as the file is appended to.

Below, each entry's existing citations are re-classified under this policy as
**→ TODO: Remove** (dev-trace doc) or **→ Anchor to DESIGN.md#...** (durable doc,
needs a real anchor, not a bare filename). Most entries here cite dev-trace docs only.

---

## src/gltf.hpp

- **L122–136** (`baseColorTextureFileDataId` doc): cites "Luna's own observation,
  `BLENDER_EXPORT_TODO.md` §4". **Cat 5/3 → TODO: Remove** (personal-observation +
  TODO-file citation; keep only the field's actual contract, what 0 means).
- **L224–234** (`Skeleton` struct doc, multi-root paragraph): "35% of a real 130k-file
  corpus, `tools/find_multiroot_skeletons.py`". **Cat 5 → TODO: Remove** (scan-tool
  citation; state only the invariant, "more than one root is legal, never rejected").
- **L271–282** (`CorrectionSet` doc): cites `TODO_correctness.md #3`,
  `WIKI_FINDINGS.md §4` (**→ TODO: Remove**, both), and `DESIGN.md`'s Non-goals
  (**→ Anchor to DESIGN.md#Non-goals**, tighten from the bare mention). **Cat 5/6**.
- **L335–348** (`Attachment` struct intro): a deleted TODO file's former item number,
  plus "`M2_COMPLETENESS.md` used to call this...". **Cat 5 → TODO: Remove** — dead
  historical weight either way, `M2_COMPLETENESS.md` here is being cited for its past
  wording, not its current design content.
- **L425–440** (`SequenceMetadata` doc): "confirmed against 157 real alias sequences
  ... zero cycles." **Cat 5 → TODO: Remove** — no doc citation, just raw investigation
  numbers; delete outright rather than mark, nothing to point at.
- **L464–476** (`zUpToYUp` doc): cites `TRANSFORM_TRIAGE.md` for the full "earlier,
  hand-derived version had a sign bug" story. **Cat 5, textbook → TODO: Remove.** Keep
  only the current formula and the determinant invariant.
- **L483–487, L490–496** (`rotationZUpToYUp`/`scaleZUpToYUp` docs): each repeats
  "see `TRANSFORM_TRIAGE.md` §5a". **Cat 6 → TODO: Remove**, all three instances.
- **L577–644** (`writeGlbMulti` doc): "3,807 real corpus files have zero vertices"
  (**→ TODO: Remove**, raw corpus stat, no anchor to attach it to even if it were
  DESIGN.md); a Khronos GitHub issue number (**→ TODO: Remove**, external tracker,
  not a project doc). **Cat 3/5** — keep the preconditions/throws/index-formula only.

## src/gltf.cpp

- **L14–18, L23–25, L38–45** (`Mat3`/`kWowToGltf`/`static_assert`): each references
  `TRANSFORM_TRIAGE.md`. **Cat 5/6 → TODO: Remove**, all three — duplicates gltf.hpp's
  own citations a third/fourth time.
- **L370–379** (`hasSyntheticRoot`): re-explains gltf.hpp's `writeGlbMulti` rationale
  verbatim, no new citation of its own. **Cat 6 → TODO: Remove** the duplicated prose;
  if a pointer is wanted, one line: "see `writeGlbMulti`'s doc comment."
- **L441–446** (`skin.skeleton` comment): repeats the Khronos-issue citation.
  **Cat 6 → TODO: Remove.**
- **L758–762, L767–777, L874–875** (texture-type/FileDataID/geoset-metadata extras
  comments): restate the matching gltf.hpp field doc almost word for word, no
  independent citation. **Cat 6 → TODO: Remove** the duplicated prose; collapse to a
  one-clause pointer at the struct doc.

## src/m2.hpp

- **L144–166** (`textureCombinerCombos` doc): "A full 130,576-file local-corpus scan
  found zero real files with this flag set." **Cat 5 → TODO: Remove** — raw scan
  result, no doc citation to redirect, not needed to use the field.
- **L401–429** (`Sequence` struct doc): full stride-derivation writeup, plus a deleted
  TODO's "former Item 1." **Cat 5, textbook → TODO: Remove.** Keep the offset table.
- **L452–460** (`aliasNext` doc): "confirmed against 157 real alias sequences...
  zero cycles." **Cat 5 → TODO: Remove** — no citation, raw investigation narrative.
- **L481–488** (`Color` doc): cites `FAILURES2.md #7`. **Cat 5 → TODO: Remove.**
- **L507–529** (`TextureTransform` doc): describes the project's own verification
  methodology in prose, no doc citation. **Cat 3 → TODO: Remove** — process
  description; if it should live anywhere, it's `DESIGN.md#Coding-Policy` (this
  project's methodology section), not repeated per-struct.
- **L626–645** (`ParticleEmitter` doc): "decoded colors form a real fire/ember
  gradient..." **Cat 5 → TODO: Remove** — raw verification narrative, no citation.
- **L711–733** (`ExtendedParticle` doc): the most severe single offender — a full
  session's investigation log, ending "See `WIKI_FINDINGS.md`". **Cat 5/6, severe
  → TODO: Remove** — confirms its own permanent home already exists elsewhere.
- **L1166–1181** (`kMinVerifiedRecordStrideVersion` doc): cites `FAILURES2.md #3`.
  **Cat 5 → TODO: Remove.**
- **L990–1006** (`FBlockMeta` doc): cites `WIKI_FINDINGS.md`. **Cat 5 → TODO: Remove.**

## src/m2.cpp

- **L914–929** (`constantTrackValueOffset` doc): "an earlier version of this code
  took element [0][0] unconditionally... would have silently rendered the entire
  model invisible." **Cat 5, textbook → TODO: Remove** — no doc citation at all, the
  rubric's own named example of a commit-message-only fact; delete outright.

## src/cmd_info.cpp

- **L92–98** (catch-block in `info()`): cites `FAILURES.md #1`. **Cat 5
  → TODO: Remove.**
- **L181–184**: cites `FINDINGS.md §3.1`. **Cat 5, textbook → TODO: Remove.**
- **L304–312**: no dev-trace-doc citation, but does cross-reference "README's own
  🚧/📖 symbol convention" for the format matrix. **Cat 5/3 → TODO: Remove** the
  "until this session" narrative; **→ Anchor to README.md#<format-matrix-section>**
  if the symbol-convention cross-reference is worth keeping at all (it's describing
  a convention defined once in README, so a tightened pointer there is legitimate;
  the surrounding history is not).

## src/cmd_dump.cpp

- **L17–57** (file-level doc): folds in a deleted TODO file's former item number
  (**→ TODO: Remove**) and repeats the ribbon/particle "no glTF slot" rationale
  already in gltf.hpp (**Cat 6 → TODO: Remove** the duplicate, keep one canonical
  copy at the `Skeleton`/`EmitterAnchor` doc in gltf.hpp).
- **L322–335** (`dumpWfv3`): cites `WIKI_FINDINGS.md §8`. **Cat 5 → TODO: Remove.**
- **L446–453** (`dumpTexl`): cites `FAILURES2.md #5`. **Cat 5, textbook
  → TODO: Remove.**
- **L474–483** (`dumpAfra`): "no struct documented at all as of the 2026-07-25
  fetch... now stale: see `WIKI_FINDINGS.md`'s AFRA section." **Cat 5
  → TODO: Remove** — dated fetch note plus a findings-doc citation, both dev-trace.
- **L497–512** (`dumpDpiv`): "Byte-decoded from scratch against all 2,632 real
  corpus hits... now corrected." **Cat 5, severe → TODO: Remove** — full
  investigation narrative where a field-layout comment belongs.
- **L529–539, L553–563** (`dumpWfv1`/`dumpWfv2`): both narrate corpus-hit counts and
  compare confidence to a sibling investigation, no anchorable citation.
  **Cat 5 → TODO: Remove**, both.
- **L1328–1349** (`physPayloadRealLength`/`dumpPfdc`): local-extraction-gap vs.
  CASC-scan narrative, FileDataID pulled and decoded inline. **Cat 5, severe
  → TODO: Remove.**
- **L1414–1438** (`dumpPcol`): cites `WIKI_FINDINGS.md §10`. **Cat 5
  → TODO: Remove.**
- **L897–901** (`writePhysShape`): cites `WIKI_FINDINGS.md §9`. **Cat 6
  → TODO: Remove** — same fact restated from phys.hpp/phys.cpp, remove all but one.

## src/cmd_export.cpp (heaviest concentration, 48 citation hits)

- **L63–72, L74–79** (`toGltf(m2::Quat)`/`toGltfScale` docs): "an earlier,
  independently hand-derived version... shared the sign bug." **Cat 5, textbook
  → TODO: Remove** — no citation, raw history; delete outright.
- **L87–129** (`repairDuplicateTimestampsAndValidate`, ~40 lines): mixes a
  DESIGN.md-worthy algorithm tradeoff (nudge vs. collapse) with "found on 5 real
  files..." **Cat 3/4/5** — the tradeoff reasoning **→ Anchor to
  DESIGN.md#Key-design-decisions** (move it there for real, then point at it); the
  file-count finding **→ TODO: Remove** outright, no doc backs it.
- **L249–263** (`kSequenceStoredInlineFlag`/`kSequenceAliasFlag`): two-stage history
  of what the wiki used to say and what husk used to assume, no citation.
  **Cat 5 → TODO: Remove.**
- **L293–300, L318–326** (`buildSequenceMetadata`/`M2AnimInputs` docs): cite a
  deleted TODO's former item number. **Cat 3/5 → TODO: Remove**; the cross-file
  plumbing description, if kept anywhere, belongs at
  **DESIGN.md#CLI-argument-grammar-for-export** or wherever `--anim`/`.skel`
  cooperation is (or should be) documented.
- **L359–363** (`findAnimFileByBasename`): cites `WIKI_FINDINGS.md §2`. **Cat 5
  → TODO: Remove.**
- **L432–449** (`buildGlobalSequenceAnimations`): cites `FAILURES2.md #7`. **Cat 5,
  textbook → TODO: Remove.**
- **L495–552** (`buildAnimations`, ~58 lines — largest single comment block in the
  file): a multi-page design essay citing `WIKI_FINDINGS.md §2` twice. **Cat 3/4,
  severe.** The citations **→ TODO: Remove**; the load-bearing invariant (0x20 must
  be checked before alias-chain resolution, or 31/38 real aliases get the wrong data
  substituted) stays as one line; the rest of the essay **→ Anchor to
  DESIGN.md#Key-design-decisions** once confirmed captured there.
- **L920–955** (`FuzzyTexturePool`): "Luna's own observation, 2026-08-01... a
  follow-up the same session..." **Cat 5, textbook → TODO: Remove** — a dated
  session narrative, no doc citation, delete outright.
- **L1234–1265**, esp. **L1257–1265**: "an initial version of this fix tried the
  fuzzy pool before the FileDataID fallback for every slot... reassigned the named
  file to an unrelated hardcoded slot." **Cat 5, textbook → TODO: Remove** — no
  citation, delete outright.
- **L1320–1339** (additional-texture-layers): cites a deleted TODO's former item
  number and `WIKI_FINDINGS.md §7`. **Cat 5 → TODO: Remove**, both.
- **L1371–1373, L2255–2311** (geoset-selection / multi-texture / tint-fade /
  texture-transform notes): each cites a different findings-doc item.
  **Cat 6 → TODO: Remove**, all — collapse the surviving rationale into gltf.hpp's
  struct doc, which already states it once.
- **L1508–1524** (`findSameBasenameSkins`): "a real corpus scan found this silently
  pairs a model with a *different* model's skin... `world/nodxt/detail`'s
  vebgrs*/vebbsh*..." **Cat 5 → TODO: Remove** the corpus receipts; the choice
  itself (prefer 2-digit suffix) stays as a one-line invariant.

## src/phys.hpp

- **L8–41** (file-level doc): cites `WIKI_FINDINGS.md §9` seven times across four
  paragraphs, embedding a verified/unverified coverage table. **Cat 3/5/6
  → TODO: Remove** every citation; if the coverage table is worth keeping at all,
  it belongs at **DESIGN.md#Boundaries** or `M2_COMPLETENESS.md` (a durable
  completeness matrix, not a build-trace doc) — keep only the byte-layout facts
  (tag reversal, SHOJ ambiguity rule) inline.
- **L184–192** (`parse()` doc): restates the file header's claim a third time, same
  citation. **Cat 6 → TODO: Remove.**

## src/phys.cpp

- **L207–212** (`parsePolytopes`): cites `WIKI_FINDINGS.md §9`. **Cat 5/6
  → TODO: Remove.**
- **L356–362** (SHOJ disambiguation): cites `WIKI_FINDINGS.md §9`, "this session."
  **Cat 5 → TODO: Remove.**
- **L460–463** (`validateReferences`): fourth restatement of the same citation in
  this file+header pair. **Cat 6 → TODO: Remove.**

## src/skel.hpp

- **L46–48** (BFID chunk bullet): cites `WIKI_FINDINGS.md §4`/`TODO_correctness.md #3`.
  **Cat 5, minor → TODO: Remove**, both.

## src/skel.cpp

- **L126–129, L157–159**: both cite `FAILURES2.md #8` to justify the same truncation
  check, independently, in three places across two files. **Cat 6 → TODO: Remove**
  all three citations; state the rule ("byte length must divide evenly, don't
  silently drop a partial record") once as a plain invariant with no doc pointer
  needed at all.

## src/skin.hpp

- **L74–78** (`textureCoordComboIndex` doc): cites `WIKI_FINDINGS.md §7`.
  **Cat 5/6, minor → TODO: Remove** — duplicates a fuller version of the same fact
  in `cmd_export.cpp`'s `M2MaterialInputs::textureCoordCombos` doc; remove both
  citations, keep one plain statement of the out-of-range case at whichever site
  survives.

## Files with no material violations

`chunk.hpp`, `chunk.cpp`, `bone.hpp`, `bone.cpp`, `json_writer.hpp`, `commands.hpp`,
`main.cpp` — stay at "why/invariant/gotcha" altitude throughout, and cite nothing
outside the code itself (e.g. `main.cpp`'s zsh-quoting-nesting explanation,
`chunk.hpp`'s "M2 tags are not byte-reversed, unlike WMO/ADT" gotcha, `bone.hpp`'s
reverse-engineered-format disclaimer). Nothing to mark here under the v1 policy.

## tests/

Read in full: `test_cli.cpp`, `test_gltf.cpp`, `test_dump.cpp`, `test_integration.cpp`,
`test_conformance.cpp`, `test_data_paths.hpp`, `test_m2.cpp`, `test_skel.cpp`,
`test_skin.cpp`, `test_phys.cpp`, `test_bone.cpp`, `test_chunk.cpp`, `test_main.cpp`,
`run_husk.hpp`, `blender_import_check.py`.

### VIOLATION-A here: dev-trace-doc citations → all `TODO: Remove`

Same disposition as `src/`'s Category 5/6 findings — these cite build-time failure
logs and investigation docs, not the persisted design, so none of them survive to
v1. **~95 distinct sites**, roughly half of them in `test_cli.cpp` alone. Not every
site is enumerated individually (same "terse one-clause pointer" carve-out as the
`src/` pass) — the concentrations:

- **`test_cli.cpp`** — heaviest offender. File header cites `FAILURES.md #5`; dozens
  of `TEST_CASE` names and inline comments cite `FAILURES.md #1–4`,
  `FAILURES2.md #1/#3/#4/#6/#7/#9`, `FINDINGS.md §2.3/§3.1/§3.2/§4.2/§4.3`,
  `BLENDER_EXPORT_TODO.md §3/§4/§5`, `RO_COMPLETENESS_TODO.md`, `M2_GAPS_TODO.md`,
  `WIKI_FINDINGS.md §2/§12`, `TODO_correctness.md #3`. **→ TODO: Remove**, all.
  One site is worse than a comment: **L3553** has a `CHECK` that asserts husk's own
  *runtime stderr output* contains the literal string `"FAILURES2.md #6"` — a
  dev-trace citation baked into pinned CLI behavior, not just a comment. This needs
  a real fix, not just a marker: either husk's own `--help`/note text drops the
  citation (preferred — user-facing CLI output should never name an internal
  findings doc) or, if the test only cares that *a* note fires, the assertion should
  match on stable behavior text, not the doc citation substring.
- **`test_gltf.cpp`** — `TRANSFORM_TRIAGE.md` cited repeatedly (L46–57, 58, 66–67,
  108–120, 1108–1113) with "the formula this test used to assert... before the fix"
  narrative; also `FAILURES2.md #1/#2/#6`, `FINDINGS.md §3.1`, `WIKI_FINDINGS.md §4`,
  `M2_GAPS_TODO.md`. **→ TODO: Remove**, all.
- **`test_dump.cpp`** — file header (L1–27) and several bodies cite
  `FINDINGS.md §4.4`, `WIKI_FINDINGS.md §8/§9/§11/§13`, `FAILURES2.md #5`,
  `RO_COMPLETENESS_TODO.md`, with "used to"/"this session" narrative throughout.
  **→ TODO: Remove**, all.
- **`test_integration.cpp`** — `WIKI_FINDINGS.md §2/§4/§12/§13`,
  `TODO_correctness.md #3`, `M2_GAPS_TODO.md`, plus dated narrative baked directly
  into `TEST_CASE` names (L101–102, L146–148). **→ TODO: Remove**, all — the
  `TEST_CASE` names themselves should be renamed to describe current behavior, not
  "used to fail because of X."
- **`test_conformance.cpp`** — `WIKI_FINDINGS.md §5`, `TRANSFORM_TRIAGE.md §2/§5c/§5e`,
  `BLENDER_EXPORT_TODO.md §8`. **→ TODO: Remove**, all.
- **`test_data_paths.hpp`** — repeated `WIKI_FINDINGS.md §2/§4/§9/§10/§13`,
  `TODO_correctness.md #3`, `TRANSFORM_TRIAGE.md §5e`, dated "this session"/"a full
  4112-file scan" narrative. **→ TODO: Remove**, all.
- **`test_m2.cpp`** — `FAILURES2.md #7/#8`, `FAILURES.md #2`, a deleted TODO's
  former Item 1 (including baked into a `TEST_CASE` name). **→ TODO: Remove**, all
  (rename the `TEST_CASE` the same way as `test_integration.cpp`'s above).
- **`test_skel.cpp`/`test_skin.cpp`/`test_phys.cpp`** — each cite
  `FAILURES2.md #8/#1` and/or `WIKI_FINDINGS.md §9`. **→ TODO: Remove**, all.
- **`blender_import_check.py`** — cites `TRANSFORM_TRIAGE.md §5c` twice.
  **→ TODO: Remove**, both.
- A handful of sites carry "used to"/"previously"/"an earlier version" narrative
  with no doc name at all: `test_integration.cpp` L101–102, L146–148;
  `test_cli.cpp` L3096, L3126, L2593–2602. **Cat 5 → TODO: Remove**, all — delete
  outright, nothing to anchor even under the durable-doc exception.

### VIOLATION-B here: scattered test-suite-design rationale → coalesce into TEST_DESIGN.md

Six topics, each restated near-verbatim across multiple files — see the newly
drafted `TEST_DESIGN.md` (repo root) for the coalesced version. Once that file
exists, every site listed below should shrink to a one-line pointer
(`// see TEST_DESIGN.md#<section>`) or be deleted if the surrounding code is
self-evident without it:

1. **Four-tier test architecture** (unit / CLI / integration / conformance) — near-
   identical taxonomy in `test_cli.cpp` L1–4, `test_integration.cpp` L1–6,
   `test_conformance.cpp` L1–11, `test_dump.cpp` L1–4, `run_husk.hpp` L9–14, each
   cross-referencing the others by name. **→ TEST_DESIGN.md#Four-tier-architecture.**
2. **`doctest::skip` + `HUSK_TEST_*` env-var fixture-resolution model** — full
   explanation repeated in `test_data_paths.hpp` L3–20, `test_main.cpp` L1–10,
   `test_integration.cpp` L8–15 (near-verbatim restatement), `test_conformance.cpp`
   L12–18. **→ TEST_DESIGN.md#Fixture-resolution-model.**
3. **`#ifdef HUSK_GLTF_VALIDATOR`/`HUSK_BLENDER` "not found on PATH at configure
   time" boilerplate** — the exact same comment copy-pasted **10 times** within
   `test_conformance.cpp` alone (L238–240, 273–275, 306–308, 342–344, 431–432,
   487–488, 569–570, 628–629, 667–669, 715–716). **→ TEST_DESIGN.md#Conformance-gating**,
   stated once; every site collapses to nothing (the `#ifdef` itself is
   self-explanatory once the convention is documented centrally).
4. **"Prove a regression test actually regresses" (mutation-testing) discipline** —
   restated as prose in `test_cli.cpp` L1159–1162, `test_conformance.cpp` L507–511,
   `test_integration.cpp` L176–183, `test_m2.cpp` L1042–1046.
   **→ TEST_DESIGN.md#Mutation-tested-regressions.**
5. **Independent-transcription-of-wire-offsets convention** — `test_m2.cpp` L1–6
   states it fully; `test_skin.cpp` L1–5 and `test_skel.cpp` L1–4 explicitly say
   "same rationale as `tests/test_m2.cpp`"; `test_cli.cpp` L790–793 restates a
   variant. **→ TEST_DESIGN.md#Independent-transcription-convention.**
6. **"Shape-only" real-data checks vs. exact synthetic-fixture checks** — corollary
   of #1, stated 3+ times within `test_integration.cpp` (L1–6, 65–68, 479–485) and
   echoed in `test_conformance.cpp` (L77–81, 89–94, 142–178).
   **→ TEST_DESIGN.md#Shape-only-vs-exact-checks.**

## Summary count under the v1 policy

**`src/`**: of the ~55 flagged blocks, **all but ~4 resolve to `TODO: Remove`
outright** (no citation worth anchoring, or a citation pointing only at a dev-trace
doc). The exceptions that keep a citation, tightened to an anchor instead of a bare
filename:

- `gltf.hpp`'s `CorrectionSet` doc → `DESIGN.md#Non-goals`
- `cmd_info.cpp` L304–312's README symbol-convention reference →
  `README.md#<format-matrix-section-heading>`
- `cmd_export.cpp`'s `repairDuplicateTimestampsAndValidate` nudge-vs-collapse
  rationale → `DESIGN.md#Key-design-decisions` (once actually moved there)
- `cmd_export.cpp`'s `buildAnimations` essay → `DESIGN.md#Key-design-decisions`
  (same, once moved)

**`tests/`**: ~95 VIOLATION-A sites, **all `TODO: Remove`** (no test-file citation
pointed at a durable doc worth anchoring — every one names a dev-trace doc), plus
one that needs an actual code fix, not just a marker
(`test_cli.cpp` L3553's stderr-substring `CHECK` on `"FAILURES2.md #6"`). Separately,
**6 VIOLATION-B topics**, ~25+ individual sites, coalesce into the new
`TEST_DESIGN.md` (drafted alongside this file) — each site trims to a one-line
pointer or disappears once the shared doc exists.

Everything marked `TODO: Remove` in this file is dev-scaffolding by the policy's own
definition: mark `// TODO: Remove`, don't delete yet, and sweep them out as a batch
pass before v1 ships. Everything marked for `TEST_DESIGN.md` should be moved there in
the same pass, not before — writing the doc without pruning the inline copies would
just add a fourth or fifth restatement instead of replacing three.
