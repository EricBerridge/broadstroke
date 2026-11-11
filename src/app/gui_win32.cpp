// The studio window on Windows: a native Win32 front end for the engine.
//
// A fixed-width panel of controls sits on the left; the rest of the window is
// a preview that shows either the loaded photo or the artwork while it is
// being made.
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
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "artist/artist.hpp"
#include "core/image.hpp"
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
  kIdStart,
  kIdSave,
  kIdShowPhoto,
};

constexpr int kPanelWidthDip = 300;        // control panel width at 96 DPI
constexpr int kMinPreviewWidthDip = 260;
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

int dpi_of_window(HWND hwnd) {
  HMODULE user = GetModuleHandleW(L"user32.dll");
  using GetDpiFn = UINT(WINAPI*)(HWND);
  GetDpiFn get_dpi = user ? reinterpret_cast<GetDpiFn>(GetProcAddress(user, "GetDpiForWindow")) : nullptr;
  if (get_dpi) {
    UINT dpi = get_dpi(hwnd);
    if (dpi != 0) return static_cast<int>(dpi);
  }
  HDC dc = GetDC(hwnd);
  int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 0;
  if (dc) ReleaseDC(hwnd, dc);
  return dpi > 0 ? dpi : 96;
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

 private:
  static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static LRESULT CALLBACK preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  static App* from_window(HWND hwnd, UINT msg, LPARAM lp);

  LRESULT on_message(UINT msg, WPARAM wp, LPARAM lp);
  void on_command(int id, int code);
  void on_drop(HDROP drop);
  void on_close();

  HWND add_control(const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex_style, int id);
  void create_controls();
  void rebuild_font();
  void layout();
  int px(int dip) const { return MulDiv(dip, dpi_, 96); }

  void choose_photo();
  void save_artwork();
  void start();
  void request_stop();
  void finish(std::unique_ptr<Artwork> art, const std::string& error);
  void drain_mailbox();
  void show_progress(const Progress& p);
  void set_status(const std::wstring& text);
  void update_description();
  void update_controls();
  bool showing_photo() const;
  const Frame* shown_frame() const;
  void invalidate_preview();
  void paint_preview(HWND hwnd);
  void draw_preview(HDC dc, int cw, int ch) const;

  // Worker thread only.
  void run_worker(std::shared_ptr<const Image> photo, Settings settings);
  void notify_ui();

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;  // set once during creation; read-only afterwards
  HWND preview_ = nullptr;
  HWND open_ = nullptr, file_label_ = nullptr;
  HWND medium_label_ = nullptr, medium_ = nullptr, description_ = nullptr;
  HWND surface_label_ = nullptr, surface_ = nullptr;
  HWND size_label_ = nullptr, size_ = nullptr;
  HWND effort_label_ = nullptr, effort_ = nullptr;
  HWND seed_label_ = nullptr, seed_ = nullptr;
  HWND start_ = nullptr, save_ = nullptr, show_photo_ = nullptr;
  HWND progress_ = nullptr, status_ = nullptr;
  HWND last_focus_ = nullptr;
  std::vector<HWND> controls_;  // every child that takes the UI font

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
  std::unique_ptr<Frame> photo_frame_;
  std::wstring photo_path_;

  std::unique_ptr<Artwork> artwork_;   // what "Save artwork..." writes
  std::unique_ptr<Frame> art_frame_;   // latest live frame, then the final image
  std::wstring art_dir_, art_name_;    // default save location and file name
  std::wstring status_text_;

  bool working_ = false;
  std::thread worker_;
  std::atomic<bool> stop_{false};            // UI -> worker: stop at the next progress call
  std::atomic<bool> notify_pending_{false};  // a wake-up is posted and not yet consumed
  Mailbox mailbox_;
};

App::~App() {
  // Safety net; the normal paths join the worker in finish() or on_close().
  stop_.store(true);
  if (worker_.joinable()) worker_.join();
  if (back_bmp_) DeleteObject(back_bmp_);
  if (font_) DeleteObject(font_);
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

  dpi_ = dpi_of_window(hwnd_);
  create_controls();
  if (!preview_ || !start_ || !progress_) return false;
  rebuild_font();

  // Initial size in DPI-scaled units, centered on and limited to the work
  // area of the monitor the window was created on.
  int w = px(1180), h = px(800);
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

  start_ = add_control(L"BUTTON", L"Start", button, 0, kIdStart);
  save_ = add_control(L"BUTTON", L"Save artwork...", button, 0, kIdSave);
  show_photo_ = add_control(L"BUTTON", L"Show photo", BS_AUTOCHECKBOX | WS_TABSTOP, 0, kIdShowPhoto);

  progress_ = add_control(PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, 0);
  status_ = add_control(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 0);

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
}

