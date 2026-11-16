#include "support.hpp"

#include <windows.h>

#include <bcrypt.h>
#include <dxgi.h>
#include <intrin.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include "core/json.hpp"

namespace bs::studio {

const std::vector<Manner>& manners() {
  static const std::vector<Manner> list = {
      {"surrealism", L"Surrealism", {"melting", "on stilts", "levitating", "giant", "repeated"}},
      {"cubism", L"Cubism", {"faceted"}},
      {"futurism", L"Futurism", {"echoing"}},
      {"metaphysical", L"Metaphysical (long shadows)", {"long shadow"}},
      {"polygonal", L"Solid of triangles", {"triangulated"}},
  };
  return list;
}

std::string pair_key(std::string a, std::string b) {
  auto tidy = [](std::string& s) {
    const size_t first = s.find_first_not_of(" \t"), last = s.find_last_not_of(" \t");
    s = first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  tidy(a);
  tidy(b);
  if (b < a) std::swap(a, b);
  return a + ":" + b;
}

std::string pair_key(const std::string& stated) {
  const size_t colon = stated.find(':');
  if (colon == std::string::npos) return {};
  return pair_key(stated.substr(0, colon), stated.substr(colon + 1));
}

std::vector<Meeting> meetings_of(const Scene& scene, int item_index) {
  std::vector<Meeting> out;
  if (item_index < 0 || item_index >= static_cast<int>(scene.items.size()) || scene.item.empty()) return out;
  auto valid = [](int id) { return id > 0 && id < 256; };

  // The item and everything that is a part of it, as the interpret step
  // rebuilds it: without the parts made of thin things.
  const int item_id = scene.items[static_cast<size_t>(item_index)].id;
  if (!valid(item_id)) return out;
  bool family[256] = {}, in_group[256] = {};
  family[item_id] = true;
  for (bool grew = true; grew;) {
    grew = false;
    for (const SceneItem& it : scene.items)
      if (valid(it.id) && valid(it.parent) && family[it.parent] && !family[it.id]) {
        family[it.id] = true;
        grew = true;
      }
  }
  for (const SceneItem& it : scene.items) {
    if (!valid(it.id) || !family[it.id]) continue;
    const bool thin =
        std::find(std::begin(kThinMaterials), std::end(kThinMaterials), it.treatment) != std::end(kThinMaterials);
    in_group[it.id] = it.id == item_id || !thin;
  }

  // Which items lie side by side, along the group's outline and inside it.
  std::vector<uint8_t> side_by_side(256 * 256, 0);
  const Plane<uint8_t>& layer = scene.item;
  auto note = [&](uint8_t a, uint8_t b) {
    if (a != b && (in_group[a] || in_group[b])) side_by_side[static_cast<size_t>(a) * 256 + b] = 1;
  };
  for (int y = 0; y < layer.h; ++y)
    for (int x = 0; x < layer.w; ++x) {
      const uint8_t a = layer(x, y);
      if (x + 1 < layer.w) note(a, layer(x + 1, y));
      if (y + 1 < layer.h) note(a, layer(x, y + 1));
    }

  std::string material[256];
  bool soft[256] = {};
  for (std::string& m : material) m = kNoMaterial;
  for (const SceneItem& it : scene.items)
    if (valid(it.id)) {
      if (!it.treatment.empty()) material[it.id] = it.treatment;
      soft[it.id] = it.edges != Edges::Hard;
    }
  auto by_nature = [](const std::string& m) {
    return std::find(std::begin(kRaggedByNature), std::end(kRaggedByNature), m) != std::end(kRaggedByNature);
  };

  for (int a = 0; a < 256; ++a)
    for (int b = 0; b < 256; ++b) {
      if (!side_by_side[static_cast<size_t>(a) * 256 + static_cast<size_t>(b)]) continue;
      const int own = in_group[a] ? a : b, other = own == a ? b : a;
      const std::string key = pair_key(material[own], material[other]);
      const bool ragged = by_nature(material[own]) || by_nature(material[other]) || (soft[own] && soft[other]);
      auto found = std::find_if(out.begin(), out.end(), [&](const Meeting& m) { return m.key == key; });
      if (found == out.end()) {
        out.push_back({key, material[own], material[other], {}, false, false});
        found = out.end() - 1;
      }
      (ragged ? found->ragged : found->abrupt) = true;
      if (const SceneItem* met = scene.find(other))
        if (std::find(found->names.begin(), found->names.end(), met->name) == found->names.end())
          found->names.push_back(met->name);
    }
  return out;
}

// ---------------------------------------------------------------------------
// Strings and paths

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string to_utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring pretty_name(const std::string& id, bool capitalize) {
  std::wstring s = widen(id);
  std::replace(s.begin(), s.end(), L'_', L' ');
  if (capitalize && !s.empty()) s[0] = static_cast<wchar_t>(std::towupper(s[0]));
  return s;
}

std::wstring file_name_of(const std::wstring& path) {
  size_t slash = path.find_last_of(L"/\\");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring directory_of(const std::wstring& path) {
  size_t slash = path.find_last_of(L"/\\");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring stem_of(const std::wstring& path) {
  std::wstring name = file_name_of(path);
  size_t dot = name.find_last_of(L'.');
  return (dot == std::wstring::npos || dot == 0) ? name : name.substr(0, dot);
}

std::wstring extension_of(const std::wstring& path) {
  std::wstring name = file_name_of(path);
  size_t dot = name.find_last_of(L'.');
  if (dot == std::wstring::npos) return {};
  std::wstring ext = name.substr(dot + 1);
  for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
  return ext;
}

std::wstring full_path(const std::wstring& path) {
  DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (n == 0) return path;
  std::wstring out(n, L'\0');
  DWORD written = GetFullPathNameW(path.c_str(), n, out.data(), nullptr);
  if (written == 0 || written >= n) return path;
  out.resize(written);
  return out;
}

std::wstring work_root() {
  wchar_t buf[1024];
  const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
  return (n > 0 && n < std::size(buf) ? std::wstring(buf) : std::wstring(L".")) + L"\\Broadstroke\\photos";
}

WorkFiles photo_work_files(const std::wstring& photo) {
  // The SHA-256 of the photo's bytes.
  std::ifstream in(std::filesystem::path(photo), std::ios::binary);
  if (!in) return {};
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  unsigned char digest[32] = {};
  bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
            BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
  std::vector<char> block(1 << 16);
  while (ok && in) {
    in.read(block.data(), static_cast<std::streamsize>(block.size()));
    const std::streamsize got = in.gcount();
    if (got > 0) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(block.data()), static_cast<ULONG>(got), 0) >= 0;
  }
  ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) return {};
  wchar_t id[13] = {};
  for (int i = 0; i < 6; ++i) swprintf(id + 2 * i, 3, L"%02x", digest[i]);
  const std::wstring ending = std::wstring(L"-") + id;

  // The folder this photo has already, whatever the photo was called then,
  // and the name its files were given.
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(work_root()), ec)) {
    const std::wstring folder = entry.path().filename().wstring();
    if (!entry.is_directory(ec) || folder.size() <= ending.size() ||
        folder.compare(folder.size() - ending.size(), ending.size(), ending) != 0)
      continue;
    WorkFiles found{entry.path().wstring(), stem_of(photo)};
    const std::wstring tail = L".scene.json";
    for (const auto& file : std::filesystem::directory_iterator(entry.path(), ec)) {
      const std::wstring name = file.path().filename().wstring();
      if (name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0)
        found.name = name.substr(0, name.size() - tail.size());
    }
    return found;
  }
  // A new one, named for the photo so that a person can find it. A long
  // name is cut: the ending tells the photos apart.
  return {work_root() + L"\\" + stem_of(photo).substr(0, 40) + ending, stem_of(photo)};
}

