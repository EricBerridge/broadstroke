#include "core/image.hpp"

#include <cctype>
#include <cstdio>

#include "stb_image.h"
#include "stb_image_write.h"

namespace bs {
namespace {

constexpr long long kMaxPixels = 120LL * 1000 * 1000;  // 120 MP decode cap

std::string lower_ext(const std::string& path) {
  size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string e = path.substr(dot + 1);
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return e;
}

const std::array<float, 256>& srgb_lut() {
  static const std::array<float, 256> lut = [] {
    std::array<float, 256> t{};
    for (int i = 0; i < 256; ++i) t[i] = srgb_to_linear(i / 255.f);
    return t;
  }();
  return lut;
}

}  // namespace

Image load_image(const std::string& path, std::string* error) {
  auto fail = [&](const std::string& msg) {
    if (error) *error = msg;
    return Image{};
  };
  int w = 0, h = 0, comp = 0;
  if (!stbi_info(path.c_str(), &w, &h, &comp)) {
    const char* why = stbi_failure_reason();
    return fail("cannot read image '" + path + "': " + (why ? why : "unknown format"));
  }
  if (w <= 0 || h <= 0 || static_cast<long long>(w) * h > kMaxPixels)
    return fail("image '" + path + "' has unsupported dimensions");
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 3);
  if (!data) {
    const char* why = stbi_failure_reason();
    return fail("cannot decode image '" + path + "': " + (why ? why : "unknown error"));
  }
  Image img(w, h);
  const auto& lut = srgb_lut();
  for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i)
    img.v[i] = {lut[data[3 * i]], lut[data[3 * i + 1]], lut[data[3 * i + 2]]};
  stbi_image_free(data);
  return img;
}

std::vector<uint8_t> to_srgb8(const Image& img) {
  std::vector<uint8_t> out(static_cast<size_t>(img.w) * img.h * 3);
  parallel_for(0, img.h, [&](int lo, int hi) {
    for (int y = lo; y < hi; ++y)
      for (int x = 0; x < img.w; ++x) {
        const Rgb& c = img(x, y);
        uint8_t* o = &out[(static_cast<size_t>(y) * img.w + x) * 3];
        o[0] = static_cast<uint8_t>(linear_to_srgb(c.r) * 255.f + 0.5f);
        o[1] = static_cast<uint8_t>(linear_to_srgb(c.g) * 255.f + 0.5f);
        o[2] = static_cast<uint8_t>(linear_to_srgb(c.b) * 255.f + 0.5f);
      }
  }, 16);
  return out;
}

bool save_image(const std::string& path, const Image& img, std::string* error) {
  if (img.empty()) {
    if (error) *error = "nothing to write";
    return false;
  }
  std::vector<uint8_t> px = to_srgb8(img);
  std::string ext = lower_ext(path);
  int ok = 0;
  if (ext == "png") ok = stbi_write_png(path.c_str(), img.w, img.h, 3, px.data(), img.w * 3);
  else if (ext == "jpg" || ext == "jpeg") ok = stbi_write_jpg(path.c_str(), img.w, img.h, 3, px.data(), 93);
  else if (ext == "bmp") ok = stbi_write_bmp(path.c_str(), img.w, img.h, 3, px.data());
  else {
    if (error) *error = "unsupported output extension '." + ext + "' (use .png, .jpg or .bmp)";
    return false;
  }
  if (!ok && error) *error = "failed to write '" + path + "'";
  return ok != 0;
}

}  // namespace bs
