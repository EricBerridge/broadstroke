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
   (structure tensor in Oklab) that follows edges and contours.
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
- The same photo, settings and seed give a bit-identical artwork.
- Stopping early returns the work done so far.

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
  resolution.
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
- Not implemented: palette knife and fan brushes; graphite pencil, charcoal,
  wax crayon, ink, gouache, markers;
  measured pigment data; a GPU path; a GUI on platforms other than Windows;
  a portrait-aware detail map (detail is generic edge density).

## 5. Open questions

- Should the artist treat faces specially (finer work, lower error
  tolerance), or stay subject-agnostic as now?
- Is a physically sized default (for example 400 mm for oil) right, or
  should the physical size follow the pixel size so stroke scale stays
  constant across resolutions?
