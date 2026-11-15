This file covers: painting the subject in an artistic manner instead of as the photograph shows it. Index: [README.md](README.md).

### 2.14 Interpretation

**Status.** An investigation. Nothing of it is in the engine. A trial
outside the engine (`tools/interpret/`) has been run in oil only, with
five manners: surrealism, cubism, futurism, the metaphysical manner of
de Chirico, and the solid of triangles. The studio window can start the
trial for the open photo, show and change its choices, and paint the
imagined subject in any medium (spec 2.10); the owner allowed every
medium there, to be judged as results come (2026-11-15). The owner's verdicts
(2026-11-14): some mechanisms of surrealism are worth keeping and can be
used in targeted ways, and it is not refined further for now; cubism is
kept and is to be refined later, its first form being far from it; the
solid of triangles is the form being worked on. The mechanisms listed
below are avenues to investigate, not requirements; the ones that were
tried are marked. The undecided points are in spec 5.

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

**Rules the owner set for mechanisms**

- Melting is only for a thing that sits on or against another thing, so
  that it can run down over that thing's edge: a watch on a wrist.
  Reason: a thing that melts where it stands (a boat on a beach) did not
  look good.
- A thing is put in a new place only where it will be seen against a
  background like the one it has. Reason: a thing is cut out with a rim
  of what was behind it, and against another background the rim shows
  (birds from a dark lawn, repeated in a bright sky). The owner chose
  this rule over mending the cut-out. It applies to repeats and to
  lifting (the owner confirmed lifting); two backgrounds are alike if
  their mean colors are less than 20 apart in CIELAB. It refuses the rock
  lifted over the sea and the watch lifted off the wrist, which earlier
  rounds showed.

