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
    p.set_rgb(filler.fill(p.rgb(), hole))  # what was behind it
    p.items = p.items[outside[0], outside[1]]
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


# ---------------------------------------------------------------- facets

def facet(p, g, s, names, rng):
    """Breaks the pixels g into a few large flat planes with straight
    edges. The region (g and a margin around it, so the planes slide into
    the surroundings) is cut again and again by a straight line at one of
    a few angles; the piece cut next is the one whose surface turns most
    (orientation layer). Each plane shows the subject a little turned,
    moved and scaled, as if seen from a place of its own, and is drawn
    toward one flat tone that runs from light to dark across it in the
    direction its surface faces."""
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
    label = region.astype(np.int32)
    angles = (90.0, 0.0, 35.0, -35.0, 62.0, -62.0)
    n, want, tries = 1, (12, 18, 26)[k], 0
    while n < want and tries < 400:
        tries += 1
        mass = ndimage.sum(weight, label, np.arange(1, n + 1))
        cell = int(np.argmax(mass * rng.uniform(0.85, 1.0, n))) + 1
        ys, xs = np.nonzero(label == cell)
        j = int(rng.integers(len(ys)))
        py, px = 0.6 * ys.mean() + 0.4 * ys[j], 0.6 * xs.mean() + 0.4 * xs[j]
        a = math.radians(angles[int(rng.integers(len(angles)))] + rng.normal(0.0, 6.0))
        far = (xs - px) * -math.sin(a) + (ys - py) * math.cos(a) > 0
        if not 0.18 < far.mean() < 0.82:
            continue
        n += 1
        label[ys[far], xs[far]] = n
    idx = np.arange(1, n + 1)
    cy = np.concatenate([[0], ndimage.mean(yy, label, idx)]).astype(np.float32)
    cx = np.concatenate([[0], ndimage.mean(xx, label, idx)]).astype(np.float32)
    rad = np.concatenate([[1], np.sqrt(ndimage.sum(np.ones_like(yy), label, idx) / math.pi)]).astype(np.float32)
    ang = np.radians(rng.uniform(-1, 1, n + 1) * (6.0, 10.0, 14.0)[k]).astype(np.float32)
    scl = rng.uniform(0.90, 1.15, n + 1).astype(np.float32)
    shx = (rng.uniform(-1, 1, n + 1) * (0.03, 0.05, 0.07)[k] * size).astype(np.float32)
    shy = (rng.uniform(-1, 1, n + 1) * (0.03, 0.05, 0.07)[k] * size).astype(np.float32)
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
    p.log.append("%s: faceted %d (%d planes)" % (names, s, n))


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
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    with open(args.choice, encoding="utf-8") as f:
        choice = json.load(f)
    stem = os.path.splitext(args.photo)[0]
    base = os.path.basename(stem)
    p = Picture(args.photo, args.scene or stem + ".scene.json")
    rng = np.random.default_rng(args.seed)
    picks = [k for k in choice["picks"] if k["mechanism"] != "as it is" and k["id"] in p.by_id]
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
        taken.update(p.group(i))
    chosen = [k for k in picks if k["mechanism"] == "faceted"]
    if chosen:
        g = np.zeros((p.h, p.w), bool)
        for k in chosen:
            g |= p.mask(k["id"])
        facet(p, g, max(k["strength"] for k in chosen), ", ".join(k["name"] for k in chosen), rng)
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
