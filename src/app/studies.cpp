#include "app/studies.hpp"

#include <cstdio>

#include "canvas/canvas.hpp"
#include "artist/paths.hpp"
#include "media/bristle_brush.hpp"
#include "media/dry_tool.hpp"
#include "media/watercolor.hpp"
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
    Canvas c(make_surface(names[i], tile, tile, 0.12f, 5), stick_palette());
    Light l;
    l.elevation = 0.45f;
    Image r = c.render(l);
    int ox = static_cast<int>(i % 3) * tile, oy = static_cast<int>(i / 3) * tile;
    for (int y = 0; y < tile; ++y)
      for (int x = 0; x < tile; ++x) sheet(ox + x, oy + y) = r(x, y);
  }
  return save_image(out, sheet);
}

// Dry media study, two panels per medium on its usual paper.
//   top left     - colored pencil: lines at rising pressure, a long line
//                  wearing the point down, hatching built up in layers
//   top right    - colored pencil: light and heavy fills, one color over
//                  another, a highlight lifted with a kneaded eraser
//   bottom left  - soft pastel: side-of-stick strokes on toned paper, light
//                  over dark, a passage rubbed in with a fingertip
//   bottom right - soft pastel: corner and pastel-pencil lines, detail
//                  hatching, light over a fixed layer
bool study_dry(const std::string& out) {
  const float mm = 0.2f;
  const int pw = 700, ph = 520;
  Image sheet(pw * 2, ph * 2);
  Rng rng(7);
  auto blit = [&](const Canvas& c, int ox, int oy) {
    Light l;
    l.ambient = 0.7f; l.elevation = 0.9f;
    Image r = c.render(l);
    for (int y = 0; y < ph; ++y)
      for (int x = 0; x < pw; ++x) sheet(ox + x, oy + y) = r(x, y);
  };
  auto draw = [&](Canvas& c, DryTool& t, std::vector<Vec2> path, float press, float tempo = 1.f) {
    Gesture g;
    g.path = std::move(path);
    g.press = press;
    g.tempo = tempo;
    t.stroke(c, perform(g, t.hand(), mm, rng));
  };
  auto hatch = [&](Canvas& c, DryTool& t, Vec2 center, Vec2 dir, float len, float spacing, int count, float press) {
    Vec2 d = normalize(dir), across = perp(d);
    for (int i = 0; i < count; ++i) {
      Vec2 m = center + across * ((i - 0.5f * count) * spacing + rng.normal() * 0.3f);
      float l = len * rng.range(0.85f, 1.1f);
      draw(c, t, arc_path(m - d * (0.5f * l), m + d * (0.5f * l), 0.03f), press * rng.range(0.85f, 1.15f), 1.3f);
    }
  };

  Palette sticks = stick_palette();
  auto mixof = [](std::initializer_list<std::pair<int, float>> parts) {
    PigVec r{};
    for (auto& p : parts) r[p.first] = p.second;
    return r;
  };
  PigVec red = mixof({{2, 1.f}}), yellow = mixof({{1, 1.f}}), blue = mixof({{4, 1.f}, {0, 0.3f}});
  PigVec green = mixof({{5, 0.25f}, {1, 1.f}}), brown = mixof({{6, 1.f}}), black = mixof({{7, 1.f}});
  PigVec white = mixof({{0, 1.f}}), sky = mixof({{4, 0.3f}, {0, 1.f}}), flesh = mixof({{0, 1.f}, {2, 0.08f}, {1, 0.1f}});

  // --- Colored pencil on drawing paper: the line ---
  {
    Canvas c(make_surface("drawing", pw, ph, mm, 2), sticks);
    PigVec cols[] = {red, blue, green, brown};
    for (int k = 0; k < 4; ++k) {
      DryTool t(colored_pencil_spec(cols[k]), mm);
      for (int i = 0; i < 4; ++i) {
        t.sharpen();
        float y = 30.f + k * 62.f + i * 13.f;
        draw(c, t, wave_path({30, y}, {330, y}, 3.f, 2.f), 0.2f + 0.25f * i);
      }
    }
    // One unsharpened pencil drawing a long meander: the line broadens.
    DryTool worn(colored_pencil_spec(black), mm);
    std::vector<Vec2> meander;
    for (int r = 0; r < 14; ++r) {
      float y = 30.f + r * 16.f;
      meander.push_back({r % 2 ? 670.f : 380.f, y});
      meander.push_back({r % 2 ? 380.f : 670.f, y});
    }
    draw(c, worn, smooth_path(meander, 1), 0.6f);
    // Hatching, cross-hatching, and tone built in layers.
    DryTool pb(colored_pencil_spec(blue), mm), pk(colored_pencil_spec(black), mm);
    for (int k = 0; k < 4; ++k) {
      Vec2 ctr{90.f + 150.f * k, 390.f};
      pb.sharpen();
      hatch(c, pb, ctr, {0.7f, -0.7f}, 110.f, 5.f, 22, 0.35f + 0.15f * k);
      if (k >= 1) hatch(c, pb, ctr, {0.7f, 0.7f}, 110.f, 5.f, 22, 0.35f + 0.1f * k);
      if (k >= 2) hatch(c, pk, ctr, {1.f, 0.1f}, 110.f, 4.f, 26, 0.5f);
      if (k >= 3) hatch(c, pk, ctr, {0.1f, 1.f}, 110.f, 3.f, 36, 0.8f);
    }
    blit(c, 0, 0);
  }

  // --- Colored pencil on drawing paper: fills and layering ---
  {
    Canvas c(make_surface("drawing", pw, ph, mm, 3), sticks);
    PigVec cols[] = {red, yellow, blue, green, brown, black};
    for (int k = 0; k < 6; ++k) {
      DryTool cp(colored_pencil_spec(cols[k]), mm);
      float y = 30.f + 30.f * k;
      draw(c, cp, wave_path({30, y}, {670, y}, 6.f, 4.f), 0.3f + 0.1f * k);
    }
    DryTool py(colored_pencil_spec(yellow), mm), pb(colored_pencil_spec(blue), mm), pr(colored_pencil_spec(red), mm);
    // Light and heavy fills; yellow alone, blue over yellow, blue alone.
    hatch(c, pr, {100, 300}, {0.7f, -0.7f}, 110.f, 2.5f, 44, 0.4f);
    hatch(c, pr, {260, 300}, {0.7f, -0.7f}, 110.f, 2.5f, 44, 0.9f);
    hatch(c, py, {430, 300}, {0.7f, -0.7f}, 110.f, 2.5f, 50, 0.7f);
    hatch(c, pb, {470, 300}, {0.7f, 0.7f}, 110.f, 2.5f, 30, 0.5f);
    hatch(c, pb, {610, 300}, {0.7f, 0.7f}, 110.f, 2.5f, 30, 0.5f);
    // A toned passage with a highlight lifted out by a kneaded eraser.
    DryTool pn(colored_pencil_spec(brown), mm), eraser(eraser_spec(5.f), mm);
    for (int k = 0; k < 2; ++k) {
      pn.sharpen();
      hatch(c, pn, {350, 450}, k ? Vec2{1.f, 0.06f} : Vec2{1.f, -0.06f}, 560.f, 2.5f, 36, 0.7f);
    }
    draw(c, eraser, wave_path({120.f, 450.f}, {580.f, 450.f}, 18.f, 2.f), 0.9f);
    draw(c, eraser, wave_path({120.f, 453.f}, {580.f, 453.f}, 18.f, 2.f), 0.9f);
    blit(c, pw, 0);
  }

  // --- Soft pastel on toned pastel paper: the side of the stick ---
  {
    Canvas c(make_surface("pastel_paper", pw, ph, mm, 5), sticks);
    PigVec cols[] = {red, yellow, blue, green, sky, flesh, white, black};
    for (int k = 0; k < 8; ++k) {
      DryTool side(pastel_spec(cols[k], true), mm);
      float x = 50.f + 80.f * k;
      draw(c, side, line_path({x, 30.f}, {x + 20.f, 230.f}), 0.2f + 0.1f * k);
    }
    // Light over dark, and a blended passage.
    DryTool dk(pastel_spec(blue, true), mm), lt(pastel_spec(flesh, true), mm), wh(pastel_spec(white, false), mm);
    for (int i = 0; i < 5; ++i) draw(c, dk, line_path({40.f, 290.f + 40.f * i}, {660.f, 290.f + 40.f * i}), 0.7f);
    for (int i = 0; i < 3; ++i) draw(c, lt, line_path({360.f + 90.f * i, 270.f}, {400.f + 90.f * i, 490.f}), 0.6f);
    DryTool finger(stump_spec(12.f), mm);
    for (int i = 0; i < 24; ++i)
      draw(c, finger, line_path({40.f, 285.f + 8.f * i}, {250.f, 285.f + 8.f * i}), 0.6f);
    draw(c, wh, wave_path({60.f, 380.f}, {640.f, 380.f}, 30.f, 2.f), 0.8f);
    blit(c, 0, ph);
  }

  // --- Soft pastel on toned pastel paper: corner, pastel pencil, fixing ---
  {
    Canvas c(make_surface("pastel_paper", pw, ph, mm, 6), sticks);
    PigVec cols[] = {red, blue, white, black};
    for (int k = 0; k < 4; ++k) {
      DryTool tip(pastel_spec(cols[k], false), mm), pencil(pastel_pencil_spec(cols[k]), mm);
      for (int i = 0; i < 3; ++i) {
        float y = 30.f + k * 50.f + i * 14.f;
        draw(c, tip, wave_path({30, y}, {330, y}, 4.f, 2.f), 0.25f + 0.3f * i);
        pencil.sharpen();
        draw(c, pencil, wave_path({370, y}, {670, y}, 4.f, 2.f), 0.25f + 0.3f * i);
      }
    }
    // Two dark blocks, the left one fixed, then light hatching over both:
    // on the fixed layer the light stays clean; on the loose one it mixes.
    DryTool dk(pastel_spec(brown, true), mm);
    for (int i = 0; i < 6; ++i) draw(c, dk, line_path({40.f, 270.f + 40.f * i}, {340.f, 270.f + 40.f * i}), 0.7f);
    c.dry(1.f);  // fixative
    for (int i = 0; i < 6; ++i) draw(c, dk, line_path({360.f, 270.f + 40.f * i}, {660.f, 270.f + 40.f * i}), 0.7f);
    DryTool lp(pastel_pencil_spec(flesh), mm);
    hatch(c, lp, {190, 380}, {0.7f, -0.7f}, 150.f, 4.f, 50, 0.6f);
    lp.sharpen();
    hatch(c, lp, {510, 380}, {0.7f, -0.7f}, 150.f, 4.f, 50, 0.6f);
    blit(c, pw, ph);
  }
  return save_image(out, sheet);
}