void App::rebuild_font() {
  // The message font, at the DPI of the monitor the window is on.
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  bool ok = false;
  HMODULE user = GetModuleHandleW(L"user32.dll");
  using InfoForDpiFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
  InfoForDpiFn info_for_dpi =
      user ? reinterpret_cast<InfoForDpiFn>(GetProcAddress(user, "SystemParametersInfoForDpi")) : nullptr;
  if (info_for_dpi)
    ok = info_for_dpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, static_cast<UINT>(dpi_)) != FALSE;
  if (!ok) {
    ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) != FALSE;
    if (ok) {
      // These metrics are for the system DPI; rescale them for this window.
      HDC screen = GetDC(nullptr);
      int system_dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 0;
      if (screen) ReleaseDC(nullptr, screen);
      if (system_dpi > 0) ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, dpi_, system_dpi);
    }
  }

  HFONT old = font_;
  font_ = ok ? CreateFontIndirectW(&ncm.lfMessageFont) : nullptr;
  for (HWND h : controls_) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  if (old) DeleteObject(old);

  if (HDC dc = GetDC(hwnd_)) {
    HGDIOBJ prev = font_ ? SelectObject(dc, font_) : nullptr;
    TEXTMETRICW tm{};
    if (GetTextMetricsW(dc, &tm)) line_h_ = std::max(8, static_cast<int>(tm.tmHeight + tm.tmExternalLeading));
    if (prev) SelectObject(dc, prev);
    ReleaseDC(hwnd_, dc);
  }
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
    MoveWindow(h, margin, y, inner, height, TRUE);
    y += height + gap_after;
  };
  auto row = [&](HWND label, HWND field, int field_h) {
    MoveWindow(label, margin, y, label_w, row_h, TRUE);
    MoveWindow(field, field_x, y, field_w, field_h, TRUE);
    y += row_h + px(8);
  };

  full_width(open_, px(30), px(6));
  full_width(file_label_, line_h_ + px(2), px(14));
  row(medium_label_, medium_, drop_h);
  full_width(description_, 3 * line_h_, px(10));
  row(surface_label_, surface_, drop_h);
  row(size_label_, size_, drop_h);
  row(effort_label_, effort_, drop_h);
  row(seed_label_, seed_, row_h);
  y += px(6);
  full_width(start_, px(36), px(8));
  full_width(save_, px(30), px(12));
  full_width(show_photo_, line_h_ + px(6), px(14));
  full_width(progress_, px(16), px(8));
  full_width(status_, 3 * line_h_, margin);
  min_client_h_ = y;

  MoveWindow(preview_, panel, 0, std::max(0, static_cast<int>(rc.right) - panel), rc.bottom, TRUE);
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
      RECT r{0, 0, px(kPanelWidthDip + kMinPreviewWidthDip), std::max(min_client_h_, px(200))};
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
    case kIdMedium:
      if (code == CBN_SELCHANGE) update_description();
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
    case kIdShowPhoto:
      if (code == BN_CLICKED) invalidate_preview();
      break;
    default:
      break;
  }
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
  if (working_) {
    MessageBeep(MB_ICONWARNING);  // the subject cannot change mid-painting
    return;
  }
  SetForegroundWindow(hwnd_);
  load_photo(path);
}

void App::on_close() {
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
  if (working_) return;
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
  if (working_) return false;
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

  // An earlier artwork stays available for saving until a new one is started;
  // switch the preview to the photo so the new subject is visible.
  SendMessageW(show_photo_, BM_SETCHECK, art_frame_ ? BST_CHECKED : BST_UNCHECKED, 0);
  if (!artwork_) set_status(L"Choose a medium and press Start.");
  update_controls();
  invalidate_preview();
  return true;
}

void App::save_artwork() {
  if (working_ || !artwork_) return;
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
  if (working_ || !photo_) return;

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
  art_name_ = stem_of(photo_path_) + L"_" + widen(settings.medium, CP_UTF8) + L".png";

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
  SendMessageW(show_photo_, BM_SETCHECK, BST_UNCHECKED, 0);
  SendMessageW(progress_, PBM_SETPOS, 0, 0);
  set_status(L"Preparing...");
  update_controls();
  SetFocus(start_);
  invalidate_preview();

  try {
    // The worker gets its own reference to the photo and a copy of the settings.
    worker_ = std::thread([this, photo = photo_, settings] { run_worker(photo, settings); });
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
void App::run_worker(std::shared_ptr<const Image> photo, Settings settings) {
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

    art = std::make_unique<Artwork>(create_artwork(*photo, settings, hooks));
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
  }

  if (frame && frame->w > 0) {
    art_frame_ = std::move(frame);
    if (!showing_photo()) invalidate_preview();
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
  update_controls();
  if (!GetFocus() && GetForegroundWindow() == hwnd_) SetFocus(start_);
  invalidate_preview();

  if (!painted && !error.empty()) {
    const std::wstring message = L"Painting failed: " + widen(error, CP_UTF8);
    MessageBoxW(hwnd_, message.c_str(), kAppTitle, MB_OK | MB_ICONERROR);
  }
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

void App::update_controls() {
  const BOOL idle = working_ ? FALSE : TRUE;
  for (HWND h : {open_, medium_, surface_, size_, effort_, seed_}) EnableWindow(h, idle);
  SetWindowTextW(start_, working_ ? L"Stop" : L"Start");
  EnableWindow(start_, (working_ || photo_) ? TRUE : FALSE);
  EnableWindow(save_, (!working_ && artwork_) ? TRUE : FALSE);
  // Comparing needs both a photo and an artwork (or one in the making).
  EnableWindow(show_photo_, (photo_frame_ && (working_ || art_frame_)) ? TRUE : FALSE);
}

// ---------------------------------------------------------------------------
// Preview

bool App::showing_photo() const {
  return SendMessageW(show_photo_, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

const Frame* App::shown_frame() const {
  if (showing_photo() && photo_frame_) return photo_frame_.get();
  if (art_frame_) return art_frame_.get();
  if (working_) return nullptr;  // the first frame of a new artwork is on its way
  return photo_frame_.get();
}

void App::invalidate_preview() {
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
  icc.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
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
    // IsDialogMessageW provides Tab / arrow-key navigation between controls.
    if (IsDialogMessageW(app.window(), &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace bs
