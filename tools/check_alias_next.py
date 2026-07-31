#!/usr/bin/env python3
"""Investigates what M2Sequence.aliasNext actually resolves against (see
WIKI_FINDINGS.md sec.12 for the result: a local sequences-array index at the
M2Bounds-corrected offset 0x3E). A prior pass (formerly TODO_correctness.md
#4, since removed) read the field at the wiki's literal, uncorrected 0x22
offset and wrongly concluded it resolves neither as "a local index into
this file's own sequence array" nor "another sequence's own id within the
same file" for bloodelffemale_hd.skel alone. This script widens that check
across a real
character-model family (blood elf female/male, base + HD) -- the aliasNext
value might resolve against a *different* file's own Sequence.id, since
AnimationData.dbc ids are shared across the whole game, not per-file.

Independent of husk's own parsing, same discipline as every other tool
here -- reads M2Sequence directly at the real, WIKI_FINDINGS.md sec.1-
corrected 64-byte stride (id@0x00 u16, flags@0x0C u32, variationNext@0x3C
i16, aliasNext@0x3E u16), for both inline M2 sequences (MD21 payload, header
offset 0x01C) and external .skel SKS1 sequences (payload offset 0x08) --
both use the exact same M2Sequence record shape (src/skel.cpp's own
parseSequences confirms this, delegating straight to m2::parseSequences).

Usage:
    direnv exec . uv run --no-project python3 tools/check_alias_next.py
"""

import struct
import sys
from pathlib import Path

SEQ_STRIDE = 0x40
OFF_ID = 0x00
OFF_FLAGS = 0x0C
OFF_VARIATION_NEXT = 0x3C
OFF_ALIAS_NEXT = 0x3E
FLAG_ALIAS = 0x40


def iter_top_level_chunks(data: bytes):
    pos = 0
    n = len(data)
    while pos + 8 <= n:
        tag = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        payload_start = pos + 8
        if payload_start + size > n:
            return
        yield tag, data[payload_start:payload_start + size]
        pos = payload_start + size


def decode_sequences(blob: bytes, count: int, offset: int):
    seqs = []
    for i in range(count):
        base = offset + i * SEQ_STRIDE
        if base + SEQ_STRIDE > len(blob):
            raise ValueError(f"sequence {i} at 0x{base:x} runs past blob end ({len(blob)} bytes)")
        sid, = struct.unpack_from("<H", blob, base + OFF_ID)
        flags, = struct.unpack_from("<I", blob, base + OFF_FLAGS)
        variation_next, = struct.unpack_from("<h", blob, base + OFF_VARIATION_NEXT)
        alias_next, = struct.unpack_from("<H", blob, base + OFF_ALIAS_NEXT)
        seqs.append({
            "index": i, "id": sid, "flags": flags,
            "variationNext": variation_next, "aliasNext": alias_next,
            "isAlias": bool(flags & FLAG_ALIAS),
        })
    return seqs


def load_m2_inline_sequences(path: Path):
    data = path.read_bytes()
    md21 = None
    for tag, payload in iter_top_level_chunks(data):
        if tag == b"MD21":
            md21 = payload
            break
    if md21 is None:
        raise ValueError(f"{path}: no MD21 chunk (not a Legion+ chunked M2?)")
    count, offset = struct.unpack_from("<II", md21, 0x01C)
    return decode_sequences(md21, count, offset)


def load_skel_sequences(path: Path):
    data = path.read_bytes()
    sks1 = None
    for tag, payload in iter_top_level_chunks(data):
        if tag == b"SKS1":
            sks1 = payload
            break
    if sks1 is None:
        raise ValueError(f"{path}: no SKS1 chunk")
    count, offset = struct.unpack_from("<II", sks1, 0x08)
    return decode_sequences(sks1, count, offset)


def main() -> int:
    root = Path("/media/luna/data/wow_export/character/bloodelf")
    files = {
        "bloodelffemale.m2 (inline)": (load_m2_inline_sequences, root / "female/bloodelffemale.m2"),
        "bloodelffemale_hd.skel": (load_skel_sequences, root / "female/bloodelffemale_hd.skel"),
        "bloodelfmale.m2 (inline)": (load_m2_inline_sequences, root / "male/bloodelfmale.m2"),
        "bloodelfmale_hd.skel": (load_skel_sequences, root / "male/bloodelfmale_hd.skel"),
    }

    all_seqs = {}
    for name, (loader, path) in files.items():
        if not path.exists():
            print(f"SKIP {name}: {path} not found")
            continue
        seqs = loader(path)
        all_seqs[name] = seqs
        print(f"{name}: {len(seqs)} sequences")

    # Global id -> set of file names it appears in, for the cross-file match.
    id_to_files = {}
    for name, seqs in all_seqs.items():
        for s in seqs:
            id_to_files.setdefault(s["id"], set()).add(name)

    print()
    print("=== Alias sequences (flags & 0x40) ===")
    total_alias = 0
    resolved_local_index = 0
    resolved_local_id = 0
    resolved_cross_file_id = 0
    unresolved = 0
    both_variation_and_alias = 0

    for name, seqs in all_seqs.items():
        local_ids = {s["id"] for s in seqs}
        for s in seqs:
            if not s["isAlias"]:
                continue
            total_alias += 1
            an = s["aliasNext"]
            vn = s["variationNext"]
            if vn != -1 and vn != 0xFFFF:
                both_variation_and_alias += 1

            local_index_ok = an < len(seqs)
            local_id_ok = an in local_ids
            cross_files = id_to_files.get(an, set()) - {name}

            if local_index_ok:
                resolved_local_index += 1
            if local_id_ok:
                resolved_local_id += 1
            if cross_files:
                resolved_cross_file_id += 1
            if not local_index_ok and not local_id_ok and not cross_files:
                unresolved += 1

            print(f"{name}: seq#{s['index']} id={s['id']} aliasNext={an} variationNext={vn} "
                  f"local_index_valid={local_index_ok} local_id_match={local_id_ok} "
                  f"cross_file_id_match_in={sorted(cross_files) if cross_files else None}")

    print()
    print(f"total alias sequences examined: {total_alias}")
    print(f"  aliasNext valid as local array index: {resolved_local_index}")
    print(f"  aliasNext matches some sequence's own id, same file: {resolved_local_id}")
    print(f"  aliasNext matches some sequence's own id, a DIFFERENT family file: {resolved_cross_file_id}")
    print(f"  aliasNext resolves nowhere (local index/id/cross-file id all fail): {unresolved}")
    print(f"  records with a real (non -1) variationNext AND flags&0x40 set: {both_variation_and_alias}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
