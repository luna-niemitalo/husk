#!/usr/bin/env python3
"""Converts Blizzard Character Equipment/Appearance Summary API JSON into a
husk-appearance/1 string (src/appearance_string.hpp's grammar).

Fetch the two payloads yourself first (see README section this script's
own --help prints), e.g.:

    curl -s -H "Authorization: Bearer $TOKEN" \\
      "https://eu.api.blizzard.com/profile/wow/character/<realm>/<name>/equipment?namespace=profile-eu&locale=en_GB" \\
      > equipment.json
    curl -s -H "Authorization: Bearer $TOKEN" \\
      "https://eu.api.blizzard.com/profile/wow/character/<realm>/<name>/appearance?namespace=profile-eu&locale=en_GB" \\
      > appearance.json

Then:

    python3 tools/blizzard_profile_to_appearance_string.py \\
      --race 52 --sex 0 equipment.json appearance.json

THIS SCRIPT'S CORE ASSUMPTION, ISOLATED ON PURPOSE:
Blizzard's exact JSON field names below were never confirmed against a live
response this session (the armory site 500'd, and no Blizzard API client was
registered to call the profile endpoints directly -- see CLAUDE.md's Resume).
They're husk's best-confidence recollection of the documented API shape, not
verified paste-and-run. If a real payload uses different keys, ASSUMED_PATHS
below is the ONLY place that needs to change -- every other part of this
script, and all of husk's own appearance_string.hpp/cmd_appearance.cpp, is
independent of Blizzard's exact schema and needs no changes.
"""

import argparse
import json
import sys

# Single point of correction if Blizzard's real field names differ from what's
# assumed here. Each entry is a tuple of dict keys to walk, applied by
# `dig()` below. Update these paths (only these) if a live payload disagrees.
ASSUMED_PATHS = {
    # One entry per src/cmd_export.cpp equipped_items[]: the slot label and
    # the currently-displayed appearance's ItemModifiedAppearanceID.
    "equipped_items": ("equipped_items",),
    "slot_type": ("slot", "type"),
    "transmog_appearance_id": ("transmog", "item_modified_appearance_id"),
    # Fallback when a slot isn't transmogged (no "transmog" object at all):
    # confirmed against a real live response (2026-08-20) -- the equipped-item
    # entry itself carries a top-level "modified_appearance_id" (0 for slot
    # types WoW doesn't support transmog on, e.g. rings/trinkets/neck).
    "base_appearance_id": ("modified_appearance_id",),
    # One entry per character-appearance response: the post-Shadowlands
    # customization-choice list.
    "customizations": ("customizations",),
    "choice_id": ("choice", "id"),
}


def dig(obj, path):
    for key in path:
        if not isinstance(obj, dict) or key not in obj:
            return None
        obj = obj[key]
    return obj


def extract_gear(equipment_json: dict) -> list[tuple[str, int]]:
    gear = []
    items = dig(equipment_json, ASSUMED_PATHS["equipped_items"]) or []
    for entry in items:
        slot = dig(entry, ASSUMED_PATHS["slot_type"])
        appearance_id = dig(entry, ASSUMED_PATHS["transmog_appearance_id"])
        if appearance_id is None:
            appearance_id = dig(entry, ASSUMED_PATHS["base_appearance_id"])
        if appearance_id == 0:
            # 0 is Blizzard's real value for a slot type that doesn't support
            # transmog at all (rings/trinkets/neck) -- expected, not a gap.
            continue
        if slot is None or appearance_id is None:
            print(f"warning: skipping equipped item with no resolvable appearance id: {entry!r}",
                  file=sys.stderr)
            continue
        gear.append((str(slot), int(appearance_id)))
    return gear


def extract_customizations(appearance_json: dict) -> list[int]:
    choices = []
    entries = dig(appearance_json, ASSUMED_PATHS["customizations"]) or []
    for entry in entries:
        choice_id = dig(entry, ASSUMED_PATHS["choice_id"])
        if choice_id is None:
            print(f"warning: skipping customization entry with no choice id: {entry!r}", file=sys.stderr)
            continue
        choices.append(int(choice_id))
    return choices


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("equipment_json", help="path to the Character Equipment Summary response")
    ap.add_argument("appearance_json", help="path to the Character Appearance Summary response")
    ap.add_argument("--race", type=int, required=True, help="ChrRacesID (not derived from either payload)")
    ap.add_argument("--sex", type=int, required=True, choices=(0, 1))
    args = ap.parse_args()

    with open(args.equipment_json) as f:
        equipment_json = json.load(f)
    with open(args.appearance_json) as f:
        appearance_json = json.load(f)

    gear = extract_gear(equipment_json)
    cust = extract_customizations(appearance_json)

    cust_field = ",".join(str(c) for c in sorted(cust))
    gear_field = ",".join(f"{slot}:{aid}" for slot, aid in sorted(gear))
    print(f"husk-appearance/1 race={args.race} sex={args.sex} cust={cust_field} gear={gear_field}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
