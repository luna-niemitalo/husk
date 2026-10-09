"""Corpus-wide inventory of every shader-, blend-, opacity- and texture-
combination reference the client would act on, across M2 (+ its 00.skin),
WMO root/group, ADT root/_tex0, WDT and the client's own .bls shader
containers. Output feeds SHADER_FINDINGS/README.md.

Excavation task (tools/CORPUS_SCANS.md): reads raw bytes on purpose. husk
exposes none of this as structured output -- skin shaderId/batch fields and
the material/blend join are consumed only inside export_materials.cpp, WMO
materials and .bls containers are not parsed by husk at all. The batch shader
table mirrors src/m2_shader_names.cpp, so out-of-range indices here mean that
table is stale for the scanned build.
Offsets transcribe src/m2_primitives.cpp, src/m2_scene.cpp, src/skin.cpp and
src/adt.cpp; WMO/WDT/BLS layouts come from documentation/wowdev-wiki.

IO shape: one read per file. Skins are not globbed -- each M2 reads only its
own <base>00.skin (the full-detail LOD carries every batch the lower LODs
reuse), which is also what lets a batch be joined to its M2 material/blend/
texture records. WMO groups read only their 0x58-byte MOGP header, .bls only
its permutation table, ADT _obj0/_obj1/_lod are rejected by name with no IO.
FileDataIDs are resolved against the listfile once, in summarize(), in the
main process.

    direnv exec . tools/venv/bin/python tools/corpus_scan_framework.py \\
        --task corpus_scan_tasks.shader_inventory_task:ShaderInventoryTask \\
        --root /media/luna/data/wow_export --output-stem shader_inventory \\
        --output-dir SHADER_FINDINGS/scan
"""
from __future__ import annotations

import json
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import corpus_scan_framework as csf  # noqa: E402 -- see sys.path.insert above; LISTFILE read from there

AGGREGATE_PATH = csf.REPO_ROOT / "SHADER_FINDINGS" / "scan" / "shader_inventory_aggregate.json"

# s_modelShaderEffect, kept in exact sync with src/m2_shader_names.cpp (see its
# comment for sources) and shader_names_task.py.
EFFECTS = [
    ("Combiners_Opaque_Mod2xNA_Alpha", "Diffuse_T1_Env"), ("Combiners_Opaque_AddAlpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_AddAlpha_Alpha", "Diffuse_T1_Env"), ("Combiners_Opaque_Mod2xNA_Alpha_Add", "Diffuse_T1_Env_T1"),
    ("Combiners_Mod_AddAlpha", "Diffuse_T1_Env"), ("Combiners_Opaque_AddAlpha", "Diffuse_T1_T1"),
    ("Combiners_Mod_AddAlpha", "Diffuse_T1_T1"), ("Combiners_Mod_AddAlpha_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_Alpha_Alpha", "Diffuse_T1_Env"), ("Combiners_Opaque_Mod2xNA_Alpha_3s", "Diffuse_T1_Env_T1"),
    ("Combiners_Opaque_AddAlpha_Wgt", "Diffuse_T1_T1"), ("Combiners_Mod_Add_Alpha", "Diffuse_T1_Env"),
    ("Combiners_Opaque_ModNA_Alpha", "Diffuse_T1_Env"), ("Combiners_Mod_AddAlpha_Wgt", "Diffuse_T1_Env"),
    ("Combiners_Mod_AddAlpha_Wgt", "Diffuse_T1_T1"), ("Combiners_Opaque_AddAlpha_Wgt", "Diffuse_T1_T2"),
    ("Combiners_Opaque_Mod_Add_Wgt", "Diffuse_T1_Env"), ("Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T1"),
    ("Combiners_Mod_Dual_Crossfade", "Diffuse_T1"), ("Combiners_Mod_Depth", "Diffuse_EdgeFade_T1"),
    ("Combiners_Opaque_Mod2xNA_Alpha_Alpha", "Diffuse_T1_Env_T2"), ("Combiners_Mod_Mod", "Diffuse_EdgeFade_T1_T2"),
    ("Combiners_Mod_Masked_Dual_Crossfade", "Diffuse_T1_T2"), ("Combiners_Opaque_Alpha", "Diffuse_T1_T1"),
    ("Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha", "Diffuse_T1_Env_T2"), ("Combiners_Mod_Depth", "Diffuse_EdgeFade_Env"),
    ("Guild", "Diffuse_T1_T2_T1"), ("Guild_NoBorder", "Diffuse_T1_T2"), ("Guild_Opaque", "Diffuse_T1_T2_T1"),
    ("Illum", "Diffuse_T1_T1"), ("Combiners_Mod_Mod_Mod_Const", "Diffuse_T1_T2_T3"),
    ("Combiners_Mod_Mod_Mod_Const", "Color_T1_T2_T3"), ("Combiners_Opaque", "Diffuse_T1"),
    ("Combiners_Mod_Mod2x", "Diffuse_EdgeFade_T1_T2"), ("Combiners_Mod", "Diffuse_EdgeFade_T1"),
    ("Combiners_Mod_Mod_Depth", "Diffuse_EdgeFade_T1_T2"),
]

