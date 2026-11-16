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
each instance, SAM 2.1 turns each box into an outline. What the boxes left
out is outlined from points. Then the first model takes a second look at
each region, marked in the photo, and says what it shows: a region under a
wrong name is given to the right item, and a region no item covers is
given to the item it shows or becomes a new item (--no-check skips this).
The paths of the models are read from a config file (see
observe.example.json). Needs torch, transformers and Pillow. Nothing here
is part of the engine.
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


LEFT_OUT = 0.004   # a patch with no item is looked at if it covers this share of the picture
LOOKED_AT = 0.003  # so is a patch of an item
MOST_LEFT, MOST_PER_ITEM, MOST_LOOKS = 12, 3, 45


def left_out(image, layer, sam_p, sam_m, device):
    """Outlines for what belongs to no item. The largest such patch is
    taken again and again: the outline model is shown its box and the
    point deepest inside it, and what it outlines there (within what has
    no item) is one region. Returns the regions."""
    import segment
    from scipy import ndimage
    free = layer == 0
    regions = []
    for _ in range(MOST_LEFT):
        patches, n = ndimage.label(free)
        if n == 0:
            break
        sizes = np.bincount(patches.ravel())
        sizes[0] = 0
        k = int(np.argmax(sizes))
        if sizes[k] < LEFT_OUT * layer.size:
            break
        patch = patches == k
        ys, xs = np.nonzero(patch)
        deep = ndimage.distance_transform_edt(patch)
        py, px = np.unravel_index(int(np.argmax(deep)), deep.shape)
        box = [float(xs.min()), float(ys.min()), float(xs.max()), float(ys.max())]
        region = segment.sam_region(image, box, [float(px), float(py)], sam_p, sam_m, device) & free
        if region.sum() < 0.25 * sizes[k]:
            region = patch  # the outline model found little there: the patch as it is
        regions.append(region)
        free &= ~region
    return regions


def as_jpeg(picture, long_side):
    scale = float(long_side) / max(picture.size)
    if scale < 1.0:
        picture = picture.resize((max(1, round(picture.width * scale)), max(1, round(picture.height * scale))),
                                 Image.LANCZOS)
    buf = io.BytesIO()
    picture.save(buf, format="JPEG", quality=90)
    return base64.b64encode(buf.getvalue()).decode("ascii")


def alone(image, region):
    """A region of the photo by itself: everything else painted plain
    gray, cut to the region with a little room, as a JPEG for the language
    model. Small holes in the region are left as the photo shows them, so
    that the model does not take the gray shapes of other things for the
    region. Shown the region bright in a dimmed photo instead, the model
    named whatever was most prominent in the photo."""
    from scipy import ndimage
    holes, n = ndimage.label(ndimage.binary_fill_holes(region) & ~region)
    if n:
        sizes = np.bincount(holes.ravel())
        small = sizes < 0.02 * region.sum()
        small[0] = False
        region = region | small[holes]
    ys, xs = np.nonzero(region)
    my, mx = int(0.06 * (ys.max() - ys.min())) + 12, int(0.06 * (xs.max() - xs.min())) + 12
    a, b = max(0, ys.min() - my), min(region.shape[0], ys.max() + my + 1)
    c, d = max(0, xs.min() - mx), min(region.shape[1], xs.max() + mx + 1)
    rgb = np.asarray(image)[a:b, c:d]
    out = np.where(region[a:b, c:d][..., None], rgb, np.uint8(128))
    return as_jpeg(Image.fromarray(out.astype(np.uint8)), 896)


