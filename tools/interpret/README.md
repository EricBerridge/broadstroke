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

| Script | Does |
| --- | --- |
| `vocab.py` | The fixed words: the manners, and the mechanisms each allows |
| `facts.py` | Which mechanisms can be carried out on each item of a scene (its size, its room in the picture, what it is) |
| `choose.py` | Asks the list model of the observe step (through llama-server, answer constrained by a schema) for a title, a short poem, and one mechanism and strength per item. The model is told which mechanisms each item can take; a choice outside them, or beyond the manner's limit of changes, is refused and noted. Writes `<stem>.<manner>.choice.json`, which can be corrected by hand |
| `imagine.py` | Carries out a choice on the photo, the item layer and the orientation layer, and writes them with a scene file as a new subject |
| `sheet.py` | One sheet per manner: per photo the oil painting as it is, the imagined subject, its oil painting, and a detail of both. It expects the painting of the unchanged photo as `<stem>_asis.png` |

Mechanisms so far:

| Manner | Mechanism | How |
| --- | --- | --- |
| surrealism | melting | the item is warped downward, more toward its foot, in drips |
| surrealism | on stilts | its narrow lower part is stretched and the rest rises |
| surrealism | levitating | it is moved up with what it carries, and its outline is laid flat as a shadow where it stood |
| surrealism | giant | it is scaled up about its foot |
| cubism | faceted | the chosen items and a margin are cut by straight lines into a few planes; each plane shows the subject a little turned, moved and scaled, and is drawn toward a flat tone that runs across it the way its surface faces |
| metaphysical | long shadow | under a low sun, the outline of each instance of the chosen items laid down along the ground from its foot; the 3D points of the scene give the foot's place, the scale and the ground's direction |

A change of shape takes the item out of the picture (with what it
encloses and, if it is moved, with what lies within its outline at its own
distance: a man in a boat),
has a fill model paint in what was behind it (LaMa, `big-lama.pt`;
`--fill plain` uses OpenCV instead), and puts the item back in its new
shape. It is not carried out on skin and hair, on the ground of a picture
(sky, water, grass, sand), or on an item that covers more than 30 percent
of it.

Needs the Python environment of `tools/observe` plus `opencv-python`,
`tools/observe/observe.local.json`, and MoGe-2 for the 3D points of the
scene (kept in `<out>/geometry/`).
