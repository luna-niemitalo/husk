# wowdev.wiki findings — `M2/.skin`

Current, correct facts only. Full evidence trail: `../../WIKI_FINDINGS_HISTORY.md`
§7.

---

## Multi-texture-layer arithmetic — verified — history §7

`.skin`'s batch `textureCount > 1` handling (wiki prose: "if textureCount is
e.g. 3 and the texunit's uv anim lookup is 2, then the 3 uv animation
lookups are 2, 3, and 4") is confirmed **byte-for-byte exact** against a
real multi-layer file
(`world/replaceabletextureprops/guild/pennant_guild_alliance_a_01.m2`, 6
layers) — an independent from-scratch parser and husk's real `husk export`
output resolve to identical FileDataIDs, including the legitimate `0`
entries for client-side "replaceable" texture types. `textureCount > 1`
batches are common in the real corpus — 226,294 of 287,005 real `.skin`
files (~79%).

## `textureCoordCombos` — confirmed present, value range not as documented — hypothesis (semantics) — history §7

Real `nonzero textureCoordCombos` arrays exist (3 real files found in a
130,576-file `.m2` scan, all Warlords doodad/creature models) but their real
values (`[33, 34]` on the one file hand-verified) don't match the wiki's
documented `-1/0/1` range at all. Every real batch's `textureCoordComboIndex`
is 0 in the checked file, so `cmd_export.cpp`'s existing `mapping == 1`
special case never fires and the safe UV-set-0 fallback is what actually
runs — confirmed correct, not a bug, but the wiki's stated value range for
this table is now known to be an oversimplification for at least these
files. Best read as genuinely-present-but-largely-vestigial data, not
confirmed against any authoritative source.
