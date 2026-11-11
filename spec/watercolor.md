This file covers: water and pigment on paper. Index: [README.md](README.md).

### 2.8 Watercolor (`src/media/watercolor.*`)

A wash is a shallow film of water lying on, and soaking into, the sheet.
None of its effects is drawn; they come out of the transport below.

**State, per cell**

- free water on the surface (mm);
- saturation of the paper beneath it (0 to 1);
- pigment suspended in the water;
- pigment settled on the fibers, which is the canvas film (spec 2.4).

**One time step** (0.3 s of simulated time)

- **Flow:** water levels along its free surface, which is its depth plus a
  share of the paper's relief, so it pools in the valleys of the grain. It
  cannot advance onto dry paper until the head exceeds the contact-line
  pinning (0.18 mm); onto damp paper it spreads freely. A cell never loses
  more than half its water in a step.
- **Pigment transport:** suspended pigment moves with the water and diffuses
  between wet neighbors, down the concentration gradient.
- **Absorption and wicking:** the paper drinks from the film at a rate set
  by the surface's absorbency, and dampness creeps outward through the
  sheet.
- **Evaporation:** water evaporates, faster at the rim of a wet area, where
  vapor escapes sideways as well as upward. The effect reaches two cells in
  from the edge. Water flows outward to replace it and carries pigment with
  it, which dries as a darker rim.
- **Settling and staining:** pigment leaves the water at the paper surface,
  so the share taken falls with the depth of the water. Heavy pigments
  settle, more in valleys in proportion to their granulation; staining
  pigments bind evenly, crown or valley.
- **Lifting:** while a cell holds enough water, settled non-staining pigment
  is partly taken up again.
- **Drying out:** a cell whose water is gone leaves whatever was suspended
  where it is, and its dampness then fades.

**Requirements**

- Pigment is conserved. Water only leaves by evaporation.
- A step computes every cell from the state before the step, so the result
  does not depend on the order cells are visited or on threads (spec 2.12).
- The sheet is divided into tiles of 32 x 32 cells. Free water and suspended
  pigment exist only in regions the simulation has been told about
  (`touch`), and a step does the full update only for cells in or next to
  water; elsewhere only dampness changes. These are savings, not
  approximations: the cells left out would have computed the same values.
- `advance(seconds)` steps the simulation. When nothing is wet, time only
  dries the damp paper, without stepping.
- `dry()` runs until no free water remains, for at most four minutes of
  simulated time, then evaporates the remainder where it stands.
- Wherever pigment may have moved, the canvas is told (spec 2.4), including
  in an area whose last water has just gone.
- The brush that works in water is the bristle brush of spec 2.6.

**Acceptance** (`tests/test_watercolor.cpp`, `broadstroke study watercolor`):
pigment is conserved while water only leaves; a wash on dry paper keeps its
shape; color bleeds into wet paper but not onto dry; a wash dries with a
darker rim; granulating pigment settles into the valleys. The study also
shows glazing (blue over dried yellow gives green) and dry brush.
