#!/usr/bin/env python3
"""Live-checks tools/blizzard_profile_to_appearance_string.py's ASSUMED_PATHS
against a real Blizzard Profile API response, one field at a time, printing
expected-vs-got for each. This is the follow-up to that script's own
docstring warning ("never confirmed against a live response") -- run it once
real API credentials exist, instead of re-deriving the exploration by hand
with a chain of curl/jq calls.

Needs a Blizzard API client (free, self-service, no approval wait):
https://develop.battle.net/access/clients -- OAuth client-credentials grant,
no per-user login required (Profile APIs are public for any character with
default privacy settings). Pass credentials via env vars, not argv, so they
never end up in shell history:

    export BATTLENET_CLIENT_ID=...
    export BATTLENET_CLIENT_SECRET=...
    tools/venv/bin/python3 tools/verify_blizzard_api_schema.py \\
        --region eu --realm argent-dawn --character linore

Every step below prints [PASS]/[FAIL] plus what was expected and what the
live response actually had. A [FAIL] here means
blizzard_profile_to_appearance_string.py's ASSUMED_PATHS needs updating --
and only that dict, per its own module doc comment.
"""

import argparse
import base64
import json
import os
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(__file__))
from blizzard_profile_to_appearance_string import ASSUMED_PATHS, dig  # noqa: E402

failures = 0


def report(step: str, expected: str, got: str, ok: bool) -> None:
    global failures
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {step}")
    print(f"    expected: {expected}")
    print(f"    got:      {got}")
    if not ok:
        failures += 1


def http_json(url: str, *, method: str = "GET", data: bytes = None, headers: dict = None) -> dict:
    req = urllib.request.Request(url, data=data, headers=headers or {}, method=method)
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read())


def get_token(region: str, client_id: str, client_secret: str) -> str:
    url = f"https://{region}.battle.net/oauth/token"
    basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    body = b"grant_type=client_credentials"
    headers = {"Authorization": f"Basic {basic}", "Content-Type": "application/x-www-form-urlencoded"}
    try:
        payload = http_json(url, method="POST", data=body, headers=headers)
    except urllib.error.HTTPError as e:
        report("OAuth token request", "200 with 'access_token'", f"HTTP {e.code}: {e.read().decode()[:200]}", False)
        return ""
    got = "'access_token' present" if "access_token" in payload else f"keys={list(payload.keys())}"
    report("OAuth token request", "'access_token' present", got, "access_token" in payload)
    return payload.get("access_token", "")


def fetch_profile(region: str, realm: str, character: str, endpoint: str, token: str) -> dict:
    url = (f"https://{region}.api.blizzard.com/profile/wow/character/{realm}/{character}/{endpoint}"
           f"?namespace=profile-{region}&locale=en_GB")
    headers = {"Authorization": f"Bearer {token}"}
    try:
        return http_json(url, headers=headers)
    except urllib.error.HTTPError as e:
        report(f"{endpoint} request", "200 with JSON body", f"HTTP {e.code}: {e.read().decode()[:200]}", False)
        return {}


def check_path(label: str, payload: dict, path_key: str) -> None:
    path = ASSUMED_PATHS[path_key]
    value = dig(payload, path)
    expected = f"ASSUMED_PATHS[{path_key!r}] = {path} resolves to a value"
    got = f"resolved to {value!r}" if value is not None else "path not found in response"
    report(label, expected, got, value is not None)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--region", required=True, help="e.g. eu, us")
    ap.add_argument("--realm", required=True, help="realm slug, e.g. argent-dawn")
    ap.add_argument("--character", required=True, help="character name, lowercase")
    args = ap.parse_args()

    client_id = os.environ.get("BATTLENET_CLIENT_ID")
    client_secret = os.environ.get("BATTLENET_CLIENT_SECRET")
    if not client_id or not client_secret:
        print("BATTLENET_CLIENT_ID / BATTLENET_CLIENT_SECRET must be set -- see this script's own --help",
              file=sys.stderr)
        return 2

    token = get_token(args.region, client_id, client_secret)
    if not token:
        print("\ncan't continue without a token.", file=sys.stderr)
        return 1

    equipment = fetch_profile(args.region, args.realm, args.character, "equipment", token)
    if equipment:
        report("equipment: equipped_items list present", "non-empty list",
                f"{len(dig(equipment, ASSUMED_PATHS['equipped_items']) or [])} item(s)",
                bool(dig(equipment, ASSUMED_PATHS["equipped_items"])))
        first_item = (dig(equipment, ASSUMED_PATHS["equipped_items"]) or [{}])[0]
        check_path("equipment: first item's slot type", first_item, "slot_type")
        check_path("equipment: first item's transmog appearance id", first_item, "transmog_appearance_id")

        # Items without a "transmog" object (never re-transmogged, or a slot
        # WoW doesn't support transmog on at all) exercise the fallback path
        # instead -- a real gap once let a wrong ASSUMED_PATHS entry through
        # undetected here, so check it explicitly against a real item.
        items = dig(equipment, ASSUMED_PATHS["equipped_items"]) or []
        untransmogged = next((it for it in items if "transmog" not in it), None)
        if untransmogged is not None:
            check_path("equipment: untransmogged item's fallback appearance id", untransmogged, "base_appearance_id")

    appearance = fetch_profile(args.region, args.realm, args.character, "appearance", token)
    if appearance:
        report("appearance: customizations list present", "non-empty list",
                f"{len(dig(appearance, ASSUMED_PATHS['customizations']) or [])} entrie(s)",
                bool(dig(appearance, ASSUMED_PATHS["customizations"])))
        first_cust = (dig(appearance, ASSUMED_PATHS["customizations"]) or [{}])[0]
        check_path("appearance: first customization's choice id", first_cust, "choice_id")

    print()
    if failures:
        print(f"{failures} check(s) failed -- update ASSUMED_PATHS in "
              "tools/blizzard_profile_to_appearance_string.py to match.")
        return 1
    print("all checks passed -- ASSUMED_PATHS matches the live schema.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
