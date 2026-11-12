// The scene file and what the engine does with it (spec 2.13): the reader
// accepts what the format allows and refuses the rest, importance moves the
// artist's care, and a scene is input like the photo.
#include <filesystem>
#include <fstream>

#include "check.hpp"
#include "artist/artist.hpp"
#include "core/json.hpp"
#include "stb_image_write.h"
#include "vision/analysis.hpp"

using namespace bs;

namespace {

// The same busy texture everywhere, so that any difference between two
// parts of the picture comes from the scene and not from the subject.
Image textured_subject(int w, int h) {
  Image img(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float n = hash01(x / 5, y / 5, 7u);
      img(x, y) = lerp(Rgb{0.18f, 0.22f, 0.30f}, Rgb{0.75f, 0.62f, 0.45f}, n);
    }
  return img;
}

// Left third: the main subject. Middle third: no item. Right third: background.
Scene thirds_scene(int w, int h) {
  Scene sc;
  sc.items = {{1, "subject", 0, 3}, {2, "backdrop", 0, 0}};
  sc.main_subject = 1;
  sc.item = Plane<uint8_t>(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) sc.item(x, y) = x < w / 3 ? 1 : (x >= 2 * w / 3 ? 2 : 0);
  return sc;
}

double mean(const Plane<float>& p, int x0, int x1) {
  double acc = 0.0;
  for (int y = 0; y < p.h; ++y)
    for (int x = x0; x < x1; ++x) acc += p(x, y);
  return acc / (static_cast<double>(x1 - x0) * p.h);
}

bool same_image(const Image& a, const Image& b) {
  if (a.v.size() != b.v.size()) return false;
  for (size_t i = 0; i < a.v.size(); ++i)
    if (a.v[i].r != b.v[i].r || a.v[i].g != b.v[i].g || a.v[i].b != b.v[i].b) return false;
  return true;
}

struct TempDir {
  std::filesystem::path dir;
  TempDir() : dir(std::filesystem::temp_directory_path() / "broadstroke_test_scene") {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  std::string path(const char* name) const { return (dir / name).string(); }
  void write(const char* name, const std::string& text) const {
    std::ofstream out(dir / name, std::ios::binary);
    out << text;
  }
  // An 8-bit grayscale PNG, left half `left`, right half `right`.
  void write_layer(const char* name, int w, int h, uint8_t left, uint8_t right) const {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) px[static_cast<size_t>(y) * w + x] = x < w / 2 ? left : right;
    stbi_write_png(path(name).c_str(), w, h, 1, px.data(), w);
  }
};

const char* kGoodScene = R"({
  "version": 1,
  "photo": "portrait.jpg",
  "scene": "A woman at a market.",
  "main_subject": 2,
  "item_layer": "items.png",
  "items": [
    {"id": 1, "name": "woman", "parent": 0, "treatment": "fabric", "importance": 3, "edges": "hard"},
    {"id": 2, "name": "face", "parent": 1, "treatment": "skin", "importance": 3},
    {"id": 7, "name": "caf\u00e9 wall", "importance": 0}
  ]
})";

}  // namespace

TEST(json_reads_documents_and_refuses_garbage) {
  Json j;
  std::string err;
  CHECK(parse_json(R"( {"a": [1, -2.5e1, true, false, null], "b": {"c": "x\n\"y\" \u00e9 \ud83d\ude00"}} )", j, &err));
  CHECK(j.is_object() && j.get("a") && j.get("a")->array.size() == 5);
  CHECK_NEAR(j.get("a")->array[1].number, -25.0, 0.0);
  CHECK(j.get("a")->array[2].boolean && j.get("a")->array[4].type == Json::Type::Null);
  CHECK(j.get("b")->get("c")->string == "x\n\"y\" \xC3\xA9 \xF0\x9F\x98\x80");
  CHECK(j.get("missing") == nullptr);
  for (const char* bad : {"", "{", "{\"a\": }", "[1, 2,]", "{\"a\": 1} x", "\"open", "[1 2]", "{\"a\": 01x}",
                          "\"\\ud83d\"", "nul"}) {
    Json k;
    CHECK(!parse_json(bad, k, &err));
    CHECK(!err.empty());
  }
  // Deep nesting is refused, not followed.
  Json k;
  CHECK(!parse_json(std::string(200, '[') + std::string(200, ']'), k, &err));
}

