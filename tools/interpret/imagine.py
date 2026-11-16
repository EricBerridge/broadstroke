"""Interpretation trial (spec 2.14), step 2: the subject the artist has in mind.

Takes a photo with its scene file and a choice written by choose.py (a
manner, and one mechanism per item) and writes a new subject for the engine
to paint: the photo as the artist imagines it, with the item layer and the
orientation layer changed to match, and a scene file.

  python imagine.py --choice <stem>.<manner>.choice.json --out <dir> photo.jpg
      [--fill lama|plain] [--lama big-lama.pt] [--tag _x] [--seed N]

The engine then paints <dir>/<stem>_<manner>.png like any photo. Each
mechanism is one routine below. A routine changes what the artist intends
to paint. Nothing is drawn onto the canvas. Nothing here is part of the
engine.
"""
import argparse
import json
import math
import os
import sys

import cv2
import numpy as np
from PIL import Image
from scipy import ndimage
from scipy.spatial import cKDTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import facts  # noqa: E402
import vocab  # noqa: E402
from facts import GROUND, MAX_SHAPE_SHARE  # noqa: E402

MOGE = "Ruicheng/moge-2-vitl-normal"


class Picture:
    def __init__(self, photo, scene_path):
        with open(scene_path, encoding="utf-8") as f:
            self.scene = json.load(f)
        folder = os.path.dirname(scene_path)
        self.items = np.array(Image.open(os.path.join(folder, self.scene["item_layer"])))
        h, w = self.items.shape
        self.photo_rgb = np.asarray(Image.open(photo).convert("RGB").resize((w, h), Image.LANCZOS))
        self.lab = cv2.cvtColor(self.photo_rgb.astype(np.float32) / 255.0, cv2.COLOR_RGB2LAB)
        self.first = self.lab.copy()
        self.normals = None
        if self.scene.get("normal_layer"):
            n = Image.open(os.path.join(folder, self.scene["normal_layer"])).convert("RGB").resize((w, h), Image.BILINEAR)
            self.normals = np.asarray(n).astype(np.float32)
        self.h, self.w, self.long = h, w, max(h, w)
        self.by_id = {it["id"]: it for it in self.scene["items"]}
        self.points = None  # the 3D point seen at each pixel, camera axes: x right, y down, z away
        self.space = None
        self.log = []

    def group(self, i):
        """The item and everything that is a part of it."""
        ids, grew = {i}, True
        while grew:
            grew = False
            for it in self.scene["items"]:
                if it.get("parent", 0) in ids and it["id"] not in ids:
                    ids.add(it["id"])
                    grew = True
        return sorted(ids)

    def mask(self, i):
        return np.isin(self.items, self.group(i))

    def soft(self, m, share=0.003):
        return cv2.GaussianBlur(m.astype(np.float32), (0, 0), max(0.6, share * self.long))

    def rgb(self):
        return (np.clip(cv2.cvtColor(self.lab, cv2.COLOR_LAB2RGB), 0, 1) * 255.0 + 0.5).astype(np.uint8)

    def set_rgb(self, rgb):
        self.lab = cv2.cvtColor(rgb.astype(np.float32) / 255.0, cv2.COLOR_RGB2LAB)

    def is_ground(self, i):
        return self.by_id[i].get("treatment") in GROUND


# ------------------------------------------------- the shape of the scene

def load_points(p, cache, hf_home):
    """The 3D point seen at each pixel, from the orientation model of the
    observe step. Kept in a file beside the output so the model runs once
    per photo."""
    if os.path.exists(cache):
        z = np.load(cache)
        p.points, valid = z["points"].astype(np.float32), z["valid"]
    else:
        if hf_home:
            os.environ["HF_HOME"] = hf_home
        import torch
        from moge.model.v2 import MoGeModel
        device = "cuda" if torch.cuda.is_available() else "cpu"
        model = MoGeModel.from_pretrained(MOGE).to(device).eval()
        with torch.no_grad():
            res = model.infer(torch.tensor(p.photo_rgb / 255.0, dtype=torch.float32, device=device).permute(2, 0, 1))
        p.points = res["points"].cpu().numpy().astype(np.float32)
        valid = res["mask"].cpu().numpy().astype(bool) & np.isfinite(p.points).all(axis=-1)
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        np.savez_compressed(cache, points=p.points.astype(np.float32), valid=valid)
        del model
        torch.cuda.empty_cache()
    p.points[~valid] = (0.0, 0.0, 1e6)
    p.space = Space(p, valid)


