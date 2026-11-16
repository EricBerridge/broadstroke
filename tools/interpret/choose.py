"""Interpretation trial (spec 2.14), step 1: the model chooses.

For each photo (which must have a scene file beside it) and each manner it
asks the list model of the observe step for a title, a very short poem, and
one mechanism and strength per item from the fixed list in vocab.py. It
writes <out>/<stem>.<manner>.choice.json, which can be corrected by hand.

  python choose.py --out <dir> --manners surrealism,cubism photo1.jpg photo2.jpg ...

Nothing here is part of the engine.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "observe"))
import cv2                  # noqa: E402
import numpy as np          # noqa: E402
from PIL import Image       # noqa: E402
import describe  # noqa: E402
import facts     # noqa: E402
import observe   # noqa: E402
import vocab     # noqa: E402

PROMPT = """You are a painter. You will paint this photograph in the manner of {manner}:
{meaning}.

The things in the picture, each with the mechanisms it may be given, the
one that would show best first:
{items}
The main subject is number {main}.

First give a title and a very short poem (three or four short lines) that
says what happens to these things in your painting.

Then, for every thing in the list, choose one mechanism from the list below
and a strength: 1 slight, 2 clear, 3 extreme. Say in a few words why. A
thing may only be given a mechanism named beside it in the list above.
{how}

Mechanisms:
{mechanisms}"""


def schema(n_items, names):
    return {
        "type": "object",
        "properties": {
            "title": {"type": "string"},
            "poem": {"type": "array", "items": {"type": "string"}, "minItems": 2, "maxItems": 4},
            "picks": {
                "type": "array", "minItems": n_items, "maxItems": n_items,
                "items": {
                    "type": "object",
                    "properties": {
                        "item": {"type": "integer", "minimum": 1, "maximum": n_items},
                        "mechanism": {"type": "string", "enum": names},
                        "strength": {"type": "integer", "minimum": 1, "maximum": 3},
                        "why": {"type": "string"},
                    },
                    "required": ["item", "mechanism", "strength", "why"],
                    "additionalProperties": False,
                },
            },
        },
        "required": ["title", "poem", "picks"],
        "additionalProperties": False,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("photos", nargs="+")
    ap.add_argument("--manners", default=",".join(vocab.MANNERS))
    ap.add_argument("--out", required=True)
    ap.add_argument("--config", default=os.path.join(HERE, "..", "observe", "observe.local.json"))
    ap.add_argument("--scene", default="", help="the scene file, if it is not beside the photo (for one photo)")
    ap.add_argument("--name", default="", help="the name the choice file is given, if not the photo's (for one photo)")
    args = ap.parse_args()
    manners = [m for m in args.manners.split(",") if m]
    for m in manners:
        if m not in vocab.MANNERS:
            sys.exit("unknown manner %s" % m)
    cfg = observe.load_config(args.config)
    os.makedirs(args.out, exist_ok=True)
    log = os.path.join(cfg.get("log_dir") or args.out, "interpret_server.log")
    proc, base = describe.start_server(cfg["llama_server"], cfg["list_model"], cfg["list_mmproj"], log)
    try:
        for path in args.photos:
            stem = os.path.splitext(path)[0]
            name = args.name or os.path.basename(stem)
            scene_path = args.scene or stem + ".scene.json"
            with open(scene_path, encoding="utf-8") as f:
                scene = json.load(f)
            # The model numbers the items 1..n in the order of the scene file.
            ids = [it["id"] for it in scene["items"]]
            layer = np.array(Image.open(os.path.join(os.path.dirname(scene_path), scene["item_layer"])))
            seen = Image.open(path).convert("RGB").resize((layer.shape[1], layer.shape[0]), Image.LANCZOS)
            may = facts.allowed(scene, layer, cv2.cvtColor(np.asarray(seen).astype(np.float32) / 255.0,
                                                           cv2.COLOR_RGB2LAB))
            main_no = ids.index(scene["main_subject"]) + 1 if scene.get("main_subject") in ids else 1
            _, b64 = observe.look(path)
            for manner in manners:
                meaning, allowed, how = vocab.MANNERS[manner]
                names = ["as it is"] + allowed
                listing = "\n".join("%d: %s - %s" % (
                    k + 1, it["name"], ", ".join(m for m in allowed if m in may[it["id"]]) or "as it is only")
                    for k, it in enumerate(scene["items"]))
                prompt = PROMPT.format(manner=manner, meaning=meaning, items=listing, main=main_no, how=how,
                                       mechanisms="\n".join("- %s: %s" % (n, vocab.MECHANISMS[n]) for n in names))
                answer = describe.ask(base, b64, prompt, schema(len(ids), names), 1500)
                parsed = answer["parsed"]
                if not parsed:
                    print("%s %s: no usable answer" % (name, manner), flush=True)
                    continue
                picks, refused = {}, []
                for p in parsed["picks"]:
                    i = ids[p["item"] - 1]
                    if p["mechanism"] != "as it is" and p["mechanism"] not in may[i]:
                        refused.append("%s: %s" % (scene["items"][p["item"] - 1]["name"], p["mechanism"]))
                        continue
                    picks.setdefault(i, p)
                most = vocab.MOST.get(manner)
                if most:
                    # The few that would show best; among equals the main
                    # subject, then what matters more.
                    rank = {it["id"]: it["importance"] for it in scene["items"]}
                    keep = sorted((i for i in picks if picks[i]["mechanism"] != "as it is"),
                                  key=lambda i: (-round(may[i][picks[i]["mechanism"]], 1),
                                                 i != scene.get("main_subject"), -rank[i]))[:most]
                    for i in list(picks):
                        if picks[i]["mechanism"] != "as it is" and i not in keep:
                            refused.append("%s: %s (more than %d changes)" % (
                                scene["items"][ids.index(i)]["name"], picks[i]["mechanism"], most))
                            del picks[i]
                choice = {"photo": os.path.basename(path), "manner": manner, "title": parsed["title"],
                          "poem": parsed["poem"], "refused": refused,
                          "picks": [{"id": i, "name": scene["items"][k]["name"],
                                     "mechanism": picks[i]["mechanism"] if i in picks else "as it is",
                                     "strength": picks[i]["strength"] if i in picks else 1,
                                     "shows": round(may[i].get(picks[i]["mechanism"], 0.0), 2) if i in picks else 0.0,
                                     "why": picks[i]["why"] if i in picks else ""}
                                    for k, i in enumerate(ids)]}
                with open(os.path.join(args.out, "%s.%s.choice.json" % (name, manner)), "w",
                          encoding="utf-8", newline="\n") as f:
                    json.dump(choice, f, indent=1, ensure_ascii=False)
                    f.write("\n")
                print("%s %s: %.0f s%s" % (name, manner, answer["seconds"],
                                            "; refused " + ", ".join(refused) if refused else ""), flush=True)
    finally:
        describe.stop_server(proc)


if __name__ == "__main__":
    sys.exit(main())