TEST(scene_file_is_read_and_checked) {
  TempDir tmp;
  tmp.write_layer("items.png", 40, 30, 2, 7);
  tmp.write("good.scene.json", kGoodScene);
  std::string err;
  Scene sc = load_scene(tmp.path("good.scene.json"), &err);
  CHECK(!sc.empty());
  CHECK(sc.items.size() == 3 && sc.main_subject == 2);
  CHECK(sc.item.w == 40 && sc.item.h == 30 && sc.item(0, 0) == 2 && sc.item(39, 29) == 7);
  CHECK(sc.find(2) && sc.find(2)->name == "face" && sc.find(2)->parent == 1 && sc.find(2)->importance == 3);
  CHECK(sc.find(7) && sc.find(7)->parent == 0);
  CHECK(sc.find(2)->treatment == "skin" && sc.find(1)->treatment == "fabric" && sc.find(7)->treatment.empty());
  CHECK(sc.find(3) == nullptr);
  CHECK(sc.fits(400, 300) && sc.fits(1333, 1000) && !sc.fits(300, 400) && !sc.fits(400, 400));
  CHECK(scene_path_for("C:/photos/a.b/portrait.jpg") == "C:/photos/a.b/portrait.scene.json");
  CHECK(file_exists(tmp.path("good.scene.json")) && !file_exists(tmp.path("absent.scene.json")));

  // Each of these differs from the good file in one way and must be refused.
  auto refused = [&](const char* what, std::string text) {
    tmp.write("bad.scene.json", text);
    std::string why;
    Scene bad = load_scene(tmp.path("bad.scene.json"), &why);
    if (!bad.empty() || why.empty()) std::printf("    not refused: %s\n", what);
    CHECK(bad.empty() && !why.empty());
  };
  auto with = [&](const std::string& from, const std::string& to) {
    std::string t = kGoodScene;
    size_t at = t.find(from);
    CHECK(at != std::string::npos);
    if (at != std::string::npos) t.replace(at, from.size(), to);
    return t;
  };
  refused("not JSON", "{ nonsense");
  refused("another version", with("\"version\": 1", "\"version\": 2"));
  refused("importance out of range", with("\"importance\": 0", "\"importance\": 4"));
  refused("importance missing", with(", \"importance\": 0", ""));
  refused("a duplicate id", with("\"id\": 7", "\"id\": 2"));
  refused("a treatment that is not text", with("\"treatment\": \"skin\"", "\"treatment\": 3"));
  refused("a parent that is not an item", with("\"parent\": 1", "\"parent\": 9"));
  refused("an item that is its own ancestor", with("\"parent\": 0", "\"parent\": 2"));
  refused("a main subject that is not an item", with("\"main_subject\": 2", "\"main_subject\": 5"));
  refused("a layer in another folder", with("\"items.png\"", "\"../items.png\""));
  refused("a layer that is missing", with("\"items.png\"", "\"absent.png\""));
  refused("a layer value no item has", with("\"id\": 7", "\"id\": 8"));
  CHECK(load_scene(tmp.path("absent.scene.json"), &err).empty());
}

