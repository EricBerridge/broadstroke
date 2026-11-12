"""Scene-understanding trial, step 2 (spec 2.13): depth and surface
orientation of a photo from MoGe-2, written as pictures to judge by eye.

  python geometry.py <out dir> photo1.jpg photo2.jpg ...

Writes <stem>_depth.jpg (near is light) and <stem>_normals.jpg (the usual
coloring: x to red, y to green, z to blue). Needs torch, moge and Pillow.
Nothing here is part of the engine.
"""
import os
import sys
import time

import numpy as np
import torch
from PIL import Image
from moge.model.v2 import MoGeModel

MODEL = "Ruicheng/moge-2-vitl-normal"


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    device = "cuda"
    model = MoGeModel.from_pretrained(MODEL).to(device).eval()
    for path in sys.argv[2:]:
        stem = os.path.splitext(os.path.basename(path))[0]
        rgb = np.asarray(Image.open(path).convert("RGB"))
        image = torch.tensor(rgb / 255.0, dtype=torch.float32, device=device).permute(2, 0, 1)
        t0 = time.time()
        with torch.no_grad():
            res = model.infer(image)
        seconds = time.time() - t0
        mask = res["mask"].cpu().numpy().astype(bool)
        depth = res["depth"].cpu().numpy()
        valid = mask & np.isfinite(depth)
        inv = np.zeros_like(depth)
        inv[valid] = 1.0 / depth[valid]
        lo, hi = np.percentile(inv[valid], [1, 99])
        shade = np.clip((inv - lo) / max(hi - lo, 1e-9), 0, 1)
        shade[~valid] = 0
        Image.fromarray((shade * 255).astype(np.uint8)).save(os.path.join(out_dir, stem + "_depth.jpg"), quality=90)
        if "normal" in res:
            n = res["normal"].cpu().numpy()
            pic = np.clip(n * 0.5 + 0.5, 0, 1)
            pic[~mask] = 0
            Image.fromarray((pic * 255).astype(np.uint8)).save(os.path.join(out_dir, stem + "_normals.jpg"),
                                                                quality=90)
        print("%s: %.2f s, depth %.2f to %.2f m, %.0f%% of the picture has geometry (the rest is sky)" % (
            stem, seconds, float(depth[valid].min()), float(depth[valid].max()), 100.0 * float(valid.mean())),
            flush=True)


if __name__ == "__main__":
    main()
