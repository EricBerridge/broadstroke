# Broadstroke

A desktop application that converts photographs into simulated traditional
artwork by modeling the physical processes of painting and drawing —
bristles deforming, pigment depleting along a stroke, paper absorbing or
resisting, pigments mixing in Kubelka-Munk space — rather than applying
image-processing filters.

See [PROJECT_PLAN.md](PROJECT_PLAN.md) for the full project description and
stage-by-stage plan. This README covers building and running what exists
today.

## Current status

**Stages 0–7 complete.** Full pipeline from photograph to painted artwork:
load (JPG/PNG/BMP) → analyze (luminance, edges, structure-tensor flow,
value bands) → plan (one of three named styles) → paint via the GPU
layer stack. Output is BMP / PNG / JPG, picked by file extension.
Long renders can stream intermediate snapshots to disk so progress is
preserved on a crash.

Three planner styles are available via `--style`:

- `watercolor` — three pigments, nearest-color assignment, streamline
  washes following the flow field, soft pencil hatching on the darkest
  band. Wet-on-wet diffusion between pigment passes.
- `pencil_sketch` — pure linework. Cross-contour hatching everywhere,
  perpendicular cross-hatch over the darkest band only.
- `pastel_drawing` — colored pastel pigment via streamlines (no
  diffusion), optional smear drag along each stick's longest stroke.

Canned demo scenes (for inspecting the physics directly):

- `pencil` — graphite strokes on a substrate.
- `watercolor` — three colored washes with wet-on-wet diffusion;
  exercises Kubelka-Munk pigment mixing.
- `mixed` — pastel underdrawing + smear, watercolor wash on top, pencil
  accents last. Demonstrates the full layer stack.

Substrates (`resources/supports/`):

- `cold_press` — rough watercolor paper
- `canvas` — woven cotton duck (proper plain-weave)
- `smooth_bristol` — plate-finish illustration board

Tools (`resources/tools/`):

- Pencils: `pencil_hb`, `pencil_2b`, `pencil_4b`
- Brushes: `brush_round`, `brush_wash`
- Pastels: `pastel_sienna`, `pastel_indigo`

Pigments (`resources/media/`):

- `burnt_sienna`, `ultramarine`, `cadmium_yellow`

Next: Stage 8 focuses on per-device workgroup tuning, a validation-layer
override for Release builds, and a minimal UI for interactive preset
exploration.

## Prerequisites

- **Vulkan SDK** (LunarG) installed and on `PATH`. The build needs
  `glslangValidator` for GLSL → SPIR-V compilation, and the runtime needs
  `vulkan-1.dll`. Download from <https://vulkan.lunarg.com/>.
- **CMake 3.24+**.
- **A C++20 compiler.** On Windows that means **Visual Studio 2022**
  (17.4+) or recent Clang. On Linux, GCC 11+ or Clang 14+.
- **A GPU with Vulkan 1.1 support** (essentially any GPU from 2017+).

## Building

### Visual Studio 2022

1. `File → Open → Folder...` and select the `broadstroke` directory.
2. Visual Studio detects `CMakePresets.json` and shows configurations in
   the toolbar. Pick **msvc-debug** or **msvc-release**.
3. Build → Build All (or **Ctrl+Shift+B**).
4. Set `broadstroke.exe` as the startup item and **F5** to run.

### VS Code

1. Install the recommended extensions (VS Code prompts on first open;
   list lives in `.vscode/extensions.json`).
2. `Ctrl+Shift+P → CMake: Select Configure Preset → msvc-debug` (or
   `ninja-debug` if you have Ninja installed).
3. `Ctrl+Shift+P → CMake: Build` (or **F7**).
4. **F5** to debug, or run `broadstroke` from
   `build/<preset>/broadstroke.exe`.

### Command line

```powershell
cmake --preset msvc-debug
cmake --build --preset debug
ctest --preset debug
build\msvc-debug\Debug\broadstroke.exe
```

## Running the demo

