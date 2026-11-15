// The studio window on Windows: a native Win32 front end for the engine.
//
// A fixed-width panel of painting controls sits on the left and a panel for
// the photo's scene (spec 2.13) on the right; between them a preview shows
// the loaded photo, its scene, or the artwork while it is being made.
//
// Nothing here changes what the engine makes: the window gathers a photo, a
// scene and settings and calls bs::create_artwork, as the command line does.
// The scene view in the preview is drawn for the screen only.
//
// Threading and ownership:
//   * The UI thread owns every window and all App state except the Mailbox.
//   * bs::create_artwork runs on a worker thread. The worker never touches a
//     window or UI state. It writes its latest progress, its latest rendered
//     frame and, at the end, the finished Artwork into the Mailbox (guarded by
//     a mutex) and posts one payload-free wake-up message to the UI thread.
//   * Wake-ups are coalesced with an atomic flag: a new one is posted only
//     after the UI thread has consumed the previous one, so the message queue
//     cannot be flooded. Because nothing travels inside a message, a frame can
//     never leak in the queue; whatever is still in the Mailbox when the
//     window goes away is freed with the App.
//   * The worker is always joined on the UI thread: when it reports that it
//     is done, or after a stop request when the window is closed mid-work.
#include "app/gui.hpp"

#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app/help_win32.hpp"
#include "app/process_win32.hpp"
#include "app/scene_edit.hpp"
#include "app/win32_common.hpp"
#include "artist/artist.hpp"
#include "core/image.hpp"
#include "core/json.hpp"
#include "surface/surface.hpp"

// Version 6 of the common controls gives the controls the current visual style.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace bs {
namespace {

using Clock = std::chrono::steady_clock;

const wchar_t kAppTitle[] = L"Broadstroke";
const wchar_t kMainClass[] = L"BroadstrokeStudioWindow";
const wchar_t kPreviewClass[] = L"BroadstrokeStudioPreview";

// Worker -> UI wake-up: "the Mailbox has something new". Carries no payload.
constexpr UINT kMsgWorker = WM_APP + 1;
// While working, the Mailbox is also polled on a slow timer so that a wake-up
// that could not be posted (full message queue) cannot stall the UI.
constexpr UINT_PTR kPollTimer = 1;
constexpr UINT kPollMs = 250;

// Control ids. They start above IDOK / IDCANCEL, which IsDialogMessageW sends
// for Enter and Escape.
enum : int {
  kIdOpen = 100,
  kIdMedium,
  kIdSurface,
  kIdSize,
  kIdEffort,
  kIdSeed,
  kIdSkyBlend,
  kIdKey,
  kIdStart,
  kIdSave,
  kIdView,
  kIdUseScene,
  kIdItems,
  kIdImportance,
  kIdTreatment,
  kIdEdges,
  kIdObserve,
  kIdManner,
  kIdImagine,
  kIdAskAgain,
  kIdMechanism,
  kIdStrength,
  kIdHelp,
};

constexpr int kPanelWidthDip = 300;        // painting panel width at 96 DPI
constexpr int kScenePanelWidthDip = 500;   // scene panel width at 96 DPI
constexpr int kMinPreviewWidthDip = 260;
constexpr int kMinItemListHeightDip = 110;
constexpr int kProgressRange = 1000;
constexpr int kMaxPreviewSide = 4096;      // larger photos are reduced for display only
constexpr COLORREF kPreviewBackground = RGB(46, 46, 46);
constexpr COLORREF kPreviewHintText = RGB(168, 168, 168);

// Live frames: at most one per kFrameInterval, and never more than about a
// quarter of the worker's time (see run_worker).
constexpr Clock::duration kFrameInterval = std::chrono::milliseconds(250);
// Progress text without a frame: about 15 updates per second.
constexpr Clock::duration kProgressInterval = std::chrono::milliseconds(66);

const int kSizes[] = {800, 1000, 1400, 2000, 2800};
constexpr int kDefaultSizeIndex = 2;

struct EffortChoice {
  const wchar_t* label;
  float value;
};
const EffortChoice kEfforts[] = {{L"Quick study", 0.5f}, {L"Finished piece", 1.0f}, {L"Meticulous", 1.5f}};
constexpr int kDefaultEffortIndex = 1;

// The watercolor key (Settings::watercolor_key) on its slider, in hundredths:
// one position per 0.05 across the engine's range.
constexpr int kKeyStepPct = 5;
const int kKeyMinPct = static_cast<int>(std::lround(kWatercolorKeyMin * 100.f));
const int kKeyMaxPct = static_cast<int>(std::lround(kWatercolorKeyMax * 100.f));
const int kKeyDefaultPct = static_cast<int>(std::lround(Settings{}.watercolor_key * 100.f));

// What the preview shows.
enum class View : int { Photo, Scene, Subject, Artwork };

// The steps outside the engine that the window can start.
enum class Tool { None, Observe, Imagine };

// The manners of spec 2.14 and the mechanisms each allows, as the interpret
// step names them (tools/interpret/vocab.py).
struct Manner {
  const char* id;
  const wchar_t* label;
  std::vector<const char*> mechanisms;
};
const Manner kManners[] = {
    {"surrealism", L"Surrealism", {"melting", "on stilts", "levitating", "giant", "repeated"}},
    {"cubism", L"Cubism", {"faceted"}},
    {"futurism", L"Futurism", {"echoing"}},
    {"metaphysical", L"Metaphysical (long shadows)", {"long shadow"}},
    {"polygonal", L"Solid of triangles", {"triangulated"}},
};
const char kAsItIs[] = "as it is";  // the mechanism of an item that is left alone

// The words a scene file uses (spec 2.13). The treatments are the list the
// observe step chooses from (tools/observe/describe.py).
const char* const kTreatments[] = {
    "skin",        "hair",         "fur_or_feathers", "fabric", "foliage_mass", "trunk_or_branches",
    "grass",       "flowers",      "sky",             "cloud",  "still_water",  "moving_water",
    "foam_or_spray", "rock",       "earth_or_sand",   "wood",   "metal",        "glass",
    "smooth_manufactured", "masonry", "food",         "other"};
const wchar_t* const kImportanceNames[] = {L"Background", L"Supporting", L"Matters", L"Main subject"};
const wchar_t* const kEdgeNames[] = {L"Hard", L"Soft", L"Lost"};

// The project is not built with UNICODE, so IDC_ARROW and friends expand to
// narrow resource names. These are the wide equivalents.
const wchar_t* const kCursorArrow = MAKEINTRESOURCEW(32512);
const wchar_t* const kCursorWait = MAKEINTRESOURCEW(32514);
const wchar_t* const kIconApplication = MAKEINTRESOURCEW(32512);

// ---------------------------------------------------------------------------
// Strings and paths

std::wstring widen(const std::string& s, UINT code_page) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(code_page, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(code_page, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
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

// "cold_press" -> "cold press" (or "Cold press").
std::wstring pretty_name(const std::string& id, bool capitalize) {
  std::wstring s = widen(id, CP_UTF8);
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

// File name without its extension.
std::wstring stem_of(const std::wstring& path) {
  std::wstring name = file_name_of(path);
  size_t dot = name.find_last_of(L'.');
  return (dot == std::wstring::npos || dot == 0) ? name : name.substr(0, dot);
}

// Lower-case extension without the dot, or empty.
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

// Whether file `a` was written after file `b`. False if either is missing.
bool written_after(const std::wstring& a, const std::wstring& b) {
  std::error_code ea, eb;
  const auto ta = std::filesystem::last_write_time(std::filesystem::path(a), ea);
  const auto tb = std::filesystem::last_write_time(std::filesystem::path(b), eb);
  return !ea && !eb && ta > tb;
}

// ---------------------------------------------------------------------------
// The steps outside the engine
//
// Observe (spec 2.13) and interpret (spec 2.14) run models and are programs
// of their own, in Python. The window starts them and reads the files they
// write; it never runs a model itself. Where they are is machine-specific
// and comes from a small local file.

struct ToolsConfig {
  std::wstring python;  // the Python interpreter that has the tools' packages
  std::wstring tools;   // the folder that holds observe/ and interpret/
};

std::wstring tools_config_path() {
  wchar_t buf[1024];
  DWORD n = GetEnvironmentVariableW(L"BROADSTROKE_STUDIO_CONFIG", buf, static_cast<DWORD>(std::size(buf)));
  if (n > 0 && n < std::size(buf)) return buf;
  n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
  if (n > 0 && n < std::size(buf)) return std::wstring(buf) + L"\\Broadstroke\\studio.json";
  return L"studio.json";
}

// Reads the local file. On failure `problem` says what is missing, for the
// user.
bool read_tools_config(ToolsConfig& config, std::wstring& problem) {
  const std::wstring path = tools_config_path();
  const std::wstring how = L"\n\nThe window starts the observe and interpret steps from what this file says:\n" + path +
                           L"\n\nIt names the Python interpreter and the tools folder. "
                           L"tools/studio.example.json shows how.";
  std::string text;
  if (!read_text(path, text)) {
    problem = L"The steps that run outside the engine are not set up on this machine." + how;
    return false;
  }
  Json doc;
  if (!parse_json(text, doc) || !doc.is_object()) {
    problem = L"The file that says where the steps outside the engine are is not valid JSON." + how;
    return false;
  }
  auto folder = [&](const char* key) {
    const Json* j = doc.get(key);
    std::wstring s = (j && j->is_string()) ? widen(j->string, CP_UTF8) : std::wstring();
    std::replace(s.begin(), s.end(), L'/', L'\\');
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    return s;
  };
  config.python = folder("python");
  config.tools = folder("tools");
  if (!is_file(config.python)) {
    problem = L"The Python interpreter was not found:\n" + config.python + how;
    return false;
  }
  if (!is_file(config.tools + L"\\observe\\observe.py") || !is_file(config.tools + L"\\interpret\\imagine.py")) {
    problem = L"The tools folder does not hold observe\\observe.py and interpret\\imagine.py:\n" + config.tools + how;
    return false;
  }
  return true;
}

// One program to run, and the folder it runs in.
struct ToolStep {
  std::wstring command, directory;
};

// ---------------------------------------------------------------------------
// Frames

// An image ready for StretchDIBits: 24-bit BGR, top-down, rows padded to a
// multiple of four bytes.
struct Frame {
  int w = 0, h = 0, stride = 0;
  std::vector<uint8_t> bgr;
};

std::unique_ptr<Frame> make_frame(const Image& img) {
  auto frame = std::make_unique<Frame>();
  if (img.empty()) return frame;
  const std::vector<uint8_t> rgb = to_srgb8(img);
  frame->w = img.w;
  frame->h = img.h;
  frame->stride = (img.w * 3 + 3) & ~3;
  frame->bgr.resize(static_cast<size_t>(frame->stride) * img.h);
  for (int y = 0; y < img.h; ++y) {
    const uint8_t* src = &rgb[static_cast<size_t>(y) * img.w * 3];
    uint8_t* dst = &frame->bgr[static_cast<size_t>(y) * frame->stride];
    for (int x = 0; x < img.w; ++x, src += 3, dst += 3) {
      dst[0] = src[2];
      dst[1] = src[1];
      dst[2] = src[0];
    }
  }
  return frame;
}

// Everything the worker thread hands to the UI thread. All fields are guarded
// by `mutex`. A newer progress or frame simply replaces an unconsumed one.
struct Mailbox {
  std::mutex mutex;
  bool has_progress = false;
  Progress progress;
  std::unique_ptr<Frame> frame;       // newest rendered frame, if not yet taken
  bool done = false;                  // the worker has finished; join it
  std::unique_ptr<Artwork> artwork;   // set together with `done`
  std::string error;                  // set together with `done` on failure
  // From the thread that runs a step outside the engine.
  bool has_tool_line = false;
  std::string tool_line;              // the newest line the step printed
  bool tool_done = false;             // the step has ended; join its thread
  ToolResult tool_result;             // set together with `tool_done`
  std::string tool_tail;              // the last lines it printed, for a failure report
};

// ---------------------------------------------------------------------------
// DPI

void enable_dpi_awareness() {
  // Looked up at run time so that the program still starts on versions of
  // Windows that do not have per-monitor awareness.
  HMODULE user = GetModuleHandleW(L"user32.dll");
  using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
  SetContextFn set_context =
      user ? reinterpret_cast<SetContextFn>(GetProcAddress(user, "SetProcessDpiAwarenessContext")) : nullptr;
  if (set_context) {
    if (set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    if (set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) return;
  }
  SetProcessDPIAware();
}

// ---------------------------------------------------------------------------
// The list of items

// The project is not built with UNICODE, so the list-view macros would send
// the narrow messages. These send the wide ones.
void list_add_column(HWND list, int index, const wchar_t* title) {
  LVCOLUMNW c{};
  c.mask = LVCF_TEXT | LVCF_SUBITEM;
  c.pszText = const_cast<wchar_t*>(title);
  c.iSubItem = index;
  SendMessageW(list, LVM_INSERTCOLUMNW, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&c));
}

void list_set_text(HWND list, int row, int column, const std::wstring& text) {
  LVITEMW it{};
  it.iSubItem = column;
  it.pszText = const_cast<wchar_t*>(text.c_str());
  SendMessageW(list, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&it));
}

int list_selected_row(HWND list) {
  return static_cast<int>(SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED));
}

// Selects one row, or none with row -1.
void list_select_row(HWND list, int row) {
  LVITEMW it{};
  it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
  SendMessageW(list, LVM_SETITEMSTATE, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(&it));
  if (row < 0) return;
  it.state = LVIS_SELECTED | LVIS_FOCUSED;
  SendMessageW(list, LVM_SETITEMSTATE, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&it));
  SendMessageW(list, LVM_ENSUREVISIBLE, static_cast<WPARAM>(row), FALSE);
}

// The color an item is tinted with in the scene view and marked with in the
// list: hues spread by the golden ratio, so neighbors in the list differ.
COLORREF item_color(int row) {
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
  return RGB(static_cast<int>(r * 255.0), static_cast<int>(g * 255.0), static_cast<int>(b * 255.0));
}

// ---------------------------------------------------------------------------
// The window

class App {
 public:
  App() = default;
  App(const App&) = delete;
  App& operator=(const App&) = delete;
  ~App();

