This file covers: water and pigment on paper. Index: [README.md](README.md).

### 2.8 Watercolor (`src/media/watercolor.*`)

Per cell: free surface water, paper saturation, suspended pigment, settled
pigment (the canvas film). Each time step:
- water levels along its free surface and cannot advance onto dry paper
  until the head exceeds the contact-line pinning;
- suspended pigment moves with the water and diffuses between wet cells;
- the paper absorbs water and dampness wicks through the sheet;
- water evaporates, faster within two cells of the edge of a wet area;
- pigment leaves the water at the paper surface: heavy pigments settle,
  more in valleys (granulation); staining pigments bind evenly; settled
  non-staining pigment is partly re-suspended while the cell is wet.
Pigment is conserved; water only leaves by evaporation.
