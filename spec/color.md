This file covers: how color is represented and computed: spectral pigments, film optics, and the color-matching mixer. Index: [README.md](README.md).

### 2.2 Color (`src/optics/`)

- Reflectance is represented in 8 bands over 400-700 nm; a film holds up to 8
  pigments, each with absorption K and scattering S per band.
- The color of a film over a background is the Kubelka-Munk two-flux solution
  for a finite layer. Limits that must hold: no film gives the background; a
  thick film gives the masstone; zero scattering gives Beer-Lambert over two
  passes.
- Spectra convert to linear sRGB with the CIE 1931 observer under D65; a
  perfect reflector is exactly white.
- The `Mixer` finds pigment proportions for a wanted color: an opaque body
  color (oil) from one to three tube colors plus white, or a transparent
  glaze over an existing reflectance (watercolor). A glaze can only darken.
  Out-of-gamut targets get the nearest achievable mix.
- Palettes: oil (titanium white, cadmium yellow, yellow ochre, cadmium red,
  alizarin crimson, ultramarine, phthalo blue, burnt umber); watercolor (the
  same colors without white, plus phthalo green); sticks (white, yellow, red,
  crimson, ultramarine, phthalo green, umber, black).
