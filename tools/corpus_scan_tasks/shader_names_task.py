"""Real M2Batch shader-*name* corpus scan, for TODO/MULTI_TEXTURE_LAYER_TODO.md
step 0 and TODO/PIXEL_SHADER_FORMULAS_TODO.md step 1.

shader_id_task.py already answered the raw-bits question (how often the
0x8000 table-lookup path fires, how often textureCount > 1). This task goes
one step further and resolves each batch's (shaderId, textureCount) pair to
its real {pixel, vertex} shader *name* pair, the exact same way
husk::m2::resolveShaderNames (src/m2_shader_names.cpp) does -- transcribed
here in Python rather than shelling out to husk, since this is pure
struct-unpack + table-lookup work with no other husk machinery needed (same
call `shader_id_task.py` already made for the raw-bits scan).

Answers two open questions with real numbers instead of guesses:

  - MULTI_TEXTURE_LAYER_TODO.md step 0: how many real batches resolve to an
    `_Env`-bearing vertex shader (an env-mapped "shiny metal" pass), out of
    the stale 3/130,576 figure that motivated re-running this at full scale.
  - PIXEL_SHADER_FORMULAS_TODO.md step 1: which of the 17 wowdev.wiki-
    undocumented `Combiners_*` pixel shaders actually get exercised by real
    corpus files, and a few example file paths for each one found.

Run against the real corpus with:

    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.shader_names_task:ShaderNamesTask \\
        --root /media/luna/data/wow_export --output-stem shader_names_corpus
"""

import struct
from pathlib import Path

_BATCH_STRIDE = 0x18
_SHADER_ID_OFFSET = 0x02
_TEXTURE_COUNT_OFFSET = 0x0E

# s_modelShaderEffect, transcribed verbatim from src/m2_shader_names.cpp
# (which itself transcribes documentation/wowdev-wiki/wikitext/M2/.skin.wiki's
# "Shader table" section) -- kept in exact sync with that file; if it ever
# changes there, mirror the change here too.
_SHADER_EFFECT_TABLE = [
    ("Combiners_Opaque_Mod2xNA_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_AddAlpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_AddAlpha_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_Mod2xNA_Alpha_Add", "Diffuse_T1_Env_T1"),
    ("Combiners_Mod_AddAlpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_AddAlpha", "Diffuse_T1_T1"),
    ("Combiners_Mod_AddAlpha", "Diffuse_T1_T1"),
    ("Combiners_Mod_AddAlpha_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_Alpha_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_Mod2xNA_Alpha_3s", "Diffuse_T1_Env_T1"),
    ("Combiners_Opaque_AddAlpha_Wgt", "Diffuse_T1_T1"),
    ("Combiners_Mod_Add_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_ModNA_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Mod_AddAlpha_Wgt", "Diffuse_T1_Env"),
    ("Combiners_Mod_AddAlpha_Wgt", "Diffuse_T1_T1"),
    ("Combiners_Opaque_AddAlpha_Wgt", "Diffuse_T1_T2"),
    ("Combiners_Opaque_Mod_Add_Wgt", "Diffuse_T1_Env"),
    ("Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T1"),
    ("Combiners_Mod_Dual_Crossfade", "Diffuse_T1_T1_T1"),
    ("Combiners_Mod_Depth", "Diffuse_EdgeFade_T1"),
    ("Combiners_Mod_AddAlpha_Alpha", "Diffuse_T1_Env_T2"),
    ("Combiners_Mod_Mod", "Diffuse_EdgeFade_T1_T2"),
    ("Combiners_Mod_Masked_Dual_Crossfade", "Diffuse_T1_T1_T1_T2"),
    ("Combiners_Opaque_Alpha", "Diffuse_T1_T1"),
    ("Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T2"),
    ("Combiners_Mod_Depth", "Diffuse_EdgeFade_Env"),
    ("Guild", "Diffuse_T1_T2_T1"),
    ("Guild_NoBorder", "Diffuse_T1_T2"),
    ("Guild_Opaque", "Diffuse_T1_T2_T1"),
    ("Illum", "Diffuse_T1_T1"),
]

# wowdev.wiki's Pixel_shader_logic_for_mixing_colors.wiki has no formula
# section at all for these 17 -- see PIXEL_SHADER_FORMULAS_TODO.md.
_UNDOCUMENTED_PIXEL_SHADERS = frozenset({
    "Combiners_Opaque_Mod2xNA_Alpha_Add",
    "Combiners_Opaque_Mod2xNA_Alpha_3s",
    "Combiners_Opaque_AddAlpha_Wgt",
    "Combiners_Mod_Add_Alpha",
    "Combiners_Opaque_ModNA_Alpha",
    "Combiners_Mod_AddAlpha_Wgt",
    "Combiners_Opaque_Mod_Add_Wgt",
    "Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha",
    "Combiners_Mod_Dual_Crossfade",
    "Combiners_Opaque_Mod2xNA_Alpha_Alpha",
    "Combiners_Mod_Masked_Dual_Crossfade",
    "Combiners_Opaque_Alpha",
    "Guild",
    "Guild_NoBorder",
    "Guild_Opaque",
    "Combiners_Mod_Depth",
    "Illum",
})


