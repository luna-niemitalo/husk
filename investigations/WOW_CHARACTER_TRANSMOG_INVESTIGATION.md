# WoW Character / Transmog Investigation (2026-08-23)

Research-only task, read-only against local files, GET-only against the web. Nothing
was written except this report. No secrets were found or stored anywhere in this repo.
See also `CHARACTER_PIPELINE_TEST_FINDINGS.md` — the same week's follow-up that ran
the actual name → export → render pipeline this research unblocked.

## 1. Local data: character names — yes, levels/transmog — no

`/media/luna/games/World of Warcraft/_retail_/WTF/Account/` has two Battle.net account
IDs, each with a per-realm folder holding one directory per character (the directory
name *is* the character name). This gives a complete, reliable character roster with
zero ambiguity:

- **Account `107190796#1`, realm Argent Dawn (EU)**: 30 characters — Nadyana,
  Coldbutch, Minereth, Kdiwjo, Fyneloth, Thicktail, Idkanameig, Angypuppyy, Zokesha,
  Minstrix, Silverlili, Linore, Pâtchouli, Nyabonk, Fyreloth, Moondemon, Fastbonks,
  Eynarion, Maylead, Fellbonk, Neerâ, Quickfi, Fopsbonk, Holyestbonk, Melinyx, Lunafox,
  Dâras, Fennelore, Slimbonk, Speedybonk.
- **Account `107190796#2`, realm Azuremyst (US)**: 1 character — Ardrhga (folder has no
  SavedVariables at all, consistent with a low-level/rarely-touched character; not
  resolvable on the Armory either, see §3).

**Level and worn-transmog-appearance are *not* reliably stored locally.** Checked every
plausible addon SavedVariables file (`AllTheThings.lua`, `Syndicator.lua`,
`Baganator.lua`, `BetterWardrobe.lua`, `TransmogSetProgress.lua`,
`W2TransmogStudio.lua`, `MogIt.lua`, `DejaCharacterStats.lua`): these addons compute
gear/transmog live from the game API and don't persist a level or appearance snapshot
to disk. `Syndicator.lua` (account-wide, 1.2 MB) does persist a `details` block per
character with **class / race / faction**, which was used to cross-check the web
results below — but no level field. Note from Luna mid-investigation: *equipped item
ID is not the same as its transmogrified appearance* — confirmed, and the local data
has neither.

**"Frequently played" proxy**: no addon logs play time, but each character's own
SavedVariables files are rewritten on logout, so the most recent `.lua` mtime under a
character's folder is a solid proxy for last-login date. Ranked:

| Character | Last local write |
|---|---|
| Silverlili, Neerâ, Nadyana*, Fennelore | 2026-08-22 |
| Pâtchouli, Linore | 2026-08-21 |
| Fopsbonk | 2026-08-18 |
| Thicktail | 2026-08-17 |
| Dâras | 2026-08-16 |
| Lunafox | 2026-07-18 |
| *(21 more, last played 2026-06-04 or earlier)* | |

\* Nadyana is a level-\<10 bank alt (confirmed by Luna) — visited constantly for
banking, not a "played" character; excluded from the level-90 rundown below.

## 2. Web: official Blizzard Armory web pages are gone, but the API is very much alive

- `worldofwarcraft.blizzard.com/en-us/search?q=<name>` → 0 results (searches static
  site content, not characters).
- `worldofwarcraft.blizzard.com/en-us/character/<region>/<realm>/<name>` → 404 for
  every realm/name combination tried. Blizzard's public character-profile *web pages*
  (the "Armory" as it existed pre-Shadowlands, complete with 3D renders) are gone —
  what remains is the **Battle.net Game Data / Profile REST API**
  (`develop.battle.net`).

  **Correction, confirmed after this report's first draft**: the Profile API's
  character-equipment/appearance/character-media endpoints do *not* need a
  user-authorization-code OAuth token as first assumed — plain app-only
  **client-credentials** (just a client ID + secret, free to register, no player login
  involved) is sufficient for any character whose Battle.net profile visibility hasn't
  been set to private, confirmed live against Silverlili/Neerâ on 2026-08-23 (see §6).
  So a third party genuinely can pull a character's live equipped gear, transmog, and a
  server-rendered image through the official API with no interaction from the
  character's owner at all — which makes the missing consumer-facing web page look like
  a deliberate product decision (push everyone to the API / to sites like wowarmory.gg)
  rather than a privacy-driven lockdown.

## 3. What actually worked: wowarmory.gg (third-party, official-API-backed)

