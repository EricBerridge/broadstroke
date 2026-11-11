// Shared working state for the per-medium artists: the subject, the canvas,
// the artist's current view of the canvas, and progress reporting.
#pragma once

#include "artist/artist.hpp"
#include "vision/analysis.hpp"

namespace bs {

struct Studio {
  Studio(const Subject& subject_, Canvas& canvas_, const Settings& settings_, const Hooks& hooks_)
      : subject(subject_), canvas(canvas_), settings(settings_), hooks(hooks_), rng(settings_.seed * 9781u + 7u),
        seen(canvas_.w, canvas_.h) {
    canvas.clear_dirty();
    canvas.render_albedo({0, 0, canvas.w, canvas.h}, seen);
  }

  const Subject& subject;
  Canvas& canvas;
  const Settings& settings;
  const Hooks& hooks;
  Rng rng;
  Image seen;        // unlit canvas color as last looked at
  int strokes = 0;
  bool cancelled = false;
  float stage_lo = 0.f, stage_hi = 1.f;  // overall-progress window of the current stage
  std::string stage;

  // Looks at the parts of the canvas that changed.
  void look() { canvas.refresh_albedo(seen); }

  void begin_stage(const std::string& name, float lo, float hi) {
    stage = name; stage_lo = lo; stage_hi = hi;
    report(0.f, true);
  }

  // Reports progress within the current stage (t in 0..1). Returns false if
  // the user asked to stop.
  bool report(float t, bool force = false) {
    if (cancelled) return false;
    if (!hooks.on_progress) return true;
    // Report every few strokes, and at least every quarter second, so slow
    // strokes (a big brush) do not starve the caller of updates.
    if (!force && (strokes - last_report_) < report_every && clock_.seconds() - last_time_ < 0.25) return true;
    last_report_ = strokes;
    last_time_ = clock_.seconds();
    Progress p;
    p.stage = stage;
    p.fraction = stage_lo + (stage_hi - stage_lo) * clamp01(t);
    p.strokes = strokes;
    if (!hooks.on_progress(p, canvas)) cancelled = true;
    return !cancelled;
  }
  int report_every = 8;

  // Working notes (what was done in each stage), returned with the artwork.
  std::vector<std::string> notes;
  void note(const std::string& text) { notes.push_back(text); }

  // Mean subject / canvas color in a small disc (the artist's glance at a spot).
  Rgb glance(const Image& img, Vec2 p, float radius) const {
    static const float off[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                    {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
    Rgb acc;
    for (auto& o : off) {
      int x = std::clamp(static_cast<int>(p.x + o[0] * radius), 0, img.w - 1);
      int y = std::clamp(static_cast<int>(p.y + o[1] * radius), 0, img.h - 1);
      acc += img(x, y);
    }
    return acc * (1.f / 9.f);
  }

 private:
  int last_report_ = 0;
  Timer clock_;
  double last_time_ = 0.0;
};

// Per-medium artists. Each works on studio.canvas until done or cancelled.
void paint_oil(Studio& studio);
void paint_watercolor(Studio& studio);
// Dry drawing media: "colored_pencil", "pastel".
void draw_dry(Studio& studio, const std::string& medium);

}  // namespace bs
