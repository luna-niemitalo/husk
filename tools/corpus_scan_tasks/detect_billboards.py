import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import corpus_scan_framework as csf  # noqa: E402 -- see sys.path.insert above; husk_info_json read from there, see REFACTOR/CLI_AND_TOOLING.md §3

class BillboardDetectionTask:
	"""
	Scans M2 files for the 'billboard' property in bone records, via
	`husk info --json` (REFACTOR/CLI_AND_TOOLING.md §3 -- structured JSON
	instead of scraping `husk info`'s prose, which husk makes no promise to
	keep stable).

	This task is designed to provide data for investigating:
	1. Bone counts in billboarded models.
	2. The relationship (index) of the billboard bone within the hierarchy.
	3. Whether billboards tend to be attached to the first, second, or last bone.
	"""

	GLOB_PATTERNS = ["*.m2"]

	# These fields will appear as columns in the resulting CSV
	FIELDNAMES = [
		"total_bones",
		"billboard_bone_id",
		"billboard_type"
	]

	# We must use 'process' mode because we are shelling out to `husk`
	PARALLEL_MODE = "process"

	@staticmethod
	def analyze(path: Path) -> dict | None:
		info = csf.husk_info_json(path)
		if info is None:
			return None

		total_bones = info["bones"]["count"]
		billboard_bones = info["bones"]["billboard_bones"]

		# Same "first match" behavior the old regex .search() had -- only the
		# first billboarded bone in index order is reported, not every one.
		if not billboard_bones:
			return None
		first = billboard_bones[0]

		return {
			"total_bones": total_bones,
			"billboard_bone_id": first["index"],
			"billboard_type": first["billboard_mode"]
		}

	@staticmethod
	def summarize(rows: list[dict], total_files: int) -> list[str]:
		if not rows:
			return ["No billboarded models found in corpus."]

		billboard_count = len(rows)
		percentage = (billboard_count / total_files) * 100 if total_files > 0 else 0

		# Calculate some quick statistics for the summary
		avg_bones = sum(r['total_bones'] for r in rows if r['total_bones']) / billboard_count

		return [
			f"Found {billboard_count} / {total_files} models with billboards ({percentage:.2f}%)",
			f"Average bone count in billboarded models: {avg_bones:.2f}",
			f"Note: Check 'billboard_bone_id.csv' for specific bone-to-billboard mappings."
		]
