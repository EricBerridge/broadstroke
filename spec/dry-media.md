This file covers: tools that leave solid material on the paper: colored pencil, pastel, stump and eraser. Index: [README.md](README.md).

### 2.7 Dry media (`src/media/dry_tool.*`)

- The tip is a height map: a sharpened cone held at an angle, or the side of
  a stick. Each step solves for the tool height at which contact pressure
  (elastic foundation, softened below the grid scale) summed over the
  footprint equals the hand's force.
- Where there is pressure and sliding, material transfers in proportion to
  pressure times distance (Archard wear), less as the surface becomes coated.
  The same volume is removed from the tip, so a point wears to a facet and
  lines broaden until the tool is sharpened.
- Soft media push part of each deposit into neighboring valleys and, under
  repeated pressure, burnish existing material down into the tooth.
- Tools: colored pencil; soft pastel (side, corner) and pastel pencil; a
  stump that drags material; a kneaded eraser that lifts it.