_MOD_PIXEL = {0: "Combiners_Mod_Opaque", 3: "Combiners_Mod_Add", 4: "Combiners_Mod_Mod2x",
              6: "Combiners_Mod_Mod2xNA", 7: "Combiners_Mod_AddNA"}
_OPAQUE_PIXEL = {0: "Combiners_Opaque_Opaque", 3: "Combiners_Opaque_AddAlpha", 4: "Combiners_Opaque_Mod2x",
                 6: "Combiners_Opaque_Mod2xNA", 7: "Combiners_Opaque_AddAlpha"}

_MULTITEX_PARTICLE = 0x10000000
_MIN_PARTICLE_VERSION = 272  # src/m2_scene.hpp kMinVerifiedParticleVersion
_PARTICLE_STRIDE = 0x1EC
_RIBBON_STRIDE = 0xB0
_BATCH_STRIDE = 0x18
_NO_INDEX = 0xFFFF


def _resolve(table: list[tuple[str, str]], shader_id: int, op_count: int) -> tuple[str, str]:
    if shader_id & 0x8000:
        index = shader_id & 0x7FFF
        return table[index] if index < len(table) else (f"<out_of_range:{index}>", f"<out_of_range:{index}>")
    lower = shader_id & 7
    if op_count == 1:
        pixel = "Combiners_Mod" if shader_id & 0x70 else "Combiners_Opaque"
        vertex = "Diffuse_Env" if shader_id & 0x80 else ("Diffuse_T2" if shader_id & 0x4000 else "Diffuse_T1")
        return pixel, vertex
    pixel = _MOD_PIXEL.get(lower, "Combiners_Mod_Mod") if shader_id & 0x70 else _OPAQUE_PIXEL.get(lower, "Combiners_Opaque_Mod")
    if shader_id & 0x80:
        vertex = "Diffuse_Env_Env" if shader_id & 0x8 else "Diffuse_Env_T1"
    elif shader_id & 0x8:
        vertex = "Diffuse_T1_Env"
    else:
        vertex = "Diffuse_T1_T2" if shader_id & 0x4000 else "Diffuse_T1_T1"
    return pixel, vertex


def _bits(value: int) -> list[int]:
    return [1 << b for b in range(32) if value & (1 << b)]


def _chunks(data: bytes, start: int, end: int, reversed_tags: bool) -> list[tuple[str, int, int]]:
    out = []
    pos = start
    while pos + 8 <= end:
        raw, size = struct.unpack_from("<4sI", data, pos)
        tag = (raw[::-1] if reversed_tags else raw).decode("latin-1")
        if pos + 8 + size > end:
            raise ValueError(f"chunk {tag!r} at {pos}: expected size <= {end - pos - 8}, got {size}")
        out.append((tag, pos + 8, size))
        pos += 8 + size
    return out


