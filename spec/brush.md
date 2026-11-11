This file covers: the bristle brush and the paint it carries, used for oil and (wet) for watercolor. Index: [README.md](README.md).

### 2.6 Bristle brush and paint (`src/media/bristle_brush.*`)

- A brush is a bundle of individually simulated bristles (flat, filbert or
  round), each with its own length, stiffness, paint volume and pigment
  composition. A large round is capped at about 30 bristles across; beyond
  that each simulated bristle stands for a lock of hairs.
- Pressing the brush compresses bristles according to their length and the
  ferrule's lean. A compressed bristle lies on the canvas over a length given
  by rigid-chord geometry and trails its anchor like a towed line, pulled
  back in line by its stiffness.
- Each bristle exchanges paint with every cell it slides over: it leaves a
  film whose thickness falls as the bristle empties and rises in the valleys
  of the surface; it reaches a cell only if the bristle or its carried film
  bridges the gap, so a nearly empty brush at light pressure paints only the
  crowns of the weave; it scrapes excess wet paint, taking part up and
  ploughing part aside; films in contact blend both ways.
- Bristles refill from the brush's belly, which also dilutes what they pick
  up. Paint volume and pigment are conserved between brush and canvas.
- The same brush works in water (`WetSink`): it then deposits water and
  suspended pigment into the watercolor simulation.
