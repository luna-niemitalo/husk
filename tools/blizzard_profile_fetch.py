#!/usr/bin/env python3
"""Fetches a WoW character's public Battle.net Profile API data (character
summary, equipment w/ transmog, appearance/customizations, character-media
render URLs) and saves each payload as JSON.

Needs a Battle.net API client (create one free at develop.battle.net) with
its credentials in a JSON file (--env-file, default ./.env) shaped:

    {"client_id": "...", "potato": "...", "name": "...", "redirect_urls": [...]}

("potato" is the client secret -- deliberately not named "secret" so a
`grep -i secret` won't turn it up.) Client-credentials alone is enough: these
endpoints are public for any character whose Battle.net profile visibility
hasn't been set to private (confirmed live, 2026-08-23 -- no user login/
OAuth-authorization-code flow needed, contrary to this repo's earlier
assumption).

Usage:
    tools/venv/bin/python3 tools/blizzard_profile_fetch.py \\
        argent-dawn silverlili --out-dir /tmp/wow_profiles

Pass --race/--sex (ChrRacesID + sex, not derivable from either payload) to
also emit a husk-appearance/1 string in the same run, via
tools/blizzard_profile_to_appearance_string.py's build_appearance_string --
no separate manual conversion step needed:

    tools/venv/bin/python3 tools/blizzard_profile_fetch.py \\
        argent-dawn silverlili --race 52 --sex 0 --out-dir /tmp/wow_profiles

Prints a one-line summary (name/level/class/race/ilvl/render URL) and exits;
never prints the client secret or the OAuth token.
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from blizzard_profile_to_appearance_string import build_appearance_string

# The uv-managed venv interpreter has no default CA trust store on NixOS
# (unlike system tools like curl, which pick up /etc/ssl/certs on their
# own) -- without this, every HTTPS call fails with
# CERTIFICATE_VERIFY_FAILED. Only set as a fallback, never overriding a
# caller's own SSL_CERT_FILE/REQUESTS_CA_BUNDLE.
if "SSL_CERT_FILE" not in os.environ and "REQUESTS_CA_BUNDLE" not in os.environ:
    _system_ca_bundle = Path("/etc/ssl/certs/ca-certificates.crt")
    if _system_ca_bundle.exists():
        os.environ["SSL_CERT_FILE"] = str(_system_ca_bundle)

OAUTH_TOKEN_URL = "https://oauth.battle.net/token"

# realm/name go in the URL path; everything else in this dict is appended
# after the realm/name segment.
ENDPOINTS = {
    "summary": "",
    "equipment": "/equipment",
    "appearance": "/appearance",
    "media": "/character-media",
}


def fetch_json(url: str, token: str | None = None, *, method: str = "GET", data: bytes | None = None,
                auth: tuple[str, str] | None = None) -> dict:
    req = urllib.request.Request(url, data=data, method=method)
    if token is not None:
        req.add_header("Authorization", f"Bearer {token}")
    if auth is not None:
        import base64
        basic = base64.b64encode(f"{auth[0]}:{auth[1]}".encode()).decode()
        req.add_header("Authorization", f"Basic {basic}")
    if data is not None:
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
    try:
        with urllib.request.urlopen(req) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        raise SystemExit(f"error: {method} {url} -> HTTP {e.code}\n{body}")


def get_token(client_id: str, client_secret: str) -> str:
    body = urllib.parse.urlencode({"grant_type": "client_credentials"}).encode()
    resp = fetch_json(OAUTH_TOKEN_URL, method="POST", data=body, auth=(client_id, client_secret))
    token = resp.get("access_token")
    if not token:
        raise SystemExit("error: token response had no access_token")
    return token


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("realm", help="realm slug, lowercase-dashed, e.g. 'argent-dawn'")
    ap.add_argument("name", help="character name, lowercase, e.g. 'silverlili'")
    ap.add_argument("--region", default="eu", choices=("eu", "us", "kr", "tw"))
    ap.add_argument("--locale", default="en_US")
    ap.add_argument("--env-file", default=".env", help="path to the client_id/potato JSON (default: ./.env)")
    ap.add_argument("--out-dir", default=".", help="directory to save the per-endpoint JSON files into")
    ap.add_argument("--race", type=int, help="ChrRacesID -- if given with --sex, also emit a husk-appearance/1 string")
    ap.add_argument("--sex", type=int, choices=(0, 1), help="0=male, 1=female -- see --race")
    args = ap.parse_args()

    if (args.race is None) != (args.sex is None):
        ap.error("--race and --sex must be given together")

    creds = json.loads(Path(args.env_file).read_text())
    token = get_token(creds["client_id"], creds["potato"])

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    realm_enc = urllib.parse.quote(args.realm.lower())
    name_enc = urllib.parse.quote(args.name.lower())
    base = f"https://{args.region}.api.blizzard.com/profile/wow/character/{realm_enc}/{name_enc}"
    ns = f"profile-{args.region}"

    payloads = {}
    for key, suffix in ENDPOINTS.items():
        url = f"{base}{suffix}?namespace={ns}&locale={args.locale}"
        payloads[key] = fetch_json(url, token=token)
        out_path = out_dir / f"{args.name}_{key}.json"
        out_path.write_text(json.dumps(payloads[key], indent=2))

    summary = payloads["summary"]
    media = payloads["media"]
    render = next((a["value"] for a in media.get("assets", []) if a.get("key") == "main-raw"), None)

    print(f"{summary.get('name')}  level {summary.get('level')}  "
          f"{summary.get('character_class', {}).get('name')}  {summary.get('race', {}).get('name')}")
    print(f"equipped ilvl {summary.get('equipped_item_level')}  "
          f"average ilvl {summary.get('average_item_level')}")
    if render:
        print(f"render: {render}")
    print(f"saved JSON to {out_dir}/{args.name}_{{{','.join(ENDPOINTS)}}}.json")

    if args.race is not None:
        appearance_string = build_appearance_string(
            payloads["equipment"], payloads["appearance"], args.race, args.sex)
        print(appearance_string)
        (out_dir / f"{args.name}_appearance_string.txt").write_text(appearance_string + "\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