TEST(importance_moves_the_artists_care) {
  const int w = 240, h = 120;
  Image photo = textured_subject(w, h);
  Subject plain = observe(photo, w, h);
  // The texture is the same everywhere, so without a scene the care is too.
  CHECK_NEAR(mean(plain.detail, 10, 70), mean(plain.detail, 170, 230), 0.05);

  Scene sc = thirds_scene(w, h);
  Subject seen = observe(photo, w, h, &sc);
  double subject_care = mean(seen.detail, 10, 70), free_care = mean(seen.detail, 90, 150),
         backdrop_care = mean(seen.detail, 170, 230);
  std::printf("    care: main subject %.2f, no item %.2f, background %.2f (without a scene %.2f)\n", subject_care,
              free_care, backdrop_care, mean(plain.detail, 90, 150));
  CHECK(subject_care > free_care + 0.05);
  CHECK(free_care > backdrop_care + 0.05);
  // Where no item is, the care is what it was.
  CHECK_NEAR(free_care, mean(plain.detail, 90, 150), 1e-6);
  for (float v : seen.detail.v) CHECK(v >= 0.f && v <= 1.f);
  // Only the care changes: the scene does not alter what the artist sees.
  CHECK(same_image(seen.color, plain.color));
  CHECK(seen.edge.v == plain.edge.v && seen.light.v == plain.light.v);

  // A layer at another resolution gives the same answer away from outlines.
  Scene coarse = thirds_scene(w / 4, h / 4);
  Subject seen_coarse = observe(photo, w, h, &coarse);
  CHECK_NEAR(mean(seen_coarse.detail, 20, 60), mean(seen.detail, 20, 60), 0.01);

  // No scene and an empty scene are the same thing.
  Scene none;
  CHECK(observe(photo, w, h, &none).detail.v == plain.detail.v);
}

TEST(a_scene_is_input_like_the_photo) {
  Image photo = textured_subject(200, 100);
  Settings s;
  s.medium = "colored_pencil";
  s.long_side_px = 200;
  s.long_side_mm = 50.f;
  s.effort = 0.6f;
  Scene sc = thirds_scene(200, 100);
  Artwork plain = create_artwork(photo, s);
  Artwork a = create_artwork(photo, s, Hooks{}, &sc), b = create_artwork(photo, s, Hooks{}, &sc);
  CHECK(same_image(a.image, b.image));       // same scene, same artwork
  CHECK(!same_image(a.image, plain.image));  // the scene changes the artwork
  bool noted = false;
  for (const std::string& n : a.notes) noted = noted || n.find("scene: 2 items, main subject: subject") == 0;
  CHECK(noted);

  // An empty scene, or one that does not have the photo's shape, is not used.
  Scene none;
  CHECK(same_image(create_artwork(photo, s, Hooks{}, &none).image, plain.image));
  Scene tall = thirds_scene(100, 200);
  Artwork c = create_artwork(photo, s, Hooks{}, &tall);
  CHECK(same_image(c.image, plain.image));
  noted = false;
  for (const std::string& n : c.notes) noted = noted || n.find("scene not used") == 0;
  CHECK(noted);
}

TEST(outlines_are_found_and_color_stays_within_an_item) {
  const int w = 240, h = 120;
  Scene sc = thirds_scene(w, h);
  Outlines o = find_outlines(sc.item);
  // On the outline between the first two thirds the distance is nothing; in
  // the middle of a third it is the way to the nearest outline.
  CHECK(o.dist(w / 3, h / 2) < 1.5f && o.dist(w / 3 - 1, h / 2) < 1.5f);
  CHECK_NEAR(o.dist(w / 6, h / 2), w / 6.0, 2.0);
  CHECK_NEAR(o.dist(w / 2, h / 2), w / 6.0, 2.0);
  // Both outlines run up and down the sheet.
  for (int x : {w / 3 - 8, w / 3 + 8, 2 * w / 3 - 8, 2 * w / 3 + 8}) CHECK(std::abs(o.along(x, h / 2).y) > 0.99f);
  // A map with one item has no outline anywhere near.
  CHECK(find_outlines(Plane<uint8_t>(40, 30, 5)).dist(20, 15) > 1000.f);

  // Three flat colors, one to an item. Seen broadly, each item keeps its
  // own color right up to its outline; an ordinary blur mixes them there.
  Image img(w, h);
  const Rgb colors[3] = {{0.8f, 0.1f, 0.1f}, {0.1f, 0.7f, 0.2f}, {0.1f, 0.2f, 0.8f}};
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) img(x, y) = colors[x < w / 3 ? 0 : (x >= 2 * w / 3 ? 2 : 1)];
  Image within = blur_within_items(img, sc.item, 6.f);
  Image mixed = img;
  gaussian_blur(mixed, 6.f);
  for (int x : {w / 3 - 1, w / 3, 2 * w / 3 - 1, 2 * w / 3}) {
    CHECK_NEAR(within(x, h / 2).r, img(x, h / 2).r, 1e-4);
    CHECK_NEAR(within(x, h / 2).b, img(x, h / 2).b, 1e-4);
    CHECK(std::abs(mixed(x, h / 2).r - img(x, h / 2).r) + std::abs(mixed(x, h / 2).b - img(x, h / 2).b) > 0.1f);
  }
  // Inside an item it is the ordinary blur: texture is smoothed.
  Image busy = textured_subject(w, h);
  Image smooth = blur_within_items(busy, sc.item, 6.f);
  CHECK(std::abs(smooth(40, 60).r - smooth(41, 60).r) < std::abs(busy(40, 60).r - busy(45, 60).r) + 1e-3f);
  // Without an item map nothing is done.
  CHECK(same_image(blur_within_items(img, Plane<uint8_t>(), 6.f), img));
}

