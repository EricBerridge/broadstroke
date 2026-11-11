This file covers: the support (paper, canvas, panel) and the state of the artwork laid on it. Index: [README.md](README.md).

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