bool is_folder(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool is_file(const std::wstring& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}

bool read_text(const std::wstring& path, std::string& out) {
  std::ifstream in(std::filesystem::path(path), std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

bool write_text(const std::wstring& path, const std::string& text) {
  std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  out.flush();
  return static_cast<bool>(out);
}

bool written_after(const std::wstring& a, const std::wstring& b) {
  std::error_code ea, eb;
  const auto ta = std::filesystem::last_write_time(std::filesystem::path(a), ea);
  const auto tb = std::filesystem::last_write_time(std::filesystem::path(b), eb);
  return !ea && !eb && ta > tb;
}

// ---------------------------------------------------------------------------
// About

namespace {

// "12.0" is written "12"; anything else keeps one decimal.
std::wstring gigabytes(uint64_t bytes) {
  const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
  wchar_t buf[32];
  if (std::fabs(gb - std::round(gb)) < 0.05) swprintf(buf, std::size(buf), L"%.0f GB", std::round(gb));
  else swprintf(buf, std::size(buf), L"%.1f GB", gb);
  return buf;
}

}  // namespace

Computer this_computer() {
  Computer c;

  // The processor's name for itself: three leaves of sixteen characters.
  int regs[4] = {};
  __cpuid(regs, static_cast<int>(0x80000000u));
  if (static_cast<unsigned>(regs[0]) >= 0x80000004u) {
    char brand[49] = {};
    for (int leaf = 0; leaf < 3; ++leaf) {
      __cpuid(regs, static_cast<int>(0x80000002u) + leaf);
      std::memcpy(brand + 16 * leaf, regs, 16);
    }
    // Some names are padded with spaces, at the ends and inside.
    std::string name;
    for (const char ch : std::string(brand)) {
      if (ch == ' ' && (name.empty() || name.back() == ' ')) continue;
      name += ch;
    }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    c.processor = widen(name);
  }
  c.logical_processors = static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

  ULONGLONG installed_kb = 0;
  if (GetPhysicallyInstalledSystemMemory(&installed_kb)) {
    c.memory_bytes = installed_kb * 1024;
  } else {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) c.memory_bytes = status.ullTotalPhys;
  }

  IDXGIFactory1* factory = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)) && adapter; ++i) {
      DXGI_ADAPTER_DESC1 desc{};
      // The adapter Windows draws with when there is no hardware is not one.
      if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
        c.graphics.push_back({desc.Description, static_cast<uint64_t>(desc.DedicatedVideoMemory)});
      adapter->Release();
      adapter = nullptr;
    }
    factory->Release();
  }
  return c;
}