```powershell
# Pipeline: photo in, painted artwork out (analyzer + planner + canvas).
build\msvc-debug\Debug\broadstroke.exe paint photo.jpg --style watercolor --substrate cold_press --output painted.png

# Scripted scenes (fixed strokes, useful for inspecting the physics).
build\msvc-debug\Debug\broadstroke.exe mixed       cold_press     mixed_cold.bmp
build\msvc-debug\Debug\broadstroke.exe watercolor  cold_press     watercolor_cold.bmp
build\msvc-debug\Debug\broadstroke.exe watercolor  smooth_bristol watercolor_bristol.bmp
build\msvc-debug\Debug\broadstroke.exe pencil      canvas         pencil_canvas.bmp
```

For the `paint` scene the source image is downscaled (box filter) so the
longest side is ≤ 1024 px; the canvas is sized to match. Other scenes
default to a fixed 1024×1024 substrate. Output format is selected by
extension: `.bmp`, `.png`, `.jpg`, or `.jpeg`.

## Layout

```
broadstroke/
  cmake/             # build helpers (shader compilation, config header)
  compute/
    bindings/        # thin Kompute wrapper; the only place Vulkan types leak
    kernels/         # .comp GLSL kernels — the actual physics
  reference/         # CPU mirror of every GPU kernel, for validation
  support/           # substrate model, procedural generators, preset loader
  tools/             # pencil/brush/pigment/pastel presets and stroke math
  analyzer/          # CPU image analysis: load, luminance, edges, flow, bands
  planner/           # streamline + hatching planner; emits a RenderPlan
  host/
    canvas.hpp/.cpp  # layer stack + bounded-box dispatch on the GPU side
    apply_plan.*     # materializes a RenderPlan onto a Canvas
    main.cpp         # demo scenes (paint | pencil | watercolor | mixed)
    io/              # file format I/O (BMP plus PNG/JPEG via stb)
  tests/
    physics/         # GPU-vs-CPU equivalence per kernel
  resources/
    supports/        # cold press, canvas, smooth bristol
    tools/           # HB/2B/4B pencils, round/wash brushes, sienna/indigo pastels
    media/           # burnt sienna, ultramarine, cadmium yellow pigments
```

## Architectural rules worth knowing now

- **Nothing outside `compute/bindings/` includes `<kompute/Kompute.hpp>`
  or any Vulkan header.** The wrapper exists precisely to keep Vulkan
  types from spreading. If you find yourself needing a Vulkan type
  elsewhere, the wrapper is missing an abstraction — fix it there.
- **Every GPU kernel has a CPU counterpart in `reference/`.** Tests in
  `tests/physics/` assert equivalence within a small tolerance. This is
  what makes later debugging tractable when a watercolor diffusion bug
  appears two minutes into a paint session. Watch for `round()` /
  `std::round()` divergence on `.5` inputs — use `floor(x + 0.5)` in
  both implementations when sampling at integer coordinates.
- **Workgroup size 16×16 for 2D kernels, 64 for 1D.** Matches AMD
  wavefronts and 2 NVIDIA warps. Stage 8 will revisit per-device tuning
  via `subgroupSize`.
- **Pass-based compose.** `substrate_to_rgba` writes the initial canvas;
  every layer (graphite, pigment via KM) is a separate `apply_*` pass
  that reads + writes the same RGBA buffer. Adding a new medium is
  "write a new apply_X kernel" — never modify a master compose.
- **Stroke kernels accept a bounded-box offset** in their params buffer.
  The host computes the AABB from the stroke + tip radius and dispatches
  only that region — a >100x speedup for short strokes on large canvases.
- **Strokes are auto-chunked** to 64 segments per dispatch by
  `Canvas::stroke_*` so no single dispatch can hit Windows TDR.

## Risk notes carried forward from the plan

- Source photographs are untrusted input — validate headers and cap
  decoded dimensions before any kernel touches them (Stage 5).
- Validation layers stay on in Debug, off in Release.
- The wetness diffusion is forward Euler; large `dt` or `water_diffusion`
  values will go unstable. Keep `dt * water_diffusion ≤ 0.25`.
