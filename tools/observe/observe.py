"""The observe step (spec 2.13): looks at a photo with models that run on
this machine and writes its scene file.

  python observe.py photo.jpg [more photos ...] [--config observe.local.json]

For each photo it writes, beside the photo:
  <stem>.scene.json         the items, and which is the main subject
  <stem>.scene.items.png    the item layer: each pixel holds the id of its item
  <stem>.scene.preview.jpg  the items tinted over the photo, to check by eye

The engine picks up <stem>.scene.json when it paints the photo. The scene
file may be corrected by hand afterwards; running this again overwrites it.

How it works: one vision-language model lists the items, a second one boxes
each instance, SAM 2.1 turns each box into an outline. The paths of the
models are read from a config file (see observe.example.json). Needs torch,
transformers and Pillow. Nothing here is part of the engine.
"""
import argparse
import base64
import io
import json
import os
import sys
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import describe

LONG_SIDE = 2048  # the photo is looked at, and the item layer written, at no more than this


def load_config(path):
    if not os.path.exists(path):
        sys.exit("no config file at %s: copy observe.example.json to it and set the paths" % path)
    with open(path, encoding="utf-8") as f:
        cfg = json.load(f)
    for key in ("llama_server", "list_model", "list_mmproj", "box_model", "box_mmproj", "box_style"):
        if key not in cfg:
            sys.exit("%s: missing \"%s\"" % (path, key))
    return cfg


def look(path):
    """The photo as the models see it: RGB, long side at most LONG_SIDE."""
    image = Image.open(path).convert("RGB")
    scale = LONG_SIDE / max(image.size)
    if scale < 1.0:
        image = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))),
                             Image.LANCZOS)
    buf = io.BytesIO()
    image.save(buf, format="JPEG", quality=92)
    return image, base64.b64encode(buf.getvalue()).decode("ascii")


def scene_items(parsed):
    """Items of the scene file from the list model's answer: numbered from 1,
    one per distinct name, parents mapped to the numbers."""
    number, items = {}, []
    by_name = {}
    for it in parsed["items"]:
        if it["name"] in by_name:
            number[it["id"]] = by_name[it["name"]]
            continue
        n = len(items) + 1
        if n > 255:
            break
        number[it["id"]] = by_name[it["name"]] = n
        items.append({"id": n, "name": it["name"], "parent": it["parent"], "treatment": it["treatment"],
                      "importance": it["importance"], "edges": it["edges"], "how_many": it["how_many"]})
    for it in items:
        parent = number.get(it["parent"], 0)
        it["parent"] = 0 if parent == it["id"] else parent
    # A parent chain that loops would make the file unreadable: cut it.
    by_id = {it["id"]: it for it in items}
    for it in items:
        seen, p = {it["id"]}, it["parent"]
        while p:
            if p in seen:
                it["parent"] = 0
                break
            seen.add(p)
            p = by_id[p]["parent"]
    return items, number.get(parsed.get("main_subject"), 0)


def depth_of(item, by_id):
    d, p = 0, item["parent"]
    while p:
        d, p = d + 1, by_id[p]["parent"]
    return d


def tidy_layer(layer, min_share=0.0004, min_part=0.05):
    """Removes specks from an item layer. An outline model leaves stray dots
    of one item inside another; the artist would take each for an outline to
    work around. A connected patch is a speck if it is small in the picture
    and small beside the largest patch of its own item (so each of many
    small birds is kept). Specks take the item of the nearest pixel that is
    kept."""
    from scipy import ndimage
    min_area = max(9, int(min_share * layer.size))
    speck = np.zeros(layer.shape, bool)
    for i in np.unique(layer):
        patches, n = ndimage.label(layer == i)
        if n < 2:
            continue
        sizes = np.bincount(patches.ravel())
        sizes[0] = 0
        drop = (sizes < 9) | ((sizes < min_area) & (sizes < min_part * sizes.max()))
        drop[0] = False
        speck |= drop[patches]
    if not speck.any() or speck.all():
        return layer
    near = ndimage.distance_transform_edt(speck, return_distances=False, return_indices=True)
    return np.ascontiguousarray(layer[near[0], near[1]])


def write_preview(image, layer, items, path):
    """The items tinted over the photo, each with its name and importance."""
    import segment
    try:
        font = ImageFont.truetype("arial.ttf", 20)
    except OSError:
        font = ImageFont.load_default()
    rgb = np.asarray(image).astype(np.float32)
    tint = rgb.copy()
    present = [it for it in items if (layer == it["id"]).any()]
    for it in present:
        m = layer == it["id"]
        tint[m] = 0.45 * rgb[m] + 0.55 * np.array(segment.COLORS[it["id"] % len(segment.COLORS)], np.float32)
    preview = Image.fromarray(tint.astype(np.uint8))
    draw = ImageDraw.Draw(preview)
    for it in present:
        ys, xs = np.nonzero(layer == it["id"])
        label = "%s (%d)" % (it["name"], it["importance"])
        cx, cy = float(np.median(xs)), float(np.median(ys))
        tw = draw.textlength(label, font=font)
        draw.rectangle([cx - tw / 2 - 4, cy - 12, cx + tw / 2 + 4, cy + 12],
                       fill=segment.COLORS[it["id"] % len(segment.COLORS)])
        draw.text((cx - tw / 2, cy - 11), label, fill=(0, 0, 0), font=font)
    preview.save(path, quality=88)
    return len(present)


