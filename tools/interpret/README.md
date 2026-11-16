# Interpret (trial)

The trial of spec 2.14: painting the subject in an artistic manner instead
of as the photograph shows it. Nothing in the engine depends on it. The
user picks a manner; a local model chooses one mechanism per item of the
photo's scene; a routine per mechanism turns the photo into the subject the
artist has in mind; the engine paints that subject like any photo. Tried in
oil only.

```powershell
python choose.py --out choices --manners surrealism photo.jpg
python imagine.py --choice choices\photo.surrealism.choice.json --out subjects photo.jpg
broadstroke paint subjects\photo_surrealism.png -m oil
python sheet.py --manners surrealism --subjects subjects --oil oil --out sheets photo
```

The studio window runs the first two commands for the photo that is open
there (a manner and "Imagine", spec 2.10) and paints the result. It keeps
the files with the photo's working files, not beside the photo, in
`interpret\choices\` and `interpret\subjects\` (it passes `--scene` and
`--name`), shows the choice per item and saves changes to the choice
file, with the pairs of materials stated ragged or abrupt in its lists
`"ragged"` and `"abrupt"` and cubism's settings as `"planes"`, `"apart"`,
`"flat"`, `"cuts"` and `"seed"`. `--triangles`, `--fill` and
`--seed` are not offered there.

Erasing (spec 2.13): `imagine.py` first takes out every item the scene
file marks `"erase": true`, with its parts, and paints in what was behind
it. An item that covers more than 10 percent of the picture is left in.
Without `--choice` it does nothing else:

```powershell
python imagine.py --out erased photo.jpg
broadstroke paint erased\photo_erased.png -m oil
```

The subject's scene file no longer has the erased items; its `"erased"`
and `"not_erased"` list the ids of the marked items that are gone and
that were left in. The studio window's "Erase" runs this for the open
photo and keeps the files in `erased\` among the working files.
`choose.py` does not ask the model about a marked item.

## Running a round

A round tries something on the five trial photos and ends in one sheet
per manner. The photos and their scene files are in
`outputs/scene_trial/photos_v2/` (not in git); results go under
`outputs/interpret_trial/round<N>/`.

1. `choose.py --manners <manner> --out round<N>/choices <photos>` asks the
   model. To try something the model did not choose, copy a
   `.choice.json`, change `mechanism` and `strength` of an item, set
   `"edited_by_hand": true`, and carry it out with a `--tag`.
2. `imagine.py --choice <file> --out round<N>/subjects [--tag _x] <photo>`
   once per choice. For the solid of triangles: `--triangles N`,
   `--ragged a:b`, `--abrupt a:b` (pairs of treatments; `none` for what
   has no item). The same pairs can stand in the choice file as
   `"ragged": ["a:b"]` and `"abrupt": ["a:b"]`; the command line comes
   first where the two disagree. For facets: `--planes N` (unless
   stated, 12, 18 or 26 by the strength) and `--apart 1..3` (how far
   each plane is turned and moved: not at all, a little, so that the
   subject is dislocated; 2 unless stated) and `--flat 1..3` (how far
   each plane is drawn toward one flat tone; 1 unless stated), which can
   also stand in the choice file as `"planes"`, `"apart"` and `"flat"`;
   `--cuts lines` cuts along the picture's own lines and
   `--cuts straight` gives the first form (`"cuts"` in the choice file);
   `--seed N` gives another arrangement (`"seed"`).
3. `broadstroke paint round<N>/subjects/<name>.png -m oil -o round<N>/oil/<name>.png`
   for each subject (about a minute each; oil uses one core, so several
   can run at once). The sheet also needs the painting of each unchanged
   photo as `round<N>/oil/<stem>_asis.png`; they can be copied from an
   earlier round.
4. `sheet.py --manners <manner> --tags ,_x --subjects round<N>/subjects
   --oil round<N>/oil --out round<N>/sheets <stems>`.

The latest rounds: round 3 (surrealism, futurism), round 6 (the solid of
triangles), round 7 (cubism: cuts along the form, tagged `_form`, beside
the first form), round 8 (cubism: the number of planes, and how far
apart they are over a wide range), round 9 (cubism: how flat the planes
are drawn), round 10 (cubism: another arrangement by `--seed N`, and
`--cuts lines` for cuts along the picture's own lines, both kept; planes
seen from another side, dropped).

## Scripts

| Script | Does |
| --- | --- |
| `vocab.py` | The fixed words: the manners, and the mechanisms each allows |
| `facts.py` | Which mechanisms can be carried out on each item of a scene, and how well each would show (0 to 1), from the item's largest instance: its shape, whether it is cut off, its size, its room in the picture, what it is |
| `choose.py` | Asks the list model of the observe step (through llama-server, answer constrained by a schema) for a title, a short poem, and one mechanism and strength per item. The model is told which mechanisms each item can take, the one that would show best first; a choice outside them is refused and noted, and beyond the manner's limit of changes the ones that would show best are kept. Writes `<stem>.<manner>.choice.json`, which can be corrected by hand |
| `imagine.py` | Erases the items the scene file marks, carries out a choice (if one is given) on the photo, the item layer and the orientation layer, and writes them with a scene file as a new subject |
| `sheet.py` | One sheet per manner: per photo the oil painting as it is, the imagined subject, its oil painting, and a detail of both. It expects the painting of the unchanged photo as `<stem>_asis.png` |

Mechanisms so far:

| Manner | Mechanism | How |
| --- | --- | --- |
| surrealism | melting | the item is warped downward, more toward its foot, in drips. Offered only for a thing whose lower outline lies against another thing |
| surrealism | on stilts | its narrow lower part is stretched and the rest rises |
| surrealism | levitating | it is moved up with what it carries, and its outline is laid flat as a shadow where it stood. Only where it will be seen against a background like the one it has |
| surrealism | giant | it is scaled up about its foot |
| surrealism | repeated | its largest instance is pasted, at three sizes, in staggered rows over the open space of the picture (ground or nothing, no other thing), only at places with a background like the one it has |
| futurism | echoing | its largest instance trails fading repeats of itself toward the nearer side of the picture |
| cubism | faceted | the chosen items and a margin are cut into a few planes: along the form (the default: a mesh of triangles, finer where the surface turns, joined into planes by the way the triangles face and their color, each plane's outline then drawn in straight runs), or by straight lines (`--cuts straight`, the first form). Each plane shows the subject a little turned, moved and scaled, and is drawn toward a flat tone that runs across it the way its surface faces |
| polygonal | triangulated | the item is replaced by a solid of triangles: points along its outline and inside it (closer where its surface turns), joined into triangles; each triangle's corners placed in space from the 3D points, which gives its normal, stated in the orientation layer for the whole triangle; each triangle given one color, the median of the picture's colors within it. The strength sets the size of the triangles, `--triangles N` their number. Where two materials meet raggedly (`facts.ragged_table`; tuned with `--ragged a:b` and `--abrupt a:b`, pairs of treatments) the triangles are finer, go on over a margin, and each is the thing's or the surroundings' by its color; where they meet abruptly the corners lie on the outline. A part made of thin things (trunks, branches) is left as the picture shows it |
| metaphysical | long shadow | under a low sun, the outline of each instance of the chosen items laid down along the ground from its foot; the 3D points of the scene give the foot's place, the scale and the ground's direction |

A change of shape takes the item out of the picture (with what it
encloses and, if it is moved, with what lies within its outline at its own
distance: a man in a boat),
has a fill model paint in what was behind it (LaMa, `big-lama.pt`;
`--fill plain` uses OpenCV instead), gives the filled place the fine
variation of the ground around it, and puts the item back in its new
shape. It is not carried out on skin and hair, on the ground of a picture
(sky, water, grass, sand), or on an item that covers more than 30 percent
of it.

The fill model is the LaMa TorchScript file `big-lama.pt` (Apache-2.0),
from the release of the `simple-lama-inpainting` project on GitHub, kept
in `%LOCALAPPDATA%\Broadstroke\scene_trial\models\lama\` (`--lama`
names another place).

Needs the Python environment of `tools/observe` plus `opencv-python`,
`tools/observe/observe.local.json`, and MoGe-2 for the 3D points of the
scene (kept in `<out>/geometry/`).