def _array(blob: bytes, field: int, stride: int, what: str) -> tuple[int, int]:
    count, ofs = struct.unpack_from("<II", blob, field)
    if count and (ofs > len(blob) or count > (len(blob) - ofs) // stride):
        raise ValueError(f"{what}: expected {count} x {stride}B at {ofs} within {len(blob)}B blob")
    return count, ofs


def _u16s(blob: bytes, field: int, what: str, signed: bool = False) -> list[int]:
    count, ofs = _array(blob, field, 2, what)
    return list(struct.unpack_from(f"<{count}{'h' if signed else 'H'}", blob, ofs))


def _weight_state(blob: bytes, track: int) -> str:
    """M2Track<fixed16>: 'animated', 'constant_zero' (the wiki's never-rendered
    case, M2/.skin "Material blend mode overrides") or 'constant'."""
    global_seq = struct.unpack_from("<h", blob, track + 2)[0]
    seqs, seqs_ofs = _array(blob, track + 12, 8, "textureWeight.values")
    per_seq = [struct.unpack_from("<II", blob, seqs_ofs + i * 8) for i in range(seqs)]
    if global_seq >= 0 or any(n > 1 for n, _ in per_seq) or sum(1 for n, _ in per_seq if n) > 1:
        return "animated"
    keyed = [(n, o) for n, o in per_seq if n == 1]
    if len(keyed) == 1 and keyed[0][1] + 2 <= len(blob) and struct.unpack_from("<h", blob, keyed[0][1])[0] == 0:
        return "constant_zero"
    return "constant"


def _analyze_m2(path: Path, facts: Counter, roles: dict[str, set[int]]) -> None:
    data = path.read_bytes()
    chunk_payloads: dict[str, bytes] = {}
    if data[:4] == b"MD20":
        blob = data
    else:
        for tag, start, size in _chunks(data, 0, len(data), reversed_tags=False):
            chunk_payloads.setdefault(tag, data[start:start + size])
            facts[f"m2.chunk:{tag}"] = 1
        if "MD21" not in chunk_payloads:
            raise ValueError(f"expected MD20 or an MD21 chunk, got first bytes {data[:4]!r}")
        blob = chunk_payloads["MD21"]

    version, = struct.unpack_from("<I", blob, 4)
    global_flags, = struct.unpack_from("<I", blob, 0x10)
    facts[f"m2.version:{version}"] = 1
    for bit in _bits(global_flags):
        facts[f"m2.global_flag:0x{bit:x}"] = 1

    txid = chunk_payloads.get("TXID", b"")
    txid_ids = list(struct.unpack_from(f"<{len(txid) // 4}I", txid)) if txid else []
    tex_count, tex_ofs = _array(blob, 0x50, 16, "textures")
    textures = [struct.unpack_from("<II", blob, tex_ofs + i * 16) for i in range(tex_count)]
    for i, (tex_type, tex_flags) in enumerate(textures):
        facts[f"m2.texture_type:{tex_type}"] += 1
        for bit in _bits(tex_flags):
            facts[f"m2.texture_flag:0x{bit:x}"] += 1

    mat_count, mat_ofs = _array(blob, 0x70, 4, "materials")
    materials = [struct.unpack_from("<HH", blob, mat_ofs + i * 4) for i in range(mat_count)]
    for mat_flags, blend in materials:
        facts[f"m2.material_blend:{blend}"] += 1
        for bit in _bits(mat_flags):
            facts[f"m2.material_flag:0x{bit:x}"] += 1

    color_count, _ = _array(blob, 0x48, 40, "colors")
    weight_count, weight_ofs = _array(blob, 0x58, 20, "textureWeights")
    transform_count, _ = _array(blob, 0x60, 60, "textureTransforms")
    light_count, _ = _array(blob, 0x108, 156, "lights")
    for name, n in (("colors", color_count), ("texture_weights", weight_count),
                    ("texture_transforms", transform_count), ("lights", light_count)):
        if n:
            facts[f"m2.has:{name}"] = 1
    weight_states = [_weight_state(blob, weight_ofs + i * 20) for i in range(weight_count)]

    texture_lookup = _u16s(blob, 0x80, "textureCombos")
    coord_combos = _u16s(blob, 0x88, "textureCoordCombos", signed=True)
    weight_combos = _u16s(blob, 0x90, "textureWeightCombos")
    transform_combos = _u16s(blob, 0x98, "textureTransformCombos")
    facts["m2.has:coord_combos" if coord_combos else "m2.coord_combos_empty"] = 1
    for c in coord_combos:
        facts[f"m2.coord_combo:{c}"] += 1
    combiner_combos = _u16s(blob, 0x130, "textureCombinerCombos") if global_flags & 0x8 else []
    for c in combiner_combos:
        facts[f"m2.combiner_combo_value:{c}"] += 1

    def fdid(tex_index: int) -> int:
        if tex_index >= len(textures) or textures[tex_index][0] != 0 or tex_index >= len(txid_ids):
            return 0
        return txid_ids[tex_index]

    def blend_of(mat_index: int) -> str:
        return str(materials[mat_index][1]) if mat_index < len(materials) else "oob"

    ribbon_count, ribbon_ofs = _array(blob, 0x120, _RIBBON_STRIDE, "ribbons")
    for i in range(ribbon_count):
        base = ribbon_ofs + i * _RIBBON_STRIDE
        for m in _u16s(blob, base + 0x1C, "ribbon.materialIndices"):
            facts[f"ribbon.blend:{blend_of(m)}"] += 1
        for t in _u16s(blob, base + 0x14, "ribbon.textureIndices"):
            if fdid(t):
                roles["ribbon.texture"].add(fdid(t))
        facts[f"ribbon.texture_count:{len(_u16s(blob, base + 0x14, 'ribbon.textureIndices'))}"] += 1

    particle_count, particle_ofs = _array(blob, 0x128, _PARTICLE_STRIDE, "particles") if version >= _MIN_PARTICLE_VERSION else (0, 0)
    if version < _MIN_PARTICLE_VERSION and struct.unpack_from("<I", blob, 0x128)[0]:
        facts["particle.skipped_pre_272_version"] += 1
    for i in range(particle_count):
        base = particle_ofs + i * _PARTICLE_STRIDE
        p_flags, = struct.unpack_from("<I", blob, base + 0x04)
        texture_id, = struct.unpack_from("<H", blob, base + 0x16)
        model_name_len, = struct.unpack_from("<I", blob, base + 0x18)
        blending, emitter_type = struct.unpack_from("<BB", blob, base + 0x28)
        multitex = bool(p_flags & _MULTITEX_PARTICLE)
        facts[f"particle.blend:{blending}|multitex:{int(multitex)}"] += 1
        facts[f"particle.emitter_type:{emitter_type}"] += 1
        if model_name_len:
            facts["particle.model_particle"] += 1
        for bit in _bits(p_flags):
            facts[f"particle.flag:0x{bit:x}"] += 1
        slots = [(texture_id >> s) & 0x1F for s in (0, 5, 10)] if multitex else [texture_id]
        for slot, t in enumerate(slots):
            if fdid(t):
                roles[f"particle.multitex{slot}" if multitex else "particle.texture"].add(fdid(t))

    skin_path = path.with_name(path.stem + "00.skin")
    try:
        skin = skin_path.read_bytes()
    except FileNotFoundError:
        facts["m2.skin00_missing"] = 1
        return
    if skin[:4] != b"SKIN":
        raise ValueError(f"{skin_path}: expected SKIN magic, got {skin[:4]!r}")
    batch_count, batch_ofs = _array(skin, 4 + 8 * 4, _BATCH_STRIDE, "skin.batches")
    for i in range(batch_count):
        (b_flags, priority, shader_id, _section, flags2, color_index, mat_index, mat_layer, tex_count,
         combo_index, coord_index, weight_index, transform_index) = struct.unpack_from(
            "<BbHHHhHHHHHHH", skin, batch_ofs + i * _BATCH_STRIDE)
        blend = blend_of(mat_index)
        pixel, vertex = _resolve(EFFECTS, shader_id, tex_count)
        facts["batch.total"] += 1
        facts[f"batch.ps:{pixel}"] += 1
        facts[f"batch.vs:{vertex}"] += 1
        facts[f"batch.ps_blend:{pixel}|{blend}"] += 1
        facts[f"batch.texture_count:{tex_count}"] += 1
        if shader_id & 0x8000:
            facts[f"batch.effect_index:{shader_id & 0x7FFF}"] += 1
        else:
            facts[f"batch.runtime_shader_id:0x{shader_id:04x}|tc:{tex_count}"] += 1
        if global_flags & 0x8:
            in_range = shader_id < len(combiner_combos)
            facts[f"batch.combiner_flag_set|shader_id_indexes_combos:{int(in_range)}"] += 1
        for bit in _bits(b_flags):
            facts[f"batch.flag:0x{bit:x}"] += 1
        for bit in _bits(flags2):
            facts[f"batch.flags2:0x{bit:x}"] += 1
        if priority:
            facts["batch.priority_plane_nonzero"] += 1
        if mat_layer:
            facts["batch.material_layer_nonzero"] += 1
        if color_index != -1:
            facts["batch.color_animated"] += 1
        if transform_index < len(transform_combos) and transform_combos[transform_index] != _NO_INDEX:
            facts["batch.uv_transform"] += 1
        if weight_index < len(weight_combos) and weight_combos[weight_index] < len(weight_states):
            facts[f"batch.weight:{weight_states[weight_combos[weight_index]]}"] += 1
        if mat_index < len(materials):
            for bit in _bits(materials[mat_index][0]):
                facts[f"batch.material_flag:0x{bit:x}"] += 1
        for layer in range(tex_count):
            coord = coord_combos[coord_index + layer] if coord_index + layer < len(coord_combos) else "none"
            tex_index = texture_lookup[combo_index + layer] if combo_index + layer < len(texture_lookup) else None
            tex_type = textures[tex_index][0] if tex_index is not None and tex_index < len(textures) else "oob"
            facts[f"batch.layer{layer}.coord:{coord}"] += 1
            facts[f"batch.layer{layer}.texture_type:{tex_type}"] += 1
            if tex_index is not None and fdid(tex_index):
                roles[f"m2.{pixel}.layer{layer}.coord{coord}"].add(fdid(tex_index))


# WMO shader names: documentation/wowdev-wiki/wikitext/WMO.wiki "Shader types (26522)" + DF row 23.
WMO_SHADERS = ["Diffuse", "Specular", "Metal", "Env", "Opaque", "EnvMetal", "TwoLayerDiffuse", "TwoLayerEnvMetal",
               "TwoLayerTerrain", "DiffuseEmissive", "waterWindow", "MaskedEnvMetal", "EnvMetalEmissive",
               "TwoLayerDiffuseOpaque", "submarineWindow", "TwoLayerDiffuseEmissive", "DiffuseTerrain",
               "AdditiveMaskedEnvMetal", "TwoLayerDiffuseMod2x", "TwoLayerDiffuseMod2xNA", "TwoLayerDiffuseAlpha",
               "Lod", "Parallax", "UnkDFShader"]
_MOGP_HEADER_END = 12 + 8 + 0x44


def _wmo_shader_name(index: int) -> str:
    return f"{index}:{WMO_SHADERS[index]}" if index < len(WMO_SHADERS) else f"{index}:<unknown>"


def _analyze_wmo(path: Path, facts: Counter, roles: dict[str, set[int]]) -> None:
    with path.open("rb") as f:
        head = f.read(_MOGP_HEADER_END)
        if head[12:16][::-1] == b"MOGP":
            group_flags, = struct.unpack_from("<I", head, 20 + 0x08)
            group_liquid, = struct.unpack_from("<I", head, 20 + 0x34)
            flags2, = struct.unpack_from("<I", head, 20 + 0x3C)
            facts["wmo_group.total"] = 1
            for bit in _bits(group_flags):
                facts[f"wmo_group.flag:0x{bit:x}"] = 1
            for bit in _bits(flags2):
                facts[f"wmo_group.flags2:0x{bit:x}"] = 1
            if group_liquid:
                facts[f"wmo_group.liquid:{group_liquid}"] = 1
            return
        data = head + f.read()

    chunks = _chunks(data, 0, len(data), reversed_tags=True)
    tags = {tag for tag, _, _ in chunks}
    for tag in tags:
        facts[f"wmo.chunk:{tag}"] = 1
    payload = {tag: data[start:start + size] for tag, start, size in reversed(chunks)}
    if "MOHD" in payload:
        mohd_flags, = struct.unpack_from("<H", payload["MOHD"], 0x3C)
        for bit in _bits(mohd_flags):
            facts[f"wmo.mohd_flag:0x{bit:x}"] = 1
    texture_ids_are_fdids = "MOTX" not in tags
    momt = payload.get("MOMT", b"")
    if len(momt) % 64:
        raise ValueError(f"MOMT: expected size multiple of 64, got {len(momt)}")
    for i in range(len(momt) // 64):
        m_flags, shader, blend, tex1, _sidn, _frame, tex2, _diff, _ground, tex3, color2, flags2 = \
            struct.unpack_from("<12I", momt, i * 64)
        name = _wmo_shader_name(shader)
        facts[f"wmo.shader:{name}"] += 1
        facts[f"wmo.shader_blend:{name}|{blend}"] += 1
        for bit in _bits(m_flags):
            facts[f"wmo.material_flag:0x{bit:x}"] += 1
        if not texture_ids_are_fdids:
            continue
        slots = [("tex1", tex1), ("tex2", tex2), ("tex3", tex3)]
        if shader == 23:
            runtime = struct.unpack_from("<4I", momt, i * 64 + 0x30)
            slots += [("color_2", color2), ("flags_2", flags2)] + [(f"runtime{k}", v) for k, v in enumerate(runtime)]
        for slot, value in slots:
            if value:
                roles[f"wmo.{name}.{slot}"].add(value)


def _analyze_adt_root(data: bytes, facts: Counter) -> None:
    for tag, start, size in _chunks(data, 0, len(data), reversed_tags=True):
        facts[f"adt.chunk:{tag}"] = 1
        if tag == "MCNK":
            mcnk_flags, = struct.unpack_from("<I", data, start)
            for bit in _bits(mcnk_flags):
                facts[f"adt.mcnk_flag:0x{bit:x}"] += 1
            for sub, _, _ in _chunks(data, start + 128, start + size, reversed_tags=True):
                facts[f"adt.mcnk_sub:{sub}"] += 1
        elif tag == "MH2O":
            for c in range(256):
                off_instances, layer_count, _ = struct.unpack_from("<III", data, start + c * 12)
                for k in range(layer_count):
                    liquid_type, lvf = struct.unpack_from("<HH", data, start + off_instances + k * 0x18)
                    facts[f"adt.liquid_type:{liquid_type}"] += 1
                    facts[f"adt.liquid_lvf:{lvf}" if lvf < 42 else "adt.liquid_object_id"] += 1


def _analyze_adt_tex0(data: bytes, facts: Counter, roles: dict[str, set[int]]) -> None:
    chunks = _chunks(data, 0, len(data), reversed_tags=True)
    diffuse: list[int] = []
    for tag, start, size in chunks:
        facts[f"tex0.chunk:{tag}"] = 1
        if tag == "MDID":
            diffuse = list(struct.unpack_from(f"<{size // 4}I", data, start))
            roles["adt.diffuse"].update(i for i in diffuse if i)
        elif tag == "MHID":
            roles["adt.height"].update(i for i in struct.unpack_from(f"<{size // 4}I", data, start) if i)
        elif tag == "MTXF":
            for value in struct.unpack_from(f"<{size // 4}I", data, start):
                for bit in _bits(value):
                    facts[f"tex0.mtxf_flag:0x{bit:x}"] += 1
        elif tag == "MTXP":
            for k in range(size // 16):
                p_flags, height_scale, height_offset = struct.unpack_from("<Iff", data, start + k * 16)
                for bit in _bits(p_flags):
                    facts[f"tex0.mtxp_flag:0x{bit:x}"] += 1
                if height_scale or height_offset:
                    facts["tex0.mtxp_height_params_nonzero"] += 1
    for tag, start, size in chunks:
        if tag != "MCNK":
            continue
        for sub, s_start, s_size in _chunks(data, start, start + size, reversed_tags=True):
            facts[f"tex0.mcnk_sub:{sub}"] += 1
            if sub == "MCLY":
                for k in range(s_size // 16):
                    texture_id, l_flags, _, _ = struct.unpack_from("<4I", data, s_start + k * 16)
                    for bit in _bits(l_flags & ~0x3F):
                        facts[f"tex0.mcly_flag:0x{bit:x}"] += 1
                    if l_flags & 0x40:
                        facts[f"tex0.mcly_anim_rot{l_flags & 0x7}_speed{(l_flags >> 3) & 0x7}"] += 1
                    if l_flags & 0x400 and texture_id < len(diffuse):
                        roles["adt.cubemap_reflection_layer"].add(diffuse[texture_id])
            elif sub == "MCMT":
                for material_id in data[s_start:s_start + s_size]:
                    if material_id:
                        facts[f"tex0.terrain_material:{material_id}"] += 1


def _analyze_wdt(data: bytes, facts: Counter) -> None:
    for tag, start, _ in _chunks(data, 0, len(data), reversed_tags=True):
        facts[f"wdt.chunk:{tag}"] = 1
        if tag == "MPHD":
            mphd_flags, = struct.unpack_from("<I", data, start)
            for bit in _bits(mphd_flags):
                facts[f"wdt.mphd_flag:0x{bit:x}"] = 1


def _analyze_bls(path: Path, facts: Counter) -> str:
    with path.open("rb") as f:
        header = f.read(40)
        magic, version, api, _perms, shader_count = struct.unpack_from("<4sI4sII", header)
        # material3_*/model3skinshader_debug ship in the GFAT container
        # (documentation/wowdev-wiki/wikitext/GFAT.wiki), not GXSH -- inventory only.
        if magic == b"TAFG":
            facts["bls.container:GFAT"] = 1
            return json.dumps({"container": "GFAT"})
        if magic != b"HSXG":
            raise ValueError(f"expected BLS magic b'HSXG', got {magic!r}")
        table = f.read(shader_count * 24)
    nonempty = sum(1 for k in range(shader_count) if struct.unpack_from("<I", table, k * 24 + 4)[0])
    facts[f"bls.version:0x{version:x}|api:{api[::-1].decode('latin-1')}"] = 1
    return json.dumps({"slots": shader_count, "compiled_permutations": nonempty})


_ADT_SKIPPED_SUFFIXES = ("_obj0", "_obj1", "_lod")


class ShaderInventoryTask:
    GLOB_PATTERNS = ["*.m2", "*.wmo", "*.adt", "*.wdt", "*.bls"]
    FIELDNAMES = ["kind", "facts", "fdids", "detail"]
    PARALLEL_MODE = "process"  # per-file struct walks over thousands of records (MCNK x256, particles)
    BATCH_SIZE = 1

    @staticmethod
    def analyze(path: Path) -> dict | None:
        suffix = path.suffix.lower()
        stem = path.stem.lower()
        facts: Counter = Counter()
        roles: dict[str, set[int]] = defaultdict(set)
        detail = ""
        if suffix == ".m2":
            kind = "m2"
            _analyze_m2(path, facts, roles)
        elif suffix == ".wmo":
            kind = "wmo"
            _analyze_wmo(path, facts, roles)
            if "wmo_group.total" in facts:
                kind = "wmo_group"
        elif suffix == ".adt":
            if stem.endswith(_ADT_SKIPPED_SUFFIXES):
                return None
            if stem.endswith("_tex0"):
                kind = "adt_tex0"
                _analyze_adt_tex0(path.read_bytes(), facts, roles)
            else:
                kind = "adt_root"
                _analyze_adt_root(path.read_bytes(), facts)
        elif suffix == ".wdt":
            kind = "wdt"
            _analyze_wdt(path.read_bytes(), facts)
        else:
            kind = "bls"
            detail = _analyze_bls(path, facts)
        return {
            "kind": kind,
            "facts": json.dumps(facts, separators=(",", ":")),
            "fdids": json.dumps({role: sorted(ids) for role, ids in roles.items()}, separators=(",", ":")),
            "detail": detail,
        }

    @staticmethod
    def summarize(rows: list[dict], total_files: int) -> list[str]:
        totals: Counter = Counter()
        file_counts: Counter = Counter()
        examples: dict[str, str] = {}
        kinds: Counter = Counter()
        role_ids: dict[str, Counter] = defaultdict(Counter)
        bls: dict[str, dict] = {}
        for row in rows:
            kinds[row["kind"]] += 1
            for key, value in json.loads(row["facts"]).items():
                totals[key] += value
                file_counts[key] += 1
                examples.setdefault(key, row["path"])
            for role, ids in json.loads(row["fdids"]).items():
                role_ids[role].update(ids)
            if row["kind"] == "bls":
                bls[row["path"]] = json.loads(row["detail"])

        names = _resolve_fdids({i for ids in role_ids.values() for i in ids})
        roles_out = {}
        for role, ids in sorted(role_ids.items()):
            tokens: Counter = Counter()
            for fdid in ids:
                tokens[_suffix_token(names.get(fdid))] += 1
            roles_out[role] = {
                "distinct_textures": len(ids),
                "suffix_tokens": tokens.most_common(15),
                "examples": [names.get(i, f"<unlisted {i}>") for i, _ in ids.most_common(5)],
            }

        aggregate = {
            "kinds": kinds,
            "facts": {k: {"total": totals[k], "files": file_counts[k], "example": examples[k]} for k in sorted(totals)},
            "roles": roles_out,
            "bls": bls,
        }
        AGGREGATE_PATH.parent.mkdir(parents=True, exist_ok=True)
        AGGREGATE_PATH.write_text(json.dumps(aggregate, indent=1))

        lines = [f"{len(rows)} / {total_files} files produced a row ({dict(kinds)})"]
        for key in sorted(totals):
            lines.append(f"{key}\ttotal={totals[key]}\tfiles={file_counts[key]}\te.g. {examples[key]}")
        lines.append("")
        for role, info in roles_out.items():
            lines.append(f"role {role}: {info['distinct_textures']} textures, suffixes {info['suffix_tokens'][:8]}")
        lines.append(f"aggregate written to {AGGREGATE_PATH}")
        return lines


_SUFFIX_RE = re.compile(r"_([a-z0-9]+)\.(?:blp|tga|png)$")


def _suffix_token(name: str | None) -> str:
    if name is None:
        return "<unlisted>"
    match = _SUFFIX_RE.search(name.lower())
    return match.group(1) if match else "<none>"


def _resolve_fdids(wanted: set[int]) -> dict[int, str]:
    found: dict[int, str] = {}
    if not wanted or csf.LISTFILE is None or not csf.LISTFILE.exists():
        return found
    with csf.LISTFILE.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            fdid_str, _, rel_path = line.partition(";")
            if fdid_str.isdigit() and int(fdid_str) in wanted:
                found[int(fdid_str)] = rel_path.strip()
    return found