class Space:
    """The camera and the ground of the scene, from its 3D points."""
    def __init__(self, p, valid):
        self.valid = valid
        ys, xs = np.nonzero(valid)
        step = max(1, len(ys) // 20000)
        X = p.points[ys[::step], xs[::step]]
        # A pinhole camera: column = ax x/z + bx, row = ay y/z + by.
        self.ax, self.bx = np.polyfit(X[:, 0] / X[:, 2], xs[::step], 1)
        self.ay, self.by = np.polyfit(X[:, 1] / X[:, 2], ys[::step], 1)
        self.ok = False
        if p.normals is None:
            return
        down = p.normals[..., 1] / 127.5 - 1.0
        cand = valid & (down < -0.8)
        if cand.mean() < 0.01:
            return
        G = p.points[cand]
        G = G[::max(1, len(G) // 40000)]
        for _ in range(4):
            c = G.mean(axis=0)
            u = np.linalg.svd(G - c, full_matrices=False)[2][2]
            if u[1] > 0:
                u = -u  # up is against the picture's downward axis
            d = np.abs((G - c) @ u)
            G = G[d <= 2.5 * np.median(d) + 1e-9]
        self.up, self.origin = u.astype(np.float32), c.astype(np.float32)
        self.scale = float(np.median(G[:, 2]))
        self.ok = True

    def height(self, P):
        return (P - self.origin) @ self.up

    def project(self, S):
        z = np.maximum(S[..., 2], 1e-6)
        return self.ax * S[..., 0] / z + self.bx, self.ay * S[..., 1] / z + self.by


def ground_axes(sp, side, toward):
    """Directions along the ground: across the picture, toward the viewer,
    the way shadows fall (to the right for side +1, to the left for -1,
    and toward the viewer by `toward`), and the level direction square to
    that."""
    up = sp.up
    across = np.array([1.0, 0.0, 0.0], np.float32)
    across = across - (across @ up) * up
    across /= np.linalg.norm(across)
    near = np.array([0.0, 0.0, -1.0], np.float32)
    near = near - (near @ up) * up
    near /= np.linalg.norm(near)
    fall = side * across + toward * near
    fall /= np.linalg.norm(fall)
    square = np.cross(up, fall)
    if square @ across < 0:
        square = -square
    return across, near, fall, square


def lay_down(p, g, fall, width_axis, cot, shift=None, centered=False):
    """The shadow of the instance g as its outline laid down along the
    ground. The instance is taken for a flat upright figure standing where
    its lowest pixels are (their 3D points give the place and the scale).
    A point of it at height t above its foot throws its shadow t * cot
    along `fall`. Across, a narrow figure is laid out along `width_axis`;
    a wide one, such as a boat, along the line its foot follows on the
    ground. The whole is one projective map of the picture, so the shadow
    keeps the figure's outline. Returns the shadow as an image, or None."""
    sp = p.space
    ys, xs = np.nonzero(g)
    x0, x1 = np.percentile(xs, [1, 99])
    y0, y1 = np.percentile(ys, [1, 99])
    tall, wide = max(y1 - y0, 1.0), max(x1 - x0, 1.0)
    band = ys >= y1 - 0.06 * tall

    def place(sel):
        P = p.points[ys[sel], xs[sel]]
        P = P[P[:, 2] < 1e5]
        return (np.median(P, axis=0), float(np.median(xs[sel])), float(np.median(ys[sel]))) if len(P) >= 5 else None
    foot = place(band)
    if foot is None:
        return None
    F, xr, _ = foot
    yr = float(y1)
    s = F[2] / abs(sp.ax)  # the size of a pixel at the foot
    ex, slope = s * width_axis, 0.0
    if wide > 1.2 * tall and not centered:
        left, right = place(band & (xs < x0 + 0.2 * wide)), place(band & (xs > x1 - 0.2 * wide))
        if left and right and right[1] - left[1] > 4:
            run = right[0] - left[0]
            run = run - (run @ sp.up) * sp.up
            if np.linalg.norm(run) > 0.3 * s * (right[1] - left[1]):
                ex, slope = run / (right[1] - left[1]), (right[2] - left[2]) / (right[1] - left[1])
                F, xr, yr = left[0], left[1], left[2]

    def on_ground(x, y):
        t = (yr + slope * (x - xr) - y) * s
        if centered:
            t -= 0.5 * tall * s
        G = F + (x - xr) * ex + t * cot * fall
        return G if shift is None else G + shift
    src = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    G = np.array([on_ground(x, y) for x, y in src], np.float32)
    if (G[:, 2] <= 1e-3).any():
        return None
    u, v = sp.project(G)
    dst = np.stack([u, v], axis=1).astype(np.float32)
    if not np.isfinite(dst).all() or np.abs(dst).max() > 20 * p.long:
        return None
    H = cv2.getPerspectiveTransform(np.float32(src), dst)
    return cv2.warpPerspective(g.astype(np.float32), H, (p.w, p.h), flags=cv2.INTER_LINEAR)


def shade_ground(p, shadow, casters, dark, blur_share):
    """Darkens the ground where `shadow` falls: on surfaces that face up,
    and not on the things that cast it. Returns the share of the picture
    shaded."""
    if p.normals is not None:
        down = p.normals[..., 1] / 127.5 - 1.0
        faces_up = np.clip((-down - 0.25) / 0.35, 0.0, 1.0)
    else:
        faces_up = 1.0
    sh = cv2.GaussianBlur(shadow, (0, 0), blur_share * p.long) * faces_up * (1.0 - p.soft(casters, 0.0015))
    p.lab[..., 0] *= 1.0 - dark * sh
    p.lab[..., 2] -= 4.0 * sh
    return float((sh > 0.3).mean())


def long_shadows(p, picks, strength):
    """Under a low sun, the shadow of every instance of the chosen items."""
    sp = p.space
    if sp is None or not sp.ok:
        p.log.append("no ground found: no shadow cast")
        return
    casters = np.zeros((p.h, p.w), bool)
    for k in picks:
        casters |= p.mask(k["id"])
    side = shadow_side(p, casters)
    # Shadows fall to the side and toward the viewer; for a figure whose
    # shadow would then run past the viewer, less toward the viewer.
    ways = [ground_axes(sp, side, toward)[2:] for toward in (0.7, 0.3, 0.0)]
    cot = (1.6, 2.4, 3.4)[strength - 1]
    shadow = np.zeros((p.h, p.w), np.float32)
    count = 0
    for k in picks:
        parts, n = ndimage.label(p.mask(k["id"]))
        sizes = np.bincount(parts.ravel())
        sizes[0] = 0
        for j in np.argsort(sizes)[::-1][:40]:
            if sizes[j] < max(80, 0.02 * sizes.max()):
                break
            one = None
            for fall, square in ways:
                one = lay_down(p, parts == j, fall, square, cot)
                if one is not None:
                    break
            if one is not None:
                shadow = np.maximum(shadow, one)
                count += 1
    share = shade_ground(p, shadow, casters, 0.5, 0.0015)
    p.log.append("long shadows of %s: %d figures, %.1f%% of the picture, sun at %.0f degrees" % (
        ", ".join(k["name"] for k in picks), count, 100.0 * share, math.degrees(math.atan(1.0 / cot))))


# ---------------------------------------------------------------- shape

class Filler:
    """Paints in what was behind an item that has moved."""
    def __init__(self, kind, lama_path):
        self.kind, self.path, self.model = kind, lama_path, None

    def fill(self, rgb, hole):
        ys, xs = np.nonzero(hole)
        if len(ys) == 0:
            return rgb
        h, w = hole.shape
        if self.kind != "lama":
            return cv2.inpaint(rgb, hole.astype(np.uint8) * 255, 7, cv2.INPAINT_TELEA)
        import torch
        if self.model is None:
            self.device = "cuda" if torch.cuda.is_available() else "cpu"
            self.model = torch.jit.load(self.path, map_location=self.device).eval()
        # The hole with room around it, at no more than 1536 on the long side.
        my, mx = int(0.5 * (ys.max() - ys.min())) + 128, int(0.5 * (xs.max() - xs.min())) + 128
        a, b = max(0, ys.min() - my), min(h, ys.max() + my)
        c, d = max(0, xs.min() - mx), min(w, xs.max() + mx)
        crop, cm = rgb[a:b, c:d], hole[a:b, c:d].astype(np.uint8)
        scale = min(1.0, 1536.0 / max(crop.shape[:2]))
        sw, sh = int(crop.shape[1] * scale) // 8 * 8, int(crop.shape[0] * scale) // 8 * 8
        small = cv2.resize(crop, (sw, sh), interpolation=cv2.INTER_AREA)
        sm = (cv2.resize(cm * 255, (sw, sh), interpolation=cv2.INTER_NEAREST) > 0).astype(np.float32)
        with torch.no_grad():
            img = torch.from_numpy(small.astype(np.float32) / 255.0).permute(2, 0, 1)[None].to(self.device)
            msk = torch.from_numpy(sm)[None, None].to(self.device)
            out = self.model(img, msk)[0].permute(1, 2, 0).clamp(0, 1).cpu().numpy()
        out = cv2.resize((out * 255.0 + 0.5).astype(np.uint8), (crop.shape[1], crop.shape[0]), interpolation=cv2.INTER_CUBIC)
        blend = cv2.GaussianBlur(cm.astype(np.float32), (0, 0), 1.5)[..., None]
        result = rgb.copy()
        result[a:b, c:d] = (crop * (1.0 - blend) + out * blend + 0.5).astype(np.uint8)
        return result


def with_riders(p, g, own, moves):
    """The instance g with what it carries. Always: what it encloses. If
    it is to be moved, also what lies within its convex outline at its own
    distance from the viewer (3D points): other things that are mostly
    there (a man in a boat), and pieces that belong to no item and touch
    it (his bundle)."""
    out = g | (ndimage.binary_fill_holes(g) & ~g)
    if not moves or p.points is None:
        return out
    hull = np.zeros(g.shape, np.uint8)
    cv2.fillConvexPoly(hull, cv2.convexHull(cv2.findNonZero(g.astype(np.uint8))), 1)
    z = p.points[..., 2]
    lo, hi = np.percentile(z[g], [3, 97])
    margin = 0.25 * (hi - lo) + 0.02 * lo
    there = (hull > 0) & ~out & (z > lo - margin) & (z < hi + margin)
    things = (p.items > 0) & ~np.isin(p.items, [i for i in p.by_id if p.is_ground(i) or i in own])
    full, n = ndimage.label(things & ~out)
    for k in np.unique(full[there & things]):
        whole = full == k
        if k and (whole & there).sum() >= 0.5 * whole.sum() and whole.sum() < g.sum():
            out |= whole
    loose, n = ndimage.label(there & (p.items == 0))
    touch = ndimage.binary_dilation(g, iterations=3)
    for k in np.unique(loose[touch]):
        piece = loose == k
        if k and 0.002 * g.sum() <= piece.sum() <= 0.3 * g.sum():
            out |= piece
    return out


def shape_map(mech, s, g, p, rng):
    """For a change of shape of the instance g: a function that says, for
    every place of the new picture, the place of the old one it shows; a
    word for the log; and how far the instance rose, in pixels."""
    ys, xs = np.nonzero(g)
    x0, x1 = np.percentile(xs, [1, 99])
    y0, y1 = np.percentile(ys, [1, 99])
    w, h = max(x1 - x0, 1.0), max(y1 - y0, 1.0)
    foot = float(xs[ys >= y1 - 0.1 * h].mean())  # where it stands
    cx = float(xs.mean())
    room = max(0.0, float(ys.min()) - 0.02 * p.h)  # how far it can rise
    k = s - 1

    if mech == "melting":
        phase = rng.uniform(0.0, 2.0 * math.pi)

        def field(x, y):
            t = np.clip((y - y0) / h, 0.0, 1.3)  # 0 at its top, 1 at its foot
            drip = 0.55 + 0.45 * np.sin(2.0 * math.pi * x / (0.4 * min(w, h)) + phase)
            return 0.05 * (k + 1) * (x - cx) * t * t, (0.16, 0.28, 0.40)[k] * h * t ** 1.6 * drip

        def back(px, py):
            qx, qy = px.copy(), py.copy()
            for _ in range(12):  # q with q + D(q) = p
                dx, dy = field(qx, qy)
                qx, qy = px - dx.astype(np.float32), py - dy.astype(np.float32)
            return qx, qy
        return back, "", 0.0

    if mech == "on stilts":
        # The lowest part of it, what it stands on, is stretched; the rest
        # rises unchanged.
        rows = np.bincount(ys, minlength=p.h).astype(np.float32)
        wide = np.nonzero(rows[int(y0):int(y1) + 1] > 0.4 * rows.max())[0]
        legs = float(np.clip(y1 - (int(y0) + wide.max()) if len(wide) else 0.3 * h, 0.1 * h, 0.5 * h))
        f = min((2.0, 3.0, 4.2)[k], 1.0 + room / legs)
        if f < 1.25:
            return None, "no room above it", 0.0

        def back(px, py):
            up = y1 - py
            old = np.where(up <= 0, up, np.where(up <= f * legs, up / f, up - (f - 1.0) * legs))
            return px, (y1 - old).astype(np.float32)
        return back, "x%.1f" % f, (f - 1.0) * legs

    if mech == "levitating":
        rise = min((0.45, 0.75, 1.1)[k] * h, room)
        if rise < 0.2 * h:
            return None, "no room above it", 0.0
        return (lambda px, py: (px, py + np.float32(rise))), "by %.0f%% of its height" % (100 * rise / h), rise

    if mech == "giant":
        f = (1.6, 2.2, 3.0)[k]
        f = min(f, 1.0 + room / h)
        if f < 1.25:
            return None, "no room above it", 0.0
        return (lambda px, py: ((foot + (px - foot) / f).astype(np.float32), (y1 + (py - y1) / f).astype(np.float32))), \
            "x%.1f" % f, 0.0
    raise ValueError(mech)


def nearest_outside(g):
    return ndimage.distance_transform_edt(g, return_distances=False, return_indices=True)


def give_grain(p, before, hole):
    """The fill model leaves a large place smooth. This gives it the fine
    variation of what is around it: for each filled pixel, the fine detail
    (not the color) found as far outside the place as the pixel is inside
    it, if that is the same kind of ground."""
    fine = before - cv2.GaussianBlur(before, (0, 0), 0.006 * p.long)
    deep, near = ndimage.distance_transform_edt(hole, return_indices=True)
    yy, xx = np.mgrid[0:p.h, 0:p.w]
    my = np.clip(2 * near[0] - yy, 0, p.h - 1)
    mx = np.clip(2 * near[1] - xx, 0, p.w - 1)
    ok = hole & ~hole[my, mx] & (p.items[my, mx] == p.items[near[0], near[1]])
    w = (ok * np.clip(deep / (0.004 * p.long), 0.0, 1.0)).astype(np.float32)
    p.lab += fine[my, mx] * w[..., None]
    return float(ok.sum()) / max(1, int(hole.sum()))


def reshape(p, i, mech, s, filler, rng):
    """Carries out a change of shape on each instance of an item that is
    not small beside the largest. Returns the pixels of the instances in
    their new places."""
    it = p.by_id[i]
    own = p.group(i)
    whole = np.isin(p.items, own)
    share = float(whole.mean())
    moved = np.zeros_like(whole)
    if it.get("treatment") in GROUND | vocab.NO_SHAPE_TREATMENTS or share > MAX_SHAPE_SHARE or share < 0.0003:
        p.log.append("%s: %s not carried out (%s, %.0f%% of the picture)" % (it["name"], mech, it.get("treatment"), 100 * share))
        return moved
    parts, n = ndimage.label(whole)
    sizes = np.bincount(parts.ravel())
    sizes[0] = 0
    notes, done = "", 0
    for k in np.argsort(sizes)[::-1][:6]:
        if sizes[k] < 0.25 * sizes.max():
            break
        g = with_riders(p, parts == k, own, mech != "melting")
        back, note, rise = shape_map(mech, s, g, p, rng)
        if back is None:
            notes = note
            continue
        if mech == "levitating":
            there = np.zeros_like(g)
            there[:-int(rise)] = g[int(rise):]
            apart = facts.alike(p.lab, g, there)
            if apart >= facts.ALIKE:
                notes = "not moved: it would be seen against another background (%.0f apart)" % apart
                continue
        below = None
        if mech == "levitating" and p.space is not None and p.space.ok:
            # Its shadow straight below it: its outline laid flat where it
            # stood, pushed a little the way shadows fall.
            across, near, fall, _ = ground_axes(p.space, shadow_side(p, g), 0.3)
            lift = rise * float(np.median(p.points[..., 2][g])) / abs(p.space.ax)
            below = lay_down(p, g, -near, across, 0.45, shift=0.3 * lift * fall, centered=True)
        new = reshape_one(p, i, g, back, rise, filler)
        if below is not None:
            share = shade_ground(p, below, new, 0.42, 0.007)
            note += ", its shadow below %.1f%% of the picture" % (100.0 * share)
        moved |= new
        notes, done = note, done + 1
    p.log.append("%s: %s %d %s (%d of %d instances)" % (it["name"], mech, s, notes, done, n))
    return moved


def reshape_one(p, i, g, back, rise, filler):
    """The instance g of item i is taken out of the picture, what was
    behind it is painted in, and it is put back in its new shape. The item
    layer, the orientation layer and the 3D points move with it."""
    py, px = np.mgrid[0:p.h, 0:p.w].astype(np.float32)
    qx, qy = back(px, py)
    inside = ndimage.distance_transform_edt(~g, return_distances=False, return_indices=True)
    outside = nearest_outside(g)
    # The item alone, its colors carried past its outline so no fringe of
    # the surroundings comes along.
    alpha = cv2.GaussianBlur(ndimage.binary_erosion(g, iterations=1).astype(np.float32), (0, 0), 1.0)
    alpha = cv2.remap(alpha, qx, qy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    item = cv2.remap(p.lab[inside[0], inside[1]], qx, qy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    ids = cv2.remap(np.where(g, p.items, 0).astype(np.uint8), qx, qy, cv2.INTER_NEAREST,
                    borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    hole = ndimage.binary_dilation(g, iterations=max(3, int(0.006 * p.long)))
    before = p.lab.copy()
    p.set_rgb(filler.fill(p.rgb(), hole))  # what was behind it
    p.items = p.items[outside[0], outside[1]]
    give_grain(p, before, hole)
    a3 = alpha[..., None]
    on = alpha >= 0.5
    p.lab = p.lab * (1.0 - a3) + item * a3
    p.items = np.where(on, np.where(ids > 0, ids, i), p.items).astype(np.uint8)
    if p.normals is not None:
        n_item = cv2.remap(p.normals[inside[0], inside[1]], qx, qy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        p.normals = p.normals[outside[0], outside[1]] * (1.0 - a3) + n_item * a3
    if p.points is not None:
        pts = cv2.remap(p.points[inside[0], inside[1]], qx, qy, cv2.INTER_NEAREST, borderMode=cv2.BORDER_REPLICATE)
        if rise > 0 and p.space.ok:
            pts = pts + p.space.up[None, None, :] * (rise * pts[..., 2:3] / abs(p.space.ay))
        p.points = np.where(on[..., None], pts, p.points[outside[0], outside[1]])
        p.space.valid = np.where(on, True, p.space.valid[outside[0], outside[1]])
    return on


# ---------------------------------------------------- repeats and echoes

def largest(p, i):
    """The largest instance of item i, with what it carries."""
    own = p.group(i)
    parts, n = ndimage.label(np.isin(p.items, own))
    if n == 0:
        return None
    sizes = np.bincount(parts.ravel())
    sizes[0] = 0
    return with_riders(p, parts == int(np.argmax(sizes)), own, True)


def cut_out(p, g):
    """The instance alone: how far each pixel belongs to it, and its
    colors carried past its outline."""
    inside = ndimage.distance_transform_edt(~g, return_distances=False, return_indices=True)
    alpha = cv2.GaussianBlur(ndimage.binary_erosion(g, iterations=1).astype(np.float32), (0, 0), 1.0)
    return alpha, p.lab[inside[0], inside[1]].copy(), inside


def repeat(p, i, s, rng):
    """The largest instance of the item is repeated, near and far, over
    the open space of the picture: in staggered rows, at three sizes, only
    where a repeat covers ground or nothing and no other thing."""
    g = largest(p, i)
    name = p.by_id[i]["name"]
    if g is None:
        return
    alpha, colors, inside = cut_out(p, g)
    ids = np.where(g, p.items, 0).astype(np.uint8)
    normals = p.normals[inside[0], inside[1]].copy() if p.normals is not None else None
    ys, xs = np.nonzero(g)
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    w, h = x1 - x0 + 1, y1 - y0 + 1
    cx, cy = 0.5 * (x0 + x1), 0.5 * (y0 + y1)
    open_space = (p.items == 0) | np.isin(p.items, [j for j in p.by_id if p.is_ground(j)])
    taken = ndimage.binary_dilation(g, iterations=max(2, int(0.15 * min(w, h))))
    places = []
    for scale in (1.0, 0.72, 0.5):
        sx, sy = 1.7 * w * scale, 1.45 * h * scale
        for r in range(int(p.h / sy) + 2):
            for c in range(int(p.w / sx) + 2):
                places.append((scale, (c + 0.5 * (r % 2)) * sx + rng.uniform(-0.1, 0.1) * sx,
                               r * sy + rng.uniform(-0.1, 0.1) * sy))
    rng.shuffle(places)
    want, put, other = (5, 9, 14)[s - 1], [], 0
    gf = g.astype(np.float32)
    for scale, px, py in places:
        if len(put) >= want:
            break
        if px - 0.5 * w * scale < 2 or px + 0.5 * w * scale > p.w - 2 or \
           py - 0.5 * h * scale < 2 or py + 0.5 * h * scale > p.h - 2:
            continue
        M = np.float32([[scale, 0, px - scale * cx], [0, scale, py - scale * cy]])
        there = cv2.warpAffine(gf, M, (p.w, p.h), flags=cv2.INTER_NEAREST) > 0.5
        n = int(there.sum())
        if n == 0 or (there & open_space & ~taken).sum() < 0.92 * n:
            continue
        if facts.alike(p.first, g, there) >= facts.ALIKE:
            other += 1
            continue
        taken |= ndimage.binary_dilation(there, iterations=max(2, int(0.1 * min(w, h) * scale)))
        put.append((scale, M))
    for scale, M in sorted(put, key=lambda q: q[0]):  # the far ones first
        a = cv2.warpAffine(alpha, M, (p.w, p.h), flags=cv2.INTER_LINEAR)
        a3 = a[..., None]
        p.lab = p.lab * (1.0 - a3) + cv2.warpAffine(colors, M, (p.w, p.h), flags=cv2.INTER_LINEAR,
                                                    borderMode=cv2.BORDER_REPLICATE) * a3
        on = a >= 0.5
        moved = cv2.warpAffine(ids, M, (p.w, p.h), flags=cv2.INTER_NEAREST)
        p.items = np.where(on, np.where(moved > 0, moved, i), p.items).astype(np.uint8)
        if normals is not None:
            p.normals = p.normals * (1.0 - a3) + cv2.warpAffine(normals, M, (p.w, p.h), flags=cv2.INTER_LINEAR,
                                                                 borderMode=cv2.BORDER_REPLICATE) * a3
    p.log.append("%s: repeated %d (%d times; %d places passed over for another background)" % (
        name, s, len(put), other))


def echo(p, i, s, rng):
    """The largest instance of the item trails fading repeats of itself,
    toward the nearer side of the picture (a thing is taken to move into
    the picture). The thing itself stays in front of them."""
    g = largest(p, i)
    name = p.by_id[i]["name"]
    if g is None:
        return
    alpha, colors, _ = cut_out(p, g)
    xs = np.nonzero(g)[1]
    w = xs.max() - xs.min() + 1
    behind = -1.0 if 0.5 * (xs.min() + xs.max()) < 0.5 * p.w else 1.0
    n = (3, 5, 7)[s - 1]
    step = (0.10, 0.13, 0.16)[s - 1] * w
    for k in range(n, 0, -1):  # the farthest, faintest first
        M = np.float32([[1, 0, behind * k * step], [0, 1, 0]])
        a3 = (cv2.warpAffine(alpha, M, (p.w, p.h), flags=cv2.INTER_LINEAR) * 0.7 * (1.0 - k / (n + 1.0)))[..., None]
        p.lab = p.lab * (1.0 - a3) + cv2.warpAffine(colors, M, (p.w, p.h), flags=cv2.INTER_LINEAR,
                                                    borderMode=cv2.BORDER_REPLICATE) * a3
    a3 = alpha[..., None]
    p.lab = (p.lab * (1.0 - a3) + colors * a3).astype(np.float32)
    p.log.append("%s: echoing %d (%d repeats, to the %s)" % (name, s, n, "left" if behind < 0 else "right"))


# ------------------------------------------------- the solid of triangles

def camera_normals(p):
    """The orientation layer as unit normals in the camera's axes (x
    right, y down, z away): the layer's blue is toward the viewer."""
    n = p.normals / 127.5 - 1.0
    n[..., 2] = -n[..., 2]
    return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)


def triangulate(p, i, s, count, table, filler, rng):
    """Replaces an item by a solid of triangles. Its outline and its
    inside are covered with points, closer together where its surface
    turns more (orientation layer). How fine the triangles are is set by
    the strength: their sides are about 6, 4 or 2.6 percent of the long
    side of the picture; or by `count`, about how many there are. Each
    triangle is flat: its three corners take their place in space from
    the 3D points of the scene, and that gives its normal, which the
    orientation layer then states for the whole triangle. Each triangle
    is given one color, the middle one of the part of the picture that
    falls on it; the brushwork within it is the painter's.

    How the solid ends is decided along its outline, stretch by stretch,
    by the two materials that meet there (`table`, facts.ragged_table).
    Where they meet abruptly (a watch on a wrist) the corners lie on the
    outline, and what the triangles leave of the thing is painted in as
    background. Where they meet raggedly (leaves against sky, a face
    against its hair) the thing is not cut out: the triangles are much
    finer there and go on over a margin beyond the outline, and each of
    them is the thing's or the surroundings' by its color, so the two run
    into each other. The triangles are finer too where two parts of the
    thing meet raggedly."""
    from scipy.spatial import Delaunay
    it = p.by_id[i]
    # A part made of thin things (the trunks and branches of a tree) is
    # left as the picture shows it: they are narrower than a triangle,
    # and what is outlined with them is mostly what is seen between them.
    own = [j for j in p.group(i) if j == i or p.by_id[j].get("treatment") not in facts.THIN]
    g = np.isin(p.items, own)
    if not g.any() or p.space is None or p.normals is None:
        p.log.append("%s: not triangulated (no pixels or no 3D shape)" % it["name"])
        return
    g = g | (ndimage.binary_fill_holes(g) & ~g)
    area = float(g.sum())
    # The side of a triangle of the mean size, and of a fine one.
    edge = math.sqrt(4.0 * (area / count) / math.sqrt(3.0)) if count else (0.06, 0.04, 0.026)[s - 1] * p.long
    fine = 0.4 * edge

    # Where materials meet raggedly: on the thing's outline, and between its parts.
    rag_out, rag_in = np.zeros(g.shape, bool), np.zeros(g.shape, bool)
    met = set()
    for dy, dx in ((0, 1), (1, 0)):
        a, b = p.items[:p.h - dy, :p.w - dx], p.items[dy:, dx:]
        ga, gb = g[:p.h - dy, :p.w - dx], g[dy:, dx:]
        ragged = (a != b) & table[a, b]
        for mask, hit in ((rag_out, ragged & (ga != gb)), (rag_in, ragged & ga & gb)):
            mask[:p.h - dy, :p.w - dx] |= hit
            mask[dy:, dx:] |= hit
        hit = ragged & (ga != gb)
        for code in np.unique(a[hit].astype(np.int32) * 256 + b[hit]):
            x, y = (int(code) // 256, int(code) % 256) if ga[hit][0] or True else (0, 0)
            inner_id, outer_id = (x, y) if x in own else (y, x)
            met.add((p.by_id[inner_id]["name"] if inner_id else "what it encloses",
                     p.by_id[outer_id]["name"] if outer_id else "what has no item"))
    far = np.full(g.shape, 1e6, np.float32)
    to_out = ndimage.distance_transform_edt(~rag_out).astype(np.float32) if rag_out.any() else far
    to_any = ndimage.distance_transform_edt(~(rag_out | rag_in)).astype(np.float32) if (rag_out | rag_in).any() else far
    off = np.where(g, ndimage.distance_transform_edt(g), ndimage.distance_transform_edt(~g)).astype(np.float32)
    region = g | ((off <= 0.7 * edge) & (to_out <= 0.7 * edge + 2.0))  # with a margin beyond a ragged outline
    side = fine + (edge - fine) * np.clip((to_any - 0.8 * edge) / edge, 0.0, 1.0)

    # Corners: along the outline of what is covered, then inside.
    corners = []
    outlines, _ = cv2.findContours(region.astype(np.uint8), cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    for c in outlines:
        c = c[:, 0, :]
        if len(c) < 3.0 * fine:
            continue
        gone = 1e9
        for x, y in c:
            gone += 1.0
            if gone >= side[y, x]:
                corners.append((x, y))
                gone = 0.0
    turn = np.zeros((p.h, p.w), np.float32)
    for c in range(3):
        turn += np.abs(cv2.Sobel(p.normals[..., c], cv2.CV_32F, 1, 0)) + np.abs(cv2.Sobel(p.normals[..., c], cv2.CV_32F, 0, 1))
    turn = cv2.GaussianBlur(turn, (0, 0), 0.5 * edge)
    turn = np.clip(turn / (np.percentile(turn[g], 90) + 1e-6), 0.0, 1.5)
    inner = ndimage.binary_erosion(region, iterations=max(1, int(0.35 * fine)))
    ys, xs = np.nonzero(inner)
    kept = np.array(corners, np.float32).reshape(-1, 2)
    for k in rng.permutation(len(ys))[:40000 if (rag_out | rag_in).any() else 15000]:
        x, y = float(xs[k]), float(ys[k])
        r = 0.9 * side[ys[k], xs[k]] / (1.0 + 0.8 * turn[ys[k], xs[k]])
        if len(kept) == 0 or ((kept[:, 0] - x) ** 2 + (kept[:, 1] - y) ** 2).min() >= r * r:
            kept = np.vstack([kept, [[x, y]]])
    if len(kept) < 4:
        p.log.append("%s: too small to triangulate" % it["name"])
        return
    reach = ndimage.binary_dilation(region, iterations=2)

    def inside(q):
        x, y = int(round(q[0])), int(round(q[1]))
        return 0 <= x < p.w and 0 <= y < p.h and reach[y, x]
    tris = []
    for t in Delaunay(kept).simplices:
        a, b, c = kept[t]
        if inside((a + b + c) / 3.0) and sum(inside(m) for m in ((a + b) / 2, (b + c) / 2, (c + a) / 2)) >= 2:
            tris.append(t)
    tris = np.array(tris)
    label = np.zeros((p.h, p.w), np.int32)
    for k, t in enumerate(tris):
        cv2.fillConvexPoly(label, np.round(kept[t]).astype(np.int32), k + 1)
    idx = np.arange(1, len(tris) + 1)

    # The corners in space, and each triangle's normal.
    firm = (off > 3) & p.space.valid  # away from the outline, where depth jumps
    near = ndimage.distance_transform_edt(~firm, return_distances=False, return_indices=True)
    r = max(2, int(0.15 * fine))
    space = np.zeros((len(kept), 3), np.float32)
    for k, (x, y) in enumerate(np.round(kept).astype(int)):
        x, y = min(max(x, 0), p.w - 1), min(max(y, 0), p.h - 1)
        a, b, c, d = max(0, y - r), min(p.h, y + r + 1), max(0, x - r), min(p.w, x + r + 1)
        win = firm[a:b, c:d]
        space[k] = np.median(p.points[a:b, c:d][win], axis=0) if win.sum() >= 3 else p.points[near[0][y, x], near[1][y, x]]
    ncam = camera_normals(p)
    seen = np.stack([np.concatenate([[0], ndimage.mean(ncam[..., c], label, idx)]) for c in range(3)], axis=1)
    seen /= np.maximum(np.linalg.norm(seen, axis=1, keepdims=True), 1e-6)
    normal = seen.copy()
    built = 0
    for k, t in enumerate(tris):
        a, b, c = space[t]
        nrm = np.cross(b - a, c - a)
        length = np.linalg.norm(nrm)
        if length < 1e-9:
            continue
        nrm /= length
        if nrm @ ((a + b + c) / 3.0) > 0:
            nrm = -nrm  # toward the viewer
        if nrm @ seen[k + 1] > 0.77:  # within 40 degrees of what the orientation layer says
            normal[k + 1] = nrm
            built += 1

    # One color for each triangle: the middle one of what the picture shows there.
    tone = np.stack([np.asarray(ndimage.median(p.lab[..., c], label, idx)) for c in range(3)], axis=1)
    painted = np.concatenate([np.zeros((1, 3)), tone]).astype(np.float32)[label]

    # Along a ragged outline each small triangle is the thing's or the
    # surroundings', by its color: it goes with whichever has, close by,
    # the larger share of pixels of about that color. If neither has any,
    # only if it lies well inside the thing. Only the thing's triangles are
    # painted flat. The others stay as the picture shows them, but for bits
    # of the thing in them, which take the color of the surroundings there,
    # so that nothing of the thing shows past its solid.
    # Comparing the triangle's color with the mean color of each side was
    # tried first: sunlit lawn at the foot of a tree is nearer the mean of
    # its sunlit crown than the mean of lawn and shade.
    mine = np.ones(len(tris) + 1, bool)
    mine[0] = False
    if rag_out.any():
        yy, xx = np.mgrid[0:p.h, 0:p.w].astype(np.float32)
        cy = np.clip(np.rint(ndimage.mean(yy, label, idx)).astype(int), 0, p.h - 1)
        cx = np.clip(np.rint(ndimage.mean(xx, label, idx)).astype(int), 0, p.w - 1)
        # The thing's pixels that vote lie well inside it: at its rim the
        # item layer is the least to be trusted.
        thing, around = g & (off > 0.35 * edge), ~g & (off > 2)
        well_in = np.asarray(ndimage.mean(thing.astype(np.float32), label, idx)) >= 0.5
        r = int(0.8 * edge)
        for k in np.nonzero(to_out[cy, cx] <= 0.9 * edge)[0]:
            a, b = max(0, cy[k] - r), min(p.h, cy[k] + r + 1)
            c, d = max(0, cx[k] - r), min(p.w, cx[k] + r + 1)
            alike = np.linalg.norm(p.lab[a:b, c:d] - tone[k], axis=-1) < 15.0
            t, o = thing[a:b, c:d], around[a:b, c:d]
            with_thing = float(alike[t].mean()) if t.any() else 0.0
            with_around = float(alike[o].mean()) if o.any() else 0.0
            # The farther a triangle lies beyond the outline, the less the
            # thing's share counts, and the deeper inside, the less the
            # surroundings': sunlit lawn half a triangle below a tree has
            # the color of its sunlit leaves.
            away = math.exp(-off[cy[k], cx[k]] / (0.25 * edge))
            if g[cy[k], cx[k]]:
                with_around *= away
            else:
                with_thing *= away
            if max(with_thing, with_around) < 0.02:
                # A color found on neither side: usually the middle color of
                # a triangle that is half the thing and half not. The
                # thing's only if it lies well inside it.
                mine[k + 1] = well_in[k]
            else:
                # The thing must be clearly ahead. With a bare lead, the gray
                # of a road seen between the trunks of a tree counted as the
                # tree's, on a few dark pixels of its crown.
                mine[k + 1] = with_thing > with_around and (with_thing >= 0.05 or with_around < 0.01)
        # Bits of the thing that show past its solid against the sky (leaves
        # at the top of a crown, against clouds) are painted over with sky.
        # A pixel is such a bit if it lies within a full side of the solid,
        # has a color that the sky of this picture does not have, belongs
        # to no other thing, and what is around it is mostly sky. This is
        # done against sky only: there the two cannot be confused. Tried
        # against any surroundings, by the pixel's nearness to the thing's
        # color, it repainted the shade under a tree.
        sky = [j for j in p.by_id if p.by_id[j].get("treatment") in ("sky", "cloud")]
        is_sky = np.isin(p.items, sky) & ~g
        if is_sky.sum() > 0.01 * is_sky.size:
            # The sky's colors: cells of 8 in CIELAB that hold at least
            # 0.05 percent of it, and the cells next to them.
            q = np.clip((p.lab + np.array([0.0, 128.0, 128.0], np.float32)) / 8.0, 0, 31).astype(np.int32)
            cells = np.zeros((32, 32, 32), np.float32)
            np.add.at(cells, (q[..., 0][is_sky], q[..., 1][is_sky], q[..., 2][is_sky]), 1.0)
            has = ndimage.maximum_filter(cells >= 0.0005 * is_sky.sum(), size=3)
            sky_like = has[q[..., 0], q[..., 1], q[..., 2]]
            solid_now = mine[label]
            far = ndimage.distance_transform_edt(~solid_now)
            m = (sky_like & ~solid_now).astype(np.float32)
            k = cv2.GaussianBlur(m, (0, 0), 0.4 * edge)
            sky_color = cv2.GaussianBlur(p.lab * m[..., None], (0, 0), 0.4 * edge) / np.maximum(k, 1e-4)[..., None]
            in_sky = k / np.maximum(cv2.GaussianBlur((~solid_now).astype(np.float32), (0, 0), 0.4 * edge), 1e-4) >= 0.6
            others = [j for j in p.by_id if j not in own and p.by_id[j].get("treatment") not in GROUND | {"other"}]
            stray = ~solid_now & (far <= edge) & ~sky_like & in_sky & ~np.isin(p.items, others)
            # With a few pixels around each bit: at its rim the bit and
            # the sky are blended, and that pale rim passes for sky.
            stray = ndimage.binary_dilation(stray, iterations=max(2, int(0.003 * p.long))) & ~solid_now & \
                ~np.isin(p.items, others)
            p.lab[stray] = sky_color[stray]
    solid = mine[label]

    # Along an abrupt outline: what the triangles leave of the thing is
    # painted in as background. The fill model is shown the place without
    # the thing, so that it does not paint the thing back in.
    left = g & (label == 0)
    if left.sum() > 0.0005 * area:
        outside = nearest_outside(g)
        hole = ndimage.binary_dilation(g, iterations=max(3, int(0.006 * p.long)))
        filled = cv2.cvtColor(filler.fill(p.rgb(), hole).astype(np.float32) / 255.0, cv2.COLOR_RGB2LAB)
        take = ndimage.binary_dilation(left, iterations=2) & (label == 0)
        before = p.lab.copy()
        p.lab[take] = filled[take]
        p.items = np.where(left, p.items[outside[0], outside[1]], p.items).astype(np.uint8)
        give_grain(p, before, take)

    a3 = cv2.GaussianBlur(solid.astype(np.float32), (0, 0), 0.7)[..., None]
    p.lab = (p.lab * (1.0 - a3) + painted * a3).astype(np.float32)
    flat = normal[label].copy()
    flat[..., 2] = -flat[..., 2]
    p.normals = np.where(solid[..., None], (flat * 0.5 + 0.5) * 255.0, p.normals).astype(np.float32)
    p.log.append("%s: %d triangles, each in one color; %s (%d normals from the 3D points, %d from the orientation layer)" % (
        it["name"], int(mine.sum()),
        "ragged where " + ", ".join("%s meets %s" % pair for pair in sorted(met)[:6]) if met else "its outline abrupt all round",
        built, len(tris) - built))


# ---------------------------------------------------------------- facets

def straight_planes(p, region, weight, want, rng):
    """The region cut again and again by a straight line at one of a few
    angles; the piece cut next is the one whose surface turns most. The
    first form of the facets: the cuts take no notice of the form.
    Returns each pixel's plane (0: outside the region) and their number."""
    label = region.astype(np.int32)
    n, tries = 1, 0
    while n < want and tries < 400:
        tries += 1
        mass = ndimage.sum(weight, label, np.arange(1, n + 1))
        cell = int(np.argmax(mass * rng.uniform(0.85, 1.0, n))) + 1
        ys, xs = np.nonzero(label == cell)
        j = int(rng.integers(len(ys)))
        py, px = 0.6 * ys.mean() + 0.4 * ys[j], 0.6 * xs.mean() + 0.4 * xs[j]
        a = math.radians(FACET_ANGLES[int(rng.integers(len(FACET_ANGLES)))] + rng.normal(0.0, 6.0))
        far = (xs - px) * -math.sin(a) + (ys - py) * math.cos(a) > 0
        if not 0.18 < far.mean() < 0.82:
            continue
        n += 1
        label[ys[far], xs[far]] = n
    return label, n


def form_planes(p, region, turn, want, rng):
    """The region cut into planes whose edges follow the form. A mesh of
    triangles is laid over it, finer where the surface turns (as for the
    solid of triangles, about nine triangles to a plane), and triangles
    that lie side by side are joined, those most alike first, until `want`
    planes are left. Alike: facing the same way (orientation layer) and,
    for less, of the same color, so that a plane is a piece of surface
    that faces one way and is lit one way. The outline of each plane is
    then drawn again in a few straight runs: left as the sides of the
    triangles it was saw-toothed. Returns each pixel's plane (0: outside
    the region) and their number."""
    from scipy.spatial import Delaunay
    edge = math.sqrt(4.0 * (float(region.sum()) / (9.0 * want)) / math.sqrt(3.0))
    corners = []
    outlines, _ = cv2.findContours(region.astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    for c in outlines:
        c = c[:, 0, :]
        if len(c) < 3.0 * edge:
            continue
        for k in range(0, len(c), max(1, int(edge))):
            corners.append((float(c[k][0]), float(c[k][1])))
    kept = np.array(corners, np.float32).reshape(-1, 2)
    inner = ndimage.binary_erosion(region, iterations=max(1, int(0.3 * edge)))
    ys, xs = np.nonzero(inner)
    for k in rng.permutation(len(ys))[:15000]:
        x, y = float(xs[k]), float(ys[k])
        r = 0.9 * edge / (1.0 + 0.8 * turn[ys[k], xs[k]])
        if len(kept) == 0 or ((kept[:, 0] - x) ** 2 + (kept[:, 1] - y) ** 2).min() >= r * r:
            kept = np.vstack([kept, [[x, y]]])
    if len(kept) < 4:
        return region.astype(np.int32), 1
    mesh = Delaunay(kept)
    number = np.zeros(len(mesh.simplices), np.int32)  # of each triangle that lies in the region; 0: not one
    tri = np.zeros((p.h, p.w), np.int32)
    for k, t in enumerate(mesh.simplices):
        cx, cy = kept[t].mean(axis=0)
        if region[min(max(int(round(cy)), 0), p.h - 1), min(max(int(round(cx)), 0), p.w - 1)]:
            number[k] = int(number.max()) + 1
            cv2.fillConvexPoly(tri, np.round(kept[t]).astype(np.int32), int(number[k]))
    count = int(number.max())
    if count < 2:
        return region.astype(np.int32), 1
    idx = np.arange(1, count + 1)
    ncam = camera_normals(p)
    size = np.maximum(np.asarray(ndimage.sum(np.ones((p.h, p.w), np.float32), tri, idx)), 1.0)
    faces = np.stack([np.asarray(ndimage.mean(ncam[..., c], tri, idx)) for c in range(3)], axis=1)
    faces /= np.maximum(np.linalg.norm(faces, axis=1, keepdims=True), 1e-6)
    color = np.stack([np.asarray(ndimage.mean(p.lab[..., c], tri, idx)) for c in range(3)], axis=1)
    # What two triangles are compared by: the way they face, and for less
    # their color (a full turn of the surface counts like 60 of CIELAB).
    kind = np.concatenate([faces, color / 60.0], axis=1)

    # Which triangles lie side by side.
    beside = {k: set() for k in range(count)}
    for k, around in enumerate(mesh.neighbors):
        if number[k]:
            for j in around:
                if j >= 0 and number[j]:
                    beside[number[k] - 1].add(int(number[j]) - 1)
    # Joined two at a time, the two that are most alike for their size
    # (Ward's rule), so that the planes come out of about one size.
    plane = np.arange(count)
    weight, mean = {k: float(size[k]) for k in range(count)}, {k: kind[k].copy() for k in range(count)}
    left = count
    while left > want:
        best, pair = None, None
        for a in beside:
            for b in beside[a]:
                if a < b:
                    cost = weight[a] * weight[b] / (weight[a] + weight[b]) * float(((mean[a] - mean[b]) ** 2).sum())
                    if best is None or cost < best:
                        best, pair = cost, (a, b)
        if pair is None:
            break  # what is left does not touch
        a, b = pair
        mean[a] = (mean[a] * weight[a] + mean[b] * weight[b]) / (weight[a] + weight[b])
        weight[a] += weight[b]
        for c in beside.pop(b):
            beside[c].discard(b)
            if c != a:
                beside[c].add(a)
                beside[a].add(c)
        plane[plane == b] = a
        left -= 1
    names = {a: k + 1 for k, a in enumerate(sorted(set(plane.tolist())))}
    label = np.concatenate([[0], [names[a] for a in plane]]).astype(np.int32)[tri]
    # Each plane's outline in straight runs: corners that stand out from
    # the run between their neighbors by less than 0.7 of a triangle's side
    # are dropped. The larger planes are drawn first, so that a small one
    # is not lost under its neighbors.
    straight = np.zeros_like(label)
    areas = np.bincount(label.ravel(), minlength=len(names) + 1)
    for k in sorted(range(1, len(names) + 1), key=lambda k: -areas[k]):
        outlines, _ = cv2.findContours((label == k).astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for c in outlines:
            if len(c) >= 3:
                cv2.fillPoly(straight, [cv2.approxPolyDP(c, 0.7 * edge, True)], int(k))
    # What that leaves of the region goes with the plane next to it.
    near = ndimage.distance_transform_edt(straight == 0, return_distances=False, return_indices=True)
    label = np.where(region, straight[near[0], near[1]], 0).astype(np.int32)
    kept_planes = np.unique(label[label > 0])
    again = np.zeros(len(names) + 1, np.int32)
    again[kept_planes] = np.arange(1, len(kept_planes) + 1)
    return again[label], len(kept_planes)


FACET_ANGLES = (90.0, 0.0, 35.0, -35.0, 62.0, -62.0)


def facet(p, g, s, names, rng, cuts="form", planes=0, apart=0):
    """Breaks the pixels g into a few large flat planes with straight
    edges. The region (g and a margin around it, so the planes slide into
    the surroundings) is cut into planes: along the form (`cuts` "form",
    form_planes), or by straight lines at a few angles ("straight", the
    first form, also used where there is no orientation layer). Each
    plane shows the subject a little turned,
    moved and scaled, as if seen from a place of its own, and is drawn
    toward one flat tone that runs from light to dark across it in the
    direction its surface faces. The strength sets how flat a plane is
    drawn and, unless `planes` gives a number, how many there are (12, 18
    or 26). `apart` is how far each plane is turned and moved: 1 not at
    all (each shows the subject where it is), 2 a little (the default),
    3 so that the subject is dislocated."""
    area = int(g.sum())
    if area < 0.002 * g.size:
        p.log.append("%s: too small to facet" % names)
        return
    size = math.sqrt(area)
    k = s - 1
    region = ndimage.binary_dilation(g, iterations=max(2, int(0.06 * size)))
    turn = np.zeros((p.h, p.w), np.float32)
    if p.normals is not None:
        for c in range(3):
            turn += np.abs(cv2.Sobel(p.normals[..., c], cv2.CV_32F, 1, 0)) + np.abs(cv2.Sobel(p.normals[..., c], cv2.CV_32F, 0, 1))
        turn = cv2.GaussianBlur(turn, (0, 0), 0.03 * size)
        turn = np.clip(turn / (np.percentile(turn[g], 90) + 1e-6), 0.0, 1.5)
    weight = (1.0 + 1.5 * turn) * region
    yy, xx = np.mgrid[0:p.h, 0:p.w].astype(np.float32)
    angles = FACET_ANGLES
    want = planes if planes > 0 else (12, 18, 26)[k]
    # How far a plane is turned (degrees) and moved (share of the subject's
    # size), at most. Tried up to 98 degrees and 0.49 (round 8): beyond
    # the third step the planes show parts of the picture that do not
    # belong where they are, and the subject is lost in a collage. Tied to
    # the strength as 6, 10 and 14 degrees it could not be told apart.
    most_turn, most_move = ((0.0, 0.0), (14.0, 0.07), (26.0, 0.13))[(apart or 2) - 1]
    if cuts == "form" and p.normals is not None:
        label, n = form_planes(p, region, turn, want, rng)
    else:
        cuts = "straight"
        label, n = straight_planes(p, region, weight, want, rng)
    idx = np.arange(1, n + 1)
    cy = np.concatenate([[0], ndimage.mean(yy, label, idx)]).astype(np.float32)
    cx = np.concatenate([[0], ndimage.mean(xx, label, idx)]).astype(np.float32)
    rad = np.concatenate([[1], np.sqrt(ndimage.sum(np.ones_like(yy), label, idx) / math.pi)]).astype(np.float32)
    ang = np.radians(rng.uniform(-1, 1, n + 1) * most_turn).astype(np.float32)
    scl = rng.uniform(0.90, 1.15, n + 1).astype(np.float32)
    shx = (rng.uniform(-1, 1, n + 1) * most_move * size).astype(np.float32)
    shy = (rng.uniform(-1, 1, n + 1) * most_move * size).astype(np.float32)
    ang[0], scl[0], shx[0], shy[0] = 0, 1, 0, 0
    dx, dy = xx - cx[label], yy - cy[label]
    ca, sa = np.cos(ang)[label], np.sin(ang)[label]
    qx = cx[label] + (ca * dx - sa * dy) / scl[label] + shx[label]
    qy = cy[label] + (sa * dx + ca * dy) / scl[label] + shy[label]
    seen = cv2.remap(p.lab, qx, qy, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)
    flat = np.zeros_like(seen)
    for c in range(3):
        flat[..., c] = np.concatenate([[0], ndimage.mean(seen[..., c], label, idx)])[label]
    # The way each plane faces, in the picture: its tone runs along it.
    if p.normals is not None:
        fx = np.concatenate([[0], ndimage.mean(p.normals[..., 0] / 127.5 - 1.0, label, idx)])
        fy = np.concatenate([[0], ndimage.mean(p.normals[..., 1] / 127.5 - 1.0, label, idx)])
    else:
        fx, fy = np.zeros(n + 1), np.zeros(n + 1)
    lean = np.hypot(fx, fy)
    rnd = np.radians(np.array(angles)[rng.integers(len(angles), size=n + 1)])
    fx = np.where(lean > 0.15, fx / np.maximum(lean, 1e-6), np.cos(rnd)).astype(np.float32)
    fy = np.where(lean > 0.15, fy / np.maximum(lean, 1e-6), np.sin(rnd)).astype(np.float32)
    flat[..., 0] += (10.0, 14.0, 18.0)[k] * np.clip((dx * fx[label] + dy * fy[label]) / rad[label], -1.2, 1.2)
    mix = ((0.38, 0.48, 0.58)[k] * region)[..., None]
    p.lab = (np.where(region[..., None], seen, p.lab) * (1.0 - mix) + flat * mix).astype(np.float32)
    # Where two planes meet, a dark line.
    edge = np.zeros((p.h, p.w), np.float32)
    edge[:, 1:] = (label[:, 1:] != label[:, :-1]) & (label[:, 1:] > 0) & (label[:, :-1] > 0)
    edge[1:, :] = np.maximum(edge[1:, :], (label[1:, :] != label[:-1, :]) & (label[1:, :] > 0) & (label[:-1, :] > 0))
    edge = cv2.GaussianBlur(edge, (0, 0), max(1.0, 0.0012 * p.long))
    p.lab[..., 0] -= 16.0 * np.clip(edge * 2.2, 0.0, 1.0)
    p.log.append("%s: faceted %d (%d planes, %s)" % (names, s, n, "cut along the form" if cuts == "form" else "straight cuts"))


# -------------------------------------------------------------------- run

def depth_of(p, i):
    d, q = 0, p.by_id[i].get("parent", 0)
    while q:
        d, q = d + 1, p.by_id[q].get("parent", 0)
    return d


def shadow_side(p, g):
    where = p.scene.get("light_from", "")
    if where == "left":
        return 1.0
    if where == "right":
        return -1.0
    return 1.0 if np.nonzero(g)[1].mean() < 0.5 * p.w else -1.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("photo")
    ap.add_argument("--choice", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scene", default="")
    ap.add_argument("--fill", choices=["lama", "plain"], default="lama")
    trial = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Broadstroke", "scene_trial", "models")
    ap.add_argument("--lama", default=os.path.join(trial, "lama", "big-lama.pt"))
    ap.add_argument("--hf-home", default=os.path.join(trial, "dense", "hf"))
    ap.add_argument("--tag", default="")
    ap.add_argument("--triangles", type=int, default=0,
                    help="for a solid of triangles: about how many (0: by the strength chosen)")
    ap.add_argument("--ragged", default="", help="for a solid of triangles: pairs of materials that meet raggedly, "
                                                 "such as skin:hair,metal:skin (first: by what the materials are)")
    ap.add_argument("--abrupt", default="", help="for a solid of triangles: pairs of materials that meet abruptly")
    ap.add_argument("--cuts", choices=["form", "straight"], default="form",
                    help="for facets: planes whose edges follow the form, or straight cuts that take no "
                         "notice of it (the first form)")
    ap.add_argument("--planes", type=int, default=0,
                    help="for facets: how many planes (0: as the choice file says, or else by the strength chosen)")
    ap.add_argument("--apart", type=int, choices=[0, 1, 2, 3], default=0,
                    help="for facets: how far each plane is turned and moved: 1 not at all, 2 a little, "
                         "3 so that the subject is dislocated (0: as the choice file says, or else 2)")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    with open(args.choice, encoding="utf-8") as f:
        choice = json.load(f)
    stem = os.path.splitext(args.photo)[0]
    base = os.path.basename(stem)
    p = Picture(args.photo, args.scene or stem + ".scene.json")
    rng = np.random.default_rng(args.seed)
    picks = [k for k in choice["picks"] if k["mechanism"] != "as it is" and k["id"] in p.by_id]
    # Pairs of materials stated to meet raggedly or abruptly: those of the
    # choice file, where the studio window writes them, and the command
    # line's, which come first where the two disagree.
    said_ragged, said_abrupt = facts.pairs(args.ragged), facts.pairs(args.abrupt)
    ragged = (facts.pairs(",".join(choice.get("ragged", []))) - said_abrupt) | said_ragged
    abrupt = (facts.pairs(",".join(choice.get("abrupt", []))) - said_ragged) | said_abrupt
    filler = Filler(args.fill, args.lama)
    if any(k["mechanism"] != "faceted" for k in picks):
        load_points(p, os.path.join(args.out, "geometry", base + ".npz"), args.hf_home)

    # A part is not changed again when the whole it belongs to is.
    taken = set()
    for k in sorted(picks, key=lambda k: depth_of(p, k["id"])):
        i, mech, s = k["id"], k["mechanism"], k["strength"]
        if i in taken:
            continue
        parent = p.by_id[i].get("parent", 0)
        if mech == "on stilts" and parent and not p.is_ground(parent) and parent not in taken:
            i = parent  # a trunk or a leg is stretched as part of what stands on it
        if mech in vocab.SHAPE_MECHANISMS:
            reshape(p, i, mech, s, filler, rng)
        elif mech == "repeated":
            repeat(p, i, s, rng)
        elif mech == "echoing":
            echo(p, i, s, rng)
        elif mech == "triangulated":
            triangulate(p, i, s, args.triangles, facts.ragged_table(p.scene["items"], ragged, abrupt), filler, rng)
        taken.update(p.group(i))
    chosen = [k for k in picks if k["mechanism"] == "faceted"]
    if chosen:
        g = np.zeros((p.h, p.w), bool)
        for k in chosen:
            g |= p.mask(k["id"])
        # The number of planes and how far apart they are: the command
        # line's, or the choice file's, where the studio window writes them.
        planes = args.planes or int(choice.get("planes", 0) or 0)
        apart = args.apart or int(choice.get("apart", 0) or 0)
        facet(p, g, max(k["strength"] for k in chosen), ", ".join(k["name"] for k in chosen), rng, args.cuts,
              max(0, planes), apart if apart in (1, 2, 3) else 0)
    casting = [k for k in picks if k["mechanism"] == "long shadow" and not p.is_ground(k["id"])]
    if casting:
        long_shadows(p, casting, max(k["strength"] for k in casting))

    os.makedirs(args.out, exist_ok=True)
    name = "%s_%s%s" % (base, choice["manner"], args.tag)
    Image.fromarray(p.rgb()).save(os.path.join(args.out, name + ".png"))
    Image.fromarray(p.items, mode="L").save(os.path.join(args.out, name + ".scene.items.png"))
    # Where the picture changed most: the middle of the detail on a sheet.
    diff = cv2.GaussianBlur(np.abs(p.lab - p.first).sum(axis=-1), (0, 0), 0.03 * p.long)
    fy, fx = np.unravel_index(int(np.argmax(diff)), diff.shape)
    scene = dict(p.scene)
    scene.update({"photo": name + ".png", "item_layer": name + ".scene.items.png", "manner": choice["manner"],
                  "title": choice["title"], "poem": choice["poem"], "carried_out": p.log,
                  "edited_by_hand": bool(choice.get("edited_by_hand")),
                  "focus": [float(fx) / p.w, float(fy) / p.h]})
    if p.normals is not None:
        Image.fromarray(np.clip(p.normals + 0.5, 0, 255).astype(np.uint8), mode="RGB").save(
            os.path.join(args.out, name + ".scene.normals.png"))
        scene["normal_layer"] = name + ".scene.normals.png"
    with open(os.path.join(args.out, name + ".scene.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(scene, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("%s: %s" % (name, "; ".join(p.log) if p.log else "nothing carried out"), flush=True)


if __name__ == "__main__":
    sys.exit(main())
