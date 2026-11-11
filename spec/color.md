This file covers: how color is represented and computed: spectral pigments, film optics, and the color-matching mixer. Index: [README.md](README.md).

### 2.2 Color (`src/optics/`)

**Representation**

- Reflectance is represented in 8 bands over 400-700 nm; a film holds up to 8
  pigments, each with absorption K and scattering S per band.
- For paint and sticks, K and S are per millimeter of film. For watercolor
  they are per unit of surface load, where a load of 1.0 is a strong wash.
- Color is never blended as RGB. Wherever two materials meet, their pigment
  amounts are combined and the color is computed from the result.

**Film optics**

- The color of a film over a background is the Kubelka-Munk two-flux solution
  for a finite layer, per band:
  `R = (1 - Rg (a - b coth(b S d))) / (a - Rg + b coth(b S d))`, with
  `a = 1 + K/S` and `b = sqrt(a^2 - 1)`, where `K d` and `S d` are the film's
  absorption and scattering times its thickness and `Rg` is the background.
- Limits that must hold: no film gives the background; a thick film gives the
  masstone `a - b`; zero scattering gives Beer-Lambert over two passes,
  `Rg exp(-2 K d)`.
- The result must be a finite number for every film, however thin. For
  `b S d` below 1e-3 the series `b coth(x) = 1/(S d) + b^2 S d / 3` is used,
  because the closed form divides by `1 - exp(-2x)`, which rounds to zero
  there. (An invalid color is written out as a black pixel.)
- The optical thickness of a film is the sum over its pigments of amount
  times K and S. Pigment amounts at or below zero contribute nothing.

**Seeing color**

- Spectra convert to linear sRGB with the CIE 1931 observer under D65; a
  perfect reflector is exactly white.
- Every color comparison the artist makes is a distance in Oklab.
- Output images are encoded to 8-bit sRGB.

**Pigments and palettes**

- A pigment is defined by parametric K and S curves, a strength, and, for
  watercolor behavior, a density, a granulation and a staining value.
- Palettes: oil (titanium white, cadmium yellow, yellow ochre, cadmium red,
  alizarin crimson, ultramarine, phthalo blue, burnt umber); watercolor (the
  same colors without white, plus phthalo green); sticks (white, yellow, red,
  crimson, ultramarine, phthalo green, umber, black).

**The mixer**

- The `Mixer` finds pigment proportions for a wanted color: an opaque body
  color (oil) from one to three tube colors plus white, or a transparent
  glaze over an existing reflectance (watercolor).
- A glaze can only darken: a target lighter than what is there returns no
  pigment. The total amount of a glaze is capped by the caller.
- Out-of-gamut targets get the nearest achievable mix.
- Opaque matches are cached per target color. The mixer is used from one
  thread.

**Acceptance** (`tests/test_optics.cpp`, `broadstroke study pigments`)

- A perfect reflector is white; RGB to spectrum and Oklab conversions round
  trip.
- The film limits above hold, and reflectance darkens monotonically with
  thickness over white.
- Yellow and blue mix to green; white lightens a color and keeps its hue.
- Two glazes compose multiplicatively; a glaze match never lightens.
- The mixer matches in-gamut colors closely.
