This file covers: how an intended stroke becomes the motion of a tool. Index: [README.md](README.md).

### 2.5 The hand (`src/motion/`)

A gesture (intended path, effort, tempo) becomes a sequence of tool poses:
- speed limited by curvature (two-thirds power law) and by finite
  acceleration, so short strokes are slower than long ones and the hand
  brakes into corners;
- position follows the aim point as a damped second-order system, with slow
  aiming drift and 8-12 Hz tremor;
- effort is loaded over a touchdown time and unloaded over the last few
  millimeters, with a short follow-through;
- the tool's lean follows the direction of travel with a lag (brushes) or is
  fixed by the grip (pencils and sticks).