def second_look(base, image, scene, items, layer, regions, tell=False):
    """Asks the list model what each region shows: the larger patches of
    every item, and the regions that have no item. Returns a list of
    (item id or 0, region, answer)."""
    from scipy import ndimage
    names = [it["name"] for it in items]
    looks = []
    for it in items:
        patches, n = ndimage.label(layer == it["id"])
        sizes = np.bincount(patches.ravel())
        sizes[0] = 0
        for k in np.argsort(sizes)[::-1][:MOST_PER_ITEM]:
            if sizes[k] >= LOOKED_AT * layer.size:
                looks.append((it["id"], patches == k))
    looks += [(0, r) for r in regions]
    looks = sorted(looks, key=lambda q: -int(q[1].sum()))[:MOST_LOOKS]
    by_id = {it["id"]: it for it in items}
    out = []
    for i, r in looks:
        ans = describe.ask_region(base, alone(image, r), scene, names, by_id[i]["name"] if i else "")
        out.append((i, r, ans))
        if tell and ans:
            print("      %-22s %5.1f%%  \"%s\"  ->  %s, %s" % (
                by_id[i]["name"] if i else "(no item)", 100.0 * float(r.sum()) / layer.size, ans["what"],
                ans["shows"], ans["treatment"]), flush=True)
    return out


def apply_looks(items, layer, main_subject, looks):
    """Corrects the items and their layer by what the second look said.
    Returns the layer, the main subject and notes on what was changed."""
    by_id = {it["id"]: it for it in items}
    by_name = {it["name"]: it for it in items}
    notes = []
    # A single part that covers its whole is not a part: they are one item.
    for it in list(items):
        p = it.get("parent", 0)
        if p and sum(1 for o in items if o.get("parent", 0) == p) == 1:
            part, whole = int((layer == it["id"]).sum()), int((layer == p).sum())
            if part > 0 and whole < 0.15 * part and not any(o.get("parent", 0) == it["id"] for o in items):
                layer[layer == it["id"]] = p
                notes.append("%s covers all of %s: they are one item, %s" % (it["name"], by_id[p]["name"], by_id[p]["name"]))
                for k, (i, region, ans) in enumerate(looks):
                    if i == it["id"]:
                        looks[k] = (p, region, ans)

    def kin(a, b):  # one is a part of the other
        for x, y in ((a, b), (b, a)):
            q = by_id[x].get("parent", 0)
            while q:
                if q == y:
                    return True
                q = by_id[q].get("parent", 0)
        return False

    for i, region, ans in looks:
        if not ans:
            continue
        share = 100.0 * float(region.sum()) / layer.size
        shows = ans["shows"]
        if i:
            it = by_id[i]
            if shows == it["name"]:
                if ans["treatment"] != it["treatment"]:
                    parts = sum(int((layer == o["id"]).sum()) for o in items if o.get("parent", 0) == i)
                    if int((layer == i).sum()) < 0.5 * parts:
                        # Its own pixels are the little that its parts leave of it.
                        notes.append("%s: what its parts leave of it is %s, not %s" % (
                            it["name"], ans["treatment"], it["treatment"]))
                        it["treatment"] = ans["treatment"]
                    else:
                        notes.append("%s: a second look says %s, the list says %s; the list is kept" % (
                            it["name"], ans["treatment"], it["treatment"]))
            elif shows in by_name and not kin(by_name[shows]["id"], i):
                layer[region & (layer == i)] = by_name[shows]["id"]
                notes.append("a patch named %s (%.1f%% of the picture) shows %s: given to it" % (
                    it["name"], share, shows))
            elif shows == describe.NONE:
                notes.append("%s: a second look does not see it in a patch of %.1f%% of the picture (%s); kept" % (
                    it["name"], share, ans["what"]))
        else:
            name = shows if shows in by_name else " ".join(ans["what"].lower().split()[:4])
            if not name:
                continue
            if name not in by_name:
                if len(items) >= 255:
                    continue
                # Named from one look at a region alone, which is often wrong:
                # no treatment is acted on, and it does not rank above supporting.
                new = {"id": max(by_id) + 1, "name": name, "parent": 0, "treatment": "other",
                       "importance": min(1, ans["importance"]), "edges": ans["edges"], "how_many": "one"}
                items.append(new)
                by_id[new["id"]], by_name[name] = new, new
                notes.append("%.1f%% of the picture had no item: a new item, %s (to be checked by eye)" % (share, name))
            else:
                notes.append("%.1f%% of the picture had no item: it shows %s" % (share, name))
            layer[region & (layer == 0)] = by_name[name]["id"]

    # An item without pixels, and without parts that have any, is dropped.
    for _ in range(len(items)):
        present = set(int(v) for v in np.unique(layer))
        gone = [it for it in items if it["id"] not in present
                and not any(o.get("parent", 0) == it["id"] for o in items)]
        if not gone:
            break
        for it in gone:
            items.remove(it)
            notes.append("%s: no pixels, dropped" % it["name"])
            if main_subject == it["id"]:
                main_subject = it.get("parent", 0)
    return tidy_layer(layer), main_subject, notes


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


