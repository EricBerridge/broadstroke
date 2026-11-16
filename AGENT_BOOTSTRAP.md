# Agent bootstrap

Read this first if you are new to the repository. It tells you what the
application is, where things are, how to build and check your work, and the
rules that are easy to break without knowing them. It links to the
specification instead of repeating it.

## What this is

Broadstroke turns a photograph into an artwork by simulating an artist
making it. A simulated hand moves a simulated tool, and the tool physically
meets a simulated sheet: bristles bend and run out of paint, a pastel stick
is dragged over the paper's tooth and wears down, watercolor flows and dries from
its edges. The photograph is only looked at. It is not a filter, and the
project's standard is that no corner is cut to fake an effect.

Three media: oil, watercolor, soft pastel. A command-line
tool, a Windows window that shows the work being made, five physics studies
and a sampling profiler.

C++20, CPU only, MSVC on Windows, built with MSBuild from
`broadstroke.slnx`. The only third-party code is `stb_image` /
`stb_image_write`, vendored.

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
               dry_tool (pastel, pastel pencil, stump, eraser)               spec 2.7
               watercolor (water and pigment on paper)                       spec 2.8
src/vision/    looking at the photo: lightness, edges, detail, flow field    spec 2.1
               scene: what the photo shows, read from its scene file         spec 2.13
src/artist/    the closed-loop artists: paint_oil, paint_dry, paint_watercolor,
               the shared Studio, tone target, stroke paths                  spec 2.9
src/app/       command line (main.cpp), studies, starting the studio window  spec 2.10
               (studio_launch), the version (version.hpp), and what the
               window shares with the tests: saving a corrected scene file
               (scene_edit), starting the steps outside the engine
               (process_win32), the help pages (help_text)
src/studio/    the studio window, WinUI 3 with C++/WinRT: MainWindow (the     spec 2.10
               layout in .xaml, the behavior in .xaml.cpp: what to paint,
               the painting controls, and in their place the editor of
               the scene's items), HelpWindow, support (strings, paths,
               pictures for the preview; no controls)
tests/         59 tests of physical invariants and of the application's     spec 2.11
               file handling and help pages, no pixel comparisons
tools/profiler sampling profiler (Windows)                                   spec 2.10
tools/observe  Python: writes a photo's scene file with local models;        spec 2.13
               not in the build
tools/interpret Python: a trial that prepares a subject in an artistic     spec 2.14
               manner for the engine to paint; not in the build
docs/studies/  reference renders of the five studies
broadstroke.slnx   the solution; one project (*.vcxproj) beside the sources of
                   each of stb, broadstroke_engine, broadstroke,
                   broadstroke_studio, broadstroke_tests, broadstroke_profiler
Broadstroke.props  the compiler and linker settings every project shares
build.ps1          finds MSBuild, restores the studio's NuGet packages into
                   packages\ (nuget.config), builds the solution, runs the tests