std::wstring about_text(const char* version) {
  const Computer c = this_computer();
  std::wstring text = L"Version " + widen(version) + L"\n";
  text += L"Copyright 2026 Eric Berridge. All rights reserved.\n\nThis computer\n";
  text += L"Processor: " + (c.processor.empty() ? std::wstring(L"unknown") : c.processor);
  if (c.logical_processors > 0) text += L", " + std::to_wstring(c.logical_processors) + L" logical processors";
  text += L"\nMemory: " + (c.memory_bytes ? gigabytes(c.memory_bytes) : std::wstring(L"unknown"));
  if (c.graphics.empty()) text += L"\nGraphics: unknown";
  for (const Computer::Graphics& g : c.graphics) {
    text += L"\nGraphics: " + g.name + L", ";
    text += g.memory_bytes ? gigabytes(g.memory_bytes) : std::wstring(L"shared memory");
  }
  return text;
}

// ---------------------------------------------------------------------------
// The steps outside the engine

std::wstring tools_config_path() {
  wchar_t buf[1024];
  DWORD n = GetEnvironmentVariableW(L"BROADSTROKE_STUDIO_CONFIG", buf, static_cast<DWORD>(std::size(buf)));
  if (n > 0 && n < std::size(buf)) return buf;
  n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
  if (n > 0 && n < std::size(buf)) return std::wstring(buf) + L"\\Broadstroke\\studio.json";
  return L"studio.json";
}