TEST(a_watercolor_wash_keeps_to_its_item) {
  // A dark form beside a pale one. Without a scene the broad first washes
  // of the dark form are laid across the boundary; with one, the washes of
  // each item end at its outline, so the pale form stays pale beside it.
  const int w = 220, h = 110;
  Image photo(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) photo(x, y) = x < w / 2 ? Rgb{0.03f, 0.05f, 0.12f} : Rgb{0.85f, 0.78f, 0.55f};
  Scene sc;
  sc.items = {{1, "dark form", 0, 2}, {2, "pale form", 0, 2}};
  sc.item = Plane<uint8_t>(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) sc.item(x, y) = x < w / 2 ? 1 : 2;
  Settings s;
  s.medium = "watercolor";
  s.long_side_px = w;
  s.long_side_mm = 110.f;
  s.effort = 0.6f;
  Artwork plain = create_artwork(photo, s);
  Artwork kept = create_artwork(photo, s, Hooks{}, &sc);
  auto strip = [&](const Image& img, int x0, int x1) {
    double acc = 0.0;
    for (int y = 10; y < h - 10; ++y)
      for (int x = x0; x < x1; ++x) acc += rgb_to_oklab(img(x, y)).L;
    return acc / (static_cast<double>(x1 - x0) * (h - 20));
  };
  double pale_plain = strip(plain.image, w / 2 + 3, w / 2 + 12), pale_kept = strip(kept.image, w / 2 + 3, w / 2 + 12);
  double far_kept = strip(kept.image, w - 40, w - 20), dark_kept = strip(kept.image, 20, 40);
  std::printf("    lightness of the pale form beside the dark one: %.3f without a scene, %.3f with (%.3f away from it)\n",
              pale_plain, pale_kept, far_kept);
  CHECK(pale_kept > pale_plain + 0.02);
  CHECK(pale_kept > far_kept - 0.08);  // about as pale beside the dark form as away from it
  CHECK(dark_kept < far_kept - 0.3);   // and the dark form was painted
  // Same scene, same painting.
  CHECK(same_image(create_artwork(photo, s, Hooks{}, &sc).image, kept.image));
}