  bool create(HINSTANCE instance);
  HWND window() const { return hwnd_; }
  bool load_photo(const std::wstring& path);
  bool handle_help_message(MSG& msg);

 private:
  static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static LRESULT CALLBACK preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static App* from_window(HWND hwnd, UINT msg, LPARAM lp);

  LRESULT on_message(UINT msg, WPARAM wp, LPARAM lp);
  void on_command(int id, int code);
  void on_notify(const NMHDR* header);
  void on_drop(HDROP drop);
  void on_close();
  void on_preview_click(int x, int y);

  HWND add_control(const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex_style, int id);
  void create_controls();
  void rebuild_font();
  void layout();
  void place(HWND control, int x, int y, int w, int h);
  int px(int dip) const { return MulDiv(dip, dpi_, 96); }

  // Tooltips and the help window.
  void add_tip(HWND control, HWND label, const wchar_t* text);
  void add_tips();
  void update_tip_rects();
  HelpTopic topic_of(HWND control) const;
  void show_help(HelpTopic topic);

  void choose_photo();
  void save_artwork();
  void start();
  void request_stop();
  void finish(std::unique_ptr<Artwork> art, const std::string& error);
  void drain_mailbox();
  void show_progress(const Progress& p);
  void set_status(const std::wstring& text);
  void update_description();
  void update_medium_options();
  void update_key_text();
  void update_controls();
  const Frame* shown_frame() const;

  // What the preview shows: the choices offered follow what there is to show.
  View view() const;
  void set_view(View v);
  void update_view_choices();

  // The scene panel.
  void load_scene_for(const std::wstring& photo_path, int photo_w, int photo_h);
  void show_scene();
  void update_scene_summary();
  void set_item_row(int row);
  void show_item_editors();
  void on_item_selected();
  void apply_item_edit();
  void build_scene_frame();
  int selected_item() const;  // index into scene_->items, or -1

  // The steps outside the engine: observe and interpret.
  bool busy() const { return working_ || tool_ != Tool::None; }
  void start_observe();
  void start_imagine(bool ask_again);
  void begin_tool(Tool which, std::vector<ToolStep> steps);
  void request_tool_stop();
  void show_tool_status();
  void finish_tool(const ToolResult& result, const std::string& tail);
  void run_tool_worker(std::vector<ToolStep> steps);  // tool thread only

  // The interpretation of the chosen manner (spec 2.14): the model's choice
  // of a mechanism per item, and the imagined subject made from it.
  int manner_index() const;  // index into kManners, or -1 for the photo as it is
  std::wstring interpret_dir() const;
  std::wstring choice_file(int manner) const;
  std::wstring subject_file(int manner) const;
  void load_interpretation();
  Json* pick_for(int item_id);
  std::wstring change_text(int item_id);
  void apply_pick_edit();
  void invalidate_preview();
  void paint_preview(HWND hwnd);
  void draw_preview(HDC dc, int cw, int ch) const;

  // Worker thread only.
  void run_worker(std::shared_ptr<const Image> photo, std::shared_ptr<const Scene> scene, Settings settings);
  void notify_ui();

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;  // set once during creation; read-only afterwards
  HWND preview_ = nullptr;
  HWND open_ = nullptr, help_button_ = nullptr, file_label_ = nullptr;
  HWND tips_ = nullptr;  // the tooltip control
  struct Tip {
    HWND control, label;  // the label may be null
    std::wstring text;
  };
  std::vector<Tip> tip_list_;
  HelpWindow help_;
  HWND medium_label_ = nullptr, medium_ = nullptr, description_ = nullptr;
  HWND surface_label_ = nullptr, surface_ = nullptr;
  HWND size_label_ = nullptr, size_ = nullptr;
  HWND effort_label_ = nullptr, effort_ = nullptr;
  HWND seed_label_ = nullptr, seed_ = nullptr;
  // One row for what only the chosen medium has: oil's sky blending, or the
  // watercolor key.
  HWND sky_blend_ = nullptr;
  HWND key_label_ = nullptr, key_ = nullptr, key_value_ = nullptr;
  HWND start_ = nullptr, save_ = nullptr;
  HWND view_label_ = nullptr, view_ = nullptr;
  HWND progress_ = nullptr, status_ = nullptr;
  // The scene panel.
  HWND scene_summary_ = nullptr, use_scene_ = nullptr, items_ = nullptr;
  HWND item_title_ = nullptr;
  HWND importance_label_ = nullptr, importance_ = nullptr;
  HWND treatment_label_ = nullptr, treatment_ = nullptr;
  HWND edges_label_ = nullptr, edges_ = nullptr;
  HWND item_hint_ = nullptr;
  HWND observe_ = nullptr;
  HWND manner_label_ = nullptr, manner_ = nullptr, imagine_ = nullptr, ask_again_ = nullptr;
  HWND change_label_ = nullptr, mechanism_ = nullptr, strength_ = nullptr;
  HIMAGELIST swatches_ = nullptr;  // one square of its color per item, for the list
  HWND last_focus_ = nullptr;
  std::vector<HWND> controls_;  // every child that takes the UI font
  std::unordered_map<HWND, RECT> placed_;  // where layout() last put each child

  HFONT font_ = nullptr;
  int dpi_ = 96;
  int line_h_ = 16;       // height of one line of UI text, pixels
  int min_client_h_ = 0;  // height the panel needs, pixels

  // Preview back buffer: the fitted image is drawn into it only when the
  // frame or the size changes, and WM_PAINT just copies it to the screen.
  HBITMAP back_bmp_ = nullptr;
  int back_w_ = 0, back_h_ = 0;
  bool back_dirty_ = true;

  std::vector<std::string> media_, surfaces_;

  // The photo is shared with the worker for the duration of a run, so a run
  // keeps its subject alive no matter what the UI does meanwhile.
  std::shared_ptr<const Image> photo_;
  // What the photo shows (spec 2.13); null if it has no scene file. It is
  // corrected in place from the scene panel, which is disabled during a run,
  // so the worker's reference never sees a change.
  std::shared_ptr<Scene> scene_;
  std::string scene_path_;                  // UTF-8; the file scene_ was read from
  std::vector<std::string> treatment_choices_;  // the entries of the treatment box
  bool filling_ = false;                    // the panel is being filled; its notifications are not edits
  int shown_item_ = -1;                     // the item the scene view highlights
  std::unique_ptr<Frame> photo_frame_;
  std::unique_ptr<Frame> scene_frame_;      // the items tinted over the photo; made when first shown
  std::wstring photo_path_;
  mutable RECT shown_rect_{};               // where the preview last drew its picture, client pixels

  std::unique_ptr<Artwork> artwork_;   // what "Save artwork..." writes
  std::unique_ptr<Frame> art_frame_;   // latest live frame, then the final image
  std::wstring art_dir_, art_name_;    // default save location and file name
  std::wstring status_text_;

  bool working_ = false;
  std::thread worker_;
  std::atomic<bool> stop_{false};            // UI -> worker: stop at the next progress call
  std::atomic<bool> notify_pending_{false};  // a wake-up is posted and not yet consumed
  Mailbox mailbox_;

  // A step outside the engine, while it runs. Its thread hands over what the
  // step prints through the Mailbox, like the painting worker.
  Tool tool_ = Tool::None;
  std::thread tool_thread_;
  std::atomic<bool> tool_stop_{false};  // UI -> tool thread: end the step
  Clock::time_point tool_started_{};
  std::wstring tool_line_;              // the newest line the step printed

  // The interpretation of the chosen manner, read from the files of the
  // interpret step beside the photo.
  Json choice_;                           // the choice file; null if there is none
  std::wstring choice_path_;
  std::shared_ptr<const Image> subject_;  // the imagined subject; null if not made
  std::shared_ptr<Scene> subject_scene_;  // its scene file
  std::unique_ptr<Frame> subject_frame_;
  bool subject_stale_ = false;            // the choice changed after the subject was made
  std::wstring art_manner_;               // the manner of the artwork shown, for its file name
};

App::~App() {
  // Safety net; the normal paths join the worker in finish() or on_close().
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
  tool_stop_.store(true);
  if (tool_thread_.joinable()) tool_thread_.join();
  if (back_bmp_) DeleteObject(back_bmp_);
  if (font_) DeleteObject(font_);
  if (swatches_) ImageList_Destroy(swatches_);
}

App* App::from_window(HWND hwnd, UINT msg, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  return reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK App::main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  App* app = from_window(hwnd, msg, lp);
  if (!app) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_NCCREATE) app->hwnd_ = hwnd;
  return app->on_message(msg, wp, lp);
}

LRESULT CALLBACK App::preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  App* app = from_window(hwnd, msg, lp);
  if (app) {
    switch (msg) {
      case WM_ERASEBKGND:
        return 1;  // WM_PAINT covers the whole client area from the back buffer
      case WM_PAINT:
        app->paint_preview(hwnd);
        return 0;
      case WM_LBUTTONDOWN:
        app->on_preview_click(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
        return 0;
      default:
        break;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool App::create(HINSTANCE instance) {
  instance_ = instance;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = main_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, kCursorArrow);
  wc.hIcon = LoadIconW(nullptr, kIconApplication);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
  wc.lpszClassName = kMainClass;
  if (!RegisterClassExW(&wc)) return false;

  WNDCLASSEXW pc{};
  pc.cbSize = sizeof(pc);
  pc.lpfnWndProc = preview_proc;
  pc.hInstance = instance;
  pc.hCursor = LoadCursorW(nullptr, kCursorArrow);
  pc.hbrBackground = nullptr;  // never erased; see preview_proc
  pc.lpszClassName = kPreviewClass;
  if (!RegisterClassExW(&pc)) return false;

  // WS_CLIPCHILDREN keeps the parent from painting over the preview, which is
  // half of what makes resizing flicker-free.
  CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES, kMainClass, kAppTitle,
                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                  CW_USEDEFAULT, nullptr, nullptr, instance, this);
  if (!hwnd_) return false;

  dpi_ = window_dpi(hwnd_);
  create_controls();
  if (!preview_ || !start_ || !progress_) return false;
  rebuild_font();

  // Initial size in DPI-scaled units, centered on and limited to the work
  // area of the monitor the window was created on.
  int w = px(1600), h = px(840);
  int x = 0, y = 0;
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi)) {
    const RECT& wa = mi.rcWork;
    w = std::min(w, static_cast<int>(wa.right - wa.left));
    h = std::min(h, static_cast<int>(wa.bottom - wa.top));
    x = wa.left + (wa.right - wa.left - w) / 2;
    y = wa.top + (wa.bottom - wa.top - h) / 2;
  }
  SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  layout();
  update_description();
  update_medium_options();
  update_view_choices();
  show_scene();
  update_controls();
  set_status(L"Open a photo to begin.");

  ShowWindow(hwnd_, SW_SHOWNORMAL);
  UpdateWindow(hwnd_);
  SetFocus(open_);
  return true;
}

