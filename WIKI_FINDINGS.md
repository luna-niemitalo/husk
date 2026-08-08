---
aliases:
  - WIKI_FINDINGS
---
# wowdev.wiki findings — index

Things husk's development turned up that go beyond, or correct, what
[wowdev.wiki](https://wowdev.wiki) currently documents (pages fetched
2026-07-24/25 via a local proxy — see `src/skel.hpp`'s doc comment for
dates). This file is an index only — **current, correct facts live in the
per-page files below**, mirroring `documentation/wowdev-wiki/md/`'s own
per-page layout so it's obvious which file corresponds to which wiki page.
The full evidence trail behind every fact (what got checked, against which
real files, what got corrected along the way) lives one step behind, in
`WIKI_FINDINGS_HISTORY.md` — read a topic file first for the current answer;
only open the history file when you need the receipts.

Confidence is called out per finding, same convention the README uses:
**verified** (checked against real game files, numbers included),
**inferred** (structurally justified but not cross-checked against a second
independent source), or **hypothesis** (plausible, not confirmed).

The local wowdev.wiki mirror itself (`documentation/wowdev-wiki/md/` —
gitignored, tool-fetched, not this file) has also been amended in place with
the corrections below, clearly delimited from the original mirrored text —
see `documentation/wowdev-wiki/HUSK_AMENDMENTS.md` for the index of what was
touched and why. The files below remain the tracked, canonical record.

## Per-page findings

| Page | Covers | Wiki page mirror |
|---|---|---|
| [`WIKI_FINDINGS/M2.md`](WIKI_FINDINGS/M2.md) | `M2Sequence` size, `bounding_box`, `M2Particle`/`FBlock`, `WFV3`, `WFV1`/`WFV2`/`DPIV`/`AFRA`/`PCOL`, `DETL`, `aliasNext`, `EXP2`/`PFDC`/`BLP2`, `global_flags`/`textureCombinerCombos` | `documentation/wowdev-wiki/md/M2.md` |
| [`WIKI_FINDINGS/M2/anim.md`](WIKI_FINDINGS/M2/anim.md) | Chunked `.anim`'s `AFM2`/`AFSB` shape | `documentation/wowdev-wiki/md/M2/.anim.md` |
| [`WIKI_FINDINGS/M2/skel.md`](WIKI_FINDINGS/M2/skel.md) | `.skel`'s `SKS1`/`SKB1` cross-chunk indexing, per-file `AFID` | `documentation/wowdev-wiki/md/M2/.skel.md` |
| [`WIKI_FINDINGS/M2/skin.md`](WIKI_FINDINGS/M2/skin.md) | `.skin` multi-texture-layer arithmetic, `textureCoordCombos` | `documentation/wowdev-wiki/md/M2/.skin.md` |
| [`WIKI_FINDINGS/BONE.md`](WIKI_FINDINGS/BONE.md) | `.bone` file format, correction-matrix semantics, slot-selection follow-ups | `documentation/wowdev-wiki/md/BONE.md` |
| [`WIKI_FINDINGS/PHYS.md`](WIKI_FINDINGS/PHYS.md) | `.phys` full struct verification | `documentation/wowdev-wiki/md/PHYS.md` |
| [`WIKI_FINDINGS/WORLD.md`](WIKI_FINDINGS/WORLD.md) | WMO/ADT/WDT/WDL/PM4/PD4 — first investigation pass, planning-stage, not yet implemented | `documentation/wowdev-wiki/md/{WMO,ADT,WDT,WDL,PM4,PD4}.md` |

## Where these live in husk