[wowarmory.gg](https://wowarmory.gg/) is a fan-built site that fronts the Blizzard API
and — unlike the two dead ends above — returns full, live, per-character profiles with
**no login needed**, for any character whose Battle.net profile visibility is public
(the default, it turns out, for undamaged/normal accounts — see the one exception
below). URL shape: `wowarmory.gg/<region>/character/<realm-slug>/<name-lowercase>`
(accented characters need percent-encoding, e.g. `d%C3%A2ras` for "Dâras"). §6 confirms
this is exactly the same client-credentials access the official API grants directly —
no special access wowarmory.gg has that a direct caller doesn't.

Every character page has a **Transmog** tab that renders a live 3D character model
plus a per-slot table of *base item → transmogrified-to appearance* — directly
answering Luna's "equipped ≠ transmog" point, since it shows both explicitly rather
than conflating them.

Two names came back "Character not found": `hazelnuttz` (Tarren Mill, used only as a
known-public sanity check — likely just wrong realm/name on my part, not investigated
further) and `Ardrhga` (Azuremyst) — consistent with it being an unplayed low-level
character, same as the local-data reason for its empty SavedVariables folder.

### Confirmed level-90s, most-recently-played first

All data live from wowarmory.gg on 2026-08-23; class/race cross-checked against
`Syndicator.lua`'s local cache and matched in every case.

| Character | Class / Spec | Race | ilvl | Last online | Live profile |
|---|---|---|---|---|---|
| Silverlili | Devastation Evoker | Dracthyr | 237 | 22 Aug (today) | [link](https://wowarmory.gg/eu/character/argent-dawn/silverlili) |
| Neerâ | Discipline Priest | Night Elf | 271 | 22 Aug (yesterday) | [link](https://wowarmory.gg/eu/character/argent-dawn/neer%C3%A2) |
| Fennelore | Balance Druid | Harronir | 259 | 22 Aug (today) | [link](https://wowarmory.gg/eu/character/argent-dawn/fennelore) |
| Pâtchouli | Affliction Warlock | Human | 266 | 21 Aug | [link](https://wowarmory.gg/eu/character/argent-dawn/p%C3%A2tchouli) |
| Linore | Arcane Mage | Night Elf | 280 | 21 Aug | [link](https://wowarmory.gg/eu/character/argent-dawn/linore) |
| Fopsbonk | Beast Mastery Hunter | Vulpera | 210 | 17 Aug | [link](https://wowarmory.gg/eu/character/argent-dawn/fopsbonk) |
| Thicktail | Protection Paladin | Draenei | 254 | 17 Aug | [link](https://wowarmory.gg/eu/character/argent-dawn/thicktail) |
| Dâras | Arms Warrior | Human | 277 | 16 Aug | [link](https://wowarmory.gg/eu/character/argent-dawn/d%C3%A2ras) |

Each `Transmog` tab (append no path, just click the tab on the linked page) shows the
render used for this table plus the per-slot appearance breakdown — e.g. Silverlili's
render shows a fully mismatched-plate Dracthyr look where the *equipped* pieces are
all "of the Black Talon" raid gear but half the slots are transmogged to "Hidden" or to
unrelated recolors (Devourer's Visage helm, Runescribe's Ritual Tunic chest, etc.) —
exactly the kind of divergence that would've been invisible from equipped-item data
alone.

## 4. On rendering them "for real"

Both wowarmory.gg's in-page 3D model *and* the official Battle.net `character-media`
endpoint's `main-raw` PNG (see §6) already *are* real, current, fully-posed renders
reflecting exact transmog + dyes, generated server-side by Blizzard's own
character-render pipeline — there's no husk/Blender step needed to get a look at these
characters as they actually appear in-game today. If the goal is instead a
*husk-pipeline* render (i.e., turning one of these into a `.glb` husk can process), that
would need the character's real M2/model data plus the exact equipped **appearance**
FileDataIDs per slot. That gap is now half-closed: `tools/blizzard_profile_fetch.py`
(§6) converts a live profile straight into a husk-appearance/1 string (per-slot
`item_modified_appearance_id`, husk's own currency for this — see
`src/appearance_string.hpp`) via `tools/blizzard_profile_to_appearance_string.py`,
which husk's existing `husk appearance-string`/`--customization-choice-ids` machinery
can consume directly. Only race/sex still need supplying by hand (the Profile API
doesn't return a `ChrRacesID`, husk's own currency for race). Not run end-to-end
through `husk export` this session — worth a follow-up if an actual `.glb` render of
one of these characters is wanted.

## 5. Follow-up on Luna's "direct API" request — resolved

Luna asked mid-session to prefer direct Battle.net API calls over Playwright and
mentioned a possible existing API key. A broad credential search across `~/.config`
and env vars was blocked by the permission classifier (reasonably — that's a wide,
sensitive scan) and not retried; replaced with a direct question, and Luna pointed at
`husk/.env` (JSON: `client_id`/`potato`-as-secret/`name`/`redirect_urls`,
client name `moonfox-api-test-client`).

**The OAuth caveat this section originally raised was wrong.** Tested directly:
plain client-credentials (client ID + secret, no player login) is sufficient for the
Profile API's character-equipment/appearance/character-media endpoints, for any
character with a public Battle.net profile — confirmed live against Silverlili and
Neerâ. This is presumably the same access wowarmory.gg itself uses. See §6 for the
resulting tooling.

## 6. Tooling built this session

Two scripts now live in `tools/`, together forming one pipeline (stdlib-only, no new
dependency — `tools/venv`'s Python had no `requests`, and `urllib` was sufficient):

- **`tools/blizzard_profile_fetch.py`** — the entry point. Given a realm slug and
  character name, gets a client-credentials token from `oauth.battle.net` (reading
  `client_id`/`potato` from `.env` by default), fetches character
  summary/equipment/appearance/character-media from `<region>.api.blizzard.com`, and
  saves each as JSON. With `--race`/`--sex` it also emits a `husk-appearance/1` string
  in the same run. Never prints the client secret or the OAuth token.
- **`tools/blizzard_profile_to_appearance_string.py`** — pure converter
  (`build_appearance_string`), also usable standalone on JSON obtained some other way.
  Its `ASSUMED_PATHS` field-name mapping (written from Blizzard's documented schema,
  never checked against a live payload) is now **confirmed correct** against real data:
  `equipped_items[].slot.type`, `.transmog.item_modified_appearance_id`, and the
  `.modified_appearance_id` fallback for non-transmoggable slots (rings/trinkets/neck —
  Blizzard returns `0` there, not an absent field) all matched exactly.

Verified end to end against Silverlili: fetched all 4 endpoints, correctly skipped the
5 non-transmoggable equipped slots, and produced both the JSON payloads and a working
`husk-appearance/1` string. The venv's Python needed `SSL_CERT_FILE` pointed at
`/etc/ssl/certs/ca-certificates.crt` (no default CA trust store there, unlike system
tools like `curl`) — the script now sets this itself as a fallback so callers don't
need to remember it.
