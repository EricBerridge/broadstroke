// The state of the artwork: the support plus everything laid on it.
//
// Each cell holds two strata:
//   * base / dry_h - whatever has already dried, baked to a reflectance
//     spectrum and a relief thickness. New work sits on top of it.
//   * pig / vol    - the active film: per-pigment amounts and total thickness
//     (pigment plus vehicle or binder). Wet paint, loose pastel dust, graphite
//     and wax all live here; tools add to it, drag it around and lift it.
// The visible color is the Kubelka-Munk reflectance of the active film over
// the base. The visible relief is surface + dry_h + film.
#pragma once

#include "core/image.hpp"
#include "optics/pigment.hpp"
#include "surface/surface.hpp"

namespace bs {

struct Light {
  Vec2 dir{-0.62f, -0.62f};  // direction the light comes from, in image space
  float elevation = 0.75f;   // radians above the surface plane
  float ambient = 0.55f;     // share of non-directional light
  float relief_gain = 1.0f;  // exaggeration of surface slopes (1 = physical)
};

class Canvas {
 public:
  Canvas(Surface surface, Palette palette);

  int w = 0, h = 0;
  float mm_per_px = 0.25f;
  Surface surface;
  Palette palette;

  Plane<Spectrum> base;
  Plane<float> dry_h;
  Plane<PigVec> pig;
  Plane<float> vol;

  // Micro-relief of surface + dried work relative to its local mean (mm).
  // Tools use it to decide which cells they touch at a given pressure.
  Plane<float> tooth;

  float film_relief = 1.f;  // 1: film thickness is geometric relief; 0: stain only
  float gloss = 0.f;        // specular strength of a thick film
  float gloss_thickness = 0.05f;  // film thickness (mm) at which gloss saturates

  // Records that cells in this region changed color. Tracked per cell, so
  // a long diagonal stroke does not mark its whole bounding box.
  void mark_dirty(const Rect& r);
  // The same in two parts, for tools working on different parts of the
  // canvas at the same time: mark_dirty_cells may be called from several
  // threads as long as their regions do not overlap; note_dirty then
  // records the region those marks lie in, from one thread.
  void mark_dirty_cells(const Rect& r);
  void note_dirty(const Rect& r) { dirty_bounds_.include(r.clipped(w, h)); }
  // Re-renders the unlit color of every cell marked since the last call
  // into `out` (the artist looking at what changed) and clears the marks.
  void refresh_albedo(Image& out);
  void clear_dirty();

  // Height of the solid/wet top surface at a cell (mm).
  float top(int x, int y) const { return surface.height(x, y) + dry_h(x, y) + film_relief * vol(x, y); }

  // Bakes the active film into the dry stratum (the paint dries). `shrink` is
  // the fraction of film thickness that remains as relief.
  void dry(float shrink = 1.f);
  void refresh_tooth();

  Spectrum reflectance(int x, int y) const;
  Rgb albedo(int x, int y) const { return spectrum_to_rgb(reflectance(x, y)); }

  // Unlit color of a region, as the artist judges it against the subject.
  void render_albedo(const Rect& r, Image& out) const;
  // The finished look: albedo shaded by the relief under a raking light.
  Image render(const Light& light) const;
  // Rendered under this canvas's own viewing light.
  Image render() const { return render(light); }
  Light light;  // how the piece is lit when viewed (set per medium)

  // Optional additional pigment (e.g. still suspended in water) drawn with the film.
  const Plane<PigVec>* overlay = nullptr;

 private:
  Plane<uint8_t> dirty_;  // per cell
  Rect dirty_bounds_;     // bounding box of the marked cells
};

}  // namespace bs
