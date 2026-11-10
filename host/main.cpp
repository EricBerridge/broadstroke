// Broadstroke front-end. Two top-level commands:
//
//   broadstroke paint <image> [flags]
//       Load a photo, run the analyzer, plan a stroke sequence in the
//       chosen style, and paint it onto a procedural substrate.
//
//   broadstroke scene <name> [flags]
//       Render one of the canned demo scenes (pencil | watercolor |
//       mixed) for inspecting the physics directly.
//
// Flags (both commands):
//   --substrate NAME            cold_press | canvas | smooth_bristol  (default cold_press)
//   --output PATH               .bmp / .png / .jpg / .jpeg            (default broadstroke.bmp / painted.bmp)
//   --validate                  request Vulkan validation diagnostics
//   --device-info               print the selected GPU and workgroup variant
//
// Flags (paint only):
//   --style NAME                watercolor | pencil_sketch | pastel_drawing  (default watercolor)
//   --snapshot-every N          write the current canvas every N steps; 0 = off
//   --snapshot-prefix PREFIX    snapshots are <prefix>_NNNN.<ext>      (default snapshot)
//
// Backward-compatible shortcut: a bare positional first arg matching one
// of the canned scene names runs the scripted scene with positional
// substrate / output, the way Stage 4 worked.

#include "broadstroke/analyzer/analyze.hpp"
#include "broadstroke/analyzer/load.hpp"
#include "broadstroke/build_config.hpp"
#include "broadstroke/compute/context.hpp"
#include "broadstroke/planner/plan.hpp"
#include "broadstroke/support/generate.hpp"
#include "broadstroke/support/preset.hpp"
#include "broadstroke/support/support_data.hpp"
#include "broadstroke/tools/brush.hpp"
#include "broadstroke/tools/pastel.hpp"
#include "broadstroke/tools/pencil.hpp"
#include "broadstroke/tools/pigment.hpp"
#include "broadstroke/tools/stroke.hpp"

#include "apply_plan.hpp"
#include "canvas.hpp"
#include "io/image_writer.hpp"
#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kDefaultCanvas = 1024;
constexpr std::uint64_t kSeed          = 0xB1ADE9001A2B3C4Dull;
constexpr float kLightElevationDeg     = 18.0f;
constexpr float kLightAzimuthDeg       = 135.0f;
constexpr float kAmbient               = 0.92f;
constexpr float kHeightScale           = 3.5f;

namespace tk  = broadstroke::tools;
namespace bh  = broadstroke::host;
namespace ba  = broadstroke::analyzer;
namespace bpl = broadstroke::planner;

std::array<float, 3> rakingLightDir() {
  const float elev = kLightElevationDeg * std::numbers::pi_v<float> / 180.0f;
  const float az   = kLightAzimuthDeg   * std::numbers::pi_v<float> / 180.0f;
  const float c = std::cos(elev);
  return { c * std::cos(az), c * std::sin(az), std::sin(elev) };
}

