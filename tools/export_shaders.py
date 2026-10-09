#!/usr/bin/env python3
"""Exports every client shader container under <corpus>/shaders/ (GXSH
.bls and the GFAT wrappers around them) to one directory per container:
every distinct compiled blob as a standalone DXBC file, plus its native
(Direct3D) listing and its Vulkan form (SPIR-V, SPIR-V text, GLSL).

	direnv exec . tools/venv/bin/python -I tools/export_shaders.py \\
		[--corpus /media/luna/data/wow_export] [--output example_exports/shaders]

Output, per container (`<stage>/<api>/<name>/`, GFAT entries unwrapped into
the same shape as standalone files):
	manifest.json      header fields, slot -> blob table, per-blob facts and
	                   listing status. Written last, so a rerun skips any
	                   container whose manifest exists.
	blobs/NNNN.dxbc    the DXBC container (DXBC-TPF for dx_5_0, DXIL for dx_6_0)
	asm/NNNN.asm       dx_5_0: vkd3d-compiler d3d-asm
	asm/NNNN.ll        dx_6_0: dxc -dumpbin
	spirv/NNNN.spv     vkd3d-compiler spirv-binary
	spirv/NNNN.spvasm  spirv-dis
	glsl/NNNN.glsl     spirv-cross
	*.err              the tool's stderr, in place of an output it failed to write
<output>/index.json and mapping.csv list every container against the .bls
(and FileDataID) it came from; errors.log has one line per .bls that could
not be exported. Neither stops the run.

Layouts: documentation/wowdev-wiki/md/BLS.md ("BLS v1.14+") and GFAT.md, with
the corrections verified against 12.1.0 files noted at each check below.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import csv
import json
import os
import struct
import subprocess
import sys
import tomllib
import zlib
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CORPUS = Path("/media/luna/data/wow_export")
DEFAULT_OUTPUT = REPO_ROOT / "example_exports" / "shaders"

GXSH = b"HSXG"
GFAT = b"TAFG"
DXBC = b"DXBC"
API_DIRS = {"DX50": "dx_5_0", "DX60": "dx_6_0", "MT11": "mtl_1_1"}
# v0x1000E marks an uncompiled permutation slot this way rather than with size 0 alone.
EMPTY_SLOT_OFFSET = 0xFFFFFFFF
# The DXBC part holding the program, mapped to vkd3d-compiler's name for that input.
PROGRAM_PARTS = {"SHEX": "dxbc-tpf", "SHDR": "dxbc-tpf", "DXIL": "dxbc-dxil"}


class FormatError(ValueError):
	pass


def expect(condition: bool, what: str, expected, actual) -> None:
	if not condition:
		raise FormatError(f"{what}: expected {expected}, got {actual}")


@dataclass
class Blob:
	stream_offset: int
	block_header: bytes
	dxbc: bytes
	parts: list[str]
	trailing: int


@dataclass
class Container:
	"""One GXSH shader file, already validated; `slots` maps permutation slot -> blob index."""
	version: int
	api: str
	header: dict
	blobs: list[Blob]
	slots: list[int | None] | None
	slot_hashes: list[str | None] | None
	source: str
	gfat: dict | None = None
	notes: list[str] = field(default_factory=list)


def read_u32s(data: bytes, offset: int, count: int, what: str) -> tuple[int, ...]:
	expect(offset + 4 * count <= len(data), f"{what} end", f"<= {len(data)}", offset + 4 * count)
	return struct.unpack_from(f"<{count}I", data, offset)


def inflate_chunks(data: bytes, ofs_chunks: int, n_chunks: int, ofs_data: int) -> bytes:
	expect(ofs_data == ofs_chunks + 4 * (n_chunks + 1), "ofsCompressedData", ofs_chunks + 4 * (n_chunks + 1), ofs_data)
	offsets = read_u32s(data, ofs_chunks, n_chunks + 1, "chunk offset table")
	# The trailing offset is the end of the last chunk, which is the end of the container.
	expect(ofs_data + offsets[-1] == len(data), "last chunk end", len(data), ofs_data + offsets[-1])
	out = bytearray()
	for i in range(n_chunks):
		expect(offsets[i] < offsets[i + 1], f"chunk {i} offsets ascending", f"< {offsets[i + 1]}", offsets[i])
		out += zlib.decompress(data[ofs_data + offsets[i]:ofs_data + offsets[i + 1]])
	return bytes(out)


def parse_dxbc(stream: bytes, start: int, what: str) -> tuple[bytes, list[str]]:
	expect(stream[start:start + 4] == DXBC, f"{what} magic", DXBC, stream[start:start + 4])
	expect(start + 32 <= len(stream), f"{what} header end", f"<= {len(stream)}", start + 32)
	size, part_count = struct.unpack_from("<II", stream, start + 24)
	expect(start + size <= len(stream), f"{what} end", f"<= {len(stream)}", start + size)
	dxbc = stream[start:start + size]
	part_offsets = read_u32s(dxbc, 32, part_count, f"{what} part table")
	parts = []
	for p in part_offsets:
		expect(p + 8 <= size, f"{what} part header end", f"<= {size}", p + 8)
		parts.append(dxbc[p:p + 4].decode("latin-1"))
	return dxbc, parts


def blob_at(stream: bytes, offset: int, size: int, what: str) -> Blob:
	block = stream[offset:offset + size]
	# The wiki's BLSBlock header is a fixed 96 bytes on every 12.1 blob checked, but
	# its length isn't stored, so locate the DXBC rather than hardcode the size.
	dxbc_start = block.find(DXBC)
	expect(dxbc_start >= 0, f"{what} DXBC magic", "present", "absent")
	dxbc, parts = parse_dxbc(block, dxbc_start, what)
	return Blob(offset, block[:dxbc_start], dxbc, parts, size - dxbc_start - len(dxbc))


def parse_gxsh_v1000e(data: bytes, source: str) -> Container:
	_, version, api, n_perm, n_slots, ofs_chunks, n_chunks, ofs_data, unk0, unk1 = struct.unpack_from("<4sI4s7I", data)
	api_name = api[::-1].decode("latin-1")
	slot_table_end = 40 + 24 * n_slots
	expect(slot_table_end + 4 <= len(data), "slot table end", f"<= {len(data)}", slot_table_end + 4)
	stream_len, = struct.unpack_from("<I", data, slot_table_end)
	# The wiki's BLS v1.14+ struct puts lastOffsetPlusSize between the slot table
	# and the chunk offsets; that holds on every 12.1 file.
	expect(ofs_chunks == slot_table_end + 4, "ofsCompressionChunks", slot_table_end + 4, ofs_chunks)
	stream = inflate_chunks(data, ofs_chunks, n_chunks, ofs_data)
	expect(len(stream) == stream_len, "inflated size (lastOffsetPlusSize)", stream_len, len(stream))

	by_offset: dict[int, int] = {}
	blobs: list[Blob] = []
	slots: list[int | None] = []
	hashes: list[str | None] = []
	for i in range(n_slots):
		offset, size, digest = struct.unpack_from("<II16s", data, 40 + 24 * i)
		if size == 0:
			expect(offset == EMPTY_SLOT_OFFSET, f"slot {i} empty-slot offset", hex(EMPTY_SLOT_OFFSET), hex(offset))
			slots.append(None)
			hashes.append(None)
			continue
		expect(offset + size <= stream_len, f"slot {i} end", f"<= {stream_len}", offset + size)
		if offset not in by_offset:
			by_offset[offset] = len(blobs)
			blobs.append(blob_at(stream, offset, size, f"slot {i} blob"))
		shared = blobs[by_offset[offset]]
		expect(len(shared.block_header) + len(shared.dxbc) + shared.trailing == size,
		       f"slot {i} size (shares offset {offset})", len(shared.block_header) + len(shared.dxbc) + shared.trailing, size)
		slots.append(by_offset[offset])
		hashes.append(digest.hex())

	header = {"permutations": n_perm, "slots": n_slots, "compressed_chunks": n_chunks,
	          "inflated_size": stream_len, "unk0": unk0, "unk1": unk1}
	return Container(version, api_name, header, blobs, slots, hashes, source)


def parse_gxsh_v1000c(data: bytes, source: str) -> Container:
	_, version, n_perm, n_shaders, ofs_chunks, n_chunks, ofs_data = struct.unpack_from("<4s6I", data)
	stream = inflate_chunks(data, ofs_chunks, n_chunks, ofs_data)
	# The 20 bytes between the header and the chunk table don't match the wiki's
	# "nShaders offsets" on the 5 files that use this version, so blobs are found
	# by walking the inflated stream instead. No slot table, no api tag.
	blobs: list[Blob] = []
	cursor = 0
	while (start := stream.find(DXBC, cursor)) >= 0:
		dxbc, parts = parse_dxbc(stream, start, f"DXBC at {start}")
		blobs.append(Blob(start, stream[cursor:start], dxbc, parts, 0))
		cursor = start + len(dxbc)
	expect(len(blobs) > 0, "DXBC containers in stream", ">= 1", 0)
	header = {"permutations": n_perm, "shaders": n_shaders, "compressed_chunks": n_chunks,
	          "inflated_size": len(stream), "unparsed_header_bytes": data[28:ofs_chunks].hex()}
	container = Container(version, "", header, blobs, None, None, source)
	container.notes.append("v0x1000C: blobs found by walking the stream for DXBC; no slot table decoded")
	return container


def parse_gxsh(data: bytes, source: str) -> Container:
	expect(len(data) >= 40, "file size", ">= 40", len(data))
	magic, version = struct.unpack_from("<4sI", data)
	expect(magic == GXSH, "magic", GXSH, magic)
	if version == 0x1000E:
		return parse_gxsh_v1000e(data, source)
	if version == 0x1000C:
		return parse_gxsh_v1000c(data, source)
	raise FormatError(f"GXSH version: expected 0x1000e or 0x1000c, got {version:#x}")


def parse_gfat(data: bytes, source: str) -> list[Container]:
	# The wiki's GFATHeader has a char numAPIs + 3 padding bytes; read as one u32,
	# which is the same thing while the padding is zero.
	_, version, n_apis = struct.unpack_from("<4sII", data)
	expect(version == 0x1000B, "GFAT version", "0x1000b", hex(version))
	containers = []
	for i in range(n_apis):
		api, start, end = struct.unpack_from("<4sII", data, 12 + 12 * i)
		api_name = api[::-1].decode("latin-1")
		expect(start < end <= len(data), f"GFAT entry {api_name} range", f"start < end <= {len(data)}", (start, end))
		container = parse_gxsh(data[start:end], source)
		# A v0x1000C payload carries no api tag of its own; the GFAT entry names it.
		expect(container.api in ("", api_name), f"GFAT entry {api_name} inner api", api_name, container.api)
		container.api = api_name
		container.gfat = {"api": api_name, "start": start, "end": end}
		containers.append(container)
	return containers


def output_dir(output: Path, source: Path, corpus_shaders: Path, container: Container) -> Path:
	rel = source.relative_to(corpus_shaders)
	stage = rel.parts[0]
	expect(container.api in API_DIRS, f"{rel} api", sorted(API_DIRS), container.api)
	api_dir = API_DIRS[container.api]
	if container.gfat is None:
		# Standalone files sit in <stage>/<api>/; the header's api must agree with that directory.
		expect(len(rel.parts) == 3, f"{rel} path depth", "<stage>/<api>/<name>.bls", rel)
		expect(rel.parts[1] == api_dir, f"{rel} api directory", api_dir, rel.parts[1])
	return output / stage / api_dir / source.stem


def load(source: Path) -> list[Container]:
	data = source.read_bytes()
	if data[:4] == GFAT:
		return parse_gfat(data, str(source))
	container = parse_gxsh(data, str(source))
	if container.api == "":
		container.api = {"dx_5_0": "DX50", "dx_6_0": "DX60", "mtl_1_1": "MT11"}[source.parent.name]
		container.notes.append("api taken from the source directory name; the v0x1000C header has none")
	return [container]


@dataclass
class Step:
	key: str
	path: Path
	command: list[str]


def listing_chains(out: Path, index: int, blob: Blob) -> list[list[Step]]:
	"""The native listing, and the Vulkan chain (SPIR-V -> its text form -> GLSL); a chain stops at its first failure."""
	vkd3d_type = next((PROGRAM_PARTS[p] for p in blob.parts if p in PROGRAM_PARTS), None)
	if vkd3d_type is None:
		return []
	stem = f"{index:04d}"
	dxbc = str(out / "blobs" / f"{stem}.dxbc")
	if vkd3d_type == "dxbc-dxil":
		# dxc rather than vkd3d: vkd3d-compiler 2.0 rejects about 70% of the 12.1
		# DX60 blobs (unhandled DXIL semantics such as barycentrics).
		listing = out / "asm" / f"{stem}.ll"
		native = Step("native", listing, ["dxc", "-dumpbin", dxbc, "-Fc", str(listing)])
	else:
		listing = out / "asm" / f"{stem}.asm"
		native = Step("native", listing, ["vkd3d-compiler", "-x", vkd3d_type, "-b", "d3d-asm", "-o", str(listing), dxbc])
	spv = out / "spirv" / f"{stem}.spv"
	spvasm = out / "spirv" / f"{stem}.spvasm"
	glsl = out / "glsl" / f"{stem}.glsl"
	vulkan = [
		Step("spirv", spv, ["vkd3d-compiler", "-x", vkd3d_type, "-b", "spirv-binary", "-o", str(spv), dxbc]),
		Step("spirv_text", spvasm, ["spirv-dis", str(spv), "-o", str(spvasm)]),
		Step("glsl", glsl, ["spirv-cross", str(spv), "--output", str(glsl)]),
	]
	return [[native], vulkan]


def err_path(listing: Path) -> Path:
	return listing.with_name(listing.name + ".err")


def write_blobs(container: Container, out: Path) -> list[list[Step]]:
	"""Writes blobs/, returns the listing chains not yet run to completion or failure."""
	for sub in ("blobs", "asm", "spirv", "glsl"):
		(out / sub).mkdir(parents=True, exist_ok=True)
	chains = []
	for i, blob in enumerate(container.blobs):
		(out / "blobs" / f"{i:04d}.dxbc").write_bytes(blob.dxbc)
		for chain in listing_chains(out, i, blob):
			if not any(err_path(step.path).exists() for step in chain) and not chain[-1].path.exists():
				chains.append(chain)
	return chains


def run_chain(chain: list[Step]) -> None:
	for step in chain:
		if step.path.exists():
			continue
		result = subprocess.run(step.command, capture_output=True, text=True)
		if result.returncode != 0:
			step.path.unlink(missing_ok=True)
			err_path(step.path).write_text(result.stderr)
			return


def step_status(chain: list[Step], step: Step, out: Path) -> str:
	if step.path.exists():
		return "ok"
	for earlier in chain:
		err = err_path(earlier.path)
		if err.exists():
			# Tool messages lead with the absolute blob path; the blob is already named by "file".
			first = (err.read_text().strip().splitlines() or ["(no stderr)"])[0]
			first = first.replace(str(out) + "/", "")
			return f"failed: {first}" if earlier is step else f"not run: {earlier.key} failed"
	return "not run"


def manifest(container: Container, out: Path) -> dict:
	blob_slots: list[list[int]] = [[] for _ in container.blobs]
	for slot, blob_index in enumerate(container.slots or []):
		if blob_index is not None:
			blob_slots[blob_index].append(slot)
	blobs = []
	for i, blob in enumerate(container.blobs):
		listings = {}
		for chain in listing_chains(out, i, blob):
			for step in chain:
				listings[step.key] = {"file": step.path.relative_to(out).as_posix(), "status": step_status(chain, step, out)}
		blobs.append({"file": f"blobs/{i:04d}.dxbc", "slots": blob_slots[i] if container.slots is not None else None,
		              "stream_offset": blob.stream_offset, "block_header": blob.block_header.hex(),
		              "dxbc_size": len(blob.dxbc), "trailing_bytes": blob.trailing, "parts": blob.parts,
		              "listings": listings})
	return {"source": container.source, "gfat": container.gfat, "version": f"{container.version:#x}",
	        "api": container.api, "header": container.header, "notes": container.notes,
	        "slots": container.slots, "slot_hashes": container.slot_hashes, "blobs": blobs}


LISTING_KEYS = ("native", "spirv", "spirv_text", "glsl")


def summary(m: dict, out: Path, output: Path, corpus_shaders: Path, fdids: dict[str, int]) -> dict:
	slots = m["slots"]
	source_rel = ("shaders" / Path(m["source"]).relative_to(corpus_shaders)).as_posix()
	return {"dir": out.relative_to(output).as_posix(), "source": source_rel,
	        "source_fdid": fdids.get(source_rel), "gfat_entry": m["gfat"]["api"] if m["gfat"] else None,
	        "api": m["api"], "version": m["version"],
	        "slots": len(slots) if slots is not None else None,
	        "compiled_slots": sum(s is not None for s in slots) if slots is not None else None,
	        "blobs": len(m["blobs"]),
	        **{f"{key}_ok": sum(b["listings"].get(key, {}).get("status") == "ok" for b in m["blobs"]) for key in LISTING_KEYS}}


def default_listfile() -> Path | None:
	"""husk's own config is the one place the listfile path lives (README.md's "Config file")."""
	config = Path(os.environ.get("HUSK_CONFIG", Path.home() / ".config" / "husk" / "config.toml"))
	if not config.exists():
		return None
	value = tomllib.loads(config.read_text()).get("listfile")
	return Path(value) if value else None


def shader_fdids(listfile: Path | None) -> dict[str, int]:
	"""Lowercased listfile path -> FileDataID, for .bls rows only."""
	if listfile is None:
		return {}
	fdids = {}
	with listfile.open(encoding="utf-8", errors="replace") as f:
		for line in f:
			fdid, _, path = line.rstrip("\n").partition(";")
			if path.endswith(".bls"):
				fdids[path.lower()] = int(fdid)
	return fdids


def write_mapping(index: list[dict], output: Path) -> None:
	columns = ["source", "source_fdid", "gfat_entry", "api", "version", "dir", "slots", "compiled_slots", "blobs",
	           *(f"{key}_ok" for key in LISTING_KEYS)]
	with (output / "mapping.csv").open("w", newline="") as f:
		writer = csv.DictWriter(f, columns)
		writer.writeheader()
		for row in index:
			writer.writerow({c: row[c] for c in columns})


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
	parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
	parser.add_argument("--listfile", type=Path, default=default_listfile(),
	                    help="community listfile CSV, for source FileDataIDs in mapping.csv (default: husk's config)")
	args = parser.parse_args()
	corpus_shaders = args.corpus / "shaders"
	output = args.output.resolve()
	output.mkdir(parents=True, exist_ok=True)
	sources = sorted(corpus_shaders.rglob("*.bls"))
	print(f"{len(sources)} shader files under {corpus_shaders} -> {output}", flush=True)

	errors: list[str] = []
	pending: list[tuple[Container, Path]] = []
	done: list[Path] = []
	chains: list[list[Step]] = []
	for source in sources:
		try:
			for container in load(source):
				out = output_dir(output, source, corpus_shaders, container)
				if (out / "manifest.json").exists():
					done.append(out)
					continue
				chains += write_blobs(container, out)
				pending.append((container, out))
		except (FormatError, zlib.error, KeyError, struct.error) as e:
			errors.append(f"{source}\t{type(e).__name__}: {e}")
	print(f"{len(done)} containers already exported, {len(pending)} to write, {len(chains)} listing chains to run",
	      flush=True)

	with cf.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
		for n, _ in enumerate(pool.map(run_chain, chains), 1):
			if n % 4000 == 0 or n == len(chains):
				print(f"  ran {n}/{len(chains)} listing chains", flush=True)

	for container, out in pending:
		(out / "manifest.json").write_text(json.dumps(manifest(container, out), indent=1))
		done.append(out)

	fdids = shader_fdids(args.listfile)
	index = [summary(json.loads((out / "manifest.json").read_text()), out, output, corpus_shaders, fdids)
	         for out in sorted(done)]
	(output / "index.json").write_text(json.dumps(index, indent=1))
	write_mapping(index, output)
	(output / "README.md").write_text((REPO_ROOT / "tools" / "export_shaders_README.md").read_text())
	(output / "errors.log").write_text("".join(line + "\n" for line in errors))
	totals = ", ".join(f"{sum(c[f'{key}_ok'] for c in index)} {key}" for key in LISTING_KEYS)
	print(f"{len(index)} containers, {sum(c['blobs'] for c in index)} blobs; ok listings: {totals}; "
	      f"{len(errors)} files failed (errors.log)")
	return 0


if __name__ == "__main__":
	sys.exit(main())