FORM_MODEL = "Ruicheng/moge-2-vitl-normal"


def load_form_model(device):
    from moge.model.v2 import MoGeModel
    return MoGeModel.from_pretrained(FORM_MODEL).to(device).eval()


def orientation_layer(image, model, device):
    """The orientation layer of a photo: for each pixel the unit normal of
    the surface seen there, as an 8-bit RGB image with r to the right, g
    downward and b toward the viewer (-1..1 stored as 0..255). Where the
    model sees no surface (open sky) the pixel faces the viewer."""
    import torch
    rgb = np.asarray(image)
    with torch.no_grad():
        res = model.infer(torch.tensor(rgb / 255.0, dtype=torch.float32, device=device).permute(2, 0, 1))
    n = res["normal"].cpu().numpy().astype(np.float32)
    # The model's z axis points into the scene; the layer's points out of it.
    n[..., 2] = -n[..., 2]
    n[~res["mask"].cpu().numpy().astype(bool)] = (0.0, 0.0, 1.0)
    n[~np.isfinite(n).all(axis=-1)] = (0.0, 0.0, 1.0)
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
    return Image.fromarray(np.clip(np.rint((n * 0.5 + 0.5) * 255.0), 0, 255).astype(np.uint8), mode="RGB")


def add_orientation(photos, cfg):
    """Adds the orientation layer to scene files that already exist. The
    items and their layer are left as they are; only this model runs."""
    if cfg.get("hf_home"):
        os.environ["HF_HOME"] = cfg["hf_home"]
    import torch
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model = load_form_model(device)
    for path in photos:
        stem = os.path.splitext(path)[0]
        name = os.path.basename(stem)
        with open(stem + ".scene.json", encoding="utf-8") as f:
            scene = json.load(f)
        image, _ = look(path)
        orientation_layer(image, model, device).save(stem + ".scene.normals.png")
        scene["normal_layer"] = name + ".scene.normals.png"
        with open(stem + ".scene.json", "w", encoding="utf-8", newline="\n") as f:
            json.dump(scene, f, indent=1, ensure_ascii=False)
            f.write("\n")
        print("%s: orientation layer written" % os.path.basename(path), flush=True)
    return 0


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


def scene_stem(photo, out, name):
    """Where a photo's scene files are, without their endings."""
    stem = os.path.splitext(photo)[0]
    return os.path.join(out, name or os.path.basename(stem)) if out else stem


