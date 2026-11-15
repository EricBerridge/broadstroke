"""Interpretation trial (spec 2.14): what can be done to each item of a
scene, and how well it would show, from its item layer and the photo. Used
to tell the model which mechanisms an item may be given, best first, and
to refuse the others. Nothing here is part of the engine."""
import cv2
import numpy as np
from scipy import ndimage

import vocab

# Items that are the ground of a picture rather than a thing in it: their
# shape is not changed, they cast no shadow and nothing rides on them.
GROUND = {"sky", "cloud", "still_water", "moving_water", "grass", "earth_or_sand", "foam_or_spray"}
# Materials whose outline is ragged by nature, whatever they meet: leaves,
# twigs, blades, hair, fur, spray, cloud, rock.
RAGGED = {"foliage_mass", "trunk_or_branches", "grass", "flowers", "hair", "fur_or_feathers", "foam_or_spray",
          "cloud", "rock"}
# Materials that are thin things: narrower than a triangle of a solid of
# triangles, and outlined together with what is seen between them.
THIN = {"trunk_or_branches"}
MAX_SHAPE_SHARE = 0.30   # the shape of an item that covers more of the picture is not changed
MIN_SHARE = 0.0005       # nor is anything done to an item smaller than this
SHOWS = 0.35             # a mechanism is offered for an item if it would show at least this well (0..1)
ALIKE = 20.0             # two backgrounds are alike if their mean colors differ by less (CIELAB)


def group(scene, i):
    """The item and everything that is a part of it."""
    ids, grew = {i}, True
    while grew:
        grew = False
        for it in scene["items"]:
            if it.get("parent", 0) in ids and it["id"] not in ids:
                ids.add(it["id"])
                grew = True
    return sorted(ids)


def pairs(text):
    """Pairs of materials from text such as "skin:hair,metal:skin"."""
    return {frozenset(part.strip().split(":")) for part in text.split(",") if ":" in part}


def ragged_table(items, ragged=(), abrupt=()):
    """Where two items meet, is the boundary ragged or abrupt? Decided by
    the two materials (treatments) that meet there, not once for a whole
    thing: ragged if either material is ragged by nature (RAGGED), or if
    the scene file says the edges of both items are soft or lost. `ragged`
    and `abrupt` are pairs of materials that are stated to be the one or
    the other and come first, so a face can run raggedly into its hair
    and stay sharp against a collar. Returns a table indexed by two item
    ids (0: no item). Measured from the outline's shape, or from how the
    two sides' colors interleave along it, a watch came out as ragged as a
    tree; what the things are tells them apart."""
    kind, soft = {0: None}, {0: False}
    for it in items:
        kind[it["id"]] = it.get("treatment")
        soft[it["id"]] = it.get("edges") in ("soft", "lost")
    table = np.zeros((256, 256), bool)
    for a in kind:
        for b in kind:
            if a == b:
                continue
            pair = frozenset((str(kind[a]), str(kind[b])))
            is_ragged = kind[a] in RAGGED or kind[b] in RAGGED or (soft[a] and soft[b])
            if pair in abrupt:
                is_ragged = False
            if pair in ragged:
                is_ragged = True
            table[a, b] = is_ragged
    return table


def ramp(x, lo, hi):
    return float(np.clip((x - lo) / (hi - lo), 0.0, 1.0))


def largest(mask):
    parts, n = ndimage.label(mask)
    sizes = np.bincount(parts.ravel())
    sizes[0] = 0
    return parts == int(np.argmax(sizes))


def around(one):
    """The pixels just outside an instance: what it is seen against."""
    ys, xs = np.nonzero(one)
    r = max(3, int(0.04 * max(ys.max() - ys.min(), xs.max() - xs.min())))
    return ndimage.binary_dilation(one, iterations=r) & ~one


def alike(lab, one, there):
    """Whether an instance, put where `there` says, would be seen against
    a background like the one it has now. Returns how far the two mean
    colors are apart (CIELAB); alike if below ALIKE. A thing is cut out
    with a rim of what was behind it, which shows against another
    background."""
    new = there & ~one
    if not new.any():
        return 0.0
    return float(np.linalg.norm(lab[around(one)].mean(axis=0) - lab[new].mean(axis=0)))


def rests_on(scene, layer, i, one):
    """How far the lower half of an instance of item i lies against
    another thing that is not background and not small beside it: the
    share of its lower outline, 0 to 1. A thing that sits on or against another can run down
    over its edge."""
    own = set(group(scene, i))
    by_id = {it["id"]: it for it in scene["items"]}
    q = by_id[i].get("parent", 0)
    while q:  # a part does not rest on its whole
        own.add(q)
        q = by_id[q].get("parent", 0)
    ys = np.nonzero(one)[0]
    ring = around(one)
    ring[:int(np.median(ys))] = False
    ids = layer[ring]
    if len(ids) == 0:
        return 0.0
    area = float(one.sum())
    under = 0
    for j in np.unique(ids):
        if j and j not in own and by_id[j].get("treatment") not in GROUND and by_id[j]["importance"] >= 1                 and (layer == j).sum() >= 0.5 * area:
            under += int((ids == j).sum())
    return under / float(len(ids))


