# Broadstroke — Project Description and Stage Plan

## What this project is

Broadstroke converts photographs into simulated traditional artwork. The key
word is _simulated_: the pipeline models the physical processes at work when
a human artist makes a mark — bristles splaying under load, graphite
depleting along a stroke, paper fibers absorbing pigment, wet pigments
mixing by Kubelka-Munk subtractive rules — rather than applying image
filters, style-transfer networks, or any other post-process that operates on
the photograph directly.

The output is an image that looks as though an artist rendered the scene in
a chosen medium (pencil, watercolor, oil, pastel…) on a chosen substrate
(cold-press watercolor paper, canvas, smooth bristol…) using a chosen tool
(HB pencil, flat watercolor brush, palette knife…).

## Goals

1. **Physical accuracy over visual tricks.** Every parameter in the system
   maps to something measurable about a real material. Tooth, absorption,
   and wetness fields are not invented textures; they model the micro-surface
   of real paper and canvas stocks.

2. **One pipeline, many media.** The substrate, tool, and medium are
   independent axes. Swapping a pencil preset for a brush preset (same
   substrate) should produce a different result for the same physical reason
   a real artist would get a different result.

3. **GPU-accelerated, CPU-validated.** Every kernel has a pixel-exact CPU
   reference implementation. Tests assert equivalence within a small floating-
   point tolerance. This makes debugging tractable across the full pipeline.

4. **Deterministic and reproducible.** Given the same photo and the same
   preset set, the output is always identical. No random seeds that vary
   run-to-run.

---

## Stage plan

### Stage 0 — Compute foundation ✅ implemented

Goal: prove the Kompute/Vulkan plumbing works end-to-end before any physics.

- Kompute v0.9.0 fetched via CMake FetchContent; isolated behind
  `compute/bindings/` so no Vulkan type leaks into application code.
- `hello_double` kernel (doubles every element in a float buffer).
- CPU reference + equivalence test establish the validation pattern used by
  all later stages.
- CMake preset infrastructure (`CMakePresets.json`); Visual Studio 2022 and
  VS Code both supported.

### Stage 1 — Substrate model ✅ implemented

Goal: represent the physical surface the artist works on.

- Multi-field substrate per pixel: heightfield, absorption coefficient,
  wetness (initially zero), grain orientation, tooth (gradient magnitude of
  heightfield, drives graphite/pigment adhesion).
- Procedural generators for three presets:
  - `cold_press` — rough watercolor paper (strong tooth, high absorption)
  - `canvas` — woven cotton duck (directional grain, moderate absorption)
  - `smooth_bristol` — plate-finish illustration board (near-zero tooth)
- `support_visualize` GPU kernel: raking-light normal map from central
  differences of the heightfield, diffuse shading with configurable light
  direction and ambient level.
- JSON preset format under `resources/supports/`; loader validated against
  `kind` field.

### Stage 2 — Pencil on paper ✅ implemented

Goal: first medium (graphite) and first tool (pencil).

- **`pencil_stroke` GPU kernel** walks every segment of a polyline and
  accumulates graphite into a per-pixel density layer:
  - Closest-point-on-segment distance for each pixel.
  - Hermite falloff over the tip's soft band (avoids degenerate smoothstep
    when softness = 0).
  - Deposit per segment = pressure × falloff × tooth × deposit_strength.
  - Exponential saturation: `density = 1 − (1 − prev) × exp(−deposit)`
    so repeated passes asymptote to 1 rather than clipping.
- **`canvas_compose` GPU kernel** blends the substrate's raking-light
  shading with the graphite layer. Graphite retains partial paper relief
  (graphite sits in the tooth valleys, letting peaks remain lighter).
- Three pencil presets under `resources/tools/`: `pencil_hb`, `pencil_2b`,
  `pencil_4b` (softer, darker, broader tip in sequence).
- `tools/` library with `PencilPreset` loader and `Stroke` / `StrokePoint`
  types; `resampleUniform` for uniform arclength resampling.
- Demo (`host/main.cpp`) draws 15 pre-baked strokes: pressure-varying
  S-curve, repeated arc passes for buildup, crosshatch grid.
- GPU/CPU equivalence tests for both new kernels.

### Stage 3 — Watercolor (wet media) ✅ implemented

