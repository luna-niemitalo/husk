"""Checks the Blender addon's reading of a model's animation_fallbacks
reference (blender/addons/husk_addon/docs.py `list_document`,
`fallback_animation`): inline, external, and an external file that's missing.

    direnv exec . tools/venv/bin/python tests/animation_fallbacks_check.py

Python because the addon is; docs.py needs no Blender. Exits non-zero on the
first failed check, printing expected and actual.
"""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "blender" / "addons"))
from husk_addon import docs  # noqa: E402


def animation(row, name=None):
    ref = {"id": {"kind": "db2_row", "table": "AnimationData", "row": row}}
    if name:
        ref.update(name=name, name_source="community")
    return ref


TABLE = {
    "kind": "animation_fallbacks",
    "space": docs.SPACE,
    "references": [],
    "otherwise": animation(0, "Stand"),
    "fallbacks": [
        {"animation": animation(5, "Run"), "fallback": animation(4, "Walk")},
        {"animation": animation(4, "Walk"), "fallback": animation(0, "Stand")},
        {"animation": animation(90), "fallback": animation(91)},
        {"animation": animation(91), "fallback": animation(90)},
    ],
}
LIST_ASSET = {"id": {"kind": "none"}, "name": "animation_fallbacks", "name_source": "synthesized"}


def model(reference_extra):
    reference = dict({"id": docs.reference_id("animation_fallbacks", LIST_ASSET), "kind": "animation_fallbacks",
                      "asset": dict(LIST_ASSET)}, **reference_extra)
    return {"space": docs.SPACE, "references": [reference]}


def expect(what, expected, actual):
    if expected != actual:
        sys.exit("FAIL %s: expected %r, got %r" % (what, expected, actual))
    print("ok   %s" % what)


def main():
    stand, walk, run = (docs.identity_key(animation(r)) for r in (0, 4, 5))
    with tempfile.TemporaryDirectory() as tmp:
        tree = Path(tmp)
        bundle = tree / "models" / "m.bundle"
        bundle.mkdir(parents=True)

        inline = docs.list_document(model({"document": TABLE}), str(bundle), "animation_fallbacks")
        expect("inline: the embedded table", TABLE, inline)

        external_model = model({})
        external_model["references"][0]["asset"]["uri"] = "../../animation_fallbacks.json"
        try:
            docs.list_document(external_model, str(bundle), "animation_fallbacks")
            sys.exit("FAIL missing: expected a FormatError, got none")
        except docs.FormatError as e:
            expected = "expected the animation_fallbacks document at %s, got no such file" % (
                tree / "animation_fallbacks.json")
            expect("missing: the error names the path expected", expected, str(e))

        (tree / "animation_fallbacks.json").write_text(json.dumps(TABLE))
        external = docs.list_document(external_model, str(bundle), "animation_fallbacks")
        expect("external: the shared file's table", TABLE, external)

        expect("no reference: None", None, docs.list_document({"references": []}, str(bundle), "animation_fallbacks"))

    expect("a clip of its own plays", run, docs.fallback_animation(TABLE, animation(5), {run, walk}))
    expect("Run falls back to Walk", walk, docs.fallback_animation(TABLE, animation(5), {walk, stand}))
    expect("Run reaches Stand through Walk", stand, docs.fallback_animation(TABLE, animation(5), {stand}))
    expect("an unlisted animation falls back to otherwise", stand, docs.fallback_animation(TABLE, animation(77), {stand}))
    expect("a chain without a clip ends in None", None, docs.fallback_animation(TABLE, animation(5), set()))
    expect("a cycle ends in None", None, docs.fallback_animation(TABLE, animation(90), {stand}))
    # WoW's Stand (row 0, `otherwise`) has an entry of its own: 0 -> 147.
    closed = docs.identity_key(animation(147))
    with_stand = dict(TABLE, fallbacks=TABLE["fallbacks"] + [{"animation": animation(0), "fallback": animation(147)}])
    expect("otherwise's own entry is followed", closed, docs.fallback_animation(with_stand, animation(77), {closed}))


if __name__ == "__main__":
    main()