def _pixel_shader_from_formula(shader_id: int, op_count: int) -> str:
    if op_count == 1:
        return "Combiners_Mod" if (shader_id & 0x70) else "Combiners_Opaque"
    lower = shader_id & 7
    if shader_id & 0x70:
        return {0: "Combiners_Mod_Opaque", 3: "Combiners_Mod_Add", 4: "Combiners_Mod_Mod2x",
                6: "Combiners_Mod_Mod2xNA", 7: "Combiners_Mod_AddNA"}.get(lower, "Combiners_Mod_Mod")
    return {0: "Combiners_Opaque_Opaque", 3: "Combiners_Opaque_AddAlpha", 4: "Combiners_Opaque_Mod2x",
            6: "Combiners_Opaque_Mod2xNA", 7: "Combiners_Opaque_AddAlpha"}.get(lower, "Combiners_Opaque_Mod")


def _vertex_shader_from_formula(shader_id: int, op_count: int) -> str:
    if op_count == 1:
        if shader_id & 0x80:
            return "Diffuse_Env"
        return "Diffuse_T2" if (shader_id & 0x4000) else "Diffuse_T1"
    if shader_id & 0x80:
        return "Diffuse_Env_Env" if (shader_id & 0x8) else "Diffuse_Env_T1"
    if shader_id & 0x8:
        return "Diffuse_T1_Env"
    return "Diffuse_T1_T2" if (shader_id & 0x4000) else "Diffuse_T1_T1"


def resolve_shader_names(shader_id: int, op_count: int) -> tuple[str, str] | None:
    """Mirrors husk::m2::resolveShaderNames (src/m2_shader_names.cpp) exactly."""
    if shader_id & 0x8000:
        index = shader_id & ~0x8000
        if index >= len(_SHADER_EFFECT_TABLE):
            return None  # out of range, don't guess -- same as the C++ side
        return _SHADER_EFFECT_TABLE[index]
    return _pixel_shader_from_formula(shader_id, op_count), _vertex_shader_from_formula(shader_id, op_count)


def _parse_batches(data: bytes) -> list[tuple[int, int]] | None:
    if data[:4] != b"SKIN":
        return None
    batches_field_offset = 4 + 8 * 4
    if batches_field_offset + 8 > len(data):
        return None
    count, off = struct.unpack_from("<II", data, batches_field_offset)
    if count == 0 or off > len(data) or count > (len(data) - off) // _BATCH_STRIDE:
        return None
    out = []
    for i in range(count):
        base = off + i * _BATCH_STRIDE
        shader_id, = struct.unpack_from("<H", data, base + _SHADER_ID_OFFSET)
        texture_count, = struct.unpack_from("<H", data, base + _TEXTURE_COUNT_OFFSET)
        out.append((shader_id, texture_count))
    return out


class ShaderNamesTask:
    GLOB_PATTERNS = ["*.skin"]
    FIELDNAMES = ["batches", "resolved_batches", "tc_gt1_batches", "env_vertex_batches",
                  "undocumented_pixel_batches", "undocumented_names"]
    PARALLEL_MODE = "thread"  # plain struct.unpack, same as shader_id_task.py
    BATCH_SIZE = 32

    @staticmethod
    def analyze(path: Path) -> dict | None:
        try:
            data = path.read_bytes()
        except OSError:
            return None
        batches = _parse_batches(data)
        if not batches:
            return None

        resolved = 0
        tc_gt1 = 0
        env_vertex = 0
        undocumented = 0
        undocumented_names: set[str] = set()
        for shader_id, tc in batches:
            names = resolve_shader_names(shader_id, tc)
            if tc > 1:
                tc_gt1 += 1
            if names is None:
                continue
            resolved += 1
            pixel, vertex = names
            if "_Env" in vertex:
                env_vertex += 1
            if pixel in _UNDOCUMENTED_PIXEL_SHADERS:
                undocumented += 1
                undocumented_names.add(pixel)

        return {
            "batches": len(batches),
            "resolved_batches": resolved,
            "tc_gt1_batches": tc_gt1,
            "env_vertex_batches": env_vertex,
            "undocumented_pixel_batches": undocumented,
            "undocumented_names": ";".join(sorted(undocumented_names)),
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        total_batches = sum(r["batches"] for r in rows)
        resolved = sum(r["resolved_batches"] for r in rows)
        tc_gt1 = sum(r["tc_gt1_batches"] for r in rows)
        env_vertex = sum(r["env_vertex_batches"] for r in rows)
        undocumented = sum(r["undocumented_pixel_batches"] for r in rows)

        per_name_files: dict[str, list[str]] = {}
        for r in rows:
            if not r["undocumented_names"]:
                continue
            for name in r["undocumented_names"].split(";"):
                per_name_files.setdefault(name, []).append(r["path"])

        lines = [
            f"{len(rows)} / {total_files} .skin files had a parseable, non-empty batch array",
            f"{total_batches} total batches, {resolved} / {total_batches} ({resolved / total_batches:.2%}) resolved to a real shader name",
            f"{tc_gt1} / {total_batches} batches ({tc_gt1 / total_batches:.2%}) have textureCount > 1",
            f"{env_vertex} / {total_batches} batches ({env_vertex / total_batches:.2%}) resolve to an _Env-bearing vertex shader (env-mapped shine pass)",
            f"{undocumented} / {total_batches} batches ({undocumented / total_batches:.2%}) resolve to one of the 17 wowdev.wiki-undocumented pixel shaders",
            f"{len(per_name_files)} / 17 undocumented pixel shaders have at least one real corpus repro:",
        ]
        for name in sorted(per_name_files):
            files = per_name_files[name]
            lines.append(f"  {name}: {len(files)} file(s), e.g. {files[0]}")
        missing = _UNDOCUMENTED_PIXEL_SHADERS - per_name_files.keys()
        if missing:
            lines.append(f"  never resolved in this corpus: {', '.join(sorted(missing))}")
        return lines
