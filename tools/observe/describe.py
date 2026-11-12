"""Scene-understanding trial, step 1 (spec 2.13): ask a local vision-language
model what a photograph shows.

Starts llama-server with one model, sends each photo twice (once for the
list of items and their treatments, once for boxes, to see how well the
model locates things), and writes the answers as JSON. Standard library
only. Nothing here is part of the engine.

  python describe.py --server <llama-server.exe> --model <model.gguf>
      --mmproj <mmproj.gguf> --name gemma-4-12b --boxes gemma
      --out <results dir> photo1.jpg photo2.jpg ...
"""
import argparse
import base64
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

# The fixed list the model must choose from. An item's name is free text; its
# treatment is not. This list is a first guess to be revised by the trial.
TREATMENT_MEANINGS = [
    ("skin", "bare skin of a person: face, hands, arms"),
    ("hair", "hair on a person's head, a beard"),
    ("fur_or_feathers", "the coat of an animal or bird"),
    ("fabric", "cloth, clothing, straps, bags, anything that drapes or folds"),
    ("foliage_mass", "leaves seen as a mass: a tree's crown, a bush, a hedge"),
    ("trunk_or_branches", "tree trunks, limbs, bare twigs"),
    ("grass", "lawn, field, low plants covering the ground"),
    ("flowers", "blossoms"),
    ("sky", "open sky without the clouds"),
    ("cloud", "clouds, mist, smoke"),
    ("still_water", "calm water, a pond, a wet reflecting surface"),
    ("moving_water", "sea swell, waves, a river, running water"),
    ("foam_or_spray", "white water: surf, foam, splashes, spray"),
    ("rock", "stone, cliffs, boulders, pebbles"),
    ("earth_or_sand", "soil, sand, mud, a dirt path"),
    ("wood", "worked wood: planks, a table, a fence"),
    ("metal", "bare or polished metal"),
    ("glass", "glass, crystal, a lens, a window pane"),
    ("smooth_manufactured", "painted, plastic or leather surface of a made object: a car body, a hull, a phone"),
    ("masonry", "walls, brick, concrete, roofs, paving"),
    ("food", "fruit, vegetables, prepared food"),
    ("other", "none of the above"),
]
TREATMENTS = [name for name, _ in TREATMENT_MEANINGS]

ITEMS_SCHEMA = {
    "type": "object",
    "properties": {
        "scene": {"type": "string"},
        "light_from": {"type": "string", "enum": ["left", "right", "above", "behind", "front", "diffuse"]},
        "items": {
            "type": "array",
            "minItems": 1,
            "maxItems": 24,
            "items": {
                "type": "object",
                "properties": {
                    "id": {"type": "string"},
                    "name": {"type": "string"},
                    "parent": {"type": "string"},
                    "treatment": {"type": "string", "enum": TREATMENTS},
                    "importance": {"type": "integer", "minimum": 0, "maximum": 3},
                    "edges": {"type": "string", "enum": ["hard", "soft", "lost"]},
                    "how_many": {"type": "string", "enum": ["one", "few", "many"]},
                },
                "required": ["id", "name", "parent", "treatment", "importance", "edges", "how_many"],
                "additionalProperties": False,
            },
        },
        # After the items: the model writes in this order, and can only name
        # the main subject once it has listed the candidates.
        "main_subject": {"type": "string"},
    },
    "required": ["scene", "light_from", "items", "main_subject"],
    "additionalProperties": False,
}

ITEMS_PROMPT = """You are helping a painter plan a painting of this photograph.
List the things in the picture that a painter would treat as separate
shapes, from the largest areas (sky, ground, water) down to the important
parts of the main subject (for a person: face, hair, hands, clothing; for a
tree: trunk, foliage).

For each item give:
- id: a short unique tag such as "a", "b", "c".
- name: a short plain noun phrase of one to three words that names what is
  visible, such as "wrist watch", "oak tree foliage", "sea foam".
- parent: the id of the item it is a part of, or "" if it is not a part of
  another item.
- treatment: the kind of surface, exactly one of the words in the list at
  the end.
- importance: 3 for the main subject and its key parts, 2 for things that
  matter to the picture, 1 for supporting areas, 0 for background.
- edges: "hard" if its outline is crisp, "soft" if blurred or gradual,
  "lost" if it merges into its surroundings.
- how_many: "one", "few" or "many" separate instances of it are visible.

Before the items give: scene, one sentence saying what the picture shows;
light_from, where the light comes from. After the items give main_subject,
the id of the single most important item.

List only what is really visible. Do not list the same thing twice. Where
there are several people or animals of a kind, list the kind once with
how_many set, not each one with its clothing.

Treatments:
""" + "\n".join("- %s: %s" % pair for pair in TREATMENT_MEANINGS)


def boxes_schema(key):
    return {
        "type": "array",
        "minItems": 1,   # without it Qwen3.5-9B answers with an empty list
        "maxItems": 60,  # 30 cut off the birds in a busy scene
        "items": {
            "type": "object",
            "properties": {
                key: {"type": "array", "items": {"type": "integer", "minimum": 0, "maximum": 1000},
                      "minItems": 4, "maxItems": 4},
                "label": {"type": "string"},
            },
            "required": [key, "label"],
            "additionalProperties": False,
        },
    }


# Each family was trained on its own box convention.
BOX_STYLES = {
    # Gemma: [y1, x1, y2, x2] on a 0..1000 grid.
    "gemma": ("box_2d", "Detect these in the image: {names}. Output a JSON list where each entry has "
                        "\"box_2d\" as [y1, x1, y2, x2] normalized to 0-1000 and \"label\"."),
    # Qwen: [x1, y1, x2, y2] on a 0..1000 grid.
    "qwen": ("bbox_2d", "Locate every instance of these in the image: {names}. Report the bounding box "
                        "coordinates in JSON format as a list where each entry has \"bbox_2d\" as "
                        "[x1, y1, x2, y2] and \"label\"."),
}


