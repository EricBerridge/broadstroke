// Spectral optics: observer normalization, Kubelka-Munk limits, pigment
// mixing behavior and the color-matching solver.
#include "check.hpp"
#include "optics/mixer.hpp"

using namespace bs;

TEST(perfect_reflector_is_white) {
  Rgb c = spectrum_to_rgb(spectrum_fill(1.f));
  CHECK_NEAR(c.r, 1.0, 1e-4);
  CHECK_NEAR(c.g, 1.0, 1e-4);
  CHECK_NEAR(c.b, 1.0, 1e-4);
}

TEST(rgb_spectrum_round_trip) {
  for (Rgb c : {Rgb{0.8f, 0.78f, 0.7f}, Rgb{0.3f, 0.4f, 0.5f}, Rgb{0.6f, 0.5f, 0.45f}}) {
    Rgb back = spectrum_to_rgb(rgb_to_spectrum(c));
    CHECK_NEAR(back.r, c.r, 2e-3);
    CHECK_NEAR(back.g, c.g, 2e-3);
    CHECK_NEAR(back.b, c.b, 2e-3);
  }
}

TEST(oklab_round_trip) {
  for (Rgb c : {Rgb{0.1f, 0.5f, 0.9f}, Rgb{0.7f, 0.2f, 0.1f}, Rgb{0.5f, 0.5f, 0.5f}}) {
    Rgb back = oklab_to_rgb(rgb_to_oklab(c));
    CHECK_NEAR(back.r, c.r, 2e-3);
    CHECK_NEAR(back.g, c.g, 2e-3);
    CHECK_NEAR(back.b, c.b, 2e-3);
  }
}

TEST(km_film_limits) {
  // No film: the background. Thick film: the masstone, whatever is beneath.
  CHECK_NEAR(km_over_band(0.f, 0.f, 0.6f), 0.6, 1e-6);
  float k = 3.f, s = 10.f;
  float masstone = km_masstone_band(k, s);
  CHECK_NEAR(km_over_band(k * 100.f, s * 100.f, 0.9f), masstone, 1e-4);
  CHECK_NEAR(km_over_band(k * 100.f, s * 100.f, 0.05f), masstone, 1e-4);
  // A very thin film barely changes the background.
  CHECK_NEAR(km_over_band(k * 1e-5f, s * 1e-5f, 0.6f), 0.6, 1e-3);
  // Pure absorber: Beer-Lambert over two passes.
  CHECK_NEAR(km_over_band(0.5f, 0.f, 0.8f), 0.8 * std::exp(-1.0), 1e-5);
  // The scattering formula approaches the absorbing one as S vanishes.
  CHECK_NEAR(km_over_band(0.5f, 1e-3f, 0.8f), 0.8 * std::exp(-1.0), 2e-3);
  // Reflectance stays physical and darkens monotonically with thickness over white.
  float prev = 1.f;
  for (float d = 0.01f; d < 5.f; d *= 1.6f) {
    float r = km_over_band(k * d, s * d, 0.95f);
    CHECK(r > 0.f && r < 1.f);
    CHECK(r <= prev + 1e-6f);
    prev = r;
  }
}

TEST(yellow_and_blue_make_green) {
  // The defining failure of RGB blending: yellow + blue must not go gray.
  Palette pal = oil_palette();
  PigVec mix{};
  mix[1] = 1.f;    // cadmium yellow
  mix[6] = 0.3f;   // phthalo blue
  Rgb c = spectrum_to_rgb(pal.masstone(mix));
  CHECK(c.g > c.r * 1.15f);
  CHECK(c.g > c.b * 1.5f);
}

TEST(white_lightens_and_keeps_hue) {
  Palette pal = oil_palette();
  PigVec red{}, pink{};
  red[3] = 1.f;
  pink[3] = 1.f; pink[0] = 4.f;
  Lab a = rgb_to_oklab(spectrum_to_rgb(pal.masstone(red)));
  Lab b = rgb_to_oklab(spectrum_to_rgb(pal.masstone(pink)));
  CHECK(b.L > a.L + 0.1f);
  CHECK(b.a > 0.f);  // still on the red side
}

TEST(glaze_darkens_multiplicatively) {
  // Two thin transparent layers equal one layer of double load.
  Palette pal = watercolor_palette();
  PigVec one{}, two{};
  one[5] = 0.3f;
  two[5] = 0.6f;
  Spectrum paper = spectrum_fill(0.9f), kd, sd;
  pal.film(one, kd, sd);
  Spectrum once = km_over(kd, sd, paper);
  Spectrum twice = km_over(kd, sd, once);
  pal.film(two, kd, sd);
  Spectrum doubled = km_over(kd, sd, paper);
  for (int b = 0; b < kBands; ++b) CHECK_NEAR(twice[b], doubled[b], 0.02);
}

TEST(mixer_matches_colors_in_gamut) {
  Mixer mixer(oil_palette());
  const Rgb targets[] = {{0.55f, 0.35f, 0.25f}, {0.30f, 0.45f, 0.70f}, {0.20f, 0.35f, 0.15f},
                         {0.80f, 0.75f, 0.65f}, {0.08f, 0.07f, 0.07f}, {0.75f, 0.45f, 0.10f}};
  for (const Rgb& t : targets) {
    PigVec f = mixer.match_opaque(t);
    CHECK_NEAR(sum(f), 1.0, 1e-3);
    for (float v : f) CHECK(v >= 0.f);
    CHECK(lab_dist(rgb_to_oklab(mixer.masstone_rgb(f)), rgb_to_oklab(t)) < 0.045f);
  }
  // Same question, same answer.
  PigVec a = mixer.match_opaque(targets[0]), b = mixer.match_opaque(targets[0]);
  for (int i = 0; i < kMaxPig; ++i) CHECK(a[i] == b[i]);
}

TEST(glaze_match_only_darkens) {
  Palette pal = watercolor_palette();
  Mixer mixer(pal);
  Spectrum paper = rgb_to_spectrum({0.9f, 0.88f, 0.83f});
  // Lighter than the paper: nothing a transparent film can do.
  CHECK(sum(mixer.match_glaze(paper, {0.98f, 0.98f, 0.98f}, 3.f)) == 0.f);
  // Darker, colored target: a film that gets close.
  Rgb want{0.25f, 0.4f, 0.6f};
  PigVec amt = mixer.match_glaze(paper, want, 3.f);
  CHECK(sum(amt) > 0.f);
  Spectrum kd, sd;
  pal.film(amt, kd, sd);
  float after = lab_dist(rgb_to_oklab(spectrum_to_rgb(km_over(kd, sd, paper))), rgb_to_oklab(want));
  float before = lab_dist(rgb_to_oklab(spectrum_to_rgb(paper)), rgb_to_oklab(want));
  CHECK(after < 0.25f * before);
}
