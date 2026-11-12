This file covers: tools that leave solid material on the paper: soft pastel, pastel pencil, stump and eraser. Index: [README.md](README.md).

### 2.7 Dry media (`src/media/dry_tool.*`)

**The tip**

- The tip is a height map: a sharpened cone held at an angle, or the side
  of a stick (a bar with rounded edges, rocked slightly onto its leading
  edge). The apex of a fresh cone is a small sphere, not a singularity.
- Material leaves the tip as it is laid down, cell by cell, so a point wears
  to a facet and lines broaden until the tool is sharpened. Sharpening
  restores the fresh shape.

**Contact**

- Each step solves for the tool height at which contact pressure summed
  over the footprint equals the hand's force. Pressure in a cell is the
  tool's stiffness times the depth by which tip and paper interfere (an
  elastic foundation).
- Contact comes on gradually: the depth passes through a smooth ramp whose
  width is 12% of the surface's relief, because a cell is itself rough below
  the grid scale. Beyond 8 widths either side the ramp is exactly zero or
  exactly linear.
- The height is found by Newton's method, starting from the previous step's
  penetration, with at most 14 iterations and steps of at most 0.05 mm.
- Only cells close enough to the paper to carry pressure enter the solve;
  the rest contribute exactly nothing. The ramp is evaluated eight cells at
  a time.
- A stiff tool barely indents and touches only the crowns of the grain; a
  soft one conforms and reaches the valleys.

**What happens where there is pressure**

- **Wear (Archard):** material transfers in proportion to pressure times
  distance slid, times the surface's grip, and less as the surface becomes
  coated: transfer falls toward a residual as the film approaches the
  thickness at which the surface stops taking more. The same volume is
  removed from the tip.
- **Flow:** soft media push a share of each deposit into the lowest
  neighboring cell.
- **Burnishing:** under pressure, material sitting on a crown is sheared
  into the lowest neighboring valley, so repeated heavy passes fill the
  tooth. No ready-made tool burnishes at present.
- **Smear:** a tool can pick up part of what is there and lay it down again
  along its path (a stump or a fingertip, and to a small degree every stick).
- **Lift:** an eraser removes a share of what is there.

**Tools**

| Tool | Tip | Stiffness (N/mm^3) | Wear (mm^2/N) | Pigment share | Notes |
| --- | --- | --- | --- | --- | --- |
| Soft pastel, side | 9 x 16 mm bar | 0.35 | 0.2 | 0.55 | broad marks |
| Soft pastel, corner | 9 mm blunt cone | 6 | 0.03 | 0.55 | narrower marks |
| Pastel pencil | 4 mm cone | 60 | 7e-3 | 0.5 | detail; sharpened as it wears |
| Stump / fingertip | blunt cone, any width | 3 | none | - | smears only |
| Kneaded eraser | blunt cone, any width | 3 | none | - | lifts only |

**Requirements**

- A broad tip takes larger steps than a fine one (from half a cell up to two
  and a half), in proportion to its contact patch.
- What the tip loses equals what the paper gains.
- The tool marks the cells it changes step by step (spec 2.4). Several tools
  may work at once on regions that do not overlap (spec 2.12).
- The same tool, in the same state, with the same motion gives the same mark.

**Acceptance** (`tests/test_dry.cpp`, `broadstroke study dry`): tip wear
equals material on the paper; a softer stick and harder pressure deposit
more; a worn point draws a broader line; light marks sit on the crowns of
the grain; a stump moves material without adding any, and an eraser removes
it.