| Finding | Code | Tests |
|---|---|---|
| `M2Sequence` = 0x40 bytes | `src/m2.hpp`/`m2.cpp` (`Sequence`, `parseSequences`) | `tests/test_m2.cpp` |
| `AFSB` real resolution | `src/cmd_export.cpp` (`buildAnimations`'s external-file branch), `src/m2.cpp` (`resolveVec3TrackSequence`/`resolveQuatTrackSequence`/`trackSequenceInnerArrays`) | `tests/test_cli.cpp`, `tests/test_integration.cpp` |
| `.skel` `SKS1` indexing + own `AFID` | `src/skel.hpp`/`skel.cpp` (`parseSequences`, `boneTrackBlob`, `findAnimFileIds`) | `tests/test_skel.cpp`, `tests/test_cli.cpp` |
| `.bone` format | `src/bone.hpp`/`bone.cpp` | `tests/test_bone.cpp` |
| `bounding_box` containment | `src/cmd_export.cpp`, `tests/test_conformance.cpp` (`transformedM2BoundingBox`) | `tests/test_conformance.cpp` |
| `M2Particle` offsets + `FBlock` timestamps | `src/m2.hpp`/`m2.cpp` (`ParticleEmitter`, `parseParticles`, resolvers), `src/cmd_dump.cpp`, `src/cmd_export.cpp`/`gltf.hpp`/`gltf.cpp` (`EmitterAnchor`) | `tests/test_m2.cpp`, `tests/test_dump.cpp`, `tests/test_gltf.cpp`, `tests/test_integration.cpp` |
| Multi-texture-layer arithmetic | `src/cmd_export.cpp` (`buildMaterialsAndPrimitives`) | `tests/test_integration.cpp` (`checkMultiTextureLayerArithmetic`) |
| `WFV3` 64-byte short variant | `src/cmd_dump.cpp` (`dumpWfv3`) | `tests/test_dump.cpp` |
| `.phys` full parser | `src/phys.hpp`/`phys.cpp`, `src/gltf.hpp`/`gltf.cpp` (`PhysicsBody` extras), `src/cmd_export.cpp` (`--phys`), `src/cmd_dump.cpp` | `tests/test_phys.cpp`, `tests/test_gltf.cpp`, `tests/test_cli.cpp`, `tests/test_dump.cpp`, `tests/test_integration.cpp`/`test_conformance.cpp` |
| `WFV1`/`WFV2`/`DPIV`/`AFRA`/`PCOL` | `src/cmd_dump.cpp` (`dumpWfv1`/`dumpWfv2`/`dumpDpiv`/`dumpAfra`/`dumpPcol`) | `tools/find_m2_unknown_chunks.py`, `tests/test_dump.cpp` |
| `DETL` stride + padding; `flags` is the one live field, rest are dead constants | `src/cmd_dump.cpp` (`dumpDetl`, `readHalfFloat`, doc comment only for the `flags` finding) | `tools/check_detl_stride.py`, `tests/test_dump.cpp` |
| `aliasNext` chain resolution | `src/m2.hpp`/`m2.cpp` (`Sequence`), `src/cmd_export.cpp` (`resolveAliasChain`, `buildAnimations`), `src/gltf.hpp`/`gltf.cpp` (`SequenceMetadata`) | `tests/test_m2.cpp`, `tests/test_gltf.cpp`, `tests/test_cli.cpp`, `tests/test_integration.cpp`, `tools/check_alias_next.py` |
| `EXP2`/`PFDC`/`BLP2` | `src/m2.hpp` (`ExtendedParticle`), `src/cmd_dump.cpp`, `DESIGN.md` Non-goals | `tests/test_dump.cpp`, `tests/test_integration.cpp` |
| `global_flags`/`textureCombinerCombos`/`resolveSkin` | `src/m2.hpp`/`m2.cpp` (`GlobalFlag`, `globalFlagNames`, `Header::textureCombinerCombos`), `src/cmd_info.cpp`, `src/cmd_export.cpp` (`resolveSkin`) | `tests/test_cli.cpp`, `blp/tests/test_decode.py` |
| WMO/ADT/WDT/WDL/PM4/PD4 investigation | none yet — planning-stage only | none yet — see the eleven `*_TODO.md` files named in `WIKI_FINDINGS/WORLD.md` |
