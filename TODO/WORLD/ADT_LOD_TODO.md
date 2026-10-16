# TODO: ADT terrain LOD — what's left of the Legion+ `ML*` family

**Status: an open punch list, not a historical record.** Fixed items get
removed outright; git history is the record.

Done (2026-10-16, `REFACTOR/BUNDLE_FORMAT.md` "Levels of detail"):
`_lod.adt`'s terrain bands (`MLVH`/`MLVI`/`MLLL`/`MLSI`) as terrain levels
1–4 with the tile's map textures, and `_obj1.adt`'s placement extents
(`MLDD`/`MLDX`/`MLMD`/`MLMX`/`MLFD`) as each instance's `distant`.

Open, in order:

1. **What an `MLFD` band means.** The three bands split the distant list
   (azeroth_32_49: all 1,410 doodads in band 0; WMOs 6 in band 0, 2 in
   band 2). Not radius (each band is radius-sorted on its own) and not
   the WMO's own level count. Carried raw as `distant.source.band`.
2. **How the client turns `radius` into a draw distance.** Carried raw.
3. **Liquid LOD** (`MLLD`/`MLLN`/`MLLV`/`MLLI`, present in 52% of a
   2,000-file sample). `MLLD`'s depth/alpha blob is an undecoded RLE
   format (the wiki gives decompiled pseudocode only); `MLLN.num_indices`
   isn't cross-checked against `MLLI` yet.
4. **WDL** (`<map>.wdl`): the map-wide coarsest terrain, beyond the
   `_lod.adt` range. Not parsed.
5. **Corpus check of the band/quad-tree identity.** Each `MLLL` range
   equals the union of one `MLND` depth on azeroth_32_49 only; husk draws
   bands from `MLLL` and never reads `MLND`. Check a seeded sample.
6. **Blend meshes** (`MBMH`/`MBBB`/`MBNV`/`MBMI`/`MBMB`, 5.5% of sampled
   `_lod.adt`). Upstream semantics are guesses; not parsed.
7. **Opaque `_obj1` chunks**: `MLDL` (value only on `MLDD` flag 0x8),
   `MLMB`, `MLDB`, and the undocumented `MDLI`/`MDLD` (1.5% of sampled
   `_obj1.adt`). Not parsed.
