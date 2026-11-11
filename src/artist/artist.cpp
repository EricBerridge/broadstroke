#include "artist/artist.hpp"

#include "artist/studio.hpp"

namespace bs {
namespace {

struct MediumInfo {
  const char* name;
  const char* surface;
  float long_mm;
  const char* blurb;
};

const MediumInfo kMedia[] = {
    {"oil", "canvas", 400.f, "oil paint, bristle brushes, layered from block-in to accents"},
    {"watercolor", "cold_press", 300.f, "transparent washes flowing and drying on paper, light to dark"},
    {"pencil", "drawing", 240.f, "graphite pencils: contour, then hatching built up in layers"},
    {"charcoal", "drawing", 400.f, "vine charcoal laid in and smudged, then compressed charcoal for the darks"},
    {"colored_pencil", "drawing", 240.f, "wax-based colored pencils layered light over light"},
    {"crayon", "drawing", 280.f, "wax crayons: broad, waxy marks that skip over the paper grain"},
    {"pastel", "pastel_paper", 400.f, "soft pastel sticks on toned, toothy paper"},
};

const MediumInfo* find_medium(const std::string& name) {
  for (const MediumInfo& m : kMedia)
    if (name == m.name) return &m;
  return nullptr;
}

}  // namespace

std::vector<std::string> medium_names() {
  std::vector<std::string> v;
  for (const MediumInfo& m : kMedia) v.push_back(m.name);
  return v;
}

bool is_medium_name(const std::string& name) { return find_medium(name) != nullptr; }

std::string default_surface(const std::string& medium) {
  const MediumInfo* m = find_medium(medium);
  return m ? m->surface : "drawing";
}

std::string describe_medium(const std::string& medium) {
  const MediumInfo* m = find_medium(medium);
  return m ? m->blurb : "";
}

Artwork create_artwork(const Image& photo, const Settings& settings_in, const Hooks& hooks) {
  Timer timer;
  Artwork art;
  if (photo.empty()) return art;
  Settings settings = settings_in;
  const MediumInfo* info = find_medium(settings.medium);
  if (!info) {
    info = &kMedia[0];
    settings.medium = info->name;
  }
  if (settings.surface.empty() || !is_surface_name(settings.surface)) settings.surface = info->surface;
  settings.long_side_px = std::clamp(settings.long_side_px, 160, 6000);
  settings.effort = std::clamp(settings.effort, 0.3f, 2.f);
  float long_mm = settings.long_side_mm > 0.f ? settings.long_side_mm : info->long_mm;

  // Canvas grid: the photo's aspect ratio at the requested resolution.
  float scale = static_cast<float>(settings.long_side_px) / std::max(photo.w, photo.h);
  int w = std::max(16, static_cast<int>(std::round(photo.w * scale)));
  int h = std::max(16, static_cast<int>(std::round(photo.h * scale)));
  float mm_per_px = long_mm / std::max(w, h);

  Palette palette;
  if (settings.medium == "oil") palette = oil_palette();
  else if (settings.medium == "watercolor") palette = watercolor_palette();
  else if (settings.medium == "pencil") palette = graphite_palette();
  else if (settings.medium == "charcoal") palette = charcoal_palette();
  else palette = stick_palette();

  Canvas canvas(make_surface(settings.surface, w, h, mm_per_px, settings.seed), palette);
  if (settings.medium != "oil") {
    // Works on paper are viewed in flatter light than a varnished canvas.
    canvas.light.ambient = 0.7f;
    canvas.light.elevation = 0.9f;
  }
  Subject subject = observe(photo, w, h);
  Studio studio(subject, canvas, settings, hooks);

  if (settings.medium == "oil") {
    canvas.gloss = 0.10f;
    paint_oil(studio);
  } else if (settings.medium == "watercolor") {
    canvas.film_relief = 0.f;
    paint_watercolor(studio);
  } else {
    draw_dry(studio, settings.medium);
  }

  art.image = canvas.render();
  art.strokes = studio.strokes;
  art.seconds = timer.seconds();
  art.medium = settings.medium;
  art.surface = settings.surface;
  art.width = w; art.height = h;
  art.mm_per_px = mm_per_px;
  art.cancelled = studio.cancelled;
  art.notes = studio.notes;
  if (hooks.on_progress) {
    Progress p;
    p.stage = studio.cancelled ? "stopped" : "finished";
    p.fraction = 1.f;
    p.strokes = studio.strokes;
    hooks.on_progress(p, canvas);
  }
  return art;
}

}  // namespace bs