bool read_tools_config(ToolsConfig& config, std::wstring& problem, std::wstring* brief) {
  const std::wstring path = tools_config_path();
  const std::wstring how = L"\n\nThe window starts the observe and interpret steps from what this file says:\n" + path +
                           L"\n\nIt names the Python interpreter and the tools folder. "
                           L"tools/studio.example.json shows how.";
  auto fail = [&](std::wstring what) {
    problem = what + how;
    std::replace(what.begin(), what.end(), L'\n', L' ');
    if (brief) *brief = what;
    return false;
  };
  std::string text;
  if (!read_text(path, text)) return fail(L"The steps that run outside the engine are not set up on this machine.");
  Json doc;
  if (!parse_json(text, doc) || !doc.is_object())
    return fail(L"The file that says where the steps outside the engine are is not valid JSON.");
  auto folder = [&](const char* key) {
    const Json* j = doc.get(key);
    std::wstring s = (j && j->is_string()) ? widen(j->string) : std::wstring();
    std::replace(s.begin(), s.end(), L'/', L'\\');
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    return s;
  };
  config.python = folder("python");
  config.tools = folder("tools");
  if (!is_file(config.python)) return fail(L"The Python interpreter was not found:\n" + config.python);
  if (!is_file(config.tools + L"\\observe\\observe.py") || !is_file(config.tools + L"\\interpret\\imagine.py"))
    return fail(L"The tools folder does not hold observe\\observe.py and interpret\\imagine.py:\n" + config.tools);
  return true;
}

// ---------------------------------------------------------------------------
// Pictures for the preview

std::unique_ptr<Frame> make_frame(const Image& img) {
  auto frame = std::make_unique<Frame>();
  if (img.empty()) return frame;
  const std::vector<uint8_t> rgb = to_srgb8(img);
  frame->w = img.w;
  frame->h = img.h;
  frame->bgra.resize(static_cast<size_t>(img.w) * img.h * 4);
  const uint8_t* src = rgb.data();
  uint8_t* dst = frame->bgra.data();
  for (size_t i = 0, n = static_cast<size_t>(img.w) * img.h; i < n; ++i, src += 3, dst += 4) {
    dst[0] = src[2];
    dst[1] = src[1];
    dst[2] = src[0];
    dst[3] = 255;
  }
  return frame;
}

std::unique_ptr<Frame> make_display_frame(const Image& img) {
  const int long_side = std::max(img.w, img.h);
  if (long_side <= kMaxPreviewSide) return make_frame(img);
  const float s = static_cast<float>(kMaxPreviewSide) / static_cast<float>(long_side);
  const int pw = std::max(1, static_cast<int>(std::lround(static_cast<float>(img.w) * s)));
  const int ph = std::max(1, static_cast<int>(std::lround(static_cast<float>(img.h) * s)));
  return make_frame(resize_area(img, pw, ph));
}

void reduce_frame(const Frame& src, int w, int h, uint8_t* out) {
  // Each new pixel is the mean of the block of old ones it covers. The
  // blocks are whole pixels: a picture reduced for the screen needs no finer
  // weighting, and whole blocks keep this fast enough to follow a resize.
  std::vector<int> x0(static_cast<size_t>(w) + 1);
  for (int x = 0; x <= w; ++x) x0[static_cast<size_t>(x)] = static_cast<int>(static_cast<long long>(x) * src.w / w);
  std::vector<uint32_t> sums(static_cast<size_t>(w) * 3);
  for (int y = 0; y < h; ++y) {
    const int ya = static_cast<int>(static_cast<long long>(y) * src.h / h);
    const int yb = std::max(ya + 1, static_cast<int>(static_cast<long long>(y + 1) * src.h / h));
    std::fill(sums.begin(), sums.end(), 0u);
    for (int sy = ya; sy < yb; ++sy) {
      const uint8_t* row = &src.bgra[static_cast<size_t>(sy) * src.w * 4];
      for (int x = 0; x < w; ++x) {
        const int xa = x0[static_cast<size_t>(x)], xb = std::max(xa + 1, x0[static_cast<size_t>(x) + 1]);
        uint32_t b = 0, g = 0, r = 0;
        for (const uint8_t* p = row + 4 * xa, *end = row + 4 * xb; p < end; p += 4) {
          b += p[0];
          g += p[1];
          r += p[2];
        }
        uint32_t* s = &sums[static_cast<size_t>(x) * 3];
        s[0] += b;
        s[1] += g;
        s[2] += r;
      }
    }
    uint8_t* dst = out + static_cast<size_t>(y) * w * 4;
    for (int x = 0; x < w; ++x, dst += 4) {
      const int xa = x0[static_cast<size_t>(x)], xb = std::max(xa + 1, x0[static_cast<size_t>(x) + 1]);
      const uint32_t count = static_cast<uint32_t>((xb - xa) * (yb - ya));
      const uint32_t* s = &sums[static_cast<size_t>(x) * 3];
      dst[0] = static_cast<uint8_t>((s[0] + count / 2) / count);
      dst[1] = static_cast<uint8_t>((s[1] + count / 2) / count);
      dst[2] = static_cast<uint8_t>((s[2] + count / 2) / count);
      dst[3] = 255;
    }
  }
}

