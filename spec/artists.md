This file covers: the closed-loop artists that decide which marks to make for each medium. Index: [README.md](README.md).

### 2.9 The artists (`src/artist/`)

**Common to all**

- The artist keeps a view of the canvas: its unlit color as last looked at.
  Looking refreshes the parts that changed (spec 2.4). Judgments compare
  that view with the subject in Oklab, at a spot (the mean over a small
  disc) or over blocks.
- Every artist is more critical where the subject's detail map is higher.
  The detail map is edge density, and with a scene file it also follows
  how much each item matters (spec 2.13).
- `effort` (0.3 to 2; 1 is a finished piece) decides how many passes are
  made and how many times each is repeated. Lower effort drops the finest
  passes.
- Progress is reported between strokes, every few strokes and at least four
  times a second. If the caller asks to stop, the artist stops and the work
  so far is returned.
- Each artist records notes of what it did, stage by stage.

**Oil** (`paint_oil.cpp`)

- Passes go from a thinned block-in with a large flat down to accents with a
  small round. Brush widths are fractions of the long side W of the
  painting:

  | Pass | Brush | Width | From effort |
  | --- | --- | --- | --- |
  | block-in | flat | W/13 | always |
  | large forms | flat | W/26 | always |
  | modeling | filbert | W/52 | always |
  | small forms | filbert | W/100 | 0.6 |
  | details | round | W/190 | 0.8 |
  | accents | round | W/330 | 1.0 |

- For each pass the subject is seen at that brush's scale (blurred to it),
  and a flow field at that scale gives the direction of the forms. With a
  scene that states orientation, the field follows the shape of the
  surfaces where the photo gives no direction (spec 2.13).
- In each sweep of a pass, candidate strokes start where the canvas differs
  most from the subject, on a grid about half a brush width apart, taken
  roughly dark to light. The first sweep of the block-in covers the whole
  canvas regardless.
- A stroke follows the flow field, stops where its color no longer suits
  the subject or where the canvas is already closer than it would be, and is
  skipped unless it improves the canvas along its whole length. The artist
  is more critical where the subject has detail.
- Paint is mixed per stroke to match the subject at the start of the stroke
  (spec 2.2). The brush is wiped nearly clean before a different color and
  half clean otherwise, then loaded with enough for the stroke and at least
  enough to wet its tips.
- Each pass dries before the next.

**Drawing media** (`paint_dry.cpp`)

- Work is hatched patches in layers of decreasing size and changing
  direction (fixed, along the forms, or across them), then lines along
  strong edges as accents.
- Before starting, the artist makes test swatches of each kind of tool on a
  scrap of the same paper at four efforts, and measures how much material a
  hatched patch lays down.
- For each patch the artist picks the stick and effort whose mark, over what
  is there, comes closest to the subject, predicted from those swatches. A
  patch is worked only if it is far enough from the subject, its
  neighborhood agrees that something is missing, and the best mark improves
  it by a clear margin.
- Hatch lines run back and forth across the patch and are clipped to the
  runs where the mark is still wanted; the effort follows how much is
  wanted.
- Sticks are sharpened when worn past a limit for their kind.
- Colored pencil and pastel each use a box of 72 sticks compounded from the
  stick palette: 15 hues each as itself, light, pale and dark, plus neutrals
  and flesh tones.
- Colored pencil: five hatched layers, then accents.
- Pastel: laid in with the side of the stick, rubbed in with a fingertip in
  smooth areas, worked up with the side and then the corner, and finished
  with pastel pencils: details, closing and accents. Its layers are fixed
  between passes.
- Closing (pastel, from effort 1.0): the paper still shows in small spots
  where no stroke reached into its grain, and they glare in the darks. The
  artist goes over the whole sheet with the pastel pencils in patches of
  1.6 mm, three rounds, and closes them. The tolerance is the same
  everywhere, whatever the detail and importance of the place, because
  covering the paper is craft and not emphasis; and a mark may be a dab as
  short as one spot.
