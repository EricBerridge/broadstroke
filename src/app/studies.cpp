#include "app/studies.hpp"

#include <cstdio>

#include "canvas/canvas.hpp"
#include "media/bristle_brush.hpp"
#include "motion/hand.hpp"
#include "optics/mixer.hpp"

namespace bs {
namespace {

void fill_rect(Image& img, int x0, int y0, int x1, int y1, Rgb c) {
  for (int y = std::max(y0, 0); y < std::min(y1, img.h); ++y)
    for (int x = std::max(x0, 0); x < std::min(x1, img.w); ++x) img(x, y) = c;
}

// Pigment chart. Per palette: each pigment as masstone, as tints with white
// (or as thinning washes when the palette has no white), then two-pigment
// mixing ladders.
bool study_pigments(const std::string& out) {
  const int sw = 44, pad = 4;
  std::vector<Palette> pals = {oil_palette(), watercolor_palette(), stick_palette()};
  Image img(sw * 12 + 40, 3 * (8 * (sw + pad) + 5 * (sw + pad) + 30), Rgb{0.2f, 0.2f, 0.2f});
  int y = 10;
  Spectrum paper = rgb_to_spectrum({0.9f, 0.89f, 0.85f});
  for (const Palette& pal : pals) {
    std::printf("%s palette masstones (sRGB 0-255):\n", pal.name.c_str());
    for (int i = 0; i < pal.n; ++i) {
      PigVec f{};
      f[i] = 1.f;
      Rgb c = spectrum_to_rgb(pal.masstone(f));
      std::printf("  %-18s %3.0f %3.0f %3.0f\n", pal.pig[i].name.c_str(), linear_to_srgb(c.r) * 255.f,
                  linear_to_srgb(c.g) * 255.f, linear_to_srgb(c.b) * 255.f);
    }
    for (int i = 0; i < pal.n; ++i) {
      for (int k = 0; k < 11; ++k) {
        Spectrum r;
        if (pal.white >= 0 && pal.name != "stick") {
          PigVec f{};
          float wr = k == 0 ? 0.f : std::pow(2.f, static_cast<float>(k) - 3.f);
          f[i] = 1.f;
          if (i != pal.white) f[pal.white] = wr;
          r = pal.masstone(f);
        } else {
          // Film of increasing load over paper.
          PigVec a{};
          float unit = pal.name == "stick" ? 0.02f : 2.f;
          a[i] = unit * std::pow(0.6f, static_cast<float>(10 - k));
          Spectrum kd, sd;
          pal.film(a, kd, sd);
          r = km_over(kd, sd, paper);
        }
        fill_rect(img, 20 + k * (sw + 2), y, 20 + k * (sw + 2) + sw, y + sw, spectrum_to_rgb(r));
      }
      y += sw + pad;
    }
    // Mixing ladders between interesting pairs.
    int pairs[5][2] = {{1, 5}, {1, 3}, {3, 5}, {4, 6}, {2, 7}};
    if (pal.name == "watercolor") {
      int p2[5][2] = {{0, 5}, {0, 2}, {3, 4}, {4, 7}, {1, 6}};
      for (int i = 0; i < 5; ++i) { pairs[i][0] = p2[i][0]; pairs[i][1] = p2[i][1]; }
    }
    for (auto& pr : pairs) {
      for (int k = 0; k < 11; ++k) {
        float t = k / 10.f;
        PigVec f{};
        Spectrum r;
        if (pal.white >= 0 && pal.name != "stick") {
          f[pr[0]] = 1.f - t; f[pr[1]] = t; f[pal.white] = 1.f;
          r = pal.masstone(f);
        } else {
          float unit = pal.name == "stick" ? 0.006f : 0.7f;
          f[pr[0]] = unit * (1.f - t); f[pr[1]] = unit * t;
          Spectrum kd, sd;
          pal.film(f, kd, sd);
          r = km_over(kd, sd, paper);
        }
        fill_rect(img, 20 + k * (sw + 2), y, 20 + k * (sw + 2) + sw, y + sw, spectrum_to_rgb(r));
      }
      y += sw + pad;
    }
    y += 30;
  }
  return save_image(out, img);
}

std::vector<Vec2> line_path(Vec2 a, Vec2 b, int n = 24) {
  std::vector<Vec2> p;
  for (int i = 0; i <= n; ++i) p.push_back(lerp(a, b, static_cast<float>(i) / n));
  return p;
}
std::vector<Vec2> wave_path(Vec2 a, Vec2 b, float amp, float cycles, int n = 60) {
  std::vector<Vec2> p;
  Vec2 d = normalize(b - a), nrm = perp(d);
  for (int i = 0; i <= n; ++i) {
    float t = static_cast<float>(i) / n;
    p.push_back(lerp(a, b, t) + nrm * (amp * std::sin(t * cycles * 2.f * kPi)));
  }
  return p;
}

// Oil brush study: brush shapes, pressure, depletion, wet-into-wet pickup.
bool study_brush(const std::string& out) {
  const float mm = 0.25f;
  Canvas canvas(make_surface("canvas", 1200, 900, mm, 3), oil_palette());
  canvas.gloss = 0.12f;
  Mixer mixer(canvas.palette);
  Rng rng(42);
  HandParams hand;

  auto paint = [&](BristleBrush& br, Rgb color, float mm3, std::vector<Vec2> path, float press,
                   float medium = 0.f) {
    br.wipe(0.05f);
    br.load(mixer.match_opaque(color), mm3, medium);
    Gesture g;
    g.path = std::move(path);
    g.press = press;
    br.stroke(canvas, perform(g, hand, mm, rng));
  };

  BrushSpec flat;
  flat.name = "flat 12 mm"; flat.width_mm = 12.f; flat.thick_mm = 3.f; flat.length_mm = 16.f;
  BrushSpec big = flat;
  big.width_mm = 25.f; big.thick_mm = 5.f; big.length_mm = 24.f; big.max_depth_mm = 6.f;
  BrushSpec filbert = flat;
  filbert.shape = BrushSpec::Shape::Filbert; filbert.width_mm = 10.f;
  BrushSpec round;
  round.shape = BrushSpec::Shape::Round; round.width_mm = 4.f; round.length_mm = 14.f;
  round.stiffness = 0.5f; round.max_depth_mm = 3.5f;

  BristleBrush b_flat(flat, mm, 1), b_big(big, mm, 2), b_fil(filbert, mm, 3), b_round(round, mm, 4);

  // Row 1: same brush, light to heavy pressure.
  for (int i = 0; i < 4; ++i) {
    float y = 70.f + 60.f * i;
    paint(b_flat, {0.05f, 0.12f, 0.45f}, 60.f, line_path({60, y}, {560, y}), 0.25f + 0.25f * i);
  }
  // Depletion: one small load over a long stroke.
  paint(b_flat, {0.55f, 0.06f, 0.05f}, 14.f, wave_path({620, 80}, {1150, 80}, 18.f, 1.5f), 0.6f);
  paint(b_flat, {0.55f, 0.06f, 0.05f}, 40.f, wave_path({620, 160}, {1150, 160}, 18.f, 1.5f), 0.6f);
  paint(b_big, {0.75f, 0.55f, 0.1f}, 220.f, wave_path({620, 250}, {1150, 250}, 14.f, 1.f), 0.6f);

  // Wet into wet: yellow field, then blue strokes dragged through it.
  for (int i = 0; i < 5; ++i)
    paint(b_big, {0.85f, 0.7f, 0.08f}, 260.f, line_path({60, 380.f + 42.f * i}, {560, 380.f + 42.f * i}), 0.65f);
  for (int i = 0; i < 4; ++i)
    paint(b_flat, {0.03f, 0.08f, 0.4f}, 45.f, line_path({110.f + 115.f * i, 340}, {150.f + 115.f * i, 600}), 0.6f);

  // Filbert and round, curved strokes.
  paint(b_fil, {0.1f, 0.35f, 0.15f}, 60.f, wave_path({620, 380}, {1150, 380}, 30.f, 2.f), 0.6f);
  paint(b_fil, {0.8f, 0.8f, 0.78f}, 60.f, wave_path({620, 470}, {1150, 470}, 30.f, 2.f), 0.6f);
  for (int i = 0; i < 4; ++i)
    paint(b_round, {0.08f, 0.05f, 0.04f}, 10.f, wave_path({620, 550.f + 22.f * i}, {1150, 550.f + 22.f * i}, 8.f, 3.f),
          0.2f + 0.2f * i);

  // Short dabs and a thinned scumble.
  for (int i = 0; i < 9; ++i) {
    float x = 90.f + 55.f * i;
    paint(b_flat, {0.7f, 0.25f + 0.05f * i, 0.2f}, 25.f, line_path({x, 690}, {x + 22.f, 730}), 0.7f);
  }
  paint(b_big, {0.3f, 0.2f, 0.12f}, 120.f, wave_path({620, 700}, {1150, 700}, 30.f, 1.f), 0.5f, 0.6f);
  paint(b_big, {0.3f, 0.2f, 0.12f}, 60.f, line_path({620, 800}, {1150, 800}), 0.25f, 0.f);

  return save_image(out, canvas.render());
}

// Bare surfaces under raking light.
bool study_surfaces(const std::string& out) {
  auto names = surface_names();
  const int tile = 360;
  Image sheet(tile * 3, tile * 3);
  for (size_t i = 0; i < names.size(); ++i) {
    Canvas c(make_surface(names[i], tile, tile, 0.12f, 5), graphite_palette());
    Light l;
    l.elevation = 0.45f;
    Image r = c.render(l);
    int ox = static_cast<int>(i % 3) * tile, oy = static_cast<int>(i / 3) * tile;
    for (int y = 0; y < tile; ++y)
      for (int x = 0; x < tile; ++x) sheet(ox + x, oy + y) = r(x, y);
  }
  return save_image(out, sheet);
}

}  // namespace

std::vector<std::string> study_names() { return {"pigments", "surfaces", "brush"}; }

bool run_study(const std::string& name, const std::string& out_path) {
  bool ok = false;
  if (name == "pigments") ok = study_pigments(out_path);
  else if (name == "surfaces") ok = study_surfaces(out_path);
  else if (name == "brush") ok = study_brush(out_path);
  else {
    std::fprintf(stderr, "unknown study '%s'\n", name.c_str());
    return false;
  }
  if (!ok) std::fprintf(stderr, "failed to write '%s'\n", out_path.c_str());
  return ok;
}

}  // namespace bs