Goal: first wet medium with subtractive color mixing.

- **Pigment in Kubelka-Munk space.** Pigments carry K (absorption) and
  S (scattering) per RGB channel (`PigmentPreset`). The `apply_pigment`
  kernel scales both by per-pixel concentration, then derives:
  - `R_inf = 1 + K/S − √((K/S)² + 2K/S)` — body color of an infinitely
    thick film (closed-form KM for opaque layers).
  - `T = exp(−(K + S) · c · thickness_scale)` — transmittance through
    a layer of thickness proportional to concentration.
  - Final reflectance per channel: `R_inf · (1 − T) + R_substrate · T`.
    Thin layer → substrate dominates (transparent wash); thick layer →
    R_inf dominates (body color). Stacking pigments gives the right
    subtractive hue shifts (sienna + ultramarine = neutral grey, etc.).
- **Brush as a separate tool.** `BrushPreset` carries tip radius/softness,
  per-unit-pressure water load, and per-unit-pressure pigment load. The
  same brush picks up any pigment — separation from the medium is real,
  not just naming.
- **Wetness + reaction-diffusion.** Each pigment layer carries a wetness
  field alongside the pigment field. `wetness_diffuse` runs a single
  forward-Euler step: wetness diffuses by 4-neighbor Laplacian, pigment
  rides along (rate scaled by min/avg of local wetness — pigment can't
  flow into a dry cell, which produces the characteristic dry-edge /
  granulation of real watercolor), and absorption sinks wetness toward
  zero. Host runs N substeps between strokes via ping-pong.
- **`watercolor_stroke` kernel** deposits both fields. Pigment deposit
  is gated by tooth (`0.5 + 0.5·tooth`); water deposit is not.
- **Three pigment presets** under `resources/media/`: `burnt_sienna`,
  `ultramarine`, `cadmium_yellow`.
- **Pass-based compose.** Replaced the monolithic `canvas_compose` with
  `substrate_to_rgba` + `apply_graphite` + `apply_pigment`. Each layer is
  one dispatch that reads + writes the same RGBA buffer, so adding a new
  medium in a future stage is "write one new apply_X kernel" rather than
  edit a master kernel.
- Input validation deferred to Stage 5 when an actual image decoder lands.

### Stage 4 — Multiple media layers and performance ✅ implemented

Goal: layer multiple media (pastel underdrawing + watercolor wash + pencil
accents, for example) and keep dispatch time bounded.

- **Layer stack** (`host/canvas.hpp`). A `Canvas` owns the substrate
  fields and an ordered `std::vector` of layers; each layer is either a
  `GraphiteLayer` (one density field + color) or a `PigmentLayer` (one
  pigment field + one wetness field + KM coefficients). `compose()` runs
  `substrate_to_rgba` then walks the layer list, dispatching the right
  `apply_*` kernel for each. Adding more layer types is one new struct
  and one branch.
- **Bounded-box dispatch.** All stroke kernels (`pencil_stroke`,
  `watercolor_stroke`, `pastel_stroke`, `pastel_smear`) read an
  `(offset_x, offset_y)` from their params and add it to the global
  invocation ID. The host computes each stroke's AABB padded by tip
  radius, clamps to canvas, and dispatches only that workgroup count.
  Short strokes on a 1024² canvas now dispatch a few hundred threads
  instead of a million.
- **TDR chunking.** `Canvas::stroke_*` splits any stroke longer than
  64 segments into overlapping chunks (1-segment overlap so adjacent
  chunks join cleanly). Each chunk is a separate dispatch, which keeps
  any single GPU job well under the 2 s Windows TDR threshold even for
  pathological stroke counts.
- **Pastel medium** with two kernels:
  - `pastel_stroke` — same per-pixel/all-segments shape as `pencil_stroke`
    but writes to a pigment field and uses additive deposit (KM thickness
    handles visual saturation, no exp falloff).
  - `pastel_smear` — directional box blur along a drag segment with
    perpendicular Hermite falloff. Models the finger / tortillon move that
    softens edges. Care taken with `floor(x + 0.5)` integer rounding so
    GPU and CPU agree at half-pixel sample positions.
    Two stick presets (`pastel_sienna`, `pastel_indigo`) carry their own
    KM coefficients on the preset, so the same `apply_pigment` compose
    pass renders pastel and watercolor.

