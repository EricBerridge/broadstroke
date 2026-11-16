# Broadstroke

Broadstroke turns a photograph into an artwork by simulating an artist making
it. A simulated hand moves a simulated tool, and the tool physically meets a
simulated sheet of paper or canvas: bristles bend, trail and run out of
paint; a pastel stick is dragged over the paper's tooth and wears down;
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
| `pastel` | `pastel_paper` | Soft pastel sticks on toned paper, fixed between layers |

Surfaces: `cold_press`, `hot_press`, `rough`, `drawing`, `bristol`,
`pastel_paper`, `canvas`, `canvas_fine`, `panel`. Any medium can be used on
any surface.

## Prerequisites

- Windows, and Visual Studio or its Build Tools with the C++ workload
  (MSVC, C++20) and an MSBuild that reads `.slnx` solutions (17.13 or
  newer). The build is verified with Visual Studio 2026.
- A CPU with AVX2.

The engine, the command line and the tests need nothing downloaded: their
only third-party code is `stb_image` and `stb_image_write`, vendored in
`third_party/stb/`. The studio window is built on the Windows App SDK
(WinUI 3) with C++/WinRT, which come as NuGet packages: the first build
fetches them from nuget.org into `packages\` and later builds find them
there. No other Visual Studio component is needed for them. No GPU or
Vulkan SDK is needed.

## Building

The build is the solution `broadstroke.slnx` with one MSBuild project per
program or library: `stb`, `broadstroke_engine` (a static library),
`broadstroke` (the command line), `broadstroke_studio` (the studio window),
`broadstroke_tests` and `broadstroke_profiler`. The compiler and linker settings they share are
in `Broadstroke.props`. There are three configurations, for x64:

| Configuration | For |
| --- | --- |
| `Release` | Use this one: the simulation is slow in Debug |
| `Debug` | Debugging |
| `Profile` | Release's code with symbols, for the profiler |

### Command line

```powershell
.\build.ps1                         # Release
.\build.ps1 -Test                   # Release, then the tests
.\build.ps1 -Configuration Debug    # or Profile
```

`build.ps1` finds MSBuild, restores the packages of the studio window and
builds the solution. From a developer prompt the same is `msbuild
broadstroke.slnx -t:Restore -p:RestorePackagesConfig=true` once, then
`msbuild broadstroke.slnx -m -p:Configuration=Release -p:Platform=x64`;
without `-p:Configuration` MSBuild builds Debug.

The programs are written to `build\<Configuration>\`, so the executable is
`build\Release\broadstroke.exe` and the tests are
`build\Release\broadstroke_tests.exe [name-filter]`. The studio window is
`build\Release\studio\broadstroke_studio.exe`; its folder also holds the
files of the Windows App SDK (about 60 MB), so that nothing has to be
installed to run it.

Keep the repository at a short path: the packages have long file names,
and a deep folder can push them past what Windows allows.

### VS Code

1. Install the recommended extensions (`.vscode/extensions.json`).
2. `Tasks: Run Build Task` (Ctrl+Shift+B) builds Release; `Tasks: Run Task`
   offers Debug, Profile, and building with the tests. The tasks call
   `build.ps1`.

### Visual Studio

Open `broadstroke.slnx`, pick **Release**, and build; Visual Studio
restores the packages itself. `broadstroke` is the startup project.

## Running

```powershell
# Make an artwork. Output defaults to <photo>_<medium>.png next to the photo.
broadstroke paint photo.jpg -m oil
broadstroke paint photo.jpg -m watercolor -s rough -o washes.png
broadstroke paint photo.jpg -m pastel --size 2000 --effort 1.5

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
| `--mm MM` | Physical long side in millimeters (default depends on the medium: 300 for watercolor, 400 for oil and pastel). Tools have real sizes, so a larger sheet means finer marks relative to the picture |
| `--effort X` | 0.4 quick study, 1 finished piece (default), 1.6 meticulous |
| `--seed N` | Same photo + scene file + settings + seed gives the same artwork |
| `--scene PATH` | Scene file saying what the photo shows (default: `<photo>.scene.json` beside the photo, if there is one) |
| `--no-scene` | Do not use a scene file |
| `--key X` | Watercolor: how light the picture is keyed, 0.5 dark to 1.5 light (default 0.85) |
| `--no-sky-blend` | Oil, with a scene: do not blend sky and cloud with a soft brush |
| `--frames DIR` | Also save progress frames into DIR (the directory must exist) |
| `--frame-every N` | Strokes between progress frames (default 400) |

Time for the test photo at the default 1400 px (1050 x 1400), Release build,
on a 12-core / 24-thread desktop CPU:

| Medium | Strokes | Time |
| --- | --- | --- |
| pastel | 333,418 | 15 s |
| watercolor | 10,009 | 58 s |
| oil | 77,719 | 56 s |

