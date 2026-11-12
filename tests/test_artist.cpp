// End to end: every medium turns a subject into an artwork that resembles
// it, deterministically, and stops when asked.
#include "check.hpp"
#include "artist/artist.hpp"

using namespace bs;

namespace {

// A simple subject: a light sky fading downward, a dark disc and a warm bar.
Image subject() {
  Image img(180, 240);
  for (int y = 0; y < img.h; ++y)
    for (int x = 0; x < img.w; ++x) {
      float t = static_cast<float>(y) / img.h;
      Rgb c = lerp(Rgb{0.45f, 0.62f, 0.85f}, Rgb{0.75f, 0.72f, 0.6f}, t);
      if (sqr(x - 70.f) + sqr(y - 110.f) < sqr(42.f)) c = {0.05f, 0.05f, 0.08f};
      if (x > 120 && x < 160 && y > 150 && y < 220) c = {0.7f, 0.25f, 0.1f};
      // A white cloud, so the sky is not the lightest thing in the picture
      // (works on paper give the lightest tones to the bare sheet).
      if (sqr((x - 130.f) / 34.f) + sqr((y - 62.f) / 12.f) < 1.f) c = {0.95f, 0.95f, 0.95f};
      img(x, y) = c;
    }
  return img;
}

// Correlation of block-mean lightness between two images of the same size.
double lightness_correlation(const Image& a, const Image& b, int block) {
  std::vector<double> xa, xb;
  for (int by = 0; by + block <= a.h; by += block)
    for (int bx = 0; bx + block <= a.w; bx += block) {
      double la = 0.0, lb = 0.0;
      for (int y = by; y < by + block; ++y)
        for (int x = bx; x < bx + block; ++x) {
          la += rgb_to_oklab(a(x, y)).L;
          lb += rgb_to_oklab(b(x, y)).L;
        }
      xa.push_back(la);
      xb.push_back(lb);
    }
  double ma = 0.0, mb = 0.0;
  for (size_t i = 0; i < xa.size(); ++i) { ma += xa[i]; mb += xb[i]; }
  ma /= xa.size(); mb /= xb.size();
  double sab = 0.0, saa = 0.0, sbb = 0.0;
  for (size_t i = 0; i < xa.size(); ++i) {
    sab += (xa[i] - ma) * (xb[i] - mb);
    saa += sqr(static_cast<float>(xa[i] - ma));
    sbb += sqr(static_cast<float>(xb[i] - mb));
  }
  return sab / std::sqrt(saa * sbb + 1e-12);
}

}  // namespace

TEST(every_medium_makes_a_likeness) {
  Image photo = subject();
  for (const std::string& medium : medium_names()) {
    Settings s;
    s.medium = medium;
    s.long_side_px = 240;
    s.long_side_mm = 60.f;  // a miniature, so the tools work at their real scale (0.25 mm per cell)
    s.effort = 0.7f;
    Artwork art = create_artwork(photo, s);
    std::printf("    %-15s %6d strokes  %.1f s  on %s\n", medium.c_str(), art.strokes, art.seconds, art.surface.c_str());
    CHECK(art.width == 180 && art.height == 240);
    CHECK(art.strokes > 20);
    CHECK(!art.cancelled);
    CHECK(art.surface == default_surface(medium));
    double corr = lightness_correlation(art.image, photo, 12);
    std::printf("    %-15s lightness correlation with the subject: %.2f\n", "", corr);
    CHECK(corr > 0.6);
  }
}

TEST(color_media_keep_the_subjects_hues) {
  Image photo = subject();
  for (const char* medium : {"oil", "pastel", "watercolor"}) {
    Settings s;
    s.medium = medium;
    s.long_side_px = 240;
    s.long_side_mm = 60.f;  // a miniature, so the tools work at their real scale (0.25 mm per cell)
    s.effort = 0.7f;
    Artwork art = create_artwork(photo, s);
    // The sky stays on the blue side and the bar on the warm side
    // (Oklab: b < 0 is blue, a > 0 and b > 0 is orange-red).
    Rgb sky, bar;
    for (int y = 10; y < 40; ++y)
      for (int x = 20; x < 160; ++x) sky += art.image(x, y);
    for (int y = 165; y < 205; ++y)
      for (int x = 128; x < 152; ++x) bar += art.image(x, y);
    Lab sky_lab = rgb_to_oklab(sky * (1.f / 4200.f)), bar_lab = rgb_to_oklab(bar * (1.f / 960.f));
    std::printf("    %-15s sky Lab %.2f %+.3f %+.3f   bar Lab %.2f %+.3f %+.3f\n", medium, sky_lab.L, sky_lab.a,
                sky_lab.b, bar_lab.L, bar_lab.a, bar_lab.b);
    CHECK(sky_lab.b < -0.008f);
    CHECK(bar_lab.a > 0.03f && bar_lab.b > 0.03f);
    CHECK(sky_lab.L > bar_lab.L);
  }
}