HWND App::add_control(const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex_style, int id) {
  HWND h = CreateWindowExW(ex_style, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
  if (h) controls_.push_back(h);
  return h;
}

void App::create_controls() {
  const DWORD label = SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE;
  const DWORD combo = CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP;
  const DWORD button = BS_PUSHBUTTON | WS_TABSTOP;

  // Creation order is tab order.
  open_ = add_control(L"BUTTON", L"Open photo...", button, 0, kIdOpen);
  help_button_ = add_control(L"BUTTON", L"Help", button, 0, kIdHelp);
  file_label_ = add_control(L"STATIC", L"No photo loaded", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0);

  medium_label_ = add_control(L"STATIC", L"Medium", label, 0, 0);
  medium_ = add_control(L"COMBOBOX", L"", combo, 0, kIdMedium);
  description_ = add_control(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 0);

  surface_label_ = add_control(L"STATIC", L"Surface", label, 0, 0);
  surface_ = add_control(L"COMBOBOX", L"", combo, 0, kIdSurface);

  size_label_ = add_control(L"STATIC", L"Size", label, 0, 0);
  size_ = add_control(L"COMBOBOX", L"", combo, 0, kIdSize);

  effort_label_ = add_control(L"STATIC", L"Effort", label, 0, 0);
  effort_ = add_control(L"COMBOBOX", L"", combo, 0, kIdEffort);

  seed_label_ = add_control(L"STATIC", L"Seed", label, 0, 0);
  seed_ = add_control(L"EDIT", L"1", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, kIdSeed);

  // Both sit in the same row; update_medium_options shows the one the
  // medium has.
  sky_blend_ = add_control(L"BUTTON", L"Blend the sky and clouds", BS_AUTOCHECKBOX | WS_TABSTOP, 0, kIdSkyBlend);
  key_label_ = add_control(L"STATIC", L"Key", label, 0, 0);
  key_ = add_control(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, kIdKey);
  key_value_ = add_control(L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, 0, 0);

  start_ = add_control(L"BUTTON", L"Start", button, 0, kIdStart);
  save_ = add_control(L"BUTTON", L"Save artwork...", button, 0, kIdSave);
  view_label_ = add_control(L"STATIC", L"Show", label, 0, 0);
  view_ = add_control(L"COMBOBOX", L"", combo, 0, kIdView);

  progress_ = add_control(PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, 0);
  status_ = add_control(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 0);

  // The scene panel, top to bottom.
  scene_summary_ = add_control(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 0);
  use_scene_ = add_control(L"BUTTON", L"Use the scene when painting", BS_AUTOCHECKBOX | WS_TABSTOP, 0, kIdUseScene);
  observe_ = add_control(L"BUTTON", L"Observe photo", button, 0, kIdObserve);
  manner_label_ = add_control(L"STATIC", L"Manner", label, 0, 0);
  manner_ = add_control(L"COMBOBOX", L"", combo, 0, kIdManner);
  imagine_ = add_control(L"BUTTON", L"Imagine", button, 0, kIdImagine);
  ask_again_ = add_control(L"BUTTON", L"Ask the model again", button, 0, kIdAskAgain);
  items_ = add_control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP,
                       WS_EX_CLIENTEDGE, kIdItems);
  item_title_ = add_control(L"STATIC", L"", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0);
  importance_label_ = add_control(L"STATIC", L"Importance", label, 0, 0);
  importance_ = add_control(L"COMBOBOX", L"", combo, 0, kIdImportance);
  treatment_label_ = add_control(L"STATIC", L"Treatment", label, 0, 0);
  treatment_ = add_control(L"COMBOBOX", L"", combo, 0, kIdTreatment);
  edges_label_ = add_control(L"STATIC", L"Edges", label, 0, 0);
  edges_ = add_control(L"COMBOBOX", L"", combo, 0, kIdEdges);
  change_label_ = add_control(L"STATIC", L"Change", label, 0, 0);
  mechanism_ = add_control(L"COMBOBOX", L"", combo, 0, kIdMechanism);
  strength_ = add_control(L"COMBOBOX", L"", combo, 0, kIdStrength);
  item_hint_ = add_control(L"STATIC",
                           L"What you set here is saved at once, to the scene file or to the manner's choice file. "
                           L"The painters act on the treatments skin, hair, sky, cloud and foliage mass.",
                           SS_LEFT | SS_NOPREFIX, 0, 0);

  preview_ = CreateWindowExW(0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, instance_,
                             this);

  auto add_item = [](HWND box, const std::wstring& text) {
    SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
  };

  media_ = medium_names();
  int default_medium = 0;
  const std::string preferred = Settings{}.medium;
  for (size_t i = 0; i < media_.size(); ++i) {
    add_item(medium_, pretty_name(media_[i], true));
    if (media_[i] == preferred) default_medium = static_cast<int>(i);
  }
  SendMessageW(medium_, CB_SETCURSEL, static_cast<WPARAM>(default_medium), 0);

  // Entry 0 means "let the medium decide"; it is never changed behind the
  // user's back, so it stays selected across medium changes until the user
  // picks a specific surface.
  surfaces_ = surface_names();
  add_item(surface_, L"(default for medium)");
  for (const std::string& s : surfaces_) add_item(surface_, pretty_name(s, true));
  SendMessageW(surface_, CB_SETCURSEL, 0, 0);

  for (int s : kSizes) add_item(size_, std::to_wstring(s) + L" px");
  SendMessageW(size_, CB_SETCURSEL, kDefaultSizeIndex, 0);

  for (const EffortChoice& e : kEfforts) add_item(effort_, e.label);
  SendMessageW(effort_, CB_SETCURSEL, kDefaultEffortIndex, 0);

  SendMessageW(seed_, EM_SETLIMITTEXT, 10, 0);  // enough for any 32-bit value
  SendMessageW(progress_, PBM_SETRANGE32, 0, kProgressRange);

  SendMessageW(sky_blend_, BM_SETCHECK, Settings{}.techniques.oil_blend_sky ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(key_, TBM_SETRANGE, FALSE, MAKELPARAM(0, (kKeyMaxPct - kKeyMinPct) / kKeyStepPct));
  SendMessageW(key_, TBM_SETPAGESIZE, 0, 2);
  SendMessageW(key_, TBM_SETPOS, TRUE, (kKeyDefaultPct - kKeyMinPct) / kKeyStepPct);
  update_key_text();

  SendMessageW(use_scene_, BM_SETCHECK, BST_CHECKED, 0);
  SendMessageW(items_, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
  const wchar_t* const columns[] = {L"Item", L"Importance", L"Treatment", L"Edges", L"Change"};
  for (int i = 0; i < static_cast<int>(std::size(columns)); ++i) list_add_column(items_, i, columns[i]);
  for (const wchar_t* name : kImportanceNames) add_item(importance_, name);
  for (const wchar_t* name : kEdgeNames) add_item(edges_, name);

  // Painting the photo as it is comes first and is the default (spec 2.14).
  add_item(manner_, L"As the photo is");
  for (const Manner& m : kManners) add_item(manner_, m.label);
  SendMessageW(manner_, CB_SETCURSEL, 0, 0);

  tips_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd_, nullptr, instance_, nullptr);
  if (tips_) {
    SendMessageW(tips_, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);  // long enough to read a few sentences
    add_tips();
  }
}

// What each control is for, in a sentence or two. The help pages
// (help_text.cpp) say more; keep the two in step.
void App::add_tips() {
  add_tip(open_, nullptr,
          L"Open a photo (JPEG, PNG or BMP). You can also drop one on the window. A scene file beside the photo is "
          L"loaded with it.");
  add_tip(help_button_, nullptr,
          L"Open the help. F1 opens the page for the part of the window you are in. The help window stays open "
          L"beside your work.");
  add_tip(medium_, medium_label_,
          L"What the artwork is made with. Each medium is simulated with its own tools: oil with bristle brushes, "
          L"watercolor with washes on paper, pastel with soft sticks.");
  add_tip(surface_, surface_label_,
          L"What the artwork is made on. The default is canvas for oil, cold-pressed paper for watercolor and pastel "
          L"paper for pastel.");
  add_tip(size_, size_label_,
          L"The long side of the artwork in pixels. It is also the resolution of the simulation, so a larger size "
          L"takes longer.");
  add_tip(effort_, effort_label_,
          L"How far the artist takes the piece. More effort is more and finer work, and more time.");
  add_tip(seed_, seed_label_,
          L"A whole number that fixes every chance decision. The same photo, scene, settings and seed give exactly "
          L"the same artwork; another seed gives another realization.");
  add_tip(sky_blend_, nullptr,
          L"Oil, with a scene: after each broad layer the sky and the clouds are gone over with a clean, soft brush, "
          L"so that they melt into a gradation. Off, the sky is painted like everything else.");
  add_tip(key_, key_label_,
          L"Watercolor: how light the picture is keyed. Lower aims the mid-tones darker, higher lighter, toward the "
          L"paper; the darks stay anchored. The default is 0.85.");
  add_tip(start_, nullptr,
          L"Make the artwork. While it is being made this button stops it, and what has been painted so far is kept. "
          L"With a manner chosen, the imagined subject is painted in place of the photo.");
  add_tip(save_, nullptr, L"Save the artwork as PNG, JPEG or BMP, by the ending of the file name.");
  add_tip(view_, view_label_,
          L"What the preview shows: the photo, its scene (the items tinted over the photo), the imagined subject of "
          L"the chosen manner, or the artwork.");
  add_tip(scene_summary_, nullptr,
          L"The scene file says what the photo shows. It lies beside the photo and has the photo's name with "
          L".scene.json in place of the ending.");
  add_tip(use_scene_, nullptr,
          L"On: the artist works with the scene, with finer work on what matters and, in watercolor and pastel, each "
          L"item worked on its own. Off: the photo is painted as if it had no scene file.");
  add_tip(observe_, nullptr,
          L"Write the photo's scene file by looking at the photo with models that run on this machine. It takes a "
          L"minute or two, and replaces a scene file that is there, with the corrections made to it.");
  add_tip(manner_, manner_label_,
          L"How the subject is interpreted. \"As the photo is\" paints the photo. A manner changes things in the "
          L"picture because of what they are; press Imagine to prepare it.");
  add_tip(imagine_, nullptr,
          L"Prepare the imagined subject of the chosen manner: a model on this machine chooses what changes, unless "
          L"there is a choice already, and the changes are carried out on the photo. Start then paints the result.");
  add_tip(ask_again_, nullptr,
          L"Have the model choose again what the manner changes. The choice shown is replaced, with anything you "
          L"changed in it.");
  add_tip(items_, nullptr,
          L"The items of the scene; a part is set in below its whole. Select an item to see it in the scene view and "
          L"to change it below. The last column says what the chosen manner does to it.");
  add_tip(importance_, importance_label_,
          L"How much the item matters to the picture. The main subject and what matters get finer work; supporting "
          L"items and the background get broader work.");
  add_tip(treatment_, treatment_label_,
          L"What the item is, as one word. The painters act on skin (watercolor), hair (pastel), sky and cloud "
          L"(watercolor and oil) and foliage mass (watercolor).");
  add_tip(edges_, edges_label_,
          L"The item's outline in watercolor and pastel: hard (crisp), soft (gradual) or lost (it merges into its "
          L"surroundings). An outline is worked softly only where both items that meet are soft or lost.");
  add_tip(mechanism_, change_label_,
          L"What the chosen manner does to this item; \"As it is\" leaves it alone. It is saved to the manner's choice "
          L"file. Press Imagine to carry it out.");
  add_tip(strength_, nullptr,
          L"How strongly the mechanism is applied, from 1 to 3. For the solid of triangles it is how fine the "
          L"triangles are.");
}

void App::rebuild_font() {
  // The message font, at the DPI of the monitor the window is on.
  HFONT old = font_;
  font_ = create_message_font(dpi_);
  for (HWND h : controls_) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  if (old) DeleteObject(old);
  line_h_ = font_line_height(hwnd_, font_);
  // A tooltip is as wide as a short paragraph and stays while it is read.
  if (tips_) SendMessageW(tips_, TTM_SETMAXTIPWIDTH, 0, px(360));
}

// ---------------------------------------------------------------------------
// Tooltips and help

// Gives a control a tooltip. The label beside it, if it has one, shows the
// same tip. The tip also shows while the control is disabled, which is when
// a user most wants to know what it is for: a disabled control passes the
// pointer through to this window, where a tool of the control's rectangle
// answers.
void App::add_tip(HWND control, HWND label, const wchar_t* text) {
  if (!tips_ || !control) return;
  tip_list_.push_back({control, label, text});
  TOOLINFOW tool{};
  tool.cbSize = sizeof(tool);
  tool.hwnd = hwnd_;
  tool.lpszText = LPSTR_TEXTCALLBACKW;  // asked for when shown (on_notify), so a tip may be of any length
  tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  tool.uId = reinterpret_cast<UINT_PTR>(control);
  SendMessageW(tips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
  // Rectangle tools: the control's and the label's, numbered from 1. Their
  // rectangles are set by update_tip_rects.
  tool.uFlags = TTF_SUBCLASS;
  for (int part = 0; part < 2; ++part) {
    tool.uId = static_cast<UINT_PTR>(2 * (tip_list_.size() - 1) + 1 + part);
    SendMessageW(tips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}

// Puts the rectangle tools where their controls are. A control that is
// hidden has none, so that the one shown in its place answers.
void App::update_tip_rects() {
  if (!tips_) return;
  for (size_t i = 0; i < tip_list_.size(); ++i) {
    const HWND parts[2] = {tip_list_[i].control, tip_list_[i].label};
    for (int part = 0; part < 2; ++part) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.hwnd = hwnd_;
      tool.uId = static_cast<UINT_PTR>(2 * i + 1 + part);
      if (parts[part] && IsWindowVisible(parts[part])) {
        GetWindowRect(parts[part], &tool.rect);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&tool.rect), 2);
      }
      SendMessageW(tips_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
  }
}

// The help page for the part of the window a control belongs to.
HelpTopic App::topic_of(HWND control) const {
  for (HWND h : {use_scene_, items_, importance_, treatment_, edges_})
    if (control == h) return HelpTopic::Scene;
  if (control == observe_) return HelpTopic::Observe;
  for (HWND h : {manner_, imagine_, ask_again_, mechanism_, strength_})
    if (control == h) return HelpTopic::Manners;
  for (HWND h : {medium_, surface_, size_, effort_, seed_, sky_blend_, key_, start_, save_, view_})
    if (control == h) return HelpTopic::Painting;
  return HelpTopic::Start;
}

void App::show_help(HelpTopic topic) { help_.show(hwnd_, instance_, topic); }

// For the message loop: F1 opens the page of the control that has the
// focus, and the help window gets its own keyboard handling.
bool App::handle_help_message(MSG& msg) {
  if (msg.message == WM_KEYDOWN && msg.wParam == VK_F1) {
    // From the help window itself F1 changes nothing.
    // The key goes to the control that has the focus, which says what the
    // user is working with.
    if (!help_.owns(msg.hwnd)) show_help(topic_of(msg.hwnd));
    return true;
  }
  return help_.handle_message(msg);
}

// Puts a child where the layout wants it. A child that moves or changes
// size is drawn again as a whole: Windows would otherwise keep the pixels it
// had, which is wrong for the preview (its picture is fitted to its size)
// and for controls that passed over each other on the way. A child that
// stays where it is is left alone.
void App::place(HWND control, int x, int y, int w, int h) {
  if (!control) return;
  const RECT want{x, y, x + w, y + h};
  const auto last = placed_.find(control);
  if (last != placed_.end() && EqualRect(&last->second, &want)) return;
  placed_[control] = want;
  SetWindowPos(control, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
}

void App::layout() {
  if (!preview_) return;  // WM_SIZE during creation
  RECT rc{};
  GetClientRect(hwnd_, &rc);

  const int panel = px(kPanelWidthDip);
  const int margin = px(12);
  const int inner = panel - 2 * margin;
  const int label_w = px(62);
  const int field_x = margin + label_w + px(6);
  const int field_w = panel - margin - field_x;
  const int row_h = line_h_ + px(8);  // the natural height of a combo box or edit box
  const int drop_h = px(320);         // a combo box's height includes its drop-down list

  int y = margin;
  auto full_width = [&](HWND h, int height, int gap_after) {
    place(h, margin, y, inner, height);
    y += height + gap_after;
  };
  auto row = [&](HWND label, HWND field, int field_h) {
    place(label, margin, y, label_w, row_h);
    place(field, field_x, y, field_w, field_h);
    y += row_h + px(8);
  };

  {
    // Open, and Help beside it.
    const int help_w = px(64);
    place(open_, margin, y, inner - help_w - px(6), px(30));
    place(help_button_, margin + inner - help_w, y, help_w, px(30));
    y += px(30) + px(6);
  }
  full_width(file_label_, line_h_ + px(2), px(14));
  row(medium_label_, medium_, drop_h);
  full_width(description_, 3 * line_h_, px(10));
  row(surface_label_, surface_, drop_h);
  row(size_label_, size_, drop_h);
  row(effort_label_, effort_, drop_h);
  row(seed_label_, seed_, row_h);
  // The medium's own setting: the check box and the slider share this row.
  {
    const int value_w = px(40);
    place(sky_blend_, margin, y, inner, row_h);
    place(key_label_, margin, y, label_w, row_h);
    place(key_, field_x - px(6), y, field_w - value_w + px(6), row_h);
    place(key_value_, panel - margin - value_w + px(4), y, value_w - px(4), row_h);
    y += row_h + px(8);
  }
  y += px(6);
  full_width(start_, px(36), px(8));
  full_width(save_, px(30), px(12));
  row(view_label_, view_, drop_h);
  y += px(6);
  full_width(progress_, px(16), px(8));
  // The status takes what is left of the panel: a step outside the engine
  // reports in whole sentences.
  const int left_h = y + 3 * line_h_ + margin;
  full_width(status_, std::max(3 * line_h_, static_cast<int>(rc.bottom) - y - margin), margin);

  // The scene panel on the right: the summary and the switch at the top, the
  // editors of the selected item at the bottom, the list of items between.
  const int scene_panel = px(kScenePanelWidthDip);
  const int sx = std::max(panel, static_cast<int>(rc.right) - scene_panel) + margin;
  const int s_inner = scene_panel - 2 * margin;
  const int s_label_w = px(74);
  const int s_field_x = sx + s_label_w + px(6);
  const int s_field_w = sx + s_inner - s_field_x;
  auto s_full = [&](HWND h, int top, int height) { place(h, sx, top, s_inner, height); };

  auto s_row = [&](HWND label, HWND field, int& at) {
    place(label, sx, at, s_label_w, row_h);
    place(field, s_field_x, at, s_field_w, drop_h);
    at += row_h + px(8);
  };

  int top = margin;
  s_full(scene_summary_, top, 2 * line_h_);
  top += 2 * line_h_ + px(8);
  {
    // The switch, and beside it the button that writes the scene file.
    const int button_w = px(130), button_h = px(28);
    place(use_scene_, sx, top, s_inner - button_w - px(8), button_h);
    place(observe_, sx + s_inner - button_w, top, button_w, button_h);
    top += button_h + px(10);
  }
  s_row(manner_label_, manner_, top);
  {
    const int half = (s_inner - px(8)) / 2, button_h = px(28);
    place(imagine_, sx, top, half, button_h);
    place(ask_again_, sx + s_inner - half, top, half, button_h);
    top += button_h + px(10);
  }
  const int head_h = top;

  const int hint_h = 3 * line_h_;
  const int editors_h = (line_h_ + px(8)) + 4 * (row_h + px(8)) + hint_h + margin;
  const int list_h = std::max(px(kMinItemListHeightDip), static_cast<int>(rc.bottom) - top - px(10) - editors_h);
  s_full(items_, top, list_h);
  {
    // The name takes what the other columns and the scroll bar leave. The
    // last column is there only when a manner is chosen.
    const int widths[] = {0, px(84), px(112), px(46), manner_index() >= 0 ? px(92) : 0};
    const int rest =
        s_inner - widths[1] - widths[2] - widths[3] - widths[4] - GetSystemMetrics(SM_CXVSCROLL) - px(6);
    for (int i = 0; i < 5; ++i)
      SendMessageW(items_, LVM_SETCOLUMNWIDTH, static_cast<WPARAM>(i), i == 0 ? std::max(px(60), rest) : widths[i]);
  }
  top += list_h + px(10);
  s_full(item_title_, top, line_h_ + px(2));
  top += line_h_ + px(8);
  s_row(importance_label_, importance_, top);
  s_row(treatment_label_, treatment_, top);
  s_row(edges_label_, edges_, top);
  {
    // What the manner does to the item: the mechanism and its strength.
    const int strength_w = px(104);
    place(change_label_, sx, top, s_label_w, row_h);
    place(mechanism_, s_field_x, top, s_field_w - strength_w - px(6), drop_h);
    place(strength_, s_field_x + s_field_w - strength_w, top, strength_w, drop_h);
    top += row_h + px(8);
  }
  s_full(item_hint_, top, hint_h);

  const int right_h = head_h + px(kMinItemListHeightDip) + px(10) + editors_h;
  min_client_h_ = std::max(left_h, right_h);

  place(preview_, panel, 0, std::max(0, static_cast<int>(rc.right) - panel - scene_panel), rc.bottom);
  update_tip_rects();
}

LRESULT App::on_message(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      // A minimized window keeps its layout; live frames are skipped by the
      // worker while it is minimized and resume when it is restored.
      if (wp != SIZE_MINIMIZED) layout();
      return 0;

    case WM_GETMINMAXINFO: {
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lp);
      RECT r{0, 0, px(kPanelWidthDip + kMinPreviewWidthDip + kScenePanelWidthDip), std::max(min_client_h_, px(200))};
      AdjustWindowRectEx(&r, static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE)), FALSE,
                         static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_EXSTYLE)));
      info->ptMinTrackSize.x = r.right - r.left;
      info->ptMinTrackSize.y = r.bottom - r.top;
      return 0;
    }

    case WM_DPICHANGED: {
      dpi_ = HIWORD(wp);
      rebuild_font();
      const RECT* suggested = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      layout();
      invalidate_preview();
      return 0;
    }

    case WM_COMMAND:
      on_command(LOWORD(wp), HIWORD(wp));
      return 0;

    case WM_NOTIFY:
      on_notify(reinterpret_cast<const NMHDR*>(lp));
      return 0;

    case WM_HSCROLL:
      // The key slider moved.
      if (reinterpret_cast<HWND>(lp) == key_) update_key_text();
      return 0;

    case WM_DROPFILES:
      on_drop(reinterpret_cast<HDROP>(wp));
      return 0;

    case kMsgWorker:
      drain_mailbox();
      return 0;

    case WM_TIMER:
      if (wp == kPollTimer) drain_mailbox();
      return 0;

    case WM_ACTIVATE:
      // A plain window does not remember which control had the focus the way
      // a dialog does, so do it by hand.
      if (LOWORD(wp) == WA_INACTIVE) {
        HWND focus = GetFocus();
        if (focus && IsChild(hwnd_, focus)) last_focus_ = focus;
      }
      break;

    case WM_SETFOCUS:
      if (last_focus_ && IsWindow(last_focus_) && IsWindowEnabled(last_focus_)) SetFocus(last_focus_);
      else if (start_ && IsWindowEnabled(start_)) SetFocus(start_);
      else if (open_ && IsWindowEnabled(open_)) SetFocus(open_);
      return 0;

    case WM_CLOSE:
      on_close();
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

