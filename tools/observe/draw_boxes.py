"""Scene-understanding trial: draw the boxes a language model gave onto the
photo, to judge by eye how well it locates things. Needs Pillow.

  python draw_boxes.py <photos dir> <results dir>/<model name>
"""
import glob
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

COLORS = [(230, 25, 75), (60, 180, 75), (255, 225, 25), (0, 130, 200), (245, 130, 48), (145, 30, 180),
          (70, 240, 240), (240, 50, 230), (210, 245, 60), (250, 190, 212), (0, 128, 128), (220, 190, 255),
          (170, 110, 40), (255, 250, 200), (128, 0, 0), (170, 255, 195)]


def main():
    photos, folder = sys.argv[1], sys.argv[2]
    try:
        font = ImageFont.truetype("arial.ttf", 22)
    except OSError:
        font = ImageFont.load_default()
    for path in sorted(glob.glob(os.path.join(folder, "*.json"))):
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
        if "photo" not in d:
            continue  # a report written by segment.py
        boxes = d.get("boxes")
        if not boxes or not boxes.get("parsed"):
            continue
        key = boxes["box_key"]
        img = Image.open(os.path.join(photos, d["photo"])).convert("RGB")
        w, h = img.size
        draw = ImageDraw.Draw(img)
        labels = {}
        for b in boxes["parsed"]:
            v = b[key]
            if key == "box_2d":   # Gemma: y1, x1, y2, x2
                y1, x1, y2, x2 = v
            else:                 # Qwen: x1, y1, x2, y2
                x1, y1, x2, y2 = v
            x1, x2 = sorted((x1 * w / 1000.0, x2 * w / 1000.0))
            y1, y2 = sorted((y1 * h / 1000.0, y2 * h / 1000.0))
            color = COLORS[labels.setdefault(b["label"], len(labels)) % len(COLORS)]
            draw.rectangle([x1, y1, x2, y2], outline=color, width=4)
            tw = draw.textlength(b["label"], font=font)
            draw.rectangle([x1, y1, x1 + tw + 8, y1 + 26], fill=color)
            draw.text((x1 + 4, y1 + 1), b["label"], fill=(0, 0, 0), font=font)
        out = os.path.splitext(path)[0] + "_boxes.jpg"
        img.save(out, quality=88)
        print(out)


if __name__ == "__main__":
    main()