Each was timed alone, twice, with the same image both times. Oil runs on one
core; watercolor's water simulation and pastel's hatched patches use
several.

### Scene files

A scene file tells the artist what the photo shows, so that the main
subject gets finer work than the background and, in watercolor and pastel,
each item is worked on its own up to its outline (softly, where the scene
says the edge is soft). It can
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

The scene file is plain JSON and may be corrected by hand, or in the studio
window.

### The studio window (Windows)

`broadstroke gui [photo]` opens a window with menus at the top, the
painting controls on the left and a preview beside them (spec 2.10). For
the same photo, scene and settings it paints exactly what `broadstroke
paint` does. The window is a program of its own, built on
WinUI 3: `broadstroke gui` starts `studio\broadstroke_studio.exe` from
beside `broadstroke.exe`, and it can be started directly too. It follows
the light or dark app setting of Windows.

- Menus: File opens a photo (Ctrl+O), saves the artwork (Ctrl+S) and
  shows the photo in its folder; View chooses what the preview shows
  (Ctrl+1 to Ctrl+4); Help has the guide (F1) and About, which says the
  version and what the computer is.
- What to paint, one choice of three: the photo using its scene, the
  photo alone, or an interpretation in a manner (spec 2.14). A line below
  the choice says what it needs first, beside the button that does it:
  "Observe photo" writes the scene file, "Imagine" prepares the imagined
  subject that Start then paints.
- Then medium, surface, size, effort, seed, and the one setting the
  medium has (oil: blend the sky; watercolor: the key), Start (F5), and
  what the preview shows (photo, scene, imagined subject, artwork).
- "Edit scene..." lists the scene's items in the place of the painting
  controls, with each item's importance, treatment and edges. A change is
  saved to the scene file at once; the file as it was is kept as
  `<stem>.scene.observed.json`. "Edit changes..." lists what the manner
  does to each item; changes there wait for "Imagine with these changes".
- The window writes nothing beside a photo. It keeps a photo's scene
  file and the files of its interpretations in a folder of the photo's
  own under `%LOCALAPPDATA%\Broadstroke\photos`, found by the photo's
  content, so renaming or moving a photo keeps them (File > Show working
  files). To paint from the command line what the window painted, give
  it that scene file with `--scene`.
- Help: every control has a tooltip. Help > Guide, or F1, opens the
  help, a window of topics that stays open beside the work without
  blocking it. Its pages are in `src/app/help_text.cpp`.
- Observing and imagining start the Python programs in `tools/`. For
  that the window needs `%LOCALAPPDATA%\Broadstroke\studio.json`, which
  names the Python interpreter and the tools folder; copy
  `tools/studio.example.json` and edit it. Without the file the rest of
  the window works.

### Studies

Each physical model can be inspected on its own, without the artist:

```powershell
broadstroke study pigments   pigments.png    # masstones, tints, glazes, mixing ladders
broadstroke study surfaces   surfaces.png    # the nine surfaces under raking light
broadstroke study brush      brush.png       # pressure, depletion, dry brush, wet-into-wet pickup
broadstroke study dry        dry.png         # soft pastel: side, corner and pastel pencil, rubbing, fixing
broadstroke study watercolor watercolor.png  # wet-on-dry, wet-in-wet, glazing, dry brush
```

Reference renders of these are in [docs/studies/](docs/studies/).

### Profiling (Windows)

`broadstroke_profiler` is a small sampling profiler built with the project
(source in `tools/profiler/`). It runs a program, samples its threads and
writes a text report: a call tree of the main thread's wall time, self and
inclusive time per function with inlined functions listed separately, and
the hottest source lines. It needs no elevation.

Profile the `Profile` configuration, which has release optimization plus
debug symbols:

```powershell
.\build.ps1 -Configuration Profile
cd build\Profile
.\broadstroke_profiler.exe oil.txt 2 .\broadstroke.exe paint ..\..\photo.jpg -m oil -o oil.png
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
  media/     bristle brush and paint, dry tools (pastel, pastel pencil, stump, eraser), watercolor
  vision/    looking at the photo: flow field, edges, detail; reading its scene file
  artist/    the closed-loop artists for oil, pastel and watercolor
  app/       command line, studies, and what the studio window shares with
             the tests: saving a corrected scene file, starting the steps
             outside the engine, the help pages
  studio/    the studio window (WinUI 3, C++/WinRT)
tests/       physical-invariant tests (no pixel comparisons)
tools/       sampling profiler (Windows); observe, the Python program that
             writes scene files (not part of the build); interpret, a trial
             that prepares a subject in an artistic manner (spec 2.14)
spec/        the specification, one file per functional area
docs/studies reference renders of the five studies
third_party/ stb image reader and writer
broadstroke.slnx, Broadstroke.props, build.ps1
             the solution, the settings its projects share, and a script
             that builds it; each project file (*.vcxproj) is beside its
             sources
nuget.config the packages of the studio window go to packages\ (not in git)
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