TEST(drawing_media_worked_by_item_leave_no_gap_at_an_outline) {
  // A narrow pale form (a strap, an ear) in a dark surrounding, with a
  // mid-gray band so the picture has a full range of tone. Worked by item,
  // each item's marks end at its outline and each patch is judged by its
  // own item. That must not cost either form its tone or leave paper bare
  // along the outline: the pale form stays as pale as without a scene, and
  // the dark stays dark right up to it.
  const int w = 240, h = 160, x0 = 110, x1 = 124;
  Image photo(w, h);
  Scene sc;
  sc.items = {{1, "dark surrounding", 0, 1}, {2, "pale form", 0, 2}, {3, "band", 0, 1}};
  sc.item = Plane<uint8_t>(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      bool pale = x >= x0 && x < x1, band = y >= h - 24;
      photo(x, y) = band ? Rgb{0.35f, 0.35f, 0.35f} : (pale ? Rgb{0.90f, 0.86f, 0.70f} : Rgb{0.03f, 0.04f, 0.08f});
      sc.item(x, y) = band ? 3 : (pale ? 2 : 1);
    }
  auto strip = [&](const Image& img, int a, int b) {
    double acc = 0.0;
    for (int y = 12; y < h - 40; ++y)
      for (int x = a; x < b; ++x) acc += rgb_to_oklab(img(x, y)).L;
    return acc / (static_cast<double>(b - a) * (h - 52));
  };
  for (const char* medium : {"pastel", "colored_pencil"}) {
    Settings s;
    s.medium = medium;
    s.long_side_px = w;
    s.long_side_mm = 60.f;
    Artwork plain = create_artwork(photo, s);
    Artwork kept = create_artwork(photo, s, Hooks{}, &sc);
    double pale_plain = strip(plain.image, x0 + 2, x1 - 2), pale_kept = strip(kept.image, x0 + 2, x1 - 2);
    double beside_plain = strip(plain.image, x0 - 7, x0 - 1), beside_kept = strip(kept.image, x0 - 7, x0 - 1);
    double away_kept = strip(kept.image, x0 - 40, x0 - 20);
    std::printf("    %-15s without and with a scene: pale form %.3f, %.3f; the dark beside it %.3f, %.3f (%.3f away)\n",
                medium, pale_plain, pale_kept, beside_plain, beside_kept, away_kept);
    CHECK(!same_image(kept.image, plain.image));  // the scene is used
    CHECK(pale_kept > pale_plain - 0.03);
    CHECK(beside_kept < beside_plain + 0.05);
    CHECK(beside_kept < away_kept + 0.12);
    CHECK(same_image(create_artwork(photo, s, Hooks{}, &sc).image, kept.image));
  }
}

TEST(skin_is_painted_wet_in_wet_in_watercolor) {
  // A dark fold across a pale form. If the form is skin, the watercolor
  // artist dampens each place before laying color into it. The treatment
  // reaches the artist (the painting differs), the fold is still painted,
  // and the same scene gives the same painting.
  const int w = 220, h = 110;
  Image photo(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) photo(x, y) = x >= 104 && x < 116 ? Rgb{0.16f, 0.09f, 0.06f} : Rgb{0.78f, 0.58f, 0.46f};
  Settings s;
  s.medium = "watercolor";
  s.long_side_px = w;
  s.long_side_mm = 110.f;
  auto fold = [&](const Image& img) {
    double darkest = 1.0;
    for (int x = 100; x < 120; ++x) {
      double col = 0.0;
      for (int y = 15; y < h - 15; ++y) col += rgb_to_oklab(img(x, y)).L;
      darkest = std::min(darkest, col / (h - 30));
    }
    return darkest;
  };
  Scene sc;
  sc.items = {{1, "form", 0, 2}};
  sc.item = Plane<uint8_t>(w, h, 1);
  Artwork dry = create_artwork(photo, s, Hooks{}, &sc);
  sc.items[0].treatment = "skin";
  Artwork wet = create_artwork(photo, s, Hooks{}, &sc);
  std::printf("    the fold is as dark as %.2f on dry paper and %.2f as skin\n", fold(dry.image), fold(wet.image));
  CHECK(!same_image(dry.image, wet.image));
  CHECK(fold(wet.image) < 0.6);
  CHECK(same_image(create_artwork(photo, s, Hooks{}, &sc).image, wet.image));
}

