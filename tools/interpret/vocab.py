"""Interpretation trial (spec 2.14): the fixed words. The user chooses a
manner; the model chooses, for each item, one mechanism of that manner
from this list only. Nothing here is part of the engine."""

# mechanism: what the model is told about it
MECHANISMS = {
    "as it is":    "leave it as the photograph shows it",
    "melting":     "it sags and runs downward like wax, over the edge of what it sits on. For hard, made things that "
                   "sit on or against another thing: a watch on a wrist, a machine on a boat, a clock on a table",
    "on stilts":   "what holds it up is stretched very long, so it stands high above the ground. For things on legs, "
                   "trunks, posts or masts: an animal, a bird, a tree, a person",
    "levitating":  "it hangs in the air above the place where it stood, and its shadow lies on the ground below. "
                   "For heavy things that could never float: a rock, a boat, a house, a car",
    "giant":       "it is far larger than it should be. For small familiar things: a bird, a pen, a fruit, a cup",
    "repeated":    "it appears many times over, near and far, in rows across the open space of the picture. "
                   "For one small thing that stands alone: a man, a bird, an apple",
    "echoing":     "it is followed by fading repeats of itself, as if seen at several moments while it moves. "
                   "For things that move or could move: a bird, a person, a boat, a car",
    "faceted":     "it is broken into flat angular planes that follow the turn of its surfaces, and the planes are "
                   "shifted against each other",
    "long shadow": "a low sun throws its long shadow across the ground",
    "triangulated": "it is rebuilt as a solid of flat triangles, each painted in one color, like a computer model "
                   "of itself",
}

# manner: (what the model is told, the mechanisms it may choose, how to choose)
MANNERS = {
    "surrealism": (
        "the manner of Dali and Magritte: an ordinary world, painted soberly, in which a few things behave as in a "
        "dream. What is hard goes soft, what is heavy floats, what is small is huge",
        ["melting", "on stilts", "levitating", "giant", "repeated"],
        "Change one, two or at most three things and leave all the rest as it is: the change is only seen against "
        "an ordinary world. Choose the things for which the change is strangest, because it contradicts what the "
        "thing is. Do not change the sky, the ground or the water. Use different mechanisms for different things."),
    "cubism": (
        "the manner of Picasso and Braque around 1910: the subject is taken apart into flat angular planes seen "
        "from slightly different sides, and the planes slide into the space around it",
        ["faceted"],
        "Facet the main subject and its parts, and the things that touch it if the picture needs it. Leave the far "
        "background, the sky and the ground as they are."),
    "futurism": (
        "the manner of Balla and Boccioni: speed and movement. A thing that moves is shown at several moments at "
        "once, trailing fading repeats of itself",
        ["echoing"],
        "Choose one, two or at most three things that move or could move, and leave all the rest as it is. Do not "
        "choose the sky, the ground or the water."),
    "polygonal": (
        "a computer model set into a painting: one thing is rebuilt as a solid of flat triangles, each painted "
        "in one color, and everything else is painted as it is",
        ["triangulated"],
        "Choose exactly one thing: the main subject, or a compact thing with a clear form of its own. The strength "
        "says how fine the triangles are: 1 coarse, 2 medium, 3 fine. Leave all the rest as it is."),
    "metaphysical": (
        "the manner of de Chirico: a still, empty place under a low sun, where every figure throws a long shadow "
        "across the ground",
        ["long shadow"],
        "Choose every thing that stands up from the ground and can throw a shadow: figures, animals, trees, boats, "
        "posts, rocks, objects on a table. Leave the ground itself, the sky and the water as they are."),
}

# The shapes of people are left alone: these treatments take no change of
# shape. Facets and shadows are not changes of shape in this sense.
NO_SHAPE_TREATMENTS = {"skin", "hair"}
# The most items a manner changes; the model is told, and held to it.
MOST = {"surrealism": 3, "futurism": 3, "polygonal": 1}
SHAPE_MECHANISMS = {"melting", "on stilts", "levitating", "giant"}
