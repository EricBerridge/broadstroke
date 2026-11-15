#include "app/help_win32.hpp"

#include <commctrl.h>
#include <richedit.h>

#include <algorithm>
#include <cwchar>
#include <string>

#include "app/win32_common.hpp"

namespace bs {
namespace {

const wchar_t kHelpClass[] = L"BroadstrokeHelpWindow";
const wchar_t kHelpTitle[] = L"Broadstroke Help";

// Child ids. IDCANCEL is what IsDialogMessageW sends for Escape.
enum : int { kIdTopics = 1001, kIdPage = 1002 };

constexpr int kTopicsWidthDip = 230;
constexpr int kMinWidthDip = 560, kMinHeightDip = 320;

// The project is not built with UNICODE; this is the wide arrow cursor.
const wchar_t* const kCursorArrow = MAKEINTRESOURCEW(32512);

// Sizes of the page's text, in twentieths of a point.
constexpr LONG kBodySize = 210, kCodeSize = 200, kHeadingSize = 250, kTitleSize = 330;

enum class Style { Title, Heading, Body, Item };

std::wstring widen_ascii(const std::string& s) {
  return std::wstring(s.begin(), s.end());  // the pages are plain ASCII
}

void move_to_end(HWND edit) {
  const int n = GetWindowTextLengthW(edit);  // at least the control's own count, which it clips to
  SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(n), static_cast<LPARAM>(n));
}

}  // namespace

HelpWindow::~HelpWindow() {
  if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
  if (font_) DeleteObject(font_);
}

LRESULT CALLBACK HelpWindow::window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    static_cast<HelpWindow*>(cs->lpCreateParams)->hwnd_ = hwnd;
  }
  HelpWindow* self = reinterpret_cast<HelpWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self ? self->on_message(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

bool HelpWindow::create(HWND owner, HINSTANCE instance) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, kCursorArrow);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
    wc.lpszClassName = kHelpClass;
    if (!RegisterClassExW(&wc)) return false;
    registered = true;
  }

  owner_ = owner;
  // Owned by the studio window: always above it, no button of its own on
  // the taskbar, closed with it. Not modal: the owner is never disabled.
  CreateWindowExW(WS_EX_CONTROLPARENT, kHelpClass, kHelpTitle,
                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, instance, this);
  if (!hwnd_) return false;
  dpi_ = window_dpi(hwnd_);

  auto child = [&](const wchar_t* cls, DWORD style, int id) {
    return CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
  };
  list_ = child(L"LISTBOX", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, kIdTopics);
  // The rich edit control lives in a library of its own. Without it the
  // page is shown as plain text.
  if (LoadLibraryW(L"Msftedit.dll"))
    text_ = child(MSFTEDIT_CLASS, WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, kIdPage);
  rich_ = text_ != nullptr;
  if (!text_) text_ = child(L"EDIT", WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, kIdPage);
  if (!list_ || !text_) return false;
  if (rich_) SendMessageW(text_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(GetSysColor(COLOR_WINDOW)));

  for (int i = 0; i < static_cast<int>(HelpTopic::Count); ++i) {
    const std::wstring title = widen_ascii(help_page(static_cast<HelpTopic>(i)).title);
    SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
  }
  rebuild_font();

  // Over the middle of the studio window, within the monitor's work area.
  int w = px(900), h = px(680);
  RECT anchor{};
  GetWindowRect(owner, &anchor);
  int x = anchor.left + (anchor.right - anchor.left - w) / 2;
  int y = anchor.top + (anchor.bottom - anchor.top - h) / 2;
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) {
    const RECT& wa = mi.rcWork;
    w = std::min(w, static_cast<int>(wa.right - wa.left));
    h = std::min(h, static_cast<int>(wa.bottom - wa.top));
    x = std::clamp(x, static_cast<int>(wa.left), static_cast<int>(wa.right) - w);
    y = std::clamp(y, static_cast<int>(wa.top), static_cast<int>(wa.bottom) - h);
  }
  SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  layout();
  return true;
}

void HelpWindow::rebuild_font() {
  HFONT old = font_;
  font_ = create_message_font(dpi_, &face_);
  if (face_.empty()) face_ = L"Segoe UI";
  SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  if (!rich_) SendMessageW(text_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  if (old) DeleteObject(old);
  line_h_ = font_line_height(hwnd_, font_);
  SendMessageW(list_, LB_SETITEMHEIGHT, 0, line_h_ + px(12));
}

void HelpWindow::layout() {
  if (!list_ || !text_) return;
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int topics = std::min(px(kTopicsWidthDip), static_cast<int>(rc.right) / 2);
  const int margin = px(10);
  // A child that changes size is drawn again as a whole. Windows would keep
  // the pixels it had, and the page's lines break elsewhere at a new width.
  const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;
  SetWindowPos(list_, nullptr, margin, margin, topics - margin, std::max(0, static_cast<int>(rc.bottom) - 2 * margin),
               flags);
  const int page_w = std::max(0, static_cast<int>(rc.right) - topics - margin);
  SetWindowPos(text_, nullptr, topics + margin, 0, page_w, rc.bottom, flags);
  // The text keeps a margin inside the page.
  RECT inner{px(18), px(14), std::max(px(18), page_w - px(18) - GetSystemMetrics(SM_CXVSCROLL)),
             std::max(px(14), static_cast<int>(rc.bottom) - px(8))};
  SendMessageW(text_, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&inner));
  RedrawWindow(text_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}

LRESULT HelpWindow::on_message(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      layout();
      return 0;

    case WM_GETMINMAXINFO: {
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lp);
      info->ptMinTrackSize.x = px(kMinWidthDip);
      info->ptMinTrackSize.y = px(kMinHeightDip);
      return 0;
    }

    case WM_DPICHANGED: {
      dpi_ = HIWORD(wp);
      rebuild_font();
      const RECT* suggested = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      layout();
      return 0;
    }

    case WM_COMMAND:
      if (LOWORD(wp) == kIdTopics && HIWORD(wp) == LBN_SELCHANGE) {
        const int i = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
        if (i >= 0 && i < static_cast<int>(HelpTopic::Count)) show_page(static_cast<HelpTopic>(i));
      } else if (LOWORD(wp) == IDCANCEL) {
        hide();
      }
      return 0;

    case WM_CTLCOLORLISTBOX: {
      // The list of topics sits on the window's own background.
      HDC dc = reinterpret_cast<HDC>(wp);
      SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
      SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }

    case WM_SETFOCUS:
      if (list_) SetFocus(list_);
      return 0;

    case WM_CLOSE:
      hide();  // kept for the next time, with its place and its page
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

void HelpWindow::show(HWND owner, HINSTANCE instance, HelpTopic topic) {
  if (!hwnd_ && !create(owner, instance)) {
    MessageBoxW(owner, L"The help window could not be opened.", kHelpTitle, MB_OK | MB_ICONWARNING);
    return;
  }
  SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(topic), 0);
  if (topic != shown_) show_page(topic);
  ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(hwnd_);
}

