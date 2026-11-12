"""Scene-understanding trial, step 2 (spec 2.13): outlines for the items a
language model named.

For each photo, takes the item names from describe.py's answer, finds each
name with Grounding DINO (boxes), turns each box into an outline with
SAM 2.1, and writes an overlay to judge by eye. Needs torch, transformers
and Pillow. Nothing here is part of the engine.

  python segment.py <photos dir> <results dir>/<model name> [--boxes-from model]

--boxes-from model uses the language model's own boxes instead of
Grounding DINO's, to compare the two ways of locating things.
"""
import argparse
import glob
import json
import os
import time

import numpy as np
import torch
from PIL import Image, ImageDraw, ImageFont
from transformers import AutoModelForZeroShotObjectDetection, AutoProcessor, Sam2Model, Sam2Processor

DINO = "IDEA-Research/grounding-dino-tiny"
SAM = "facebook/sam2.1-hiera-large"
COLORS = [(230, 25, 75), (60, 180, 75), (255, 225, 25), (0, 130, 200), (245, 130, 48), (145, 30, 180),
          (70, 240, 240), (240, 50, 230), (210, 245, 60), (250, 190, 212), (0, 128, 128), (220, 190, 255),
          (170, 110, 40), (255, 250, 200), (128, 0, 0), (170, 255, 195), (128, 128, 0), (255, 215, 180),
          (0, 0, 128), (128, 128, 128), (255, 255, 255), (20, 20, 20), (100, 60, 200), (200, 100, 60)]


def iou(a, b):
    x1, y1, x2, y2 = max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])
    inter = max(0.0, x2 - x1) * max(0.0, y2 - y1)
    union = (a[2] - a[0]) * (a[3] - a[1]) + (b[2] - b[0]) * (b[3] - b[1]) - inter
    return inter / union if union > 0 else 0.0


def dino_boxes(image, name, how_many, processor, model, device):
    inputs = processor(images=image, text=name.lower().strip(". ") + ".", return_tensors="pt").to(device)
    with torch.no_grad():
        outputs = model(**inputs)
    res = processor.post_process_grounded_object_detection(
        outputs, inputs.input_ids, threshold=0.25, text_threshold=0.2, target_sizes=[image.size[::-1]])[0]
    found = sorted(zip(res["scores"].tolist(), res["boxes"].tolist()), reverse=True)
    keep = []
    limit = {"one": 1, "few": 6, "many": 20}[how_many]
    for score, box in found:
        if len(keep) >= limit:
            break
        if all(iou(box, k[1]) < 0.6 for k in keep):
            keep.append((score, box))
    return keep


def model_boxes(answer, name, w, h):
    """The boxes a language model gave for one item name, in pixels."""
    boxes = answer.get("boxes") or {}
    key = boxes.get("box_key")
    out = []
    for b in boxes.get("parsed") or []:
        if b["label"] != name:
            continue
        v = b[key]
        if key == "box_2d":
            y1, x1, y2, x2 = v
        else:
            x1, y1, x2, y2 = v
        xa, xb = sorted((x1 * w / 1000.0, x2 * w / 1000.0))
        ya, yb = sorted((y1 * h / 1000.0, y2 * h / 1000.0))
        if xb - xa > 2 and yb - ya > 2:
            out.append((1.0, [xa, ya, xb, yb]))
    return out


def sam_masks(image, boxes, processor, model, device):
    inputs = processor(images=image, input_boxes=[boxes], return_tensors="pt").to(device)
    with torch.no_grad():
        outputs = model(**inputs, multimask_output=False)
    masks = processor.post_process_masks(outputs.pred_masks.cpu(), inputs["original_sizes"])[0]
    return masks.numpy().reshape(len(boxes), -1, masks.shape[-2], masks.shape[-1])[:, 0] > 0


