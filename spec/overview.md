This file covers: purpose, the pipeline from photo to artwork, the reasons behind the main design choices, what is not built, and open questions. Index: [README.md](README.md).

## 1. Purpose

Broadstroke turns a photograph into an artwork by simulating an artist making
it: a hand moving a tool, and the tool physically meeting paper or canvas.
It is not a filter. The photograph is only looked at; every mark on the
output is deposited by a simulated tool, and the look of each medium must
come out of the model of that medium rather than being drawn on.

In scope: oil paint with bristle brushes, watercolor, colored pencil and
soft pastel, on procedurally generated papers, canvas and panel; a
command-line tool; a Windows window that shows the work being made.

### 2.1 Pipeline

1. **Observe** (`src/vision/`): the photo is resampled to the canvas grid and
   analyzed for lightness, edge strength, a detail map, and a flow field
   (structure tensor in Oklab) that follows edges and contours. If the
   photo has a scene file, the detail map also follows how much each item
   in the scene matters, the artist knows which item each cell belongs to,
   and in oil the flow field follows the shape of the surfaces where the
   colors give no direction (spec 2.13).
2. **Set up the easel** (`src/artist/artist.cpp`): a surface is generated at a
   physical scale (`mm_per_px` = physical long side / pixel long side) and a
   canvas is created with the medium's pigment palette.
3. **Work closed-loop** (`src/artist/paint_*.cpp`): the artist decides a mark,
   the hand model turns it into motion, the tool simulation applies it, the
   artist looks at what actually happened and chooses the next mark from the
   difference between canvas and subject.
4. **Render** (`Canvas::render`): Kubelka-Munk color of everything on the
   canvas, shaded by the real relief (surface + dried work + wet film) under a
   raking light.

Requirements:
- No pixel of the photo is copied to the output.
- The same photo, scene file, settings and seed give a bit-identical
  artwork (spec 2.12 states exactly what that covers).
- Stopping early returns the work done so far.
- Quantities are physical: millimeters, newtons, seconds. `mm_per_px` ties
  the grid to the sheet, and tools are specified in millimeters, so the same
  tool makes a finer mark relative to the picture on a larger sheet.
- A visual effect (bristle streaks, dry-brush breakup, grain showing through,
  a dark rim on a wash, a line broadening as a point wears) must come out of
  the model that causes it. Nothing draws an effect onto the canvas.

## 3. Essential rationale

- **CPU, not GPU.** Strokes are sequential and local: each depends on the
  canvas the previous one left, and touches a small footprint. That makes
  per-stroke GPU dispatch slow and the code hard to debug, for no gain in
  fidelity. The previous GPU implementation was replaced for this reason.
  Two things run in parallel, both with results that do not depend on
  thread timing: watercolor's field update (by tiles) and the drawing
  media's hatched patches (those clear of each other, by stick).
- **Spectral Kubelka-Munk.** RGB blending cannot make green from yellow and
  blue, cannot make a glaze darken correctly, and has no notion of opacity.
  Eight bands and eight pigments keep a cell small enough to simulate at full
  resolution, and make a spectrum or a pigment vector exactly one AVX
  register.
- **One active film per cell.** Wet paint in a cell is a single mix, so
  painting into wet paint blends rather than layers. Layering comes from
  drying (oil passes, pastel fixative), which is also how it is done in
  practice.
- **Reduced-order bristles.** Full elastic-rod dynamics per hair is far more
  than the marks need. What shapes a brush mark is which bristles touch, how
  far they trail, and how much paint each has left; those are simulated.
- **Elastic-foundation contact.** It is the simplest contact model in which
  a hard point rides on the crowns of the grain and a soft stick reaches the
  valleys, which is the visible difference between dry media.
- **Closed loop with test swatches.** The artist cannot know what a mark
  will look like except by experience; predicting from swatches and then
  looking at the result keeps the planner honest about the physics instead
  of assuming an idealized stroke.
- **A tool's strokes stay in order.** A brush starts each stroke with the
  residue of the last, and a stick with the point the last patch wore.
  That chain is why oil runs on one core, and why the drawing media run
  different sticks in parallel but never one stick in two places. The
  pastel artist's four pieces of each color (spec 2.9) are four physical
  objects, each with its own chain.
- **A painting is one realization of a chaotic process.** The artist judges
  each mark against thresholds, so a difference in the last digit of one
  number can change a decision and, from there, the rest of the picture. A
  change to the arithmetic therefore either leaves the output byte-identical
  or produces a different picture of the same quality, about as different as
  a new seed. It never produces "almost the same pixels". This is why
  changes are checked the way spec 2.11 describes.
- **Steps scale with distance, not with the grid.** The paint a bristle
  exchanges and the material a stick lays down are functions of the distance
  slid, so the brush can advance a whole cell per step, and a broad stick
  several, without changing the mark. Half-cell steps doubled the work for
  nothing.
- **Change tracking is per cell.** The artist's view is re-rendered only
  where cells changed. Tracking by one bounding box re-rendered far too much
  for diagonal strokes and for scattered wet areas; tracking in 16-cell
  blocks was slower than a box for short pencil strokes.
- **Workers spin before they sleep.** The watercolor step issues three small
  jobs per time step, back to back. Putting worker threads to sleep and
  waking them cost more than the jobs, so an idle worker watches for the
  next job for 100 microseconds first. It costs CPU time, not wall time.

## 4. Status and what is not implemented

- All four media run end to end.
- Graphite pencil, charcoal and wax crayon were removed (2026-11): their
  results were not good enough and would have needed extensive tuning.
- The kneaded eraser exists as a tool and in the dry study but no artist
  routine uses it.
- Watercolor backruns are possible in the model (re-wetting lifts
  non-staining pigment) but have not been tuned or verified.
- Pigment spectra and material constants are parametric fits chosen to
  behave like the named materials; they are not measured data.
- Bit-identical output has been verified on one machine with one compiler
  (MSVC). Whether another compiler, C library or processor gives the same
  bits has not been tested (spec 2.12).
- The studio window has been exercised for live preview, stop, save and
  non-ASCII paths. Dropping a file onto it, the Open dialog, moving between
  monitors of different DPI and sizes above 1400 px have not been driven.
- Not implemented: palette knife and fan brushes; graphite pencil, charcoal,
  wax crayon, ink, gouache, markers; measured pigment data; a GPU path; a
  GUI on platforms other than Windows; parallel execution of oil strokes.
- Scene understanding (spec 2.13) is partly built. A scene file tells the
  artists how much each part of the picture matters; the watercolor,
  pastel and colored pencil artists work by item with it; watercolor works
  skin wet in wet; and oil takes stroke direction from the form where the
  photo gives none. Work by item in oil, the other treatments and depth
  are not implemented. Without a scene file detail is generic edge
  density.
- The observe step that writes scene files is a Python program outside the
  build, tried on five photographs and on one machine.
- In the studio window, opening a photo that has a scene file has not been
  driven.

## 5. Open questions

- Scene understanding (spec 2.13): how the large undefined areas of a
  picture (sky, water, background) get their outlines; boxes serve them
  poorly.
- Scene understanding: how the observe step is shipped. It is a Python
  program now, because the segmentation model is published for Python; what
  a user of the application runs is undecided.
- Scene understanding: the list of treatments and the numbers each one
  carries.
- Is a physically sized default (for example 400 mm for oil) right, or
  should the physical size follow the pixel size so stroke scale stays
  constant across resolutions?
