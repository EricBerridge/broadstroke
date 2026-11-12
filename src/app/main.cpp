// Command-line front end.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/gui.hpp"
#include "app/studies.hpp"
#include "artist/artist.hpp"
#include "surface/surface.hpp"

namespace {

void usage() {
  std::printf(
      "Broadstroke - turns a photograph into artwork by simulating the artist's strokes.\n\n"
      "usage:\n"
      "  broadstroke paint <photo> [options]   make an artwork from a photo\n"
      "  broadstroke gui [photo]               open the studio window (Windows)\n"
      "  broadstroke study <name> <out.png>    render a physics study\n"
      "  broadstroke list                      list media, surfaces and studies\n\n"
      "paint options:\n"
      "  -m, --medium NAME     oil (default), watercolor, pastel\n"
      "  -s, --surface NAME    support; default depends on the medium\n"
      "  -o, --output PATH     .png, .jpg or .bmp (default: <photo>_<medium>.png)\n"
      "      --size PX         long side of the artwork in pixels (default 1400)\n"
      "      --mm MM           physical long side in millimeters (default depends on the medium)\n"
      "      --effort X        0.4 quick study .. 1 finished (default) .. 1.6 meticulous\n"
      "      --seed N          random seed; same seed, same artwork\n"
      "      --scene PATH      scene file saying what the photo shows\n"
      "                        (default: <photo>.scene.json beside the photo, if there is one)\n"
      "      --no-scene        do not use a scene file\n"
      "      --frames DIR      also save progress frames into DIR\n"
      "      --frame-every N   strokes between progress frames (default 400)\n");
}

std::string stem(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return path;
  return path.substr(0, dot);
}

int cmd_list() {
  std::printf("media:\n");
  for (const auto& m : bs::medium_names())
    std::printf("  %-15s %s (default surface: %s)\n", m.c_str(), bs::describe_medium(m).c_str(),
                bs::default_surface(m).c_str());
  std::printf("surfaces:\n ");
  for (const auto& s : bs::surface_names()) std::printf(" %s", s.c_str());
  std::printf("\nstudies:\n ");
  for (const auto& s : bs::study_names()) std::printf(" %s", s.c_str());
  std::printf("\n");
  return 0;
}

int cmd_paint(int argc, char** argv) {
  if (argc < 3) {
    usage();
    return 2;
  }
  std::string photo_path = argv[2], out_path, frames_dir, scene_path;
  bs::Settings settings;
  int frame_every = 400;
  bool use_scene = true;
  for (int i = 3; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", what);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "-m" || a == "--medium") settings.medium = next("--medium");
    else if (a == "-s" || a == "--surface") settings.surface = next("--surface");
    else if (a == "-o" || a == "--output") out_path = next("--output");
    else if (a == "--size") settings.long_side_px = std::atoi(next("--size"));
    else if (a == "--mm") settings.long_side_mm = static_cast<float>(std::atof(next("--mm")));
    else if (a == "--effort") settings.effort = static_cast<float>(std::atof(next("--effort")));
    else if (a == "--seed") settings.seed = static_cast<uint32_t>(std::strtoul(next("--seed"), nullptr, 10));
    else if (a == "--scene") scene_path = next("--scene");
    else if (a == "--no-scene") use_scene = false;
    else if (a == "--frames") frames_dir = next("--frames");
    else if (a == "--frame-every") frame_every = std::max(1, std::atoi(next("--frame-every")));
    else {
      std::fprintf(stderr, "unknown option '%s'\n", a.c_str());
      return 2;
    }
  }
  if (!bs::is_medium_name(settings.medium)) {
    std::fprintf(stderr, "unknown medium '%s' (try: broadstroke list)\n", settings.medium.c_str());
    return 2;
  }
  if (!settings.surface.empty() && !bs::is_surface_name(settings.surface)) {
    std::fprintf(stderr, "unknown surface '%s' (try: broadstroke list)\n", settings.surface.c_str());
    return 2;
  }
  if (out_path.empty()) out_path = stem(photo_path) + "_" + settings.medium + ".png";

  std::string err;
  bs::Image photo = bs::load_image(photo_path, &err);
  if (photo.empty()) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }

  // The scene file is input like the photo: one that is named, or found
  // beside the photo, must be usable, or nothing is painted.
  bs::Scene scene;
  if (use_scene && scene_path.empty() && bs::file_exists(bs::scene_path_for(photo_path)))
    scene_path = bs::scene_path_for(photo_path);
  if (use_scene && !scene_path.empty()) {
    scene = bs::load_scene(scene_path, &err);
    if (scene.empty()) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    if (!scene.fits(photo.w, photo.h)) {
      std::fprintf(stderr, "scene file '%s': its item layer (%d x %d) does not have the photo's proportions (%d x %d)\n",
                   scene_path.c_str(), scene.item.w, scene.item.h, photo.w, photo.h);
      return 1;
    }
  }

  bs::Hooks hooks;
  int last_frame_strokes = -1000000, frame_no = 0, last_pct = -1;
  std::string last_stage;
  hooks.on_progress = [&](const bs::Progress& p, const bs::Canvas& canvas) {
    int pct = static_cast<int>(p.fraction * 100.f);
    if (p.stage != last_stage || pct != last_pct) {
      std::printf("\r  %3d%%  %-34s %7d strokes", pct, p.stage.c_str(), p.strokes);
      std::fflush(stdout);
      last_stage = p.stage;
      last_pct = pct;
    }
    if (!frames_dir.empty() && p.strokes - last_frame_strokes >= frame_every) {
      last_frame_strokes = p.strokes;
      char name[64];
      std::snprintf(name, sizeof name, "/frame_%04d.jpg", frame_no++);
      bs::save_image(frames_dir + name, canvas.render());
    }
    return true;
  };

  std::printf("%s on %s\n", settings.medium.c_str(),
              (settings.surface.empty() ? bs::default_surface(settings.medium) : settings.surface).c_str());
  bs::Artwork art = bs::create_artwork(photo, settings, hooks, scene.empty() ? nullptr : &scene);
  std::printf("\n");
  if (art.image.empty()) {
    std::fprintf(stderr, "nothing was painted\n");
    return 1;
  }
  for (const auto& n : art.notes) std::printf("  %s\n", n.c_str());
  if (!bs::save_image(out_path, art.image, &err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  std::printf("wrote %s  (%d x %d px, %.0f x %.0f mm, %d strokes, %.1f s)\n", out_path.c_str(), art.width,
              art.height, art.width * art.mm_per_px, art.height * art.mm_per_px, art.strokes, art.seconds);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  std::string cmd = argv[1];
  if (cmd == "paint") return cmd_paint(argc, argv);
  if (cmd == "list") return cmd_list();
  if (cmd == "gui") return bs::run_gui(argc >= 3 ? argv[2] : "");
  if (cmd == "study") {
    if (argc < 4) {
      usage();
      return 2;
    }
    return bs::run_study(argv[2], argv[3]) ? 0 : 1;
  }
  if (cmd == "-h" || cmd == "--help" || cmd == "help") {
    usage();
    return 0;
  }
  std::fprintf(stderr, "unknown command '%s'\n\n", cmd.c_str());
  usage();
  return 2;
}