void App::on_command(int id, int code) {
  switch (id) {
    case kIdOpen:
      if (code == BN_CLICKED) choose_photo();
      break;
    case kIdHelp:
      if (code == BN_CLICKED) show_help(HelpTopic::Start);
      break;
    case kIdMedium:
      if (code == CBN_SELCHANGE) {
        update_description();
        update_medium_options();
      }
      break;
    case kIdStart:
      if (code == BN_CLICKED) {
        if (working_) request_stop();
        else start();
      }
      break;
    case kIdSave:
      if (code == BN_CLICKED) save_artwork();
      break;
    case kIdView:
      if (code == CBN_SELCHANGE) invalidate_preview();
      break;
    case kIdImportance:
    case kIdTreatment:
    case kIdEdges:
      if (code == CBN_SELCHANGE) apply_item_edit();
      break;
    case kIdObserve:
      if (code == BN_CLICKED) {
        if (tool_ == Tool::Observe) request_tool_stop();
        else start_observe();
      }
      break;
    case kIdManner:
      if (code == CBN_SELCHANGE) {
        load_interpretation();
        layout();  // the list gains or loses its last column
        if (!busy()) set_view(subject_frame_ ? View::Subject : View::Photo);
      }
      break;
    case kIdImagine:
      if (code == BN_CLICKED) {
        if (tool_ == Tool::Imagine) request_tool_stop();
        else start_imagine(false);
      }
      break;
    case kIdAskAgain:
      if (code == BN_CLICKED) start_imagine(true);
      break;
    case kIdMechanism:
    case kIdStrength:
      if (code == CBN_SELCHANGE) apply_pick_edit();
      break;
    default:
      break;
  }
}

void App::on_notify(const NMHDR* header) {
  if (header && tips_ && header->hwndFrom == tips_ && header->code == TTN_GETDISPINFOW) {
    // A tooltip is about to show: hand it its text. The tool is either a
    // control (its handle) or a rectangle (its number).
    NMTTDISPINFOW* info = reinterpret_cast<NMTTDISPINFOW*>(const_cast<NMHDR*>(header));
    for (size_t i = 0; i < tip_list_.size(); ++i) {
      const bool by_handle = (info->uFlags & TTF_IDISHWND) != 0;
      const bool match = by_handle ? reinterpret_cast<HWND>(header->idFrom) == tip_list_[i].control
                                   : (header->idFrom == 2 * i + 1 || header->idFrom == 2 * i + 2);
      if (!match) continue;
      info->lpszText = tip_list_[i].text.data();
      break;
    }
    return;
  }
  if (!header || header->hwndFrom != items_ || header->code != LVN_ITEMCHANGED || filling_) return;
  const NMLISTVIEW* change = reinterpret_cast<const NMLISTVIEW*>(header);
  if ((change->uChanged & LVIF_STATE) && ((change->uOldState ^ change->uNewState) & LVIS_SELECTED)) on_item_selected();
}

// A click on the scene view selects the item under it.
void App::on_preview_click(int x, int y) {
  if (view() != View::Scene || !scene_ || scene_->empty()) return;
  const int w = shown_rect_.right - shown_rect_.left, h = shown_rect_.bottom - shown_rect_.top;
  if (w <= 0 || h <= 0 || x < shown_rect_.left || x >= shown_rect_.right || y < shown_rect_.top ||
      y >= shown_rect_.bottom)
    return;
  const Plane<uint8_t>& layer = scene_->item;
  const int lx = std::clamp(static_cast<int>(static_cast<long long>(x - shown_rect_.left) * layer.w / w), 0, layer.w - 1);
  const int ly = std::clamp(static_cast<int>(static_cast<long long>(y - shown_rect_.top) * layer.h / h), 0, layer.h - 1);
  const int id = layer(lx, ly);
  int row = -1;
  for (size_t i = 0; i < scene_->items.size(); ++i)
    if (scene_->items[i].id == id) row = static_cast<int>(i);
  list_select_row(items_, row);
}

