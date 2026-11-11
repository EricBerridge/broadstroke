This file covers: how an intended stroke becomes the motion of a tool. Index: [README.md](README.md).

### 2.5 The hand (`src/motion/`)

A gesture is what the artist intends: a path on the canvas, an effort from 0
to 1 (constant or per point), a tempo, and whether the tool lands at the
start and lifts at the end. `perform` turns it into a sequence of tool
poses, sampled 400 times a second: position, applied effort, tilt, the
direction the handle leans, the direction a flat tool faces, and speed.

**Speed**

- On a long straight run the hand moves at its cruising speed times the
  gesture's tempo.
- Curvature slows it by the two-thirds power law: speed falls with the cube
  root of curvature, down to a floor of 7% of cruising speed. Curves gentler
  than the hand's curve radius do not slow it.
- Acceleration is finite, in both directions: the hand reaches no speed it
  could not have accelerated to, and brakes in time for every corner. Short
  strokes are therefore slower than long ones.
- A stroke that lands starts nearly from rest. A stroke that lifts ends in a
  flick, not a stop.

**Position**

- The hand tracks a moving aim point as a damped second-order system (a
  natural frequency and a damping ratio), so it lags the intended path
  slightly and rounds corners.
- A slow random drift of aim and an 8-12 Hz tremor are added. Both are
  given in millimeters, so they matter more for small tools.
- When lifting, the hand follows through a short way past the end of the
  path.

**Effort**

- Effort is loaded over a short time after touchdown and unloaded over the
  last few millimeters before lift-off.
- It fluctuates slightly around the intended value.

**Lean**

- A brush leans toward the direction of travel and re-orients with a lag; a
  flat brush is rolled to face the travel, more slowly still.
- A pencil or stick keeps the lean its grip gives it.

**Requirements**

- All randomness comes from the random stream passed in; the same gesture,
  hand and stream give the same poses.
- Hand parameters are per tool: a large brush cruises faster and wanders
  more than a small one; dry tools use a fixed grip.
- A path of fewer than two points, or shorter than a micrometer, gives no
  poses.

**Acceptance** (`tests/test_motion.cpp`): a stroke starts and ends where
intended; short strokes are slower than long ones; the hand slows through a
corner; lean follows travel for brushes and stays put for pencils; the
motion is deterministic.
