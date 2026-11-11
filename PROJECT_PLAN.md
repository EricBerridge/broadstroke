# Broadstroke - Specification

## 1. Purpose

Broadstroke turns a photograph into an artwork by simulating an artist making
it: a hand moving a tool, and the tool physically meeting paper or canvas.
It is not a filter. The photograph is only looked at; every mark on the
output is deposited by a simulated tool, and the look of each medium must
come out of the model of that medium rather than being drawn on.

In scope: oil paint with bristle brushes, watercolor, colored pencil and
soft pastel, on procedurally generated papers, canvas and panel; a
command-line tool; a Windows window that shows the work being made.

## 2. Current specification

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

### 2.2 Color (`src/optics/`)

- Reflectance is represented in 8 bands over 400-700 nm; a film holds up to 8
  pigments, each with absorption K and scattering S per band.
- The color of a film over a background is the Kubelka-Munk two-flux solution
  for a finite layer. Limits that must hold: no film gives the background; a
  thick film gives the masstone; zero scattering gives Beer-Lambert over two
  passes.
- Spectra convert to linear sRGB with the CIE 1931 observer under D65; a
  perfect reflector is exactly white.
- The `Mixer` finds pigment proportions for a wanted color: an opaque body
  color (oil) from one to three tube colors plus white, or a transparent
  glaze over an existing reflectance (watercolor). A glaze can only darken.
  Out-of-gamut targets get the nearest achievable mix.
- Palettes: oil (titanium white, cadmium yellow, yellow ochre, cadmium red,
  alizarin crimson, ultramarine, phthalo blue, burnt umber); watercolor (the
  same colors without white, plus phthalo green); sticks (white, yellow, red,
  crimson, ultramarine, phthalo green, umber, black).

### 2.3 Surfaces (`src/surface/`)

A surface is a height field in millimeters, with a bare color, an absorbency
and a grip. Available: `cold_press`, `hot_press`, `rough`, `drawing`,
`bristol`, `pastel_paper` (toned), `canvas`, `canvas_fine` (plain weave, part
filled by priming), `panel`. Generation is deterministic in (name, size,
scale, seed). Relief is zero-mean. Grain finer than about 2.5 cells is not
generated (it would alias into per-cell noise).

### 2.4 The canvas (`src/canvas/`)

Each cell holds a dried stratum (reflectance spectrum + relief thickness) and
an active film (per-pigment amounts + total thickness). Tools add to, move
and remove the active film. `dry()` bakes the film into the dried stratum;
later work then sits on top of it optically. Visible color is the film over
the dried stratum; visible relief is surface + dried relief + film.

### 2.5 The hand (`src/motion/`)

A gesture (intended path, effort, tempo) becomes a sequence of tool poses:
- speed limited by curvature (two-thirds power law) and by finite
  acceleration, so short strokes are slower than long ones and the hand
  brakes into corners;
- position follows the aim point as a damped second-order system, with slow
  aiming drift and 8-12 Hz tremor;
- effort is loaded over a touchdown time and unloaded over the last few
  millimeters, with a short follow-through;
- the tool's lean follows the direction of travel with a lag (brushes) or is
  fixed by the grip (pencils and sticks).

### 2.6 Bristle brush and paint (`src/media/bristle_brush.*`)

- A brush is a bundle of individually simulated bristles (flat, filbert or
  round), each with its own length, stiffness, paint volume and pigment
  composition. A large round is capped at about 30 bristles across; beyond
  that each simulated bristle stands for a lock of hairs.
- Pressing the brush compresses bristles according to their length and the
  ferrule's lean. A compressed bristle lies on the canvas over a length given
  by rigid-chord geometry and trails its anchor like a towed line, pulled
  back in line by its stiffness.
- Each bristle exchanges paint with every cell it slides over: it leaves a
  film whose thickness falls as the bristle empties and rises in the valleys
  of the surface; it reaches a cell only if the bristle or its carried film
  bridges the gap, so a nearly empty brush at light pressure paints only the
  crowns of the weave; it scrapes excess wet paint, taking part up and
  ploughing part aside; films in contact blend both ways.
- Bristles refill from the brush's belly, which also dilutes what they pick
  up. Paint volume and pigment are conserved between brush and canvas.