```

The flow of one artwork: `create_artwork` (`src/artist/artist.cpp`) observes
the photo (and its scene, if it has a scene file), makes a surface and a
canvas, and hands a `Studio` to the medium's artist. The artist loops: decide a mark, `perform` it into poses
(`src/motion/`), apply it with a tool (`src/media/`), look at the canvas,
decide the next.

## Build, run, test

```powershell
.\build.ps1 -Test        # builds Release, then runs the tests
build\Release\broadstroke_tests.exe [name-filter]
build\Release\broadstroke.exe paint photo.jpg -m oil
build\Release\broadstroke.exe study brush brush.png
build\Release\broadstroke.exe gui photo.jpg     # starts build\Release\studio\broadstroke_studio.exe
```

Use Release; the simulation is slow in Debug. `.\build.ps1 -Configuration
Debug` builds for debugging, and `-Configuration Profile` (release
optimization plus symbols) for the profiler. Each goes to
`build\<Configuration>\`.

The studio window is built on the Windows App SDK and C++/WinRT, which are
NuGet packages (`src/studio/packages.config`; the versions are also at the
top of `broadstroke_studio.vcxproj`). The first build needs the network to
fetch them. The studio is unpackaged and self-contained: its output folder
`build\<Configuration>\studio\` holds the SDK's files (spec 2.10).

In the studio's code, an element named in the XAML (`x:Name="start"`)
becomes a member function of the window class (`start()`), so a method or
a local variable of that name collides with it. A control that has a
tooltip sits in a host element named `...Tip`. The shortcuts of the menu
items are accelerators of the root grid (`OnShortcut`), and what the state
permits is asked in one place (`can_open`, `can_save`, `can_start`). To
drive the window from a script, use UI Automation: the XAML names are the
automation ids, and a menu's items are there only while the menu is open.

A source file added to the code is added to its project's `.vcxproj`.
`Broadstroke.props` holds the settings that reach the compiler; the
engine's bytes depend on them, so changing one is a change to the engine
and is proved like one (see the rules below). MSBuild's defaults are
switched off there on purpose: no whole-program optimization, nothing but
what is listed.

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
  - Output-preserving: prove it by comparing lossless renders (all three
    media at a small and the full size, and the five studies) before and
    after. Not JPEG.
  - Output-changing: the owner decides. Give before and after images (the
    whole picture, a full-resolution detail, the study) and wait for a yes
    or no on that one change before starting the next.
  - A change to the studio window is output-preserving for the engine.
    Prove that too, and compare an artwork saved from the window with the
    file the command line writes for the same settings (spec 2.11).
- **The window only gathers input.** It calls `create_artwork` with a
  photo, a scene and settings, as the command line does. The steps that
  run models (observe, interpret) are started as programs of their own and
  their files are read; no model runs in the window (spec 2.10).
- **Judge by looking.** Passing tests do not mean the artwork is good. A
  physical model is checked with its study, an artist with a real photo.
- **Measure, do not guess, about speed.** Profile first
  (`broadstroke_profiler`, README), and compare timings by alternating runs
  of the two builds; times drift by several percent over a day.
- **The specification moves with the code.** A change to observable
  behavior, a requirement or a design decision updates the section that owns
  it in `spec/`, in the same change. Section numbers are permanent. README
  and PROJECT_PLAN are kept current too.
- **The help moves with the window.** The window's help pages
  (`src/app/help_text.cpp`) and tooltips (`add_tips` in
  `src/studio/MainWindow.xaml.cpp`) are what a user reads. A change to what a
  control does changes both, in the same change (spec 2.10).
- **Text.** US English, plain ASCII in source files, no typographic
  punctuation, UTF-8 without a BOM. No mention of AI assistance in commits,
  code or docs. Details in `.github/copilot-instructions.md`.
- **The test photo is private.** `test.jpg` in the repository root is the
  owner's personal photo and is ignored by git, as are `outputs/` and scene
  files (`*.scene.json` and the images beside them). Never commit it, any
  render of it, or its scene file. The images in `docs/studies/` contain no
  photo and are safe.

## Experiments in progress

Two things are worked on outside the engine, in Python, with models that
run on the owner's machine. What is intended next is the list "Intended
work" in [PROJECT_PLAN.md](PROJECT_PLAN.md).

- **The observe step** (`tools/observe/`, spec 2.13) writes a photo's
  scene file. Built and kept.
- **The interpretation trial** (`tools/interpret/`, spec 2.14) prepares a
  subject in an artistic manner for the engine to paint. An
  investigation, in oil only; nothing of it is in the engine.
  [tools/interpret/README.md](tools/interpret/README.md) says how to run
  a round and where its results are.

What you need that is not in git (all on the owner's machine):

- The five public trial photos with their scene files:
  `outputs/scene_trial/photos_v2/` (written with the observe step's
  check; use these) and `outputs/scene_trial/photos/` (the set the sheets
  before 2026-11-14 were made with; do not overwrite).
- The sheets of every round, under `outputs/scene_check/` and
  `outputs/interpret_trial/`.
- The models and the Python environment, under
  `%LOCALAPPDATA%\Broadstroke\scene_trial`, and the paths to them in
  `tools/observe/observe.local.json`.
- `%LOCALAPPDATA%\Broadstroke\studio.json`, which tells the studio window
  where the Python interpreter and the tools folder are (spec 2.10).

Both steps can be started from the studio window. The window writes
nothing beside a photo: it keeps a photo's scene file and the files of its
interpretations in a folder of that photo's own under
`%LOCALAPPDATA%\Broadstroke\photos`, found by the photo's content (spec
2.10). The command line and the trial rounds still work on scene files
beside the photos.

How the owner judges this work: by sheets that put several results in one
image, the painting of the photo as it is beside the result; one yes or no
per change; painting the photo as it is stays the default. He wants a
mechanism that does something to a thing because of what it is; a change
of tone or color laid over the picture he calls a filter and does not
want (spec 2.14).

## Facts worth knowing before you optimize

- Oil runs on one core and is almost entirely the bristle simulation.
  Watercolor's water step and pastel's patches run in parallel.
- Things that were tried and did not pay: tracking canvas changes in
  16-cell blocks (slower than per cell for short strokes); vectorizing the
  dampness update in the water step (no gain); a GPU (strokes are
  sequential and local, spec 3).
- Changing any arithmetic that feeds the artist's judgment re-rolls the
  painting, so "a faster formula" is an output-changing change even when it
  is accurate to the last digit.
