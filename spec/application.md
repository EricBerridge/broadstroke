This file covers: the command line, the studio window, the studies, the profiler, and how the engine is verified. Index: [README.md](README.md).

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
