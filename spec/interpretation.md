This file covers: painting the subject in an artistic manner instead of as the photograph shows it. Index: [README.md](README.md).

### 2.14 Interpretation

**Status.** An investigation. Nothing of it is in the engine. A trial
outside the engine (`tools/interpret/`) has been run in oil only, with
three manners: surrealism, cubism and the metaphysical manner of de
Chirico. Surrealism is the manner being refined first. Cubism is kept
and is to be refined later; its first form is far from it. The mechanisms
listed below are avenues to investigate, not requirements; the ones that
were tried are marked. The undecided points are in spec 5.

**Problem**

The artists paint what the photograph shows. An artist also interprets:
a watch melts, a figure stands on legs too long for it, a rock hangs in
the air. Such a change is made to a thing because of what it is, and the
scene (spec 2.13) now says what each thing is.

**Requirements**

- The faithful artwork is the default. An interpretation is made only
  when the user asks for one. Without that the artwork is what the engine
  makes without this feature, byte for byte.
- The user chooses a manner (surrealism, cubism and so on). A model that
  runs on the local machine chooses, for each item of the scene, one
  mechanism of that manner and a strength, from fixed lists. Free text
  from a model (a title, a poem) may be kept as a caption and drives
  nothing.
- No model runs in the engine. An interpretation is prepared outside it
  and written as files, which can be corrected by hand: the choices, and
  the subject as the artist imagines it (a picture, its item layer, its
  orientation layer and a scene file). The engine paints the imagined
  subject like any photo, so the same files, settings and seed give the
  same bytes (spec 2.12).
- The rules of spec 1 and 2.1 hold. A mechanism changes what the artist
  intends to paint, or how the hand works. Nothing is drawn onto the
  canvas, and no pixel of the imagined subject reaches it.
- A mechanism does something to a thing: it changes its shape, its place,
  its size, how many of it there are, or what it is made of, and it is
  chosen because of what the thing is. A change of tone or color laid
  evenly over an item or over the picture is not a mechanism. Reason: in
  the first trial such changes (a mood's key and color; an item darkened,
  bleached, grayed or hazed) read as a filter over the photograph, and
  they showed every error of the item layer as a rim around the shape.

**What the trials showed** (oil, the five trial photographs; sheets in
`outputs/interpret_trial/sheets/` and `outputs/interpret_trial/round2/sheets/`)

- The list model of the observe step (spec 2.13) chooses sensibly from a
  fixed list and writes a usable caption. Told to change at most three
  things and to prefer the change that contradicts what the thing is, it
  picks the watch to melt, the tree to stand on stilts and the birds to be
  giant. Told nothing of the kind it wanted the hand to melt, and changed
  everything. For cubism and for shadows it chooses nearly every item.
  It does not keep to a limit it is told (three changes), and it picks
  things on which a mechanism cannot be seen, so the step that asks it
  tells it which mechanisms each item can take (from the item's size, its
  room in the picture and what it is), and refuses a choice outside them
  or beyond the limit. Its choices change from one wording of the request
  to the next.
- A change of shape, size or place works on a compact, well outlined
  thing and stays readable when painted: a melting watch, a giant pen, a
  tree on a trunk four times as long, giant birds, a rock hanging over the
  sea. What was behind the thing, painted in by a fill model, is not
  noticed in the painting.
- It fails quietly where the scene file is poor or there is no room: an
  item with few pixels, a thing cut off by the edge of the picture.
- A thing that is lifted must take with it what it carries, which the
  item layer does not say. Taking what lies within its outline at its own
  distance from the viewer (the 3D points of the scene) carries the man
  with his boat and the strap with the watch.
- Melting must drip at a spacing set by the thing's height. Set by its
  width, a boat came out wavy; set by its height, a rock runs into the
  sea in drips.
- A tree lifted with little room above it reads as a taller tree, not a
  floating one: stilts suit it better.
- Facets as cells around scattered points looked like a mosaic filter and
  were dropped. Facets from straight cuts read as a subject shattered into
  planes, in the painting too. They are not yet cubism: the cuts do not
  follow the form, and no plane shows another side of the subject.
- Shadows cast by following rays through the 3D points of the scene are
  not usable: one view gives the fronts of things only, so a figure's
  shadow comes out as a wedge or a ragged patch. What works is the
  figure's outline laid down along the ground from its foot, with the 3D
  points giving only the foot's place, the scale and the ground's
  direction: on an open beach the men, the boat and the birds throw long
  shadows of their own shape, and a lifted rock or boat has its shadow
  below it. Where the photograph has strong shadows of its own, or little
  open ground, the added ones are hardly seen.