def tidy_existing(photos):
    """Tidies the item layers of scene files that already exist; no model runs."""
    for path in photos:
        stem = os.path.splitext(path)[0]
        with open(stem + ".scene.json", encoding="utf-8") as f:
            scene = json.load(f)
        layer_path = os.path.join(os.path.dirname(path), scene["item_layer"])
        layer = np.array(Image.open(layer_path))
        tidy = tidy_layer(layer)
        Image.fromarray(tidy, mode="L").save(layer_path)
        image = Image.open(path).convert("RGB").resize((layer.shape[1], layer.shape[0]), Image.LANCZOS)
        write_preview(image, tidy, scene["items"], stem + ".scene.preview.jpg")
        print("%s: %.1f%% of the layer changed" % (os.path.basename(path), 100.0 * float((tidy != layer).mean())))
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Write the scene file of each photo (spec 2.13).")
    ap.add_argument("photos", nargs="+")
    ap.add_argument("--config", default=os.path.join(here, "observe.local.json"))
    ap.add_argument("--tidy", action="store_true",
                    help="run no model: remove the specks from the item layers of existing scene files")
    args = ap.parse_args()
    if args.tidy:
        return tidy_existing(args.photos)
    cfg = load_config(args.config)
    # Where the SAM weights are kept. It must be set before the libraries
    # that read it are loaded, which is why they are imported here.
    if cfg.get("hf_home"):
        os.environ["HF_HOME"] = cfg["hf_home"]
    import torch
    import segment
    log = os.path.join(cfg.get("log_dir") or here, "observe_server.log")

    seen = [(path,) + look(path) for path in args.photos]

    # The two language models take turns on the graphics card.
    t0 = time.time()
    proc, base = describe.start_server(cfg["llama_server"], cfg["list_model"], cfg["list_mmproj"], log)
    try:
        lists = [describe.ask_items(base, b64) for _, _, b64 in seen]
    finally:
        describe.stop_server(proc)
    print("items listed in %.0f s" % (time.time() - t0), flush=True)

    t0 = time.time()
    proc, base = describe.start_server(cfg["llama_server"], cfg["box_model"], cfg["box_mmproj"], log)
    try:
        boxes = []
        for (_, _, b64), answer in zip(seen, lists):
            names = describe.item_names(answer["parsed"])
            boxes.append(describe.ask_boxes(base, b64, names, cfg["box_style"]) if names else None)
    finally:
        describe.stop_server(proc)
    print("items boxed in %.0f s" % (time.time() - t0), flush=True)

    t0 = time.time()
    device = "cuda" if torch.cuda.is_available() else "cpu"
    sam_p, sam_m = segment.load_sam(device)
    failed = 0
    for (path, image, _), answer, box_answer in zip(seen, lists, boxes):
        stem = os.path.splitext(path)[0]
        name = os.path.basename(stem)
        if not answer["parsed"] or not box_answer or not box_answer["parsed"]:
            print("%s: the models gave no usable answer; no scene file written" % os.path.basename(path))
            failed += 1
            continue
        items, main_subject = scene_items(answer["parsed"])
        by_id = {it["id"]: it for it in items}
        w, h = image.size
        masks = {}
        for it in items:
            found = segment.model_boxes({"boxes": box_answer}, it["name"], w, h)
            if found:
                masks[it["id"]] = segment.sam_masks(image, [b for _, b in found], sam_p, sam_m, device).any(axis=0)
        # A part is painted after its whole, and within a level the larger
        # area first, so each pixel ends up with the deepest item covering it.
        order = sorted(masks, key=lambda i: (depth_of(by_id[i], by_id), -int(masks[i].sum())))
        layer = np.zeros((h, w), np.uint8)
        for i in order:
            layer[masks[i]] = i
        layer = tidy_layer(layer)
        Image.fromarray(layer, mode="L").save(stem + ".scene.items.png")

        scene = {"version": 1, "photo": os.path.basename(path), "scene": answer["parsed"]["scene"],
                 "light_from": answer["parsed"]["light_from"], "main_subject": main_subject,
                 "item_layer": name + ".scene.items.png", "items": items}
        with open(stem + ".scene.json", "w", encoding="utf-8", newline="\n") as f:
            json.dump(scene, f, indent=1, ensure_ascii=False)
            f.write("\n")

        shown = write_preview(image, layer, items, stem + ".scene.preview.jpg")
        print("%s: %d items, %d with pixels, %.0f%% of the picture belongs to no item" % (
            os.path.basename(path), len(items), shown, 100.0 * float((layer == 0).mean())), flush=True)
    print("outlines in %.0f s" % (time.time() - t0), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