- An accent is judged like any other mark. Along each strong edge the
  artist finds the stretch where the drawing is lighter than what is wanted
  by at least 0.03 in lightness, picks the pencil and effort whose line
  over what is there comes closest, and draws only where that line is
  wanted, pressing as much as it is wanted. No line is made where no pencil
  improves the drawing by a clear margin. Reason: a strong edge is not a
  dark one. A dark line along the edge between two light forms, such as a
  wrinkle in a face, is wrong however strong the edge is.
- What is wanted along an edge differs by medium. Pastel wants the subject
  as it is, so an accent only supplies a dark the drawing lacks. Colored
  pencil states each strong edge a little more firmly: the line is moved
  into the dark beside the edge, and that dark is wanted 0.13 darker in
  lightness than the subject has it. Every strong edge then gets a soft
  line in the color of its own dark side. Reason: with accents only where
  a dark was missing, a pencil drawing lost the definition of its small
  shapes; with a black line on every edge, faces were scored with lines.
- Hatched patches are worked in waves. A wave is a set of patches far
  enough apart that none can see or mark what another looks at. Each is
  planned from the sheet as it stands (stick, effort, hatch lines, the
  hand's motion); patches whose plans overlap wait for a later wave. The
  planned strokes are then made: patches that share a stick one after
  another in their order, because the stick carries its worn point from
  one to the next, and different sticks at the same time. Each patch has
  its own random stream, so the drawing does not depend on scheduling.
- The pastel artist has four pieces of each soft pastel color (a stick
  broken up); within a wave the patches of a color are dealt out among its
  pieces in turn, and each piece keeps its own worn shape. Pastel pencils
  and colored pencils are one of each color.
- Fingertip blending and accents are made one stroke at a time.
- With a scene, pastel and colored pencil are worked by item: each item in
  a patch is judged and worked on its own, and its marks end at its outline.
  In pastel they lap over an outline that is a soft one. Hair is hatched
  along its strands in every layer (spec 2.13).

**Watercolor** (`paint_watercolor.cpp`)

- Washes go light to dark with decreasing round brushes: first washes
  (brush W/16), second washes (W/30), shapes (W/55), details (W/110, from
  effort 0.7) and fine details (W/190, from effort 1.0). The white of the
  paper is the only white.
- The sheet is worked patch by patch, top to bottom. A patch gets a wash
  only if it is far enough from the subject, the subject is not lighter than
  what is there, and the glaze that comes closest (spec 2.2) improves it by
  a clear margin. First washes apply only part of the correction and stay
  light.
- Strokes of a wash run along the forms where the subject has a direction,
  otherwise level across the sheet, and are clipped to where the wash is
  still wanted. The brush is charged once per patch.
- Time advances while the artist works (spec 2.8), more for a patch that was
  washed than for one that was passed over, so earlier work dries
  meanwhile. Each layer dries completely before it is judged again.
- The artist sees pigment still suspended in water as part of the picture.
- With a scene the sheet is worked item by item, strokes follow and end at
  the items' outlines, skin and sky are worked wet in wet, and color is
  let bleed over an outline that is a soft one (spec 2.13).

**Tone intent** (works on paper, `target.cpp`)

- The subject's lightness is re-keyed into the range from the bare paper (or
  the lightest opaque stick) to the medium's darkest mark, between the 2nd
  and 98th percentile of the subject.
- Mid-tones can be opened toward the paper while the darks stay anchored,
  and local contrast is stated more firmly than in the photograph. Colored
  pencil is keyed lighter than pastel; watercolor slightly raises chroma.

**Acceptance** (`tests/test_artist.cpp`): every medium makes a likeness of a
synthetic subject (block lightness correlates with it); the color media
keep its hues; the same seed gives the same artwork and a different seed a
different one; work stops when asked and returns what was done; unknown
settings fall back; along a strong edge between two pale forms pastel
leaves no dark line and colored pencil only a soft one.
