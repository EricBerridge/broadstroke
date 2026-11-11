#include "optics/pigment.hpp"

#include <functional>

namespace bs {
namespace {

float band_lo(int b) { return kLambdaMin + (kLambdaMax - kLambdaMin) * b / kBands; }
float band_hi(int b) { return kLambdaMin + (kLambdaMax - kLambdaMin) * (b + 1) / kBands; }

// Average a spectral curve over each band.
Spectrum band_average(const std::function<float(float)>& f) {
  Spectrum s;
  const int n = 12;
  for (int b = 0; b < kBands; ++b) {
    float lo = band_lo(b), hi = band_hi(b), acc = 0.f;
    for (int i = 0; i < n; ++i) acc += f(lo + (hi - lo) * (i + 0.5f) / n);
    s[b] = acc / n;
  }
  return s;
}

float sig(float nm, float center, float width) {  // rising logistic edge
  return 1.f / (1.f + std::exp(-(nm - center) / width));
}
float gauss(float nm, float center, float sigma) {
  return std::exp(-0.5f * sqr((nm - center) / sigma));
}

// CIE 1931 2-degree observer: multi-lobe Gaussian fit (Wyman, Sloan, Shirley 2013).
float lobe(float nm, float mu, float s1, float s2) {
  float t = (nm - mu) / (nm < mu ? s1 : s2);
  return std::exp(-0.5f * t * t);
}
float cie_x(float nm) {
  return 1.056f * lobe(nm, 599.8f, 37.9f, 31.0f) + 0.362f * lobe(nm, 442.0f, 16.0f, 26.7f) -
         0.065f * lobe(nm, 501.1f, 20.4f, 26.2f);
}
float cie_y(float nm) {
  return 0.821f * lobe(nm, 568.8f, 46.9f, 40.5f) + 0.286f * lobe(nm, 530.9f, 16.3f, 31.1f);
}
float cie_z(float nm) {
  return 1.217f * lobe(nm, 437.0f, 11.8f, 36.0f) + 0.681f * lobe(nm, 459.0f, 26.0f, 13.8f);
}

// CIE standard illuminant D65, relative power, 380-730 nm in 10 nm steps.
float d65(float nm) {
  static const float t[] = {49.98f, 54.65f, 82.75f, 91.49f, 93.43f, 86.68f, 104.86f, 117.01f,
                            117.81f, 114.86f, 115.92f, 108.81f, 109.35f, 107.80f, 104.79f, 107.69f,
                            104.41f, 104.05f, 100.00f, 96.33f, 95.79f, 88.69f, 90.01f, 89.60f,
                            87.70f, 83.29f, 83.70f, 80.03f, 80.21f, 82.28f, 78.28f, 69.72f,
                            71.61f, 74.35f, 61.60f, 69.89f};
  float f = std::clamp((nm - 380.f) / 10.f, 0.f, 34.999f);
  int i = static_cast<int>(f);
  return lerp(t[i], t[i + 1], f - i);
}

// Per-band weights that turn a reflectance spectrum into linear sRGB.
struct RgbMatrix {
  float m[3][kBands];
  float inv[kBands][3];  // minimum-norm lift from RGB offsets back to spectra
};

const RgbMatrix& rgb_matrix() {
  static const RgbMatrix M = [] {
    RgbMatrix r{};
    const int n = 24;
    for (int b = 0; b < kBands; ++b) {
      float lo = band_lo(b), hi = band_hi(b);
      float X = 0.f, Y = 0.f, Z = 0.f;
      for (int i = 0; i < n; ++i) {
        float nm = lo + (hi - lo) * (i + 0.5f) / n;
        float e = d65(nm);
        X += cie_x(nm) * e; Y += cie_y(nm) * e; Z += cie_z(nm) * e;
      }
      r.m[0][b] = 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
      r.m[1][b] = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
      r.m[2][b] = 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;
    }
    // A perfect reflector must be exactly white.
    for (int c = 0; c < 3; ++c) {
      float s = 0.f;
      for (int b = 0; b < kBands; ++b) s += r.m[c][b];
      for (int b = 0; b < kBands; ++b) r.m[c][b] /= s;
    }
    // inv = M^T (M M^T)^-1
    float g[3][3];
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) {
        g[i][j] = 0.f;
        for (int b = 0; b < kBands; ++b) g[i][j] += r.m[i][b] * r.m[j][b];
      }
    float det = g[0][0] * (g[1][1] * g[2][2] - g[1][2] * g[2][1]) -
                g[0][1] * (g[1][0] * g[2][2] - g[1][2] * g[2][0]) +
                g[0][2] * (g[1][0] * g[2][1] - g[1][1] * g[2][0]);
    float gi[3][3];
    gi[0][0] = (g[1][1] * g[2][2] - g[1][2] * g[2][1]) / det;
    gi[0][1] = (g[0][2] * g[2][1] - g[0][1] * g[2][2]) / det;
    gi[0][2] = (g[0][1] * g[1][2] - g[0][2] * g[1][1]) / det;
    gi[1][0] = (g[1][2] * g[2][0] - g[1][0] * g[2][2]) / det;
    gi[1][1] = (g[0][0] * g[2][2] - g[0][2] * g[2][0]) / det;
    gi[1][2] = (g[0][2] * g[1][0] - g[0][0] * g[1][2]) / det;
    gi[2][0] = (g[1][0] * g[2][1] - g[1][1] * g[2][0]) / det;
    gi[2][1] = (g[0][1] * g[2][0] - g[0][0] * g[2][1]) / det;
    gi[2][2] = (g[0][0] * g[1][1] - g[0][1] * g[1][0]) / det;
    for (int b = 0; b < kBands; ++b)
      for (int c = 0; c < 3; ++c) {
        r.inv[b][c] = 0.f;
        for (int k = 0; k < 3; ++k) r.inv[b][c] += r.m[k][b] * gi[k][c];
      }
    return r;
  }();
  return M;
}

// Builds a pigment from its scattering level and K/S ratio curve.
Pigment from_ks(const char* name, float s_level, const std::function<float(float)>& k_over_s) {
  Pigment p;
  p.name = name;
  Spectrum ratio = band_average(k_over_s);
  for (int b = 0; b < kBands; ++b) {
    p.S[b] = s_level;
    p.K[b] = s_level * ratio[b];
  }
  return p;
}

}  // namespace

