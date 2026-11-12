# Agent bootstrap

Read this first if you are new to the repository. It tells you what the
application is, where things are, how to build and check your work, and the
rules that are easy to break without knowing them. It links to the
specification instead of repeating it.

## What this is

Broadstroke turns a photograph into an artwork by simulating an artist
making it. A simulated hand moves a simulated tool, and the tool physically
meets a simulated sheet: bristles bend and run out of paint, a pencil point
rides on the paper's grain and wears down, watercolor flows and dries from
its edges. The photograph is only looked at. It is not a filter, and the
project's standard is that no corner is cut to fake an effect.

Four media: oil, watercolor, colored pencil, soft pastel. A command-line
tool, a Windows window that shows the work being made, five physics studies
and a sampling profiler.

C++20, CPU only, CMake, MSVC on Windows. The only third-party code is
`stb_image` / `stb_image_write`, vendored.

## Where to read

| You want | Read |
| --- | --- |
| What the application must do, and why | [spec/README.md](spec/README.md) (index), then the area file |
| The reasons behind the main design choices | spec 3, in [spec/overview.md](spec/overview.md) |
| What is not built, and open questions | spec 4 and 5, same file |
| Building, running, options, timings | [README.md](README.md) |
| Where the project stands | [PROJECT_PLAN.md](PROJECT_PLAN.md) |
| Writing rules (language, punctuation, commits) | [.github/copilot-instructions.md](.github/copilot-instructions.md) |
| How to maintain the specification | [.claude/skills/specification-stewardship/SKILL.md](.claude/skills/specification-stewardship/SKILL.md) |

## Source map

```
src/core/      math, images, file I/O, seeded RNG, thread pool, eight-lane math (simd.hpp),
               a JSON reader
src/optics/    spectral pigments, Kubelka-Munk film optics, the mixer        spec 2.2
src/surface/   paper, canvas and panel height fields                         spec 2.3
src/canvas/    the artwork's state, rendering, change tracking               spec 2.4
src/motion/    the hand: a gesture becomes tool poses                        spec 2.5
src/media/     bristle_brush (oil, and wet for watercolor)                   spec 2.6
               dry_tool (colored pencil, pastel, stump, eraser)              spec 2.7
               watercolor (water and pigment on paper)                       spec 2.8
src/vision/    looking at the photo: lightness, edges, detail, flow field    spec 2.1
               scene: what the photo shows, read from its scene file         spec 2.13
src/artist/    the closed-loop artists: paint_oil, paint_dry, paint_watercolor,
               the shared Studio, tone target, stroke paths                  spec 2.9
src/app/       command line (main.cpp), studies, studio window (gui_win32)   spec 2.10
tests/         55 physical-invariant tests, no pixel comparisons             spec 2.11
tools/profiler sampling profiler (Windows)                                   spec 2.10
tools/observe  Python: writes a photo's scene file with local models;        spec 2.13
               not in the build
docs/studies/  reference renders of the five studies
```

The flow of one artwork: `create_artwork` (`src/artist/artist.cpp`) observes
the photo (and its scene, if it has a scene file), makes a surface and a
canvas, and hands a `Studio` to the medium's artist. The artist loops: decide a mark, `perform` it into poses
(`src/motion/`), apply it with a tool (`src/media/`), look at the canvas,
decide the next.

## Build, run, test

```powershell
cmake --preset msvc-release
cmake --build --preset release
ctest --preset release
build\msvc-release\Release\broadstroke.exe paint photo.jpg -m oil
build\msvc-release\Release\broadstroke.exe study brush brush.png
build\msvc-release\Release\broadstroke.exe gui photo.jpg
```

Use Release; the simulation is slow in Debug. `msvc-debug` exists for
debugging, and `msvc-profile` (release optimization plus symbols) for the
profiler. A local `CMakeUserPresets.json` may inherit from these presets, so
do not rename them.

## Rules that are easy to break

- **The physics rule.** No pixel of the photo reaches the canvas, and no
  texture, mask or post-process draws an effect. If an effect is wanted, the
  model that causes it is fixed. Color is pigment amounts through
  Kubelka-Munk, never RGB blending. Quantities are physical (mm, N, s).
- **Determinism.** Same photo, scene file, settings and seed give the same
  bytes. No model runs inside the engine; a scene file is input (spec
  2.13). Every
  random choice comes from a seeded `bs::Rng`; parallel work writes disjoint
  data and draws from its own random stream; nothing depends on the clock
  or on thread timing (spec 2.12).
- **A tool's strokes stay in order.** A brush or a stick carries state from
  one stroke to the next. Do not run one tool's strokes in parallel (spec 3).
- **A change either preserves the output or changes the picture.** The
  artist's decisions are chaotic: a difference in the last digit of one
  number can produce a different painting. So there is no "almost the same
  output" (spec 3). Check which kind your change is, the way spec 2.11
  describes:
  - Output-preserving: prove it by comparing lossless renders (all four
    media at a small and the full size, and the five studies) before and
    after. Not JPEG.
  - Output-changing: the owner decides. Give before and after images (the
    whole picture, a full-resolution detail, the study) and wait for a yes
    or no on that one change before starting the next.
- **Judge by looking.** Passing tests do not mean the artwork is good. A
  physical model is checked with its study, an artist with a real photo.
- **Measure, do not guess, about speed.** Profile first
  (`broadstroke_profiler`, README), and compare timings by alternating runs
  of the two builds; times drift by several percent over a day.
- **The specification moves with the code.** A change to observable
  behavior, a requirement or a design decision updates the section that owns
  it in `spec/`, in the same change. Section numbers are permanent. README
  and PROJECT_PLAN are kept current too.
- **Text.** US English, plain ASCII in source files, no typographic
  punctuation, UTF-8 without a BOM. No mention of AI assistance in commits,
  code or docs. Details in `.github/copilot-instructions.md`.
- **The test photo is private.** `test.jpg` in the repository root is the
  owner's personal photo and is ignored by git, as are `outputs/` and scene
  files (`*.scene.json` and the images beside them). Never commit it, any
  render of it, or its scene file. The images in `docs/studies/` contain no
  photo and are safe.

## Facts worth knowing before you optimize

- Oil runs on one core and is almost entirely the bristle simulation.
  Watercolor's water step and the drawing media's patches run in parallel.
- Things that were tried and did not pay: tracking canvas changes in
  16-cell blocks (slower than per cell for short strokes); vectorizing the
  dampness update in the water step (no gain); a GPU (strokes are
  sequential and local, spec 3).
- Changing any arithmetic that feeds the artist's judgment re-rolls the
  painting, so "a faster formula" is an output-changing change even when it
  is accurate to the last digit.
