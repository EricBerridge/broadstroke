"""Interpretation trial (spec 2.14): one sheet per manner, to judge by eye.

  python sheet.py --subjects <dir> --oil <dir> --out <dir> --manners a,b stem [stem ...]

Each sheet has one row per photo: the oil painting of the photo as it is,
the subject the artist had in mind, the oil painting of that subject, and
a detail of both paintings at full resolution where the subject changed
most. Above each row: the title and what was carried out. Nothing here is
part of the engine.
"""
import argparse
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def font(size):
    try:
        return ImageFont.truetype("arial.ttf", size)
    except OSError:
        return ImageFont.load_default()


def fit(im, w, h):
    """The picture scaled to fit a w x h cell, centered on white."""
    s = min(w / im.width, h / im.height)
    im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
    cell = Image.new("RGB", (w, h), "white")
    cell.paste(im, ((w - im.width) // 2, (h - im.height) // 2))
    return cell


def clip_text(draw, text, fnt, width):
    if draw.textlength(text, font=fnt) <= width:
        return text
    while text and draw.textlength(text + " ...", font=fnt) > width:
        text = text[:-1]
    return text + " ..."


def sheet(manner, stems, args):
    w, h, d, pad, bar = 700, 525, 525, 10, 62
    rows = []
    for stem, tag in [(stem, tag) for stem in stems for tag in ("", "_hand")]:
        name = "%s_%s%s" % (stem, manner, tag)
        scene_path = os.path.join(args.subjects, name + ".scene.json")
        oil = os.path.join(args.oil, name + ".png")
        asis = os.path.join(args.oil, stem + "_asis.png")
        if not (os.path.exists(scene_path) and os.path.exists(oil) and os.path.exists(asis)):
            continue
        with open(scene_path, encoding="utf-8") as f:
            rows.append((stem, json.load(f), Image.open(asis).convert("RGB"),
                         Image.open(os.path.join(args.subjects, name + ".png")).convert("RGB"),
                         Image.open(oil).convert("RGB")))
    if not rows:
        return
    width = 3 * w + 2 * d + 6 * pad
    out = Image.new("RGB", (width, 44 + len(rows) * (bar + h + pad) + pad), "white")
    draw = ImageDraw.Draw(out)
    heads = ["Oil, the photo as it is", "What the artist has in mind", "Oil, %s" % manner,
             "Detail, as it is (full resolution)", "Detail, %s (full resolution)" % manner]
    xs = [pad, 2 * pad + w, 3 * pad + 2 * w, 4 * pad + 3 * w, 5 * pad + 3 * w + d]
    for x, head in zip(xs, heads):
        draw.text((x, 10), head, fill="black", font=font(22))
    y = 44
    for stem, scene, asis, mind, oil in rows:
        hand = " (choices edited by hand)" if scene.get("edited_by_hand") else ""
        draw.text((pad, y + 4), clip_text(draw, "%s: \"%s\"%s  -  %s" % (
            stem, scene["title"], hand, " / ".join(scene["poem"])), font(21), width - 2 * pad), fill="black", font=font(21))
        done = "; ".join(e.strip() for e in scene.get("carried_out", [])) or "nothing carried out"
        draw.text((pad, y + 32), clip_text(draw, "Carried out: " + done, font(18), width - 2 * pad),
                  fill=(70, 70, 70), font=font(18))
        y += bar
        for x, im in zip(xs[:3], (asis, mind, oil)):
            out.paste(fit(im, w, h), (x, y))
        fx, fy = scene.get("focus", [0.5, 0.5])
        side = min(d, oil.width, oil.height)
        x0 = min(max(0, int(fx * oil.width) - side // 2), oil.width - side)
        y0 = min(max(0, int(fy * oil.height) - side // 2), oil.height - side)
        box = (x0, y0, x0 + side, y0 + side)
        out.paste(fit(asis.crop(box), d, d), (xs[3], y))
        out.paste(fit(oil.crop(box), d, d), (xs[4], y))
        y += h + pad
    path = os.path.join(args.out, "%s.jpg" % manner)
    out.save(path, quality=90)
    print(path, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stems", nargs="+")
    ap.add_argument("--manners", required=True)
    ap.add_argument("--subjects", required=True)
    ap.add_argument("--oil", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for manner in args.manners.split(","):
        sheet(manner, args.stems, args)


if __name__ == "__main__":
    sys.exit(main())
