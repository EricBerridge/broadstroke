This file covers: the support (paper, canvas, panel) and the state of the artwork laid on it. Index: [README.md](README.md).

### 2.3 Surfaces (`src/surface/`)

A surface is a height field in millimeters at the simulation's cell size,
with a bare color, an absorbency and a grip. It is not a decorative texture:
tools ride on this relief, deposit on its peaks first, fill its valleys under
pressure, and water pools in it.

| Name | Character | Relief (mm) | Absorbency | Grip |
| --- | --- | --- | --- | --- |
| `cold_press` | watercolor paper, pebbled | 0.060 | 0.55 | 0.75 |
| `rough` | watercolor paper, coarse | 0.110 | 0.60 | 0.90 |
| `hot_press` | watercolor paper, smooth | 0.010 | 0.40 | 0.35 |
| `bristol` | very smooth board | 0.005 | 0.25 | 0.25 |
| `drawing` | drawing paper, fine tooth | 0.022 | 0.50 | 0.60 |
| `pastel_paper` | toned (warm gray), toothy | 0.045 | 0.50 | 0.95 |
| `canvas` | plain weave, part filled by priming | 0.110 | 0.30 | 0.80 |
| `canvas_fine` | finer weave, more priming | 0.055 | 0.25 | 0.65 |
| `panel` | primed panel, nearly flat | 0.020 | 0.20 | 0.50 |

- Relief is zero-mean, scaled so that its 2nd to 98th percentile spans plus
  and minus the nominal relief.
- Absorbency (0 hard sized or primed, 1 blotter) sets how fast the sheet
  drinks water. Grip (0 to 1) sets how strongly the surface abrades dry media.
- Generation is deterministic in (name, size, scale, seed).
- Paper grain finer than about 2.5 cells is not generated: it would alias
  into per-cell noise, which tools would then reproduce as speckle.
- An unknown surface name falls back to `drawing`.
- Any medium can be used on any surface.

**Acceptance** (`tests/test_surface.cpp`, `broadstroke study surfaces`):
relief is centered and bounded; the same inputs give the same surface;
rough paper is rougher than bristol.

### 2.4 The canvas (`src/canvas/`)

**State**

- Each cell holds a dried stratum (a reflectance spectrum and a relief
  thickness) and an active film (per-pigment amounts and a total thickness).
- Tools add to, move and remove the active film. Wet paint, loose pastel
  dust and pencil wax all live in it.
- `dry()` bakes the film into the dried stratum: the visible reflectance
  becomes the new base, and a given share of the film's thickness remains as
  relief. Later work then sits on top of it optically. Oil passes dry
  between layers; pastel is fixed the same way.
- Visible color is the film over the dried stratum (spec 2.2). Visible relief
  is surface + dried relief + film. For watercolor the film is a stain and
  adds no relief.
- Tooth is the relief of surface plus dried work relative to its local mean
  over about 2.5 mm, the scale a brush tip or the face of a stick averages.
  Tools use it to decide which cells they reach at a given pressure. It is
  recomputed when a layer dries.
- A second pigment field can be overlaid on the film when rendering.
  Watercolor uses it for pigment still suspended in water, so the artist
  sees wet color.

**Rendering**

- The finished image is the color of each cell shaded by the slope of the
  visible relief under a raking light, with an ambient share.
- A thick film can carry a specular highlight (oil). Works on paper are lit
  flatter and without gloss.
- The unlit color of a region can be rendered on its own; it is what the
  artist compares with the subject.

**Change tracking**

- The canvas records which cells changed color since the artist last looked,
  one mark per cell, and re-renders the unlit color of exactly those cells on
  request. Unchanged cells are not re-rendered.
- Tools mark the cells they change as they go, step by step, not as one
  bounding box per stroke.
- Marking can be done from several threads working on non-overlapping
  regions; the bookkeeping of where the marks lie is then completed from one
  thread (spec 2.12).
- After the artist looks, their view equals the unlit color of the canvas
  everywhere. (A view that lagged behind in places was a defect: the artist
  judged against a picture that no longer existed.)

**Acceptance** (`tests/test_surface.cpp`): drying bakes the film into the
base and leaves no active film.