void App::on_drop(HDROP drop) {
  std::wstring path;
  UINT n = DragQueryFileW(drop, 0, nullptr, 0);
  if (n > 0) {
    path.assign(n, L'\0');
    DragQueryFileW(drop, 0, path.data(), n + 1);
  }
  DragFinish(drop);
  if (path.empty()) return;
  if (busy()) {
    MessageBeep(MB_ICONWARNING);  // the subject cannot change mid-painting, or under a step that reads it
    return;
  }
  SetForegroundWindow(hwnd_);
  load_photo(path);
}

void App::on_close() {
  if (tool_thread_.joinable()) {
    // The step outside the engine is ended with everything it started.
    ShowWindow(hwnd_, SW_HIDE);
    tool_stop_.store(true);
    tool_thread_.join();
    tool_ = Tool::None;
  }
  if (worker_.joinable()) {
    // Hide first so the window is gone at once, then wait for the worker: it
    // stops at its next progress call, which is at most a few strokes away.
    // The worker only ever posts to this window (it never sends), so blocking
    // the UI thread here cannot deadlock. The window is destroyed only after
    // the join, so the worker never posts to a dead handle.
    ShowWindow(hwnd_, SW_HIDE);
    stop_.store(true);
    worker_.join();
    working_ = false;
  }
  KillTimer(hwnd_, kPollTimer);
  DestroyWindow(hwnd_);
}

// ---------------------------------------------------------------------------
// Photo and artwork files

void App::choose_photo() {
  if (busy()) return;
  wchar_t path[2048] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter = L"Photos (*.jpg;*.jpeg;*.png;*.bmp)\0*.jpg;*.jpeg;*.png;*.bmp\0All files (*.*)\0*.*\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = static_cast<DWORD>(std::size(path));
  ofn.lpstrTitle = L"Open photo";
  ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&ofn)) load_photo(path);
}

bool App::load_photo(const std::wstring& path) {
  if (busy()) return false;
  const std::wstring full = full_path(path);

  std::string error;
  std::shared_ptr<const Image> photo;
  std::unique_ptr<Frame> frame;
  HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, kCursorWait));
  try {
    // The image loader takes UTF-8 paths.
    Image img = load_image(to_utf8(full), &error);
    if (!img.empty()) {
      const int long_side = std::max(img.w, img.h);
      if (long_side > kMaxPreviewSide) {
        // Display copy only; the artist still gets the full photo.
        const float s = static_cast<float>(kMaxPreviewSide) / static_cast<float>(long_side);
        const int pw = std::max(1, static_cast<int>(std::lround(static_cast<float>(img.w) * s)));
        const int ph = std::max(1, static_cast<int>(std::lround(static_cast<float>(img.h) * s)));
        frame = make_frame(resize_area(img, pw, ph));
      } else {
        frame = make_frame(img);
      }
      photo = std::make_shared<const Image>(std::move(img));
    } else if (error.empty()) {
      error = "cannot read image";
    }
  } catch (const std::exception& e) {
    photo.reset();
    error = std::string("cannot load the photo: ") + e.what();
  }
  SetCursor(old_cursor);

  if (!photo) {
    MessageBoxW(hwnd_, widen(error, CP_UTF8).c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    return false;
  }

  photo_ = std::move(photo);
  photo_frame_ = std::move(frame);
  photo_path_ = full;
  const std::wstring caption = file_name_of(full) + L"  (" + std::to_wstring(photo_->w) + L" x " +
                               std::to_wstring(photo_->h) + L")";
  SetWindowTextW(file_label_, caption.c_str());
  load_scene_for(full, photo_->w, photo_->h);
  load_interpretation();

  // An earlier artwork stays available for saving until a new one is started;
  // switch the preview to the photo so the new subject is visible.
  update_view_choices();
  set_view(View::Photo);
  if (!artwork_) set_status(L"Choose a medium and press Start.");
  update_controls();
  invalidate_preview();
  return true;
}

// A scene file beside the photo says what it shows (spec 2.13). One that
// cannot be used is reported, and the photo is used without it.
void App::load_scene_for(const std::wstring& photo_path, int photo_w, int photo_h) {
  std::shared_ptr<Scene> scene;
  std::string scene_path, scene_error;
  try {
    scene_path = scene_path_for(to_utf8(photo_path));
    if (file_exists(scene_path)) {
      Scene loaded = load_scene(scene_path, &scene_error);
      if (!loaded.empty() && !loaded.fits(photo_w, photo_h)) {
        loaded = Scene{};
        scene_error = "scene file '" + scene_path + "': its item layer does not have the photo's proportions";
      }
      if (!loaded.empty()) scene = std::make_shared<Scene>(std::move(loaded));
    }
  } catch (const std::exception& e) {
    scene.reset();
    scene_error = std::string("cannot load the scene file: ") + e.what();
  }
  if (!scene && !scene_error.empty()) {
    const std::wstring message = widen(scene_error, CP_UTF8) + L"\n\nThe photo is opened without it.";
    MessageBoxW(hwnd_, message.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
  }
  scene_ = std::move(scene);
  scene_path_ = scene_ ? scene_path : std::string();
  show_scene();
}

// ---------------------------------------------------------------------------
// The scene panel

int App::selected_item() const {
  if (!scene_) return -1;
  const int row = list_selected_row(items_);
  return (row >= 0 && row < static_cast<int>(scene_->items.size())) ? row : -1;
}

void App::update_scene_summary() {
  std::wstring text;
  if (!photo_) {
    text = L"Open a photo to see its scene.";
  } else if (!scene_) {
    text = L"This photo has no scene file.\nIt is painted without one.";
  } else {
    text = file_name_of(widen(scene_path_, CP_UTF8)) + L"\n" + std::to_wstring(scene_->items.size()) + L" items";
    if (const SceneItem* main = scene_->find(scene_->main_subject))
      text += L", main subject: " + widen(main->name, CP_UTF8);
  }
  SetWindowTextW(scene_summary_, text.c_str());
}

// One row of the list from its item. A part is set in below its whole.
void App::set_item_row(int row) {
  const SceneItem& it = scene_->items[static_cast<size_t>(row)];
  int depth = 0;
  for (const SceneItem* p = scene_->find(it.parent); p && depth < 8; p = scene_->find(p->parent)) ++depth;
  list_set_text(items_, row, 0, std::wstring(static_cast<size_t>(depth) * 3, L' ') + widen(it.name, CP_UTF8));
  list_set_text(items_, row, 1, kImportanceNames[std::clamp(it.importance, 0, 3)]);
  list_set_text(items_, row, 2, pretty_name(it.treatment, false));
  list_set_text(items_, row, 3, kEdgeNames[static_cast<int>(it.edges)]);
  list_set_text(items_, row, 4, change_text(it.id));
}

// Fills the panel from scene_: the summary, the list, the editors.
void App::show_scene() {
  filling_ = true;
  SendMessageW(items_, LVM_DELETEALLITEMS, 0, 0);
  SendMessageW(items_, LVM_SETIMAGELIST, LVSIL_SMALL, 0);
  if (swatches_) ImageList_Destroy(swatches_);
  swatches_ = nullptr;
  scene_frame_.reset();
  shown_item_ = -1;

  if (scene_) {
    const int n = static_cast<int>(scene_->items.size());
    const int side = std::max(8, line_h_ - px(2));
    swatches_ = ImageList_Create(side, side, ILC_COLOR24, n, 0);
    if (HDC screen = GetDC(hwnd_)) {
      HDC mem = CreateCompatibleDC(screen);
      for (int i = 0; i < n && swatches_ && mem; ++i) {
        HBITMAP bmp = CreateCompatibleBitmap(screen, side, side);
        if (!bmp) break;
        HGDIOBJ old = SelectObject(mem, bmp);
        RECT r{0, 0, side, side};
        SetDCBrushColor(mem, item_color(i));
        FillRect(mem, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SelectObject(mem, old);
        ImageList_Add(swatches_, bmp, nullptr);
        DeleteObject(bmp);
      }
      if (mem) DeleteDC(mem);
      ReleaseDC(hwnd_, screen);
    }
    SendMessageW(items_, LVM_SETIMAGELIST, LVSIL_SMALL, reinterpret_cast<LPARAM>(swatches_));
    for (int i = 0; i < n; ++i) {
      LVITEMW row{};
      row.mask = LVIF_TEXT | LVIF_IMAGE;
      row.iItem = i;
      row.iImage = i;
      row.pszText = const_cast<wchar_t*>(L"");
      SendMessageW(items_, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&row));
      set_item_row(i);
    }
  }
  filling_ = false;
  update_scene_summary();
  show_item_editors();
}

// The editors show the selected item, or nothing.
void App::show_item_editors() {
  filling_ = true;
  const int i = selected_item();
  SendMessageW(treatment_, CB_RESETCONTENT, 0, 0);
  treatment_choices_.clear();
  if (i < 0) {
    SetWindowTextW(item_title_, scene_ ? L"Select an item in the list, or click it in the scene." : L"");
    SendMessageW(importance_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    SendMessageW(edges_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
  } else {
    const SceneItem& it = scene_->items[static_cast<size_t>(i)];
    SetWindowTextW(item_title_, widen(it.name, CP_UTF8).c_str());
    SendMessageW(importance_, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(it.importance, 0, 3)), 0);
    SendMessageW(edges_, CB_SETCURSEL, static_cast<WPARAM>(it.edges), 0);

    // The fixed words, then whatever else the file says, so that showing an
    // item never changes it.
    treatment_choices_.push_back(std::string());
    for (const char* t : kTreatments) treatment_choices_.push_back(t);
    if (std::find(treatment_choices_.begin(), treatment_choices_.end(), it.treatment) == treatment_choices_.end())
      treatment_choices_.push_back(it.treatment);
    int current = 0;
    for (size_t k = 0; k < treatment_choices_.size(); ++k) {
      const std::wstring text = treatment_choices_[k].empty() ? L"(not stated)" : pretty_name(treatment_choices_[k], true);
      SendMessageW(treatment_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
      if (treatment_choices_[k] == it.treatment) current = static_cast<int>(k);
    }
    SendMessageW(treatment_, CB_SETCURSEL, static_cast<WPARAM>(current), 0);
  }

  // What the chosen manner does to the item: one of the manner's mechanisms
  // and a strength, from the choice file.
  SendMessageW(mechanism_, CB_RESETCONTENT, 0, 0);
  SendMessageW(strength_, CB_RESETCONTENT, 0, 0);
  const int manner = manner_index();
  Json* pick = (i >= 0 && manner >= 0) ? pick_for(scene_->items[static_cast<size_t>(i)].id) : nullptr;
  if (pick) {
    const Json* mechanism = pick->get("mechanism");
    const Json* strength = pick->get("strength");
    const std::string chosen = (mechanism && mechanism->is_string()) ? mechanism->string : kAsItIs;
    SendMessageW(mechanism_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"As it is"));
    int current = 0, k = 1;
    for (const char* name : kManners[manner].mechanisms) {
      SendMessageW(mechanism_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pretty_name(name, true).c_str()));
      if (chosen == name) current = k;
      ++k;
    }
    SendMessageW(mechanism_, CB_SETCURSEL, static_cast<WPARAM>(current), 0);
    // For the solid of triangles the strength is how fine the triangles are.
    const bool triangles = std::string(kManners[manner].id) == "polygonal";
    const wchar_t* const fineness[] = {L"1 coarse", L"2 medium", L"3 fine"};
    const wchar_t* const strengths[] = {L"1 slight", L"2 clear", L"3 extreme"};
    for (int level = 0; level < 3; ++level)
      SendMessageW(strength_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(triangles ? fineness[level] : strengths[level]));
    const int s = (strength && strength->is_number()) ? std::clamp(static_cast<int>(strength->number), 1, 3) : 1;
    SendMessageW(strength_, CB_SETCURSEL, static_cast<WPARAM>(s - 1), 0);
  }
  filling_ = false;
  update_controls();
}

void App::on_item_selected() {
  show_item_editors();
  const int i = selected_item();
  if (i == shown_item_) return;
  shown_item_ = i;
  scene_frame_.reset();  // made again, with this item brought out, when next shown
  // Selecting an item is asking to see it; a painting in progress stays in
  // view.
  if (i >= 0 && !busy()) set_view(View::Scene);
  if (view() == View::Scene) invalidate_preview();
}

// One of the three editors changed: the item takes the new value and the
// scene file is saved, so the file always says what will be painted.
void App::apply_item_edit() {
  const int i = selected_item();
  if (filling_ || busy() || i < 0) return;
  SceneItem& it = scene_->items[static_cast<size_t>(i)];
  const SceneItem before = it;
  const int importance = static_cast<int>(SendMessageW(importance_, CB_GETCURSEL, 0, 0));
  const int treatment = static_cast<int>(SendMessageW(treatment_, CB_GETCURSEL, 0, 0));
  const int edges = static_cast<int>(SendMessageW(edges_, CB_GETCURSEL, 0, 0));
  if (importance >= 0 && importance <= 3) it.importance = importance;
  if (treatment >= 0 && treatment < static_cast<int>(treatment_choices_.size()))
    it.treatment = treatment_choices_[static_cast<size_t>(treatment)];
  if (edges >= 0 && edges <= 2) it.edges = static_cast<Edges>(edges);
  if (it.importance == before.importance && it.treatment == before.treatment && it.edges == before.edges) return;

  bool kept_observed = false;
  std::string error;
  bool ok = false;
  try {
    ok = save_scene_items(scene_path_, scene_->items, &kept_observed, &error);
  } catch (const std::exception& e) {
    error = std::string("cannot save the scene file: ") + e.what();
  }
  if (!ok) {
    it = before;
    show_item_editors();
    MessageBoxW(hwnd_, (widen(error, CP_UTF8) + L"\n\nThe change was not made.").c_str(), kAppTitle,
                MB_OK | MB_ICONWARNING);
    return;
  }
  set_item_row(i);
  std::wstring text = L"Saved " + file_name_of(widen(scene_path_, CP_UTF8)) + L".";
  if (kept_observed)
    text += L"\nThe file as it was is kept as " + file_name_of(widen(observed_copy_path(scene_path_), CP_UTF8)) + L".";
  set_status(text);
}

// The items tinted over the photo, for the screen only. With an item
// selected, it and its parts keep the photo's light and the rest is dimmed.
void App::build_scene_frame() {
  scene_frame_.reset();
  if (!scene_ || scene_->empty() || !photo_frame_ || photo_frame_->w <= 0 || photo_frame_->h <= 0) return;
  auto frame = std::make_unique<Frame>(*photo_frame_);
  const Plane<uint8_t>& layer = scene_->item;
  const int w = frame->w, h = frame->h;

  uint8_t tint[256][3] = {};
  bool chosen[256] = {};
  const int selected = selected_item();
  const int selected_id = selected >= 0 ? scene_->items[static_cast<size_t>(selected)].id : 0;
  for (size_t i = 0; i < scene_->items.size(); ++i) {
    const SceneItem& it = scene_->items[i];
    const COLORREF c = item_color(static_cast<int>(i));
    tint[it.id][0] = GetBValue(c);
    tint[it.id][1] = GetGValue(c);
    tint[it.id][2] = GetRValue(c);
    // An item is brought out with the whole it is a part of.
    int steps = 0;
    for (const SceneItem* p = &it; p && steps < 256; p = scene_->find(p->parent), ++steps)
      if (p->id == selected_id) chosen[it.id] = true;
  }

  std::vector<int> column(static_cast<size_t>(w));
  for (int x = 0; x < w; ++x) column[static_cast<size_t>(x)] = std::min(layer.w - 1, static_cast<int>(static_cast<long long>(x) * layer.w / w));
  auto row_of = [&](int y) { return std::min(layer.h - 1, static_cast<int>(static_cast<long long>(y) * layer.h / h)); };
  // Outlines are drawn thick enough to survive the reduction to the window.
  const int thick = std::max(1, std::max(w, h) / 500);
  for (int y = 0; y < h; ++y) {
    const int ly = row_of(y), ly_below = row_of(std::min(h - 1, y + thick));
    uint8_t* px_row = &frame->bgr[static_cast<size_t>(y) * frame->stride];
    for (int x = 0; x < w; ++x) {
      const int lx = column[static_cast<size_t>(x)], lx_right = column[static_cast<size_t>(std::min(w - 1, x + thick))];
      const int id = layer(lx, ly);
      const int right = layer(lx_right, ly), below = layer(lx, ly_below);
      uint8_t* p = px_row + 3 * x;
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
  scene_frame_ = std::move(frame);
}

void App::save_artwork() {
  if (busy() || !artwork_) return;
  wchar_t path[2048] = L"";
  std::wcsncpy(path, art_name_.c_str(), std::size(path) - 1);
  path[std::size(path) - 1] = L'\0';

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwnd_;
  ofn.lpstrFilter = L"PNG image (*.png)\0*.png\0JPEG image (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
                    L"Bitmap (*.bmp)\0*.bmp\0";
  ofn.lpstrFile = path;
  ofn.nMaxFile = static_cast<DWORD>(std::size(path));
  ofn.lpstrInitialDir = art_dir_.empty() ? nullptr : art_dir_.c_str();
  ofn.lpstrTitle = L"Save artwork";
  ofn.lpstrDefExt = L"png";
  ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn)) return;

  // The writer picks the format from the extension, so make sure there is a
  // supported one; otherwise follow the selected file type.
  std::wstring out = path;
  const std::wstring ext = extension_of(out);
  if (ext != L"png" && ext != L"jpg" && ext != L"jpeg" && ext != L"bmp")
    out += ofn.nFilterIndex == 2 ? L".jpg" : (ofn.nFilterIndex == 3 ? L".bmp" : L".png");

  std::string error;
  bool ok = false;
  HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, kCursorWait));
  try {
    ok = save_image(to_utf8(out), artwork_->image, &error);
  } catch (const std::exception& e) {
    error = std::string("cannot save the artwork: ") + e.what();
  }
  SetCursor(old_cursor);

  if (!ok) {
    if (error.empty()) error = "cannot save the artwork";
    MessageBoxW(hwnd_, widen(error, CP_UTF8).c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
    return;
  }
  art_dir_ = directory_of(out);
  set_status(L"Saved " + file_name_of(out));
}