def post(url, payload, timeout=600):
    req = urllib.request.Request(url, data=json.dumps(payload).encode("utf-8"),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def ask(base, image_b64, prompt, schema, max_tokens):
    payload = {
        "messages": [{"role": "user", "content": [
            {"type": "image_url", "image_url": {"url": "data:image/jpeg;base64," + image_b64}},
            {"type": "text", "text": prompt},
        ]}],
        "temperature": 0.0,
        "seed": 1,
        "max_tokens": max_tokens,
        "response_format": {"type": "json_schema", "json_schema": {"name": "answer", "schema": schema}},
    }
    t0 = time.time()
    reply = post(base + "/v1/chat/completions", payload)
    seconds = time.time() - t0
    choice = reply["choices"][0]
    text = choice["message"].get("content") or ""
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        parsed = None
    return {
        "seconds": round(seconds, 2),
        "finish_reason": choice.get("finish_reason"),
        "usage": reply.get("usage"),
        "timings": reply.get("timings"),
        "parsed": parsed,
        "raw": None if parsed is not None else text,
    }


def wait_ready(base, proc, limit=300):
    t0 = time.time()
    while time.time() - t0 < limit:
        if proc.poll() is not None:
            raise RuntimeError("llama-server exited with code %s" % proc.returncode)
        try:
            with urllib.request.urlopen(base + "/health", timeout=2) as resp:
                if resp.status == 200:
                    return time.time() - t0
        except (urllib.error.URLError, OSError):
            pass
        time.sleep(0.5)
    raise RuntimeError("llama-server did not become ready")


def start_server(server, model, mmproj, log_path, port=18080, extra=()):
    """Starts llama-server with one model and waits until it answers.
    Returns the process and its base URL."""
    cmd = [server, "-m", model, "--mmproj", mmproj, "--host", "127.0.0.1", "--port", str(port),
           "-ngl", "999", "-c", "16384", "-np", "1", "--no-webui", "--reasoning", "off",
           "--log-file", log_path] + list(extra)
    base = "http://127.0.0.1:%d" % port
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_ready(base, proc)
    except Exception:
        stop_server(proc)
        raise
    return proc, base


def stop_server(proc):
    proc.terminate()
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()


def ask_items(base, image_b64):
    """The list of items in the photo, with treatments and the main subject."""
    return ask(base, image_b64, ITEMS_PROMPT, ITEMS_SCHEMA, 3000)


def item_names(parsed):
    """The distinct item names of an answer from ask_items, in order."""
    names = []
    for it in (parsed or {}).get("items", []):
        if it["name"] not in names:
            names.append(it["name"])
    return names


def ask_boxes(base, image_b64, names, style):
    """A box around each instance of each named item. `style` is the box
    convention of the model's family, a key of BOX_STYLES."""
    key, box_prompt = BOX_STYLES[style]
    boxes = ask(base, image_b64, box_prompt.format(names=", ".join(names)), boxes_schema(key), 3000)
    boxes["box_key"] = key
    return boxes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--mmproj", required=True)
    ap.add_argument("--name", required=True, help="label for the results folder")
    ap.add_argument("--boxes", choices=sorted(BOX_STYLES), required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--port", type=int, default=18080)
    ap.add_argument("--image-max-tokens", type=int, default=0)
    ap.add_argument("--image-min-tokens", type=int, default=0)
    ap.add_argument("--items-from", default="",
                    help="results folder of another model: use its item lists and ask this model only for boxes")
    ap.add_argument("--extra", nargs="*", default=[], help="more llama-server arguments")
    ap.add_argument("photos", nargs="+")
    args = ap.parse_args()

    out_dir = os.path.join(args.out, args.name)
    os.makedirs(out_dir, exist_ok=True)
    extra = list(args.extra)
    if args.image_max_tokens:
        extra += ["--image-max-tokens", str(args.image_max_tokens)]
    if args.image_min_tokens:
        extra += ["--image-min-tokens", str(args.image_min_tokens)]
    t0 = time.time()
    proc, base = start_server(args.server, args.model, args.mmproj, os.path.join(out_dir, "server.log"),
                              args.port, extra)
    try:
        print("model loaded in %.1f s" % (time.time() - t0), flush=True)
        for path in args.photos:
            stem = os.path.splitext(os.path.basename(path))[0]
            with open(path, "rb") as f:
                image_b64 = base64.b64encode(f.read()).decode("ascii")
            if args.items_from:
                with open(os.path.join(args.items_from, stem + ".json"), encoding="utf-8") as f:
                    items = json.load(f)["items"]
            else:
                items = ask_items(base, image_b64)
            names = item_names(items["parsed"])
            boxes = ask_boxes(base, image_b64, names, args.boxes) if names else None
            with open(os.path.join(out_dir, stem + ".json"), "w", encoding="utf-8", newline="\n") as f:
                json.dump({"photo": os.path.basename(path), "model": args.name, "items": items, "boxes": boxes},
                          f, indent=1)
            print("%s: items %.1f s (%s, %d items), boxes %s" % (
                stem, items["seconds"], items["finish_reason"], len(names),
                "%.1f s (%d boxes)" % (boxes["seconds"], len(boxes["parsed"] or [])) if boxes else "-"), flush=True)
    finally:
        stop_server(proc)


if __name__ == "__main__":
    sys.exit(main())
