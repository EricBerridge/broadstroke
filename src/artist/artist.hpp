// The artist: looks at a photograph and makes an artwork of it by performing
// strokes with simulated tools on a simulated surface.
//
// The artist works closed-loop, the way a person does: decide a mark, make it
// through the physical simulation, look at what actually happened on the
// canvas, and choose the next mark from the difference between the canvas
// and the subject. No pixel of the photograph is ever copied to the output.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "canvas/canvas.hpp"
#include "core/image.hpp"
#include "vision/scene.hpp"

namespace bs {

// Techniques an artist may be told not to use. Each is on unless switched
// off here; nothing in the command line or the window sets them yet.
struct Techniques {
  bool oil_blend_sky = true;  // oil, with a scene: sky and cloud are blended with a clean soft brush (spec 2.13)
};

struct Settings {
  std::string medium = "oil";  // see medium_names()
  std::string surface;         // empty: the medium's customary support
  int long_side_px = 1400;     // simulation and output resolution
  float long_side_mm = 0.f;    // physical size of the artwork; 0: medium default
  uint32_t seed = 1;
  float effort = 1.f;          // 0.4 = quick study .. 1 = finished piece .. 1.6 = meticulous
  Techniques techniques;
};

struct Progress {
  std::string stage;   // what the artist is doing
  float fraction = 0;  // 0..1 overall
  int strokes = 0;     // strokes made so far
};

struct Hooks {
  // Called between strokes (throttled by the caller's needs). The canvas may
  // be rendered from inside the callback. Return false to stop early; the
  // artwork made so far is still returned.
  std::function<bool(const Progress&, const Canvas&)> on_progress;
};

struct Artwork {
  Image image;         // the finished piece, lit
  int strokes = 0;
  double seconds = 0;
  std::string medium, surface;
  int width = 0, height = 0;
  float mm_per_px = 0;
  bool cancelled = false;
  std::vector<std::string> notes;  // what the artist did, stage by stage
};

std::vector<std::string> medium_names();
bool is_medium_name(const std::string& name);
std::string default_surface(const std::string& medium);
std::string describe_medium(const std::string& medium);

// `scene` (spec 2.13) says what the photo shows; it is input like the photo
// and may be null. A scene whose item layer does not have the photo's
// proportions is not used, and the notes say so.
Artwork create_artwork(const Image& photo, const Settings& settings, const Hooks& hooks = Hooks{},
                       const Scene* scene = nullptr);

}  // namespace bs