// ---------------------------------------------------------------------------
// Painting

void App::start() {
  if (busy() || !photo_) return;

  // What is painted: the photo, or with a manner chosen the subject imagined
  // from it, which the engine paints like any photo (spec 2.14).
  std::shared_ptr<const Image> subject = photo_;
  std::shared_ptr<const Scene> subject_scene = scene_;
  const int manner = manner_index();
  if (manner >= 0) {
    if (!subject_ || subject_stale_) {
      const wchar_t* why = subject_ ? L"What the manner changes was edited after the imagined subject was made."
                                    : L"The imagined subject for this manner has not been made yet.";
      MessageBoxW(hwnd_, (std::wstring(why) + L"\n\nPress Imagine first, or choose \"As the photo is\".").c_str(),
                  kAppTitle, MB_OK | MB_ICONINFORMATION);
      return;
    }
    subject = subject_;
    subject_scene = subject_scene_;
  }

  auto selection = [](HWND box, int count, int fallback) {
    int i = static_cast<int>(SendMessageW(box, CB_GETCURSEL, 0, 0));
    return (i >= 0 && i < count) ? i : fallback;
  };

  Settings settings;
  const int medium = selection(medium_, static_cast<int>(media_.size()), 0);
  if (!media_.empty()) settings.medium = media_[static_cast<size_t>(medium)];
  const int surface =selection(surface_, static_cast<int>(surfaces_.size()) + 1, 0);
  settings.surface = surface == 0 ? std::string() : surfaces_[static_cast<size_t>(surface - 1)];
  settings.long_side_px = kSizes[selection(size_, static_cast<int>(std::size(kSizes)), kDefaultSizeIndex)];
  settings.effort = kEfforts[selection(effort_, static_cast<int>(std::size(kEfforts)), kDefaultEffortIndex)].value;
  // Each medium's own setting is passed whatever the medium is; the engine
  // uses the one that applies.
  settings.techniques.oil_blend_sky = SendMessageW(sky_blend_, BM_GETCHECK, 0, 0) == BST_CHECKED;
  const int key_pct = kKeyMinPct + kKeyStepPct * static_cast<int>(SendMessageW(key_, TBM_GETPOS, 0, 0));
  settings.watercolor_key = static_cast<float>(key_pct) / 100.f;

  wchar_t seed_text[32] = L"";
  GetWindowTextW(seed_, seed_text, static_cast<int>(std::size(seed_text)));
  wchar_t* end = nullptr;
  const unsigned long seed = std::wcstoul(seed_text, &end, 10);  // saturates at the 32-bit maximum
  if (end == seed_text) {
    settings.seed = 1;
    SetWindowTextW(seed_, L"1");
  } else {
    settings.seed = static_cast<uint32_t>(seed);
  }

  // The previous artwork is discarded here, not when a photo is loaded.
  artwork_.reset();
  art_frame_.reset();
  art_dir_ = directory_of(photo_path_);
  art_manner_ = manner >= 0 ? widen(kManners[manner].id, CP_UTF8) : std::wstring();
  art_name_ = stem_of(photo_path_) + (art_manner_.empty() ? L"" : L"_" + art_manner_) + L"_" +
              widen(settings.medium, CP_UTF8) + L".png";

  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.has_progress = false;
    mailbox_.frame.reset();
    mailbox_.done = false;
    mailbox_.artwork.reset();
    mailbox_.error.clear();
  }
  stop_.store(false);
  working_ = true;
  update_view_choices();
  set_view(View::Artwork);
  SendMessageW(progress_, PBM_SETPOS, 0, 0);
  set_status(L"Preparing...");
  update_controls();
  SetFocus(start_);
  invalidate_preview();

  // The scene is used unless it is switched off, as with --no-scene.
  std::shared_ptr<const Scene> scene;
  if (SendMessageW(use_scene_, BM_GETCHECK, 0, 0) == BST_CHECKED) scene = subject_scene;

  try {
    // The worker gets its own references to the subject and the scene, and a
    // copy of the settings.
    worker_ = std::thread([this, subject, scene, settings] { run_worker(subject, scene, settings); });
  } catch (const std::exception&) {
    working_ = false;
    update_controls();
    set_status(L"Could not start painting.");
    MessageBoxW(hwnd_, L"Could not start the painting thread.", kAppTitle, MB_OK | MB_ICONERROR);
    return;
  }
  SetTimer(hwnd_, kPollTimer, kPollMs, nullptr);
}

void App::request_stop() {
  if (!working_) return;
  stop_.store(true);
  EnableWindow(start_, FALSE);  // re-enabled as "Start" once the worker has stopped
  set_status(L"Stopping...");
}

// Worker thread. Touches only its arguments, stop_, notify_pending_, mailbox_
// and the (immutable) window handle.
void App::run_worker(std::shared_ptr<const Image> photo, std::shared_ptr<const Scene> scene, Settings settings) {
  std::unique_ptr<Artwork> art;
  std::unique_ptr<Frame> final_frame;
  std::string error;
  try {
    Clock::time_point next_frame = Clock::now();
    Clock::time_point last_post{};
    std::string last_stage;

    Hooks hooks;
    hooks.on_progress = [&](const Progress& p, const Canvas& canvas) {
      // After a stop request nothing more is reported; the UI keeps the last
      // progress it saw and the final image arrives with the Artwork.
      if (stop_.load()) return false;

      const Clock::time_point now = Clock::now();
      std::unique_ptr<Frame> frame;
      // Nobody can see a frame while the window is minimized, so do not pay
      // for one. IsIconic is safe to call from any thread.
      if (now >= next_frame && !IsIconic(hwnd_)) {
        frame = make_frame(canvas.render());
        // Wait at least four times as long as the frame took, so rendering
        // stays at or below a quarter of the worker's time however large the
        // canvas is.
        const Clock::duration cost = Clock::now() - now;
        next_frame = now + std::max(kFrameInterval, cost * 4);
      }

      if (frame || p.stage != last_stage || now - last_post >= kProgressInterval) {
        last_stage = p.stage;
        last_post = now;
        {
          std::lock_guard<std::mutex> lock(mailbox_.mutex);
          mailbox_.progress = p;
          mailbox_.has_progress = true;
          if (frame) mailbox_.frame = std::move(frame);
        }
        notify_ui();
      }
      return !stop_.load();
    };

    art = std::make_unique<Artwork>(create_artwork(*photo, settings, hooks, scene.get()));
    if (!art->image.empty()) final_frame = make_frame(art->image);
  } catch (const std::exception& e) {
    error = e.what();
    if (error.empty()) error = "unknown error";
    art.reset();
    final_frame.reset();
  } catch (...) {
    error = "unknown error";
    art.reset();
    final_frame.reset();
  }

  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.done = true;
    mailbox_.artwork = std::move(art);
    mailbox_.error = std::move(error);
    if (final_frame) mailbox_.frame = std::move(final_frame);
  }
  notify_ui();
}

// Worker thread. Posts a wake-up unless one is already waiting to be handled.
void App::notify_ui() {
  if (notify_pending_.exchange(true)) return;
  if (!PostMessageW(hwnd_, kMsgWorker, 0, 0)) notify_pending_.store(false);  // the poll timer picks it up
}

// UI thread: takes whatever the worker has left in the Mailbox.
void App::drain_mailbox() {
  bool has_progress = false, done = false;
  Progress progress;
  std::unique_ptr<Frame> frame;
  std::unique_ptr<Artwork> art;
  std::string error;
  bool has_tool_line = false, tool_done = false;
  std::string tool_line, tool_tail;
  ToolResult tool_result;
  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    // Cleared under the lock: anything the worker writes from here on finds
    // the flag clear and posts a new wake-up, so no update is ever missed.
    notify_pending_.store(false);
    has_progress = std::exchange(mailbox_.has_progress, false);
    if (has_progress) progress = mailbox_.progress;
    frame = std::move(mailbox_.frame);
    done = std::exchange(mailbox_.done, false);
    art = std::move(mailbox_.artwork);
    error = std::move(mailbox_.error);
    mailbox_.error.clear();
    has_tool_line = std::exchange(mailbox_.has_tool_line, false);
    if (has_tool_line) tool_line = mailbox_.tool_line;
    tool_done = std::exchange(mailbox_.tool_done, false);
    if (tool_done) {
      tool_result = mailbox_.tool_result;
      tool_tail = std::move(mailbox_.tool_tail);
      mailbox_.tool_tail.clear();
    }
  }

  if (has_tool_line) tool_line_ = widen(tool_line, CP_UTF8);
  if (tool_ != Tool::None && !tool_done) show_tool_status();
  if (tool_done) finish_tool(tool_result, tool_tail);

  if (frame && frame->w > 0) {
    art_frame_ = std::move(frame);
    if (view() == View::Artwork) invalidate_preview();
  }
  if (has_progress && working_ && !stop_.load()) show_progress(progress);
  if (done) finish(std::move(art), error);
}