### Stage 5 — Image analyzer ✅ implemented

Goal: extract the structural information from the source photograph that
the stroke planner needs. CPU-only; no dependency on `compute/`.

- **Image loader** (`load.cpp`). `stb_image` decodes JPG/PNG/BMP and
  converts sRGB → linear float on the way in. Header is pre-validated
  with `stbi_info` and dimensions are capped before any decode allocation
  so a malformed or oversize file never gets near GPU memory — covers
  the "untrusted input" risk note from the original plan.
- **Luminance** (`luminance.cpp`). Rec. 709 weights → per-pixel `Y` in
  [0, 1]. Drives wash strength in the planner (darker = more pigment).
- **Sobel edges** (`edges.cpp`). Standard 3×3 Sobel kernels normalized
  by 1/8. Produces signed `gx`, `gy` plus a magnitude convenience.
- **Structure-tensor flow** (`flow.cpp`). Builds the 2×2 tensor
  `J = [Gx² GxGy; GxGy Gy²]`, smooths it with a separable box blur,
  and takes the eigenvector of the smaller eigenvalue at each pixel —
  that's the direction _along_ texture, which is what wash strokes
  should follow. Falls back to (0, 0) on degenerate (perfectly flat)
  pixels; the planner treats zero-flow as "no preferred direction" and
  uses the previous step's vector.
- **Value bands** (`luminance.cpp`). Quantizes luminance into N equal
  bands (default 4: shadows / mid-low / mid-high / lights). The pencil
  hatching pass fires only on pixels in the darkest 1–2 bands.
- **Top-level `analyze()`** (`analyze.cpp`) bundles all of the above
  into an `Analysis` struct: `(rgb, luminance, edges, flow, value_band)`
  per pixel. Cheap to keep in memory — about 6 floats + 1 byte per pixel
  on top of the source RGBA.

### Stage 6 — Stroke planner ✅ implemented

Goal: turn an `Analysis` + a working pigment palette into a `RenderPlan`
the host can replay onto a `Canvas`.

- **`RenderPlan` value type** (`planner/render_plan.hpp`). A list of
  `LayerSpec` entries (which layers to allocate on the canvas) and a
  list of `Step` entries (what to do with them). Steps are flat structs
  with an op discriminator — no `std::variant`, easy to dump for
  debugging. Pure data: holds tool _names_ (resolved by the executor),
  not preset references; no GPU resources.
- **Pigment-assignment + streamline planner** (`plan.cpp`):
  1. Allocate one pigment layer per palette entry.
  2. For each pixel, assign it to the nearest pigment by RGB distance to
     the pigment's swatch color. Per-pixel "wash strength" = darkness
     (`1 − luminance`) — darker pixels need more pigment.
  3. For each pigment, walk a grid of seed points; if the seed is
     assigned to that pigment and strength is above threshold, trace a
     streamline through the flow field and emit a watercolor stroke.
     Tracing handles the bidirectional eigenvector (flips on U-turn) and
     extends both forward and backward from the seed. Pressure along the
     stroke is sampled from the strength field — strokes naturally taper
     at value transitions.
  4. After each pigment's strokes, append a diffusion step.
- **Pencil hatching pass.** For the darkest band(s), emit short pencil
  strokes perpendicular to the flow field (i.e., along the gradient).
  Cross-contour hatching that follows the structure of the photo rather
  than a fixed angle.
- **`apply_plan(canvas, plan)`** in `host/apply_plan.cpp`: allocates the
  layers, then dispatches each step to the matching `Canvas` method,
  loading tool presets from disk on first use (cached). The host owns
  the planner-to-canvas glue so the planner stays GPU-free.

### Stage 7 — Full pipeline integration ✅ implemented

Goal: photo in → artwork out, end-to-end.

- **Photo pipeline CLI** (`host/main.cpp`): `broadstroke paint <image>
[--style NAME] [--substrate NAME] [--output PATH]
[--snapshot-every N] [--snapshot-prefix PREFIX]`. The old canned scenes
  remain available as `broadstroke scene <name>` and through the legacy
  positional form.
- **Image input** is handled by the analyzer loader: JPG/PNG/BMP decode via
  `stb_image`, sRGB → linear conversion, header pre-validation, and a
  dimension cap before decode allocation. The host box-filters arbitrary
  source photos so the longest working side is ≤ 1024 px.
