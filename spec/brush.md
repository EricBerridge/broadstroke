This file covers: the bristle brush and the paint it carries, used for oil and (wet) for watercolor. Index: [README.md](README.md).

### 2.6 Bristle brush and paint (`src/media/bristle_brush.*`)

**The brush**

- A brush is a bundle of individually simulated bristles (flat, filbert or
  round), each with its own length, stiffness, paint volume and pigment
  composition. Lengths and stiffnesses vary slightly from bristle to bristle.
- Bristles are spaced a little under one cell apart across the brush. A flat
  has two to five rows through its thickness; a filbert's outer bristles are
  shorter; a round's outer hairs are shorter by its taper, so it comes to a
  point.
- A large round is capped at about 30 bristles across; beyond that each
  simulated bristle stands for a lock of hairs.
- Behind the bristle tips is the belly: one reservoir of paint that refills
  the tips and dilutes whatever they pick up.

**Geometry of a step**

- Pressing the brush compresses each bristle according to its length and to
  the ferrule's lean: the leading side of a leaning brush sits lower and is
  compressed more. A bristle that is not compressed does not touch.
- Tips spread under compression, and gather into tufts as their paint runs
  low.
- A compressed bristle lies on the canvas over a length given by rigid-chord
  geometry. Its tip is held by friction and towed by the ferrule, and its
  stiffness pulls it back in line behind the handle.
- The brush advances at most one cell per step, so each tip visits every
  cell along its track. The exchange and the towing are both scaled by the
  distance moved, so the mark does not depend on the step (spec 3).

**Paint exchange**

Each bristle exchanges paint with up to five cells along the length it lies
on, in proportion to how far it slid (or was pressed in, for a dab) and how
hard it presses.

- It reaches a cell only if the bristle, or the film it carries, bridges
  the gap down to the surface. Under light pressure the tip rides on the
  crowns of the weave, so a nearly empty brush paints only the crowns (dry
  brush).
- It leaves a film whose thickness falls as the bristle empties (as its
  fill to the power 0.7, read from a table accurate to about 1e-5), is
  thinner under heavy pressure, and is thicker in valleys than on crowns, so
  a full coat levels the surface.
- Where the canvas already holds more than that film, it scrapes the excess:
  part rides up into the bristle, part is ploughed aside or ahead as a ridge.
- Films in contact blend both ways.
- Tips refill from the belly as the brush travels, and paint circulates
  between tip and belly.
- Paint volume and pigment are conserved between brush and canvas.

**Loading**

- `load` adds paint of a given recipe and thinner share to the belly and
  wets every tip evenly as far as the paint goes. `wipe` removes a share of
  everything the brush holds.
- The artist loads at least enough to wet the tips: an underloaded brush
  leaves specks, not a stroke.

**In water** (`WetSink`)

- The same brush works for watercolor: it then deposits water and suspended
  pigment into the watercolor simulation's fields instead of the canvas
  film, and reads the water depth as the film it works in.
- In water it does not scrape or plough.

**Requirements**

- The brush marks the cells it changes step by step (spec 2.4), and reports
  the region its last stroke touched.
- The same brush, load and motion give the same mark.

**Acceptance** (`tests/test_brush.cpp`, `broadstroke study brush`): paint is
conserved between brush and canvas; a small load runs dry along the stroke;
pressure widens the mark; a brush picks up wet paint it crosses; a dry brush
catches the crowns of the weave first.
