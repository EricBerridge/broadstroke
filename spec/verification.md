This file covers: how the engine is verified, and the rules for determinism, parallel execution and arithmetic. Index: [README.md](README.md).

### 2.11 Verification

**Tests** (`tests/`, target `broadstroke_tests`, 55 tests)

- Tests assert physical invariants: conservation, limits, monotonic
  responses, determinism. There are no pixel comparisons against stored
  images.
- Each area's acceptance list names its tests: color (spec 2.2), surfaces
  and canvas (2.3, 2.4), the hand (2.5), the brush (2.6), dry media (2.7),
  watercolor (2.8), the artists (2.9), scene understanding (2.13).
- The artist tests run miniatures at a realistic scale (0.25 mm per cell),
  so tools work at their real size.

**Studies** (`broadstroke study <name> <out>`)

- Five scripted scenes show one physical model each, without an artist:
  pigments, surfaces, brush, dry, watercolor. Reference renders are kept in
  `docs/studies/`.
- A change to a physical model is judged by its study; a change to an
  artist by a real photograph. Tests cannot tell whether a drawing is good.

**Checking a change to the engine**

A change falls into one of two kinds (spec 3 explains why there is nothing
in between):

- **Output-preserving.** Every image the program produces is byte-identical
  before and after. This is checked by rendering all three media, at a small
  and at the full size, and all five studies, with both builds, to a
  lossless format, and comparing the files. A lossy format can hide a
  difference.
- **Output-changing.** A study should differ only in the last digits where
  the change is purely numerical; a painting comes out as a different
  realization. It is judged by looking: the picture before and after, a
  full-resolution detail, and, for scale, the same picture from a different
  seed.

Timings are compared by alternating runs of the two builds on the same
machine in one sitting; times drift by several percent over a day.

### 2.12 Determinism, parallel execution and arithmetic

**Determinism**

- The same photo, scene file, settings and seed give a bit-identical artwork
  on the same build of the program, whatever the number of threads and
  however they are scheduled. The scene file is input: the models that
  wrote it are outside this guarantee, and running them again may write a
  different file (spec 2.13).
- Every random choice is drawn from a seeded generator (`bs::Rng`, PCG32).
  Work that may run in parallel with other work draws from a stream of its
  own, seeded before the parallel work starts.
- Nothing in the result may depend on the clock. (Progress reporting is
  throttled by time, which changes only when the caller is told, not what
  is made.)

**Parallel execution** (`src/core/parallel.*`)

- `parallel_for` splits a range into chunks and runs them on a pool of one
  worker per hardware thread less one, plus the caller. Chunks must write
  disjoint data; under that rule the result equals a serial run. Nested
  calls run serially on the calling thread.
- A job wakes only as many workers as it has chunks to share, and the caller
  waits only for workers that joined. An idle worker spins for 100
  microseconds before it sleeps (spec 3).
- What runs in parallel: the watercolor step, by tiles, when enough tiles
  are active; the planning and the strokes of the pastel artist's waves
  (spec 2.9); re-rendering a large changed region; drying a layer; the
  final render; and the whole-image passes of observation (blurs, the flow
  field, the tone target).
- What does not: the strokes of any one tool (spec 3), and therefore oil.
- Code that marks canvas cells from several threads uses the two-part
  marking of spec 2.4.

**Arithmetic**

- A processor with AVX2 is required.
- A spectrum and a pigment vector are eight floats, one AVX register
  (`src/core/simd.hpp`). Additions, subtractions, multiplications and
  divisions on them are done lane by lane, in the order a scalar loop would
  do them, with nothing fused or reordered.
- Three functions are approximations by design, each accurate to the stated
  level: `exp8` and `log8` (about one unit in the last place), and the
  brush's coat-thinning table (about 1e-5). They are used for film
  reflectance, pigment settling and the dry tools' contact ramp.
- The build must not enable fast-math or floating-point contraction. (The
  MSVC build does not. Other compilers are not verified, spec 4.)