inline std::uint8_t toByte(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

void rgbaToRgbBytes(const std::vector<float>& rgba,
                    std::vector<std::uint8_t>& out) {
  out.resize(rgba.size() / 4 * 3);
  for (std::size_t i = 0, n = rgba.size() / 4; i < n; ++i) {
    out[i * 3 + 0] = toByte(rgba[i * 4 + 0]);
    out[i * 3 + 1] = toByte(rgba[i * 4 + 1]);
    out[i * 3 + 2] = toByte(rgba[i * 4 + 2]);
  }
}

bool writeRgbaImage(const std::filesystem::path& path,
                    std::uint32_t w, std::uint32_t h,
                    const std::vector<float>& rgba) {
  std::vector<std::uint8_t> rgb;
  rgbaToRgbBytes(rgba, rgb);
  return broadstroke::io::writeImage(path, w, h, rgb);
}

void printDeviceProfile(const broadstroke::compute::Context& ctx) {
  const auto& profile = ctx.profile();
  const auto wg = profile.workgroup_2d;
  std::printf("device:   %s\n", profile.device_name.c_str());
  std::printf("tuning:   subgroup=%u max-workgroup=%u local=%ux%u\n",
              profile.subgroup_size, profile.max_workgroup_invocations,
              wg.local_x, wg.local_y);
  if (profile.validation_requested) {
    std::printf("validate: VK_LAYER_KHRONOS_validation %s\n",
                profile.validation_layer_available ? "available" : "not found");
  }
}

// -- Canned scenes (Stage 4 demos) ----------------------------------------

void scenePencil(bh::Canvas& canvas) {
  auto pencil = tk::loadPencil("pencil_2b");
  const auto layer = canvas.add_graphite_layer(pencil.color);
  {
    tk::Stroke s; constexpr int N = 80;
    for (int i = 0; i <= N; ++i) {
      const float t = static_cast<float>(i) / N;
      s.points.push_back({120.0f + 780.0f * t,
                          320.0f + 140.0f * std::sin(t * 6.2831853f),
                          std::clamp(0.25f + 0.7f * std::sin(t * std::numbers::pi_v<float>),
                                     0.1f, 0.98f)});
    }
    canvas.stroke_pencil(layer, s, pencil);
  }
  for (int k = 0; k < 6; ++k) {
    tk::Stroke s;
    s.points = {{740.0f + k * 14.0f, 820.0f, 0.55f},
                {850.0f + k * 14.0f, 930.0f, 0.85f}};
    canvas.stroke_pencil(layer, s, pencil);
  }
  for (int k = 0; k < 6; ++k) {
    tk::Stroke s;
    s.points = {{760.0f, 820.0f + k * 14.0f, 0.55f},
                {870.0f, 930.0f + k * 14.0f, 0.85f}};
    canvas.stroke_pencil(layer, s, pencil);
  }
}

void sceneWatercolor(bh::Canvas& canvas) {
  auto brush_round = tk::loadBrush("brush_round");
  auto brush_wash  = tk::loadBrush("brush_wash");
  auto sienna   = tk::loadPigment("burnt_sienna");
  auto ultramar = tk::loadPigment("ultramarine");
  auto yellow   = tk::loadPigment("cadmium_yellow");

  const auto sienna_layer = canvas.add_pigment_layer(sienna.K,   sienna.S);
  const auto ultra_layer  = canvas.add_pigment_layer(ultramar.K, ultramar.S);
  const auto yellow_layer = canvas.add_pigment_layer(yellow.K,   yellow.S);

  {
    tk::Stroke s; constexpr int N = 60;
    for (int i = 0; i <= N; ++i) {
      const float t = static_cast<float>(i) / N;
      const float ang = -1.4f + 2.6f * t;
      s.points.push_back({320.0f + 240.0f * std::cos(ang),
                          480.0f + 240.0f * std::sin(ang),
                          0.55f + 0.3f * std::sin(t * std::numbers::pi_v<float>)});
    }
    canvas.stroke_watercolor(sienna_layer, s, brush_wash);
  }
  canvas.diffuse(sienna_layer, 14);
  {
    tk::Stroke s;
    s.points = {{640.0f, 130.0f, 0.4f}, {760.0f, 250.0f, 0.85f}, {900.0f, 380.0f, 0.65f}};
    canvas.stroke_watercolor(ultra_layer, s, brush_wash);
  }
  canvas.diffuse(ultra_layer, 12);
  {
    tk::Stroke s; constexpr int N = 40;
    for (int i = 0; i <= N; ++i) {
      const float t = static_cast<float>(i) / N;
      const float ang = t * 6.2831853f;
      s.points.push_back({560.0f + 180.0f * std::cos(ang),
                          760.0f + 100.0f * std::sin(ang), 0.7f});
    }
    canvas.stroke_watercolor(yellow_layer, s, brush_round);
  }
  canvas.diffuse(yellow_layer, 16);
  canvas.diffuse(sienna_layer, 8);
  canvas.diffuse(ultra_layer,  8);
}

void sceneMixed(bh::Canvas& canvas) {
  auto pastel_sienna = tk::loadPastel("pastel_sienna");
  auto pastel_indigo = tk::loadPastel("pastel_indigo");
  auto pencil_4b     = tk::loadPencil("pencil_4b");
  auto brush_wash    = tk::loadBrush("brush_wash");
  auto ultramar      = tk::loadPigment("ultramarine");

  const auto pastel_layer = canvas.add_pigment_layer(
      pastel_sienna.K, pastel_sienna.S, 2.5f);
  {
    tk::Stroke s; constexpr int N = 24;
    for (int i = 0; i <= N; ++i) {
      const float t = static_cast<float>(i) / N;
      s.points.push_back({160.0f + 720.0f * t,
                          200.0f + 80.0f * std::sin(t * 3.0f),
                          0.55f + 0.2f * std::sin(t * std::numbers::pi_v<float>)});
    }
    canvas.stroke_pastel(pastel_layer, s, pastel_sienna);
  }
  canvas.smear_pastel(pastel_layer, 200.0f, 240.0f, 880.0f, 240.0f, pastel_sienna);
  {
    tk::Stroke s;
    s.points = {{300.0f, 200.0f, 0.6f}, {500.0f, 360.0f, 0.85f}, {700.0f, 280.0f, 0.55f}};
    canvas.stroke_pastel(pastel_layer, s, pastel_indigo);
  }

  const auto wash_layer = canvas.add_pigment_layer(ultramar.K, ultramar.S, 1.6f);
  {
    tk::Stroke s; constexpr int N = 50;
    for (int i = 0; i <= N; ++i) {
      const float t = static_cast<float>(i) / N;
      s.points.push_back({150.0f + 760.0f * t,
                          540.0f + 140.0f * std::sin(t * 4.0f),
                          0.5f + 0.35f * std::sin(t * std::numbers::pi_v<float>)});
    }
    canvas.stroke_watercolor(wash_layer, s, brush_wash);
  }
  canvas.diffuse(wash_layer, 18);

  const auto pencil_layer = canvas.add_graphite_layer(pencil_4b.color);
  for (int k = 0; k < 8; ++k) {
    tk::Stroke s;
    const float y0 = 800.0f + k * 18.0f;
    s.points = {{220.0f, y0, 0.4f}, {820.0f, y0 + 28.0f, 0.85f}};
    canvas.stroke_pencil(pencil_layer, s, pencil_4b);
  }
}

// -- Palette helpers for the planner --------------------------------------

std::vector<bpl::Pigment> watercolorPalette() {
  auto sienna   = tk::loadPigment("burnt_sienna");
  auto ultramar = tk::loadPigment("ultramarine");
  auto yellow   = tk::loadPigment("cadmium_yellow");
  return {
    {"burnt_sienna",   sienna.K,   sienna.S,   sienna.swatch},
    {"ultramarine",    ultramar.K, ultramar.S, ultramar.swatch},
    {"cadmium_yellow", yellow.K,   yellow.S,   yellow.swatch},
  };
}

// For pastel, Pigment::name carries the *pastel preset name* (loaded by
// the executor as a tools::PastelPreset). Swatch is approximated from the
// pastel's body color via R_inf at full thickness.
std::vector<bpl::Pigment> pastelPalette() {
  std::vector<bpl::Pigment> out;
  for (const auto* name : {"pastel_sienna", "pastel_indigo"}) {
    auto p = tk::loadPastel(name);
    // R_inf channel-wise from KM closed form, c=1 effective:
    // ratio = K/S → R_inf = 1 + ratio - sqrt(ratio² + 2*ratio).
    std::array<float, 3> swatch{};
    for (int ch = 0; ch < 3; ++ch) {
      const float r = p.K[ch] / std::max(p.S[ch], 1e-4f);
      swatch[ch] = std::clamp(1.0f + r - std::sqrt(r * r + 2.0f * r), 0.0f, 1.0f);
    }
    out.push_back({name, p.K, p.S, swatch});
  }
  return out;
}

// -- Snapshot writer -------------------------------------------------------

struct SnapshotWriter {
  std::filesystem::path prefix;       // e.g. "snapshot"
  std::string           extension;    // ".bmp", ".png", ...
  std::uint32_t         every = 0;    // 0 disables
  std::uint32_t         width  = 0;
  std::uint32_t         height = 0;
  std::array<float, 3>  light_dir{};
  std::uint32_t         frame = 0;

  void operator()(bh::Canvas& canvas, std::size_t step, std::size_t total) {
    if (every == 0) return;
    if (step % every != 0 && step != total) return;
    const std::filesystem::path out =
        prefix.string() + "_" + std::string(4 - std::min<std::size_t>(4, std::to_string(frame).size()), '0') +
        std::to_string(frame) + extension;
    auto rgba = canvas.compose(light_dir, kAmbient, kHeightScale);
    std::vector<std::uint8_t> rgb;
    rgbaToRgbBytes(rgba, rgb);
    broadstroke::io::writeImage(out, width, height, rgb);
    std::printf("  snapshot %s (step %zu/%zu)\n",
                out.string().c_str(), step, total);
    ++frame;
  }
};

// -- Drivers ---------------------------------------------------------------

int runScene(const std::string& scene, const std::string& supportName,
             const std::filesystem::path& outPath,
             broadstroke::compute::ContextOptions contextOptions,
             bool deviceInfo) {
  std::printf("scene:    %s\n", scene.c_str());
  std::printf("support:  %s\n", supportName.c_str());
  std::printf("size:     %u x %u\n", kDefaultCanvas, kDefaultCanvas);
  std::printf("output:   %s\n", outPath.string().c_str());

  auto support = broadstroke::support::loadPreset(supportName);
  broadstroke::support::SupportData data;
  data.width = kDefaultCanvas; data.height = kDefaultCanvas;
  broadstroke::support::generate(data, support, kSeed);

  broadstroke::compute::Context ctx(contextOptions);
  if (deviceInfo || contextOptions.validate) {
    printDeviceProfile(ctx);
  }
  bh::Canvas canvas(ctx, data, support.base_color);

  if      (scene == "pencil")     scenePencil(canvas);
  else if (scene == "watercolor") sceneWatercolor(canvas);
  else if (scene == "mixed")      sceneMixed(canvas);
  else {
    std::fprintf(stderr, "unknown scene '%s' (try pencil|watercolor|mixed)\n",
                 scene.c_str());
    return 3;
  }

  auto rgba = canvas.compose(rakingLightDir(), kAmbient, kHeightScale);
  if (!writeRgbaImage(outPath, kDefaultCanvas, kDefaultCanvas, rgba)) {
    std::fprintf(stderr, "failed to write %s\n", outPath.string().c_str());
    return 1;
  }
  std::printf("wrote %s\n", outPath.string().c_str());
  return 0;
}

bpl::RenderPlan buildPlan(const std::string& style,
                          const ba::Analysis& analysis) {
  if (style == "watercolor") {
    bpl::PlanOptions opt;
    return bpl::plan_watercolor(analysis, watercolorPalette(), opt);
  }
  if (style == "pencil_sketch") {
    bpl::PencilSketchOptions opt;
    return bpl::plan_pencil_sketch(analysis, opt);
  }
  if (style == "pastel_drawing") {
    bpl::PastelDrawingOptions opt;
    opt.smear_strokes = true;
    return bpl::plan_pastel_drawing(analysis, pastelPalette(), opt);
  }
  throw std::runtime_error("unknown style: " + style +
                           " (try watercolor|pencil_sketch|pastel_drawing)");
}

int runPaint(const std::filesystem::path& imagePath,
             const std::string& style,
             const std::string& supportName,
             const std::filesystem::path& outPath,
             std::uint32_t snapshot_every,
             const std::filesystem::path& snapshot_prefix,
             broadstroke::compute::ContextOptions contextOptions,
             bool deviceInfo) {
  std::printf("scene:    paint\n");
  std::printf("style:    %s\n", style.c_str());
  std::printf("input:    %s\n", imagePath.string().c_str());
  std::printf("support:  %s\n", supportName.c_str());
  std::printf("output:   %s\n", outPath.string().c_str());
  if (snapshot_every) {
    std::printf("snapshot: every %u steps, prefix=%s\n",
                snapshot_every, snapshot_prefix.string().c_str());
  }

  using clk = std::chrono::steady_clock;
  const auto t0 = clk::now();

  auto img    = ba::loadImage(imagePath);
  auto fitted = ba::fitWithin(img, kDefaultCanvas);
  std::printf("source:   %u x %u → working size %u x %u\n",
              img.width, img.height, fitted.width, fitted.height);

  auto analysis = ba::analyze(fitted);
  const auto t_analyze = clk::now();
  std::printf("analyze:  %.2f s\n",
              std::chrono::duration<double>(t_analyze - t0).count());

  auto plan = buildPlan(style, analysis);
  const auto t_plan = clk::now();
  std::printf("plan:     %zu layers, %zu steps  (%.2f s)\n",
              plan.layers.size(), plan.steps.size(),
              std::chrono::duration<double>(t_plan - t_analyze).count());

  auto support = broadstroke::support::loadPreset(supportName);
  broadstroke::support::SupportData data;
  data.width  = fitted.width;
  data.height = fitted.height;
  broadstroke::support::generate(data, support, kSeed);

  broadstroke::compute::Context ctx(contextOptions);
  if (deviceInfo || contextOptions.validate) {
    printDeviceProfile(ctx);
  }
  bh::Canvas canvas(ctx, data, support.base_color);

  bh::ApplyOptions ao;
  SnapshotWriter snap;
  snap.prefix    = snapshot_prefix;
  snap.extension = outPath.has_extension() ? outPath.extension().string() : ".bmp";
  snap.every     = snapshot_every;
  snap.width     = fitted.width;
  snap.height    = fitted.height;
  snap.light_dir = rakingLightDir();

  // Print every 10% even when no snapshot is configured.
  const std::size_t total = plan.steps.size();
  ao.on_step = [&](bh::Canvas& c, std::size_t step, std::size_t tot) {
    snap(c, step, tot);
    if (tot >= 10 && step % (tot / 10) == 0) {
      std::printf("  step %zu / %zu (%.0f%%)\n", step, tot,
                  100.0 * static_cast<double>(step) / static_cast<double>(tot));
    }
  };
  bh::applyPlan(canvas, plan, ao);

  const auto t_render = clk::now();
  std::printf("render:   %.2f s (%zu steps)\n",
              std::chrono::duration<double>(t_render - t_plan).count(), total);

  auto rgba = canvas.compose(rakingLightDir(), kAmbient, kHeightScale);
  if (!writeRgbaImage(outPath, fitted.width, fitted.height, rgba)) {
    std::fprintf(stderr, "failed to write %s\n", outPath.string().c_str());
    return 1;
  }
  std::printf("wrote %s\n", outPath.string().c_str());
  return 0;
}

// -- Argument parsing ------------------------------------------------------

// Tiny long-flag parser: `--key value` pairs scattered after the positional
// args. Unknown flags are reported and abort the run; that's safer than
// silently ignoring a typo'd `--styel` and producing the wrong scene.
struct Flags {
  std::string substrate = "cold_press";
  std::filesystem::path output;
  std::string style = "watercolor";
  std::uint32_t snapshot_every = 0;
  std::filesystem::path snapshot_prefix = "snapshot";
  bool validate = false;
  bool device_info = false;
};

bool parseFlags(int argc, char** argv, int start_at, Flags& out) {
  for (int i = start_at; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", name);
        return nullptr;
      }
      return argv[++i];
    };
    if      (a == "--substrate")        { auto v = need("--substrate");        if (!v) return false; out.substrate = v; }
    else if (a == "--output")           { auto v = need("--output");           if (!v) return false; out.output = v; }
    else if (a == "--style")            { auto v = need("--style");            if (!v) return false; out.style = v; }
    else if (a == "--snapshot-every")   { auto v = need("--snapshot-every");   if (!v) return false; out.snapshot_every  = static_cast<std::uint32_t>(std::stoul(v)); }
    else if (a == "--snapshot-prefix")  { auto v = need("--snapshot-prefix");  if (!v) return false; out.snapshot_prefix = v; }
    else if (a == "--validate")         { out.validate = true; }
    else if (a == "--device-info")      { out.device_info = true; }
    else {
      std::fprintf(stderr, "unknown flag: %s\n", a.c_str());
      return false;
    }
  }
  return true;
}