Rgb spectrum_to_rgb(const Spectrum& r) {
  const RgbMatrix& M = rgb_matrix();
  Rgb c;
  for (int b = 0; b < kBands; ++b) {
    c.r += M.m[0][b] * r[b];
    c.g += M.m[1][b] * r[b];
    c.b += M.m[2][b] * r[b];
  }
  c.r = std::max(c.r, 0.f); c.g = std::max(c.g, 0.f); c.b = std::max(c.b, 0.f);
  return c;
}

Spectrum rgb_to_spectrum(Rgb c) {
  // Start from a flat spectrum at the color's mean level and add the
  // minimum-norm correction that reproduces the RGB exactly.
  const RgbMatrix& M = rgb_matrix();
  float base = (c.r + c.g + c.b) / 3.f;
  float d[3] = {c.r - base, c.g - base, c.b - base};
  Spectrum s;
  for (int b = 0; b < kBands; ++b) {
    float v = base + M.inv[b][0] * d[0] + M.inv[b][1] * d[1] + M.inv[b][2] * d[2];
    s[b] = std::clamp(v, 0.002f, 1.f);
  }
  return s;
}

// The spectra below are parametric fits to the published reflectance curves
// of the named pigments: where each absorbs, how sharply its edge rises, how
// dark its masstone is, and how strongly it scatters (its opacity). S is in
// 1/mm of tube paint: at S ~ 150/mm a 0.1 mm film of white hides the ground.
Pigment make_pigment(PigmentId id, float strength, float scatter) {
  Pigment p;
  switch (id) {
    case PigmentId::TitaniumWhite:
      // Near-perfect scatterer with a slight violet absorption edge.
      p = from_ks("titanium white", 170.f,
                  [](float nm) { return 0.0030f + 0.035f * (1.f - sig(nm, 412.f, 8.f)); });
      break;
    case PigmentId::CadmiumYellow:
      // Opaque; absorbs blue, sharp edge near 500 nm.
      p = from_ks("cadmium yellow", 95.f,
                  [](float nm) { return 0.005f + 12.f * (1.f - sig(nm, 492.f, 8.f)); });
      p.density = 1.6f; p.granulation = 0.25f; p.staining = 0.35f;
      break;
    case PigmentId::YellowOchre:
      // Earth yellow: gradual rise from violet to red.
      p = from_ks("yellow ochre", 70.f,
                  [](float nm) { return 0.20f + 6.5f * (1.f - sig(nm, 492.f, 24.f)); });
      p.density = 1.4f; p.granulation = 0.55f; p.staining = 0.2f;
      break;
    case PigmentId::CadmiumRed:
      // Opaque; absorbs everything below ~600 nm.
      p = from_ks("cadmium red", 80.f,
                  [](float nm) { return 0.006f + 16.f * (1.f - sig(nm, 579.f, 6.f)); });
      p.density = 1.6f; p.granulation = 0.2f; p.staining = 0.5f;
      break;
    case PigmentId::AlizarinCrimson:
      // Transparent lake: dark masstone, bluish-pink undertone in tints.
      p = from_ks("alizarin crimson", 11.f, [](float nm) {
        return 0.06f + 46.f * (1.f - sig(nm, 600.f, 12.f)) * (1.f - 0.45f * gauss(nm, 420.f, 26.f));
      });
      p.density = 0.6f; p.granulation = 0.0f; p.staining = 0.85f;
      break;
    case PigmentId::UltramarineBlue:
      // Transparent, violet-leaning blue with a broad absorption band near 600 nm.
      p = from_ks("ultramarine blue", 17.f,
                  [](float nm) { return 0.55f + 24.f * gauss(nm, 600.f, 62.f); });
      p.density = 1.5f; p.granulation = 0.9f; p.staining = 0.15f;
      break;
    case PigmentId::PhthaloBlue:
      // Very strong transparent green-leaning blue.
      p = from_ks("phthalo blue", 8.f, [](float nm) {
        return 1.6f + 110.f * gauss(nm, 640.f, 68.f) + 22.f * (1.f - sig(nm, 428.f, 10.f));
      });
      p.density = 0.5f; p.granulation = 0.0f; p.staining = 0.95f;
      break;
    case PigmentId::PhthaloGreen:
      // Very strong transparent blue-leaning green: narrow window near 505 nm.
      p = from_ks("phthalo green", 8.f,
                  [](float nm) { return 1.6f + 100.f * (1.f - gauss(nm, 505.f, 36.f)); });
      p.density = 0.5f; p.granulation = 0.0f; p.staining = 0.95f;
      break;
    case PigmentId::BurntSienna:
      // Transparent red-brown earth.
      p = from_ks("burnt sienna", 30.f,
                  [](float nm) { return 1.0f + 12.f * (1.f - sig(nm, 592.f, 22.f)); });
      p.density = 1.4f; p.granulation = 0.6f; p.staining = 0.3f;
      break;
    case PigmentId::BurntUmber:
      // Dark neutral brown earth.
      p = from_ks("burnt umber", 26.f,
                  [](float nm) { return 3.6f + 13.f * (1.f - sig(nm, 600.f, 42.f)); });
      p.density = 1.5f; p.granulation = 0.7f; p.staining = 0.3f;
      break;
    case PigmentId::IvoryBlack:
      p = from_ks("ivory black", 6.f, [](float nm) { return 62.f - 0.02f * (nm - 400.f); });
      p.density = 0.8f; p.granulation = 0.35f; p.staining = 0.5f;
      break;
  }
  for (int b = 0; b < kBands; ++b) {
    p.K[b] *= strength;
    p.S[b] *= strength * scatter;
  }
  return p;
}

