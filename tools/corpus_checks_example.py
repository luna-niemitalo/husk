#!/usr/bin/env python3
"""Minimal reference usage of corpus_checks.py: runs every check, in order,
against exactly one file. Not a corpus scanner -- no threading, no
scheduling, no batching. That's the point: this is the smallest possible
example of what one worker in a real scheduler does for one file, to check
against when wiring up the real thing.

Usage:
	direnv exec . uv run --python tools/venv/bin/python tools/corpus_checks_example.py <path/to/model.m2> <corpus_root>
"""

import json
import os
import shutil
import sys
from pathlib import Path
import time
from tracemalloc import start
from concurrent.futures import ThreadPoolExecutor, as_completed
import tqdm


import corpus_checks as cc

def find_m2_files(root: Path) -> list[str]:
	print(f"scanning for .m2 files in {root}")
	files = []
	for p in root.rglob("*.m2"):
		files.append(str(p.resolve()))
	print(files[0:10])
	return files

def process_file(path: str, bar: tqdm.tqdm) -> None:
	cc.test_header(path)
	cc.test_export(path)
	cc.test_dump_chunks(path)
	cc.test_fidelity(path)
	cc.test_finite(path)
	cc.test_mesh_completeness(path)
	cc.run_gltf_validator(cc.scratch_glb_path(path))
	shutil.copyfile(cc.status_path(path), path + ".status.json")

	cc.cleanup(path)

	bar.update(1)

def main() -> int:
	if len(sys.argv) != 2:
		print(f"usage: {sys.argv[0]} <corpus_root>", file=sys.stderr)
		return 1

	cc.CORPUS_ROOT = Path(sys.argv[1]).resolve()
	print(f"corpus root: {cc.CORPUS_ROOT}")
	cc.STATUS_DIR = Path("/dev/shm/husk-corpus-status")
	cc.SCRATCH_DIR = Path("/dev/shm/husk-corpus-scratch")

	m2_files = find_m2_files(cc.CORPUS_ROOT)
	print(f"found {len(m2_files)} .m2 files")
	bar = tqdm.tqdm(total=len(m2_files), desc="Processing files", unit="file")

	with ThreadPoolExecutor(max_workers=40) as pool:
		futures = {pool.submit(process_file, f, bar): f for f in m2_files}

		for fut in as_completed(futures):
			path = futures[fut]
			try:
				fut.result()
			except Exception as e:
				tqdm.write(f"[error] {path}: {e}")

	return 0


if __name__ == "__main__":
	sys.exit(main())