**What the trials showed** (oil, the five trial photographs; sheets in
`outputs/interpret_trial/sheets/`, `round2/sheets/` and `round3/sheets/` there)

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
- Which mechanisms an item is offered is worked out from its largest
  instance: whether it has a clear shape of its own, is cut off by the
  picture's edge, stands with its foot in the picture, is large enough to
  be seen, and has room above it to rise or grow. Each mechanism weighs
  these its own way, and one that would show poorly is not offered. With
  this the model no longer lifts a tree that has no room or melts a shirt
  that runs out of the picture. It cannot catch an error of the scene
  file: an item named "person in red" that outlines only an arm is lifted
  as an arm.
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
  sea in drips and the crown of a tree hangs in lobes. On a long low
  thing the even drips read as a zigzag. Melting is now kept to things
  that sit on or against another (the owner's rule above).
- A thing repeated over the open space of the picture works as a routine
  but does not read yet: repeats of a small bird are specks, and in a
  sky they carried the dark of the lawn they were cut from (the owner's
  rule above now keeps them to a like background).
- Fading repeats trailing a thing read as movement on a clear, compact
  thing (a row of watches along an arm, a boat, men) and as a double
  exposure on a large one (a tree).
- Giving a large filled place the fine variation of the ground around it
  helps little: under a lifted rock the place is still told by its
  smoothness. A better fill is needed for large things.
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
| Soft, melting | Dali's soft watches | rigid made things that sit on or against another thing | a warp (have; works on a watch) |
| Elongated supports | Dali's long-legged elephants; Modigliani's necks | legs, trunks, masts | a warp (have; works on a tree) |
| Inflated volume | Botero | figures, fruit, animals | a warp |
| Tubes and cylinders | Leger; Lempicka | limbs, trunks, folds | fitting simple solids |
| Angular facets | analytic cubism | the main subject | straight cuts (have; a first form); cuts that follow the form are not built |
| Stylized contour | art deco (stepped, streamlined); art nouveau (sinuous) | a clear silhouette | redrawing a contour |
| Solid of triangles | computer models of things; see below | one thing with a clear form | a mesh from the 3D points (have; works) |
| *Place, size and number* | | | |
| Levitation | Magritte, The Castle of the Pyrenees | heavy things: a rock, a boat | cut, fill, paste, a shadow below (have; works) |
| Wrong scale | Magritte, Personal Values | small familiar things | cut, fill, paste (have; works) |
| Rows of repeats | Magritte, Golconde | one figure or thing | pasting repeats (have; does not read yet) |
| Echoes of motion | Balla, Dynamism of a Dog on a Leash | what moves: birds, people | pasting fading repeats (have; works on a compact thing) |
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

**The solid of triangles** (the owner's idea, 2026-11-14; tried in oil)

A thing in the photograph is replaced by a model of itself built of
triangles, where technology meets painting. The owner's decisions:

- The thing's surface is covered with triangles from its 3D shape. How
  fine they are can be set.
- Each triangle is painted in one color, a representative one of the part
  of the picture that falls on it.
- It must look painted, not filled by an algorithm: the brushwork within
  a triangle is the painter's.
- How the solid ends is decided where two materials meet, not once for
  the whole thing, and can be tuned by pair of materials. A watch ends
  abruptly; a tree runs raggedly into the sky; a face may run raggedly
  into its hair and stay sharp against clothing and background. Reason:
  with one abrupt outline for every thing, a tree's solid was blocky and
  the tree itself showed past it, and a rock's edge against the foam was
  blocky.
- No wireframe. Reason: tried as thin black lines along the edges, it did
  not look good. Each triangle keeping the picture's detail, relit as one
  plane from a light direction fitted to the photo, was tried with it and
  was not what the owner wanted either: on a smooth surface it read as a
  net laid over the thing.

How the trial does it (a manner of its own, "polygonal"; the model
chooses one thing):

- The mesh (building block 9). Points are put along the thing's outline
  and inside it, closer together where its surface turns more
  (orientation layer), and joined into triangles. How fine: the sides of
  the triangles are about 6, 4 or 2.6 percent of the picture's long side
  by the strength chosen, or a number of triangles is given. Each corner
  takes its place in space from the 3D points of the scene, a little
  inside the outline, where depth does not jump. One view gives the side
  that is seen, which is all a picture shows.
- Each triangle's color is the middle one (the median) of the picture's
  colors within it.
- Ragged or abrupt, for every stretch of boundary, from the two items
  that meet there. Ragged if either material (treatment) is ragged by
  nature (masses of leaves, branches, grass, flowers, hair, fur, foam,
  cloud, rock), or if the scene file says the edges of both items are
  soft or lost; a pair of materials can be stated to be the one or the
  other (`--ragged skin:hair`, `--abrupt skin:hair`), and that comes
  first. Measuring raggedness was tried and not kept: by the shape of the
  outline, and by how the colors of the two sides interleave along it, a
  watch came out as ragged as a tree.
- Along an abrupt stretch the corners lie on the outline, and what the
  triangles leave of the thing is painted in as background.
- Along a ragged stretch the thing is not cut out. The triangles are
  finer there (sides 0.4 of the others') and go on over a margin beyond
  the outline (0.7 of a full side). The triangles are finer too where
  two parts of the thing meet raggedly.
- Each small triangle at a ragged stretch is the thing's or the
  surroundings' by a vote on its color. Each side's share of pixels of
  about that color (within 15 in CIELAB), within 0.8 of a full side, is
  counted; the thing's pixels that vote lie well inside it (0.35 of a
  full side), because at its rim the item layer is the least to be
  trusted. The thing's share counts for less the farther the triangle
  lies beyond the outline, and the surroundings' the deeper it lies
  inside (halved about every 0.17 of a full side). The triangle is the
  thing's if the thing is clearly ahead. A color found on neither side
  is usually the middle color of a triangle that is half the thing and
  half not; it is the thing's only if it lies well inside it.
- Only the thing's triangles are painted flat. The others stay as the
  picture shows them.
- Bits of the thing that show past its solid against the sky (leaves at
  the top of a crown, against clouds) are painted over with the sky's
  color there. A pixel is such a bit if it lies within a full side of
  the solid, has a color the sky of this picture does not have, belongs
  to no other thing, and what is around it is mostly sky (0.6 or more);
  a few pixels around each bit go with it, because its rim is a blend
  that passes for sky. The sky's colors are those of the items that are
  sky or cloud. This is done against sky only, where the two cannot be
  confused.
- A part made of thin things (the trunks and branches of a tree) is not
  made into triangles and is left as the picture shows it. Reason: they
  are narrower than a triangle, and what the item layer outlines with
  them is mostly what is seen between them.
- Rules tried for this and not kept, each because of the lawn at the foot
  of a tree, which showed as pale triangles: comparing the triangle's
  color with the mean color of each side (sunlit lawn is nearer the mean
  of a sunlit crown than the mean of lawn and shade); a vote without
  regard to how far the triangle lies from the outline (sunlit lawn has
  the color of sunlit leaves); painting over, against any surroundings,
  every pixel nearer the thing's color than the triangle's or the
  surroundings' (shaded lawn was repainted in the lawn's middle color).
- Each triangle's normal is that of its three corners in space, and the
  orientation layer of the imagined subject states it for the whole
  triangle, so the oil painter strokes each plane its own way (spec
  2.13). Where the normal differs by more than 40 degrees from what the
  orientation layer said of the triangle, the layer's is taken: the 3D
  points are noisy on foliage and on thin things (about half the
  triangles of a tree and of a boat), and sound on a watch, a rock and a
  face.

What the trial showed (oil; sheets in
`outputs/interpret_trial/round5/sheets/` and `round6/sheets/`):

- A tree, a watch, a boat, a rock and a face each read as a solid of
  flat planes, at all three sizes of triangle. The coarse size is the
  more abstract; a face needs the fine one to stay a face.
- The planes are painted: each shows the strokes of the brush, and its
  edges are a little soft.
- The model, asked for one thing, names nearly all; the one kept is the
  main subject if it was named.
- With the outline decided where materials meet: a tree and a rock end
  in fine triangles that run into the sky and the foam, and nothing of
  the thing shows past its solid; a watch is as it was; a face runs into
  its hair at the hairline and is sharp elsewhere.
- The tree's solid ends cleanly above its trunks, which stay as the
  picture shows them, and no leaves show past it against the sky.

It would also give cubism what its first form lacks: cuts that follow the
form.

**Building blocks** (the mechanisms above reduce to these)

1. Cut an item out and paint in what was behind it (have: a fill model).
2. Warp an item with a displacement field (have).
3. Move, scale, turn or repeat one instance of an item (have: move,
   scale and repeat).
4. Redraw a contour in straight runs or smooth curves and warp the item
   to it.
5. Split an item into facets by the orientation of its surface (have:
   straight cuts, placed where the surface turns most).
6. Generate ornament inside an item's shape.
7. Lay a figure's outline down along the ground as its shadow (have).
   The 3D points that the orientation model of spec 2.13 also estimates
   give the ground, its direction and the scale.
8. Show a part of an item from a shifted viewpoint.
9. Build a mesh of triangles over an item from the 3D points, at a
   chosen fineness (have).

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