// Watercolor study on cold-press paper:
//   top left     - wet-on-dry strokes: hard, darkened edges; ultramarine granulates
//   top right    - wet-in-wet: color dropped into a clean-water wash bleeds and feathers
//   bottom left  - glazing: a blue wash over a dried yellow one mixes optically to green
//   bottom mid   - backrun: clean water dropped into a half-dry wash pushes pigment outward
//   bottom right - dry brush: a nearly empty brush skips over the grain
bool study_watercolor(const std::string& out) {
  const float mm = 0.25f;
  Canvas canvas(make_surface("cold_press", 1200, 800, mm, 6), watercolor_palette());
  canvas.film_relief = 0.f;
  WatercolorSim sim(canvas);
  canvas.overlay = &sim.susp;
  Rng rng(11);

  BrushSpec round;
  round.name = "round"; round.shape = BrushSpec::Shape::Round;
  round.width_mm = 12.f; round.length_mm = 28.f; round.max_depth_mm = 7.f; round.taper = 0.14f;
  round.stiffness = 0.3f; round.spring = 0.35f; round.splay = 1.4f; round.clump = 0.5f;
  BristleBrush brush(round, mm, 5);
  brush.body.coat_mm = 0.13f; brush.body.tip_hold_mm = 0.5f; brush.body.rate = 1.2f;
  brush.body.mix = 0.15f; brush.body.level = 0.f; brush.body.feed = 0.3f;
  brush.set_wet({&sim.water, &sim.susp});
  HandParams hand;
  hand.cruise_mm_s = 110.f; hand.tilt = 0.5f;

  auto wash = [&](PigVec load, float mm3, std::vector<Vec2> path, float press) {
    brush.wipe(0.02f);
    PigVec per_mm{};
    for (int i = 0; i < kMaxPig; ++i) per_mm[i] = load[i] / brush.body.coat_mm;
    brush.load_wet(per_mm, mm3);
    Gesture g;
    g.path = std::move(path);
    g.press = press;
    brush.stroke(canvas, perform(g, hand, mm, rng));
    sim.touch(brush.last_touched());
    sim.advance(1.5f);
  };
  auto pig = [](int i, float a, int j = -1, float b = 0.f) {
    PigVec p{};
    p[i] = a;
    if (j >= 0) p[j] = b;
    return p;
  };
  // Watercolor palette order: yellow 0, ochre 1, red 2, crimson 3, ultramarine 4,
  // phthalo blue 5, phthalo green 6, umber 7.

  // Wet on dry.
  wash(pig(4, 1.0f), 260.f, wave_path({60, 80}, {540, 80}, 14.f, 1.f), 0.6f);
  wash(pig(2, 0.8f), 260.f, wave_path({60, 180}, {540, 180}, 14.f, 1.f), 0.6f);
  wash(pig(7, 0.9f, 4, 0.4f), 260.f, wave_path({60, 280}, {540, 280}, 14.f, 1.f), 0.35f);

  // Wet in wet: clean water first, then color dropped in.
  for (int i = 0; i < 7; ++i)
    wash(PigVec{}, 420.f, line_path({640.f, 60.f + 38.f * i}, {1140.f, 60.f + 38.f * i}), 0.8f);
  wash(pig(3, 1.2f), 120.f, wave_path({700, 120}, {1080, 120}, 10.f, 2.f), 0.3f);
  wash(pig(5, 1.0f), 120.f, wave_path({700, 220}, {1080, 220}, 10.f, 2.f), 0.3f);
  wash(pig(0, 1.2f), 120.f, line_path({880, 80}, {900, 280}), 0.3f);
  sim.dry();

  // Glaze: yellow, dry, then blue across it.
  for (int i = 0; i < 5; ++i)
    wash(pig(0, 0.9f), 380.f, line_path({60.f, 440.f + 36.f * i}, {380.f, 440.f + 36.f * i}), 0.75f);
  sim.dry();
  for (int i = 0; i < 4; ++i)
    wash(pig(5, 0.5f), 380.f, line_path({170.f + 36.f * i, 410.f}, {170.f + 36.f * i, 700.f}), 0.75f);

  // Backrun: a wash, left to go damp, then a drop of clean water.
  for (int i = 0; i < 6; ++i)
    wash(pig(7, 0.8f, 3, 0.3f), 380.f, line_path({470.f, 440.f + 36.f * i}, {780.f, 440.f + 36.f * i}), 0.75f);
  sim.advance(70.f);
  wash(PigVec{}, 300.f, line_path({600.f, 520.f}, {650.f, 560.f}), 0.9f);

  // Dry brush.
  wash(pig(4, 1.2f, 7, 0.5f), 16.f, wave_path({860, 480}, {1150, 480}, 20.f, 1.f), 0.3f);
  wash(pig(2, 1.2f), 10.f, wave_path({860, 600}, {1150, 600}, 20.f, 1.f), 0.25f);
  sim.dry();

  Light l;
  l.ambient = 0.7f; l.elevation = 0.9f;
  return save_image(out, canvas.render(l));
}

}  // namespace

std::vector<std::string> study_names() { return {"pigments", "surfaces", "brush", "dry", "watercolor"}; }

bool run_study(const std::string& name, const std::string& out_path) {
  bool ok = false;
  if (name == "pigments") ok = study_pigments(out_path);
  else if (name == "surfaces") ok = study_surfaces(out_path);
  else if (name == "brush") ok = study_brush(out_path);
  else if (name == "dry") ok = study_dry(out_path);
  else if (name == "watercolor") ok = study_watercolor(out_path);
  else {
    std::fprintf(stderr, "unknown study '%s'\n", name.c_str());
    return false;
  }
  if (!ok) std::fprintf(stderr, "failed to write '%s'\n", out_path.c_str());
  return ok;
}

}  // namespace bs