def measure(mask):
    """What matters of an item's largest instance for how a change of it
    will show: its share of the picture, the room above it (in its own
    heights), how much of its outline lies on the picture's edge, how
    solid its shape is, and how narrow its foot is."""
    h, w = mask.shape
    one = largest(mask)
    ys, xs = np.nonzero(one)
    tall, wide = max(1, ys.max() - ys.min()), max(1, xs.max() - xs.min())
    rim = one & ~ndimage.binary_erosion(one, iterations=2, border_value=0)
    ry, rx = np.nonzero(rim)
    at_edge = (ry < 3) | (ry >= h - 3) | (rx < 3) | (rx >= w - 3)
    hull = np.zeros(one.shape, np.uint8)
    cv2.fillConvexPoly(hull, cv2.convexHull(cv2.findNonZero(one.astype(np.uint8))), 1)
    rows = np.bincount(ys, minlength=h).astype(np.float32)
    low = rows[int(ys.max() - 0.1 * tall):ys.max() + 1]
    foot_cut = float((one[h - 3:].any(axis=0)).sum()) / wide  # how much of its width runs out at the bottom
    return {"share": float(one.mean()), "tall": tall / h, "room": (ys.min() - 0.02 * h) / tall,
            "edge": float(at_edge.mean()), "solid": float(one.sum()) / max(1, int(hull.sum())),
            "foot": float(low.mean() / max(1.0, rows.max())), "foot_cut": foot_cut}


def shows(m):
    """For each change of shape, place, size or number: how well it would
    show on an instance with the measures m, 0 to 1."""
    solid = ramp(m["solid"], 0.3, 0.6)                 # a clear shape of its own
    whole = 1.0 - ramp(m["edge"], 0.10, 0.35)          # not cut off by the picture's edge
    stands = 1.0 - ramp(m["foot_cut"], 0.15, 0.5)      # its foot is in the picture
    seen = ramp(m["share"], 0.0015, 0.006)             # large enough to be seen as it is
    return {
        # Only for a thing that sits on or against another thing, so that
        # it can run down over that thing's edge.
        "melting": solid * seen * ramp(m.get("rests", 0.0), 0.25, 0.5),
        # It must rise clear of where it stood to read as floating, and
        # be seen there against a background like the one it had.
        "levitating": solid * whole * stands * seen * ramp(m["room"], 0.45, 0.9) * (1.0 if m.get("lifts", True) else 0.0),
        # It needs something narrow to stand on, and some room to rise.
        "on stilts": solid * whole * stands * seen * ramp(m["room"], 0.1, 0.35) * (1.0 if m["foot"] <= 0.5 else 0.45),
        # For small things, with room to grow.
        "giant": solid * whole * stands * ramp(m["room"], 0.5, 1.2) * (1.0 - ramp(m["share"], 0.02, 0.06)),
        # For a small thing that stands alone.
        "repeated": solid * whole * stands * ramp(m["share"], 0.0001, 0.0003) * (1.0 - ramp(m["share"], 0.015, 0.04)),
        # Its repeats trail beside it.
        "echoing": solid * whole * ramp(m["share"], 0.0005, 0.003) * (1.0 - ramp(m["share"], 0.10, 0.20)),
    }


def allowed(scene, layer, lab=None):
    """For each item id, the mechanisms that can be carried out on it, each
    with how well it would show (0..1), best first. `lab` is the photo in
    CIELAB at the layer's size; without it backgrounds are not compared."""
    out = {}
    for it in scene["items"]:
        mask = np.isin(layer, group(scene, it["id"]))
        share = float(mask.mean())
        kind = it.get("treatment")
        can = {}
        if share >= MIN_SHARE and kind not in GROUND:
            can = {"faceted": 1.0, "long shadow": 1.0}
            form = measure(mask)
            well = ramp(form["share"], 0.005, 0.02) * ramp(form["solid"], 0.3, 0.6)
            if well >= SHOWS:
                can["triangulated"] = well  # large enough for its triangles to be seen
            if kind not in vocab.NO_SHAPE_TREATMENTS and share <= MAX_SHAPE_SHARE:
                m = measure(mask)
                one = largest(mask)
                m["rests"] = rests_on(scene, layer, it["id"], one)
                if lab is not None:
                    ys = np.nonzero(one)[0]
                    rise = int(min(0.75 * (ys.max() - ys.min()), max(0.0, ys.min() - 0.02 * layer.shape[0])))
                    lifted = np.zeros_like(one)
                    if rise > 0:
                        lifted[:-rise] = one[rise:]
                    m["lifts"] = alike(lab, one, lifted) < ALIKE
                can.update({k: v for k, v in shows(m).items() if v >= SHOWS})
        out[it["id"]] = dict(sorted(can.items(), key=lambda kv: -kv[1]))
    return out
