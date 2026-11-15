// The help window of the studio window (spec 2.10): the pages of
// help_text.hpp, a list of topics beside the page that is open.
//
// It is a window of its own that the studio window owns: it stays above the
// studio window and goes away with it, but it is not modal. Both windows
// take input, so a page can stay open beside the work it explains.
#pragma once

#include <windows.h>

#include <string>

#include "app/help_text.hpp"

namespace bs {

class HelpWindow {
 public:
  HelpWindow() = default;
  HelpWindow(const HelpWindow&) = delete;
  HelpWindow& operator=(const HelpWindow&) = delete;
  ~HelpWindow();

  // Shows a topic, creating the window the first time. `owner` is the
  // studio window.
  void show(HWND owner, HINSTANCE instance, HelpTopic topic);
  void hide();
  // Whether a window is the help window or one of its controls.
  bool owns(HWND hwnd) const { return hwnd_ && (hwnd == hwnd_ || IsChild(hwnd_, hwnd)); }

  // For the message loop: takes the messages that are the help window's
  // (keyboard navigation in it, Esc to close it). True if the message was
  // handled here.
  bool handle_message(MSG& msg);

 private:
  static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT on_message(UINT msg, WPARAM wp, LPARAM lp);
  bool create(HWND owner, HINSTANCE instance);
  void rebuild_font();
  void layout();
  void show_page(HelpTopic topic);
  int px(int dip) const { return MulDiv(dip, dpi_, 96); }

  HWND owner_ = nullptr, hwnd_ = nullptr;
  HWND list_ = nullptr;  // the topics
  HWND text_ = nullptr;  // the page, a read-only rich edit control
  bool rich_ = false;    // false: the rich edit library is missing and the page is plain text
  HFONT font_ = nullptr;
  std::wstring face_;    // the face of the system's message font, for the page's text
  int dpi_ = 96;
  int line_h_ = 16;
  HelpTopic shown_ = HelpTopic::Count;
};

}  // namespace bs