void App::finish(std::unique_ptr<Artwork> art, const std::string& error) {
  if (worker_.joinable()) worker_.join();  // the worker has already left its last statement
  KillTimer(hwnd_, kPollTimer);
  working_ = false;

  const bool painted = art && !art->image.empty();
  if (painted) {
    artwork_ = std::move(art);
    wchar_t seconds[32];
    std::swprintf(seconds, std::size(seconds), L"%.1f", artwork_->seconds);
    std::wstring text = artwork_->cancelled ? L"Stopped: " : L"Finished: ";
    text += std::to_wstring(artwork_->strokes) + L" strokes in " + seconds + L" s\n";
    text += pretty_name(artwork_->medium, true) + L" on " + pretty_name(artwork_->surface, false) + L", " +
            std::to_wstring(artwork_->width) + L" x " + std::to_wstring(artwork_->height) + L" px";
    if (!artwork_->cancelled) SendMessageW(progress_, PBM_SETPOS, kProgressRange, 0);
    set_status(text);
  } else {
    art_frame_.reset();
    SendMessageW(progress_, PBM_SETPOS, 0, 0);
    set_status(L"Nothing was painted.");
  }
  update_view_choices();
  update_controls();
  if (!GetFocus() && GetForegroundWindow() == hwnd_) SetFocus(start_);
  invalidate_preview();

  if (!painted && !error.empty()) {
    const std::wstring message = L"Painting failed: " + widen(error, CP_UTF8);
    MessageBoxW(hwnd_, message.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
  }
}

// ---------------------------------------------------------------------------
// The steps outside the engine

// Writes the photo's scene file by running the observe step (spec 2.13).
void App::start_observe() {
  if (busy() || !photo_) return;
  ToolsConfig config;
  std::wstring problem;
  if (!read_tools_config(config, problem)) {
    MessageBoxW(hwnd_, problem.c_str(), kAppTitle, MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (file_exists(scene_path_for(to_utf8(photo_path_)))) {
    const int answer = MessageBoxW(hwnd_,
                                   L"This photo has a scene file. Observing it again writes a new one in its place; "
                                   L"corrections made to it are lost.\n\nObserve the photo again?",
                                   kAppTitle, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer != IDYES) return;
  }
  const std::wstring folder = config.tools + L"\\observe";
  begin_tool(Tool::Observe, {{quote_argument(config.python) + L" -u " + quote_argument(folder + L"\\observe.py") + L" " +
                                  quote_argument(photo_path_),
                              folder}});
}

// Prepares the imagined subject of the chosen manner (spec 2.14): the model
// chooses a mechanism per item, unless a choice file is there already, and
// the choice is carried out on the photo.
void App::start_imagine(bool ask_again) {
  const int manner = manner_index();
  if (busy() || !photo_ || manner < 0) return;
  ToolsConfig config;
  std::wstring problem;
  if (!read_tools_config(config, problem)) {
    MessageBoxW(hwnd_, problem.c_str(), kAppTitle, MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (!scene_) {
    MessageBoxW(hwnd_, L"A manner changes things because of what they are, so it needs the photo's scene.\n\n"
                       L"Press \"Observe photo\" first.",
                kAppTitle, MB_OK | MB_ICONINFORMATION);
    return;
  }
  const bool have_choice = choice_.is_object();
  if (ask_again && have_choice) {
    const int answer = MessageBoxW(hwnd_,
                                   L"The model chooses again what this manner changes. The choice shown now, with "
                                   L"anything you changed in it, is replaced.\n\nAsk the model again?",
                                   kAppTitle, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (answer != IDYES) return;
  }

  const std::wstring folder = config.tools + L"\\interpret";
  const std::wstring out = interpret_dir();
  const std::wstring python = quote_argument(config.python) + L" -u ";
  std::vector<ToolStep> steps;
  if (ask_again || !have_choice)
    steps.push_back({python + quote_argument(folder + L"\\choose.py") + L" --out " + quote_argument(out + L"\\choices") +
                         L" --manners " + widen(kManners[manner].id, CP_UTF8) + L" " + quote_argument(photo_path_),
                     folder});
  steps.push_back({python + quote_argument(folder + L"\\imagine.py") + L" --choice " +
                       quote_argument(choice_file(manner)) + L" --out " + quote_argument(out + L"\\subjects") + L" " +
                       quote_argument(photo_path_),
                   folder});
  begin_tool(Tool::Imagine, std::move(steps));
}

void App::begin_tool(Tool which, std::vector<ToolStep> steps) {
  // The steps print as they go, in UTF-8.
  SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
  SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.has_tool_line = false;
    mailbox_.tool_line.clear();
    mailbox_.tool_done = false;
    mailbox_.tool_tail.clear();
  }
  tool_stop_.store(false);
  tool_ = which;
  tool_started_ = Clock::now();
  tool_line_.clear();
  // The step does not say how far it is; the bar only shows that it runs.
  SetWindowLongW(progress_, GWL_STYLE, GetWindowLongW(progress_, GWL_STYLE) | PBS_MARQUEE);
  SendMessageW(progress_, PBM_SETMARQUEE, TRUE, 30);
  show_tool_status();
  update_controls();

  try {
    tool_thread_ = std::thread([this, steps = std::move(steps)]() mutable { run_tool_worker(std::move(steps)); });
  } catch (const std::exception&) {
    ToolResult failed;
    failed.error = "cannot start a thread";
    finish_tool(failed, std::string());
    return;
  }
  SetTimer(hwnd_, kPollTimer, kPollMs, nullptr);
}

void App::request_tool_stop() {
  if (tool_ == Tool::None) return;
  tool_stop_.store(true);
  EnableWindow(tool_ == Tool::Observe ? observe_ : imagine_, FALSE);
  set_status(L"Stopping...");
}

void App::show_tool_status() {
  if (tool_ == Tool::None || tool_stop_.load()) return;
  const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - tool_started_).count();
  std::wstring text = tool_ == Tool::Observe ? L"Observing the photo" : L"Imagining the subject";
  text += L" - " + std::to_wstring(seconds) + L" s";
  if (!tool_line_.empty()) text += L"\n" + tool_line_;
  set_status(text);
}

// Tool thread. Touches only its argument, tool_stop_, notify_pending_,
// mailbox_ and the (immutable) window handle.
void App::run_tool_worker(std::vector<ToolStep> steps) {
  ToolResult result;
  std::deque<std::string> tail;
  try {
    for (const ToolStep& step : steps) {
      result = run_tool(
          step.command, step.directory,
          [&](const std::string& line) {
            tail.push_back(line);
            if (tail.size() > 12) tail.pop_front();
            {
              std::lock_guard<std::mutex> lock(mailbox_.mutex);
              mailbox_.tool_line = line;
              mailbox_.has_tool_line = true;
            }
            notify_ui();
          },
          tool_stop_);
      if (!result.ok()) break;
    }
  } catch (const std::exception& e) {
    result = ToolResult{};
    result.error = e.what();
  }
  std::string text;
  for (const std::string& line : tail) text += line + "\n";
  {
    std::lock_guard<std::mutex> lock(mailbox_.mutex);
    mailbox_.tool_done = true;
    mailbox_.tool_result = result;
    mailbox_.tool_tail = std::move(text);
  }
  notify_ui();
}

void App::finish_tool(const ToolResult& result, const std::string& tail) {
  if (tool_thread_.joinable()) tool_thread_.join();  // it has already left its last statement
  if (!working_) KillTimer(hwnd_, kPollTimer);
  const Tool which = tool_;
  tool_ = Tool::None;
  SendMessageW(progress_, PBM_SETMARQUEE, FALSE, 0);
  SetWindowLongW(progress_, GWL_STYLE, GetWindowLongW(progress_, GWL_STYLE) & ~PBS_MARQUEE);
  SendMessageW(progress_, PBM_SETPOS, 0, 0);
  const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - tool_started_).count();

  // Whatever the step wrote is read again, also after a failure: the files
  // say what there is.
  if (which == Tool::Observe) {
    if (result.ok()) {
      // The new file is the one as observed; a copy kept of an older one
      // would mislead.
      std::error_code ec;
      std::filesystem::remove(std::filesystem::path(widen(observed_copy_path(scene_path_for(to_utf8(photo_path_))), CP_UTF8)),
                              ec);
    }
    if (photo_) load_scene_for(photo_path_, photo_->w, photo_->h);
  }
  load_interpretation();
  update_view_choices();

  if (result.ok()) {
    if (which == Tool::Observe) {
      set_view(scene_ ? View::Scene : View::Photo);
      set_status(scene_ ? L"Observed in " + std::to_wstring(seconds) + L" s: " + std::to_wstring(scene_->items.size()) +
                              L" items."
                        : L"Observed, but no scene file was written.");
    } else {
      set_view(subject_frame_ ? View::Subject : View::Photo);
      std::wstring text = L"Imagined in " + std::to_wstring(seconds) + L" s.";
      if (!tool_line_.empty()) text += L"\n" + tool_line_;
      set_status(text);
    }
  } else if (result.stopped) {
    set_status(L"Stopped.");
  } else {
    set_status(which == Tool::Observe ? L"Observing failed." : L"Imagining failed.");
    std::wstring message = which == Tool::Observe ? L"The observe step failed." : L"The interpret step failed.";
    if (!result.error.empty()) message += L"\n\n" + widen(result.error, CP_UTF8);
    else if (!tail.empty()) message += L"\n\nIts last lines:\n" + widen(tail, CP_UTF8);
    MessageBoxW(hwnd_, message.c_str(), kAppTitle, MB_OK | MB_ICONWARNING);
  }
  update_controls();
  invalidate_preview();
}

// ---------------------------------------------------------------------------
// The interpretation

int App::manner_index() const {
  const int i = static_cast<int>(SendMessageW(manner_, CB_GETCURSEL, 0, 0)) - 1;  // entry 0 is the photo as it is
  return (i >= 0 && i < static_cast<int>(std::size(kManners))) ? i : -1;
}

// The files of the interpret step are kept beside the photo, in a folder of
// their own.
std::wstring App::interpret_dir() const {
  return directory_of(photo_path_) + L"\\" + stem_of(photo_path_) + L".interpret";
}

std::wstring App::choice_file(int manner) const {
  return interpret_dir() + L"\\choices\\" + stem_of(photo_path_) + L"." + widen(kManners[manner].id, CP_UTF8) +
         L".choice.json";
}

std::wstring App::subject_file(int manner) const {
  return interpret_dir() + L"\\subjects\\" + stem_of(photo_path_) + L"_" + widen(kManners[manner].id, CP_UTF8) + L".png";
}

// Reads what there is for the chosen manner: the choice and the imagined
// subject with its scene. Nothing is made here.
void App::load_interpretation() {
  choice_ = Json{};
  choice_path_.clear();
  subject_.reset();
  subject_scene_.reset();
  subject_frame_.reset();
  subject_stale_ = false;

  const int manner = manner_index();
  if (manner >= 0 && photo_) {
    const std::wstring choice_path = choice_file(manner), subject_path = subject_file(manner);
    std::string text;
    Json doc;
    if (read_text(choice_path, text) && parse_json(text, doc) && doc.is_object() && doc.get("picks") &&
        doc.get("picks")->is_array()) {
      choice_ = std::move(doc);
      choice_path_ = choice_path;
    }
    if (is_file(subject_path)) {
      HCURSOR old_cursor = SetCursor(LoadCursorW(nullptr, kCursorWait));
      try {
        std::string error;
        Image img = load_image(to_utf8(subject_path), &error);
        if (!img.empty()) {
          const int long_side = std::max(img.w, img.h);
          if (long_side > kMaxPreviewSide) {
            const float s = static_cast<float>(kMaxPreviewSide) / static_cast<float>(long_side);
            subject_frame_ = make_frame(resize_area(img, std::max(1, static_cast<int>(std::lround(img.w * s))),
                                                    std::max(1, static_cast<int>(std::lround(img.h * s)))));
          } else {
            subject_frame_ = make_frame(img);
          }
          // The subject's own scene file, written with it: the scene with
          // the items where the manner put them.
          const std::string scene_path = scene_path_for(to_utf8(subject_path));
          if (file_exists(scene_path)) {
            Scene loaded = load_scene(scene_path, &error);
            if (!loaded.empty() && loaded.fits(img.w, img.h)) subject_scene_ = std::make_shared<Scene>(std::move(loaded));
          }
          subject_ = std::make_shared<const Image>(std::move(img));
        }
      } catch (const std::exception&) {
        subject_.reset();
        subject_scene_.reset();
        subject_frame_.reset();
      }
      SetCursor(old_cursor);
      // A choice edited after the subject was made has not been carried out.
      subject_stale_ = subject_ && !choice_path_.empty() && written_after(choice_path_, subject_path);
    }
  }

  if (scene_) {
    filling_ = true;
    for (int row = 0; row < static_cast<int>(scene_->items.size()); ++row) set_item_row(row);
    filling_ = false;
  }
  show_item_editors();
  update_view_choices();
  update_controls();
}

// The entry of the choice file for an item, or null.
Json* App::pick_for(int item_id) {
  if (!choice_.is_object()) return nullptr;
  Json& picks = choice_.member("picks");
  if (!picks.is_array()) return nullptr;
  for (Json& pick : picks.array) {
    const Json* id = pick.get("id");
    if (pick.is_object() && id && id->is_number() && id->number == static_cast<double>(item_id)) return &pick;
  }
  return nullptr;
}

// What the manner does to an item, for the list: empty if it is left alone.
std::wstring App::change_text(int item_id) {
  Json* pick = manner_index() >= 0 ? pick_for(item_id) : nullptr;
  if (!pick) return {};
  const Json* mechanism = pick->get("mechanism");
  const Json* strength = pick->get("strength");
  if (!mechanism || !mechanism->is_string() || mechanism->string == kAsItIs) return {};
  std::wstring text = pretty_name(mechanism->string, true);
  if (strength && strength->is_number()) text += L" " + std::to_wstring(static_cast<int>(strength->number));
  return text;
}

// The mechanism or the strength of the selected item was changed: the choice
// file is saved, marked as edited by hand, and the imagined subject is from
// then on out of date.
void App::apply_pick_edit() {
  const int i = selected_item(), manner = manner_index();
  if (filling_ || busy() || i < 0 || manner < 0) return;
  Json* pick = pick_for(scene_->items[static_cast<size_t>(i)].id);
  if (!pick) return;
  const int m = static_cast<int>(SendMessageW(mechanism_, CB_GETCURSEL, 0, 0));
  const int s = static_cast<int>(SendMessageW(strength_, CB_GETCURSEL, 0, 0));
  const std::vector<const char*>& names = kManners[manner].mechanisms;
  if (m < 0 || m > static_cast<int>(names.size()) || s < 0 || s > 2) return;
  const Json before = choice_;
  pick->member("mechanism") = Json::of(std::string(m == 0 ? kAsItIs : names[static_cast<size_t>(m - 1)]));
  pick->member("strength") = Json::of(static_cast<double>(s + 1));
  choice_.member("edited_by_hand") = Json::of(true);
  if (!write_text(choice_path_, write_json(choice_))) {
    choice_ = before;
    show_item_editors();
    MessageBoxW(hwnd_, (L"The choice file cannot be written:\n" + choice_path_ + L"\n\nThe change was not made.").c_str(),
                kAppTitle, MB_OK | MB_ICONWARNING);
    return;
  }
  subject_stale_ = subject_ != nullptr;
  set_item_row(i);
  set_status(L"Saved the choice.\nPress Imagine to carry it out.");
  update_controls();
}

void App::show_progress(const Progress& p) {
  const float fraction = std::clamp(p.fraction, 0.f, 1.f);
  SendMessageW(progress_, PBM_SETPOS, static_cast<WPARAM>(std::lround(fraction * kProgressRange)), 0);
  const int percent = static_cast<int>(fraction * 100.f);
  set_status(pretty_name(p.stage, true) + L"\n" + std::to_wstring(percent) + L"% - " + std::to_wstring(p.strokes) +
             L" strokes");
}

void App::set_status(const std::wstring& text) {
  if (text == status_text_) return;  // avoid needless repaints of the label
  status_text_ = text;
  SetWindowTextW(status_, text.c_str());
}

void App::update_description() {
  const int i = static_cast<int>(SendMessageW(medium_, CB_GETCURSEL, 0, 0));
  std::wstring text;
  if (i >= 0 && i < static_cast<int>(media_.size()))
    text = widen(describe_medium(media_[static_cast<size_t>(i)]), CP_UTF8);
  SetWindowTextW(description_, text.c_str());
}

// Shows the one setting the chosen medium has, in the row kept for it.
void App::update_medium_options() {
  const int i = static_cast<int>(SendMessageW(medium_, CB_GETCURSEL, 0, 0));
  const std::string medium = (i >= 0 && i < static_cast<int>(media_.size())) ? media_[static_cast<size_t>(i)] : "";
  ShowWindow(sky_blend_, medium == "oil" ? SW_SHOW : SW_HIDE);
  for (HWND h : {key_label_, key_, key_value_}) ShowWindow(h, medium == "watercolor" ? SW_SHOW : SW_HIDE);
  update_tip_rects();  // the two share a row; only the one shown answers there
}

void App::update_key_text() {
  const int pct = kKeyMinPct + kKeyStepPct * static_cast<int>(SendMessageW(key_, TBM_GETPOS, 0, 0));
  wchar_t text[16];
  std::swprintf(text, std::size(text), L"%.2f", pct / 100.0);
  SetWindowTextW(key_value_, text);
}

void App::update_controls() {
  // Idle: neither painting nor running a step outside the engine.
  const BOOL idle = busy() ? FALSE : TRUE;
  for (HWND h : {open_, medium_, surface_, size_, effort_, seed_, sky_blend_, key_}) EnableWindow(h, idle);
  SetWindowTextW(start_, working_ ? L"Stop" : L"Start");
  EnableWindow(start_, (working_ || (photo_ && idle)) ? TRUE : FALSE);
  EnableWindow(save_, (idle && artwork_) ? TRUE : FALSE);
  EnableWindow(view_, photo_frame_ ? TRUE : FALSE);
  // The scene is input to a run, so it does not change during one. The list
  // stays open for looking.
  EnableWindow(use_scene_, (idle && scene_) ? TRUE : FALSE);
  EnableWindow(items_, scene_ ? TRUE : FALSE);
  const BOOL editing = (idle && selected_item() >= 0) ? TRUE : FALSE;
  for (HWND h : {importance_, treatment_, edges_}) EnableWindow(h, editing);

  // A step that is running is stopped with its own button.
  SetWindowTextW(observe_, tool_ == Tool::Observe ? L"Stop" : (scene_ ? L"Observe again" : L"Observe photo"));
  EnableWindow(observe_, (tool_ == Tool::Observe || (photo_ && idle)) ? TRUE : FALSE);
  const bool manner = manner_index() >= 0;
  EnableWindow(manner_, (photo_ && idle) ? TRUE : FALSE);
  SetWindowTextW(imagine_, tool_ == Tool::Imagine ? L"Stop" : ((subject_ && !subject_stale_) ? L"Imagine again" : L"Imagine"));
  EnableWindow(imagine_, (tool_ == Tool::Imagine || (photo_ && idle && manner)) ? TRUE : FALSE);
  EnableWindow(ask_again_, (photo_ && idle && manner && choice_.is_object()) ? TRUE : FALSE);
  const BOOL picking = (editing && manner && choice_.is_object()) ? TRUE : FALSE;
  for (HWND h : {mechanism_, strength_}) EnableWindow(h, picking);
}

// ---------------------------------------------------------------------------
// Preview

View App::view() const {
  const int i = static_cast<int>(SendMessageW(view_, CB_GETCURSEL, 0, 0));
  if (i < 0) return View::Photo;
  return static_cast<View>(SendMessageW(view_, CB_GETITEMDATA, static_cast<WPARAM>(i), 0));
}

// Chooses a view if it is on offer; otherwise the choice stays.
void App::set_view(View v) {
  const int n = static_cast<int>(SendMessageW(view_, CB_GETCOUNT, 0, 0));
  for (int i = 0; i < n; ++i) {
    if (static_cast<View>(SendMessageW(view_, CB_GETITEMDATA, static_cast<WPARAM>(i), 0)) != v) continue;
    SendMessageW(view_, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
    break;
  }
  invalidate_preview();
}

// Offers the views there is something to show for, keeping the current one
// if it is still among them.
void App::update_view_choices() {
  const View current = view();
  SendMessageW(view_, CB_RESETCONTENT, 0, 0);
  auto offer = [&](const wchar_t* text, View v) {
    const LRESULT i = SendMessageW(view_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    if (i >= 0) SendMessageW(view_, CB_SETITEMDATA, static_cast<WPARAM>(i), static_cast<LPARAM>(v));
  };
  if (photo_frame_) offer(L"Photo", View::Photo);
  if (photo_frame_ && scene_) offer(L"Scene", View::Scene);
  if (subject_frame_) offer(L"Imagined subject", View::Subject);
  if (working_ || art_frame_) offer(L"Artwork", View::Artwork);
  if (SendMessageW(view_, CB_GETCOUNT, 0, 0) > 0) SendMessageW(view_, CB_SETCURSEL, 0, 0);
  set_view(current);
}

const Frame* App::shown_frame() const {
  switch (view()) {
    case View::Scene:
      if (scene_frame_) return scene_frame_.get();
      break;
    case View::Subject:
      if (subject_frame_) return subject_frame_.get();
      break;
    case View::Artwork:
      if (art_frame_) return art_frame_.get();
      if (working_) return nullptr;  // the first frame of a new artwork is on its way
      break;
    default:
      break;
  }
  return photo_frame_.get();
}

void App::invalidate_preview() {
  if (view() == View::Scene && !scene_frame_) build_scene_frame();
  back_dirty_ = true;
  if (preview_) InvalidateRect(preview_, nullptr, FALSE);
}

void App::paint_preview(HWND hwnd) {
  PAINTSTRUCT ps{};
  HDC dc = BeginPaint(hwnd, &ps);
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int cw = rc.right, ch = rc.bottom;
  if (dc && cw > 0 && ch > 0) {
    if (!back_bmp_ || back_w_ != cw || back_h_ != ch) {
      if (back_bmp_) DeleteObject(back_bmp_);
      back_bmp_ = CreateCompatibleBitmap(dc, cw, ch);
      back_w_ = cw;
      back_h_ = ch;
      back_dirty_ = true;
    }
    HDC mem = back_bmp_ ? CreateCompatibleDC(dc) : nullptr;
    if (mem) {
      HGDIOBJ old = SelectObject(mem, back_bmp_);
      if (back_dirty_) {
        draw_preview(mem, cw, ch);
        back_dirty_ = false;
      }
      BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteDC(mem);
    } else {
      draw_preview(dc, cw, ch);  // out of GDI memory: draw directly
    }
  }
  EndPaint(hwnd, &ps);
}

void App::draw_preview(HDC dc, int cw, int ch) const {
  RECT all{0, 0, cw, ch};
  SetDCBrushColor(dc, kPreviewBackground);
  FillRect(dc, &all, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

  shown_rect_ = RECT{};
  const Frame* frame = shown_frame();
  if (!frame || frame->w <= 0 || frame->h <= 0) {
    const wchar_t* hint = working_ ? L"Preparing..." : L"Open a photo, or drop one here";
    HGDIOBJ old_font = font_ ? SelectObject(dc, font_) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kPreviewHintText);
    DrawTextW(dc, hint, -1, &all, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old_font) SelectObject(dc, old_font);
    return;
  }

  // Aspect-fit inside a small margin, centered. The photo and the artwork
  // share an aspect ratio, so toggling between them keeps the picture in place.
  const int pad = std::min(px(12), std::min(cw, ch) / 8);
  const double aw = cw - 2 * pad, ah = ch - 2 * pad;
  const double scale = std::min(aw / frame->w, ah / frame->h);
  const int dw = std::clamp(static_cast<int>(std::lround(frame->w * scale)), 1, cw);
  const int dh = std::clamp(static_cast<int>(std::lround(frame->h * scale)), 1, ch);
  const int dx = (cw - dw) / 2, dy = (ch - dh) / 2;
  shown_rect_ = RECT{dx, dy, dx + dw, dy + dh};

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
  bmi.bmiHeader.biWidth = frame->w;
  bmi.bmiHeader.biHeight = -frame->h;  // negative: rows run top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 24;
  bmi.bmiHeader.biCompression = BI_RGB;

  // HALFTONE averages source pixels when shrinking; it requires the brush
  // origin to be set afterwards.
  SetStretchBltMode(dc, HALFTONE);
  SetBrushOrgEx(dc, 0, 0, nullptr);
  StretchDIBits(dc, dx, dy, dw, dh, 0, 0, frame->w, frame->h, frame->bgr.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

}  // namespace

int run_gui(const std::string& initial_photo) {
  // The executable is a console program. If the console was created just for
  // this process (started from Explorer or a shortcut), let it go so that no
  // empty console window lingers next to the studio window. A console shared
  // with a shell is left alone.
  DWORD console_processes[2] = {};
  if (GetConsoleProcessList(console_processes, 2) == 1) FreeConsole();

  enable_dpi_awareness();  // must happen before the first window exists

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  App app;
  if (!app.create(GetModuleHandleW(nullptr))) {
    MessageBoxW(nullptr, L"Could not create the studio window.", kAppTitle, MB_OK | MB_ICONERROR);
    return 1;
  }

  // The path comes from argv, so it is in the ANSI code page.
  if (!initial_photo.empty()) app.load_photo(widen(initial_photo, CP_ACP));

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    // F1, and the help window's own keyboard handling.
    if (app.handle_help_message(msg)) continue;
    // IsDialogMessageW provides Tab / arrow-key navigation between controls.
    if (IsDialogMessageW(app.window(), &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace bs
