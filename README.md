# Broadstroke

Broadstroke turns a photograph into an artwork by simulating an artist making
it. A simulated hand moves a simulated tool, and the tool physically meets a
simulated sheet of paper or canvas: bristles bend, trail and run out of
paint; a colored pencil's point rides on the paper's grain and wears down;
watercolor flows, soaks in and dries from its edges. The photograph is only
looked at. Nothing in the output is a filter applied to it.

[spec/](spec/README.md) is the specification: what each model does and why.
[PROJECT_PLAN.md](PROJECT_PLAN.md) records where the project stands, and
[AGENT_BOOTSTRAP.md](AGENT_BOOTSTRAP.md) is the starting point for an agent
new to the code. This file covers building, running and finding your way
around.

## Media

| Medium | Default surface | How it is worked |
| --- | --- | --- |
| `oil` | `canvas` | Thinned block-in with a large flat, then smaller brushes down to accents; each layer dries before the next |
| `watercolor` | `cold_press` | Transparent washes, light to dark; water flow, bleeding, granulation and edge darkening are simulated |
| `colored_pencil` | `drawing` | A box of wax pencils layered by hatching |
| `pastel` | `pastel_paper` | Soft pastel sticks on toned paper, fixed between layers |

Surfaces: `cold_press`, `hot_press`, `rough`, `drawing`, `bristol`,
`pastel_paper`, `canvas`, `canvas_fine`, `panel`. Any medium can be used on
any surface.

## Prerequisites

- CMake 3.24 or newer.
- A C++20 compiler. On Windows: Visual Studio 2022 or newer (MSVC). GCC 11+
  or Clang 14+ should work elsewhere (the studio window is Windows-only).
- A CPU with AVX2.

There is nothing to download: the only third-party code is `stb_image` and
`stb_image_write`, vendored in `third_party/stb/`. No GPU or Vulkan SDK is
needed.

## Building

### Visual Studio

1. `File > Open > Folder...` and select this directory.
2. Pick the **msvc-release** configuration (use Release: the simulation is
   slow in Debug).
3. Build > Build All. Set `broadstroke.exe` as the startup item.

### VS Code

1. Install the recommended extensions (`.vscode/extensions.json`).
2. `CMake: Select Configure Preset` > `msvc-release`.
3. `CMake: Build` (F7).

### Command line

```powershell
cmake --preset msvc-release
cmake --build --preset release
ctest --preset release
```

The executable is `build\msvc-release\Release\broadstroke.exe`.
`ninja-release` and `ninja-debug` presets are also provided.

## Running

```powershell
# Make an artwork. Output defaults to <photo>_<medium>.png next to the photo.
broadstroke paint photo.jpg -m oil
broadstroke paint photo.jpg -m watercolor -s rough -o washes.png
broadstroke paint photo.jpg -m colored_pencil --size 2000 --effort 1.5

# Open the studio window and watch it being made (Windows).
broadstroke gui photo.jpg

# See what is available.
broadstroke list
```

Options for `paint`:

| Option | Meaning |
| --- | --- |
| `-m, --medium NAME` | Medium (default `oil`) |
| `-s, --surface NAME` | Surface (default depends on the medium) |
| `-o, --output PATH` | `.png`, `.jpg` or `.bmp` |
| `--size PX` | Long side in pixels (default 1400). This is also the simulation resolution |
| `--mm MM` | Physical long side in millimeters (default depends on the medium: 240 for colored pencil, 400 for oil). Tools have real sizes, so a larger sheet means finer marks relative to the picture |
| `--effort X` | 0.4 quick study, 1 finished piece (default), 1.6 meticulous |
| `--seed N` | Same photo + scene file + settings + seed gives the same artwork |
| `--scene PATH` | Scene file saying what the photo shows (default: `<photo>.scene.json` beside the photo, if there is one) |
| `--no-scene` | Do not use a scene file |
| `--frames DIR` | Also save progress frames into DIR (the directory must exist) |
| `--frame-every N` | Strokes between progress frames (default 400) |

Time for the test photo at the default 1400 px (1050 x 1400), Release build,
on a 12-core / 24-thread desktop CPU:

| Medium | Strokes | Time |
| --- | --- | --- |
| colored pencil | 187,860 | 8 s |
| pastel | 333,418 | 15 s |
| watercolor | 9,547 | 56 s |
| oil | 77,719 | 56 s |