def load_sam(device):
    return Sam2Processor.from_pretrained(SAM), Sam2Model.from_pretrained(SAM).to(device).eval()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("photos")
    ap.add_argument("folder")
    ap.add_argument("--boxes-from", choices=["dino", "model"], default="dino")
    args = ap.parse_args()
    device = "cuda"
    dino_p = AutoProcessor.from_pretrained(DINO)
    dino_m = AutoModelForZeroShotObjectDetection.from_pretrained(DINO).to(device).eval()
    sam_p, sam_m = load_sam(device)
    try:
        font = ImageFont.truetype("arial.ttf", 20)
    except OSError:
        font = ImageFont.load_default()

    for path in sorted(glob.glob(os.path.join(args.folder, "*.json"))):
        with open(path, encoding="utf-8") as f:
            answer = json.load(f)
        if "photo" not in answer:
            continue  # one of this script's own reports
        parsed = answer["items"]["parsed"]
        if not parsed:
            continue
        image = Image.open(os.path.join(args.photos, answer["photo"])).convert("RGB")
        w, h = image.size
        t0 = time.time()
        # One entry per distinct name; an item's instances share its number.
        names, entries = [], []
        for it in parsed["items"]:
            if it["name"] in names:
                continue
            names.append(it["name"])
            if args.boxes_from == "dino":
                found = dino_boxes(image, it["name"], it["how_many"], dino_p, dino_m, device)
            else:
                found = model_boxes(answer, it["name"], w, h)
            if found:
                masks = sam_masks(image, [b for _, b in found], sam_p, sam_m, device)
                entries.append((it, [b for _, b in found], masks.any(axis=0)))
            else:
                entries.append((it, [], None))
        seconds = time.time() - t0

        # Paint the largest areas first so parts sit on top of their wholes.
        order = sorted((i for i, e in enumerate(entries) if e[2] is not None),
                       key=lambda i: -int(entries[i][2].sum()))
        label = np.zeros((h, w), np.uint8)
        for i in order:
            label[entries[i][2]] = i + 1
        rgb = np.asarray(image).astype(np.float32)
        tint = rgb.copy()
        for i in order:
            m = label == i + 1
            tint[m] = 0.45 * rgb[m] + 0.55 * np.array(COLORS[i % len(COLORS)], np.float32)
        out = Image.fromarray(tint.astype(np.uint8))
        draw = ImageDraw.Draw(out)
        report = []
        for i, (it, boxes, mask) in enumerate(entries):
            shown = float((label == i + 1).mean()) if mask is not None else 0.0
            report.append({"name": it["name"], "boxes": len(boxes), "share_of_picture": round(shown, 4)})
            if mask is None or shown == 0.0:
                continue
            ys, xs = np.nonzero(label == i + 1)
            cx, cy = float(np.median(xs)), float(np.median(ys))
            tw = draw.textlength(it["name"], font=font)
            draw.rectangle([cx - tw / 2 - 4, cy - 12, cx + tw / 2 + 4, cy + 12], fill=COLORS[i % len(COLORS)])
            draw.text((cx - tw / 2, cy - 11), it["name"], fill=(0, 0, 0), font=font)
        suffix = "_outlines.jpg" if args.boxes_from == "dino" else "_outlines_modelboxes.jpg"
        out.save(os.path.splitext(path)[0] + suffix, quality=88)
        missing = [r["name"] for r in report if r["share_of_picture"] == 0.0]
        print("%s: %.1f s, %d names, not found or fully covered: %s, unassigned %.0f%%" % (
            os.path.splitext(os.path.basename(path))[0], seconds, len(names), missing or "none",
            100.0 * float((label == 0).mean())), flush=True)
        with open(os.path.splitext(path)[0] + suffix.replace(".jpg", ".json"), "w", encoding="utf-8",
                  newline="\n") as f:
            json.dump({"seconds": round(seconds, 2), "items": report,
                       "unassigned": round(float((label == 0).mean()), 4)}, f, indent=1)


if __name__ == "__main__":
    main()