void printUsage() {
  std::printf(
    "Usage:\n"
    "  broadstroke paint <image>      [--style NAME] [--substrate NAME] [--output PATH]\n"
    "                                 [--snapshot-every N] [--snapshot-prefix PREFIX]\n"
    "                                 [--validate] [--device-info]\n"
    "  broadstroke scene <name>       [--substrate NAME] [--output PATH]\n"
    "                                 [--validate] [--device-info]\n"
    "  broadstroke ui                 (Windows preset explorer)\n"
    "  broadstroke <scene>            (legacy positional form: scene [substrate [output]])\n"
    "\n"
    "Styles  : watercolor | pencil_sketch | pastel_drawing\n"
    "Scenes  : pencil | watercolor | mixed\n"
    "Output  : .bmp (default), .png, .jpg/.jpeg\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("Broadstroke v%s\n", broadstroke::kProjectVersion);
  std::printf("Stage 8  tuned pipeline\n\n");

  if (argc < 2) { printUsage(); return 0; }
  const std::string cmd = argv[1];

  try {
    if (cmd == "paint") {
      if (argc < 3) { printUsage(); return 3; }
      const std::filesystem::path image = argv[2];
      Flags fl;
      if (!parseFlags(argc, argv, /*start_at=*/3, fl)) return 3;
      if (fl.output.empty()) fl.output = "broadstroke_painted.bmp";
      broadstroke::compute::ContextOptions contextOptions;
      contextOptions.validate = fl.validate;
      return runPaint(image, fl.style, fl.substrate, fl.output,
              fl.snapshot_every, fl.snapshot_prefix,
              contextOptions, fl.device_info);
    }

    if (cmd == "scene") {
      if (argc < 3) { printUsage(); return 3; }
      const std::string scene = argv[2];
      Flags fl;
      if (!parseFlags(argc, argv, /*start_at=*/3, fl)) return 3;
      if (fl.output.empty()) fl.output = "broadstroke_canvas.bmp";
      broadstroke::compute::ContextOptions contextOptions;
      contextOptions.validate = fl.validate;
      return runScene(scene, fl.substrate, fl.output, contextOptions, fl.device_info);
    }

    if (cmd == "ui") {
      return broadstroke::host::runPresetExplorerUi(argc > 2 ? argv[2] : nullptr);
    }

    // Legacy positional form: `broadstroke <scene> [substrate] [output]`.
    if (cmd == "pencil" || cmd == "watercolor" || cmd == "mixed") {
      const std::string supportName = (argc > 2) ? argv[2] : "cold_press";
      const std::filesystem::path out =
          (argc > 3) ? argv[3] : "broadstroke_canvas.bmp";
      return runScene(cmd, supportName, out, broadstroke::compute::ContextOptions{}, false);
    }

    std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
    printUsage();
    return 3;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 2;
  }
}
