This file covers: the command line, the studio window, the studies and the profiler. Index: [README.md](README.md).

### 2.10 Application (`src/app/`, `tools/`)

**Engine entry point** (`create_artwork`)

- Input: a photo and settings (medium, surface, long side in pixels, long
  side in millimeters, seed, effort). Output: the lit image, the number of
  strokes, the time taken, the medium and surface used, the size, the
  artist's notes, and whether the work was stopped.
- The artwork has the photo's aspect ratio at the requested resolution,
  which is also the simulation resolution.
- Each medium has a customary surface and physical size, used when none is
  given: oil on `canvas` at 400 mm; watercolor on `cold_press` at 300 mm;
  colored pencil on `drawing` at 240 mm; pastel on `pastel_paper` at 400 mm.
- An unknown medium falls back to oil, and an unknown surface to the
  medium's customary one. The long side is limited to 160 to 6000 pixels
  and the effort to 0.3 to 2. An empty photo gives an empty artwork.
- A progress callback receives the stage, the overall fraction, the stroke
  count and the canvas (which it may render). Returning false stops the
  work.

**Command line**

- `broadstroke paint <photo> [-m medium] [-s surface] [-o out] [--size px]
  [--mm long-side-mm] [--effort x] [--seed n] [--frames dir] [--frame-every n]`
  makes an artwork. The output defaults to `<photo>_<medium>.png` next to
  the photo. With `--frames`, progress frames are also saved every so many
  strokes.
- `broadstroke gui [photo]` opens the studio window.
- `broadstroke study <pigments|surfaces|brush|dry|watercolor> <out>` renders
  a scripted scene that shows one physical model at a time.
- `broadstroke list` lists media, surfaces and studies.
- An unknown medium, surface or option on the command line is an error, with
  a non-zero exit code.

**Files**

- Photos are untrusted input: the header is validated and the pixel count
  capped (120 megapixels) before decoding.
- Output is PNG, JPEG or BMP, chosen by the file extension.
- In the studio window, paths may contain non-ASCII characters.

**Studio window** (Windows, `gui_win32.cpp`)

- Controls: open a photo (a dialog, or a file dropped on the window),
  medium, surface, size, effort, seed, start and stop, save the artwork, and
  a toggle between the photo and the artwork.
- The preview shows the canvas as it is being worked, lit as the finished
  piece will be.
- The artwork is made on a worker thread; the window stays responsive.
  The worker never touches a window. It leaves its latest progress, frame
  and finally the artwork in a shared mailbox and wakes the window with a
  message that carries no data; wake-ups are coalesced so the message queue
  cannot flood.
- Closing the window while work is in progress stops the work and waits for
  the worker.
- On other platforms `gui` reports that the window is not available.

**Profiler** (Windows, `tools/profiler/`)

- `broadstroke_profiler <report> <interval-ms> <exe> [args]` runs a program,
  samples its threads and writes a text report of where the main thread's
  wall time went: a call tree, self and inclusive time per function with
  inlined functions listed separately, and the hottest source lines. It
  needs no elevation.
- The `msvc-profile` preset builds the application with release
  optimization and symbols for it.
- Sampling slows the program, so the report is for shares of time; absolute
  times come from an unprofiled run.