Rgb item_color(int row) {
  const double hue = std::fmod(0.07 + 0.618033988749895 * row, 1.0) * 6.0;
  const int sector = static_cast<int>(hue);
  const double f = hue - sector, lo = 0.30;
  const double up = lo + (1.0 - lo) * f, down = 1.0 - (1.0 - lo) * f;
  double r = 1.0, g = 1.0, b = 1.0;
  switch (sector) {
    case 0: r = 1.0; g = up; b = lo; break;
    case 1: r = down; g = 1.0; b = lo; break;
    case 2: r = lo; g = 1.0; b = up; break;
    case 3: r = lo; g = down; b = 1.0; break;
    case 4: r = up; g = lo; b = 1.0; break;
    default: r = 1.0; g = lo; b = down; break;
  }
  return Rgb{static_cast<uint8_t>(r * 255.0), static_cast<uint8_t>(g * 255.0), static_cast<uint8_t>(b * 255.0)};
}

std::unique_ptr<Frame> make_scene_frame(const Frame& photo, const Scene& scene, int selected) {
  if (scene.empty() || photo.w <= 0 || photo.h <= 0) return nullptr;
  auto frame = std::make_unique<Frame>(photo);
  const Plane<uint8_t>& layer = scene.item;
  const int w = frame->w, h = frame->h;

  uint8_t tint[256][3] = {};
  bool chosen[256] = {};
  const int selected_id = selected >= 0 ? scene.items[static_cast<size_t>(selected)].id : 0;
  for (size_t i = 0; i < scene.items.size(); ++i) {
    const SceneItem& it = scene.items[i];
    const Rgb c = item_color(static_cast<int>(i));
    tint[it.id][0] = c.b;
    tint[it.id][1] = c.g;
    tint[it.id][2] = c.r;
    // An item is brought out with the whole it is a part of.
    int steps = 0;
    for (const SceneItem* p = &it; p && steps < 256; p = scene.find(p->parent), ++steps)
      if (p->id == selected_id) chosen[it.id] = true;
  }

  std::vector<int> column(static_cast<size_t>(w));
  for (int x = 0; x < w; ++x)
    column[static_cast<size_t>(x)] = std::min(layer.w - 1, static_cast<int>(static_cast<long long>(x) * layer.w / w));
  auto row_of = [&](int y) { return std::min(layer.h - 1, static_cast<int>(static_cast<long long>(y) * layer.h / h)); };
  // Outlines are drawn thick enough to survive the reduction to the window.
  const int thick = std::max(1, std::max(w, h) / 500);
  for (int y = 0; y < h; ++y) {
    const int ly = row_of(y), ly_below = row_of(std::min(h - 1, y + thick));
    uint8_t* px_row = &frame->bgra[static_cast<size_t>(y) * w * 4];
    for (int x = 0; x < w; ++x) {
      const int lx = column[static_cast<size_t>(x)], lx_right = column[static_cast<size_t>(std::min(w - 1, x + thick))];
      const int id = layer(lx, ly);
      const int right = layer(lx_right, ly), below = layer(lx, ly_below);
      uint8_t* p = px_row + 4 * x;
      if (selected_id == 0) {
        // Every item in its color; outlines dark.
        const bool outline = id != right || id != below;
        for (int c = 0; c < 3; ++c) {
          int v = id ? (p[c] * 11 + tint[id][c] * 9) / 20 : p[c] * 3 / 5;
          if (outline) v = v * 2 / 5;
          p[c] = static_cast<uint8_t>(v);
        }
      } else {
        const bool in = chosen[id];
        const bool outline = in != chosen[right] || in != chosen[below];
        for (int c = 0; c < 3; ++c) {
          int v = in ? (p[c] * 4 + tint[id][c]) / 5 : p[c] * 3 / 10;
          if (outline) v = 255;
          p[c] = static_cast<uint8_t>(v);
        }
      }
    }
  }
  return frame;
}

}  // namespace bs::studio