namespace {

// Normals of simple surfaces, as the orientation layer states them: r to
// the right, g downward, b toward the viewer.
Image upright_cylinder(int w, int h) {  // its axis runs up the sheet
  Image n(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float s = std::clamp((x + 0.5f - 0.5f * w) / (0.55f * w), -0.95f, 0.95f);
      n(x, y) = {s, 0.f, std::sqrt(1.f - s * s)};
    }
  return n;
}
Image lying_cylinder(int w, int h) {  // its axis runs across the sheet
  Image n(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float s = std::clamp((y + 0.5f - 0.5f * h) / (0.55f * h), -0.95f, 0.95f);
      n(x, y) = {0.f, s, std::sqrt(1.f - s * s)};
    }
  return n;
}

// How much more the picture changes across the sheet than down it: the
// mean step of lightness between neighbors side by side, over that between
// neighbors one above the other. Strokes that run down the sheet raise it.
double across_over_down(const Image& img) {
  double ax = 0.0, ay = 0.0;
  for (int y = 8; y < img.h - 8; ++y)
    for (int x = 8; x < img.w - 8; ++x) {
      float l = rgb_to_oklab(img(x, y)).L;
      ax += std::abs(rgb_to_oklab(img(x + 1, y)).L - l);
      ay += std::abs(rgb_to_oklab(img(x, y + 1)).L - l);
    }
  return ax / std::max(ay, 1e-9);
}

}  // namespace

TEST(the_form_says_which_way_a_surface_runs) {
  const int w = 160, h = 120;
  Plane<uint8_t> one(w, h, 1);
  // A cylinder: along its axis, wherever it is curved.
  FlowField up = form_flow(upright_cylinder(w, h), one), lying = form_flow(lying_cylinder(w, h), one);
  for (int x : {40, 60, 100, 120}) {
    CHECK(std::abs(up.dir(x, h / 2).y) > 0.99f);
    CHECK(up.coherence(x, h / 2) > 0.9f && up.strength(x, h / 2) > 0.5f);
  }
  for (int y : {30, 45, 75, 90}) {
    CHECK(std::abs(lying.dir(w / 2, y).x) > 0.99f);
    CHECK(lying.coherence(w / 2, y) > 0.9f && lying.strength(w / 2, y) > 0.5f);
  }
  // A flat surface that faces the viewer squarely has no direction.
  FlowField facing = form_flow(Image(w, h, Rgb{0.f, 0.f, 1.f}), one);
  CHECK(facing.strength(w / 2, h / 2) < 0.01f);
  // A flat floor that recedes (its normal leans upward): level across the sheet.
  FlowField floor = form_flow(Image(w, h, Rgb{0.f, -0.8f, 0.6f}), one);
  CHECK(std::abs(floor.dir(w / 2, h / 2).x) > 0.99f && floor.coherence(w / 2, h / 2) > 0.9f);
  CHECK(floor.strength(w / 2, h / 2) > 0.12f);
  // A wall that recedes to one side: up and down.
  FlowField wall = form_flow(Image(w, h, Rgb{0.8f, 0.f, 0.6f}), one);
  CHECK(std::abs(wall.dir(w / 2, h / 2).y) > 0.99f);
  // The change of the normal from one item to the next is not curvature:
  // a floor and a wall that meet at an outline keep their own directions.
  Image corner(w, h);
  Plane<uint8_t> two(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      corner(x, y) = x < w / 2 ? Rgb{0.f, -0.8f, 0.6f} : Rgb{0.8f, 0.f, 0.6f};
      two(x, y) = x < w / 2 ? 1 : 2;
    }
  FlowField met = form_flow(corner, two);
  CHECK(std::abs(met.dir(w / 2 - 2, h / 2).x) > 0.99f && std::abs(met.dir(w / 2 + 2, h / 2).y) > 0.99f);

  // The artist takes the direction of the form only where the photo has none.
  FlowField flow;
  flow.dir = Plane<Vec2>(w, h, Vec2{1.f, 0.f});
  flow.coherence = Plane<float>(w, h, 0.f);
  flow.strength = Plane<float>(w, h, 0.f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w / 2; ++x) flow.coherence(x, y) = flow.strength(x, y) = 0.8f;  // the photo is clear here
  guide_by_form(flow, up);
  CHECK(std::abs(flow.dir(40, h / 2).x) > 0.99f);   // kept
  CHECK(std::abs(flow.dir(120, h / 2).y) > 0.99f);  // taken from the form
  CHECK(flow.coherence(120, h / 2) > 0.9f);
  FlowField untouched = flow;
  guide_by_form(untouched, FlowField{});  // no form: nothing changes
  CHECK(untouched.coherence.v == flow.coherence.v && untouched.strength.v == flow.strength.v);
}