- The same brush works in water (`WetSink`): it then deposits water and
  suspended pigment into the watercolor simulation.

### 2.7 Dry media (`src/media/dry_tool.*`)

- The tip is a height map: a sharpened cone held at an angle, or the side of
  a stick. Each step solves for the tool height at which contact pressure
  (elastic foundation, softened below the grid scale) summed over the
  footprint equals the hand's force.
- Where there is pressure and sliding, material transfers in proportion to
  pressure times distance (Archard wear), less as the surface becomes coated.
  The same volume is removed from the tip, so a point wears to a facet and
  lines broaden until the tool is sharpened.
- Soft media push part of each deposit into neighboring valleys and, under
  repeated pressure, burnish existing material down into the tooth.
- Tools: colored pencil; soft pastel (side, corner) and pastel pencil; a
  stump that drags material; a kneaded eraser that lifts it.

### 2.8 Watercolor (`src/media/watercolor.*`)

Per cell: free surface water, paper saturation, suspended pigment, settled
pigment (the canvas film). Each time step:
- water levels along its free surface and cannot advance onto dry paper
  until the head exceeds the contact-line pinning;
- suspended pigment moves with the water and diffuses between wet cells;
- the paper absorbs water and dampness wicks through the sheet;
- water evaporates, faster within two cells of the edge of a wet area;
- pigment leaves the water at the paper surface: heavy pigments settle,
  more in valleys (granulation); staining pigments bind evenly; settled
  non-staining pigment is partly re-suspended while the cell is wet.
Pigment is conserved; water only leaves by evaporation.

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
- **Watercolor:** washes light to dark with decreasing round brushes. Each
  patch gets the glaze that, over what is on the paper, comes closest to the
  subject. Time advances while the artist works, and each layer dries before
  it is judged again.
- **Tone intent** (works on paper): the subject's lightness is re-keyed into
  the range from the bare paper (or the lightest opaque stick) to the
  medium's darkest mark, with mid-tones opened toward the paper and local
  contrast stated more firmly.

### 2.10 Application (`src/app/`)

- `broadstroke paint <photo> [-m medium] [-s surface] [-o out] [--size px]
  [--mm long-side-mm] [--effort x] [--seed n] [--frames dir] [--frame-every n]`
- `broadstroke gui [photo]`: Windows window with photo, medium, surface,
  size, effort and seed controls, a live preview of the canvas, stop, save,
  and a photo/artwork toggle. Painting runs on a worker thread.
- `broadstroke study <pigments|surfaces|brush|dry|watercolor> <out>`: scripted
  scenes that show one physical model at a time.
- `broadstroke list`: media, surfaces, studies.
- `broadstroke_profiler <report> <interval-ms> <exe> [args]` (Windows, source
  in `tools/profiler/`): a sampling profiler for performance work. It reports
  the main thread's wall time by call tree, function and source line. The
  `msvc-profile` preset builds the app with release optimization and symbols
  for it.
- Photos are untrusted input: the header is validated and the pixel count
  capped before decoding. Output is PNG, JPEG or BMP by extension.

### 2.11 Verification

`broadstroke_tests` (38 tests, no pixel comparisons) asserts, among others:
Kubelka-Munk limits; yellow + blue mix to green; glazes compose
multiplicatively; the mixer matches in-gamut colors; surfaces are zero-mean
and deterministic; strokes start and end where intended, slow in corners and
are deterministic; paint is conserved between brush and canvas; a small load
runs dry; pressure widens the mark; a brush picks up wet paint; a dry brush
paints high cells first; tip wear equals material on the paper; softer
sticks and harder pressure deposit more; a worn point draws a broader line;
a stump adds nothing and an eraser removes; watercolor conserves pigment; a
wash on dry paper keeps its shape; color bleeds into wet paper; a wash dries
with a darker rim; granulating pigment favors valleys; every medium produces
a likeness of a synthetic subject and keeps its hues; same seed, same
artwork; stop works.

## 3. Essential rationale

- **CPU, not GPU.** Strokes are sequential and local: each depends on the
  canvas the previous one left, and touches a small footprint. That makes
  per-stroke GPU dispatch slow and the code hard to debug, for no gain in
  fidelity. The previous GPU implementation was replaced for this reason.
  Only watercolor's field update is parallel (by tiles, deterministically).
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