Palette oil_palette() {
  Palette pal;
  pal.name = "oil";
  pal.add(make_pigment(PigmentId::TitaniumWhite), true);
  pal.add(make_pigment(PigmentId::CadmiumYellow));
  pal.add(make_pigment(PigmentId::YellowOchre));
  pal.add(make_pigment(PigmentId::CadmiumRed));
  pal.add(make_pigment(PigmentId::AlizarinCrimson));
  pal.add(make_pigment(PigmentId::UltramarineBlue));
  pal.add(make_pigment(PigmentId::PhthaloBlue));
  pal.add(make_pigment(PigmentId::BurntUmber));
  return pal;
}

Palette watercolor_palette() {
  // Surface loads: 1.0 of a pigment is a saturated wash. Gum-arabic films are
  // thin and the pigments are chosen for transparency, so scattering is low.
  const float k = 1.f / 230.f, sc = 0.45f;
  Palette pal;
  pal.name = "watercolor";
  pal.add(make_pigment(PigmentId::CadmiumYellow, k, sc));
  pal.add(make_pigment(PigmentId::YellowOchre, k * 1.6f, sc));
  pal.add(make_pigment(PigmentId::CadmiumRed, k, sc));
  pal.add(make_pigment(PigmentId::AlizarinCrimson, k * 0.42f, sc));
  pal.add(make_pigment(PigmentId::UltramarineBlue, k * 0.8f, sc));
  pal.add(make_pigment(PigmentId::PhthaloBlue, k * 0.22f, sc));
  pal.add(make_pigment(PigmentId::PhthaloGreen, k * 0.22f, sc));
  pal.add(make_pigment(PigmentId::BurntUmber, k * 1.3f, sc));
  return pal;
}

Palette stick_palette() {
  // Pure pigment; sticks dilute these in wax, clay or chalk binder.
  const float k = 2.2f;
  Palette pal;
  pal.name = "stick";
  pal.add(make_pigment(PigmentId::TitaniumWhite, k), true);
  pal.add(make_pigment(PigmentId::CadmiumYellow, k));
  pal.add(make_pigment(PigmentId::CadmiumRed, k));
  pal.add(make_pigment(PigmentId::AlizarinCrimson, k * 0.6f));
  pal.add(make_pigment(PigmentId::UltramarineBlue, k * 0.8f));
  pal.add(make_pigment(PigmentId::PhthaloGreen, k * 0.35f));
  pal.add(make_pigment(PigmentId::BurntUmber, k));
  pal.add(make_pigment(PigmentId::IvoryBlack, k));
  return pal;
}

}  // namespace bs
