This file covers: the closed-loop artists that decide which marks to make for each medium. Index: [README.md](README.md).

### 2.9 The artists (`src/artist/`)

- **Oil:** passes from a thinned block-in with a large flat down to accents
  with a small round. In each pass strokes start where the canvas differs
  most from the subject as seen at that brush's scale, follow the flow field,
  stop where their color no longer helps, and are skipped unless they improve
  the canvas along their whole length. Paint is mixed per stroke; the brush
  is wiped between different colors. Each pass dries before the next.
- **Drawing media:** contour lines along strong edges, then hatched patches
  in layers of decreasing size and changing direction. For each patch the
  artist picks the stick and effort whose mark over what is there comes
  closest to the subject, predicted from test swatches made on a scrap of the
  same paper; hatch lines are clipped to where the mark is still wanted.
  Colored pencil and pastel each use a box of sticks compounded from the
  stick palette. Pastel is laid in with the side of the stick, rubbed in
  with a fingertip, worked up with the corner and finished with pastel
  pencils; its layers are fixed between passes.
  Hatched patches are worked in waves. A wave is a set of patches far
  enough apart that none can see or mark what another looks at. Each is
  planned from the sheet as it stands (stick, effort, hatch lines, the
  hand's motion); patches whose plans overlap wait for a later wave. The
  planned strokes are then made: patches that share a stick one after
  another in their order, because the stick carries its worn point from
  one to the next, and different sticks at the same time. Each patch has
  its own random stream, so the drawing does not depend on scheduling.
  The pastel artist has four pieces of each soft pastel color (a stick
  broken up); within a wave the patches of a color are dealt out among its
  pieces in turn, and each piece keeps its own worn shape. Pastel pencils
  and colored pencils are one of each color.
- **Watercolor:** washes light to dark with decreasing round brushes. Each
  patch gets the glaze that, over what is on the paper, comes closest to the
  subject. Time advances while the artist works, and each layer dries before
  it is judged again.
- **Tone intent** (works on paper): the subject's lightness is re-keyed into
  the range from the bare paper (or the lightest opaque stick) to the
  medium's darkest mark, with mid-tones opened toward the paper and local
  contrast stated more firmly.