void HelpWindow::hide() {
  if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
  ShowWindow(hwnd_, SW_HIDE);
  if (owner_ && IsWindow(owner_)) SetForegroundWindow(owner_);
}

bool HelpWindow::handle_message(MSG& msg) {
  if (!hwnd_ || !IsWindowVisible(hwnd_) || !owns(msg.hwnd)) return false;
  // The page takes the keyboard for itself, so Escape is caught here.
  if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
    hide();
    return true;
  }
  return IsDialogMessageW(hwnd_, &msg) != FALSE;
}

// Writes a topic's page into the control: its title, then the pieces of its
// body, each with the look of its kind.
void HelpWindow::show_page(HelpTopic topic) {
  shown_ = topic;
  const HelpPage& page = help_page(topic);
  const std::vector<HelpBlock> blocks = parse_help(page.body);

  if (!rich_) {
    std::wstring plain = widen_ascii(page.title) + L"\r\n";
    for (const HelpBlock& b : blocks) {
      plain += b.kind == HelpBlock::Kind::Heading ? L"\r\n" : L"";
      plain += (b.kind == HelpBlock::Kind::Item ? L"  - " : L"") + widen_ascii(b.text) + L"\r\n";
      if (b.kind == HelpBlock::Kind::Paragraph) plain += L"\r\n";
    }
    SetWindowTextW(text_, plain.c_str());
    return;
  }

  auto characters = [&](Style style, bool typed) {
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_COLOR;
    cf.dwEffects = CFE_AUTOCOLOR | ((style == Style::Title || style == Style::Heading) ? CFE_BOLD : 0);
    cf.yHeight = style == Style::Title ? kTitleSize : (style == Style::Heading ? kHeadingSize : (typed ? kCodeSize : kBodySize));
    const std::wstring face = typed ? L"Consolas" : face_;
    std::wcsncpy(cf.szFaceName, face.c_str(), LF_FACESIZE - 1);
    return cf;
  };
  auto paragraph = [&](Style style) {
    PARAFORMAT2 pf{};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET | PFM_SPACEBEFORE | PFM_SPACEAFTER | PFM_LINESPACING;
    pf.wNumbering = style == Style::Item ? PFN_BULLET : 0;
    pf.dxStartIndent = style == Style::Item ? 160 : 0;
    pf.dxOffset = style == Style::Item ? 240 : 0;  // the lines after the first hang under the text
    pf.dySpaceBefore = style == Style::Heading ? 280 : 0;
    pf.dySpaceAfter = style == Style::Title ? 200 : (style == Style::Heading ? 100 : (style == Style::Item ? 80 : 150));
    pf.bLineSpacingRule = 5;  // in twentieths of a line
    pf.dyLineSpacing = 23;
    return pf;
  };
  bool first = true;
  auto add = [&](const std::wstring& text, Style style) {
    if (!first) {
      move_to_end(text_);
      SendMessageW(text_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L"\r"));
    }
    first = false;
    move_to_end(text_);
    PARAFORMAT2 pf = paragraph(style);
    SendMessageW(text_, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
    // Text between backticks is shown as typed; the backticks are left out.
    bool typed = false;
    size_t at = 0;
    while (at <= text.size()) {
      size_t end = text.find(L'`', at);
      if (end == std::wstring::npos) end = text.size();
      if (end > at) {
        const std::wstring run = text.substr(at, end - at);
        CHARFORMAT2W cf = characters(style, typed);
        move_to_end(text_);
        SendMessageW(text_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
        SendMessageW(text_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(run.c_str()));
      }
      typed = !typed;
      at = end + 1;
    }
  };

  SendMessageW(text_, WM_SETREDRAW, FALSE, 0);
  SendMessageW(text_, EM_SETREADONLY, FALSE, 0);
  SetWindowTextW(text_, L"");
  add(widen_ascii(page.title), Style::Title);
  for (const HelpBlock& b : blocks)
    add(widen_ascii(b.text), b.kind == HelpBlock::Kind::Heading ? Style::Heading
                                                               : (b.kind == HelpBlock::Kind::Item ? Style::Item : Style::Body));
  SendMessageW(text_, EM_SETREADONLY, TRUE, 0);
  SendMessageW(text_, EM_SETSEL, 0, 0);
  SendMessageW(text_, WM_VSCROLL, SB_TOP, 0);
  SendMessageW(text_, WM_SETREDRAW, TRUE, 0);
  RedrawWindow(text_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

}  // namespace bs