- Six moods (serene, joyful, melancholy, eerie, dramatic, dreamlike) were
  tried first, as a key and color for the whole picture with a tone or
  color phrase per item. Not kept, for the reason given above. Dramatic,
  melancholy and eerie may return as modifiers of a manner.

**Mechanisms to investigate**

By what they act on. "Have" means a routine exists in the trial; it does
not mean the mechanism is accepted.

| Mechanism | Seen in | Suits | Needs |
| --- | --- | --- | --- |
| *Form of one thing* | | | |
| Soft, melting | Dali's soft watches | rigid made things: metal, glass | a warp (have; works on a watch) |
| Elongated supports | Dali's long-legged elephants; Modigliani's necks | legs, trunks, masts | a warp (have; works on a tree) |
| Inflated volume | Botero | figures, fruit, animals | a warp |
| Tubes and cylinders | Leger; Lempicka | limbs, trunks, folds | fitting simple solids |
| Angular facets | analytic cubism | the main subject | straight cuts (have; a first form); cuts that follow the form are not built |
| Stylized contour | art deco (stepped, streamlined); art nouveau (sinuous) | a clear silhouette | redrawing a contour |
| *Place, size and number* | | | |
| Levitation | Magritte, The Castle of the Pyrenees | heavy things: a rock, a boat | cut, fill, paste, a shadow below (have; works) |
| Wrong scale | Magritte, Personal Values | small familiar things | cut, fill, paste (have; works) |
| Rows of repeats | Magritte, Golconde | one figure or thing | cut, fill, paste |
| Echoes of motion | Balla, Dynamism of a Dog on a Leash | what moves: birds, people | pasting fading repeats |
| Arrangement by chance | Arp's dropped papers; Hoch's photomontage | several small things | pasting at seeded random places |
| Reduction | minimalism | all but the subject | removing and filling |
| *Substance and surface* | | | |
| Ornament in place of texture | Klimt's gold pattern; art deco sunburst, chevron, zigzag | fabric, sky, walls | a pattern generator |
| Flat field with a dark contour | cloisonnism; synthetic cubism | large simple shapes | on its own it is a change of tone, see the requirement above |
| *Space and light* | | | |
| Long raking shadows | de Chirico's squares | figures on open ground | the figure's outline laid down along the ground (have; works on open ground) |
| Several viewpoints at once | cubism | faces, objects | viewing a part from a shifted place; the hardest |
| Lines of force | Boccioni, Balla | moving subjects | a line generator |
| *Manner of the hand* | | | |
| Dabs of pure color | pointillism | the whole picture | the engine |
| Swirling strokes | Van Gogh | sky, foliage | the engine |
| Restricted earth palette | analytic cubism | the whole picture | the engine |

A mechanism of the last group is a change to an artist (spec 2.9) and is
approved like any change of the picture (spec 2.11).

**Building blocks** (the mechanisms above reduce to these)

1. Cut an item out and paint in what was behind it (have: a fill model).
2. Warp an item with a displacement field (have).
3. Move, scale, turn or repeat one instance of an item (have: move and
   scale).
4. Redraw a contour in straight runs or smooth curves and warp the item
   to it.
5. Split an item into facets by the orientation of its surface (have:
   straight cuts, placed where the surface turns most).
6. Generate ornament inside an item's shape.
7. Lay a figure's outline down along the ground as its shadow (have).
   The 3D points that the orientation model of spec 2.13 also estimates
   give the ground, its direction and the scale.
8. Show a part of an item from a shifted viewpoint.

**The trial's tools** (`tools/interpret/`, Python, outside the build;
[README](../tools/interpret/README.md))

| Part | Model | License |
| --- | --- | --- |
| The choice of a mechanism per item, the caption | Gemma 4 12B, as in spec 2.13 | Apache-2.0 |
| Painting in what was behind an item | LaMa (big-lama) | Apache-2.0 |
| The 3D shape of the scene | MoGe-2, as in spec 2.13 | MIT |

- In the trial a change of shape is not carried out on skin and hair, on
  the ground of a picture (sky, water, grass, sand), or on an item that
  covers more than 30 percent of it. Facets and shadows are applied to
  people. Whether people's shapes may be changed is undecided (spec 5).

**Acceptance.** None yet. The trial is judged by sheets that show the
painting of the photograph as it is beside the painting of the imagined
subject.