Each was timed alone, twice, with the same image both times. Oil runs on one
core; watercolor's water simulation and the hatched patches of colored
pencil and pastel use several.

### Scene files

A scene file tells the artist what the photo shows, so that the main
subject gets finer work than the background and, in watercolor, pastel and
colored pencil, each item is worked on its own up to its outline. It can
also state how the surfaces are oriented, so that oil strokes follow the
form where the photo's colors give no direction (spec 2.13). It is optional:
without one the artwork is exactly what it was before scene files existed.
`tools/observe/observe.py` writes one with models that run on this machine
(two vision-language models through llama.cpp, and SAM 2.1); see
[tools/observe/README.md](tools/observe/README.md) for what it needs.

```powershell
python tools\observe\observe.py photo.jpg   # writes photo.scene.json, its two layers and a preview
broadstroke paint photo.jpg -m oil           # picks up photo.scene.json
broadstroke paint photo.jpg -m oil --no-scene
```

The scene file is plain JSON and may be corrected by hand.

### Studies

Each physical model can be inspected on its own, without the artist:

```powershell
broadstroke study pigments   pigments.png    # masstones, tints, glazes, mixing ladders
broadstroke study surfaces   surfaces.png    # the nine surfaces under raking light
broadstroke study brush      brush.png       # pressure, depletion, dry brush, wet-into-wet pickup
broadstroke study dry        dry.png         # colored pencil lines, wear, hatching and layering; pastel side, corner and pencil
broadstroke study watercolor watercolor.png  # wet-on-dry, wet-in-wet, glazing, dry brush
```

Reference renders of these are in [docs/studies/](docs/studies/).

### Profiling (Windows)

`broadstroke_profiler` is a small sampling profiler built with the project
(source in `tools/profiler/`). It runs a program, samples its threads and
writes a text report: a call tree of the main thread's wall time, self and
inclusive time per function with inlined functions listed separately, and
the hottest source lines. It needs no elevation.

Profile the `msvc-profile` build, which has release optimization plus debug
symbols:

```powershell
cmake --preset msvc-profile
cmake --build --preset profile
cd build\msvc-profile\Release
.\broadstroke_profiler.exe oil.txt 2 .\broadstroke.exe paint ..\..\..\photo.jpg -m oil -o oil.png
```

The arguments are the report file, the sampling interval in milliseconds,
then the program and its arguments. Sampling slows the run by about 20%, so
read shares from the report and take absolute times from an unprofiled run.
The header comment of `tools/profiler/sampler.cpp` explains how to read the
report.

## Layout

```
src/
  core/      math, images, file I/O, deterministic RNG, thread pool, eight-lane math
  optics/    spectral pigments, Kubelka-Munk film optics, color-matching mixer
  surface/   paper, canvas and panel height fields
  canvas/    the state of the artwork (dried stratum + active film) and its rendering
  motion/    the hand: gesture to tool poses
  media/     bristle brush and paint, dry tools (colored pencil, pastel, stump, eraser), watercolor
  vision/    looking at the photo: flow field, edges, detail; reading its scene file
  artist/    the closed-loop artists for oil, drawing media and watercolor
  app/       command line, studies, studio window
tests/       physical-invariant tests (no pixel comparisons)
tools/       sampling profiler (Windows); observe, the Python program that
             writes scene files (not part of the build)
spec/        the specification, one file per functional area
docs/studies reference renders of the five studies
third_party/ stb image reader and writer
```

## Rules worth knowing before changing anything

- **Effects must emerge.** If a brush streak, a grain pattern or a dark rim
  on a wash is wanted, the model that causes it gets fixed. Nothing draws an
  effect onto the canvas directly.
- **No RGB blending.** Color is always pigment amounts through Kubelka-Munk.
- **Physical units.** Millimeters, newtons, seconds. `mm_per_px` ties the
  grid to the physical sheet; tools are specified in millimeters.
- **Deterministic.** Every random choice comes from a seeded `bs::Rng`;
  parallel loops write disjoint data, and work done in parallel draws from
  its own random stream, so thread timing never changes the result.
- **Judge by looking.** The tests check invariants (conservation, limits,
  monotonic responses). They cannot tell whether a drawing is good; run a
  study or a photo.