TEST(a_scene_may_state_orientation) {
  TempDir tmp;
  tmp.write_layer("items.png", 40, 30, 2, 7);
  // The orientation layer: facing right in the left half, facing up in the right half.
  auto write_normals = [&](const char* name, int w, int h, int channels) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * channels, 128);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * channels];
        if (channels == 3) {
          p[0] = x < w / 2 ? 255 : 128;
          p[1] = x < w / 2 ? 128 : 0;
          p[2] = 128;
        }
      }
    stbi_write_png(tmp.path(name).c_str(), w, h, channels, px.data(), w * channels);
  };
  write_normals("normals.png", 80, 60, 3);
  write_normals("gray.png", 80, 60, 1);
  write_normals("tall.png", 60, 80, 3);
  std::string text = kGoodScene;
  size_t at = text.find("\"item_layer\"");
  CHECK(at != std::string::npos);
  auto with_layer = [&](const std::string& name) {
    std::string t = text;
    t.insert(at, "\"normal_layer\": " + name + ", ");
    return t;
  };
  tmp.write("form.scene.json", with_layer("\"normals.png\""));
  std::string err;
  Scene sc = load_scene(tmp.path("form.scene.json"), &err);
  CHECK(!sc.empty() && sc.normal.w == 80 && sc.normal.h == 60);
  if (!sc.normal.empty()) {
    CHECK_NEAR(sc.normal(10, 30).r, 1.0, 0.02);
    CHECK_NEAR(sc.normal(10, 30).g, 0.0, 0.02);
    CHECK_NEAR(sc.normal(70, 30).g, -1.0, 0.02);
    for (const Rgb& n : sc.normal.v) CHECK_NEAR(n.r * n.r + n.g * n.g + n.b * n.b, 1.0, 1e-3);
  }
  // A scene without the member has no orientation, and is still read.
  tmp.write("plain.scene.json", kGoodScene);
  CHECK(load_scene(tmp.path("plain.scene.json"), &err).normal.empty());
  auto refused = [&](const char* what, const std::string& name) {
    tmp.write("bad.scene.json", with_layer(name));
    std::string why;
    Scene bad = load_scene(tmp.path("bad.scene.json"), &why);
    if (!bad.empty() || why.empty()) std::printf("    not refused: %s\n", what);
    CHECK(bad.empty() && !why.empty());
  };
  refused("an orientation layer that is not RGB", "\"gray.png\"");
  refused("an orientation layer of another shape", "\"tall.png\"");
  refused("an orientation layer in another folder", "\"../normals.png\"");
  refused("an orientation layer that is missing", "\"absent.png\"");
  refused("an orientation layer that is not a name", "5");
}

TEST(strokes_follow_the_form_where_the_photo_is_plain) {
  // A plain subject gives the oil painter no direction. With a scene that
  // says the surface is an upright cylinder the strokes run down the sheet;
  // if it says the cylinder lies across, they run across.
  const int w = 200, h = 200;
  Image photo(w, h, Rgb{0.45f, 0.30f, 0.22f});
  Scene sc;
  sc.items = {{1, "form", 0, 2}};
  sc.item = Plane<uint8_t>(w, h, 1);
  Settings s;
  s.medium = "oil";
  s.long_side_px = w;
  s.long_side_mm = 100.f;
  s.effort = 0.6f;
  sc.normal = upright_cylinder(w, h);
  Artwork up = create_artwork(photo, s, Hooks{}, &sc);
  sc.normal = lying_cylinder(w, h);
  Artwork lying = create_artwork(photo, s, Hooks{}, &sc);
  double r_up = across_over_down(up.image), r_lying = across_over_down(lying.image);
  std::printf("    change across the sheet over change down it: %.2f upright, %.2f lying\n", r_up, r_lying);
  CHECK(r_up > 1.05 * r_lying);
  CHECK(r_up > 1.0 && r_lying < 1.0);
}
