"""Interpretation trial (spec 2.14): what can be done to each item of a
scene, from its item layer alone. Used to tell the model which mechanisms
an item may be given, and to refuse the others. Nothing here is part of
the engine."""
import numpy as np
from scipy import ndimage

import vocab

# Items that are the ground of a picture rather than a thing in it: their
# shape is not changed, they cast no shadow and nothing rides on them.
GROUND = {"sky", "cloud", "still_water", "moving_water", "grass", "earth_or_sand", "foam_or_spray"}
MAX_SHAPE_SHARE = 0.30   # the shape of an item that covers more of the picture is not changed
MIN_SHARE = 0.0005       # nor is anything done to an item smaller than this
ROOM = 0.3               # to rise or grow, an item needs this share of its height free above it


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


def allowed(scene, layer):
    """For each item id, the mechanisms that can be carried out on it."""
    h, w = layer.shape
    out = {}
    for it in scene["items"]:
        mask = np.isin(layer, group(scene, it["id"]))
        share = float(mask.mean())
        kind = it.get("treatment")
        can = []
        if share >= MIN_SHARE and kind not in GROUND:
            can += ["faceted", "long shadow"]
            if kind not in vocab.NO_SHAPE_TREATMENTS and share <= MAX_SHAPE_SHARE:
                parts, n = ndimage.label(mask)
                sizes = np.bincount(parts.ravel())
                sizes[0] = 0
                ys = np.nonzero(parts == int(np.argmax(sizes)))[0]
                tall = max(1, ys.max() - ys.min())
                if ys.max() < 0.97 * h or tall < 0.5 * h:  # not a large thing cut off at its foot
                    can.append("melting")
                if (ys.min() - 0.02 * h) / tall >= ROOM:
                    can += ["on stilts", "levitating", "giant"]
        out[it["id"]] = can
    return out
