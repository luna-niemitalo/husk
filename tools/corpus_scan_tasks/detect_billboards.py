import re
import subprocess
from pathlib import Path

class BillboardDetectionTask:
	"""
	Scans M2 files for the 'billboard' property in bone records using `husk info`.

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
		try:
			# Run husk info on the file
			# We use check=True to ensure we don't process error output
			result = subprocess.run(
				["/home/luna/dev/husk/build/husk", "info", str(path)],
				capture_output=True,
				text=True,
				check=True
			)
			output = result.stdout
		except subprocess.SubprocessError as e:
			# write a subprocess crash to a log file
			with open("husk_errors.log", "a") as log_file:
				log_file.write(f"Error processing {path}: {e}\n")
				return None
		except Exception as e:
			# If husk fails or isn't found, skip the file
			print(f"Warning: Failed to run husk on {path}. Error: {e}")
			exit(1)
			return None


		# Regex patterns to extract data from husk info output
		# Example: "bones: 3 (offset 0x190)"
		bone_count_re = re.compile(r"bones:\s+(\d+)")
		# Example: "  bone 1: billboard=spherical"
		billboard_re = re.compile(r"bone\s+(\d+):\s+billboard=(\w+)")

		total_bones = None
		match_bone_id = None
		billboard_type = None

		# 1. Extract total bone count
		bone_count_match = bone_count_re.search(output)
		if bone_count_match:
			total_bones = int(bone_count_match.group(1))

		# 2. Extract billboard information
		# We look for the specific bone that has the billboard flag
		billboard_match = billboard_re.search(output)
		if billboard_match:
			match_bone_id = int(billboard_match.group(1))
			billboard_type = billboard_match.group(2)

		# If we found a billboard, return the data. Otherwise, return None to skip.
		if billboard_type:
			return {
				"total_bones": total_bones,
				"billboard_bone_id": match_bone_id,
				"billboard_type": billboard_type
			}

		return None

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