- **Style-specific planners**: `watercolor`, `pencil_sketch`, and
  `pastel_drawing` all build a `RenderPlan` from the same `Analysis` data.
  Watercolor uses the three pigment presets plus optional pencil hatching;
  pencil sketch emits graphite hatching only; pastel drawing uses pastel KM
  pigment layers plus a smear pass along each stick's longest stroke.
- **Host orchestration** runs load → fit → analyze → plan → generate matching
  substrate → allocate `Canvas` → `applyPlan()` stroke execution → compose →
  write final image. This is the first default path where the analyzer,
  planner, GPU layer stack, and file output all participate together.
- **Crash-tolerant progress snapshots** are optional rather than continuous:
  `ApplyOptions::on_step` lets the CLI compose and write
  `<prefix>_NNNN.<ext>` every N plan steps (and always at the final step when
  snapshots are enabled), while also printing coarse progress for long plans.
- **Image output** now selects by extension through `host/io/writeImage()`:
  BMP uses the in-tree writer; PNG and JPG/JPEG use `stb_image_write`.

### Stage 8 — Polish and per-device tuning ✅ implemented

Goal: production quality and portable performance.

- **Per-device workgroup tuning.** `compute::Context` queries the selected
  Vulkan device name, `subgroupSize`, and `maxComputeWorkGroupInvocations`
  inside `compute/bindings/` so no Vulkan types leak outward. The shader
  build now emits default SPIR-V plus 8×8 and 16×16 local-size variants for
  the 2D canvas kernels. `Canvas::dispatch_2d()` asks the context which
  variant to use and computes matching group counts. Devices with small
  workgroup limits or 32-wide subgroups choose 8×8; wider/high-limit devices
  choose 16×16.
- **Validation diagnostics.** `paint` and `scene` accept `--validate`, and
  both commands accept `--device-info`. The flag prints the selected device,
  chosen local-size variant, subgroup size, max workgroup invocations, and
  whether `VK_LAYER_KHRONOS_validation` is installed. Kompute v0.9.0's own
  debug callback path remains compiled out because its `vk::DispatchLoaderDynamic`
  branch is incompatible with the current SDK defaults; Broadstroke keeps the
  diagnostic path explicit rather than silently enabling validation in every
  Release run.
- **UI skeleton.** `broadstroke ui [image]` opens a Windows preset explorer
  with an input path, style selector, substrate selector, validation toggle,
  render button, status line, and BMP canvas preview. It intentionally shells
  back into the same CLI render path so the window stays a thin preset
  exploration surface rather than a separate image editor.

---

## Architectural invariants (enforced now, not future work)

| Rule                                                                                             | Reason                                                                                                                                                                                   |
| ------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Only `compute/bindings/` includes `<kompute/Kompute.hpp>` or any Vulkan header                   | Keeps Vulkan types from spreading; every abstraction the rest of the code needs must be expressed at the bindings layer                                                                  |
| Every GPU kernel has a CPU mirror in `reference/`                                                | Makes kernel bugs debuggable; equivalence tests catch regressions before they reach the full pipeline                                                                                    |
| Tests assert GPU/CPU equivalence within a small tolerance                                        | Floating-point differences are expected; unbounded divergence is not. Use `floor(x + 0.5)` for integer sampling — bare `round()` differs between GPU (banker's) and C++ (away-from-zero) |
| 2D kernels compile 8×8 and 16×16 local-size variants; 1D kernels stay 64-wide                    | Runtime device profiling picks a safe variant without leaking Vulkan types outside `compute/bindings/`                                                                                   |
| Pass-based compose: each layer is an `apply_*` kernel that reads + writes the shared RGBA buffer | Adding a new medium is one new kernel, never an edit to a master compose                                                                                                                 |
| Stroke kernels accept `(offset_x, offset_y)` in their params                                     | Lets the host dispatch only the stroke's AABB instead of the full canvas                                                                                                                 |
| Strokes are auto-chunked at 64 segments per dispatch                                             | Keeps any single GPU job well under Windows TDR (~2 s)                                                                                                                                   |
| Source photographs are untrusted — validate headers and cap dimensions                           | Malformed input must not reach GPU memory allocation                                                                                                                                     |