def write_scene(stem, scene, layer, photo):
    folder = os.path.dirname(stem)
    Image.fromarray(layer, mode="L").save(os.path.join(folder, scene["item_layer"]))
    with open(stem + ".scene.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(scene, f, indent=1, ensure_ascii=False)
        f.write("\n")
    image = Image.open(photo).convert("RGB").resize((layer.shape[1], layer.shape[0]), Image.LANCZOS)
    write_preview(image, layer, scene["items"], stem + ".scene.preview.jpg")


SAME_ITEM = 0.7  # an outline that agrees this far with an item is that item
NEAR = (0.004, 0.008)  # how far around a click a thing is looked for, as shares of the long side


def add_item(photo, stem, name, at, cfg):
    """Adds an item for a thing the scene does not have as an item of its
    own: the thing at a point of the photo, outlined by SAM 2.1. `at` is
    the point as fractions of the width and the height. The item layer as
    it was before the first item was added is kept beside it, so that an
    added item can be removed again. Says what it did in one line."""
    with open(stem + ".scene.json", encoding="utf-8") as f:
        text = f.read()
    scene = json.loads(text)
    layer_path = os.path.join(os.path.dirname(stem), scene["item_layer"])
    layer = np.array(Image.open(layer_path))
    free = [i for i in range(1, 256) if i not in {it["id"] for it in scene["items"]}]
    if not free:
        print("Not added: the scene has 255 items, and can have no more", flush=True)
        return 0
    if cfg.get("hf_home"):
        os.environ["HF_HOME"] = cfg["hf_home"]
    import torch
    import segment
    device = "cuda" if torch.cuda.is_available() else "cpu"
    sam_p, sam_m = segment.load_sam(device)
    image, _ = look(photo)
    image = image.resize((layer.shape[1], layer.shape[0]), Image.LANCZOS)
    h, w = layer.shape
    x, y = min(max(at[0], 0.0), 1.0) * (w - 1), min(max(at[1], 0.0), 1.0) * (h - 1)
    # The click, and places close around it: a thin thing is easily missed
    # by a few pixels, and what is outlined then is what lies behind it.
    points = [(x, y)]
    for r in NEAR:
        for k in range(8):
            a = 2.0 * np.pi * k / 8
            points.append((min(max(x + r * max(w, h) * np.cos(a), 0.0), w - 1.0),
                           min(max(y + r * max(w, h) * np.sin(a), 0.0), h - 1.0)))
    found = segment.sam_things(image, points, sam_p, sam_m, device)

    def item_of(outline):
        """The item an outline is, if it is one already."""
        for it in scene["items"]:
            own = layer == it["id"]
            both = float((own & outline).sum())
            if both > 0 and both / float((own | outline).sum()) >= SAME_ITEM:
                return it
        return None

    # The outline at the click itself, unless that is an item already; then
    # the surest outline close by that is not.
    mask, behind = None, None
    for k in [0] + sorted(range(1, len(found)), key=lambda k: -found[k][1]):
        outline = found[k][0]
        if not outline.any():
            continue
        it = item_of(outline)
        if it is None:
            mask = outline
            break
        behind = behind or it
    if mask is None:
        if behind is not None:
            print("Not added: what is outlined there is the item \"%s\". Click right on the thing." % behind["name"],
                  flush=True)
        else:
            print("Not added: nothing was outlined there", flush=True)
        return 0
    kept = stem + ".scene.items.observed.png"
    if not os.path.exists(kept):
        Image.fromarray(layer, mode="L").save(kept)
    if not scene.get("edited"):
        # As the studio window does at its first correction: the file as observed is kept.
        with open(stem + ".scene.observed.json", "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        scene["edited"] = True
    layer = layer.copy()
    layer[mask] = free[0]
    scene["items"].append({"id": free[0], "name": name, "parent": 0, "treatment": "other", "importance": 1,
                           "edges": "hard", "how_many": "one", "added": True})
    write_scene(stem, scene, layer, photo)
    print("Added \"%s\": %.2f%% of the picture" % (name, 100.0 * float(mask.mean())), flush=True)
    return 0


def remove_item(photo, stem, item_id):
    """Removes an item that was added by add_item: its pixels are given
    back to the items they had when the photo was observed. No model runs."""
    with open(stem + ".scene.json", encoding="utf-8") as f:
        scene = json.load(f)
    item = next((it for it in scene["items"] if it["id"] == item_id), None)
    kept = stem + ".scene.items.observed.png"
    if item is None or not item.get("added") or not os.path.exists(kept):
        print("Not removed: only an item that was added can be removed", flush=True)
        return 0
    layer = np.array(Image.open(os.path.join(os.path.dirname(stem), scene["item_layer"])))
    before = np.array(Image.open(kept))
    own = layer == item_id
    layer[own] = before[own]
    scene["items"].remove(item)
    for it in scene["items"]:
        if it.get("parent") == item_id:
            it["parent"] = 0
    if scene.get("main_subject") == item_id:
        scene["main_subject"] = 0
    write_scene(stem, scene, layer, photo)
    print("Removed \"%s\"" % item["name"], flush=True)
    return 0


def check_existing(photos, cfg, log, tell):
    """Outlines what the items of existing scene files leave out and takes
    the second look, as observe.py itself does; the list of items and the
    boxes are not asked for again."""
    if cfg.get("hf_home"):
        os.environ["HF_HOME"] = cfg["hf_home"]
    import torch
    import segment
    device = "cuda" if torch.cuda.is_available() else "cpu"
    sam_p, sam_m = segment.load_sam(device)
    work = []
    for path in photos:
        stem = os.path.splitext(path)[0]
        with open(stem + ".scene.json", encoding="utf-8") as f:
            scene = json.load(f)
        layer = np.array(Image.open(os.path.join(os.path.dirname(path), scene["item_layer"])))
        image, _ = look(path)
        image = image.resize((layer.shape[1], layer.shape[0]), Image.LANCZOS)
        work.append((stem, scene, image, layer, left_out(image, layer, sam_p, sam_m, device)))
    del sam_m
    torch.cuda.empty_cache()
    proc, base = describe.start_server(cfg["llama_server"], cfg["list_model"], cfg["list_mmproj"], log)
    try:
        for stem, scene, image, layer, regions in work:
            print(os.path.basename(stem), flush=True)
            looks = second_look(base, image, scene.get("scene", ""), scene["items"], layer, regions, tell)
            layer, scene["main_subject"], notes = apply_looks(scene["items"], layer, scene.get("main_subject", 0), looks)
            if notes:
                scene["check"] = notes
            Image.fromarray(layer, mode="L").save(os.path.join(os.path.dirname(stem), scene["item_layer"]))
            with open(stem + ".scene.json", "w", encoding="utf-8", newline="\n") as f:
                json.dump(scene, f, indent=1, ensure_ascii=False)
                f.write("\n")
            write_preview(image, layer, scene["items"], stem + ".scene.preview.jpg")
            for note in notes:
                print("    " + note, flush=True)
    finally:
        describe.stop_server(proc)
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description="Write the scene file of each photo (spec 2.13).")
    ap.add_argument("photos", nargs="+")
    ap.add_argument("--config", default=os.path.join(here, "observe.local.json"))
    ap.add_argument("--tidy", action="store_true",
                    help="run no model: remove the specks from the item layers of existing scene files")
    ap.add_argument("--orientation", action="store_true",
                    help="run only the orientation model: add the orientation layer to existing scene files")
    ap.add_argument("--check", action="store_true",
                    help="list and box nothing: outline what the items of existing scene files leave out, and take "
                         "the second look")
    ap.add_argument("--tell", action="store_true", help="with the second look: print every answer")
    ap.add_argument("--no-check", action="store_true",
                    help="no second look: the items as listed and boxed, and what the boxes left out without an item")
    ap.add_argument("--out", default="",
                    help="write the scene files into this folder, not beside each photo (the studio window keeps "
                         "them with its working files; the modes that work on existing scene files do not use it)")
    ap.add_argument("--name", default="", help="with --out, for one photo: the name the files are given, if not the photo's")
    ap.add_argument("--add", default="", metavar="NAME",
                    help="for one photo that has a scene file: add an item of this name for the thing at --at, "
                         "outlined by the outline model (it looks in --out, if given)")
    ap.add_argument("--at", default="", metavar="X,Y",
                    help="with --add: where the thing is, as fractions of the photo's width and height")
    ap.add_argument("--remove", type=int, default=0, metavar="ID",
                    help="for one photo: remove an item that was added with --add; no model runs")
    args = ap.parse_args()
    if args.tidy:
        return tidy_existing(args.photos)
    if args.remove:
        return remove_item(args.photos[0], scene_stem(args.photos[0], args.out, args.name), args.remove)
    cfg = load_config(args.config)
    if args.add:
        try:
            at = [float(v) for v in args.at.split(",")]
        except ValueError:
            at = []
        if len(at) != 2:
            sys.exit("--add needs --at X,Y, two numbers from 0 to 1")
        return add_item(args.photos[0], scene_stem(args.photos[0], args.out, args.name), args.add.strip() or "thing",
                        at, cfg)
    if args.orientation:
        return add_orientation(args.photos, cfg)
    if args.check:
        return check_existing(args.photos, cfg, os.path.join(cfg.get("log_dir") or here, "observe_server.log"),
                              args.tell)
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
    work = []
    for (path, image, _), answer, box_answer in zip(seen, lists, boxes):
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
        regions = [] if args.no_check else left_out(image, layer, sam_p, sam_m, device)
        work.append({"path": path, "image": image, "answer": answer["parsed"], "items": items,
                     "main": main_subject, "layer": layer, "regions": regions, "notes": []})
    del sam_m
    torch.cuda.empty_cache()
    print("outlines in %.0f s" % (time.time() - t0), flush=True)

    if work and not args.no_check:
        # The second look: the list model again, now on one region at a time.
        t0 = time.time()
        proc, base = describe.start_server(cfg["llama_server"], cfg["list_model"], cfg["list_mmproj"], log)
        try:
            for job in work:
                looks = second_look(base, job["image"], job["answer"]["scene"], job["items"], job["layer"],
                                    job["regions"], args.tell)
                job["layer"], job["main"], job["notes"] = apply_looks(job["items"], job["layer"], job["main"], looks)
                job["looks"] = len(looks)
        finally:
            describe.stop_server(proc)
        print("second look at %d regions in %.0f s" % (sum(job["looks"] for job in work), time.time() - t0), flush=True)

    t0 = time.time()
    form_model = load_form_model(device)
    for job in work:
        path, image, items, layer = job["path"], job["image"], job["items"], job["layer"]
        stem = os.path.splitext(path)[0]
        name = os.path.basename(stem)
        if args.out:
            os.makedirs(args.out, exist_ok=True)
            name = args.name or name
            stem = os.path.join(args.out, name)
        Image.fromarray(layer, mode="L").save(stem + ".scene.items.png")
        if os.path.exists(stem + ".scene.items.observed.png"):
            os.remove(stem + ".scene.items.observed.png")  # it was the layer of the scene before
        orientation_layer(image, form_model, device).save(stem + ".scene.normals.png")
        scene = {"version": 1, "photo": os.path.basename(path), "scene": job["answer"]["scene"],
                 "light_from": job["answer"]["light_from"], "main_subject": job["main"],
                 "item_layer": name + ".scene.items.png", "normal_layer": name + ".scene.normals.png",
                 "items": items}
        if job["notes"]:
            scene["check"] = job["notes"]
        with open(stem + ".scene.json", "w", encoding="utf-8", newline="\n") as f:
            json.dump(scene, f, indent=1, ensure_ascii=False)
            f.write("\n")

        shown = write_preview(image, layer, items, stem + ".scene.preview.jpg")
        print("%s: %d items, %d with pixels, %.0f%% of the picture belongs to no item" % (
            os.path.basename(path), len(items), shown, 100.0 * float((layer == 0).mean())), flush=True)
        for note in job["notes"]:
            print("    " + note, flush=True)
    print("orientation and files in %.0f s" % (time.time() - t0), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