TEST(same_seed_same_artwork) {
  Image photo = subject();
  Settings s;
  s.medium = "pastel";
  s.long_side_px = 200;
  s.effort = 0.6f;
  s.seed = 5;
  Artwork a = create_artwork(photo, s), b = create_artwork(photo, s);
  CHECK(a.strokes == b.strokes);
  bool same = a.image.v.size() == b.image.v.size();
  for (size_t i = 0; same && i < a.image.v.size(); ++i)
    same = a.image.v[i].r == b.image.v[i].r && a.image.v[i].g == b.image.v[i].g;
  CHECK(same);
  s.seed = 6;
  Artwork c = create_artwork(photo, s);
  bool differs = false;
  for (size_t i = 0; i < a.image.v.size() && i < c.image.v.size(); ++i) differs = differs || a.image.v[i].g != c.image.v[i].g;
  CHECK(differs);
}

TEST(stops_when_asked) {
  Image photo = subject();
  Settings s;
  s.medium = "oil";
  s.long_side_px = 240;
  Hooks hooks;
  int calls = 0;
  hooks.on_progress = [&](const Progress& p, const Canvas&) {
    ++calls;
    return p.strokes < 30;
  };
  Artwork art = create_artwork(photo, s, hooks);
  CHECK(art.cancelled);
  CHECK(art.strokes >= 30 && art.strokes < 80);
  CHECK(!art.image.empty());
  CHECK(calls > 1);
}

TEST(unknown_settings_fall_back) {
  Image photo = subject();
  Settings s;
  s.medium = "finger_paint";
  s.surface = "sandpaper";
  s.long_side_px = 160;
  s.effort = 0.4f;
  Artwork art = create_artwork(photo, s);
  CHECK(art.medium == "oil");
  CHECK(art.surface == "canvas");
  CHECK(create_artwork(Image{}, s).image.empty());
}

TEST(accents_do_not_darken_an_edge_between_pale_forms) {
  // Two pale forms meet at a sharp edge. The edge is strong, but neither
  // side of it is dark, so the finished drawing must not have a dark line
  // along it: pastel makes an accent only where the drawing lacks a dark.
  // (A dark band and a white one give the picture its full range of tone.)
  const int w = 240, h = 180;
  Image photo(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      Rgb c = x < w / 2 ? Rgb{0.80f, 0.62f, 0.50f} : Rgb{0.62f, 0.74f, 0.80f};
      if (y < 30) c = {0.03f, 0.03f, 0.05f};
      if (y >= h - 30) c = {0.95f, 0.95f, 0.95f};
      photo(x, y) = c;
    }
  struct Case { const char* medium; double may_darken; };
  for (const Case& cs : {Case{"pastel", 0.04}}) {
    const char* medium = cs.medium;
    Settings s;
    s.medium = medium;
    s.long_side_px = w;
    s.long_side_mm = 60.f;
    Artwork art = create_artwork(photo, s);
    auto strip = [&](int x0, int x1) {
      double acc = 0.0;
      for (int y = 50; y < h - 50; ++y)
        for (int x = x0; x < x1; ++x) acc += rgb_to_oklab(art.image(x, y)).L;
      return acc / (static_cast<double>(x1 - x0) * (h - 100));
    };
    double left = strip(40, 80), edge = strip(w / 2 - 3, w / 2 + 3), right = strip(160, 200);
    std::printf("    %-15s lightness: left form %.3f, along the edge %.3f, right form %.3f\n", medium, left, edge, right);
    CHECK(edge > std::min(left, right) - cs.may_darken);
  }
}
